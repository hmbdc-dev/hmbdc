#include "hmbdc/Copyright.hpp"
#pragma once
#include  <boost/endian/conversion.hpp>
#include  <iostream>

namespace hmbdc {
struct Endian {
private:
    static constexpr uint32_t i32 = 0x01020304;
    static constexpr uint8_t i32_1stbyte = (const uint8_t&)i32;
    static_assert(i32_1stbyte == 0x04 || i32_1stbyte == 0x01, "Cannot determine endianness!");
public:
    static constexpr bool little = i32_1stbyte == 0x04;
#ifdef HMBDC_XMIT_BIG_ENDIAN
    static constexpr bool match_xmit = !little;
#else
    static constexpr bool match_xmit = little;
#endif
    template <typename iType>
    static
    iType toXmit(iType v) {
        if (match_xmit) {
            return v;
        } else {
            return boost::endian::endian_reverse(v);
        }
    }
    template <typename iType>
    static
    iType toNative(iType v) {
        return toXmit(v);
    }
};

#pragma pack(push, 1)
template <typename T>
struct XmitEndian {
    using RawType = T;
    static_assert(sizeof(T) > 1, "not effective or apply");
    explicit XmitEndian(T v = 0)
    : vx_(Endian::match_xmit?v:boost::endian::endian_reverse(v)) {
    }

    operator T() const {
        return Endian::match_xmit?vx_:boost::endian::endian_reverse(vx_);
    }

    XmitEndian& operator = (T v) {
        if (Endian::match_xmit) {
            vx_ = v;
        } else {
            vx_ = boost::endian::endian_reverse(v);
        }
        return *this;
    }

    XmitEndian& operator += (T v) {
        if (Endian::match_xmit) {
            vx_ += v;
        } else {
            auto tmp = boost::endian::endian_reverse(vx_) + v;
            vx_ = boost::endian::endian_reverse(tmp);
        }
        return *this;
    }

    XmitEndian& operator -= (T v) {
        if (Endian::match_xmit) {
            vx_ -= v;
        } else {
            auto tmp = boost::endian::endian_reverse(vx_) - v;
            vx_ = boost::endian::endian_reverse(tmp);
        }
        return *this;
    }

    friend std::istream& operator >> (std::istream& is, XmitEndian& t) {
        T tmp;
        is >> tmp;
        t = tmp;
        return is;
    }

private:
    T vx_;
};
#pragma pack(pop)

#pragma pack(push, 1)
template <typename T, size_t N>
struct XmitEndianByteField {
    using RawType = T;
    static_assert(sizeof(T) >= N, "not effective or apply");
    XmitEndianByteField(){}
    explicit XmitEndianByteField(T v) {
        vx_ = v;
        if (!Endian::match_xmit) {
            for (auto i = 0; i < N / 2; ++i) 
                std::swap(vb_[i], vb_[N - i - 1]);
        }
    }

    operator T() const {
        return Endian::match_xmit?vx_:boost::endian::endian_reverse(
            vx_ << (sizeof(T) - N) * 8);
    }

    XmitEndianByteField& operator = (T v) {
        vx_ = v;
        if (!Endian::match_xmit) {
            for (auto i = 0u; i < N / 2; ++i) 
                std::swap(vb_[i], vb_[N - i - 1]);
        }
        return *this;
    }

    friend std::istream& operator >> (std::istream& is, XmitEndianByteField& f) {
        T tmp;
        is >> tmp;
        f = tmp;
        return is;
    }

private:
    union {
        uint8_t vb_[N];
        T vx_ : N*8;
    };
};
#pragma pack(pop)

// template <typename UT, typename T, uint8_t index, uint8_t bits>
// struct XmitEndianBitField {
//     static_assert(sizeof(UT) >= sizeof(T), "UT not big enough");
//     operator T() const {
//         return Endian::match_xmit && bits > 8?vx_:boost::endian::endian_reverse(vx_);
//     }

//     XmitEndianBitField& operator = (T v) {
//         if (!Endian::match_xmit && bits > 8) {
//             v &= (1u << bits) - 1;
//             auto utv = (UT)(boost::endian::endian_reverse(v));
//             vx_ = 0;
//             if (Endian::little) {
//                 *reinterpret_cast<UT*>(this) ^= (utv >> index);
//             } else {
//                 *reinterpret_cast<UT*>(this) ^= (utv << (sizeof(UT) * 8 - index - bits));
//             }
//         } else {
//             vx_ = (v & ((1u << bits) - 1));
//         }
//         return *this;
//     }

//     friend std::istream& operator >> (std::istream& is, XmitEndianBitField& f) {
//         T tmp;
//         is >> tmp;
//         f = tmp;
//         return is;
//     }

// private:
//     UT                  : index;
//     T vx_               : bits;
// } __attribute__((packed));
}
