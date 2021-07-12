#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rnetmap/SendTransportEngine.hpp"
#include "hmbdc/tips/rnetmap/RecvTransportEngine.hpp"
#include "hmbdc/tips/rnetmap/DefaultUserConfig.hpp"
#include "hmbdc/app/utils/NetContextUtil.hpp"
#include "hmbdc/app/Config.hpp"

#include "hmbdc/pattern/GuardedSingleton.hpp"

namespace hmbdc { namespace tips { namespace rnetmap {
struct Protocol
: hmbdc::pattern::GuardedSingleton<Protocol> 
, private hmbdc::app::utils::NetContextUtil {
    static constexpr char const* name() { return "rnetmap"; }
    static constexpr auto dftConfig() { return DefaultUserConfig; }

    using SendTransportEngine = rnetmap::SendTransportEngine;
    template <typename Buffer, typename AttachmentAllocator>
    using RecvTransportEngine = rnetmap::RecvTransportEngine<Buffer, AttachmentAllocator>;
    std::string getTipsDomainName(app::Config cfg) {
        cfg.resetSection("tx", false);
        cfg.setAdditionalFallbackConfig(app::Config(DefaultUserConfig));
        return cfg.getExt<std::string>("ifaceAddr") + '-' + cfg.getExt<std::string>("netmapPort");
    }

    private:
    friend pattern::SingletonGuardian<Protocol>;
    Protocol(){
        checkEpollTaskInitialization();
    }
};
}}}

