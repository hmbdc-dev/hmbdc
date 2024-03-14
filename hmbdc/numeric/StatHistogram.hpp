#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/Exception.hpp"
#include <map>
#include <vector>
#include <utility>
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <ext/mt_allocator.h>

namespace hmbdc { namespace numeric {
namespace stathistogram_detail {

struct StatHistogramBase {
    template <typename Hist>
    static 
    void display(std::ostream& os, Hist const& hist, size_t sampleSize
        , std::vector<float> percentages = {0, 1, 10, 50, 90, 99, 100}) {
        auto h = hist.report(percentages);
        for (auto i = 0u; i < percentages.size(); ++i) {
            os << percentages[i] << "%=" << h[i] << ',';
        }
        os << "sample=" << sampleSize;
    }

};

/**
 * @brief collect sample values and keep histogram for top percentages
 * @details top values are the smaller values
 * 
 * @tparam T value type that supports less than operator
 * @tparam DETAILED if false, the samples are kept in coarser grain and the class's 
 *         speed performance is better
 */
template <typename T, bool DETAILED = true>
struct StatHistogram
: private StatHistogramBase {
    StatHistogram()
    : threshold_(std::numeric_limits<T>::max())
    , worst_(std::numeric_limits<T>::lowest())
    , sampleSize_(0ul)
    {}

    explicit StatHistogram(T threshold)
    : threshold_(threshold)
    , worst_(std::numeric_limits<T>::lowest())
    , sampleSize_(0ul){}

    bool add(T sample) {
        ++sampleSize_;
        if (sample < threshold_)
            buckets_[sample]++;
        else
            buckets_[threshold_]++;

        if (sample > worst_) {
            worst_ = sample;
            return true;
        }
        return false;
    }

    size_t sampleSize() const {
        return sampleSize_;
    }
   
    StatHistogram<T>& operator += (StatHistogram<T> const& other) {
        if (threshold_ == other.threshold_) {
            for (auto const& v : other.buckets_) {
                buckets_[v.first] += v.second;
            }
            worst_ = std::max(worst_, other.worst_);
        } else {
            HMBDC_THROW(std::runtime_error, "histogram collection parameters mismatch - failed");
        }
        sampleSize_ += other.sampleSize_;
        return *this;
    }

    std::vector<T> report(std::vector<float> percentages 
        = {0, 1, 10, 50, 90, 99, 100}) const {
        std::vector<T> p(percentages.size());
        if (!buckets_.empty() && !p.empty()) {
            *p.begin() = buckets_.begin()->first;
            *p.rbegin() = worst_;
        }
        size_t count = 0;
        size_t perIndex = 1;
        for(auto& i : buckets_) {
            count += i.second;
            for (auto j = perIndex; j < percentages.size() - 1; ++j) {
                if (count * 100ul >= percentages[j] * sampleSize_) {
                    p[j] = i.first;
                    perIndex++;
                } else {
                    break;
                }
            }
        }

        return p;
    }

    void display(std::ostream& os
        , std::vector<float> percentages = {0, 1, 10, 50, 90, 99, 100}) const {
        StatHistogramBase::display(os, *this, sampleSize_, percentages);
    }
    
    friend
    std::ostream& operator << (std::ostream& os, StatHistogram const& hist) {
        hist.display(os);
        return os;
    }
private:

    using Buckets = std::map<T, size_t, std::less<T>
        , __gnu_cxx::__mt_alloc<std::pair<const T, size_t>>
    >;
    Buckets buckets_;
    T threshold_;
    T worst_;
    size_t sampleSize_;
};

template <typename T>
struct StatHistogram<T, false>
: private StatHistogramBase {
    StatHistogram(
        T thresholdMin
        , T thresholdMax
        , size_t bucketCount = 1000u)
    : thresholdMin_(thresholdMin)
    , thresholdMax_(thresholdMax)
    , best_(std::numeric_limits<T>::max())
    , worst_(std::numeric_limits<T>::lowest())
    , sampleSize_(0ul)
    , unit_((thresholdMax - thresholdMin) / bucketCount)
    , buckets_(bucketCount + 1) {
        if (thresholdMax <= thresholdMin) {
            HMBDC_THROW(std::runtime_error, "thresholdMax <= thresholdMin");
        }
    }

    int add(T sample) {
        ++sampleSize_;

        if (sample < thresholdMin_)
            buckets_[0]++;
        else if (sample < thresholdMax_)
            buckets_[(sample - thresholdMin_) / unit_]++;
        else
            buckets_[buckets_.size() - 1]++;

        auto res = 0;
        if (sample < best_) {
            best_ = sample;
            res = -1;
        } 
        if (sample > worst_) {
            worst_ = sample;
            res = 1;
        }

        return res;
    }

    size_t sampleSize() const {
        return sampleSize_;
    }
   
    StatHistogram<T, false>& operator += (StatHistogram<T, false> const& other) {
        if (thresholdMax_ == other.thresholdMax_ &&
            thresholdMin_ == other.thresholdMin_ &&
            buckets_.size() == other.buckets_.size()) {
            for (auto i = 0u; i < buckets_.size(); ++i) {
                buckets_[i] += other.buckets_[i];
            }
            worst_ = std::max(worst_, other.worst_);
            best_ =  std::min(best_, other.best_);
            sampleSize_ += other.sampleSize_;
        } else {
            HMBDC_THROW(std::runtime_error, "thresholds or bucketCount mismatch - failed");
        }
        return *this;
    }

    std::vector<T> report(std::vector<float> percentages 
        = {0, 1, 10, 50, 90, 99, 100}) const {

        std::vector<T> p(percentages.size());
        if (sampleSize_ && !p.empty()) {
            *p.begin() = best_;
            *p.rbegin() = worst_;
            size_t count = 0;
            auto val = thresholdMin_;
            size_t perIndex = 1;
            for(auto& i : buckets_) {
                count += i;
                val += unit_;
                for (auto j = perIndex; j < percentages.size() - 1; ++j) {
                    if (count * 100ul >= percentages[j] * sampleSize_) {
                        p[j] = std::min(val, worst_);
                        perIndex++;
                    } else {
                        break;
                    }
                }
            }
        }

        return p;
    }
    
    void display(std::ostream& os
        , std::vector<float> percentages = {0, 1, 10, 50, 90, 99, 100}) const {
        StatHistogramBase::display(os, *this, sampleSize_, percentages);
    }
    
    friend
    std::ostream& operator << (std::ostream& os, StatHistogram const& hist) {
        hist.display(os);
        return os;
    }

private:
    T thresholdMin_;
    T thresholdMax_;
    T best_;
    T worst_;
    size_t sampleSize_;
    using Buckets = std::vector<size_t>;
    T unit_;
    Buckets buckets_;
};

} //stathistogram_detail

template <typename T, bool DETAILED = true>
using StatHistogram = stathistogram_detail::StatHistogram<T, DETAILED>;
}}

