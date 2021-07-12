#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/app/Client.hpp"
#include "hmbdc/app/ClientWithStash.hpp"
#include "hmbdc/app/Context.hpp"
#include "hmbdc/app/Message.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/app/Config.hpp"
#include "hmbdc/pattern/GuardedSingleton.hpp"

namespace hmbdc { namespace app {
template <typename Singleton>
using SingletonGuardian = pattern::SingletonGuardian<Singleton>;
}}
