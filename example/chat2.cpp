//this is an additon to chat.cpp example to introduce:
//never blocking publish, serialization, thread pool, timer and logger usage of HMBDC TIPS
//
//the additional assumption here is that a single Admin (thread) cannot monitor all the conversations
//we need a pool of Admins to collectively monitor all the Chatters
//Also periodically print out the ChatMessage count from each Admin - you might see message on the screen
//show interleaving indicating they are printed from different Admins (threads)
//search for <====== mark in the code where the key additions related to timer and thread pool
//from chat.cpp shows
//the app join a chat group and start to have group chat
//to run:
//./chat2 <local-ip> admin admin                  # start the groups by admin, and keep it running
//./chat2 <local-ip> <chat-group-name> <my-name>  # join the group
//to build:
//g++ chat2.cpp -std=c++1z -pthread -D BOOST_BIND_GLOBAL_PLACEHOLDERS -Ipath-to-boost -lrt -o /tmp/chat2
//
//to debug:
//somtimes you need to reset shared memory by "rm /dev/shm/*" 
//on each host, the shared memory is owned (create and delete) by the first chat process started on local host
//- see ipcTransportOwnership config for shared memory ownership in tips/DefaultUserConfig.hpp
//
#include "hmbdc/tips/tcpcast/Protocol.hpp" //use tcpcast for communication
#include "hmbdc/tips/Tips.hpp"
#include "hmbdc/time/Timers.hpp"                                                        // <=====

#include "hmbdc/app/Logger.hpp"                                                         // <=====


#include <iostream>
#include <string>
#include <memory>
#include <unistd.h>

using namespace std;
using namespace hmbdc::app;
using namespace hmbdc::time;                                                            // <=====
using namespace hmbdc::tips;

//
//this is the message the administrator "annouces", each Chatter subscribes to it
//to hear what the admin has to say regardless of chatting group
//
struct Announcement
: hasTag<1001> {
    /// it is recommended to externalize serilization/deserialization (flatbuf or protobuf)
    /// and use hasSharedPtrAttachment to send the already serilized data
    /// if you need to go lightweight, here we show the built-in ser/des mechanism
    std::string msg;        /// this needs serialization to go out of process,          // <=====
                            /// if not provided, the pub/sub only happens within 
                            /// local process
    /// provide serialization logic here
    /// toHmbdcIpcable and fromHmbdcIpcable are the entrances
    /// for TIPS to call
    /// toHmbdcIpcable converts the message into a POD type or a hasMemoryAttachment derived
    /// message for transfer
    /// fromHmbdcIpcable converts the above message back
    struct Serialized
    : hasMemoryAttachment  /// make the message contains an attachment to hold std::string's content
    , hasTag<1001> {       /// the serialized message must have the same tag number as original
        Serialized(Announcement const& announcement) {  /// construct the serialized message
            hasMemoryAttachment& att = *this;
            att.len = announcement.msg.size();
            att.attachment = malloc(att.len);
            memcpy(att.attachment, announcement.msg.c_str(), att.len);
            att.afterConsumedCleanupFunc = [](hasMemoryAttachment* h) {
                ::free(h->attachment);                  /// how to release the attachment
            };
        }

        /// how to convert it back to Announcement - deserialize
        Announcement fromHmbdcIpcable() {
            auto& att = (hasMemoryAttachment&)(*this);
            Announcement res;
            res.msg = std::string((char*)att.attachment, att.len);
            return res;
        };
    };

    /// entrance to serilize Announcement
    auto toHmbdcIpcable() const {
        return Serialized{*this};
    }
};

//
//this is the message going back and forth carrying text conversations - unlimited in size
//
//we make all the chat groups use the same ChatMessage type, but each group will
//be assigned a unique tag number ranging from 1002 - (1002 + 100)
//
struct ChatMessage 
: hasSharedPtrAttachment<ChatMessage, char[]>    //message text is saved in a shared_ptr<char[]>
                                                 //no limit on the attachment length 
, inTagRange<1002, 100> {           //up to 100 chat groups; only configured 3 in the example
    ChatMessage(char const* myId, uint16_t grouId, char const* msg)
    : hasSharedPtrAttachment(std::shared_ptr<char[]>(new char[strlen(msg) + 1]), strlen(msg) + 1)
    , inTagRange(grouId) {
        snprintf(id, sizeof(id), "%s", myId);
        snprintf(hasSharedPtrAttachment::attachmentSp.get(), hasSharedPtrAttachment::len
            , "%s", msg);
    }

    char id[16];            //chatter

    // ChatMessage is a Message type whose tag is runtime determined (vs hasTag type of message that is 
    // statically tagged Message type - see Announcement), this method is used in the tag determination
    static uint16_t getGroupId(std::string const& group) {
        static const std::string groupNames[] = {{"tennis"}, {"volleyball"}, {"basketball"}};
        auto it = std::find(std::begin(groupNames), std::end(groupNames), group);
        //returns group id which is used as the tag offset (against th base tag 1002)
        if (it == std::end(groupNames)) throw std::out_of_range("non-existing group");
        return it - std::begin(groupNames);
    }
};

/// Admin node that runs in a thread and gets message callbacks
struct Admin 
: Node<Admin, std::tuple<ChatMessage>, std::tuple<Announcement>>
, TimerManager {                                                                        // <======
    Admin()
    : reportTimer_(Duration::seconds(10), [this](TimerManager&, SysTime const&) {       // <======
        HMBDC_LOG_n("chatMessageCount = ", chatMessageCount_); // to avoid message interleaving
    }) { //start the timer
        schedule(SysTime::now(), reportTimer_);                                         // <======
    }

    /// message callback - won't compile if missing
    void handleMessageCb(ChatMessage const& m) {
        chatMessageCount_++;                                                            
        HMBDC_LOG_N(m.id, ": ", m.attachmentSp.get());
    }

    void annouce(std::string&& text) {
        Announcement m;
        m.msg = std::move(text);
        /// never blocking best effort publish                                          // <======
        tryPublish(m);
    }

    private:
    ReoccuringTimer reportTimer_;                                                  
    size_t chatMessageCount_ = 0;
};

/// Chatter node
struct Chatter 
: Node<Chatter, std::tuple<Announcement, ChatMessage>, std::tuple<ChatMessage>> {
    Chatter(std::string id, std::string group)
    : id(id)
    , groupId(ChatMessage::getGroupId(group)) {
    }

    /// unlike Admin who subscribes all ChatMessage
    /// for ChatMessage Chatter only subscribes to one tag offset number (groupId: 0 - 99)
    /// tennis uses the tag offset = 0, valleyball = 1, ...
    /// here we tell the framework what the tag offset is for ChatMessage
    /// Note Admin does not implemet this function and all ChatMessage
    /// are delivered to Admin regardles of the tag offset of each message
    void addTypeTagRangeSubsForCfg(ChatMessage*, std::function<void(uint16_t)> addOffsetInRange) const {
        addOffsetInRange(groupId);
    }

    void handleMessageCb(Announcement const& m) {
        cout << "ADMIN ANNOUCEMENT: " << m.msg << endl;
        if (m.msg == "TERM") {
            cout << "Press Enter to exit" << endl;
            stopFlag = true;
            throw 0; //any exception in callback signal end of messaging
        }
    }

    void handleMessageCb(ChatMessage const& m) {
        if (id != m.id) { //do not reprint myself
            cout << m.id << ": " << m.attachmentSp.get() << endl;
        }
    }

    /// thread safe function
    void say(std::string const& something) {
        ChatMessage m(id.c_str(), groupId, something.c_str());
        publish(m); /// can publish from any thread
    }

    string const id;
    uint16_t const groupId;
    std::atomic<bool> stopFlag{false};
};

int main(int argc, char** argv) {
    using namespace std;
    if (argc != 4) {
        cerr << argv[0] << " local-ip chat-group-name my-name" << endl;
        cerr << "multicast should be enabled on local-ip network" << endl;
        return -1;
    }
    std::string ifaceAddr = argv[1];
    std::string chatGroup = argv[2];
    std::string myId = argv[3];

    Config config; //other config values are default
    config.put("ifaceAddr", ifaceAddr);//which interface to use for communication
    
    SingletonGuardian<tcpcast::Protocol> g; //RAII for tcpcast::Protocol resources
    
    /// hmbdc uses shared memory with very efficient algorithm for inter process commnunication on the same host
    /// to avoid unnecessary copying
    /// here we declare some properties for the shared memory config
    using IpcProp = ipc_property<4  /// up to 4 chatters (PROCESSES) on the SAME host to do IPC, largest number is 256
        , 1000                      /// largest sizeof(Message) size to send to IPC - ok to set a big enough value
                                    /// all IPC parties needs to match on this
    >;  

    if (myId == "admin") { //as admin
        /// cout used by 3 Admin threads mixes output
        /// use the a simple SynLogger to avoid it - see AsynLogger.hpp for high performance logger
        SingletonGuardian<SyncLogger> logGuard(std::cout);     /// logger RAII           // <=====
        SyncLogger::instance().setMinLogLevel(SyncLogger::L_NOTICE);    /// hide debug messages
        using SubMessages = typename Admin::RecvMessageTuple;
        using NetProp = net_property<tcpcast::Protocol
            , 1000
        >;

        using ChatDomain = Domain<SubMessages, IpcProp, NetProp>;
        auto domain = ChatDomain{config};           /// admin should create the chat group and own it
                                                    /// so run it first

        /// a thread pool of 3 Admins - they collectively handle its messages           // <======
        vector<unique_ptr<Admin>> admins;                                               // <======
        for (auto i = 0; i < 3; ++i) {                                                  // <======
            admins.emplace_back(new Admin);                                             // <======
        }                                                                               // <======
        domain.addPool(admins.data(), admins.data() + 3).startPumping(); // start pool of 3 threads  // <======

        //we can read the admin's input and send messages out now
        string line;
        cout << "start typing a message" << endl;
        cout << "ctrl-d to terminate" << endl;

        while(getline(cin, line)) {
            admins[0]->annouce(std::move(line));
        }
        admins[0]->annouce("TERM");
        sleep(1); //so the message does go out to the network
        domain.stop();
        domain.join();
    } else {  //as a chatter
        using SubMessages = typename Chatter::RecvMessageTuple;
        using NetProp = net_property<tcpcast::Protocol
            , 128
        >;

        using ChatDomain = Domain<SubMessages, IpcProp, NetProp>;
        auto domain = ChatDomain{config};
        if (domain.ownIpcTransport()) {
            cout << "You own the IPC transport now!" << endl;
        }
        Chatter chatter(myId, chatGroup);
        domain.add(chatter).startPumping();

        //we can read the user's input and send messages out now
        string line;
        cout << "start type a message" << endl;
        cout << "ctrl-d to terminate" << endl;

        while(!chatter.stopFlag && getline(cin, line)) {
            chatter.say(line);
        }

        domain.stop();
        domain.join();
    }
}
