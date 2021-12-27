#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/udpcast/SendTransportEngine.hpp"
#include "hmbdc/tips/udpcast/RecvTransportEngine.hpp"
#include "hmbdc/tips/udpcast/DefaultUserConfig.hpp"
#include "hmbdc/app/utils/NetContextUtil.hpp"
#include "hmbdc/app/Config.hpp"

#include "hmbdc/pattern/GuardedSingleton.hpp"
#include <algorithm>

namespace hmbdc { namespace tips { namespace udpcast {
struct Protocol 
: pattern::GuardedSingleton<Protocol> 
, private app::utils::NetContextUtil {
    static constexpr char const* name() { return "udpcast"; }
    static constexpr auto dftConfig() { return DefaultUserConfig; }
    using SendTransportEngine = udpcast::SendTransportEngine;
    template <typename Buffer, typename>
    using RecvTransportEngine = udpcast::RecvTransportEngine<Buffer>;
    std::string getTipsDomainName(app::Config cfg) {
        cfg.resetSection("tx", false);
        cfg.setAdditionalFallbackConfig(app::Config(DefaultUserConfig));

        auto res = cfg.getExt<std::string>("localDomainName");
        if (res != "tips-auto") return res;
        res = cfg.getExt<std::string>("ifaceAddr") + '-' + cfg.getExt<std::string>("udpcastDests");
        std::replace(res.begin(), res.end(), '/', ':');
        return res;
    }

    private:
    friend pattern::SingletonGuardian<Protocol>;
    Protocol(){
        checkEpollTaskInitialization();
    }
};
}}}


