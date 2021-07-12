#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/netmap/SendTransportEngine.hpp"
#include "hmbdc/tips/netmap/RecvTransportEngine.hpp"
#include "hmbdc/tips/netmap/DefaultUserConfig.hpp"
#include "hmbdc/app/Config.hpp"

#include "hmbdc/pattern/GuardedSingleton.hpp"
#include <algorithm>
namespace hmbdc { namespace tips { namespace netmap {

struct Protocol 
: hmbdc::pattern::GuardedSingleton<Protocol> {
    static constexpr char const* name() { return "netmap"; }
    static constexpr auto dftConfig() { return DefaultUserConfig; }
    using SendTransportEngine = hmbdc::tips::netmap::SendTransportEngine;

    template <typename Buffer, typename>
    using RecvTransportEngine = hmbdc::tips::netmap::RecvTransportEngine<Buffer>;

    std::string getTipsDomainName(app::Config cfg) {
        cfg.resetSection("tx", false);
        cfg.setAdditionalFallbackConfig(app::Config(DefaultUserConfig));

        auto res= cfg.getExt<std::string>("ifaceAddr") + '-' + cfg.getExt<std::string>("netmapPort");
        std::replace(res.begin(), res.end(), '/', ':');
        return res;
    }

    private:
    friend struct hmbdc::pattern::SingletonGuardian<Protocol>;
    Protocol() {}
};

}}}


