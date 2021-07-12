#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/utils/EpollTask.hpp"
#include "hmbdc/app/Base.hpp"

#include <utility>

namespace hmbdc { namespace app { namespace utils {

struct NetContextUtil {
protected:
    void checkEpollTaskInitialization() {
		utils::EpollTask::initialize();
    }
};
}}}
