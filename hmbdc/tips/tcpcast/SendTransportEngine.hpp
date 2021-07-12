#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/tips/tcpcast/SendServer.hpp"
#include "hmbdc/tips/tcpcast/Transport.hpp"
#include "hmbdc/tips/tcpcast/Messages.hpp"
#include "hmbdc/tips/udpcast/SendTransportEngine.hpp"
#include "hmbdc/tips/tcpcast/DefaultUserConfig.hpp"
#include "hmbdc//MetaUtils.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/numeric/BitMath.hpp"

#include <boost/circular_buffer.hpp>
#include <memory>
#include <tuple>
#include <regex>
#include <type_traits>
#include <mutex>

namespace hmbdc { namespace tips { namespace tcpcast {

namespace send_detail {
HMBDC_CLASS_HAS_DECLARE(hmbdc_net_queued_ts);
using pattern::MonoLockFreeBuffer;
/**
 * @brief capture the transportation mechanism
 * 
 */
struct SendTransport 
: Transport {
    /**
     * @brief ctor
     * @param cfg jason specifing the transport - see example, perf-tcpcast.cpp
     * @param maxMessageSize max messafe size in bytes to be sent
     */
    SendTransport(hmbdc::app::Config, size_t);

    size_t subscribingPartyDetectedCount(uint16_t tag) const {
        return outboundSubscriptions_.check(tag);
    }

    size_t bufferedMessageCount() const {
        return buffer_.remainingSize();
    }

    template <app::MessageC Message>
    void queue(Message&& msg) {
        if (!minRecvToStart_ && !outboundSubscriptions_.check(msg.getTypeTag())) {
            using M = typename std::decay<Message>::type;
            if constexpr (std::is_base_of<app::hasMemoryAttachment, M>::value) {
                msg.release();
            }
            return;
        }
        auto n = 1;
        auto it = buffer_.claim(n);
        queue(it, std::forward<Message>(msg));
        buffer_.commit(it, n);
    }

    template <app::MessageC Message>
    bool tryQueue(Message&& msg) {
        if (!minRecvToStart_ && !outboundSubscriptions_.check(msg.getTypeTag())) {
            using M = typename std::decay<Message>::type;
            if constexpr (std::is_base_of<app::hasMemoryAttachment, M>::value) {
                msg.release();
            }
            return true; //don't try again
        }
        auto n = 1;
        auto it = buffer_.tryClaim(n);
        if (it) {
            queue(it, std::forward<Message>(msg));
            buffer_.commit(it, n);
            return true;
        }
        return false;
    }

    template <typename Message, typename ... Args>
    void queueInPlace(Args&&... args) {
        static_assert(!std::is_base_of<app::JustBytes, Message>::value
            , "use queueJustBytes instead");
        static_assert(std::is_trivially_destructible<Message>::value
            , "cannot send message with dtor");
        static_assert(!std::is_base_of<app::hasMemoryAttachment, Message>::value 
            || app::is_hasMemoryAttachment_first_base_of<Message>::value
            , "hasMemoryAttachment has to the first base for Message");
        auto s = buffer_.claim();
        char* addr = static_cast<char*>(*s);
        auto h = new (addr) TransportMessageHeader;
        h->messagePayloadLen = sizeof(app::MessageWrap<Message>);
        if (hmbdc_likely(sizeof(Message) <= maxMessageSize_)) {
            auto wrap = new (addr + sizeof(TransportMessageHeader)) 
                app::MessageWrap<Message>(std::forward<Args>(args)...);
            h->flag = wrap->scratchpad().desc.flag;
        } else {
            HMBDC_THROW(std::out_of_range
                , "maxMessageSize too small to hold a message");
        }
        buffer_.commit(s);
    }

    void queueJustBytes(uint16_t tag, void const* bytes, size_t len
        , app::hasMemoryAttachment* att) {
        if (!minRecvToStart_ && !outboundSubscriptions_.check(tag)) {
            if (att) {
                att->release();
            }
            return;
        }
        auto s = buffer_.claim();
        char* addr = static_cast<char*>(*s);
        auto h = new (addr) TransportMessageHeader;
        h->messagePayloadLen = len + sizeof(app::MessageHead);
        if (hmbdc_likely(len <= maxMessageSize_)) {
            auto wrap = new (addr + sizeof(TransportMessageHeader)) 
               app:: MessageWrap<app::JustBytes>(tag, bytes, len, att);
            h->flag = wrap->scratchpad().desc.flag;
        } else {
            HMBDC_THROW(std::out_of_range
                , "maxMessageSize too small to hold a message");
        }
        buffer_.commit(s);
    }

    void stop();

protected:
    size_t maxMessageSize_;
    size_t minRecvToStart_;
    MonoLockFreeBuffer buffer_;

    std::unique_ptr<udpcast::SendTransport> mcSendTransport_;
    hmbdc::time::Rater rater_;
    hmbdc::app::Config mcConfig_;
    TypeTagSet outboundSubscriptions_;

private:
    template<typename M, typename ... Messages>
    void queue(MonoLockFreeBuffer::iterator it, M&& m, Messages&&... msgs) {
        using Message = typename std::decay<M>::type;
        static_assert(std::is_trivially_destructible<Message>::value, "cannot send message with dtor");
        static_assert(!std::is_base_of<app::hasMemoryAttachment, Message>::value 
            || app::is_hasMemoryAttachment_first_base_of<Message>::value
            , "hasMemoryAttachment has to the first base for Message");

        auto s = *it;
        char* addr = static_cast<char*>(s);
        auto h = new (addr) TransportMessageHeader;
        h->messagePayloadLen = sizeof(app::MessageWrap<Message>);
        if (hmbdc_likely(sizeof(Message) <= maxMessageSize_)) {
            auto wrap = new (addr + sizeof(TransportMessageHeader)) 
                app::MessageWrap<Message>(std::forward<M>(m));
            h->flag = wrap->scratchpad().desc.flag;
        } else {
            HMBDC_THROW(std::out_of_range, "maxMessageSize too small to hold a message when constructing SendTransportEngine");
        }
        if constexpr (has_hmbdc_net_queued_ts<Message>::value) {
            h->template wrapped<Message>().hmbdc_net_queued_ts = hmbdc::time::SysTime::now();
        }
        queue(++it, std::forward<Messages>(msgs)...);
    }

    void queue(MonoLockFreeBuffer::iterator it) {}
};

struct SendTransportEngine
: SendTransport
, hmbdc::time::TimerManager
, hmbdc::time::ReoccuringTimer 
, hmbdc::app::Client<SendTransportEngine> {  
    SendTransportEngine(hmbdc::app::Config const&, size_t);
    using SendTransport::hmbdcName;
    using SendTransport::schedSpec;

    template <app::MessageTupleC Messages, typename Node>
    void advertiseFor(Node const& node, uint16_t mod, uint16_t res) {
        std::scoped_lock<std::mutex> g(advertisedTypeTags_.lock);
        advertisedTypeTags_.markPubsFor<Messages>(node, mod, res);
        server_->advertisingMessages(advertisedTypeTags_);
    }

    void rotate() {
        this->checkTimers(hmbdc::time::SysTime::now());
        SendTransportEngine::invokedCb(0);
    }

    /*virtual*/ 
    void invokedCb(size_t) HMBDC_RESTRICT override  {
        runOnce();
        mcSendTransport_->runOnce(true);
    }

    /*virtual*/ 
    bool droppedCb() override {
        stop();
        stopped_ = true;
        return true;
    };

    /**
     * @brief check how many recipient sessions are still active
     * @return numeric_limits<size_t>::max() if the sending hasn't started due to 
     * minRecvToStart has not been met yet
     */
    size_t sessionsRemainingActive() const {
        return server_->readySessionCount();
    }

private:
    void cullSlow() {
        auto seq = buffer_.readSeq();
        if (seq == lastSeq_ && buffer_.isFull()) {
            server_->killSlowestSession();
        }
        lastSeq_ = seq;
    }

    void runOnce();

    size_t mtu_;
    size_t maxSendBatch_;
    bool waitForSlowReceivers_;
    MonoLockFreeBuffer::iterator begin_;
    ToSend toSend_;
    TypeTagSetST advertisedTypeTags_;

    std::optional<SendServer> server_;
    size_t lastSeq_;
    bool stopped_;
};
} //send_detail

using SendTransport = send_detail::SendTransport;
using SendTransportEngine = send_detail::SendTransportEngine;
}}}

namespace hmbdc { namespace tips { namespace tcpcast {

namespace send_detail {
using pattern::MonoLockFreeBuffer;

inline
SendTransport::
SendTransport(hmbdc::app::Config  cfg
        , size_t maxMessageSize) 
: Transport((cfg.setAdditionalFallbackConfig(hmbdc::app::Config(DefaultUserConfig))
    , cfg.resetSection("tx", false)))
, maxMessageSize_(maxMessageSize)
, minRecvToStart_(config_.getExt<size_t>("minRecvToStart"))
, buffer_(maxMessageSize + sizeof(TransportMessageHeader) + sizeof(app::MessageHead)
    , config_.getExt<uint16_t>("outBufferSizePower2")
        ?config_.getExt<uint16_t>("outBufferSizePower2")
        :hmbdc::numeric::log2Upper(128ul * 1024ul / maxMessageSize)
) 
, rater_(hmbdc::time::Duration::seconds(1u)
    , config_.getExt<size_t>("sendBytesPerSec")
    , config_.getExt<size_t>("sendBytesBurst")
    , config_.getExt<size_t>("sendBytesBurst") != 0ul //no rate control by default
) 
, mcConfig_(config_) { 
    mcConfig_.put("loopback", true); //always allow advertising to loopback, so other process
                                     //on the same machine get them
    mcConfig_.put("outBufferSizePower2", 3u);
    
    mcSendTransport_.reset(new udpcast::SendTransport(
        mcConfig_, sizeof(app::MessageWrap<TypeTagSource>)));
}

inline
void 
SendTransport::
stop() {
    buffer_.reset();
};

inline
SendTransportEngine::
SendTransportEngine(hmbdc::app::Config const& cfg
    , size_t maxMessageSize)
: SendTransport(cfg, maxMessageSize)
, hmbdc::time::ReoccuringTimer(
    hmbdc::time::Duration::seconds(config_.getExt<size_t>("typeTagAdvertisePeriodSeconds")))
, mtu_(config_.getExt<size_t>("mtu") - 20 - 20) //20 bytes ip header and 20 bytes tcp header
, maxSendBatch_(config_.getExt<size_t>("maxSendBatch"))
, waitForSlowReceivers_(config_.getExt<bool>("waitForSlowReceivers"))
, lastSeq_(0)
, stopped_(false) {
    toSend_.reserve(maxSendBatch_ * 2);
    // inititialize begin_ to first item
    MonoLockFreeBuffer::iterator end;
    buffer_.peek(begin_, end, 0);
    
    server_.emplace(config_, buffer_.capacity(), outboundSubscriptions_);
    setCallback(
        [this](hmbdc::time::TimerManager& tm, hmbdc::time::SysTime const& now) {
            std::unique_lock<std::mutex> g(advertisedTypeTags_.lock, std::try_to_lock);
            if (!g.owns_lock()) return; 
            for (auto& ad : server_->advertisingMessages()) {
                mcSendTransport_->queue(ad);
            }
            if (!waitForSlowReceivers_) cullSlow();
        }
    );

    schedule(hmbdc::time::SysTime::now(), *this);
}

inline
void 
SendTransportEngine::
runOnce() HMBDC_RESTRICT {
    MonoLockFreeBuffer::iterator begin, end;
    //only START to send when enough sessions are ready to receive or
    //buffer is full
    if (hmbdc_likely(!minRecvToStart_ 
        || server_->readySessionCount() >= minRecvToStart_)) {
        minRecvToStart_ = 0; //now started no going back
        buffer_.peek(begin, end, maxSendBatch_);
    } else {
        buffer_.peek(begin, end, 0);
    }

    auto it = begin_;
    uint16_t currentTypeTag = 0;
    size_t toSendByteSize = 0;

    while (it != end) {
        void* ptr = *it;
        auto item = static_cast<TransportMessageHeader*>(ptr);

        if (hmbdc_unlikely(!rater_.check(item->wireSize()))) break;
        if (hmbdc_unlikely(!item->flag 
            && toSendByteSize + item->wireSize() > mtu_)) break;
        it++;
        if (hmbdc_unlikely(!currentTypeTag)) {
            currentTypeTag = item->typeTag();
            toSendByteSize = item->wireSize();
            toSend_.push_back(iovec{ptr, item->wireSize()});
        } else if (item->typeTag() == currentTypeTag) {          
            toSendByteSize += item->wireSize();
            toSend_.push_back(iovec{ptr, item->wireSize()});
        } else {
            server_->queue(currentTypeTag
                , move(toSend_)
                , toSendByteSize
                , it);
            toSend_.clear();

            currentTypeTag = item->typeTag();
            toSendByteSize = item->wireSize();
            toSend_.push_back(iovec{ptr, item->wireSize()});
        }

        if (hmbdc_unlikely(item->flag == app::hasMemoryAttachment::flag)) {
            auto& a = item->wrapped<app::hasMemoryAttachment>();
            toSend_.push_back(iovec{a.attachment, a.len});
            toSendByteSize += a.len;
        }
        rater_.commit();
    }

    if (toSend_.size()) {
        server_->queue(currentTypeTag
            , move(toSend_)
            , toSendByteSize
            , it);
        toSend_.clear();
    }
    begin_ = it;
    //use it here which is the maximum
    auto newStart = server_->runOnce(begin, it);
    if (begin != end) { //only need to do this when there r things read from buffer_
        for (auto it = begin; it != newStart; ++it) {
            void* ptr = *it;
            auto item = static_cast<TransportMessageHeader*>(ptr);
            if (hmbdc_unlikely(item->flag == app::hasMemoryAttachment::flag)) {
                auto& a = item->wrapped<app::hasMemoryAttachment>();
                a.release();
            }
        }
        buffer_.wasteAfterPeek(begin, newStart - begin, true);
    }
}
} //send_detail
}}}

