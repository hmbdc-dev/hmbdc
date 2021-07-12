#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/app/Base.hpp"
#include "hmbdc/text/LfbStream.hpp"
#include "hmbdc/time/Time.hpp"
#include <string>
#include <memory>
#include <tuple>

#define HMBDC_ALOG_D(...)   hmbdc::app::utils::AsyncLoggerT<HMBDC_ALOG_CONTEXT>::instance().LOG_D(__VA_ARGS__, LogTrailer(__FILE__, __LINE__))
#define HMBDC_ALOG_N(...)   hmbdc::app::utils::AsyncLoggerT<HMBDC_ALOG_CONTEXT>::instance().LOG_N(__VA_ARGS__, LogTrailer(__FILE__, __LINE__))
#define HMBDC_ALOG_W(...)   hmbdc::app::utils::AsyncLoggerT<HMBDC_ALOG_CONTEXT>::instance().LOG_W(__VA_ARGS__, LogTrailer(__FILE__, __LINE__))
#define HMBDC_ALOG_C(...)   hmbdc::app::utils::AsyncLoggerT<HMBDC_ALOG_CONTEXT>::instance().LOG_C(__VA_ARGS__, LogTrailer(__FILE__, __LINE__))
#define HMBDC_ALOG_DEBUG(x) hmbdc::app::utils::AsyncLoggerT<HMBDC_ALOG_CONTEXT>::instance().LOG_D(#x "=", x,   LogTrailer(__FILE__, __LINE__))

#define HMBDC_ALOG_d(...)   hmbdc::app::utils::AsyncLoggerT<HMBDC_ALOG_CONTEXT>::instance().LOG_D(__VA_ARGS__, EmptyLogTrailer())
#define HMBDC_ALOG_n(...)   hmbdc::app::utils::AsyncLoggerT<HMBDC_ALOG_CONTEXT>::instance().LOG_N(__VA_ARGS__, EmptyLogTrailer())
#define HMBDC_ALOG_w(...)   hmbdc::app::utils::AsyncLoggerT<HMBDC_ALOG_CONTEXT>::instance().LOG_W(__VA_ARGS__, EmptyLogTrailer())
#define HMBDC_ALOG_c(...)   hmbdc::app::utils::AsyncLoggerT<HMBDC_ALOG_CONTEXT>::instance().LOG_C(__VA_ARGS__, EmptyLogTrailer())

namespace hmbdc { namespace app { namespace utils {

char const g_LogLevelStr[][12] = {
    "debug :    ", 
    "notice:    ", 
    "warning:   ", 
    "critical:  "
};

/**
 * @class AsyncLoggerT<>
 * @brief a high performance async logger that doesn't penalize logging threads as much
 * when the logging load is heavy
 * @details see @example hmbdc-log.cpp
 * 
 */
template <typename Ctx>
struct AsyncLoggerT
: Client<AsyncLoggerT<Ctx>, LoggingT<Ctx::MAX_MESSAGE_SIZE>>
, text::lfb_stream::OStringStream<typename Ctx::Buffer
    , MessageWrap<LoggingT<Ctx::MAX_MESSAGE_SIZE>>
    , LoggingT<Ctx::MAX_MESSAGE_SIZE>::typeTag
  >
, pattern::GuardedSingleton<AsyncLoggerT<Ctx>> {
	static_assert(Ctx::MAX_MESSAGE_SIZE != 0
		, "HMBDC_ALOG_CONTEXT needs to be 'compile time sized'");
	using Logging = LoggingT<Ctx::MAX_MESSAGE_SIZE>;
	friend struct pattern::SingletonGuardian<AsyncLoggerT<Ctx>>;

	enum Level {
		L_DEBUG = 0,
		L_NOTICE,
		L_WARNING,
		L_CRITICAL,
		L_OFF
	};
private:
	std::ostream& log_;
	static std::unique_ptr<Ctx> pCtx_s; //Logger is a singleton
	Ctx& ctx_;
	Level minLevel_;
	std::string schedPolicy_;
	int priority_;

	using Stream = text::lfb_stream::OStringStream<typename Ctx::Buffer
	    , MessageWrap<Logging>, Logging::typeTag>;

	AsyncLoggerT(std::ostream& log
		, uint64_t cpuAffinityMask = 0
		, char const* schedPolicy = "SCHED_IDLE"
		, int priority = 0
		, uint16_t logBufferSizePower2Num = 8)
	: Stream((pCtx_s = std::unique_ptr<Ctx>(new Ctx(logBufferSizePower2Num)))->buffer())
	, log_(log)
	, ctx_(*pCtx_s)
	, minLevel_(L_DEBUG)
	, schedPolicy_(schedPolicy)
	, priority_(priority) {
		ctx_.start(*this
			, cpuAffinityMask?cpuAffinityMask:(1ul << std::thread::hardware_concurrency()) - 1ul);
	}

	~AsyncLoggerT() {
		if (pCtx_s) {
			pCtx_s->stop();
			pCtx_s->join();
		}
	}

	AsyncLoggerT(std::ostream& log, Ctx& ctx
		, char const* schedPolicy = "SCHED_IDLE"
		, int priority = 0)
	: Stream(ctx.buffer())
	, log_(log)
	, ctx_(ctx)
	, minLevel_(L_DEBUG)
	, schedPolicy_(schedPolicy)
	, priority_(priority) {
	}

	struct LogHeader {
		LogHeader(Level level)
		: l(level)
		, ts(hmbdc::time::SysTime::now()) {
		}

		Level l;
		hmbdc::time::SysTime ts;

		friend std::ostream& operator << (std::ostream& os, LogHeader const& h) {
			os << h.ts << " " << g_LogLevelStr[h.l];
			return os;
		}
	};

public:
    char const* hmbdcName() const { return "logger"; }
    void setMinLogLevel(Level minLevel) {
    	minLevel_ = minLevel;
    }

    std::tuple<char const*, int> schedSpec() const {
        return std::make_tuple(schedPolicy_.c_str(), priority_);
    }

	template <typename ...Args>
	void LOG_D(Args&&... args) {
#ifndef NDEBUG
		Stream::operator()(LogHeader(L_DEBUG), std::forward<Args>(args)...);
#endif
	}

	template <typename ...Args>
	void LOG_N(Args&&... args) {
		if (minLevel_ <= L_NOTICE)
			Stream::operator()(LogHeader(L_NOTICE), std::forward<Args>(args)...);
	}
	template <typename ...Args>
	void LOG_W(Args&&... args) {
		if (minLevel_ <= L_WARNING)
		    Stream::operator()(LogHeader(L_WARNING), std::forward<Args>(args)...);
	}
	template <typename ...Args>
	void LOG_C(Args&&... args) {
		if (minLevel_ <= L_CRITICAL)
			Stream::operator()(LogHeader(L_CRITICAL), std::forward<Args>(args)...);
	}

	void handleMessageCb(Logging& logItem) {
		Stream::dump(log_, logItem);
	}
};

template <typename Ctx>
std::unique_ptr<Ctx> AsyncLoggerT<Ctx>::pCtx_s;

}}}
