//
// HMBDC TIPS supports C++ type and numeric tag based pub/sub due to its optimal performance, simplicity and flexibility.
// However, many existing apps use string topic based pub/sub middleware and here is an example to show 
// how to statically map topic strings into tags in the code with minimum effort.
// 
// Note: If you are in need of runtime determined topic base pub/sub i.e. the topic strings used cannot be determined
// at compile time, HMBDC TIPS also supports runtime tagging.
// Please refer to the use of ChatMesage (tag range) shown in the chat example.
//
#include "hmbdc/tips/rmcast/Protocol.hpp"
#include "hmbdc/tips/Tips.hpp"
#include "hmbdc/os/Signals.hpp"

#include <iostream>
#include <string_view>
#include <memory>
#include <unistd.h>
#include <assert.h>

using namespace hmbdc;

// all string topic ever will be used in the pub/sub domain are listed here at compile time
// put it in a header, so this can be reused in all compile unit consistently
static constexpr char const * const TopicsDictionary[] = {
    "topic_1",
    "topic_2",
    "topic_3",
    // ...
};

constexpr const uint16_t messageTagStartingAt = app::LastSystemMessage::typeTag + 2; // 1001

// compile time resolving the tag and topic mapping if possible
constexpr uint16_t tagForTopic(std::string_view topic) {
    for (auto topicIt = std::begin(TopicsDictionary); topicIt != std::end(TopicsDictionary); ++topicIt) {
        if (topic == *topicIt) {
            return (uint16_t)(topicIt - std::begin(TopicsDictionary) + messageTagStartingAt);
        }
    }
    assert(false); // the topic is not listed in TopicsDictionary
    return -1;
}
constexpr std::string_view tagToTopic(uint16_t tag) {
    auto offset = tag - messageTagStartingAt;
    if (offset < std::begin(TopicsDictionary) - std::end(TopicsDictionary)) {
        return TopicsDictionary[offset];
    } else {
        assert(false); // the tag is not listed in TopicsDictionary
        return std::string_view{};
    }
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
    void messageDispatchingStartedCb(std::atomic<size_t> const*) override {
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
