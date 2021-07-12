#include "hmbdc/Copyright.hpp"
#pragma once
#include <stdint.h>
#include <type_traits>

namespace hmbdc { namespace numeric {

template <uint64_t v>
struct set_bits_count {
    enum {
        value = set_bits_count<v/2u>::value + v % 2u
    };
};

template <>
struct set_bits_count<0> {
    enum {
        value = 0
    };
};


template <typename U>
uint16_t setBitsCount(U x) {
    static_assert(std::is_unsigned<U>::value, "");
	uint16_t c = 0;
    for (c = 0; x; c++, x &= x-1);
    return c;
}

template <typename U>
U nthSetBitFromLsb(U x, uint16_t n) {
    static_assert(std::is_unsigned<U>::value, "");
    U res;
    do {
    	res = x - (x & (x - 1));
    	x -= res;
    } while (n--);
    return res;
}


template <typename U>
uint16_t log2Upper(U x) {
    static_assert(std::is_unsigned<U>::value, "");
    uint16_t l = 0u;
    auto xx = x;
    while (xx >>= 1u) { ++l; }
    return (1ul << l) == x ? l : ++l;
}

template <typename U, typename Indexes>
U fromSetBitIndexes(Indexes const& indexes) {
    U res{0};
    for (auto i : indexes) {
        res |= (1u << i);
    }
    return res;
}
}}

