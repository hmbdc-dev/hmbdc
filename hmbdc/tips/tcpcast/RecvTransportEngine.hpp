#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/tcpcast/Transport.hpp"
#include "hmbdc/tips/tcpcast/RecvSession.hpp"
#include "hmbdc/tips/tcpcast/Messages.hpp"
#include "hmbdc/tips/tcpcast/DefaultUserConfig.hpp"
#include "hmbdc/tips/udpcast/RecvTransportEngine.hpp"
#include "hmbdc/app/Logger.hpp"

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/unordered_map.hpp>
#include <mutex>
#include <regex>

#include <memory>

namespace hmbdc { namespace tips { namespace tcpcast {

/**
 * @brief interface to power a tcpcast transport receiving functions
 */
struct RecvTransport 
: Transport {
    using Transport::Transport;
    virtual ~RecvTransport(){}
};

namespace recvtransportengine_detail {
template <typename OutputBuffer, typename AttachmentAllocator>
struct RecvTransportEngine 
: RecvTransport
, hmbdc::time::TimerManager
, hmbdc::time::ReoccuringTimer
, hmbdc::app::Client<RecvTransportEngine<OutputBuffer, AttachmentAllocator>> {
    using MD = hmbdc::app::MessageDispacher<RecvTransportEngine, std::tuple<TypeTagSource>>;

/**
 * @brief ctor
 * @details io_service could be passed in by user, in this case NO more than two threads should
 * power this io_service instance since that would violate the thread garantee of Client, which
 * is no callbacks are called in parallel
 * 
 * @param cfg specify the details of the tcpcast transport
 * @param outputBuffer holding the results
 */
    RecvTransportEngine(hmbdc::app::Config cfg
        , OutputBuffer& outputBuffer)
    : RecvTransport(((cfg.setAdditionalFallbackConfig(hmbdc::app::Config(DefaultUserConfig))
        , cfg.resetSection("rx", false))))
    , hmbdc::time::ReoccuringTimer(hmbdc::time::Duration::seconds(cfg.getExt<size_t>("heartbeatPeriodSeconds")))
    , config_(cfg)
    , mcConfig_(config_) 
    , buffer_(std::max({sizeof(app::MessageWrap<TypeTagSource>)})
    , config_.getExt<uint16_t>("cmdBufferSizePower2"))
    , outputBuffer_(outputBuffer)
    , maxItemSize_(outputBuffer.maxItemSize())
    , myIp_(inet_addr(hmbdc::comm::inet::getLocalIpMatchMask(
        config_.getExt<std::string>("ifaceAddr")).c_str())) {
        if (!config_.getExt<bool>("allowRecvWithinProcess")) {
            myPid_ = getpid();
        }
        if (outputBuffer.maxItemSize() < sizeof(SessionStarted) 
            || outputBuffer.maxItemSize() < sizeof(SessionDropped))  {
            HMBDC_THROW(std::out_of_range
                , "buffer is too small for notification: SessionStarted and SessionDropped");
        }
        mcConfig_.put("outBufferSizePower2", 3u);
        mcReceiver_.emplace(mcConfig_, buffer_);
        mcReceiver_->template subscribe<TypeTagSource>();

        setCallback(
            [this](hmbdc::time::TimerManager& tm, hmbdc::time::SysTime const& now) {
                for (auto& s : recvSessions_) {
                    s.second->heartbeat();
                }
            }
        );

        schedule(hmbdc::time::SysTime::now(), *this);
    }

    void rotate() {
        this->checkTimers(hmbdc::time::SysTime::now());
        RecvTransportEngine::invokedCb(0);
    }

    using Transport::hmbdcName;
    using Transport::schedSpec;

/**
 * @brief should not htipsen ever unless an exception thrown
 * 
 * @param e exception thown
 */
    /*virtual*/ 
    void stoppedCb(std::exception const& e) override {
        HMBDC_LOG_C(e.what());
    };

/**
 * @brief power the io_service and other things
 * 
 */
    /*virtual*/ 
    void invokedCb(size_t) HMBDC_RESTRICT override  {
        for (auto it = recvSessions_.begin(); it != recvSessions_.end();) {
            if (hmbdc_unlikely(!(*it).second->runOnce())) {
                (*it).second->stop();
                recvSessions_.erase(it++);
            } else {
                it++;
            }
        }
        pattern::MonoLockFreeBuffer::iterator begin, end;
        auto n = buffer_.peek(begin, end);
        auto it = begin;
        while (it != end) {
            MD()(*this, *static_cast<app::MessageHead*>(*it++));
        }
        buffer_.wasteAfterPeek(begin, n);

        mcReceiver_->runOnce(true);
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
        subscriptions_.markSubsFor<Messages>(node, mod, res);
    }

/**
 * @brief only used by MD
 */
    void handleMessageCb(TypeTagSource const& t) {
        auto ip = inet_addr(t.ip);
        if (ip == myIp_ 
            && !t.loopback) {
            return;
        }

        if (t.srcPid == myPid_ && ip == myIp_) return;
        auto key = std::make_pair(ip, t.port);
        if (recvSessions_.find(key) == recvSessions_.end()) {
            for (auto i = 0u; i < t.typeTagCountContained; ++i) {
                if (subscriptions_.check(t.typeTags[i])) {
                    try {
                        auto sess = new Session(
                            config_
                            , subscriptions_
                            , outputBuffer_
                        );
                        sess->start(ip, t.port);
                        recvSessions_[key] = typename Session::ptr(sess);
                    } catch (std::exception const& e) {
                        HMBDC_LOG_C(e.what());
                    }
                    break;
                }
            }
        } //else ignore
    }
    
private:
    using Session = RecvSession<OutputBuffer, AttachmentAllocator>;

    hmbdc::app::Config config_, mcConfig_;
    pattern::MonoLockFreeBuffer buffer_;
    OutputBuffer& outputBuffer_;
    size_t maxItemSize_;
    std::optional<udpcast::RecvTransportImpl<decltype(buffer_)>> mcReceiver_;

    TypeTagSet subscriptions_;
    boost::unordered_map<std::pair<uint64_t, uint16_t>
        , typename Session::ptr> recvSessions_;
    uint32_t myIp_;
    uint32_t myPid_ = 0; 
};

} //recvtransportengine_detail
template <typename OutputBuffer, typename AttachmentAllocator>
using RecvTransportEngine = recvtransportengine_detail::RecvTransportEngine<OutputBuffer, AttachmentAllocator>;
}}}
