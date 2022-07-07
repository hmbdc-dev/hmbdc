#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/Exception.hpp"
#include "hmbdc/Compile.hpp"

#include <string>
#include <stdexcept>
#include <functional>
#include <algorithm>
#include <stdio.h>
#include <string.h>
    
namespace hmbdc { namespace text {
/**
 * @brief std::string is easy to use, but it is not transferable between two processes,
 * This is for a quick hack for that purpose if we can limit the max size of the string
 * 
 * @tparam SIZE max char size the class can hold, silencely truncked if over the limit
 */
template <uint16_t SIZE>
struct XferableString {
private:
    using ThisT = XferableString<SIZE>;
    char v_[SIZE]; 
    friend struct std::hash<XferableString<SIZE>>;

public:
    using rawType = char[SIZE];
    enum{capacity = SIZE,};

    XferableString(){v_[0] = 0;}
    XferableString(char const* s, size_t len = SIZE) {
#ifndef __clang__
#pragma GCC diagnostic push        
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
        snprintf(v_, std::min((size_t)SIZE, len + 1), "%s", s);
    }
    XferableString(std::string const& s) {
        snprintf(v_, SIZE, "%s", s.c_str());
    }
#ifndef __clang__    
#pragma GCC diagnostic pop
#endif

    operator std::string () const {
        return std::string(c_str());
    }

    char const* c_str() const { return v_; }
    auto operator == (ThisT const& other) const {
        return strncmp(v_, other.v_, SIZE) == 0;
    }
    bool operator != (ThisT const& other) const {
        return strncmp(v_, other.v_, SIZE) != 0;
    }
    bool operator < (ThisT const& other) const {
        return strncmp(v_, other.v_, SIZE) < 0;
    }
    friend
    std::ostream& operator << (std::ostream& os, ThisT const& s) {
        os << s.v_;
        return os;
    }
    friend
    std::istream& operator >> (std::istream& is, ThisT& s) {
        std::string tmp;
        is >> tmp;
        s = XferableString(tmp);
        return is;
    }

    void clear() { v_[0] = 0; }
    size_t size() const {
        char const* b = v_;
        return std::find(b, b + SIZE, '\x00') - b;
    }

    size_t copyTo(char* to) const {
        char const* b = v_;
        char * p = to;
        for (; p != to + capacity && *b;) {
            *(p++) = *(b++);
        }
        return p - to;
    }
};

}}

namespace std {
    template <uint16_t SIZE>
    struct hash<hmbdc::text::XferableString<SIZE>> {
        size_t operator()(const hmbdc::text::XferableString<SIZE>& x) const {
            uint32_t hash, i;
            for(hash = i = 0; i < SIZE && x.v_[i]; ++i) {
                hash += x.v_[i];
                hash += (hash << 10);
                hash ^= (hash >> 6);
            }
            hash += (hash << 3);
            hash ^= (hash >> 11);
            hash += (hash << 15);
            return hash;
        }
    };
};
