#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/utils/EpollTask.hpp"
#include "hmbdc/app/Base.hpp"

#include "hmbdc/tips/TypeTagSet.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/pattern/LockFreeBufferT.hpp"
#include "hmbdc/comm/inet/Misc.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/circular_buffer.hpp>

#include <memory>
#include <utility>

#include <cinttypes>
#include <sys/uio.h>
#include <sys/socket.h>

namespace hmbdc { namespace tips { namespace reliable {

using Buffer = hmbdc::pattern::LockFreeBufferT<2>;
namespace backupsendservert_detail {
template <typename TypeTagBackupSource, typename Me>
struct AsyncBackupSendServerT;
}

namespace backupsendsessiont_detail {

using ToSend = std::vector<iovec>;
using ToSendQueue = boost::circular_buffer<std::tuple<
        HMBDC_SEQ_TYPE
        , size_t
    >
>;

template <typename TransportMessageHeader>
struct BackupSendSessionT {
    using ptr = std::shared_ptr<BackupSendSessionT>;
    using Buffer = reliable::Buffer;
    BackupSendSessionT(int fd
    , Buffer const& buffer
    , hmbdc::time::Rater& rater
    , Buffer::iterator bufIt
    , size_t maxSendBatchHint
    , TypeTagSet& outboundSubscriptions)
    : buffer_(buffer)
    , rater_(rater)
    , outboundSubscriptions_(outboundSubscriptions)
    , filledLen_(0)
    , initialized_(false)
    , ready_(false)
    , msghdr_{0}
    , msghdrRelic_{0}
    , msghdrRelicSize_(0)
    , bufIt_(bufIt)
    , bufItNext_(bufIt)
    , sendBackupMessageCount_(0)
     {
        maxSendBatchHint = std::min(maxSendBatchHint * 2, size_t(UIO_MAXIOV)); //double for attachment
        msghdrRelic_.msg_iov = new iovec[maxSendBatchHint]; 
        auto addrPort = hmbdc::comm::inet::getPeerIpPort(fd);
        id_ = addrPort.first + ":" + std::to_string(addrPort.second);
        auto forRead = dup(fd);
        if (forRead == -1) {
            HMBDC_THROW(std::runtime_error, "dup failed errno=" << errno);
        }

        readFd_.fd = forRead;
        hmbdc::app::utils::EpollTask::instance().add(
            hmbdc::app::utils::EpollTask::EPOLLIN
                | hmbdc::app::utils::EpollTask::EPOLLET, readFd_);
        writeFd_.fd = fd;
        hmbdc::app::utils::EpollTask::instance().add(hmbdc::app::utils::EpollTask::EPOLLOUT
            | hmbdc::app::utils::EpollTask::EPOLLET, writeFd_);

        toSend_.reserve(maxSendBatchHint);
        toSendQueue_.set_capacity(buffer.capacity());

        char* addr = flush_;
        auto h = reinterpret_cast<TransportMessageHeader*>(addr);
        h->messagePayloadLen = 0;
    }

    virtual ~BackupSendSessionT() {
        clientSubscriptions_.exportTo([this](uint16_t tag, auto&&) {
            outboundSubscriptions_.sub(tag);
        });

        delete [] msghdrRelic_.msg_iov;
        HMBDC_LOG_N("BackupSendSessionT retired: ", id(), " lifetime sent:", sendBackupMessageCount_);
    }

    void start() {
        HMBDC_LOG_N("BackupSendSessionT started: ", id(), " minSeq=", bufIt_.seq_);
        auto l = send(writeFd_.fd, &bufIt_.seq_, sizeof(bufIt_.seq_), MSG_NOSIGNAL);
        if (l != sizeof(bufIt_.seq_)) {
            HMBDC_THROW(std::runtime_error, "cannot initialize the new connection, errno=" << errno);
        }
    }

    void stop() {
        ready_ = false;
    }

    char const* id() const {
        return id_.c_str();
    }

    bool ready() const {
        return ready_;
    }


    bool runOnce() HMBDC_RESTRICT {
        if (hmbdc_unlikely(!doRead())) return false;
        if (hmbdc_unlikely(writeFd_.isFdReady() && msghdrRelicSize_)) {
            auto l = sendmsg(writeFd_.fd, &msghdrRelic_, MSG_NOSIGNAL|MSG_DONTWAIT);
            if (hmbdc_unlikely(l < 0)) {
                if (!writeFd_.checkErr()) {
                    HMBDC_LOG_C("sendmsg failed errno=", errno);
                    return false;
                }
                return true;
            } 
            msghdrRelicSize_ -= size_t(l);
            if (hmbdc_unlikely(msghdrRelicSize_)) {
                // HMBDC_LOG_C(msghdrRelicSize_);
                comm::inet::extractRelicTo(msghdrRelic_, msghdrRelic_, l);
                return true;
            } else {
                bufItNext_ = bufIt_;
            }
        }
        while(!msghdrRelicSize_ && toSendQueue_.size()) {
            auto it = toSendQueue_.begin();
            auto seq = std::get<0>(*it);
            auto len = std::get<1>(*it);
            if (hmbdc_unlikely(seq < bufItNext_.seq_ 
                || len > buffer_.capacity()
                || seq + len > buffer_.readSeq(0))) {
                //the connection is bad, invalid data, bail out
                HMBDC_LOG_C(id(), ", received bad data ", seq, ',', len, " stopping connection");
                return false;
            }

            if (hmbdc_unlikely(len)) {
                bufIt_.seq_ = seq;
                size_t bytesToSend = 0;
                toSend_.clear();
                for (; len && toSend_.size() < toSend_.capacity() - 1; len--) {
                    void* ptr = *bufIt_++;
                    auto item = static_cast<TransportMessageHeader*>(ptr);

                    bool ifResend = clientSubscriptions_.check(item->typeTag());
                    ifResend = ifResend && (item->typeTag() != app::MemorySeg::typeTag
                        || clientSubscriptions_.check(item->template wrapped<app::MemorySeg>().inbandUnderlyingTypeTag));
                    ifResend = ifResend && (item->typeTag() != app::StartMemorySegTrain::typeTag
                        || clientSubscriptions_.check(item->template wrapped<app::StartMemorySegTrain>().inbandUnderlyingTypeTag));

                        // HMBDC_LOG_N(item->wrapped<SeqMessage>().seq);
                    if (ifResend) {
                        if (hmbdc_unlikely(item->typeTag() == app::MemorySeg::typeTag)) {
                            toSend_.push_back(iovec{(void*)item->wireBytes(), item->wireSize() - item->wireSizeMemorySeg()});
                            toSend_.push_back(iovec{(void*)item->wireBytesMemorySeg(), item->wireSizeMemorySeg()});
                        } else {
                            toSend_.push_back(iovec{(void*)item->wireBytes(), item->wireSize()}); 
                        }
                        bytesToSend += item->wireSize();
                    } else { //dont send a message the recv is going to drop, send a flush
                        toSend_.push_back(iovec{(void*)flush_, sizeof(flush_)});
                        bytesToSend += sizeof(flush_);
                    }
                }
                msghdr_.msg_iov = &(toSend_[0]);
                msghdr_.msg_iovlen = toSend_.size();
                sendBackupMessageCount_ += msghdr_.msg_iovlen;
                auto l = sendmsg(writeFd_.fd, &msghdr_, MSG_NOSIGNAL|MSG_DONTWAIT);
                if (hmbdc_unlikely(l < 0)) {
                    if (!writeFd_.checkErr()) {
                        HMBDC_LOG_C("sendmsg failed errno=", errno);
                        return false;
                    }
                    return true;
                }
                msghdrRelicSize_ = bytesToSend - size_t(l);
                if (hmbdc_likely(!len)) {
                    toSendQueue_.pop_front();
                } else {
                    std::get<0>(*it) += std::get<1>(*it) - len;
                    std::get<1>(*it) = len;
                }
                rater_.check(bytesToSend << 8u);
                rater_.commit();
                if (hmbdc_unlikely(msghdrRelicSize_)) {
                    comm::inet::extractRelicTo(msghdrRelic_, msghdr_, l);
                    return true;
                }
                bufItNext_ = bufIt_;
            } else {
                bufItNext_.seq_ = std::get<0>(*it);
                toSendQueue_.pop_front();
            }
        }

        return true;
    }

private:
    template <typename TypeTagBackupSource, typename Me>
    friend struct hmbdc::tips::reliable::backupsendservert_detail::AsyncBackupSendServerT;

    bool doRead() HMBDC_RESTRICT {
        if (readFd_.isFdReady()) {
            auto l = recv(readFd_.fd, data_ + filledLen_, sizeof(data_) - filledLen_
                , MSG_NOSIGNAL|MSG_DONTWAIT);
            if (hmbdc_unlikely(l < 0)) {
                if (!readFd_.checkErr()) {
                    HMBDC_LOG_C("recv failed errno=", errno);
                    return false;
                }
            } else if (hmbdc_unlikely(l == 0)) {
                    HMBDC_LOG_W("peer dropped:", id());
                    return false;
            } else {
                filledLen_ += l;
            }
        }
        while (filledLen_) {
            auto p = std::find(data_, data_ + filledLen_, '\t');
            if (p != data_ + filledLen_) {
                *p = '\000';
                std::string t(data_ + 1);
                if (t.size() == 0) {
                    ready_ = true;
                }
                else if (data_[0] == '+') {
                    auto tag = (uint16_t)std::stoi(t);
                    if (clientSubscriptions_.set(tag)) {
                        outboundSubscriptions_.add(tag);
                    }
                } else if (data_[0] == '-') {
                    auto tag = (uint16_t)std::stoi(t);
                    if (clientSubscriptions_.unset(tag)) {
                        outboundSubscriptions_.sub(tag);
                    }
                } else if (data_[0] == '=') {
                    uint64_t seq;
                    size_t len;
                    if (hmbdc_likely(2 == sscanf(data_ + 1, "%" SCNu64 ",%zu", &seq, &len))) {
                        toSendQueue_.push_back(std::make_tuple(seq, len));
                    } else {
                        HMBDC_LOG_C(id(), ", received bad data ", data_ + 1, " stopping connection");
                        return false;
                    }
                } //else ignore
                memmove(data_, p + 1, filledLen_ - (p - data_ + 1));
                filledLen_ -= p - data_ + 1;
            } else {
                break; //no complete request
            }
        }
        return true;
    }

private:
    Buffer const& HMBDC_RESTRICT buffer_;
    hmbdc::time::Rater& HMBDC_RESTRICT rater_;
    TypeTagSet clientSubscriptions_;
    TypeTagSet& outboundSubscriptions_;
    hmbdc::app::utils::EpollFd writeFd_;
    hmbdc::app::utils::EpollFd readFd_;
    char data_[16 * 1024];
    size_t filledLen_;
    std::string id_;
    bool initialized_;
    bool ready_;
    ToSend toSend_;
    ToSendQueue toSendQueue_;
    msghdr msghdr_;
    msghdr msghdrRelic_;
    size_t msghdrRelicSize_;
    Buffer::iterator bufIt_;
    Buffer::iterator bufItNext_;
    char flush_[sizeof(TransportMessageHeader)];
    size_t sendBackupMessageCount_;
};

} //backupsendsessiont_detail

template <typename TransportMessageHeader>
using BackupSendSessionT = backupsendsessiont_detail::BackupSendSessionT<TransportMessageHeader>;
}}}
