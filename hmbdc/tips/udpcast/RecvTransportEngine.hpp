#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/udpcast/Transport.hpp"
#include "hmbdc/tips/udpcast/DefaultUserConfig.hpp"
#include "hmbdc/tips/TypeTagSet.hpp"
#include "hmbdc/pattern/MonoLockFreeBuffer.hpp"
#include "hmbdc//MetaUtils.hpp"

#include <boost/lexical_cast.hpp>

#include <memory>
#include <type_traits>
#include <mutex>

namespace hmbdc { namespace tips { namespace udpcast {

/**
 * @brief interface to power a multicast transport receiving functions
 */
struct RecvTransport : Transport {
    using Transport::Transport;
    virtual ~RecvTransport(){}
};

namespace recvtransportengine_detail {
using Buffer = hmbdc::pattern::MonoLockFreeBuffer;

/**
 * @class RecvTransportImpl<>
 * @brief impl class
 * 
 * @tparam OutputBuffer type of buffer to hold resulting network messages
 */
template <typename OutputBuffer>
struct RecvTransportImpl 
: RecvTransport {
    RecvTransportImpl(Config cfg
        , OutputBuffer& outputBuffer)
	: RecvTransport((cfg.setAdditionalFallbackConfig(Config{DefaultUserConfig})
        , cfg.resetSection("rx", false)))
    , outputBuffer_(outputBuffer)
    , maxItemSize_(outputBuffer.maxItemSize())
    , buf_(new char[mtu_])
    , bufCur_(buf_)
    , bytesRecved_(0) {
        uint32_t yes = 1;
        if (setsockopt(fd, SOL_SOCKET,SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            HMBDC_LOG_C("failed to set reuse address errno=", errno);
        }
        auto sz = config_.getExt<int>("udpRecvBufferBytes");
        if (sz) {
            if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz)) < 0) {
                HMBDC_LOG_C("failed to set send buffer size=", sz);
            }
        }

        auto udpcastListenPort = config_.getExt<uint16_t>("udpcastListenPort");
        struct sockaddr_in udpcastListenAddrPort;
        memset(&udpcastListenAddrPort, 0, sizeof(udpcastListenAddrPort));
        udpcastListenAddrPort.sin_family = AF_INET;
        auto ipStr = cfg.getExt<std::string>("udpcastListenAddr") == std::string("ifaceAddr")
            ? comm::inet::getLocalIpMatchMask(cfg.getExt<std::string>("ifaceAddr"))
            :cfg.getExt<std::string>("udpcastListenAddr");
        udpcastListenAddrPort.sin_addr.s_addr = inet_addr(ipStr.c_str());
        udpcastListenAddrPort.sin_port = htons(udpcastListenPort);
        if (::bind(fd, (struct sockaddr *)&udpcastListenAddrPort, sizeof(udpcastListenAddrPort)) < 0) {
            HMBDC_THROW(std::runtime_error, "failed to bind udpcast listen address " 
                << ipStr << ':' << cfg.getExt<short>("udpcastListenPort") << " errno=" << errno);
        }

        uint32_t tmp = sizeof(udpcastListenAddrPort);
        if (udpcastListenPort == 0
            && getsockname(fd, (struct sockaddr *)&udpcastListenAddrPort, &tmp)) {
            HMBDC_THROW(std::runtime_error, "failed getsockname for udpcast listen address " 
                << ipStr << ':' << cfg.getExt<short>("udpcastListenPort") << " errno=" << errno);
        }

        if ((udpcastListenAddrPort.sin_addr.s_addr & 0x000000F0) == 0xE0) {//multicast address
            /* use setsockopt() to request that the kernel join a multicast group */
            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = udpcastListenAddrPort.sin_addr.s_addr;
            auto iface = 
                comm::inet::getLocalIpMatchMask(config_.getExt<std::string>("ifaceAddr")); 
            mreq.imr_interface.s_addr=inet_addr(iface.c_str());
            if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
                HMBDC_THROW(std::runtime_error, "failed to join " << ipStr << ":"
                    << udpcastListenPort << " errno=" << errno);
            }
        }
        listenAddr_ = ipStr;
        listenPort_ = ntohs(udpcastListenAddrPort.sin_port);
        HMBDC_LOG_N("listen at ", listenAddr_+ ":" + std::to_string(listenPort_));

        start();
    }

    auto const& listenAddr() const {
        return listenAddr_;
    }

    auto listenPort() const {
        return listenPort_;
    }

    template <app::MessageTupleC Messages, typename CcNode>
    void subscribeFor(CcNode const& node, uint16_t mod, uint16_t res) {
        subscriptions_.markSubsFor<Messages>(node, mod, res, [](uint16_t){});
    }

    template <app::MessageC Message>
    void subscribe() {
        subscriptions_.add(Message::typeTag);
    }

    ~RecvTransportImpl() {
        delete [] buf_;
    }

/**
 * @brief start the show by schedule the mesage recv
 */
    void start() {
        hmbdc::app::utils::EpollTask::instance().add(
            hmbdc::app::utils::EpollTask::EPOLLIN
                | hmbdc::app::utils::EpollTask::EPOLLET, *this);
    }

    void runOnce(bool alwaysPoll = false) HMBDC_RESTRICT {
        if (hmbdc_unlikely(alwaysPoll || !isFdReady())) {
            hmbdc::app::utils::EpollTask::instance().poll();
        }
        resumeRead();
    }
   
    sockaddr_in udpcastListenRemoteAddr = {0};
private:
    void resumeRead() HMBDC_RESTRICT {
        do {
            while (bytesRecved_) {
                auto h = reinterpret_cast<TransportMessageHeader*>(bufCur_);
                auto wireSize = h->wireSize();

                if (hmbdc_likely(bytesRecved_ >= wireSize)) {
                    if (subscriptions_.check(h->typeTag())) {
                        if (hmbdc_unlikely(wireSize > bytesRecved_)) {
                            break;
                        }
                        auto l = std::min<size_t>(maxItemSize_, h->messagePayloadLen());
                        outputBuffer_.put(h->payload(), l);
                    }
                    bytesRecved_ -= wireSize;
                    bufCur_ += wireSize;
                } else {
                    break;
                }
            }
            bytesRecved_ = 0;
            bufCur_ = buf_;
            if (isFdReady()) {
                socklen_t addrLen = sizeof(udpcastListenRemoteAddr);
                auto l = recvfrom(fd, buf_, mtu_, MSG_NOSIGNAL|MSG_DONTWAIT
                    , (struct sockaddr *)&udpcastListenRemoteAddr, &addrLen);
                if (hmbdc_unlikely(l < 0)) {
                    if (!checkErr()) {
                        HMBDC_LOG_C("recvmmsg failed errno=", errno);
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

    OutputBuffer& outputBuffer_;
    size_t maxItemSize_;
    char* buf_;
    char* bufCur_;
    size_t bytesRecved_;
    TypeTagSet subscriptions_;
    std::string listenAddr_;
    uint16_t listenPort_;
};

template <typename OutputBuffer>
struct RecvTransportEngineImpl 
: RecvTransportImpl<OutputBuffer>
, hmbdc::app::Client<RecvTransportEngineImpl<OutputBuffer>> {
    using RecvTransportImpl<OutputBuffer>::RecvTransportImpl;
    using RecvTransportImpl<OutputBuffer>::hmbdcName;
    using RecvTransportImpl<OutputBuffer>::schedSpec;

    void rotate() {
        RecvTransportEngineImpl::invokedCb(0);
    }

/**
 * @brief power the io_service and other things
 * 
 */
    /*virtual*/ 
    void invokedCb(size_t) HMBDC_RESTRICT override  {
        RecvTransportImpl<OutputBuffer>::runOnce();
    }

    using Transport::hmbdcName;    
/**
 * @brief should not happen ever unless an exception thrown
 * 
 * @param e exception thown
 */
    /*virtual*/ 
    void stoppedCb(std::exception const& e) override {
        HMBDC_LOG_C(e.what());
    };

private:
};

} //recvtransportengine_detail

template <typename OutputBuffer>
using RecvTransportImpl = recvtransportengine_detail::RecvTransportImpl<OutputBuffer>;

template <typename OutputBuffer>
using RecvTransportEngine = recvtransportengine_detail::RecvTransportEngineImpl<OutputBuffer>;
 
}}}
