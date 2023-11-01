//
// HMBDC TIPS supportz C++ type based pub/sub since that has the best performance, simplicity and flexibility,
// However, many existing apps use string topic based pub/sub middleware and here is an example to show 
// how to make hmbdc do static string topic based pub/sub from anywhere in the code with minimum effort.
// 
// If you are in need of runtime determined topic base pub/sub when the topic string used cannot be determined
// at compile time, HMBDC TIPS also supports runtime tagging. 
// Please refer to the use of ChatMesage (tag range) shown in the chat example.
//
#include "hmbdc/tips/rmcast/Protocol.hpp"
#include "hmbdc/tips/Tips.hpp"
#include "hmbdc/os/Signals.hpp"

#include <gtest/gtest.h>

#include <iostream>
#include <string_view>
#include <memory>
#include <unistd.h>

using namespace hmbdc;

static constexpr char const * const TopicsDictionary[] = {
    "topic_1",
    "topic_2",
    "topic_3",
};

constexpr uint16_t tagForTopic(std::string_view topic) {
    for (auto topicIt = std::begin(TopicsDictionary); topicIt != std::end(TopicsDictionary); ++topicIt) {
        if (topic == *topicIt) {
            return (uint16_t)(topicIt - std::begin(TopicsDictionary) + 1001);
        }
    }
    return -1;
}

constexpr std::string_view tagToTopic(uint16_t tag) {
    auto offset = tag - 1001;
    if (offset < std::begin(TopicsDictionary) - std::end(TopicsDictionary)) {
        return TopicsDictionary[offset];
    }
    return std::string_view{};
}

struct MsgForTopic1
: app::hasTag<tagForTopic("topic_1")>
, tips::do_not_send_via<tips::INTER_THREAD | tips::INTER_PROCESS> { //assumes only use hmbdc to handle network pub/sub
    char content[512];
};

struct MsgForTopic2
: app::hasTag<tagForTopic("topic_2")>
, tips::do_not_send_via<tips::INTER_THREAD | tips::INTER_PROCESS> { //assumes only use hmbdc to handle network pub/sub
    char content[256];
};

struct MsgForTopic3
: app::hasTag<tagForTopic("topic_3")>
, tips::do_not_send_via<tips::INTER_THREAD | tips::INTER_PROCESS> { //assumes only use hmbdc to handle network pub/sub
    char content[16];
};

struct RecvNode
: tips::Node<RecvNode, std::tuple<MsgForTopic2, MsgForTopic3>> {
    void messageDispatchingStartedCb(size_t const*) override {
        HMBDC_LOG_N("RecvNode ready");
    }

    void handleMessageCb(MsgForTopic2 const& r) {
        HMBDC_LOG_N("received ", tagToTopic(r.getTypeTag()), r.content);
    }

    void handleMessageCb(MsgForTopic3 const& r) {
        HMBDC_LOG_N("received ", tagToTopic(r.getTypeTag()), r.content);
    }

    void stoppedCb(std::exception const& e) override {
        HMBDC_LOG_C(e.what());
    };
};

/// @brief  write a skeleton Node showing what to publish to the domain
struct SendNodeForRegistrationOnly
: tips::Node<SendNodeForRegistrationOnly, std::tuple<>
    , std::tuple<MsgForTopic2, MsgForTopic3>> {};

app::Config cfgFile( 
R"|(
{
    "loopback" : true,                                  "__loopback"                    :"only need it in same-host test, remove this in the real world", 
    "ifaceAddr"         : "127.0.0.1",                  "__ifaceAddr"                   :"change it in the real world",
    "mtu"               : 1500,                         "__mtu"                         :"mtu, check ifconfig output for this value for each NIC in use",
    "tcpIfaceAddr"      : "ifaceAddr",                  "__tcpIfaceAddr"                :"ip address for the NIC interface for TCP (backup) traffic IO, default points to the same as ifaceAddr"
}
)|");

int main(int argc, char** argv) {
    using namespace std;
    using namespace boost;
    pattern::SingletonGuardian<app::SyncLogger> logGuard(std::cout); // RAII
    pattern::SingletonGuardian<tips::rmcast::Protocol> g;  // RAII

    if (argc > 1) { // sender
        using MyDomain = tips::Domain<std::tuple<> // recv mothing
            , tips::NoIpc 
            , tips::net_property<tips::rmcast::Protocol>>;
        MyDomain domain{cfgFile};

        /// still need a skeleton Node for registration
        SendNodeForRegistrationOnly registrateOnly;
        domain.addPubSubFor(registrateOnly);
        domain.startPumping();

        std::atomic<bool> stopped = false;
        hmbdc::os::HandleSignals::onTermIntDo(  // wait for ctrl - c
            [&domain, &stopped]() {
                stopped = true;
                domain.stop();
        });

        auto toPub = MsgForTopic2{{}, {}, {"some msg on topic 2"}};

        while (!stopped) {
            domain.publish(toPub); // publish anywhere you have access to the domain, threadsafe
            HMBDC_LOG_N("sending");
            // usleep(1); // suggest pace ur publish - using the Rater class in hmbdc here
        }
        domain.join();
    } else { // recv
        using MyDomain = tips::Domain<std::tuple<MsgForTopic2, MsgForTopic3>, tips::NoIpc
            , tips::net_property<tips::rmcast::Protocol>>;
        MyDomain domain{cfgFile};
        RecvNode recv;
        domain.add(recv);
        domain.startPumping();
        hmbdc::os::HandleSignals::onTermIntDo(  // wait for ctrl - c
            [&domain]() {
                domain.stop();
        });
        domain.join();
    }
}
