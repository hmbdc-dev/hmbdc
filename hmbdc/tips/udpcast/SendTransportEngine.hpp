#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/udpcast/Transport.hpp"
#include "hmbdc/tips/udpcast/Messages.hpp"
#include "hmbdc/tips/udpcast/DefaultUserConfig.hpp"
#include "hmbdc/app/Client.hpp"
#include "hmbdc/comm/inet/Endpoint.hpp"
#include "hmbdc/pattern/MonoLockFreeBuffer.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/numeric/BitMath.hpp"

#include <regex>
#include <memory>
#include <iostream>
#include <mutex>

namespace hmbdc { namespace tips { namespace udpcast {

namespace sendtransportengine_detail {
HMBDC_CLASS_HAS_DECLARE(hmbdc_net_queued_ts);
using Buffer = hmbdc::pattern::MonoLockFreeBuffer;

struct SendTransport 
: Transport {
    SendTransport(Config, size_t); 
    ~SendTransport();

    size_t bufferedMessageCount() const {
        return buffer_.remainingSize();
    }
    
    size_t subscribingPartyDetectedCount(uint16_t tag) const {
        return 0; //not supported
    }

    template <app::MessageTupleC Messages, typename Node>
    void advertiseFor(Node const& node, uint16_t mod, uint16_t res) {
        //only applies to connection oriented transport
    }

    template <app::MessageC Message>
    void queue(Message&& msg) {
        auto n = 1;
        auto it = buffer_.claim(n);
        queue(it, std::forward<Message>(msg));
        buffer_.commit(it, n);
    }

    template <typename Message>
    bool tryQueue(Message&& msg) {
        auto n = 1;
        auto it = buffer_.tryClaim(n);
        if (it) {
            queue(it, std::forward<Message>(msg));
            buffer_.commit(it, n);
            return true;
        }
        return false;
    }

    void runOnce(bool alwaysPoll = false) HMBDC_RESTRICT {
        resumeSend();
        if (hmbdc_unlikely(alwaysPoll || !isFdReady())) {
            app::utils::EpollTask::instance().poll();
        }
    }

    void stop();

    /// not threadsafe - call it in runOnce thread
    void addDest(comm::inet::Endpoint const& dest) {
        if (std::find(udpcastDests_.begin(), udpcastDests_.end(), dest) == udpcastDests_.end()) {
            udpcastDests_.push_back(dest);
        }
    }

    /// not threadsafe - call it in runOnce thread
    void eraseDest(comm::inet::Endpoint const& dest) {
        std::remove(udpcastDests_.begin(), udpcastDests_.end(), dest);
    }

private:
    size_t maxMessageSize_;
    typename Buffer::iterator begin_, it_, end_;
    Buffer buffer_;
    hmbdc::time::Rater rater_;
    size_t maxSendBatch_;

    iovec* toSendMsgs_;
    size_t toSendMsgsHead_;
    size_t toSendMsgsTail_;
    mmsghdr* toSendPkts_;
    size_t toSendPktsHead_;
    size_t toSendPktsTail_;
    std::vector<comm::inet::Endpoint> udpcastDests_;

    uint16_t
    outBufferSizePower2();

    template<typename M, typename ... Messages>
    void queue(typename Buffer::iterator it, M&& m, Messages&&... msgs) {
        using Message = typename std::decay<M>::type;
        static_assert(std::is_trivially_destructible<Message>::value, "cannot send message with dtor");
        auto s = *it;
        char* addr = static_cast<char*>(s);
        auto h = reinterpret_cast<TransportMessageHeader*>(addr);
        if (hmbdc_likely(sizeof(Message) <= maxMessageSize_)) {
            new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<Message>(std::forward<M>(m));
            h->messagePayloadLen() = sizeof(app::MessageWrap<Message>);
        } else {
            HMBDC_THROW(std::out_of_range
                , "maxMessageSize too small to hold a message when constructing SendTransportEngine");
        }
        if constexpr (has_hmbdc_net_queued_ts<Message>::value) {
            h->template wrapped<Message>().hmbdc_net_queued_ts = hmbdc::time::SysTime::now();
        }
        queue(++it, std::forward<Messages>(msgs)...);
    }

    void queue(typename Buffer::iterator it) {} 
    void resumeSend();
};

struct SendTransportEngine
: SendTransport
, hmbdc::app::Client<SendTransportEngine> {  
    using SendTransport::SendTransport;
    using SendTransport::hmbdcName;
    using SendTransport::schedSpec;

    void rotate() {
        SendTransportEngine::invokedCb(0);
    }

    /*virtual*/ 
    void invokedCb(size_t) HMBDC_RESTRICT override  {
        runOnce();
    }
    /*virtual*/ bool droppedCb() override {
        stop();
        return true;
    };

    using Transport::hmbdcName;
};

} //sendtransportengine_detail
using SendTransport = sendtransportengine_detail::SendTransport;
using SendTransportEngine = sendtransportengine_detail::SendTransportEngine;
}}}



namespace hmbdc { namespace tips { namespace udpcast {

namespace sendtransportengine_detail {

inline
SendTransport::
SendTransport(Config cfg
    , size_t maxMessageSize) 
: Transport((cfg.setAdditionalFallbackConfig(Config(DefaultUserConfig))
    , cfg.resetSection("tx", false)))
, maxMessageSize_(maxMessageSize)
, buffer_(maxMessageSize + sizeof(TransportMessageHeader) + sizeof(app::MessageHead), outBufferSizePower2())
, rater_(hmbdc::time::Duration::seconds(1u)
    , config_.getExt<size_t>("sendBytesPerSec")
    , config_.getExt<size_t>("sendBytesBurst")
    , config_.getExt<size_t>("sendBytesBurst") != 0ul) //no rate control by default
, maxSendBatch_(config_.getExt<size_t>("maxSendBatch")) 
, toSendMsgs_(new iovec[maxSendBatch_])
, toSendMsgsHead_(0)
, toSendMsgsTail_(0)
, toSendPkts_(new mmsghdr[maxSendBatch_])
, toSendPktsHead_(0)
, toSendPktsTail_(0) {
    char loopch = config_.getExt<bool>("loopback")?1:0;
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, sizeof(loopch)) < 0) {
        HMBDC_THROW(std::runtime_error, "failed to set loopback=" << config_.getExt<bool>("loopback"));
    }

    auto sz = config_.getExt<int>("udpSendBufferBytes");
    if (sz) {
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz)) < 0) {
            HMBDC_LOG_C("failed to set send buffer size=", sz);
        }
    }

    auto ttl = config_.getExt<int>("ttl");
    if (ttl > 0 && setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        HMBDC_THROW(std::runtime_error, "failed to set set ttl=" << ttl);
    }

    hmbdc::app::utils::EpollTask::instance().add(
        hmbdc::app::utils::EpollTask::EPOLLOUT
            | hmbdc::app::utils::EpollTask::EPOLLET, *this);
    auto minHead = sizeof(app::MessageHead) + sizeof(TransportMessageHeader);
    if (maxMessageSize_ +  minHead > mtu_) {
        HMBDC_THROW(std::out_of_range, "maxMessageSize needs <= " << mtu_ - minHead);
    }
    cfg(udpcastDests_, "udpcastDests");
    if (udpcastDests_.size() == 0) {
        HMBDC_THROW(std::out_of_range, "empty udpcastDests");
    }
    memset(toSendMsgs_, 0, sizeof(iovec) * maxSendBatch_);
    memset(toSendPkts_, 0, sizeof(mmsghdr) * maxSendBatch_);
    std::for_each(toSendPkts_, toSendPkts_ + maxSendBatch_
        , [this](mmsghdr& pkt) {
            pkt.msg_hdr.msg_name = &udpcastDests_.begin()->v;
            pkt.msg_hdr.msg_namelen = sizeof(udpcastDests_.begin()->v);
    });
}

inline
SendTransport::
~SendTransport() {
    delete []toSendPkts_;
    delete []toSendMsgs_;
}

inline
void 
SendTransport::
stop() {
    buffer_.reset();
}

inline
uint16_t
SendTransport::
outBufferSizePower2() {
    auto res = config_.getExt<uint16_t>("outBufferSizePower2");
    if (res) {
        return res;
    }
    res =hmbdc::numeric::log2Upper(8ul * 1024ul / (8ul + maxMessageSize_));
    HMBDC_LOG_N("auto set --outBufferSizePower2=", res);
    return res;
}

inline
void 
SendTransport::
resumeSend() HMBDC_RESTRICT {
    bool rateOk = true;
    while (rateOk) {
        if (it_ == end_) {
            buffer_.wasteAfterPeek(begin_, end_ - begin_);
            buffer_.peek(it_, end_, maxSendBatch_);
            begin_ = it_;
        }
        //make a packet
        size_t packetBytes = 0;
        while (it_ != end_ && toSendMsgsTail_ < maxSendBatch_) {
            void* ptr = *it_;
            auto item = static_cast<TransportMessageHeader*>(ptr);
            rateOk = rater_.check(item->wireSize());
            if (hmbdc_unlikely(!rateOk)) {
                break;
            }
            packetBytes += item->wireSize();
            if (hmbdc_unlikely(packetBytes > mtu_)) {
                toSendPkts_[toSendPktsTail_].msg_hdr.msg_iov = toSendMsgs_ + toSendMsgsHead_;
                toSendPkts_[toSendPktsTail_++].msg_hdr.msg_iovlen 
                    = toSendMsgsTail_ - toSendMsgsHead_;
                toSendMsgsHead_ = toSendMsgsTail_;
                packetBytes = item->wireSize();
            }

            toSendMsgs_[toSendMsgsTail_].iov_base = ptr;
            toSendMsgs_[toSendMsgsTail_++].iov_len = item->wireSize();
            rater_.commit();
            it_++;
        }
        if (packetBytes) {
            //wrap up current packet
            toSendPkts_[toSendPktsTail_].msg_hdr.msg_iov = toSendMsgs_ + toSendMsgsHead_;
            toSendPkts_[toSendPktsTail_++].msg_hdr.msg_iovlen 
                = toSendMsgsTail_ - toSendMsgsHead_;
            toSendMsgsHead_ = toSendMsgsTail_;
        }

        auto toSendPktsCount = toSendPktsTail_ - toSendPktsHead_;
        if (toSendPktsCount && isFdReady()) {
            for (auto it = udpcastDests_.begin(); it != udpcastDests_.end(); ++it) {
                auto& addr = it->v;
                if (hmbdc_unlikely(toSendPkts_[toSendPktsHead_].msg_hdr.msg_name != &addr)) {
                    std::for_each(toSendPkts_ + toSendPktsHead_, toSendPkts_ + toSendPktsHead_ + toSendPktsCount
                        , [&addr](mmsghdr& pkt) {
                            pkt.msg_hdr.msg_name = &addr;
                        }
                    );
                }
                auto ll = sendmmsg(fd, toSendPkts_ + toSendPktsHead_, toSendPktsCount, MSG_NOSIGNAL|MSG_DONTWAIT);
                if (ll < 0 && errno != EAGAIN) {
                    HMBDC_LOG_C("unreachable dest=", *it, " erased, errno=", errno);
                    udpcastDests_.erase(it++);
                }
            }
            // no retry for unreliable transport to avoid buffer full
            toSendPktsHead_ = toSendPktsTail_ = 0;
            toSendMsgsHead_ = toSendMsgsTail_ = 0;
        } else {
            //nothing to do now
            return;
        }
    }
}
} //sendtransportengine_detail
}}}
