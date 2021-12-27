#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/rmcast/SendTransportEngine.hpp"
#include "hmbdc/tips/rmcast/RecvTransportEngine.hpp"
#include "hmbdc/tips/rmcast/DefaultUserConfig.hpp"
#include "hmbdc/app/utils/NetContextUtil.hpp"
#include "hmbdc/app/Config.hpp"
#include "hmbdc/pattern/GuardedSingleton.hpp"
#include <algorithm>

namespace hmbdc { namespace tips { namespace rmcast {

struct Protocol
: hmbdc::pattern::GuardedSingleton<Protocol> 
, private hmbdc::app::utils::NetContextUtil {
    static constexpr char const* name() { return "rmcast"; }
    static constexpr auto dftConfig() { return DefaultUserConfig; }

    using SendTransportEngine = rmcast::SendTransportEngine;
    template <typename Buffer, typename AttachmentAllocator>
    using RecvTransportEngine = rmcast::RecvTransportEngine<Buffer, AttachmentAllocator>;
    std::string getTipsDomainName(app::Config cfg) {
        cfg.resetSection("tx", false);
        cfg.setAdditionalFallbackConfig(app::Config(DefaultUserConfig));
        auto res = cfg.getExt<std::string>("localDomainName");
        if (res != "tips-auto") return res; 
        
        res = cfg.getExt<std::string>("ifaceAddr") + '-' 
            + cfg.getExt<std::string>("mcastAddr") + ":" + cfg.getExt<std::string>("mcastPort");
        std::replace(res.begin(), res.end(), '/', ':');
        return res;
    }

    private:
    friend pattern::SingletonGuardian<Protocol>;
    Protocol(){
        checkEpollTaskInitialization();
    }
};

} //rmcast

}}

