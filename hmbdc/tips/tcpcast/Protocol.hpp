#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/tcpcast/Messages.hpp"
#include "hmbdc/tips/tcpcast/SendTransportEngine.hpp"
#include "hmbdc/tips/tcpcast/RecvTransportEngine.hpp"
#include "hmbdc/tips/tcpcast/DefaultUserConfig.hpp"
#include "hmbdc/app/utils/NetContextUtil.hpp"
#include "hmbdc/app/Config.hpp"

#include "hmbdc/pattern/GuardedSingleton.hpp"

#include <algorithm>

namespace hmbdc { namespace tips { namespace tcpcast {

struct Protocol 
: pattern::GuardedSingleton<Protocol> 
, private app::utils::NetContextUtil {
    static constexpr char const* name() { return "tcpcast"; }
    static constexpr auto dftConfig() { return DefaultUserConfig; }
    using SendTransportEngine = tcpcast::SendTransportEngine;
    template <typename Buffer, typename AttachmentAllocator>
    using RecvTransportEngine = tcpcast::RecvTransportEngine<Buffer, AttachmentAllocator>;
    std::string getTipsDomainName(app::Config cfg) {
        cfg.resetSection("tx", false);
        cfg.setAdditionalFallbackConfig(app::Config(DefaultUserConfig));
        auto res = cfg.getExt<std::string>("ifaceAddr") + '-' + cfg.getExt<std::string>("udpcastDests");
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

