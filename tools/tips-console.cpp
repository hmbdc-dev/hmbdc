#include "hmbdc/tips/Tips.hpp"
#include "hmbdc/tips/tcpcast/Protocol.hpp"
#include "hmbdc/tips/rmcast/Protocol.hpp"
#include "hmbdc/tips/ConsoleNode.hpp"

#ifndef HMBDC_NO_NETMAP
#include "hmbdc/tips/rnetmap/Protocol.hpp"
#endif

#include <boost/format.hpp>
#include <boost/program_options.hpp>

#include <unordered_set>
#include <iostream>
#include <iomanip>
#include <memory>
#include <unistd.h>
#include <atomic>
#include <optional>


using namespace std;
using namespace boost;
using namespace hmbdc;
using namespace hmbdc::tips;
using namespace hmbdc::app;

template <typename NetProt, uint32_t IpcCapacity>
struct Runner {
    int help(std::string const& netProt, uint32_t ipcCapacity) {
        if (netProt != NetProt::name() || ipcCapacity != IpcCapacity) {
            return 0;
        }
        cout << NetProt::name() << " configurations:\n";
        cout << NetProt::dftConfig() << "\n";
        cout << "IPC configurations:\n";
        cout << tips::DefaultUserConfig << "\n";
        return 0;
    }

    int operator()(std::string const& netProt, uint32_t ipcCapacity
        , Config const& config) {
        if (netProt != NetProt::name() || ipcCapacity != IpcCapacity) {
            return 0;
        }
        
        pattern::SingletonGuardian<NetProt> ncGuard;
        using ConsoleDomain 
            = SingleNodeDomain<ConsoleNode, ipc_property<IpcCapacity, 0>, net_property<NetProt, 0>>;
        ConsoleDomain domain{config};
        ConsoleNode console{config};
        domain.add(console);
        domain.startPumping();
        console.waitUntilFinish();
        domain.stop();
        domain.join();
        return 0;
    }
};

int main(int argc, char** argv) {
    pattern::SingletonGuardian<SyncLogger> logGuard(std::cerr);

    namespace po = boost::program_options;
    po::options_description desc("Allowed cmd options");
    auto helpStr = 
    R"|(
This program provides a console for a TIPS domain.
)|";
    desc.add_options()
    ("help,h", helpStr)
    ;

    std::string cfgString =
R"|(
{
    "ifaceAddr"     : "127.0.0.1",      "__ifaceAddr"   : "(non-rnetmap) ip address for the NIC interface for IO, 0.0.0.0/0 pointing to the first intereface that is not a loopback",
    "netmapPort"    : "UNSPECIFIED",    "__netmapPort"  : "(rnemtap) the netmap device (i.e. netmap:eth0-2, netmap:ens4) for sending/receiving, no default value. When multiple tx rings exists for a device (like 10G NIC), the sender side must be specific on which tx ring to use",
    "ipcCapacity"   : 64,               "__ipcCapacity" : "See capacity in ipc_property in Domain.hpp: 4 8 ... 256  - this needs to match ipc_property's IpcCapacity value of the monitored system",
    "netProt"       : "tcpcast",        "__netProt"     : "one of tcpcast udpcast rmcast netmap rnetmap and nonet",
    "bufWidth"      : 1000,             "__bufWidth"    : "max message that the console handles (excluding attachment) - this needs to match ipcMaxMessageSizeRuntime value of the monitored system",
    "logLevel"      : 3,                "__logLevel"    : "only log Warning and above by default"
}
)|";

    Config config(cfgString.c_str());
    auto params = config.content();
    for (auto it = params.begin(); it != params.end();) {
    	string& name = it->first;
    	string& val = it->second;
    	it++;
    	string& comment = it->second;
    	it++;
    	desc.add_options()
    		(name.c_str(), po::value<string>(&val)->default_value(val), comment.c_str());
    }

    po::positional_options_description p;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).
        options(desc).positional(p).run(), vm);
    po::notify(vm);

#define RUNNER_LIST(x) Runner<x::Protocol, 4>, Runner<x::Protocol, 8>, Runner<x::Protocol, 16>, Runner<x::Protocol, 64>, Runner<x::Protocol, 128>, Runner<x::Protocol, 256>
    using Runners = std::tuple<
        RUNNER_LIST(nonet)
        , RUNNER_LIST(tcpcast)
        , RUNNER_LIST(rmcast)
#ifndef HMBDC_NO_NETMAP        
        , RUNNER_LIST(rnetmap)
#endif        
    >;
    config = Config{};
    for (auto it = params.begin(); it != params.end(); ++it) {
        config.put(it->first, it->second);
    }

    auto netProt = config.getExt<std::string>("netProt");
    auto ipcCapacity = config.getExt<uint32_t>("ipcCapacity");
    if (vm.count("help")) {
        cout << desc << "\n";
        std::apply(
            [=](auto&&... args) {
                (args.help(netProt, ipcCapacity), ...);
            }
            , Runners()
        );
        return 0;
    }

    if (vm.count("cfg")) {
        auto cfg = config.getExt<std::string>("cfg");
        auto ifs = std::ifstream(cfg);
        cfgString = std::string((std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>());
    }

    SyncLogger::instance().setMinLogLevel((SyncLogger::Level)config.getExt<int>("logLevel"));

    HMBDC_LOG_N(" in effect config: ", config);

    config.put("ipcMaxMessageSizeRuntime", config.getExt<std::string>("bufWidth"));
    config.put("netMaxMessageSizeRuntime", config.getExt<std::string>("bufWidth"));
    std::apply(
        [&](auto&&... args) {
            (args(netProt, ipcCapacity, config), ...);
        }
        , Runners()
    );

    return 0;
}
