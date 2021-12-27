#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/tips/tcpcast/Transport.hpp"
#include "hmbdc/tips/tcpcast/Messages.hpp"
#include "hmbdc/tips/TypeTagSet.hpp"
#include "hmbdc/comm/inet/Misc.hpp"


#include <boost/lexical_cast.hpp>

#include <functional>
#include <memory>
#include <utility>
#include <regex>

#include <iostream>

namespace hmbdc { namespace tips { namespace tcpcast {

namespace recvsession_detail {

template <typename OutputBuffer, typename AttachmentAllocator>
struct RecvSession {
    using ptr = std::shared_ptr<RecvSession<OutputBuffer, AttachmentAllocator>>;
    using CleanupFunc = std::function<void()>;
    RecvSession(hmbdc::app::Config const& config
        , TypeTagSet const& subscriptions
        , OutputBuffer& outputBuffer)
    : config_(config)
    , subscriptions_(subscriptions)
    , writeFd_(config)
    , outputBuffer_(outputBuffer)
    , initialized_(false)
    , stopped_(false)
    , bufSize_(config.getExt<size_t>("maxTcpReadBytes"))
    , buf_((char*)memalign(SMP_CACHE_BYTES, bufSize_))
    , bufCur_(buf_)
    , filledLen_(0)
    , currTransportHeadFlag_(0)
    , hasMemoryAttachmentItemSpace_(
        new char[std::max(outputBuffer_.maxItemSize()
            , sizeof(hmbdc::app::hasMemoryAttachment) + sizeof(hmbdc::app::MessageHead))] {0})
    , hasMemoryAttachment_(&((hmbdc::app::MessageHead*)(hasMemoryAttachmentItemSpace_.get()))
        ->get<hmbdc::app::hasMemoryAttachment>()){
    }

    RecvSession(RecvSession const&) = delete;
    RecvSession& operator = (RecvSession const&) = delete;
    ~RecvSession() {
        free(buf_);
        hasMemoryAttachment_->release();
        HMBDC_LOG_N("RecvSession retired: ", id());
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
        hmbdc::app::utils::EpollTask::instance().add(
            hmbdc::app::utils::EpollTask::EPOLLOUT
                | hmbdc::app::utils::EpollTask::EPOLLET, writeFd_);
        hmbdc::app::utils::EpollTask::instance().add(
            hmbdc::app::utils::EpollTask::EPOLLIN
                | hmbdc::app::utils::EpollTask::EPOLLET, readFd_);
    }

    void stop() {
        if (currTransportHeadFlag_ == hmbdc::app::hasMemoryAttachment::flag) {
            currTransportHeadFlag_ = 0;
        }
        outputBuffer_.putSome(sessionDropped_);
    }

    void heartbeat() {
        if (!initialized_) return;
        char const* hb = "+\t"; 
        if (hmbdc_unlikely(2 != send(writeFd_.fd, hb, 2, MSG_NOSIGNAL))){
            HMBDC_LOG_C("error when sending heartbeat to ", id(), " errno=", errno);
            stopped_ = true;
        }
    }

    char const* id() const {
        return id_.c_str();
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

private:
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
        
        auto addrPort = hmbdc::comm::inet::getPeerIpPort(writeFd_.fd);
        id_ = addrPort.first + ":" + std::to_string(addrPort.second);
        strncpy(sessionStarted_.payload.ip, id_.c_str()
            , sizeof(sessionStarted_.payload.ip));
        sessionStarted_.payload.ip[sizeof(sessionStarted_.payload.ip) - 1] = 0;
        strncpy(sessionDropped_.payload.ip, id_.c_str()
            , sizeof(sessionDropped_.payload.ip));
        sessionDropped_.payload.ip[sizeof(sessionDropped_.payload.ip) - 1] = 0;
        outputBuffer_.putSome(sessionStarted_);
        HMBDC_LOG_N("RecvSession started: ", id());
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

    bool doRead() {
        do {
            if (hmbdc_unlikely(currTransportHeadFlag_ == hmbdc::app::hasMemoryAttachment::flag
                && filledLen_)) {
                auto s = memoryAttachment_.write(bufCur_, filledLen_);
                if (memoryAttachment_.writeDone()) {
                    memoryAttachment_.close();
                    currTransportHeadFlag_ = 0;
                    outputBuffer_.putAtt((hmbdc::app::MessageWrap<hmbdc::app::hasMemoryAttachment>*)
                        hasMemoryAttachmentItemSpace_.get(), outputBuffer_.maxItemSize());
                }
                bufCur_ += s;
                filledLen_ -= s;
            }
            while (currTransportHeadFlag_ == 0 
                && filledLen_ >= sizeof(TransportMessageHeader)) {
                auto h = reinterpret_cast<TransportMessageHeader*>(bufCur_);
                auto wireSize = h->wireSize();
                if (hmbdc_likely(filledLen_ >= wireSize)) {
                    if (hmbdc_likely(subscriptions_.check(h->typeTag()))) {
                        if (hmbdc_unlikely(h->flag == hmbdc::app::hasMemoryAttachment::flag)) {
                            currTransportHeadFlag_ = h->flag;
                            auto l = std::min<size_t>(outputBuffer_.maxItemSize(), h->messagePayloadLen);
                            memcpy(hasMemoryAttachmentItemSpace_.get(), h->payload(), l);
                            memoryAttachment_.open(h->typeTag(), hasMemoryAttachment_);
                            // hasMemoryAttachment_->attachment = memoryAttachment_.open(hasMemoryAttachment_->len);
                            // hasMemoryAttachment_->afterConsumedCleanupFunc 
                            //     = [](hmbdc::app::hasMemoryAttachment* a) {::free(a->attachment);a->attachment = nullptr;};
                        } else {
                            auto l = std::min<size_t>(outputBuffer_.maxItemSize(), h->messagePayloadLen);
                            outputBuffer_.put(h->payload(), l);
                            currTransportHeadFlag_ = 0;
                        }
                    }
                    bufCur_ += wireSize;
                    //memmove(buf_, buf_ + h->wireSize(), filledLen_ - h->wireSize());
                    filledLen_ -= wireSize;
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
                } else if (hmbdc_unlikely(l == 0)) {
                    HMBDC_LOG_W("peer dropped:", id());
                    return false;
                } else {
                    filledLen_ += l;
                }
            } else {
                return true; //has done what can be done
            }
        } while (filledLen_);
        return true;
    }

    hmbdc::app::Config const& config_;
    TypeTagSet const& subscriptions_;
    hmbdc::app::utils::EpollFd readFd_;
    EpollFd writeFd_;
    OutputBuffer& outputBuffer_;
    std::string id_;
    hmbdc::app::MessageWrap<SessionStarted> sessionStarted_;
    hmbdc::app::MessageWrap<SessionDropped> sessionDropped_;
    bool initialized_;
    bool stopped_;
    size_t const bufSize_;
    char* buf_;
    char* bufCur_;
    size_t filledLen_;
    uint8_t currTransportHeadFlag_;
    std::unique_ptr<char[]> hasMemoryAttachmentItemSpace_;
    hmbdc::app::hasMemoryAttachment* hasMemoryAttachment_;
    
    struct DownloadMemory {
        DownloadMemory()
        : addr_(nullptr)
        , fullLen_(0)
        , len_(0) {
        }

        void* open(uint16_t typeTag, app::hasMemoryAttachment* att) {
            addr_ = (char*)alloc_(typeTag, att);
            if (addr_) {
                fullLen_ = att->len;
                len_ = 0;
            } 
            return addr_;
        }

        bool writeDone() const {
            return fullLen_ == len_;
        }

        size_t write(void const* mem, size_t l) {
            auto wl = std::min(l, fullLen_ - len_);
            if (addr_) {
                memcpy(addr_ + len_, mem, wl);
            }
            len_ += wl;
            return wl;
        }

        void close() {
            addr_ = nullptr;
            fullLen_ = 0;
            len_ = 0;
        }

    private:
        AttachmentAllocator alloc_;
        char* addr_;
        size_t fullLen_;
        size_t len_;
    };
    DownloadMemory memoryAttachment_;
};
} //recvsession_detail
template <typename OutputBuffer, typename AttachmentAllocator>
using RecvSession = recvsession_detail::RecvSession<OutputBuffer, AttachmentAllocator>;
}}}
