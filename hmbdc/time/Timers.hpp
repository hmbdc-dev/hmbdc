#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/time/Time.hpp"
#include "hmbdc/Exception.hpp"
#include "hmbdc/Compile.hpp"

#include <functional>
#include <algorithm>

#include <stdexcept>
#include <ctime>

#include <boost/intrusive/set.hpp>


namespace hmbdc { namespace time {
struct TimerManager;
namespace timers_detail {
    void noop(TimerManager&, SysTime const&);
}

struct Timer
: boost::intrusive::set_base_hook<
    boost::intrusive::link_mode<boost::intrusive::normal_link>
    //the default safe_link appears too strict since many times the object is deleted
    //before the container(TimerManager) is deleted and assert fires unnecesarily in that case
> {
protected:
    SysTime getFireAt() const { return fireAt_; }

public:
    using Callback = std::function<void (TimerManager&, SysTime const&)>;
    Timer(Callback cb = timers_detail::noop)
    : armed(false)
    , callback_(cb)
    {}
    void setCallback(Callback cb) {
        callback_ = cb;
    }
    bool operator < (Timer const& other) const
        { return fireAt_ < other.fireAt_; }
    bool operator <= (SysTime const& t) const
        { return !(t < fireAt_); }

    bool scheduled() const {
        return armed;
    }

    Timer(Timer const&) = delete;
    Timer& operator = (Timer const&) = delete;
    virtual ~Timer() = default;

private:
    friend struct TimerManager;
    void fired(TimerManager& tm, SysTime const& now) {
        callback_(tm, now);
    }
    bool armed;
    SysTime fireAt_;
    Callback callback_;
    virtual void reschedule(TimerManager& tm, SysTime const& now) = 0;
};

namespace timers_detail {
inline
void noop(TimerManager&, SysTime const&)
{}

} //timers_detail

struct TimerManagerTrait{};

struct TimerManager : TimerManagerTrait {
    /**
     * @brief schedule the timer to start at a specific time
     * @details make sure the Timer is not already scheduled
     * otherwise undefined behavior
     * 
     * @param fireAt the firing time
     * @param timer the timer to fire
     */
    void schedule(SysTime fireAt, Timer& timer) {
        timer.fireAt_ = fireAt;
        timers_.insert(timer);
        timer.armed = true;
    }
    /**
     * @brief cancel a timer previously scheduled with the TimerManager
     * @details if not scheduled, no effect
     * 
     * @param timer to be canceled
     */
    void cancel(Timer& timer) {
        auto range = timers_.equal_range(timer);
        for (auto it = range.first; it != range.second; ++it) {
            if (&*it == &timer) {
                timers_.erase(it);
                timer.armed = false;
                return;
            }
        }
    }
    
    void checkTimers(time::SysTime);

    Duration untilNextFire() const {
        auto res = std::numeric_limits<Duration>::max();
        if (timers_.begin() != timers_.end()) {
            res = std::max(Duration(0), timers_.begin()->fireAt_ - SysTime::now());
        }
        return res;
    }
    
private:
    using Timers = boost::intrusive::multiset<Timer>;
    Timers timers_;
};


struct ReoccuringTimer : Timer {
    ReoccuringTimer(Duration const& interval, Callback callback = timers_detail::noop)
    : Timer(callback)
    , interval_(interval)
    {}
    void resetInterval(Duration d) {
        interval_ = d;
    }
private:
    Duration interval_;
    virtual void reschedule(TimerManager& tm, SysTime const& now)
        { tm.schedule(now + interval_, *this); };
};

struct DailyTimer : Timer {
    DailyTimer(Callback callback = timers_detail::noop)
    : Timer(callback)
    {}
private:
    void reschedule(TimerManager& tm, SysTime const& now) override { 
        Duration day = Duration::seconds(86400);
        SysTime newFireTime = getFireAt() + day;
        while (newFireTime < now) newFireTime += day;
        tm.schedule(newFireTime, *this); 
    };
};

struct OneTimeTimer : Timer {
    OneTimeTimer(Callback callback = timers_detail::noop)
    : Timer(callback)
    {}

private:
    void reschedule(TimerManager&, SysTime const&) override {};
};

inline
void 
TimerManager::
checkTimers(time::SysTime now) HMBDC_RESTRICT {
	for (auto it = timers_.begin(); it != timers_.end() && hmbdc_unlikely(*it <= now);) {
        if (hmbdc_unlikely((*it).armed)) {
            (*it).armed = false;
            (*it).fired(*this, now); //this could change timers_
            it = timers_.begin();
        } else {
        	it++;
        }
    }

    for (auto it = timers_.begin(); it != timers_.end() && hmbdc_unlikely(*it <= now);) {
        auto tmp = it++;
        if (!(*tmp).armed) {
            timers_.erase(tmp);
            (*tmp).reschedule(*this, now);
        } else {
            it++;
        }
    }
}

}}
