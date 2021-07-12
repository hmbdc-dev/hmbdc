#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/tips/tcpcast/Transport.hpp"
#include "hmbdc/tips/tcpcast/Messages.hpp"
#include "hmbdc/tips/TypeTagSet.hpp"

#include "hmbdc/time/Time.hpp"
#include "hmbdc/pattern/MonoLockFreeBuffer.hpp"
#include "hmbdc/comm/inet/Misc.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/circular_buffer.hpp>

#include <random>
#include <memory>
#include <utility>
#include <iostream>
#include <fcntl.h>

namespace hmbdc { namespace tips { namespace tcpcast {

namespace sendserver_detail {
struct SendServer;
}

using ToSend = std::vector<iovec>;
using ToSendQueue = boost::circular_buffer<std::tuple<uint16_t //TypeTag
    , ToSend
    , size_t //total bytes above
    , hmbdc::pattern::MonoLockFreeBuffer::iterator //end iterator
    >
>;

namespace sendsession_detail {

struct SendSession {
    using ptr = std::shared_ptr<SendSession>;
    SendSession(int fd
        , ToSendQueue & toSendQueue
        , size_t maxSendBatch
        , TypeTagSet& outboundSubscriptions)
    : readLen_(0)
    , ready_(false) 
    , toSendQueue_(toSendQueue)
    , toSendQueueIndex_(0)
    , msghdr_{0}
    , msghdrRelic_{0}
    , msghdrRelicSize_(0)
    , outboundSubscriptions_(outboundSubscriptions) {
        msghdrRelic_.msg_iov = new iovec[maxSendBatch * 2]; //double for attachment
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
        hmbdc::app::utils::EpollTask::instance().add(
            hmbdc::app::utils::EpollTask::EPOLLOUT
                | hmbdc::app::utils::EpollTask::EPOLLET, writeFd_);
        HMBDC_LOG_N("SendSession started: ", id());
    }

    ~SendSession() {
        clientSubscriptions_.exportTo([this](uint16_t tag, auto&&) {
            outboundSubscriptions_.sub(tag);
        });
        delete [] msghdrRelic_.msg_iov;
        HMBDC_LOG_N("SendSession retired: ", id());
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
                comm::inet::extractRelicTo(msghdrRelic_, msghdrRelic_, l);
                return true;
            } else {
                toSendQueueIndex_++;
            }
        }
        for (; !msghdrRelicSize_ && writeFd_.isFdReady() 
            && toSendQueueIndex_ != toSendQueue_.size();) {
            if (clientSubscriptions_.check(std::get<0>(toSendQueue_[toSendQueueIndex_]))) {
                msghdr_.msg_iov = &(std::get<1>(toSendQueue_[toSendQueueIndex_])[0]);
                msghdr_.msg_iovlen = std::get<1>(toSendQueue_[toSendQueueIndex_]).size();
                auto l = sendmsg(writeFd_.fd, &msghdr_, MSG_NOSIGNAL|MSG_DONTWAIT);
                if (hmbdc_unlikely(l < 0)) {
                    if (!writeFd_.checkErr()) {
                        HMBDC_LOG_C("sendmsg failed errno=", errno);
                        return false;
                    }
                    return true;
                }
                msghdrRelicSize_ = std::get<2>(toSendQueue_[toSendQueueIndex_]) - size_t(l);
                if (hmbdc_unlikely(msghdrRelicSize_)) {
                    comm::inet::extractRelicTo(msghdrRelic_, msghdr_, l);
                    return true;
                }
            }
            toSendQueueIndex_++;
        }
        return true;
    }

    char const* id() const {
        return id_.c_str();
    }

    bool ready() const {
        return ready_;
    }

private:
    bool 
    doRead() HMBDC_RESTRICT {
        if (hmbdc_unlikely(readFd_.isFdReady())) {
            auto l = recv(readFd_.fd, data_ + readLen_, sizeof(data_) - readLen_,  MSG_NOSIGNAL|MSG_DONTWAIT);
            if (hmbdc_unlikely(l < 0)) {
                if (!readFd_.checkErr()) {
                    HMBDC_LOG_C("recv failed errno=", errno);
                    return false;
                }
            } else {
                readLen_ += l;
            }
        };
        while (readLen_) {
            auto p = std::find(data_, data_ + readLen_, '\t');
            if (p != data_ + readLen_) {
                *p = '\000';
                std::string t(data_ + 1);
                if (t.size() == 0) {
                    ready_ = true;
                } else if (data_[0] == '+') {
                    auto tag = (uint16_t)std::stoi(t);
                    if (clientSubscriptions_.set(tag)) {
                        outboundSubscriptions_.add(tag);
                    }
                } else if (data_[0] == '-') {
                    auto tag = (uint16_t)std::stoi(t);
                    if (clientSubscriptions_.unset(tag)) {
                        outboundSubscriptions_.sub(tag);
                    }
                } else {
                        HMBDC_LOG_C("voilating protocle by ", id_);
                        return false;
                }
                memmove(data_, p + 1, readLen_ - (p - data_ + 1));
                readLen_ -= p - data_ + 1;
            } else {
                break; //no complete type tag request
            }
        }
        return true;
    }
    app::utils::EpollFd writeFd_;
    app::utils::EpollFd readFd_;
    char data_[16 * 1024];
    size_t readLen_;
    std::string id_;
    bool ready_;

    friend struct hmbdc::tips::tcpcast::sendserver_detail::SendServer;
    ToSendQueue& toSendQueue_;
    ToSendQueue::size_type toSendQueueIndex_;
    msghdr msghdr_;
    msghdr msghdrRelic_;
    size_t msghdrRelicSize_;

    TypeTagSet clientSubscriptions_;
    TypeTagSet& outboundSubscriptions_;
};
} //sendsession_detail

using SendSession = sendsession_detail::SendSession;

}}}
