#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/Exception.hpp"
#include "hmbdc/Compile.hpp"
// #include "hmbdc/pattern/Typed.h"

#include <string>
#include <stdexcept>
#include <functional>
#include <algorithm>
#include <stdio.h>
#include <string.h>
    
namespace hmbdc { namespace text { 
struct TypedStringHash;
template <char const NAME[], uint16_t SIZE>
struct TypedString
{
private:
    using ThisT = TypedString<NAME, SIZE>;
    char v_[SIZE]; 
    friend struct std::hash<TypedString<NAME, SIZE>>;

public:
    using rawType = char[SIZE];
    enum{capacity = SIZE,};

    TypedString(){v_[0] = 0;}
    explicit TypedString(char const* s, size_t len = SIZE) {
#ifndef __clang__
#pragma GCC diagnostic push        
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
        snprintf(v_, std::min((size_t)SIZE, len + 1), "%s", s);
    }
    explicit TypedString(std::string const& s) {
        snprintf(v_, SIZE, "%s", s.c_str());
    }
#ifndef __clang__    
#pragma GCC diagnostic pop
#endif
    static constexpr char const* typeName() { return NAME; }
    char const* c_str() const { return v_; }
    bool operator == (ThisT const& other) const {
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
        s = TypedString(tmp);
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
    template <char const NAME[], uint16_t SIZE>
    struct hash<hmbdc::text::TypedString<NAME, SIZE>> {
        size_t operator()(const hmbdc::text::TypedString<NAME, SIZE>& x) const {
            uint32_t hash, i;
            for(hash = i = 0; i < SIZE && x.v_[i]; ++i)
            {
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
