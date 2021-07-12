#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/Exception.hpp"

#include <functional>
#include <signal.h>
#include <memory.h>

namespace hmbdc { namespace os {

/**
 * @brief provides functions to handle signals
 */
struct
HandleSignals {
	/**
	 * @brief specfy what to do when SIGTERM or SIGINT is received
	 * @details will install signal handlers - might make previous installed
	 * handlers not working, so don't call this more than once
	 * 
	 * @param doThis a function<void()> or more lkely a lambda specifying
	 * what to do
	 */
	static
	void 
	onTermIntDo(std::function<void()> doThis) {
		onTermInt_s() = doThis;

		struct sigaction act;
		memset(&act, 0, sizeof(act));   
		act.sa_sigaction = handler;
		act.sa_flags = SA_SIGINFO;

		if (sigaction(SIGTERM, &act, NULL) ||
		    sigaction(SIGINT, &act, NULL)) {
		    HMBDC_THROW(std::runtime_error, "cannot install signal handler");
		}
	}

private:
	static std::function<void()>& onTermInt_s() {
		static std::function<void()> func;
		return func;
	};
	static
	void 
	handler(int signum, siginfo_t *, void *) {
	    onTermInt_s()();
	}
};

}}

