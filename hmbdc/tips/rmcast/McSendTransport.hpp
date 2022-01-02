#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/Base.hpp"
#include "hmbdc/tips/rmcast/Transport.hpp"
#include "hmbdc/tips/rmcast/Messages.hpp"
#include "hmbdc/tips/udpcast/Transport.hpp"
#include "hmbdc/app/utils/EpollTask.hpp"
#include "hmbdc/comm/inet/Endpoint.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/pattern/LockFreeBufferT.hpp"


#include <boost/bind.hpp>
#include <memory>

#include <iostream>

namespace hmbdc { namespace tips { namespace rmcast {

namespace mcsendtransport_detail {

using Buffer = hmbdc::pattern::LockFreeBufferT<2>;

struct McSendTransport 
: Transport {
    using ptr = std::shared_ptr<McSendTransport>;
    McSendTransport(hmbdc::app::Config const& cfg
        , size_t maxMessageSize
        , Buffer& buffer
        , hmbdc::time::Rater& rater
        , ToCleanupAttQueue& toCleanupAttQueue)
    : Transport(cfg)
    , maxMessageSize_(maxMessageSize)
    , buffer_(buffer)
    , wasteSize_(0)
    , mcFd_(cfg)
    , mcAddr_(comm::inet::Endpoint(config_.getExt<std::string>("mcastAddr")
        , config_.getExt<uint16_t>("mcastPort")).v)
    , toSend_{0}
    , rater_(rater)
    , maxSendBatch_(config_.getExt<size_t>("maxSendBatch"))
    , adPending_(false)
    , seqAlertPending_(false)
    , seqAlert_(nullptr)
    , startSending_(false)
    , toCleanupAttQueue_(toCleanupAttQueue) {
        toSend_.msg_name = &mcAddr_;
        toSend_.msg_namelen = sizeof(mcAddr_);
        auto totalHead = sizeof(app::MessageHead) + sizeof(TransportMessageHeader);
        if (maxMessageSize_ + totalHead > mtu_) {
            HMBDC_THROW(std::out_of_range, "maxMessageSize need <= " << mtu_ - totalHead);
        }

        auto addr = seqAlertBuf_;
        auto h = reinterpret_cast<TransportMessageHeader*>(addr);
        new (addr + sizeof(TransportMessageHeader)) app::MessageWrap<SeqAlert>();
        h->messagePayloadLen = sizeof(app::MessageWrap<SeqAlert>);
        h->setSeq(std::numeric_limits<HMBDC_SEQ_TYPE>::max());
        seqAlert_ = &(h->wrapped<SeqAlert>());

        toSendMsgs_.reserve((maxSendBatch_ + 2) * 2); //double for MemorySeg cases

        auto ttl = config_.getExt<int>("ttl");
        if (ttl > 0 && setsockopt(mcFd_.fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
            HMBDC_THROW(std::runtime_error, "failed to set set ttl=" << ttl << " errno=" << errno);
        }
        char loopch = config_.getExt<bool>("loopback")?1:0;
        if (setsockopt(mcFd_.fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, sizeof(loopch)) < 0) {
            HMBDC_THROW(std::runtime_error, "failed to set loopback=" << config_.getExt<bool>("loopback"));
        }

        auto sz = config_.getExt<int>("udpSendBufferBytes");
        if (sz) {
            if (setsockopt(mcFd_.fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz)) < 0) {
                HMBDC_LOG_C("failed to set send buffer size=", sz);
            }
        }
        hmbdc::app::utils::EpollTask::instance().add(
            hmbdc::app::utils::EpollTask::EPOLLOUT 
                | hmbdc::app::utils::EpollTask::EPOLLET, mcFd_);
    }

    void startSend(){
        startSending_ = true;
    }

    template <typename AdvertisingMessages>
    void setAds(AdvertisingMessages const& ads) {
        decltype(adBufs_) newAdBufs;
        for (auto const& ad : ads) {
            newAdBufs.emplace_back();
            auto addr = newAdBufs.rbegin()->data();
            auto h = new (addr) TransportMessageHeader;
            new (addr + sizeof(TransportMessageHeader)) 
                app::MessageWrap<TypeTagBackupSource>(ad);
            h->messagePayloadLen = sizeof(app::MessageWrap<TypeTagBackupSource>);
            h->setSeq(std::numeric_limits<HMBDC_SEQ_TYPE>::max());
        }
        std::swap(adBufs_, newAdBufs);
    }

    void setAdPending() {
        adPending_ = true;
    }

    void setSeqAlertPending() {
        seqAlertPending_ = true;
    }


    void runOnce(size_t sessionCount) HMBDC_RESTRICT {
        resumeSend(sessionCount);
    }

    void stop(){
        buffer_.reset(0);
    }

private:
    size_t maxMessageSize_;
    Buffer& HMBDC_RESTRICT buffer_;
    size_t wasteSize_;

    udpcast::EpollFd mcFd_; 
    sockaddr_in mcAddr_;
    msghdr toSend_;
    hmbdc::time::Rater& HMBDC_RESTRICT rater_;
    size_t maxSendBatch_;
    std::vector<std::array<char, sizeof(TransportMessageHeader) 
        + sizeof(app::MessageWrap<TypeTagBackupSource>)>> adBufs_;
    bool adPending_;
    char seqAlertBuf_[sizeof(TransportMessageHeader) +  sizeof(app::MessageWrap<SeqAlert>)];
    bool seqAlertPending_;
    SeqAlert* HMBDC_RESTRICT seqAlert_;
    bool startSending_;
    std::vector<iovec> toSendMsgs_;
    ToCleanupAttQueue& toCleanupAttQueue_;

    void 
    resumeSend(size_t sessionCount) {
        do {
            if (hmbdc_likely(toSendMsgs_.size())) {
                if (hmbdc_unlikely(!mcFd_.isFdReady())) return;

                toSend_.msg_iov = &(toSendMsgs_[0]);
                toSend_.msg_iovlen = toSendMsgs_.size();
                if (sendmsg(mcFd_.fd, &toSend_, MSG_DONTWAIT) < 0) {
                    if (!mcFd_.checkErr()) {
                        HMBDC_LOG_C("sendmmsg failed errno=", errno);
                    }
                    return; 
                } else {
                    buffer_.wasteAfterPeek(0, wasteSize_);
                    toSendMsgs_.clear();
                    wasteSize_ = 0;
                }
            }

            size_t batchBytes = 0;
            if (hmbdc_unlikely(adPending_)) {
                for (auto& adBuf : adBufs_) {
                    toSendMsgs_.push_back(iovec{(void*)adBuf.data(), adBuf.size()});
                    batchBytes += sizeof(adBuf);
                }
                adPending_ = false;
            }

            if (hmbdc_likely(startSending_) && !toSendMsgs_.size()) {
                Buffer::iterator begin,  end;
                buffer_.peek(0, begin, end, maxSendBatch_);
                if (hmbdc_unlikely(!sessionCount)) {
                    buffer_.wasteAfterPeek(0u, end - begin);
                    begin = end;
                }
                auto it = begin;
                while (it != end) {
                    void* ptr = *it;
                    auto item = static_cast<TransportMessageHeader*>(ptr);
                    if (hmbdc_unlikely(!rater_.check(item->wireSize()))) break;
                    batchBytes += item->wireSize();
                    if (hmbdc_unlikely(batchBytes > mtu_)) break;
                    if (hmbdc_unlikely(item->typeTag() == app::MemorySeg::typeTag)) {
                        toSendMsgs_.push_back(iovec{(void*)item->wireBytes(), item->wireSize() - item->wireSizeMemorySeg()});
                        toSendMsgs_.push_back(iovec{(void*)item->wireBytesMemorySeg(), item->wireSizeMemorySeg()});
                    } else {
                        if (hmbdc_unlikely(item->typeTag() == app::StartMemorySegTrain::typeTag)) {
                            auto& trainHead = item->template wrapped<app::StartMemorySegTrain>();
                            auto itActual = it; itActual.seq_ += trainHead.segCount + 1;
                            auto actual = static_cast<TransportMessageHeader*>(*itActual);
                            toCleanupAttQueue_.push_back(std::make_tuple(itActual.seq_
                                , &actual->template wrapped<app::hasMemoryAttachment>()
                                , trainHead.att.afterConsumedCleanupFunc));
                        }
                        toSendMsgs_.push_back(iovec{(void*)item->wireBytes(), item->wireSize()}); 
                    }
                    it++;

                    seqAlert_->expectSeq = item->getSeq() + 1;
                    rater_.commit();
                }
                wasteSize_ = it - begin;
            }
            if (hmbdc_unlikely(seqAlertPending_)) {
                if (!wasteSize_) {
                    toSendMsgs_.push_back(iovec{seqAlertBuf_, sizeof(seqAlertBuf_)});
                    batchBytes += sizeof(seqAlertBuf_);
                } //else no need to send seq alert
                seqAlertPending_ = false;
            }
        } while (hmbdc_likely(toSendMsgs_.size()));
    }
};

} //mcsendtransport_detail
using McSendTransport = mcsendtransport_detail::McSendTransport;
}}}
