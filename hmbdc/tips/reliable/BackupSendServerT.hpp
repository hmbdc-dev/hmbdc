#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/reliable/TcpEpollFd.hpp"
#include "hmbdc/tips/TypeTagSet.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/pattern/LockFreeBufferT.hpp"
#include "hmbdc/comm/inet/Misc.hpp"

#include <boost/circular_buffer.hpp>
#include <unordered_set>
#include <memory>
#include <tuple>
#include <utility>
#include <netinet/tcp.h>

namespace hmbdc { namespace tips { namespace reliable {

using ToCleanupAttQueue = boost::circular_buffer<std::tuple<
        HMBDC_SEQ_TYPE
        , app::hasMemoryAttachment*
        , app::hasMemoryAttachment::AfterConsumedCleanupFunc
    >
>;

namespace backupsendservert_detail {
template <typename TypeTagBackupSource, typename BackupSendSession>
struct BackupSendServerT {
    BackupSendServerT(app::Config const& cfg)
    : config_(cfg)
    , serverFd_(config_)
    , serverAddr_(serverFd_.localAddr) {
        if (listen(serverFd_.fd, 10) < 0) {
            HMBDC_THROW(std::runtime_error, "failed to listen, errno=" << errno);
        }
        hmbdc::app::utils::EpollTask::instance().add(
            hmbdc::app::utils::EpollTask::EPOLLIN
                | hmbdc::app::utils::EpollTask::EPOLLET, serverFd_);
    }

    void advertisingMessages(TypeTagSetST& tts) {
        decltype(advertisingMessages_) newAds;
        tts.exportTo([&](uint16_t tag, auto&& subCount) {
            if (newAds.size() == 0
                || !newAds.rbegin()->addTypeTag(tag)) {
                newAds.emplace_back(serverFd_.localIp
                    , serverFd_.localPort
                    , time::Duration::microseconds(config_.getExt<uint32_t>("recvReportDelayMicrosec"))
                    , config_.getExt<bool>("loopback"));
                newAds.rbegin()->addTypeTag(tag);
            }
        });
        std::swap(advertisingMessages_, newAds);
        for (auto& m : advertisingMessages_) {
            HMBDC_LOG_N("adverise at ", m);
        }
    }

    auto const& advertisingMessages() const {
        return advertisingMessages_;
    }

protected:
    app::Config config_;
    TcpEpollFd serverFd_;
    sockaddr_in& serverAddr_;
    std::vector<TypeTagBackupSource> advertisingMessages_;
};


template <typename TypeTagBackupSource, typename BackupSendSession>
struct AsyncBackupSendServerT 
: BackupSendServerT<TypeTagBackupSource, BackupSendSession> {
    using Buffer = typename BackupSendSession::Buffer;
    AsyncBackupSendServerT(app::Config const& cfg
        , Buffer& buffer
        , time::Rater& rater
        , ToCleanupAttQueue& toCleanupAttQueue
        , TypeTagSet& outboundSubscriptions
    ) : BackupSendServerT<TypeTagBackupSource, BackupSendSession>(cfg)
    , buffer_(buffer) 
    , rater_(rater) 
    , maxSendBatch_(cfg.getExt<uint32_t>("maxSendBatch")) 
    , toCleanupAttQueue_(toCleanupAttQueue) 
    , replayHistoryForNewRecv_(cfg.getExt<size_t>("replayHistoryForNewRecv"))
    , outboundSubscriptions_(outboundSubscriptions) {
        leastIt_.seq_ = std::numeric_limits<HMBDC_SEQ_TYPE>::max();
    }

    virtual ~AsyncBackupSendServerT() = default;

    void runOnce() HMBDC_RESTRICT {
        // utils::EpollTask::instance().poll();
        doAccept();
        typename Buffer::iterator leastIt;
        auto newSeq_= buffer_.readSeq(0);
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (hmbdc_unlikely(!(*it)->runOnce())) {
                (*it)->stop();
                sessions_.erase(it++);
            } else {
                if (newSeq_ > (*it)->bufItNext_.seq_) {
                    newSeq_ = (*it)->bufItNext_.seq_;
                }
                it++;
            }
        }
        if (hmbdc_likely(replayHistoryForNewRecv_ < newSeq_)) {
            newSeq_ -= replayHistoryForNewRecv_;
        } else {
            newSeq_ = 0;
        }
        if (leastIt_.seq_ != newSeq_) {
            while (toCleanupAttQueue_.size()) {
                auto it = toCleanupAttQueue_.begin();
                if (newSeq_ > std::get<0>(*it)) {
                    auto f = std::get<2>(*it);
                    if (f) f(std::get<1>(*it));
                    toCleanupAttQueue_.pop_front();
                } else {
                    break;
                }
            }
            buffer_.catchUpTo(1, newSeq_);
        }
        leastIt_.seq_ = newSeq_;
    }

    void stop() {
        while (toCleanupAttQueue_.size()) {
            auto it = toCleanupAttQueue_.begin();
            auto f = std::get<2>(*it);
            if (f) f(std::get<1>(*it));
            toCleanupAttQueue_.pop_front();
        }
    }

    size_t sessionCount() const {
        return sessions_.size();
    }

    size_t readySessionCount() HMBDC_RESTRICT const {
        size_t res = 0;
        for (auto const& s : sessions_) {
            if (s->ready()) res++;
        }
        return res;
    }

    void killSlowestSession() {
        if (sessions_.size() == 0) {
            return;
        }

        auto it = sessions_.begin();
        auto leastSessIt = it;
        typename Buffer::iterator leastIt = (*it++)->bufItNext_;
        for (; it != sessions_.end(); it++) {
            if ((*it)->bufItNext_ < leastIt) {
                leastIt = (*it)->bufItNext_;
                leastSessIt = it;
            }
        }
        HMBDC_LOG_C((*leastSessIt)->id(), " too slow, dropping");
        (*leastSessIt)->stop();
        sessions_.erase(leastSessIt);
    }

private:
    void doAccept() HMBDC_RESTRICT {
        auto& serverFd_ = this->serverFd_;
        auto& serverAddr_ = this->serverAddr_;
        if (hmbdc_unlikely(serverFd_.isFdReady())) {
            auto addrlen = sizeof(serverAddr_);
            auto conn = accept(serverFd_.fd, (struct sockaddr *)&serverAddr_, (socklen_t*)&addrlen);
            if (conn == -1) {
                if (!serverFd_.checkErr()) {
                    HMBDC_LOG_C("accept failure, errno=", errno);
                }
                return;
            }
            auto sz = this->config_.template getExt<int>("tcpSendBufferBytes");
            if (sz) {
                if (setsockopt(conn, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz)) < 0) {
                    HMBDC_LOG_C("failed to set send buffer size=", sz, " errno=", errno);
                }
            }
            int flag = this->config_.template getExt<bool>("nagling")?0:1;
            if (setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, (char*) &flag, sizeof(flag)) < 0) {
                HMBDC_LOG_C("failed to set TCP_NODELAY, errno=", errno);
            }
            try { 
                typename Buffer::iterator it;
                buffer_.peek(1, it, it, 0);
                auto s = std::make_shared<BackupSendSession>(conn
                    , buffer_
                    , rater_
                    , it
                    , maxSendBatch_
                    , outboundSubscriptions_);
                s->start();
                sessions_.insert(s);
            } catch (std::exception const& e) {
                HMBDC_LOG_C(e.what());
            }
        };
    }

    using Sessions = std::unordered_set<typename BackupSendSession::ptr>;
    Sessions sessions_;
    Buffer& HMBDC_RESTRICT buffer_; 
    hmbdc::time::Rater& HMBDC_RESTRICT rater_;
    typename Buffer::iterator leastIt_;
    size_t maxSendBatch_;
    ToCleanupAttQueue& toCleanupAttQueue_;
    size_t replayHistoryForNewRecv_;
    TypeTagSet& outboundSubscriptions_;
};

} //backupsendservert_detail

template <typename TypeTagBackupSource, typename BackupSendSession>
using AsyncBackupSendServerT = backupsendservert_detail::AsyncBackupSendServerT<TypeTagBackupSource, BackupSendSession>;

}}}
