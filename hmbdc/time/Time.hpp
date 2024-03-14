#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/Endian.hpp"
#include <sys/time.h>
#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <limits>
#include <chrono>

namespace hmbdc { namespace time {
struct Duration;
struct SysTime
{
    SysTime() : nsecSinceEpoch_(0) 
    {}
    
    ///UTC as input
    explicit SysTime(int64_t sec, int64_t usec = 0, int64_t nsec = 0) {
        nsecSinceEpoch_ = sec * 1000000000l + usec*1000l + nsec;
    }
    
    static SysTime now() {
        struct timespec spec;
        clock_gettime(CLOCK_REALTIME, &spec);
        return SysTime(spec.tv_sec, 0, spec.tv_nsec);
    }

    int64_t nsecSinceEpoch() const { return nsecSinceEpoch_; }
    int64_t usecSinceEpoch() const { return nsecSinceEpoch_ / 1000l; }
    Duration sinceMidnight() const;
    SysTime previousMidnight() const;

    bool operator < (SysTime const& other) const {
        return nsecSinceEpoch_ < other.nsecSinceEpoch_;
    }

    bool operator <= (SysTime const& other) const {
        return nsecSinceEpoch_ <= other.nsecSinceEpoch_;
    }

    bool operator > (SysTime const& other) const {
        return nsecSinceEpoch_ > other.nsecSinceEpoch_;
    }

    bool operator >= (SysTime const& other) const {
        return nsecSinceEpoch_ >= other.nsecSinceEpoch_;
    }
    bool operator == (SysTime const& other) const {
        return nsecSinceEpoch_ == other.nsecSinceEpoch_;
    }

    bool operator != (SysTime const& other) const {
        return nsecSinceEpoch_ != other.nsecSinceEpoch_;
    }

    Duration operator - (SysTime const&) const;
    SysTime operator + (Duration const&) const;
    SysTime operator - (Duration const&) const;  

    explicit operator bool() const {
        return nsecSinceEpoch_;
    }

    auto toChrono() const {
        using R = std::chrono::time_point<std::chrono::system_clock>;
        return R{std::chrono::nanoseconds(nsecSinceEpoch())};
    }

    static auto fromChrono(auto time_point) {
        return hmbdc::time::SysTime{0, 0,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
            time_point.time_since_epoch()).count()};
    }
    
    static
    SysTime 
    fromYYYYMMDDhhmmSSmmmUtc(int64_t v) {
        struct tm v_tm = {0};
        v_tm.tm_year = static_cast<int>(v/10000000000000l);
        v %= 10000000000000l;
        v_tm.tm_year -= 1900;
        v_tm.tm_mon  = static_cast<int>(v/100000000000l);
        v %= 100000000000l;
        v_tm.tm_mon  -= 1;
        v_tm.tm_mday = static_cast<int>(v/1000000000l);
        v %= 1000000000l;
        v_tm.tm_hour = static_cast<int>(v/10000000l);
        v %= 10000000l;
        v_tm.tm_min  = static_cast<int>(v/100000l);
        v %= 100000l;
        v_tm.tm_sec  = static_cast<int>(v/1000l);
        v %= 1000l;
        v_tm.tm_isdst = -1;
        return SysTime(timegm(&v_tm), v * 1000l);
    }

    static
    SysTime 
    fromYYYYMMDDhhmmSSmmm(int64_t v) {
        struct tm v_tm = {0};
        v_tm.tm_year = static_cast<int>(v/10000000000000l);
        v %= 10000000000000l;
        v_tm.tm_year -= 1900;
        v_tm.tm_mon  = static_cast<int>(v/100000000000l);
        v %= 100000000000l;
        v_tm.tm_mon  -= 1;
        v_tm.tm_mday = static_cast<int>(v/1000000000l);
        v %= 1000000000l;
        v_tm.tm_hour = static_cast<int>(v/10000000l);
        v %= 10000000l;
        v_tm.tm_min  = static_cast<int>(v/100000l);
        v %= 100000l;
        v_tm.tm_sec  = static_cast<int>(v/1000l);
        v %= 1000l;
        v_tm.tm_isdst = -1;
        return SysTime(mktime(&v_tm), v * 1000l);
    }

    void toXmitEndian() {
        nsecSinceEpoch_ = Endian::toXmit(nsecSinceEpoch_);
    }

    void toNativeEndian() {
        toXmitEndian();
    }

private:
   int64_t nsecSinceEpoch_;
    friend std::ostream& operator << (std::ostream& os, SysTime const& t);
};

inline
std::ostream& operator << (std::ostream& os, SysTime const& t) {
    char buf[32];
    char buf2[32];
    tm ts;
    time_t sec = (time_t)(t.nsecSinceEpoch_ / 1000000000ll);
    localtime_r(&sec, &ts);
    strftime(buf, sizeof(buf), "%Y%m%d%H%M%S.", &ts);
    sprintf(buf2, "%09lld", t.nsecSinceEpoch_ % 1000000000ll);
    os << buf << buf2;
    return os;
}

struct Duration {
    Duration() : nsec_(0){}
    static Duration seconds(int64_t sec) {return Duration(sec);}

    static Duration milliseconds(int64_t msec) {return Duration(0, msec * 1000);}

    static Duration microseconds(int64_t usec) { return Duration(0, usec); }

    static Duration nanoseconds(int64_t nsec) { return Duration(0, 0, nsec); }

    explicit Duration(int64_t sec, int64_t usec = 0, int64_t nsec = 0) { 
        nsec_ = sec * 1000000000l + usec * 1000l + nsec;}

    int64_t seconds() const { return nsec_ / 1000000000l; }
    int64_t milliseconds() const { return nsec_ / 1000000l; }
    int64_t microseconds() const { return nsec_ / 1000l; }
    int64_t nanoseconds() const { return nsec_; }

    explicit operator bool() const {
        return nsec_;
    }

    explicit operator double() const {
        return (double)nsec_ / 1000000000.0;
    }

    bool operator < (Duration const& other) const { return nsec_ < other.nsec_; }
    bool operator > (Duration const& other) const { return nsec_ > other.nsec_; }
    bool operator == (Duration const& other) const { return nsec_ == other.nsec_; }
    bool operator != (Duration const& other) const { return nsec_ != other.nsec_; }
    bool operator >= (Duration const& other) const { return nsec_ >= other.nsec_; }
    bool operator <= (Duration const& other) const { return nsec_ <= other.nsec_; }

    Duration& operator += (Duration const& other) {
        nsec_ += other.nsec_;
        return *this;
    }

    Duration operator - () const {
        return nanoseconds(-nsec_);
    }

    Duration operator - (Duration const& other) const {
        return nanoseconds(nsec_ - other.nsec_);
    }

    Duration operator + (Duration const& other) const {
        return nanoseconds(nsec_ + other.nsec_);
    }

    Duration& operator -= (Duration const& other) {
        nsec_ -= other.nsec_;
        return *this;
    }    
    double operator / (Duration const& other) const {
        return (double)nsec_ / other.nsec_;
    }

    Duration operator * (int64_t m) const {
        return Duration(0, 0, nsec_ * m);
    }

    Duration operator / (int64_t const& d) const {
        return Duration(0, 0, nsec_ / d);
    }

    Duration operator % (Duration const& d) const {
        return Duration(0, 0, nsec_ % d.nsec_);
    }

    void toXmitEndian() {
        nsec_ = Endian::toXmit(nsec_);
    }

    void toNativeEndian() {
        toXmitEndian();
    }

    auto toChrono() const {
        using R = std::chrono::time_point<std::chrono::system_clock>::duration;
        return std::chrono::duration_cast<R>
            (std::chrono::nanoseconds(nanoseconds()));
    }

    static auto fromChrono(auto d) {    
        return hmbdc::time::Duration::nanoseconds(
            std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    }

private:
    friend struct SysTime;
    friend std::ostream& operator << 
        (std::ostream& os, Duration const& d);
    friend std::istream& operator >> 
        (std::istream& is, Duration& d);

    int64_t nsec_;
};

inline
Duration 
SysTime::
operator - (SysTime const& b) const {
    return Duration::nanoseconds(nsecSinceEpoch_ 
        - b.nsecSinceEpoch_);
}

inline
SysTime 
SysTime::
operator + (Duration const& d) const {
    return SysTime(0, 0, nsecSinceEpoch_ + d.nsec_);
}

inline
SysTime 
SysTime::
operator - (Duration const& d) const {
    return SysTime(0, 0, nsecSinceEpoch_ - d.nsec_);
}

inline
Duration 
SysTime::
sinceMidnight() const {
    return *this - previousMidnight();
}

inline
SysTime 
SysTime::
previousMidnight() const {
    tm ts;
    time_t sec = (time_t)(nsecSinceEpoch_ / 1000000000l);
    localtime_r(&sec, &ts);
    return SysTime(sec) - Duration(ts.tm_hour * 3600 + ts.tm_min * 60 + ts.tm_sec);
}

inline
SysTime&
operator += (SysTime & t, Duration const& d) { 
    t = t + d;
    return t; 
}

inline
SysTime& 
operator -= (SysTime & t, Duration const& d) { 
    t = t - d;
    return t; 
}

inline
std::ostream&
operator << (std::ostream& os, Duration const& d) {
    char buf[32];
    if (d.nsec_ >= 0) {
        sprintf(buf, "%09lld", d.nsec_ % 1000000000ll);
        os << d.nsec_ / 1000000000l << '.' << buf;
    } else {
        sprintf(buf, "%09lld", (-d.nsec_) % 1000000000ll);
        os << '-' << (-d.nsec_) / 1000000000l << '.' << buf;
    }

    return os;
}


inline
std::istream& 
operator >> (std::istream& is, Duration& d) {
    double t;
    is >> t;
    d.nsec_ = (int64_t)(t * 1000000000l);
    return is;
}    

}}

namespace std {
template <>
struct numeric_limits<hmbdc::time::Duration> : numeric_limits<int64_t> {
public:
    static hmbdc::time::Duration lowest() throw() 
        {return hmbdc::time::Duration::nanoseconds(numeric_limits<int64_t>::lowest());}
    static hmbdc::time::Duration min() throw() 
        {return hmbdc::time::Duration::nanoseconds(numeric_limits<int64_t>::min());}
    static hmbdc::time::Duration max() throw() 
        {return hmbdc::time::Duration::nanoseconds(numeric_limits<int64_t>::max());}
};
}
