#pragma once
#include <string>

namespace hmbdc::os {
/**
 * @brief support obtain stack back traces of all threads of the calling process
 *
 */
struct StackTraces {
    /**
     * @brief obtain the all thread's stack backtrace in a multi-line string, 
     * with the calling thread's bt first. 
     * This call is threadsafe but only the first call is effective and later calls are ignored.
     * @return std::string the backtrace result; redundant calls returns empty
     */
    static std::string get();

    /**
     * @brief only used in unit test
     * 
     */
    static void reset();

    /**
     * @brief if you want when program error happens (from SEGV to divide zerro), 
     * all the stack traces of threads be sent to stderr, call this
     * 
     * @return true if installed fine
     * @return false 
     */
    static bool installForErrorDetection();
};
}

