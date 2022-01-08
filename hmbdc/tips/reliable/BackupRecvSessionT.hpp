#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/reliable/TcpEpollFd.hpp"
#include "hmbdc/tips/reliable/AttBufferAdaptor.hpp"
#include "hmbdc/tips/Messages.hpp"
#include "hmbdc/app/Config.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/pattern/SeqArb.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/comm/inet/Misc.hpp"

#include <unordered_set>
#include <boost/lexical_cast.hpp>

#include <functional>
#include <memory>
#include <utility>
#include <regex>

#include <iostream>

namespace hmbdc { namespace tips { namespace reliable {

using Tags = std::unordered_set<uint16_t>;

namespace backuprecvsessiont_detail {
template <typename TransportMessageHeader
    , typename SessionStarted
    , typename SessionDropped
    , typename SeqAlert
    , typename OutputBufferIn
    , typename SendFrom
    , typename AttachmentAllocator>
struct BackupRecvSessionT 
: hmbdc::time::ReoccuringTimer {
    using ptr = std::shared_ptr<BackupRecvSessionT<TransportMessageHeader
        , SessionStarted
        , SessionDropped
        , SeqAlert
        , OutputBufferIn
        , SendFrom
        , AttachmentAllocator>>;
    BackupRecvSessionT(hmbdc::app::Config const& config
        , SendFrom const& sendFrom
        , TypeTagSet const& subscriptions
        , OutputBufferIn& outputBuffer
        , time::Duration recvReportDelay)
    : hmbdc::time::ReoccuringTimer(recvReportDelay)
    , sendFrom(sendFrom)
    , config_(config)
    , minSeq_(std::numeric_limits<HMBDC_SEQ_TYPE>::max())
    , subscriptions_(subscriptions)
    , writeFd_(config_)
    , outputBuffer_(outputBuffer)
    , bufSize_(config.getExt<size_t>("maxTcpReadBytes"))
    , buf_((char*)memalign(SMP_CACHE_BYTES, bufSize_))
    , bufCur_(buf_)
    , filledLen_(0)
    , seqArb_(std::numeric_limits<HMBDC_SEQ_TYPE>::max())
    , initialized_(false)
    , ready_(false)
    , stopped_(false)
    , recvBackupMessageCount_(0)
    , gapPending_(false)
    , gapPendingSeq_(0) {
        setCallback(
            [this](hmbdc::time::TimerManager& tm, hmbdc::time::SysTime const& now) {
                sendGapReport(seqArb_.expectingSeq(), 0);
            }
        );
    }

    BackupRecvSessionT(BackupRecvSessionT const&) = delete;
    BackupRecvSessionT& operator = (BackupRecvSessionT const&) = delete;
    virtual ~BackupRecvSessionT() {
        ::free(buf_);
        HMBDC_LOG_N("BackupRecvSession retired: ", id(), " lifetime recved:", recvBackupMessageCount_);
    }

    void stop() {
        outputBuffer_.putSome(sessionDropped_);
    }

    void start(in_addr_t ip, uint16_t port) {
        sockaddr_in remoteAddr = {0};
        remoteAddr.sin_family = AF_INET;
        remoteAddr.sin_addr.s_addr = ip;
        remoteAddr.sin_port = htons(port);
        if (connect(writeFd_.fd, (sockaddr*)&remoteAddr, sizeof(remoteAddr)) < 0) {
            if (errno != EINPROGRESS) {
                HMBDC_THROW(std::runtime_error, "connect fail, errno=" << errno);
            }
        }

        auto forRead = dup(writeFd_.fd);
        if (forRead == -1) {
            HMBDC_THROW(std::runtime_error, "dup failed errno=" << errno);
        }

        readFd_.fd = forRead;
        hmbdc::app::utils::EpollTask::instance().add(hmbdc::app::utils::EpollTask::EPOLLOUT
            | hmbdc::app::utils::EpollTask::EPOLLET, writeFd_);
        hmbdc::app::utils::EpollTask::instance().add(hmbdc::app::utils::EpollTask::EPOLLIN
            | hmbdc::app::utils::EpollTask::EPOLLET, readFd_);
    }

    bool runOnce() {
        if (hmbdc_unlikely(stopped_)) return false;
        if (hmbdc_unlikely(!initialized_ && writeFd_.isFdReady())) {
            try {
                initializeConn();
            } catch (std::exception const& e) {
                HMBDC_LOG_W(e.what());
                return false;
            } catch (...) {
                HMBDC_LOG_C("unknown exception");
                return false;
            }
        }
        return doRead();
    }

    void sendSubscribe(uint16_t t) {
        char buf[70];
        auto l = sprintf(buf, "+%d\t", t) - 1;
        if (hmbdc_unlikely(send(writeFd_.fd, buf, l, MSG_NOSIGNAL) != l)) {
            HMBDC_LOG_C("error when sending subscribe to ", id());
            stopped_ = true;
        }
    }

    void sendUnsubscribe(uint16_t const& t) {
        char buf[70];
        auto l = sprintf(buf, "-%d\t", t) - 1;
        if (hmbdc_unlikely(l != send(writeFd_.fd, buf, l, MSG_NOSIGNAL))) {
            HMBDC_LOG_C("error when sending unsubscribe to ", id(), " errno=", errno);
            stopped_ = true;
        }
    }

    char const* id() const {
        return id_.c_str();
    }

    int accept(TransportMessageHeader* h) {
        auto seq = h->getSeq();
        // if (seq != std::numeric_limits<HMBDC_SEQ_TYPE>::max()) 
        //     HMBDC_LOG_N(h->typeTag(), '@', seq);
        auto a = arb(0, seq, h);
        if (a == 1) {
            auto tag = h->typeTag();
            if (tag == app::StartMemorySegTrain::typeTag) {
                tag = h->template wrapped<app::StartMemorySegTrain>().inbandUnderlyingTypeTag;
            }
            if (subscriptions_.check(tag)) {
                outputBuffer_.put(h);
            }
        }
        return a;
    }
    
    SendFrom const sendFrom;
private:
    using OutputBuffer = AttBufferAdaptor<OutputBufferIn, TransportMessageHeader, AttachmentAllocator>;
    void initializeConn() {
        int flags = fcntl(writeFd_.fd, F_GETFL, 0);
        flags &= ~O_NONBLOCK;
        if (fcntl(writeFd_.fd, F_SETFL, flags) < 0) {
            HMBDC_THROW(std::runtime_error, "fcntl failed errno=" << errno);
        }

        auto sz = config_.getExt<int>("tcpRecvBufferBytes");
        if (sz) {
            if (setsockopt(readFd_.fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz)) < 0) {
                HMBDC_LOG_C("failed to set send buffer size=", sz);
            }
        }
        
        auto flag = config_.getExt<bool>("nagling")?0:1;
        if (setsockopt(writeFd_.fd, IPPROTO_TCP, TCP_NODELAY, (char*) &flag, sizeof(flag)) < 0) {
            HMBDC_LOG_C("failed to set TCP_NODELAY, errno=", errno);
        }

        auto addrPort = hmbdc::comm::inet::getPeerIpPort(writeFd_.fd);
        id_ = addrPort.first + ":" + std::to_string(addrPort.second);
        strncpy(sessionStarted_.payload.ip, id_.c_str()
            , sizeof(sessionStarted_.payload.ip));
        sessionStarted_.payload.ip[sizeof(sessionStarted_.payload.ip) - 1] = 0;
        strncpy(sessionDropped_.payload.ip, id_.c_str()
            , sizeof(sessionDropped_.payload.ip));
        sessionDropped_.payload.ip[sizeof(sessionDropped_.payload.ip) - 1] = 0;
        outputBuffer_.putSome(sessionStarted_);
        sendSubscriptions();
        initialized_ = true;
    }

    void sendSubscriptions() {
        std::ostringstream oss;
        subscriptions_.exportTo([&oss](uint16_t tag, auto&& count) {
            if (count) {
                oss << '+' << tag << '\t';
            }
        });
        auto s = oss.str() + "+\t"; //mark all sent with "+\t"
        auto sz = size_t(send(writeFd_.fd, s.c_str(), s.size(), MSG_NOSIGNAL));
        if (hmbdc_unlikely(sz != s.size())) {
            HMBDC_LOG_C("error when sending subscriptions to ", id());
            stopped_ = true;
        }
    }

    bool 
    doRead() HMBDC_RESTRICT {
        if (hmbdc_unlikely(seqArb_.expectingSeq() == std::numeric_limits<HMBDC_SEQ_TYPE>::max()) &&
            filledLen_ >= sizeof(HMBDC_SEQ_TYPE)) {
            minSeq_ = *reinterpret_cast<HMBDC_SEQ_TYPE*>(bufCur_);
            auto wireSize = sizeof(HMBDC_SEQ_TYPE);
            bufCur_ += wireSize;
            filledLen_ -= wireSize;
            HMBDC_LOG_N("BackupRecvSession started ", id(), " with minSeq=", minSeq_);
            seqArb_.expectingSeq() = minSeq_;
        }
        while (minSeq_ != std::numeric_limits<HMBDC_SEQ_TYPE>::max() 
            && filledLen_ >= sizeof(TransportMessageHeader)) {
            auto h = reinterpret_cast<TransportMessageHeader*>(bufCur_);
            auto wireSize = h->wireSize();

            if (hmbdc_likely(filledLen_ >= wireSize)) {
                if (hmbdc_likely(h->messagePayloadLen //0 for flushing - no typeTag
                    && subscriptions_.check(h->typeTag()))) {
                    outputBuffer_.put(h);
                }
                bufCur_ += wireSize;
                //memmove(buf_, buf_ + h->wireSize(), filledLen_ - h->wireSize());
                filledLen_ -= wireSize;

                arb(1, seqArb_.expectingSeq(), h); //just punch the card
                recvBackupMessageCount_++;
            } else {
                break;
            }
        }
        memmove(buf_, bufCur_, filledLen_);
        bufCur_ = buf_;

        if (readFd_.isFdReady()) {
            auto l = recv(readFd_.fd, buf_ + filledLen_, bufSize_ - filledLen_
                , MSG_NOSIGNAL|MSG_DONTWAIT);
            if (hmbdc_unlikely(l < 0)) {
                if (hmbdc_unlikely(!readFd_.checkErr())) {
                    HMBDC_LOG_C("recv failed errno=", errno);
                    return false;
                }
                return true;
            } else if (hmbdc_unlikely(l == 0)) {
                HMBDC_LOG_W("peer dropped:", id());
                return false;
            }
            filledLen_ += l;
        }

        return true;
    }

    void sendGapReport(HMBDC_SEQ_TYPE missSeq, size_t len) {
        if (hmbdc_likely(!gapPending_ && missSeq != std::numeric_limits<HMBDC_SEQ_TYPE>::max())) {
            char buf[70];
            auto l = sprintf(buf, "=%" PRIu64 ",%zu\t", missSeq, len);

            if (hmbdc_unlikely(l != send(writeFd_.fd, buf, l, MSG_NOSIGNAL))) {
                HMBDC_LOG_C("error when sending gap report to ", id(), " errno=", errno);
                stopped_ = true;
            }

            if (len) {
                gapPendingSeq_ = missSeq + len - 1;
                gapPending_ = true;
            }
        }
    }

    int arb(uint16_t part, HMBDC_SEQ_TYPE seq
        , TransportMessageHeader* h) HMBDC_RESTRICT {
        //UDP packet out of order case
        if (hmbdc_unlikely(gapPending_ && part == 0)) return 0; 
        if (seq != std::numeric_limits<HMBDC_SEQ_TYPE>::max()) {
            auto res = seqArb_(part, seq, [](size_t) {
                //impossible to get here
            });
            if (res == 0) {
                sendGapReport(seqArb_.expectingSeq(), seq - seqArb_.expectingSeq());
            } else if (seq == gapPendingSeq_) {
                gapPending_ = false;
            }
            return res;
        } else if (h->typeTag() == SeqAlert::typeTag) {
            auto& alert = h->template wrapped<SeqAlert>();
            auto nextSeq = alert.expectSeq;

            if (seqArb_.expectingSeq() < nextSeq) {
                sendGapReport(seqArb_.expectingSeq(), nextSeq - seqArb_.expectingSeq());
            }
            // HMBDC_LOG_D(std::this_thread::get_id(), "=-1");
            return -1;
        }
        return 1;
    }

    hmbdc::app::Config const& config_;
    HMBDC_SEQ_TYPE minSeq_;
    TypeTagSet const& subscriptions_;
    hmbdc::app::utils::EpollFd readFd_;
    TcpEpollFd writeFd_;
    OutputBuffer outputBuffer_;
    std::string id_;
    hmbdc::app::MessageWrap<SessionStarted> sessionStarted_;
    hmbdc::app::MessageWrap<SessionDropped> sessionDropped_;
    size_t const bufSize_;
    char* buf_;
    char* bufCur_;
    size_t filledLen_;
    pattern::SingleThreadSeqArb<2, HMBDC_SEQ_TYPE> seqArb_;
    bool initialized_;
    bool ready_;
    bool stopped_;
    size_t recvBackupMessageCount_;
    bool gapPending_; //a gap is reported and not filled yet
    HMBDC_SEQ_TYPE gapPendingSeq_;
    uint16_t attTypeTag_;
    void* attUserScratchpad_;
};
} //backuprecvsessiont_detail

template <typename TransportMessageHeader
    , typename SessionStarted
    , typename SessionDropped
    , typename SeqAlert
    , typename OutputBuffer
    , typename SendFrom
    , typename AttachmentAllocator>
using BackupRecvSessionT = 
    backuprecvsessiont_detail::BackupRecvSessionT<
        TransportMessageHeader
        , SessionStarted
        , SessionDropped
        , SeqAlert
        , OutputBuffer
        , SendFrom
        , AttachmentAllocator
    >;
}}}
