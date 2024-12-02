// hello-world for hmbdc POTS 
// - see concepts of domain and Node https://www.hummingbirdcode.net/p/concept-model.html
// to build:
// g++ hello-world-pots.cpp -O3 -std=c++1z -pthread -D BOOST_BIND_GLOBAL_PLACEHOLDERS -Ipath-to-boost -lrt -o /tmp/hw-pots
//
// example screenshot on localhost - start receiver first on term 1, then sender on term 2:
// [term1] /tmp/hw-pots 127.0.0.1 recv
// running as receiver, ctrl-c to exit
//
// <debug log msgs>
// ...
// hello world!
// hello world!
// hello world!
// hello world!
// hello world!
// ...
// [term2] /tmp/hw-pots 127.0.0.1
//
// <debug log msgs>
/// ...
// start messaging and timers
// sending a Hello
// world hi back
// msgs just received =1
// ...
//
// WHEN RUNNING ON DIFFERENT HOSTS, MAKE SURE FIREWALLS ARE OFF ON THEM SO UDP MULTICAST WORK AMONG THEM
//
// to debug:
// somtimes you need to reset shared memory by "rm /dev/shm/*" 
// - see ipcTransportOwnership config for shared memory ownership in tips/DefaultUserConfig.hpp
//
#include "hmbdc/pots/Pots.hpp"
#include "hmbdc/os/Signals.hpp"

#include <iostream>
#include <iterator>

using namespace hmbdc;

// list all topic - in the same order in each compiling unit
char const* AllTopics[] = {
    "/hello",
    "/hi-back",
};

/// write a Node publish the message @1HZ for 10 times
struct Sender : pots::Node<Sender> {
    Sender() 
    : Node{
        {"/hi-back"}        // 1 subscription
        , {"/hello"}        // publish 1 topic
    } {
        // schedule the 1HZ timer - first firing asap from now
        TimerManager::schedule(time::SysTime::now(), timer1Hz);
    }

    /// node/thread name
    char const* hmbdcName() const {return "Sender";}

    /// called only once before any message dispatching happens
    virtual void messageDispatchingStartedCb(std::atomic<size_t> const*) override {
        std::cout << "start messaging and timers" << std::endl;
    }

    // callback called once whenever this Sender
    // thread is unblocked - by new message arriving, timer wake up
    // or max blocking timeouts (1 sec by default)
    void invokedCb(size_t count) override {
        if (count) std::cout << "msgs just received =" << count << std::endl;
    }

    /// message callback
    virtual void potsCb(std::string_view topic, void const* msg, size_t msgLen) override {
        std::cout << static_cast<char const*>(msg) << std::endl;
    }

    /// timer
    time::ReoccuringTimer timer1Hz{time::Duration::seconds(1) // reoccure every 1 sec
        , [this](auto&&, auto&&) {                            // do this when timer fires  
        static auto count = 10;
        if (count--) { 
            std::cout << "sending a Hello" << std::endl;
            auto msg = "hello world!";
            publish("/hello", msg, strlen(msg) + 1);
        } else {
            throw (ExitCode(0)); // node finished and exit it by throw an exception
        }
    }};
};

/// write a Node subscribe to the message
struct Receiver : pots::Node<Receiver> {
    Receiver()
    : Node(
        {"/hello"}          // 1 subscription
        , {"/hi-back"})     // publish 1 topic
    {}
    char const* hmbdcName() const {return "Receiver";}
    /// message callback - won't compile if missing
    void potsCb(std::string_view topic, void const* msg, size_t msgLen) {
        std::cout << static_cast<char const*>(msg) << std::endl;

        std::string back{"world hi back"};
        publish("/hi-back", back.c_str(), back.size() + 1);
    }
};

int main(int argc, char** argv) {
    using namespace std;
    if (argc < 2) {
        cerr << argv[0] << " local-ip [recv]" << endl;
        cerr << "start application as sender (default) or as Receiver" << endl;
        return -1;
    }
    auto ifaceAddr = argv[1];
    bool isSender = argc <= 2;
    if (!isSender) {
        cout << "running as receiver, ctrl-c to exit" << endl;
    }
    
    hmbdc::pattern::SingletonGuardian<pots::MessageConfigurator> 
        g{std::begin(AllTopics), std::end(AllTopics)}; //Step 1: RAII
    auto domain = pots::DefaultDomain{ifaceAddr};

    if (isSender) { //running as sender
        Sender sender;
        domain.add(sender);                     /// start sender as a thread
        domain.startPumping();                  /// start process-level message IO
        sleep(10);           // let the sender thread run for a while
        domain.stop();       // wrap up and exit
        domain.join();
    } else {  //as a receiver
        auto domain = pots::DefaultDomain{ifaceAddr};
        Receiver recv;
        domain.add(recv).startPumping();            /// recv Node and IO started
        /// handle ctrl-c
        hmbdc::os::HandleSignals::onTermIntDo([&](){domain.stop();});
        domain.join();
    }
}
