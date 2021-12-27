#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rmcast/Transport.hpp"
#include "hmbdc/tips/udpcast/Transport.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/comm/inet/Endpoint.hpp"
#include "hmbdc//MetaUtils.hpp"

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

#include <memory>
#include <type_traits>

namespace hmbdc { namespace tips { namespace rmcast {

namespace mcrecvtransport_detail {

/**
 * @brief impl class
 * 
 * @tparam OutputBuffer type of buffer to hold resulting network messages
 * between different recv transport. By default, keeping all
 */
template <typename OutputBuffer, typename Ep2SessionDict>
struct McRecvTransport 
: Transport {
    using SELF = McRecvTransport;
    friend struct NetContext; //only be created by NetContext

    McRecvTransport(hmbdc::app::Config const& cfg
        , hmbdc::pattern::MonoLockFreeBuffer& cmdBuffer
        , TypeTagSet const& subscriptions
        , Ep2SessionDict& sessionDict)
	: Transport(cfg)
    , cmdBuffer_(cmdBuffer)
    , sendFrom_{0}
    , mcFd_(cfg)
    , buf_(new char[mtu_])
    , bufCur_(nullptr)
    , bytesRecved_(0)
    , subscriptions_(subscriptions)
    , sessionDict_(sessionDict) {
        uint32_t yes = 1;
        if (setsockopt(mcFd_.fd, SOL_SOCKET,SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            HMBDC_LOG_C("failed to set reuse address errno=", errno);
        }
        auto sz = config_.getExt<int>("udpRecvBufferBytes");
        if (sz) {
            if (setsockopt(mcFd_.fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz)) < 0) {
                HMBDC_LOG_C("failed to set send buffer size=", sz);
            }
        }

        /* bind to receive address */
        auto mcAddr = comm::inet::Endpoint(config_.getExt<std::string>("mcastAddr")
            , config_.getExt<uint16_t>("mcastPort")).v;
        if (::bind(mcFd_.fd, (struct sockaddr *)&mcAddr, sizeof(mcAddr)) < 0) {
            HMBDC_THROW(std::runtime_error, "failed to bind " 
                << config_.getExt<std::string>("mcastAddr") << ':'
                    << cfg.getExt<short>("mcastPort"));
        }
        /* use setsockopt() to request that the kernel join a multicast group */
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr=inet_addr(config_.getExt<std::string>("mcastAddr").c_str());
        auto iface = 
            comm::inet::getLocalIpMatchMask(config_.getExt<std::string>("ifaceAddr")); 
        mreq.imr_interface.s_addr=inet_addr(iface.c_str());
        if (setsockopt(mcFd_.fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            HMBDC_THROW(std::runtime_error, "failed to join " << config_.getExt<std::string>("mcastAddr") << ':'
                << cfg.getExt<short>("mcastPort"));
        }
    }

/**
 * @brief start the show by schedule the mesage recv
 */
    void start() {
        hmbdc::app::utils::EpollTask::instance().add(
            hmbdc::app::utils::EpollTask::EPOLLIN
                | hmbdc::app::utils::EpollTask::EPOLLET, mcFd_);
    }

/**
 * @brief power the io_service and other things
 * 
 */
    void runOnce() HMBDC_RESTRICT {
        resumeRead();
    }

private:
    void resumeRead() {
        do {
            if (hmbdc_unlikely(sendFrom_.sin_family != AF_INET)) {
                bytesRecved_ = 0;
            }
            if (hmbdc_likely(bytesRecved_)) {
                if (!bufCur_) {
                    bufCur_ = buf_;
                }
                while (bytesRecved_ >= sizeof(TransportMessageHeader)) {
                    auto h = reinterpret_cast<TransportMessageHeader*>(bufCur_);
                    auto wireSize = h->wireSize();
                    if (hmbdc_likely(bytesRecved_ >= wireSize)) {
                        if (hmbdc_unlikely(h->typeTag() == TypeTagBackupSource::typeTag)) {
                            auto it = cmdBuffer_.claim();
                            auto b = static_cast<app::MessageHead*>(*it);
                            size_t l = h->messagePayloadLen;
                            l = std::min(cmdBuffer_.maxItemSize(), l);
                            memcpy(b, h->payload(), l);
                            auto& bts = b->template get<TypeTagBackupSource>();
                            bts.sendFrom = sendFrom_;
                            cmdBuffer_.commit(it);
                        } else {
                            auto session = sessionDict_.find(sendFrom_);
                            if (hmbdc_unlikely(session != sessionDict_.end())) {
                                auto a = session->second->accept(h);
                                if (a == 0) {
                                    return; //wait for backup
                                } 
                            }
                        }
                        bytesRecved_ -= wireSize;
                        bufCur_ += wireSize;
                    } else {
                        break;
                    }
                }
            }
            bufCur_ = nullptr;
            bytesRecved_ = 0;
            auto addrLen = sizeof(sendFrom_);
            if (mcFd_.isFdReady()) {
                auto l = recvfrom(mcFd_.fd, buf_, mtu_, MSG_NOSIGNAL|MSG_DONTWAIT
                , (sockaddr*)&sendFrom_, (socklen_t*)&addrLen);
                if (hmbdc_unlikely(l < 0)) {
                    if (!mcFd_.checkErr()) {
                        HMBDC_LOG_C("recvmsg failed errno=", errno);
                    }
                    return;
                } else if (l == 0) {
                    //nothing to do now
                    return;
                }
                bytesRecved_ = l;
            }
        } while(bytesRecved_);
    }
    
    hmbdc::pattern::MonoLockFreeBuffer& HMBDC_RESTRICT cmdBuffer_;
    sockaddr_in sendFrom_;
    udpcast::EpollFd mcFd_;
    char* buf_;
    char* bufCur_;
    size_t bytesRecved_;
    TypeTagSet const& HMBDC_RESTRICT subscriptions_;
    Ep2SessionDict& HMBDC_RESTRICT sessionDict_;
};
} //mcrecvtransport_detail
template <typename OutputBuffer, typename Ep2SessionDict>
using McRecvTransport = mcrecvtransport_detail::McRecvTransport<OutputBuffer, Ep2SessionDict>;

}}}
