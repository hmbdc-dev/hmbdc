#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/app/utils/EpollTask.hpp"
#include "hmbdc/app/Config.hpp"
#include "hmbdc/comm/inet/Misc.hpp"
#include "hmbdc/time/Timers.hpp"
#include "hmbdc/Exception.hpp"

#include <memory>
#include <string>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

namespace hmbdc { namespace tips { namespace udpcast {

using Config = hmbdc::app::Config;
struct EpollFd : app::utils::EpollFd {
    EpollFd(Config const& cfg) {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(fd < 0) {
            HMBDC_THROW(std::runtime_error, "failed to create socket");
        }
        if (cfg.getExt<bool>("multicastBoundToIface")) {
            auto iface =
                comm::inet::getLocalIpMatchMask(cfg.getExt<std::string>("ifaceAddr"));
            struct in_addr localInterface;
            localInterface.s_addr = inet_addr(iface.first.c_str());
            if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF
                , (char *)&localInterface, sizeof(localInterface)) < 0) {
                HMBDC_THROW(std::runtime_error, "failed to set ifaceAddr " << cfg.getExt<std::string>("ifaceAddr"));
            }

            struct ifreq ifr = {{0}};
            strcpy(ifr.ifr_name, iface.second.c_str());
            if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) < 0) {
                HMBDC_THROW(std::runtime_error, "failed to set SO_BINDTODEVICE " << iface.second
                    << " for ifaceAddr " << cfg.getExt<std::string>("ifaceAddr"));
            }
        }
    }
};

struct Transport : EpollFd {
    using ptr = std::shared_ptr<Transport>;

    Transport(Config const& cfg)
    : EpollFd(cfg)
    , config_(cfg)
    , mtu_(config_.getExt<size_t>("mtu")) {
        mtu_ -= (8u + 20u);  // 8bytes udp header and 20bytes ip header
        cfg (hmbdcName_, "hmbdcName")
            (schedPolicy_, "schedPolicy")
            (schedPriority_, "schedPriority")
        ;
    }

    std::tuple<char const*, int> schedSpec() const {
        return std::make_tuple(this->schedPolicy_.c_str(), this->schedPriority_);
    }

    bool operator == (Transport const& other ) const {
        return &config_ == &other.config_;
    }

    bool operator < (Transport const& other ) const {
        return &config_ < &other.config_;
    }

    Transport (Transport const&) = delete;
    Transport& operator = (Transport const&) = delete;
    virtual ~Transport() = default;

protected:
    char const* hmbdcName() const { 
        return this->hmbdcName_.c_str();
    }
    std::string hmbdcName_;
    std::string schedPolicy_;
    int schedPriority_;
    Config const config_;
    size_t mtu_;
};

}}}
