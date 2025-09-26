#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/utils/EpollTask.hpp"
#include "hmbdc/app/Config.hpp"
#include "hmbdc/comm/inet/Misc.hpp"
#include <string>
#include <stdexcept>

#include <sys/types.h>         
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
       
namespace hmbdc { namespace tips { namespace reliable {
struct TcpEpollFd : app::utils::EpollFd {
    TcpEpollFd(app::Config const& cfg)
    : localAddr{0} {
        using std::string;
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if(fd < 0) {
            HMBDC_THROW(std::runtime_error, "failed to create socket, errno=" << errno);
        }

        if (cfg.getExt<bool>("tcpKeepAlive")) {
            int keepAlive = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(keepAlive)) < 0) {
                HMBDC_THROW(std::runtime_error, "failed to set KEEPALIVE, errno=" << errno);
            }
        }

        int flags = fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        if (fcntl(fd, F_SETFL, flags) < 0) {
            HMBDC_THROW(std::runtime_error, "fcntl failed errno=" << errno);
        }

        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = inet_addr(
            hmbdc::comm::inet::getLocalIpMatchMask(
                cfg.getExt<string>("tcpIfaceAddr") == string("ifaceAddr")
                ?cfg.getExt<string>("ifaceAddr"):cfg.getExt<string>("tcpIfaceAddr")
            ).first.c_str()
        );
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
}}}
