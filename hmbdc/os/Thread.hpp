#include "hmbdc/Copyright.hpp"
#pragma once 
#include "hmbdc/os/Thread.hpp"
#include "hmbdc/Exception.hpp"

#include <string>
#include <thread>

#include <stdexcept>

#include <unistd.h>
#include <pthread.h>
#include <string.h>

namespace hmbdc { namespace os {

struct ThreadConfigException : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void
configureCurrentThread(char const*threadName, unsigned long cpumask
    , char const* schepolicy = "SCHED_OTHER", int priority = 0);

inline
void 
configureCurrentThread(char const*threadName, unsigned long cpumask
    , char const* schepolicy, int priority) {
    int res0 = 0, res1 = 0;
#ifndef _QNX_SOURCE            
    if (cpumask && cpumask != 0xfffffffffffffffful) {
        cpumask &= (1ul << std::thread::hardware_concurrency()) - 1ul;
        if (cpumask != (1ul << std::thread::hardware_concurrency()) - 1ul) {
            cpu_set_t mask;
            CPU_ZERO(&mask);
            for (unsigned long i = 0; i < sizeof(cpumask) * 8ul; ++i) {
                if (cpumask & (1ul << i)) {
                   CPU_SET(i, &mask);
                }
            }
            res0 = pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
        }
    }
#endif        
    sched_param param = {0};
    int policy = 0;
    if (!schepolicy || strlen(schepolicy) == 0) {
        // skip if not set
    } else if (std::string(schepolicy) == "SCHED_FIFO") {
        policy = SCHED_FIFO;
    }
    else if (std::string(schepolicy) == "SCHED_RR") {
        policy = SCHED_RR;  
    } else if (std::string(schepolicy) == "SCHED_IDLE") {
#ifndef _QNX_SOURCE        
        policy = SCHED_IDLE;
#else
        policy = SCHED_SPORADIC;
#endif        
    } else if (std::string(schepolicy) == "SCHED_DEADLINE") {
        policy = SCHED_DEADLINE;
    } else if (std::string(schepolicy) == "SCHED_OTHER") {
        policy = SCHED_OTHER;
        if (nice(priority) == -1) {
            HMBDC_THROW(ThreadConfigException, "nice() failure errno=" << errno);
        }
#ifndef _QNX_SOURCE
        priority = 0;
#endif
    } else {
        HMBDC_THROW(ThreadConfigException, "Unknown scheduling policy: " << schepolicy);
    }
    param.sched_priority = priority;
    if (policy) {
        res1 = pthread_setschedparam(pthread_self(), policy, &param);
    }
    int res2 = pthread_setname_np(pthread_self(), threadName);
    if (res0 || res1 || res2)
        HMBDC_THROW(ThreadConfigException, "cpumask=" << cpumask << " pthread_setaffinity_np=" << res0 
            << " pthread_setschedparam=" << res1 << " pthread_setname_np=" << res2 << " errno=" << errno);
}

inline void yield(unsigned x) {
    std::this_thread::yield();
}
}}
