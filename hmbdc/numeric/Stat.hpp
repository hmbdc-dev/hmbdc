#include "hmbdc/Copyright.hpp"
#pragma once

#include <ostream>
#include <cmath>
#include <limits>
#include <algorithm>

namespace hmbdc { namespace numeric {
template <typename T>
struct Stat {
    Stat()
    : min_(std::numeric_limits<T>::max())
    , max_(std::numeric_limits<T>::min())
    , sum_(0)
    , sumSq_(0)
    , sampleSize_(0ul)
    {}

    void add(T sample) {
        ++sampleSize_;
        sum_ += sample;
        sumSq_ += double(sample) * double(sample);
        min_ = std::min(min_, sample);
        max_ = std::max(max_, sample);
    }

    size_t sampleSize() const {
        return sampleSize_;
    }
    
    friend
    std::ostream& operator << (std::ostream& os, Stat const& st) {
        if (st.sampleSize_) {
            os << "mean=" << st.sum_ / st.sampleSize_ 
                << " std_dev=" << sqrt((st.sumSq_ - double(st.sum_) * double(st.sum_) / st.sampleSize_) / st.sampleSize_)
                << " min=" << st.min_ << " max=" << st.max_
                << " sampleSize=" << st.sampleSize_;
        } else {
            os << "NA sampleSize=" << st.sampleSize_;
        }
        return os;
    }
private:
    T min_;
    T max_;
    T sum_;
    double sumSq_;
    size_t sampleSize_;
};

}}


