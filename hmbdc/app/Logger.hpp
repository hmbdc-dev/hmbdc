#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/time/Time.hpp"
#include "hmbdc/pattern/GuardedSingleton.hpp"
#include  <ostream>
#include  <mutex>


#ifdef HMBDC_RUNTIME_DEBUG
#define HMBDC_LOG_R(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_R(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[1], __VA_ARGS__, hmbdc::app::LogTrailer(__FILE__, __LINE__))
#define HMBDC_LOG_r(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_R(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[1], __VA_ARGS__, '\n')
#else
#define HMBDC_LOG_R(...)
#define HMBDC_LOG_r(...)
#endif

#define HMBDC_LOG_D(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_D(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[0], __VA_ARGS__, hmbdc::app::LogTrailer(__FILE__, __LINE__))
#define HMBDC_LOG_N(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_N(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[2], __VA_ARGS__, hmbdc::app::LogTrailer(__FILE__, __LINE__))
#define HMBDC_LOG_W(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_W(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[3], __VA_ARGS__, hmbdc::app::LogTrailer(__FILE__, __LINE__))
#define HMBDC_LOG_C(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_C(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[4], __VA_ARGS__, hmbdc::app::LogTrailer(__FILE__, __LINE__))
#define HMBDC_LOG_DEBUG(x) 		if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_D(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[0], #x "=", x,   hmbdc::app::LogTrailer(__FILE__, __LINE__))
#define HMBDC_LOG_NOTICE(x)		if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_N(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[2], #x "=", x,   hmbdc::app::LogTrailer(__FILE__, __LINE__))
#define HMBDC_LOG_WARNING(x)	if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_W(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[3], #x "=", x,   hmbdc::app::LogTrailer(__FILE__, __LINE__))
#define HMBDC_LOG_CRITICAL(x)	if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_C(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[4], #x "=", x,   hmbdc::app::LogTrailer(__FILE__, __LINE__))
#define HMBDC_LOG_d(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_D(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[0], __VA_ARGS__, '\n')
#define HMBDC_LOG_n(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_N(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[2], __VA_ARGS__, '\n')
#define HMBDC_LOG_w(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_W(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[3], __VA_ARGS__, '\n')
#define HMBDC_LOG_c(...)   if (hmbdc::app::SyncLogger::initialized()) hmbdc::app::SyncLogger::instance().LOG_C(hmbdc::time::SysTime::now(), hmbdc::app::g_SyncLogLevelStr[4], __VA_ARGS__, '\n')

#define HMBDC_LOG_ONCE(...) {static bool done = false; if (!done) {done = true; {__VA_ARGS__}}}

namespace hmbdc { namespace app {

char const g_SyncLogLevelStr[][12 + 1] = {
    " DEBUG   :  ", 
    " RDEBUG  :  ", 
    " NOTICE  :  ", 
    " WARNING :  ", 
    " CRITICAL:  "
};


struct LogTrailer {
	LogTrailer(char const* const file,  int line)
	: f(file)
	, l(line) {
	}
    char const* const f;
	int l;

	friend std::ostream& operator << (std::ostream& os, LogTrailer const& t) {
		os << ' ' << t.f << ':' << t.l << std::endl;
		return os;
	}
};
struct EmptyLogTrailer {
	friend std::ostream& operator << (std::ostream& os, EmptyLogTrailer const&) {
		os << std::endl;
		return os;
	}
};

/**
 * @brief a very straightforward logger that works synchronisely.
 * @details Only use the macrs defined above. Only use for light logging
 * refer to utils::AsyncLoggerT for heavy logging
 */
struct SyncLogger 
: pattern::GuardedSingleton<SyncLogger> {
	friend struct pattern::SingletonGuardian<SyncLogger>;

	enum Level {
		L_DEBUG = 0,
		L_RDEBUG,
		L_NOTICE,
		L_WARNING,
		L_CRITICAL,
		L_OFF
	};

    void setMinLogLevel(Level minLevel) {
    	minLevel_ = minLevel;
    }

	template <typename ...Args>
	void LOG_D(Args&&... args) {
#ifndef NDEBUG		
		if (minLevel_ <= L_DEBUG) {
			std::lock_guard<std::recursive_mutex> g(mutex_);
			log(std::forward<Args>(args)...);
		}
#endif		 
	}

	template <typename ...Args>
	void LOG_R(Args&&... args) {
#ifdef HMBDC_RUNTIME_DEBUG
		if (minLevel_ <= L_RDEBUG) {
		    std::lock_guard<std::recursive_mutex> g(mutex_);
			log(std::forward<Args>(args)...);
		}
#endif		
	} 

	template <typename ...Args>
	void LOG_N(Args&&... args) {
		if (minLevel_ <= L_NOTICE) {
		    std::lock_guard<std::recursive_mutex> g(mutex_);
			log(std::forward<Args>(args)...);
		}
	} 
	template <typename ...Args>
	void LOG_W(Args&&... args) {
		if (minLevel_ <= L_WARNING) {
		    std::lock_guard<std::recursive_mutex> g(mutex_);
			log(std::forward<Args>(args)...);
		}
	}
	template <typename ...Args>
	void LOG_C(Args&&... args) {
		if (minLevel_ <= L_CRITICAL) {
		    std::lock_guard<std::recursive_mutex> g(mutex_);
			log(std::forward<Args>(args)...);
		}
	}

private:
	template <typename Arg, typename ...Args>
	void log(Arg&& arg, Args&&... args) {
		log_ << std::forward<Arg>(arg);
		log(std::forward<Args>(args)...);
	}
	void log() {
	}
	template <typename ... NoOpArgs>
	SyncLogger(std::ostream& log, NoOpArgs&&...)
	: log_(log)
#ifndef NDEBUG
	, minLevel_(L_DEBUG) 
#else
	, minLevel_(L_NOTICE)
#endif	
	{}
    std::ostream& log_;
	Level minLevel_;
    std::recursive_mutex mutex_;
};
}}
