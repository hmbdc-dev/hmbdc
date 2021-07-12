//on top of chat.cpp, this is to show how to use zero copy (0cpy) feature of TIPS to accelerate IPC
//communication - with minimum programming
//search for <====== mark in the code where the key additions related to timer and thread pool
//
//the app join a chat group and start to have group chat
//to run:
//./chat3 <local-ip> admin admin                  # start the groups by admin, and keep it running
//./chat3 <local-ip> <chat-group-name> <my-name>  # join the group
//to build:
//g++ chat3.cpp -std=c++1z -D BOOST_BIND_GLOBAL_PLACEHOLDERS -Wall -Werror -pthread  -Ipath-to-boost -lrt -o /tmp/chat
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

struct TextInShm {                      ///                                     <======
    size_t hmbdc0cpyShmRefCount;        /// as the first field inidicating this is allocated in shared memory
                                        /// the above is compile time checked if not true
    char text[1];                       /// openended - do not know the length
};

struct ChatMessage 
: hasSharedPtrAttachment<ChatMessage, TextInShm>    /// message text is saved in a shared_ptr<TextInShm> <======
                                                    /// recipients even in a differt host gets the same type
                                                    /// valid shared_ptr to use
, inTagRange<1002, 100> {           //up to 100 chat groups; only configured 3 in the example
    template <typename Domain>
    ChatMessage(char const* myId, uint16_t grouId, char const* msg, Domain& domain)
    : inTagRange(grouId) {
        /// allocate memory from shm - and it is managed by TIPS                <======
        /// no need to worry about deallocate afterwards on local machine
        /// or on other processes or remote hosts, the message type works everywhere
        domain.allocateInShmFor0cpy(*this
            , sizeof(TextInShm::hmbdc0cpyShmRefCount) + strlen(msg) + 1); ///   <===== - that's it! send it away
        snprintf(id, sizeof(id), "%s", myId);
        snprintf(hasSharedPtrAttachment::attachmentSp->text, strlen(msg) + 1
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
template <typename ChatDomain>
struct Admin 
: Node<Admin<ChatDomain>, std::tuple<ChatMessage>> { //only subscribe ChatMessage
    Admin(ChatDomain& domain)
    : domain_(domain)
    {}

    /// specify what types to publish
    using SendMessageTuple = std::tuple<Announcement>;

    /// message callback - won't compile if missing
    void handleMessageCb(ChatMessage const& m) {
        cout << m.id << ": " << m.attachmentSp->text << endl;
    }

    void annouce(char const* text) {
        Announcement m;
        snprintf(m.msg, sizeof(m.msg), "%s", text);
        domain_.publish(m);
    }

    private:
    ChatDomain& domain_;
};

/// Chatter node
struct Chatter 
: Node<Chatter, std::tuple<Announcement, ChatMessage>> { //subscribe both Announcement and ChatMessage
    /// specify what types to publish
    using SendMessageTuple = std::tuple<ChatMessage>;

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
    void addTypeTagRangeSubsFor(ChatMessage*, std::function<void(uint16_t)> addOffsetInRange) const {
        addOffsetInRange(groupId);
    }

    void handleMessageCb(Announcement const& m) {
        cout << "ADMIN ANNOUCEMENT: " << m.msg << endl;
        if (std::string(m.msg) == "TERM") {
            cout << "Press Enter to exit" << endl;
            stopFlag = true;
            throw 0; //any exception in callback signal end of messaging
        }
    }

    void handleMessageCb(ChatMessage const& m) {
        if (id != m.id) { //do not reprint myself
            cout << m.id << ": " << m.attachmentSp->text << endl;
        }
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
    /// here we declare some properties for the shared memory config
    using IpcProp = ipc_property<4  /// up to 4 chatters on the SAME host to do IPC, largest number is 256
        , 1000                      /// largest message size to send to IPC - 1000 is more than enough
                                    /// all IPC parties needs to match on this
    >;  

    if (myId == "admin") { //as admin
        using SubMessages = typename Admin<void*>::RecvMessageTuple;
        using NetProp = net_property<tcpcast::Protocol
            , 1400  /// big enough to hold largest message (excluding attachment - which isn't compile time 
                    /// determined anyway) to send to net
                    /// compile time checked if that is the case - make it 64 bytes to see the compiling error
        >;

        using ChatDomain = Domain<SubMessages, IpcProp, NetProp>;
        auto domain = ChatDomain{config};           /// admin should create the chat group and own it
                                                    /// so run it first
        Admin<ChatDomain> admin{domain};
        domain.start(admin);
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
        Chatter chatter(myId, chatGroup);
        domain.start(chatter);

        //we can read the user's input and send messages out now
        string line;
        cout << "start type a message" << endl;
        cout << "ctrl-d to terminate" << endl;

        while(!chatter.stopFlag && getline(cin, line)) {
            ChatMessage m(myId.c_str(), chatter.groupId, line.c_str(), domain);
            domain.publish(m); //now publish
        }

        domain.stop();
        domain.join();
    }
}
