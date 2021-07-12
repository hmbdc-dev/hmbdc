#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/tips/tcpcast/Transport.hpp"
#include "hmbdc/tips/tcpcast/Messages.hpp"
#include "hmbdc/tips/tcpcast/SendSession.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/pattern/MonoLockFreeBuffer.hpp"
#include "hmbdc/comm/inet/Misc.hpp"

#include <unordered_set>
#include <memory>
#include <utility>
#include <iostream>

#include <netinet/tcp.h>

namespace hmbdc { namespace tips { namespace tcpcast {

namespace sendserver_detail {

struct SendServer {
    SendServer(hmbdc::app::Config const& cfg
        , size_t toSendQueueMaxSize
        , TypeTagSet& outboundSubscriptions)
    : config_(cfg)
    , serverFd_(cfg)
    , serverAddr_(serverFd_.localAddr)
    , maxSendBatch_(config_.getExt<size_t>("maxSendBatch"))
    , outboundSubscriptions_(outboundSubscriptions) {
        if (listen(serverFd_.fd, 10) < 0) {
            HMBDC_THROW(std::runtime_error, "failed to listen, errno=" << errno);
        }
        hmbdc::app::utils::EpollTask::instance().add(
            hmbdc::app::utils::EpollTask::EPOLLIN
                | hmbdc::app::utils::EpollTask::EPOLLET, serverFd_);
        toSendQueue_.set_capacity(toSendQueueMaxSize);
    }

    void advertisingMessages(TypeTagSetST& tts) {
        decltype(advertisingMessages_) newAds;
        tts.exportTo([&](uint16_t tag, auto&& subCount) {
            if (newAds.size() == 0
                || !newAds.rbegin()->addTypeTag(tag)) {
                newAds.emplace_back(serverFd_.localIp
                    , serverFd_.localPort
                    , config_.getExt<bool>("loopback"));
                newAds.rbegin()->addTypeTag(tag);
            }
        });
        std::swap(advertisingMessages_, newAds);
        for (auto& m : advertisingMessages_) {
            HMBDC_LOG_N("advertise at ", m);
        }
    }

    auto const& advertisingMessages() const {
        return advertisingMessages_;
    }

    void queue(uint16_t tag
        , ToSend && toSend
        , size_t toSendByteSize
        , hmbdc::pattern::MonoLockFreeBuffer::iterator it) {
        toSendQueue_.push_back(std::forward_as_tuple(tag, std::forward<ToSend>(toSend), toSendByteSize, it));
    }

    /**
     * @brief run the server's async send function and decide which items in buffer
     * can be release
     * @details called all the time
     * 
     * @param begin if nothing can be released in buffer return this
     * @param end if all can be releases return this
     * 
     * @return iterator in buffer poing to new start (not released)
     */
    hmbdc::pattern::MonoLockFreeBuffer::iterator runOnce(hmbdc::pattern::MonoLockFreeBuffer::iterator begin
        , hmbdc::pattern::MonoLockFreeBuffer::iterator end) HMBDC_RESTRICT {
        doAccept();
        if (sessions_.size() == 0) {
            toSendQueue_.clear();
            return end;
        }
        //nothing to retire by default
        auto newStartIt = begin;

        auto minIndex = toSendQueue_.size();
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (minIndex > (*it)->toSendQueueIndex_) {
                minIndex = (*it)->toSendQueueIndex_;
            }
            if (hmbdc_unlikely(!(*it)->runOnce())) {
                sessions_.erase(it++);
            } else {
                it++;
            }
        }
        if (minIndex) {
            newStartIt = std::get<3>(toSendQueue_[minIndex - 1]);
            toSendQueue_.erase_begin(minIndex);
            for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
                (*it)->toSendQueueIndex_ -= minIndex;
            }
        }

        return newStartIt;
    }

    size_t readySessionCount() const {
        size_t res = 0;
        for (auto const& s : sessions_) {
            if (s->ready()) res++;
        }
        return res;
    }

    void killSlowestSession() {
        for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
            if (0 == (*it)->toSendQueueIndex_) {
                HMBDC_LOG_C((*it)->id(), " too slow, dropping");
                sessions_.erase(it);
                break;
            }
        }
    }

private:
    void doAccept() HMBDC_RESTRICT {
        if (hmbdc_unlikely(serverFd_.isFdReady())) {
            auto addrlen = sizeof(serverAddr_);
            auto conn = accept(serverFd_.fd, (struct sockaddr *)&serverAddr_, (socklen_t*)&addrlen);
            if (conn == -1) {
                if (!serverFd_.checkErr()) {
                    HMBDC_LOG_C("accept failure, errno=", errno);
                }
                return;
            }
            auto sz = config_.getExt<int>("tcpSendBufferBytes");
            if (sz) {
                if (setsockopt(conn, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz)) < 0) {
                    HMBDC_LOG_C("failed to set send buffer size=", sz, " errno=", errno);
                }
            }
            int flag = config_.getExt<bool>("nagling")?0:1;
            if (setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, (char*) &flag, sizeof(flag)) < 0) {
                HMBDC_LOG_C("failed to set TCP_NODELAY, errno=", errno);
            }
            try {
                auto s = std::make_shared<SendSession>(
                    conn, toSendQueue_, maxSendBatch_, outboundSubscriptions_);
                sessions_.insert(s);
            } catch (std::exception const& e) {
                HMBDC_LOG_C(e.what());
            }
        }
    }
    
    hmbdc::app::Config const& config_;
    using Sessions = std::unordered_set<SendSession::ptr>;
    Sessions sessions_;
    ToSendQueue toSendQueue_; 
    EpollFd serverFd_;
    sockaddr_in& serverAddr_;
    mutable std::vector<TypeTagSource> advertisingMessages_;

    size_t maxSendBatch_;
    TypeTagSet& outboundSubscriptions_;
};
} //sendserver_detail
using SendServer = sendserver_detail::SendServer;
}}}
