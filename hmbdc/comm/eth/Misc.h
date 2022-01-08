#include "hmbdc/Copyright.hpp"
#pragma once

#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>  

namespace hmbdc { namespace comm { namespace eth {
inline
uint32_t 
checksum(const void *data, uint16_t len, uint32_t sum) {
    auto addr = (const uint8_t *)data;
    uint32_t i;

        /* Checksum all the pairs of bytes first... */
        for (i = 0; i < (len & ~1U); i += 2) {
                sum += (u_int16_t)ntohs(*((u_int16_t *)(addr + i)));
                if (sum > 0xFFFF)
                        sum -= 0xFFFF;
        }
    /*
     * If there's a single byte left over, checksum it, too.
     * Network byte order is big-endian, so the remaining byte is
     * the high byte.
     */
    if (i < len) {
        sum += addr[i] << 8;
        if (sum > 0xFFFF)
            sum -= 0xFFFF;
    }
    return sum;
}

inline
uint16_t 
cksum_add(uint16_t sum, uint16_t a) {
    uint16_t res;

    res = sum + a;
    return (res + (res < a));
}

inline 
uint16_t 
wrapsum(uint32_t sum) {
    sum = ~sum & 0xFFFF;
    return (htons(sum));
}

struct virt_header {
	uint8_t fields[12];
} __attribute__((__packed__));

struct pkt {
	struct virt_header vh;
	struct ether_header eh;
	struct {
		struct ip ip;
		struct udphdr udp;
		uint8_t body[1];	
	} ipv4;
} __attribute__((__packed__, aligned (2)));

// template<int s> struct Wow;
// Wow<sizeof(pkt)> wow;
static_assert(sizeof(pkt) == 58u, "wierd that size is not 58?");

template <size_t N>
struct pkt_n {
    struct virt_header vh;
    struct ether_header eh;
    struct {
        struct ip ip;
        struct udphdr udp;
        uint8_t body[N];    
    } ipv4;

    operator pkt&() {
        return reinterpret_cast<pkt&>(*this);
    }

} __attribute__((__packed__));

}}}
