//on top of chat.cpp, this is to show how to use zero copy (0cpy) feature of TIPS to accelerate IPC
//communication - with minimum programming
//search for <====== mark in the code where the key additions related to timer and thread pool
//
//Additionally it demonstrates how to write ChatterInterface as Node interface classes that 
//diff concrete Chatter Nodes can derived from. Since it involves virtual functions and it could be
//slightly slower than the previous Chatter that do not use this technique
//
//the app join a chat group and start to have group chat
//to run:
//./chat3 <local-ip> admin admin                  # start the groups by admin, and keep it running
//./chat3 <local-ip> <chat-group-name> <my-name>  # join the group
//to build:
//g++ chat3.cpp -std=c++1z -D BOOST_BIND_GLOBAL_PLACEHOLDERS -pthread  -Ipath-to-boost -lrt -o /tmp/chat
//
//to debug:
//somtimes you need to reset shared memory by "rm /dev/shm/*" 
//on each host, the shared memory is owned (create and delete) by the first chat process started on local host
//- see ipcTransportOwnership config for shared memory ownership in tips/DefaultUserConfig.hpp
//
#include "hmbdc/tips/tcpcast/Protocol.hpp" //use tcpcast for communication
#include "hmbdc/tips/Tips.hpp"


#include <iostream>
#include <string>
#include <memory>
#include <unistd.h>

using namespace std;
using namespace hmbdc::app;
using namespace hmbdc::tips;

//
//this is the message the administrator "annouces", each Chatter subscribes to it
//to hear what the admin has to say regardless of chatting group
//
struct Announcement 
: hasTag<1001> {
    char msg[1000];         //1000 char max per chat message
};

//
//this is the message going back and forth carrying text conversations - unlimited in size
//
//we make all the chat groups use the same ChatMessage type, but each group will
//be assigned a unique tag number ranging from 1002 - (1002 + 100)
//
struct ChatMessage 
: hasSharedPtrAttachment<ChatMessage, char[]>
, hasTag<1002, 100> {       //up to 100 chat groups; only configured 3 in the example
    template <typename Node>
    ChatMessage(char const* myId, uint16_t grouId, char const* msg, Node& node)
    : hasTag(grouId) {
        /// allocate memory from shm - and it is managed by TIPS                <======
        /// no need to worry about deallocate afterwards on local machine
        /// or on other processes or remote hosts, the message type works everywhere
        node.allocateInShmFor0cpy(*this, strlen(msg) + 1); ///   <===== that's it! fill and send it away
        snprintf(id, sizeof(id), "%s", myId);
        snprintf(hasSharedPtrAttachment::attachmentSp.get(), strlen(msg) + 1
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
: Node<Admin, std::tuple<ChatMessage>, std::tuple<Announcement>> {
    /// message callback - won't compile if missing
    void handleMessageCb(ChatMessage const& m) {
        cout << m.id << ": " << m.attachmentSp.get() << endl;
    }

    void annouce(char const* text) {
        Announcement m;
        snprintf(m.msg, sizeof(m.msg), "%s", text);
        /// never blocking best effort publish
        tryPublish(m);
    }
};

/// Chatter node
struct ChatterInterface
: Node<ChatterInterface, std::tuple<Announcement, ChatMessage>, std::tuple<ChatMessage>> {
    virtual void addTypeTagRangeSubsForCfg(ChatMessage*, std::function<void(uint16_t)> addOffsetInRange) const  = 0;
    virtual void handleMessageCb(Announcement const& m) = 0;
    virtual void handleMessageCb(ChatMessage const& m) = 0;
    virtual void say(std::string const& something) = 0;
    virtual bool stopped() const = 0;
};

struct Chatter : ChatterInterface {
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
    void addTypeTagRangeSubsForCfg(ChatMessage*, std::function<void(uint16_t)> addOffsetInRange) const override {
        addOffsetInRange(groupId);
    }

    void handleMessageCb(Announcement const& m) override {
        cout << "ADMIN ANNOUCEMENT: " << m.msg << endl;
        if (std::string(m.msg) == "TERM") {
            cout << "Press Enter to exit" << endl;
            stopFlag_ = true;
            throw 0; //any exception in callback signal end of messaging
        }
    }

    void handleMessageCb(ChatMessage const& m) override {
        if (id != m.id) { //do not reprint myself
            cout << m.id << ": " << m.attachmentSp.get() << endl;
        }
    }

    /// thread safe function
    void say(std::string const& something) override {
        ChatMessage m(id.c_str(), groupId, something.c_str(), *this);
        publish(m); /// can publish from any thread
    }

    bool stopped() const override {
        return stopFlag_;
    }

    string const id;
    uint16_t const groupId;

    private:
        std::atomic<bool> stopFlag_{false};
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
    /// here we declare some properties for the shared memory config
    using IpcProp = ipc_property<4  /// up to 4 chatters on the SAME host to do IPC, largest number is 256
        , 1000                      /// largest message size to send to IPC - 1000 is more than enough
                                    /// all IPC parties needs to match on this
    >;  

    if (myId == "admin") { //as admin
        using SubMessages = typename Admin::RecvMessageTuple;
        using NetProp = net_property<tcpcast::Protocol
            , 1000  /// big enough to hold largest message (excluding attachment - which isn't compile time 
                    /// determined anyway) to send to net
                    /// compile time checked if that is the case - make it 64 bytes to see the compiling error
        >;

        using ChatDomain = Domain<SubMessages, IpcProp, NetProp>;
        auto domain = ChatDomain{config};           /// admin should create the chat group and own it
                                                    /// so run it first
        Admin admin;
        domain.add(admin).startPumping();
        //we can read the admin's input and send messages out now
        string line;
        cout << "start type a message" << endl;
        cout << "ctrl-d to terminate" << endl;

        while(getline(cin, line)) {
            admin.annouce(line.c_str());
        }
        admin.annouce("TERM");
        sleep(1); //so the message does go out to the network
        domain.stop();
        domain.join();
    } else {  //as a chatter
        using SubMessages = typename Chatter::RecvMessageTuple;
        using NetProp = net_property<tcpcast::Protocol
            , 128 /// largest message size to send to net
        >;

        using ChatDomain = Domain<SubMessages, IpcProp, NetProp>;
        auto domain = ChatDomain{config};
        if (domain.ownIpcTransport()) {
            cout << "You own the IPC transport now!" << endl;
        }
        Chatter concreteChatter(myId, chatGroup);
        ChatterInterface& chatter{concreteChatter};

        // use the interface not the concrete
        domain.add(chatter).startPumping();

        //we can read the user's input and send messages out now
        string line;
        cout << "start typing a message" << endl;
        cout << "ctrl-d to terminate" << endl;

        while(!chatter.stopped() && getline(cin, line)) {
            chatter.say(line); //now reliable publish
        }

        domain.stop();
        domain.join();
    }
}
