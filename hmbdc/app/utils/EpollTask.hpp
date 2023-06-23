#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/pattern/GuardedSingleton.hpp"
#include "hmbdc/Config.hpp"
#include "hmbdc/Compile.hpp"
#include <memory>       
#include <algorithm>       
#include <atomic>       
#include <mutex>   
#include <unistd.h>

#ifdef HMBDC_NO_EPOLL
#include <poll.h>
#else
#include <sys/epoll.h>
#endif

namespace hmbdc { namespace app { namespace utils {

struct EpollFd;
#ifndef HMBDC_NO_EPOLL
struct EpollTask 
: pattern::GuardedSingleton<EpollTask> {
    enum {
        EPOLLIN = ::EPOLLIN,
        EPOLLOUT = ::EPOLLOUT,
        EPOLLET = ::EPOLLET
    };

    static void initialize() {
        static hmbdc::pattern::SingletonGuardian<hmbdc::app::utils::EpollTask> gEpollTaskGuard;
    }
    
    void setWaitTime(int t) {
        timeoutMillisec_ = t;
    }

	void add(uint32_t events, EpollFd&);
    bool del(EpollFd&);
    void poll();

private:
	friend pattern::SingletonGuardian<EpollTask>;
    friend EpollFd;
	EpollTask(size_t maxFdCount = HMBDC_EPOLL_FD_MAX);
	~EpollTask();
    void setPollPending() { pollPending_ = true; }

    bool stopped_;
	int epollFd_;
    std::atomic<bool> pollPending_ = false;
	// std::thread pollThread_;
    int timeoutMillisec_ = 0;
};
#else
struct EpollTask 
: pattern::GuardedSingleton<EpollTask> {
    enum {
        EPOLLIN = POLLIN,
        EPOLLOUT = POLLOUT,
        EPOLLET = 0
    };
    static void initialize() {
        static hmbdc::pattern::SingletonGuardian<hmbdc::app::utils::EpollTask> gEpollTaskGuard;
    }

    void add(uint32_t events, EpollFd&);
    bool del(EpollFd&);
    void poll();

private:
    friend pattern::SingletonGuardian<EpollTask>;
    friend EpollFd;
    void setPollPending() {}
    EpollTask(size_t maxFdCount = HMBDC_EPOLL_FD_MAX);

    ~EpollTask();

    bool stopped_;
    size_t maxFdCount_;
    ::pollfd* pollFds_;
    std::atomic<bool>** sentinels_;
    std::mutex lock_;
};
#endif

struct EpollFd {
    EpollFd(EpollFd const&) = delete;
    EpollFd& operator = (EpollFd const&) = delete;
	EpollFd()
    : fd(-1)
    , fdReady_(false)
    , fdReadyLocal_(false) 
    {}

    virtual 
    ~EpollFd() {
        if (fd > 0) {
            utils::EpollTask::instance().del(*this);
            close(fd);
        }
    }

    bool isFdReady() {
        if (hmbdc_likely(fdReadyLocal_)) {
            return true;
        } else {
            EpollTask::instance().setPollPending();
            return fdReadyLocal_ = fdReady_.exchange(false, std::memory_order_relaxed);
        }
    }

    bool checkErr() {
        fdReadyLocal_ = false;
        if (errno != EAGAIN) {
            if (!utils::EpollTask::instance().del(*this)) {
            } else {
                close(fd);
                fd = -1;
            }
            return false;
        }
        return true;
    }

    int fd;
    
private:
	friend struct EpollTask;
    std::atomic<bool> fdReady_;
    bool fdReadyLocal_;
};
}}}

#ifndef HMBDC_NO_EPOLL
#include <sys/epoll.h>
namespace hmbdc { namespace app { namespace utils {

inline
EpollTask::
EpollTask(size_t maxFdCount)
: stopped_(false) {
	epollFd_ = epoll_create(maxFdCount);
	if (epollFd_ == -1) {
		HMBDC_THROW(std::runtime_error, "cannot create epoll errno=" << errno);
	}
}

inline
void 
EpollTask::
add(uint32_t events, EpollFd& t) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = &t.fdReady_;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, t.fd, &ev) == -1) {
        HMBDC_THROW(std::runtime_error, "cannot add into epoll, errno=" << errno);
    }
}

inline
bool
EpollTask::
del(EpollFd& t) {
    return epoll_ctl(epollFd_, EPOLL_CTL_DEL, t.fd, NULL) != -1;
}

inline
void 
EpollTask::
poll() {
	if (!pollPending_) return;
	struct epoll_event events[HMBDC_EPOLL_FD_MAX];
	int nfds;
	nfds = epoll_wait(epollFd_, events, HMBDC_EPOLL_FD_MAX, timeoutMillisec_);
	if (hmbdc_unlikely(nfds == -1 && errno != EINTR)) {
		HMBDC_THROW(std::runtime_error, "epoll_pwait errno=" << errno);
	}
	for (int i = 0; i < nfds; ++i) {
		auto& sentinel = *static_cast<bool*>(events[i].data.ptr);
		sentinel = true;
	}
	pollPending_ = false;
	if (nfds > 0) std::atomic_thread_fence(std::memory_order_release);
}

inline
EpollTask::
~EpollTask() {
	close(epollFd_);
	stopped_ = true;
	// pollThread_.join();
}

}}}

#else 

namespace hmbdc { namespace app { namespace utils {

inline
EpollTask::
EpollTask(size_t maxFdCount)
: stopped_(false)
, maxFdCount_(maxFdCount)
, pollFds_(new ::pollfd[maxFdCount])
, sentinels_(new std::atomic<bool>* [maxFdCount]) {
	for (auto i = 0u; i < maxFdCount; ++i) {
		pollFds_[i].fd = -1;
		sentinels_[i] = nullptr;
	}
}

inline
void 
EpollTask::
add(uint32_t events, EpollFd& t) {
	std::lock_guard<std::mutex> g(lock_);
	auto slot = std::find_if(pollFds_, pollFds_ + maxFdCount_
		, [](pollfd& pf){return pf.fd < 0;});
	if (slot != pollFds_ + maxFdCount_) {
		slot->fd = t.fd;
		slot->events = (short)events;
		slot->revents = 0;
		sentinels_[slot - pollFds_] = &t.fdReady_;
	} else {
        HMBDC_THROW(std::runtime_error, "cannot add into poll, errno=" << errno);
    }
}

inline
bool
EpollTask::
del(EpollFd& t) {
	std::lock_guard<std::mutex> g(lock_);
	auto slot = std::find_if(pollFds_, pollFds_ + maxFdCount_
		, [fd = t.fd](pollfd& pf){return pf.fd == fd;});
	if (slot != pollFds_ + maxFdCount_) {
		slot->fd = -slot->fd;
		sentinels_[slot - pollFds_] = nullptr;
		return true;
	} else {
		return false;
	}
}

inline
void 
EpollTask::
poll() {
	if (!lock_.try_lock()) return;
	auto nfds = ::poll(pollFds_, (nfds_t)maxFdCount_, 0);
	for (auto i = 0u; nfds > 0 && i < maxFdCount_; ++i) {
		if (pollFds_[i].revents) {
			*sentinels_[i] = true;
			nfds--;
		}
	}
	lock_.unlock();

	if (hmbdc_unlikely(nfds< 0 && errno != EINTR)) {
		HMBDC_THROW(std::runtime_error, "poll errno=" << errno);
	}
}

inline
EpollTask::
~EpollTask() {
	stopped_ = true;
	delete []sentinels_;
	delete []pollFds_;
}

}}}

#endif