// hello-world for hmbdc TIPS 
// - see concepts of domain and Node https://github.com/hmbdc-dev/hmbdc
// to build:
// g++ hello-world.cpp -O3 -std=c++1z -pthread -D BOOST_BIND_GLOBAL_PLACEHOLDERS -Ipath-to-boost -lrt -o /tmp/hw
//
// example screenshot on localhost - start receiver first on term 1, then sender on term 2:
// [term1] /tmp/hw 127.0.0.1 recv
// running as receiver, ctrl-c to exit
//
// [term2]$ ls /dev/shm # show persistent shared memory created and owned by the receiver process
// 127.0.0.1-232.43.212.235:4321  127.0.0.1-232.43.212.235:4321-att-pool  127.0.0.1-232.43.212.235:4321-ipcsubs
// [term2] /tmp/hw 127.0.0.1 # now send a hello - see it appear on term1
//
// WHEN RUNNING ON DIFFERENT HOSTS, MAKE SURE FIREWALLS ARE OFF ON THEM SO UDP MULTICAST WORK AMONG THEM
//
// to debug:
// somtimes you need to reset shared memory by "rm /dev/shm/*" 
// - see ipcTransportOwnership config for shared memory ownership in tips/DefaultUserConfig.hpp
//
#include "hmbdc/tips/tcpcast/Protocol.hpp" //use tcpcast for inter-host communication
#include "hmbdc/tips/Tips.hpp"
#include "hmbdc/os/Signals.hpp"

#include <iostream>

using namespace std;
using namespace hmbdc::app;
using namespace hmbdc::tips;

/// write a POC message type to publish later
struct Hello : hasTag<1001> { //16bit msg tag (>1000) is unique per message type
    char msg[6] = "hello";
};

/// write a simple Node subscribe to the above message
struct Receiver : Node<Receiver
    , std::tuple<Hello> // only subscribe Hello
    , std::tuple<>      // not publishing anything
> {
    /// message callback - won't compile if missing
    void handleMessageCb(Hello const& m) {
        cout << m.msg << endl;
    }
};

int main(int argc, char** argv) {
    using namespace std;
    if (argc < 2) {
        cerr << argv[0] << " local-ip [recv]" << endl;
        cerr << "start application as sender (default) or as Receiver" << endl;
        return -1;
    }
    std::string ifaceAddr = argv[1];
    bool isSender = argc <= 2;

    Config config; //other config values are default
    config.put("ifaceAddr", ifaceAddr);//which net IP to use for net communication
    
    SingletonGuardian<tcpcast::Protocol> g; //RAII for tcpcast::Protocol resources

    if (isSender) {
        cout << "running as sender, ctrl-c to exit" << endl;
        using MyDomain = Domain<std::tuple<>    /// no subscribing
            , ipc_property<>                    /// default IPC params (using shared mem)
            , net_property<tcpcast::Protocol>>; /// use tcpcast as network transport
        auto domain = MyDomain{config};
        domain.startPumping();
        /// handle ctrl-c
        auto stopped = std::atomic<bool>{false};
        hmbdc::os::HandleSignals::onTermIntDo([&](){stopped = true;});
        while (!stopped) { // until ctrl-c pressed
            // publish is a fast/async and theadsafe call, all subscribing Nodes in the domain receive it
            domain.publish(Hello{});
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        domain.stop();
        domain.join();
    } else {
        cout << "running as receiver, ctrl-c to exit" << endl;
        using MyDomain = Domain<std::tuple<Hello>   /// subscribe to Hello - compile time checked
            , ipc_property<>                        /// match sender side
            , net_property<tcpcast::Protocol>>;     /// match sender side
        auto domain = MyDomain{config};
        Receiver recv;
        domain.add(recv).startPumping();            /// recv Node and IO started
        /// handle ctrl-c
        hmbdc::os::HandleSignals::onTermIntDo([&](){domain.stop();});
        domain.join();
    }
}
