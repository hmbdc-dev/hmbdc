#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/app/utils/EpollTask.hpp"
#include "hmbdc/app/Config.hpp"
#include "hmbdc/comm/inet/Misc.hpp"

#include <stdexcept>
#include <memory>
#include <string>
#include <stdexcept>

#include <sys/types.h>         
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

namespace hmbdc { namespace tips { namespace tcpcast {

struct EpollFd : app::utils::EpollFd {
    EpollFd(app::Config const& cfg)
    : localAddr{0} {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if(fd < 0) {
            HMBDC_THROW(std::runtime_error, "failed to create socket, errno=" << errno);
        }

        int flags = fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        if (fcntl(fd, F_SETFL, flags) < 0) {
            HMBDC_THROW(std::runtime_error, "fcntl failed errno=" << errno);
        }

        uint32_t yes = 1;
        if (setsockopt(fd, SOL_SOCKET,SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            HMBDC_LOG_C("failed to set reuse address errno=", errno);
        }

        if (cfg.getExt<bool>("tcpKeepAlive")) {
            int keepAlive = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(keepAlive)) < 0) {
                HMBDC_LOG_C("failed to set KEEPALIVE, errno=", errno);
            }
        }

        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = inet_addr(hmbdc::comm::inet::getLocalIpMatchMask(
            cfg.getExt<std::string>("ifaceAddr")).c_str());
        localAddr.sin_port = htons(cfg.getExt<uint16_t>("tcpPort"));
        if (::bind(fd, (sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
            HMBDC_THROW(std::runtime_error, "failed to bind, errno=" << errno);
        }

        auto addrLen = socklen_t(sizeof(localAddr));
        if (getsockname(fd, (sockaddr*)&localAddr, &addrLen) < 0) {
            HMBDC_THROW(std::runtime_error, "getsockname failure, errno=" << errno);
        }

        char ipaddr[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &(localAddr.sin_addr), ipaddr, INET_ADDRSTRLEN)) {
            HMBDC_THROW(std::runtime_error, "failed to inet_ntop, errno=" << errno);
        }
        localIp = ipaddr;
        localPort = htons(localAddr.sin_port);
    }

    sockaddr_in localAddr;
    std::string localIp;
    uint16_t localPort;
};

struct Transport {
    using ptr = std::shared_ptr<Transport>;

    Transport(app::Config const& cfg)
    : config_(cfg) {
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
    
    app::Config const config_;
    int schedPriority_;

private:
    std::string hmbdcName_;
    std::string schedPolicy_;
};
}}}
