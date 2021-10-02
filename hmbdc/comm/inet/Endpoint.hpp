#pragma once

#include "hmbdc/Exception.hpp"

#include <string>
#include <iostream>
#include <algorithm>
#include <stdexcept>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>

namespace hmbdc { namespace comm { namespace inet {
struct Endpoint {
    sockaddr_in v = {0};
    Endpoint(){}
    explicit Endpoint(std::string const& ipPort) {
        char ip[64];
        uint16_t port;
        auto s = ipPort;
        std::replace(std::begin(s), std::end(s), ':', ' ');

        if (2 != sscanf(s.c_str(), "%s %" SCNu16, ip, &port)) {
            HMBDC_THROW(std::runtime_error, "incorrect address " << ipPort);
        }
        memset(&v, 0, sizeof(v));
        v.sin_family = AF_INET;
        v.sin_addr.s_addr = inet_addr(ip);
        v.sin_port = htons(port);
    }


    Endpoint(std::string const& ip, uint16_t port) {
        memset(&v, 0, sizeof(v));
        v.sin_family = AF_INET;
        v.sin_addr.s_addr = inet_addr(ip.c_str());
        v.sin_port = htons(port);
    }
    
    bool operator == (Endpoint const& other) const {
        return memcmp(&v, &other.v, sizeof(v)) == 0;
    }

    friend
    std::ostream& operator << (std::ostream& os, Endpoint& ep) {
        os << inet_ntoa(ep.v.sin_addr) << ":" << ntohs(ep.v.sin_port);
        return os;
    }

    friend
    std::istream& operator >> (std::istream& is, Endpoint& ep) {
        std::string ipPort;
        is >> ipPort
            ;
        if (is) {
            ep = Endpoint(ipPort);
        }
        return is;
    }
};
}}}

