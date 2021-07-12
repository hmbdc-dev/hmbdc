#include "hmbdc/Copyright.hpp"
#pragma once

#include <sstream>
#include <stdexcept>

#define HMBDC_THROW(Exception, x) {\
    std::ostringstream os;\
    os << x << " at " << __FILE__ << ':' << __LINE__;\
    throw Exception(os.str()); \
}

namespace hmbdc {
    /**
     * @brief Unknown excpetion
     */
    struct UnknownException 
    : std::exception {
        UnknownException(){}
        char const* what() const noexcept override {
        	return "Unknown excpetion";
        }
    };
   
    /**
     * @brief Exception that just has an exit code
     */
	struct ExitCode 
    : std::exception {
		explicit ExitCode(int c) {
			sprintf(code, "%d", c);
		}
        char const* what() const noexcept override {
        	return code;
        }

    private:
        char code[16];
	};
}

