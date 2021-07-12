#include "hmbdc/Copyright.hpp"
#pragma once

#include <functional>

namespace hmbdc { namespace comm { namespace inet {

template <typename asio_endpoint>
struct HashEndpoint {
	typedef asio_endpoint argument_type;
    typedef std::size_t result_type;
    result_type operator()(argument_type const& e) const noexcept {
	    uint64_t v = e.port();
	    v += ((e.address().to_v4().to_ulong()) << 32u);
	    return std::hash<uint64_t>{}(v);
    }
};

template <typename sockaddr_in>
struct HashSockAddrIn {
	typedef sockaddr_in argument_type;
    typedef std::size_t result_type;
    result_type operator()(sockaddr_in const& e) const noexcept {
	    uint64_t v = e.sin_port;
	    v <<= 32u;
	    v += e.sin_addr.s_addr;
	    return std::hash<uint64_t>{}(v);
    }
};

template <typename sockaddr_in>
struct SockAddrInEqual {
	bool operator ()(sockaddr_in const& a, sockaddr_in const& b) const {
		return a.sin_addr.s_addr == b.sin_addr.s_addr &&
			a.sin_port == b.sin_port;
	}
};
}}}

