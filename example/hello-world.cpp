// hello-world for hmbdc TIPS 
// - see concept of doamin and Node https://www.hummingbirdcode.net/p/concept-model.html
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
#include "hmbdc/tips/tcpcast/Protocol.hpp" //use tcpcast for communication
#include "hmbdc/tips/Tips.hpp"
#include "hmbdc/os/Signals.hpp"

#include <iostream>

using namespace std;
using namespace hmbdc::app;
using namespace hmbdc::tips;

/// write a message type to publish later
struct Hello
: hasTag<1001> {            //16bit msg tag (>1000) is unique per message type
    char msg[6] = "hello";
};

/// write a Node subscribe to the message
struct Receiver
: Node<Receiver, std::tuple<Hello>> { //only subscribe Hello
    /// specify what types to publish - nothing
    using SendMessageTuple = std::tuple<>;
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
    if (!isSender) {
        cout << "running as receiver, ctrl-c to exit" << endl;
    }

    Config config; //other config values are default
    config.put("ifaceAddr", ifaceAddr);//which net IP to use for communication
    
    SingletonGuardian<tcpcast::Protocol> g; //RAII for tcpcast::Protocol resources

    if (isSender) { //running as sender
        using MyDomain = Domain<std::tuple<>    /// no subscribing
            , ipc_property<>                    /// default IPC params
            , net_property<tcpcast::Protocol>>; /// use tcpcast as network transport
        config.put("minRecvToStart", 1);    /// ask the Domain to buffer the network messages until 
                                            /// the first connection is established
        auto domain = MyDomain{config};
        domain.addPub<std::tuple<Hello>>(); 
        domain.publish(Hello{});
        sleep(1); //so the message does go out to the network
        domain.stop();
        domain.join();
    } else {  //as a receiver
        using MyDomain = Domain<std::tuple<Hello>   /// subscribe to Hello
            , ipc_property<>                        /// match sender side
            , net_property<tcpcast::Protocol>>;     /// match sender side
        auto domain = MyDomain{config};
        Receiver recv;
        domain.start(recv);                         /// recv Node started
        /// handle ctrl-c
        hmbdc::os::HandleSignals::onTermIntDo([&](){domain.stop();});
        domain.join();
    }
}