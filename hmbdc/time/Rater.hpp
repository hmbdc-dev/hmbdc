#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/Exception.hpp"
#include "hmbdc/time/Time.hpp"

#include <iostream>
#include <stdexcept>

namespace hmbdc { namespace time {
struct Rater {
    Rater()
    : drop_()
    , bucket_()
    , currentLevel_()
    , previousCheck_()
    , enabled_(false)
    {}

    Rater(Duration duration, size_t times, size_t burst, bool enabled = true)
    : drop_(duration / (times?times:1ul))
    , bucket_(drop_ * burst)
    , currentLevel_(bucket_)
    , previousCheck_()
    , enabled_(enabled?times != 0:false)
    {}

    bool check(size_t n = 1u) {
        if (!enabled_) return true;
        n_ = n;
        auto now = SysTime::now();     
        currentLevel_ += now - previousCheck_;
        previousCheck_ = now;
        if (currentLevel_ > bucket_) currentLevel_ = bucket_;
        return currentLevel_ >= drop_ * n;
    }

    void commit() {
        currentLevel_ -= drop_ * n_;
    }

    bool enabled() const {
        return enabled_;
    }

private:
    Duration drop_;
    Duration bucket_;
    Duration currentLevel_;
    SysTime  previousCheck_;
    bool enabled_;
    size_t n_;
};
}}
