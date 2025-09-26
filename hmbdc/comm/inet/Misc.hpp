#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/Exception.hpp"

#include <string>
#include <stdexcept>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stdlib.h>       
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <utility>

struct msghdr;
namespace hmbdc { namespace comm { namespace inet {
/**
 * @brief resolve a local ip interface using a mask
 * 
 * @param mask in this format: "192.168.0.1/24" or "192.168.0.101" which is the same as "192.168.0.101/32"
 * when using 192.168.0.1/0
 * @param includeLoopback if it is false, the loopback address is excluded from the result
 * @return a local ip matches the mask and its interface name if no exception is thrown
 */
std::pair<std::string, std::string> getLocalIpMatchMask(std::string const& mask);
std::string getLocalIpThruName(std::string const& name);
std::pair<std::string, uint16_t> getPeerIpPort(int fd);
void extractRelicTo(msghdr& to, msghdr const& from, int sent);

inline
std::pair<std::string, std::string>
getLocalIpMatchMask(std::string const& mask) {
    bool includeLoopback = mask != "0.0.0.0/0";
    auto offset = mask.find('/');
    auto ip_part = mask;
    uint32_t maskLen = 32;

    if (offset != std::string::npos) {
        ip_part = mask.substr(0, offset);
        auto bit_part = mask.substr(offset + 1u);
        maskLen = atoi(bit_part.c_str());
    }
    uint32_t targetIp = inet_addr(ip_part.c_str());
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) { 
        HMBDC_THROW(std::runtime_error, " getifaddrs failed ");
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
        continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) {
            s = getnameinfo(ifa->ifa_addr,
                sizeof(struct sockaddr_in),
                host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                HMBDC_THROW(std::runtime_error, " getnameinfo failed ");
            }
            struct in_addr addr;
            if (inet_aton(host, &addr)
                && (maskLen == 0 
                    || (targetIp << (32u - maskLen)) == (addr.s_addr << (32u - maskLen)))
                && (includeLoopback || !(ifa->ifa_flags & IFF_LOOPBACK))) {
                freeifaddrs(ifaddr);
                return std::make_pair(std::string(host), std::string(ifa->ifa_name));
            }
        }
    }
    freeifaddrs(ifaddr);
    HMBDC_THROW(std::runtime_error, mask << " does not resolve locally");
}

inline
std::string 
getLocalIpThruName(std::string const& name) {
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        HMBDC_THROW(std::runtime_error, " getifaddrs failed ");
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
        continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET
            && strcmp(name.c_str(), ifa->ifa_name) == 0) {
            s = getnameinfo(ifa->ifa_addr,
                sizeof(struct sockaddr_in),
                host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                HMBDC_THROW(std::runtime_error, " getnameinfo failed ");
            }
            return std::string(host);
        }
    }
    HMBDC_THROW(std::runtime_error, name << " does not resolve locally");
}

inline
std::pair<std::string, uint16_t> 
getPeerIpPort(int fd) {
    sockaddr addr = {0};
    auto addrlen = sizeof(addr);
    if (getpeername(fd, &addr, (socklen_t*)&addrlen) == -1) {
        HMBDC_THROW(std::runtime_error, "invalid socket errno=" << errno);
    }
    sockaddr_in* p = (sockaddr_in *)&addr;
    char ipaddr[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &(p->sin_addr), ipaddr, INET_ADDRSTRLEN)) {
    } else {
        HMBDC_THROW(std::runtime_error, "no ip addr errno=" << errno);
    }
    return make_pair(std::string(ipaddr), p->sin_port);
}

inline
void 
extractRelicTo(msghdr& to, msghdr const& from, int sent) {
    auto l = size_t(sent);
    auto i = 0;
    for (; l > 0 && i < (int)from.msg_iovlen; ++i) {
        auto& msg = from.msg_iov[i];
        if (msg.iov_len <= l) {
            l -= msg.iov_len;
        } else {
            break;
        }
    }
    
    auto& msg = from.msg_iov[i];
    to.msg_iov[0].iov_base = (char*)msg.iov_base + l;
    to.msg_iov[0].iov_len = msg.iov_len - l;
    auto to_msg_iovlen = from.msg_iovlen - i;

    if (to_msg_iovlen > 1) { // make g++-12 happy
        memmove(to.msg_iov + 1
        , from.msg_iov + i + 1
        , (to_msg_iovlen - 1) * sizeof(iovec)
        );
    }
    to.msg_iovlen = to_msg_iovlen;
}
}}}
