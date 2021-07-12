#include "hmbdc/Copyright.hpp"
#pragma once

#include <sstream>

namespace hmbdc { namespace text { 
inline
uint32_t 
ipStr2Uint32(char const* ipStr) {
    uint32_t byte1, byte2, byte3, byte4;
    char dot;
    std::istringstream s(ipStr);
    s >> byte1 >> dot >> byte2 >> dot >> byte3 >> dot >> byte4 >> dot;
    auto res = byte1;
    res = (res << 8u) + byte2;
    res = (res << 8u) + byte3;
    res = (res << 8u) + byte4;
    return res;
}

}}
