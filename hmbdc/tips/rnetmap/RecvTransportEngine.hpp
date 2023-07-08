#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rnetmap/Transport.hpp"
#include "hmbdc/tips/rnetmap/NmRecvTransport.hpp"
#include "hmbdc/tips/rnetmap/BackupRecvSession.hpp" 
#include "hmbdc/tips/rnetmap/DefaultUserConfig.hpp"
#include "hmbdc/tips/reliable/AttBufferAdaptor.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/comm/inet/Hash.hpp"

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/unordered_map.hpp>

#include <memory>
#include <regex>
#include <type_traits>
#include <mutex>

namespace hmbdc { namespace tips { namespace rnetmap {
struct RecvTransport : EngineTransport {
    RecvTransport(app::Config cfg)
    : EngineTransport((cfg.setAdditionalFallbackConfig(app::Config(DefaultUserConfig))
        , cfg.resetSection("rx", false)))
    {}
    virtual ~RecvTransport(){}

};

namespace recvtransportengine_detail {

template <typename OutputBuffer, typename AttachmentAllocator>
struct RecvTransportImpl 
: RecvTransport
, hmbdc::time::TimerManager {
    using MD = hmbdc::app::MessageDispacher<RecvTransportImpl, std::tuple<TypeTagBackupSource>>;
    friend struct NetContext; //only be created by NetContext
    RecvTransportImpl(hmbdc::app::Config const& cfgIn
        , OutputBuffer& outputBuffer)
	: RecvTransport(cfgIn)
    , cmdBuffer_(std::max({sizeof(app::MessageWrap<TypeTagBackupSource>)})
        , config_.getExt<uint16_t>("cmdBufferSizePower2"))
    , outputBuffer_(outputBuffer)
    , myBackupIp_(inet_addr(hmbdc::comm::inet::getLocalIpMatchMask(
        config_.getExt<std::string>("tcpIfaceAddr") == std::string("ifaceAddr")
            ?config_.getExt<std::string>("ifaceAddr"):config_.getExt<std::string>("tcpIfaceAddr")
    ).c_str()))
    , nmRecvTransport_(config_ 
        , cmdBuffer_   //to store cmds
        , subscriptions_
        , ep2SessionDict_)
    , loopback_(config_.getExt<bool>("loopback")) {
        subscriptions_.addAll<std::tuple<app::StartMemorySegTrain, app::MemorySeg>>();
        if (!config_.getExt<bool>("allowRecvWithinProcess")) {
            myPid_ = getpid();
        }
    }

    void runOnce() HMBDC_RESTRICT {
        hmbdc::app::utils::EpollTask::instance().poll();
        pattern::MonoLockFreeBuffer::iterator begin, end;
        auto n = cmdBuffer_.peek(begin, end);
        auto it = begin;
        while (it != end) {
            MD()(*this, *static_cast<app::MessageHead*>(*it++));
        }
        cmdBuffer_.wasteAfterPeek(begin, n);
        nmRecvTransport_.runOnce(); 
        for (auto it = recvSessions_.begin(); it != recvSessions_.end();) {
            if (hmbdc_unlikely(!it->second->runOnce())) {
                it->second->stop();
                cancel(*it->second);
                ep2SessionDict_.erase(it->second->sendFrom);
                recvSessions_.erase(it++);
            } else {
                it++;
            }
        }
    }

/**
 * @brief only used by MD
 */
    void handleMessageCb(TypeTagBackupSource const& t) {
        auto ip = inet_addr(t.ip);
        //unless setting loopback, don't EVER listen to myself (own process)
        if (ip == myBackupIp_ && (!loopback_)) {
            return;
        }
        if (t.srcPid == myPid_ && ip == myBackupIp_) return;
        auto key = std::make_pair(ip, t.port);
        if (recvSessions_.find(key) == recvSessions_.end()) {
            for (auto i = 0u; i < t.typeTagCountContained; ++i) {
                if (subscriptions_.check(t.typeTags[i])) {
                    try {
                        typename Session::ptr sess {
                            new Session(
                                config_
                                , t.sendFrom
                                , subscriptions_
                                , outputBuffer_
                                , t.recvReportDelay
                            )
                        };
                        sess->start(ip, t.port);
                        recvSessions_[key] = sess;
                        ep2SessionDict_[t.sendFrom] = sess;
                        schedule(hmbdc::time::SysTime::now(), *sess);
                    } catch (std::exception const& e) {
                        HMBDC_LOG_C(e.what());
                    }
                    break;
                }
            }
        } //else ignore
    }

    /**
     * @brief check how many other parties are sending to this engine
     * @return recipient session count are still active
     */
    size_t sessionsRemainingActive() const {
        return recvSessions_.size();
    }
    
    template <app::MessageTupleC Messages, typename CcNode>
    void subscribeFor(CcNode const& node, uint16_t mod, uint16_t res) {
        subscriptions_.markSubsFor<Messages>(node, mod, res, [](uint16_t){});
    }

private:
    using Session = BackupRecvSession<OutputBuffer, AttachmentAllocator>;

    pattern::MonoLockFreeBuffer cmdBuffer_;
    OutputBuffer& outputBuffer_;
    TypeTagSet subscriptions_;
    uint64_t myBackupIp_;
    using Ep2SessionDict //sender's ip address port -> 
        = boost::unordered_map<uint32_t, typename Session::ptr>;
    Ep2SessionDict ep2SessionDict_;
    boost::unordered_map<std::pair<uint64_t, uint16_t>
        , typename Session::ptr> recvSessions_;
    NmRecvTransport<OutputBuffer, Ep2SessionDict> nmRecvTransport_;
    bool loopback_;
    uint32_t myPid_ = 0;
};

template <typename OutputBuffer, typename AttachmentAllocator>
struct RecvTransportEngine 
: RecvTransportImpl<OutputBuffer, AttachmentAllocator>
, hmbdc::app::Client<RecvTransportEngine<OutputBuffer, AttachmentAllocator>> {
    using Impl = RecvTransportImpl<OutputBuffer, AttachmentAllocator>;
    using Impl::Impl;
    
    void rotate() {
        this->checkTimers(hmbdc::time::SysTime::now());
        RecvTransportEngine::invokedCb(0);
    }

    /*virtual*/ 
    void invokedCb(size_t) HMBDC_RESTRICT override  {
        Impl::runOnce();
    }
    
    /*virtual*/ 
    void stoppedCb(std::exception const& e) override {
        HMBDC_LOG_C(e.what());
    };

    using EngineTransport::hmbdcName;

    std::tuple<char const*, int> schedSpec() const {
        return std::make_tuple(this->schedPolicy_.c_str(), this->schedPriority_);
    }
};
} //recvtransportengine_detail

template <typename OutputBuffer, typename AttachmentAllocator>
using RecvTransportEngine = recvtransportengine_detail::RecvTransportEngine<
    OutputBuffer, AttachmentAllocator>;
}}}
