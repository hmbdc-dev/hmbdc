#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/Client.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/tips/rmcast/Transport.hpp"
#include "hmbdc/tips/rmcast/BackupSendServer.hpp"
#include "hmbdc/tips/rmcast/Messages.hpp"
#include "hmbdc/tips/rmcast/McSendTransport.hpp"
#include "hmbdc/tips/rmcast/DefaultUserConfig.hpp"

#include "hmbdc/time/Time.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/numeric/BitMath.hpp"
#include "hmbdc/MetaUtils.hpp"

#include <algorithm>
#include <memory>
#include <tuple>
#include <mutex>
#include <type_traits>

namespace hmbdc { namespace tips { namespace rmcast {

namespace sendtransportengine_detail {
HMBDC_CLASS_HAS_DECLARE(hmbdc_net_queued_ts);

/**
 * @brief capture the transportation mechanism
 * 
 */
struct SendTransport 
: EngineTransport
, hmbdc::time::TimerManager {
    /**
     * @brief ctor
     * @param cfg jason specifing the transport - see example, perf-rmcast.cpp
     * @param maxMessageSize max messafe size in bytes to be sent
     * hold the message in buffer until buffer is full to start sending
     */
    SendTransport(hmbdc::app::Config cfg, size_t maxMessageSize);
    
    size_t bufferedMessageCount() const {
        return buffer_.remainingSize();
    }

    size_t subscribingPartyDetectedCount(uint16_t tag) const {
        return outboundSubscriptions_.check(tag);
    }
        
    template <typename MemoryAttachementMessage>
    typename std::enable_if<std::is_base_of<app::hasMemoryAttachment
        , typename std::decay<MemoryAttachementMessage>::type>::value, void>::type
    queue(MemoryAttachementMessage&& msg) {
        if (!minRecvToStart_ && !outboundSubscriptions_.check(msg.getTypeTag())) {
            msg.release();
            return;
        }
        using Message = typename std::decay<MemoryAttachementMessage>::type;
        if (hmbdc_unlikely(sizeof(Message) > maxMessageSize_ 
            || sizeof(app::MemorySeg) > maxMessageSize_
            || sizeof(app::StartMemorySegTrain) > maxMessageSize_)) {
            HMBDC_THROW(std::out_of_range
                , "maxMessageSize too small to hold a message when constructing SendTransportEngine");
        }

        // + train header and actual msg
        size_t n = (msg.app::hasMemoryAttachment::len + maxMemorySegPayloadSize_ - 1ul) / maxMemorySegPayloadSize_ + 2; 
        if (hmbdc_unlikely(n > buffer_.capacity())) {
            HMBDC_THROW(std::out_of_range
                , "send engine buffer too small capacity=" << buffer_.capacity() << "<" << n);
        }
        auto it = buffer_.claim(n);
        queueMemorySegTrain(it, n, std::forward<MemoryAttachementMessage>(msg));
        buffer_.commit(it, n);
    }


    template <app::MessageC Message>
    typename std::enable_if<!std::is_base_of<hmbdc::app::hasMemoryAttachment
        , typename std::decay<Message>::type>::value, void>::type
    queue(Message&& msg) {
        if (!outboundSubscriptions_.check(msg.getTypeTag())) return;
        auto n = 1;
        auto it = buffer_.claim(n);
        queue(it, std::forward<Message>(msg));
        buffer_.commit(it, n);
    }

    template <app::MessageC Message>
    typename std::enable_if<!std::is_base_of<hmbdc::app::hasMemoryAttachment
        , typename std::decay<Message>::type>::value, bool>::type
    tryQueueMessages(Message&& msg) {
        if (!outboundSubscriptions_.check(msg.getTypeTag())) return true;
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
        static_assert(std::is_trivially_destructible<Message>::value, "cannot send message with dtor");
        auto s = buffer_.claim();
        char* addr = static_cast<char*>(*s);
        auto h = new (addr) TransportMessageHeader;
        if (hmbdc_likely(sizeof(Message) <= maxMessageSize_)) {
            new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<Message>(std::forward<Args>(args)...);
            h->messagePayloadLen = sizeof(app::MessageWrap<Message>);
            h->setSeq(s.seq_);
        } else {
            HMBDC_THROW(std::out_of_range
                , "maxMessageSize too small to hold a message when constructing SendTransportEngine");
        }
        buffer_.commit(s);
    }

    void queueJustBytes(uint16_t tag, void const* bytes, size_t len
        , app::hasMemoryAttachment* att) {
        if (!att) {
            if (!outboundSubscriptions_.check(tag)) return;
            auto it = buffer_.claim();
            auto s = *it;
            char* addr = static_cast<char*>(s);
            auto h = new (addr) TransportMessageHeader;
            if (hmbdc_likely(len <= maxMessageSize_)) {
                new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<app::JustBytes>(tag, bytes, len, att);
                h->messagePayloadLen = sizeof(app::MessageHead) + len;
                h->setSeq(it.seq_);
            } else {
                HMBDC_THROW(std::out_of_range
                    , "maxMessageSize too small to hold a message when constructing SendTransportEngine");
            }
            buffer_.commit(it);
        } else {
            if (!minRecvToStart_ && !outboundSubscriptions_.check(tag)) {
                att->release();
                return;
            }
            if (hmbdc_unlikely(len > maxMessageSize_ 
                || sizeof(app::MemorySeg) > maxMessageSize_
                || sizeof(app::StartMemorySegTrain) > maxMessageSize_)) {
                HMBDC_THROW(std::out_of_range
                    , "maxMessageSize too small to hold a message when constructing SendTransportEngine");
            }

            // + train header and actual msg
            size_t n = (att->len + maxMemorySegPayloadSize_ - 1ul) / maxMemorySegPayloadSize_ + 2;
            if (hmbdc_unlikely(n > buffer_.capacity())) {
                HMBDC_THROW(std::out_of_range
                    , "send engine buffer too small capacity=" << buffer_.capacity() << "<" << n);
            }
            auto it = buffer_.claim(n);
            queueMemorySegTrain(it, n, tag, *att, len);
            buffer_.commit(it, n);
        }
    }

    void runOnce() {
        hmbdc::app::utils::EpollTask::instance().poll();
        if (hmbdc_unlikely(minRecvToStart_ != std::numeric_limits<size_t>::max()
            && asyncBackupSendServer_.readySessionCount() >= minRecvToStart_)) {
            minRecvToStart_ = std::numeric_limits<size_t>::max(); //now started no going back
            mcSendTransport_.startSend();
        }
        asyncBackupSendServer_.runOnce();
        if (advertisedTypeTagsDirty_) {
            std::unique_lock<std::mutex> g(advertisedTypeTags_.lock, std::try_to_lock);
            if (g.owns_lock()) {
                mcSendTransport_.setAds(asyncBackupSendServer_.advertisingMessages());
                advertisedTypeTagsDirty_ = false;
            }
        }
        mcSendTransport_.runOnce(asyncBackupSendServer_.sessionCount());
    }

    void stop();

  
    size_t sessionsRemainingActive() const {
        return asyncBackupSendServer_.readySessionCount();
    }

    template <app::MessageTupleC Messages, typename Node>
    void advertiseFor(Node const& node, uint16_t mod, uint16_t res) {
        std::scoped_lock<std::mutex> g(advertisedTypeTags_.lock);
        advertisedTypeTags_.markPubsFor<Messages>(node, mod, res);
        asyncBackupSendServer_.advertisingMessages(advertisedTypeTags_);
        advertisedTypeTagsDirty_ = true;
    }

private:
    size_t maxMessageSize_;
    using Buffer = hmbdc::pattern::LockFreeBufferT<2>;
    Buffer buffer_;

    hmbdc::time::Rater rater_;
    McSendTransport mcSendTransport_;
    size_t minRecvToStart_;
    hmbdc::time::ReoccuringTimer typeTagAdTimer_;
    HMBDC_SEQ_TYPE lastBackupSeq_;
    hmbdc::time::ReoccuringTimer flushTimer_;
    uint16_t maxMemorySegPayloadSize_;
    bool waitForSlowReceivers_;
    reliable::ToCleanupAttQueue toCleanupAttQueue_;
    TypeTagSet outboundSubscriptions_;
    AsyncBackupSendServer asyncBackupSendServer_;
    bool stopped_;
    std::atomic<bool> advertisedTypeTagsDirty_ = false;
    TypeTagSetST advertisedTypeTags_;

private:
    uint16_t
    outBufferSizePower2();

    template<typename M>
    void queueMemorySegTrain(typename Buffer::iterator it, size_t n, M&& m) {
        using Message = typename std::decay<M>::type;
        //train head
        auto s = *it;
        char* addr = static_cast<char*>(s);
        auto h = new (addr) TransportMessageHeader;
        auto& trainHead = (new (addr + sizeof(TransportMessageHeader)) 
            app::MessageWrap<app::StartMemorySegTrain>)->template get<app::StartMemorySegTrain>();

        trainHead.inbandUnderlyingTypeTag = m.getTypeTag();
        trainHead.att = m;
        trainHead.segCount = n - 2;
        h->messagePayloadLen = sizeof(app::MessageWrap<app::StartMemorySegTrain>);
        h->setSeq(it++.seq_);
   
        //train body
        {
            auto totalLen = (size_t)m.app::hasMemoryAttachment::len;
            char* base = (char*)m.app::hasMemoryAttachment::attachment;
            decltype(totalLen)  offset = 0;
            while(totalLen > offset) {
                auto s = *it;
                char* addr = static_cast<char*>(s);
                auto h = new (addr) TransportMessageHeader;
                auto& seg = (new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<app::MemorySeg>)
                    ->template get<app::MemorySeg>();
                seg.seg = base + offset;
                seg.inbandUnderlyingTypeTag = m.getTypeTag();
                auto l = (uint16_t)std::min((size_t)maxMemorySegPayloadSize_, totalLen - offset);
                seg.len = l;
                h->messagePayloadLen = sizeof(app::MessageHead) + l; //skipped bytes
                h->setSeq(it++.seq_);
                offset += l;
            }
        }

        //actual message
        {
            auto s = *it;
            char* addr = static_cast<char*>(s);
            auto h = new (addr) TransportMessageHeader;
            new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<Message>(std::forward<M>(m));
            h->messagePayloadLen = sizeof(app::MessageWrap<Message>);
            h->flag = app::hasMemoryAttachment::flag;
            h->setSeq(it++.seq_);
        }
    }

    void queueMemorySegTrain(typename Buffer::iterator it, size_t n
        , uint16_t tag, app::hasMemoryAttachment const& m, size_t len);

    template<typename M, typename ... Messages>
    void queue(typename Buffer::iterator it
        , M&& m, Messages&&... msgs) {
        using Message = typename std::decay<M>::type;
        static_assert(std::is_trivially_destructible<Message>::value, "cannot send message with dtor");
        auto s = *it;
        char* addr = static_cast<char*>(s);
        auto h = new (addr) TransportMessageHeader;
        if (hmbdc_likely(sizeof(Message) <= maxMessageSize_)) {
            new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<Message>(std::forward<M>(m));
            h->messagePayloadLen = sizeof(app::MessageWrap<Message>);
            h->setSeq(it.seq_);
        } else {
            HMBDC_THROW(std::out_of_range
                , "maxMessageSize too small to hold a message when constructing SendTransportEngine");
        }
        if constexpr (has_hmbdc_net_queued_ts<Message>::value) {
            h->template wrapped<Message>().hmbdc_net_queued_ts = hmbdc::time::SysTime::now();
        }
        queue(++it, std::forward<Messages>(msgs)...);
    }

    void queue(Buffer::iterator it) {} 

    void cullSlow();
};

struct SendTransportEngine
: SendTransport
, hmbdc::app::Client<SendTransportEngine> {  
    using SendTransport::SendTransport;

    void rotate() {
        if (runLock_.try_lock()) {
            this->checkTimers(hmbdc::time::SysTime::now());
            SendTransportEngine::invokedCb(0);
            runLock_.unlock();
        }
    }

    /*virtual*/ 
    void messageDispatchingStartedCb(size_t const*)  override {
        runLock_.lock();
    }


    /*virtual*/ 
    void invokedCb(size_t) HMBDC_RESTRICT override  {
        this->runOnce();
    }

    /*virtual*/ 
    bool droppedCb() override {
        stop();
        return true;
    };

    using EngineTransport::hmbdcName;

    std::tuple<char const*, int> schedSpec() const {
        return std::make_tuple(this->schedPolicy_.c_str(), this->schedPriority_);
    }

    ~SendTransportEngine() {
        this->runLock_.unlock();
    }

private:
    std::mutex runLock_;
};

} //sendtransportengine_detail
using SendTransport = sendtransportengine_detail::SendTransport;
using SendTransportEngine = sendtransportengine_detail::SendTransportEngine;
}}}

namespace hmbdc { namespace tips { namespace rmcast {

namespace sendtransportengine_detail {

inline
SendTransport::
SendTransport(hmbdc::app::Config cfg
    , size_t maxMessageSize) 
: EngineTransport((cfg.setAdditionalFallbackConfig(hmbdc::app::Config(DefaultUserConfig))
    , cfg.resetSection("tx", false)))
, maxMessageSize_(maxMessageSize)
, buffer_(maxMessageSize + sizeof(TransportMessageHeader) + sizeof(app::MessageHead)
    , outBufferSizePower2()) 
, rater_(hmbdc::time::Duration::seconds(1u)
    , config_.getExt<size_t>("sendBytesPerSec")
    , config_.getExt<size_t>("sendBytesBurst")
    , config_.getExt<size_t>("sendBytesBurst") != 0ul)
, mcSendTransport_(config_, maxMessageSize, buffer_, rater_, toCleanupAttQueue_)
, minRecvToStart_(config_.getExt<size_t>("minRecvToStart"))
, typeTagAdTimer_(hmbdc::time::Duration::seconds(config_.getExt<uint32_t>("typeTagAdvertisePeriodSeconds")))
, lastBackupSeq_(std::numeric_limits<HMBDC_SEQ_TYPE>::max())
, flushTimer_(hmbdc::time::Duration::microseconds(config_.getExt<uint32_t>("netRoundtripLatencyMicrosec")))
, maxMemorySegPayloadSize_(std::min(mtu_ - sizeof(TransportMessageHeader) - sizeof(app::MessageHead)
    , TransportMessageHeader::maxPayloadSize() - sizeof(app::MessageHead))
)
, waitForSlowReceivers_(config_.getExt<bool>("waitForSlowReceivers")) 
, asyncBackupSendServer_(config_, buffer_, rater_, toCleanupAttQueue_, outboundSubscriptions_)
, stopped_(false) { 
    if (maxMessageSize_ > mtu_) {
        HMBDC_THROW(std::out_of_range, "mtu needs to >= " << maxMessageSize_);
    }

    if (maxMessageSize_ > TransportMessageHeader::maxPayloadSize()) {
        HMBDC_THROW(std::out_of_range
            , "maxMessageSize_ needs to <=" << TransportMessageHeader::maxPayloadSize());
    }
    auto sendBytesBurst = config_.getExt<size_t>("sendBytesBurst");
    auto sendBytesBurstMin = maxMessageSize + sizeof(TransportMessageHeader);
    if (sendBytesBurst && sendBytesBurst < sendBytesBurstMin) {
        HMBDC_THROW(std::out_of_range, "sendBytesBurst needs to >= " << sendBytesBurstMin);
    }

    toCleanupAttQueue_.set_capacity(buffer_.capacity() + 10);
    typeTagAdTimer_.setCallback(
        [this](hmbdc::time::TimerManager& tm, hmbdc::time::SysTime const& now) {
            mcSendTransport_.setAdPending();
            if (!waitForSlowReceivers_) cullSlow();
        }
    );
    schedule(hmbdc::time::SysTime::now(), typeTagAdTimer_);

    flushTimer_.setCallback(
        [this](hmbdc::time::TimerManager& tm, hmbdc::time::SysTime const& now) {
            mcSendTransport_.setSeqAlertPending(); 
        }
    );
    schedule(hmbdc::time::SysTime::now(), flushTimer_);
}

inline
void 
SendTransport::
stop() {
    asyncBackupSendServer_.stop();
    buffer_.reset(0);
    buffer_.reset(1);
    stopped_ = true;
};

inline
uint16_t
SendTransport::
outBufferSizePower2() {
    auto res = config_.getExt<uint16_t>("outBufferSizePower2");
    if (res) {
        return res;
    }
    res =hmbdc::numeric::log2Upper(128ul * 1024ul / (8ul + maxMessageSize_));
    HMBDC_LOG_N("auto set --outBufferSizePower2=", res);
    return res;
}

inline
void 
SendTransport::
queueMemorySegTrain(typename Buffer::iterator it, size_t n
    , uint16_t tag, app::hasMemoryAttachment const& m, size_t len) {
    //train head
    auto s = *it;
    char* addr = static_cast<char*>(s);
    auto h = new (addr) TransportMessageHeader;
    auto& trainHead = (new (addr + sizeof(TransportMessageHeader)) 
        app::MessageWrap<app::StartMemorySegTrain>)->template get<app::StartMemorySegTrain>();
    trainHead.inbandUnderlyingTypeTag = tag;
    trainHead.segCount = n - 2;
    trainHead.att = m;
    h->messagePayloadLen = sizeof(app::MessageWrap<app::StartMemorySegTrain>);
    h->setSeq(it++.seq_);

    //train body
    {
        auto totalLen = (size_t)m.len;
        char* base = (char*)m.attachment;
        decltype(totalLen)  offset = 0;
        while(totalLen > offset) {
            auto s = *it;
            char* addr = static_cast<char*>(s);
            auto h = new (addr) TransportMessageHeader;
            auto& seg = (new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<app::MemorySeg>)
                ->template get<app::MemorySeg>();
            seg.seg = base + offset;
            auto l = (uint16_t)std::min((size_t)maxMemorySegPayloadSize_, totalLen - offset);
            seg.len = l;
            seg.inbandUnderlyingTypeTag = tag;
            h->messagePayloadLen = sizeof(app::MessageHead) + l; //skipped bytes
            h->setSeq(it++.seq_);
            offset += l;
        }
    }

    //actual message
    { 
        auto s = *it;
        char* addr = static_cast<char*>(s);
        auto h = new (addr) TransportMessageHeader;
        new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<app::JustBytes>(
            tag, &m, len, &m);
        h->messagePayloadLen = sizeof(app::MessageHead) + len;
        h->flag = app::hasMemoryAttachment::flag;
        h->setSeq(it++.seq_);
    }
}

inline
void 
SendTransport::
cullSlow() {
    if (hmbdc_likely(minRecvToStart_ == std::numeric_limits<size_t>::max())) {
        auto seq = buffer_.readSeq(1);
        if (seq == lastBackupSeq_ && buffer_.isFull()) {
            asyncBackupSendServer_.killSlowestSession();
        }
        lastBackupSeq_ = seq;
    }
}
} //sendtransportengine_detail
}}}

