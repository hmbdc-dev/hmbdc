#include "hmbdc/app/Base.hpp"
#include "hmbdc/pattern/MonoLockFreeBuffer.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/os/Signals.hpp"
#include "hmbdc/numeric/StatHistogram.hpp"

#include <boost/program_options.hpp>
#include <boost/range/combine.hpp>
#include <iostream>
#include <memory>
#include <functional>
#include <unistd.h>
#include <sys/mman.h>
 #include <signal.h>

using namespace hmbdc;
using namespace hmbdc::app;
using namespace hmbdc::time;
using namespace hmbdc::numeric;
using namespace std;

using MyLogger = SyncLogger;

struct Message
: hasTag<1001> {
    Message() : seq(0)
    {}
    size_t seq;
    friend ostream& operator << (ostream& os, Message const& r) {
        os << "Message " << ' ' << r.seq;
        return os;
    }

} __attribute__((__packed__));


template <typename Context>
struct SenderClient 
: Client<SenderClient<Context>>
, TimerManager
, ReoccuringTimer {
    using ptr = shared_ptr<SenderClient>;
    SenderClient(Context& ctx, uint16_t id)
    : ReoccuringTimer(Duration::seconds(1u))
    , ctx_(ctx)
    , id_(id)
    , periodicMessageCount_(0) {
        rawMessageSize_ = sizeof(MessageWrap<Message>);
        setCallback(
            [this](TimerManager& tm, SysTime const& now) {
            report();
            }
        );
        schedule(SysTime::now(), *this);
    }

    char const* hmbdcName() const { 
        return "perf-tx"; 
    }

    /*virtual*/ 
    void invokedCb(size_t) HMBDC_RESTRICT override {
        Message m[20];
        ctx_.send(m[0]
            , m[1]
            , m[2]
            , m[3]
            , m[4]
            , m[5]
            , m[6]
            , m[7]
            , m[8]
            , m[9]
            , m[10]
            , m[11]
            , m[12]
            , m[13]
            , m[14]
            , m[15]
            , m[16]
            , m[17]
            , m[18]
            , m[19]
        );
        periodicMessageCount_+=20;
    }

    void stoppedCb(exception const& e) override {
        cout << (e.what()) << endl;
    };

    void report() {
        HMBDC_LOG_n("sender ", id_, ":msgSize=", sizeof(Message), " rawMessageSize="
            , rawMessageSize_, " mps=", periodicMessageCount_);
        periodicMessageCount_ = 0u;
    }

    Context& ctx_;
    uint16_t id_;
    size_t periodicMessageCount_;
    size_t rawMessageSize_;
};


template <typename Context>
struct ReceiverClient 
: Client<ReceiverClient<Context>, Message>
, TimerManager
, ReoccuringTimer {
    using ptr = shared_ptr<ReceiverClient>;

    ReceiverClient(Context& ctx, uint16_t id)
    : ReoccuringTimer(Duration::seconds(1u))
    , ctx_(ctx)
    , id_(id)
    , periodicMessageCount_(0)
    {
        setCallback(
            [this](TimerManager& tm, SysTime const& now) {
            report();
            }
        );
        schedule(SysTime::now(), *this);
    }

    char const* hmbdcName() const { 
        return "perf-rx"; 
    }

    void handleMessageCb(Message const& r) {
        periodicMessageCount_++;
    }

    void stoppedCb(exception const& e) override {
        cout << (e.what()) << endl;
    };

    void report() {
        HMBDC_LOG_n("receiver ", id_, ": mps=", periodicMessageCount_
        );

        periodicMessageCount_ = 0u;
    }

    Context& ctx_;
    uint16_t id_;
    size_t periodicMessageCount_;
};

namespace {
    bool part;
    vector<uint16_t> sendCpus;
    vector<uint16_t> recvCpus;
    uint16_t bufferSizePower2;
    char ipcRole;
    bool usePool;
}

using MyLogCtx = hmbdc::app::Context<24, hmbdc::app::context_property::partition>;


template <typename Ctx>
void run() {
    uint64_t poolMask = 0;
    Ctx ctx(bufferSizePower2);

    vector<typename ReceiverClient<Ctx>::ptr> receivers;
    for (uint16_t i = 0; i< recvCpus.size(); ++i) {
        auto r = new ReceiverClient<Ctx>(ctx, i);
        receivers.emplace_back(r);
        if (!usePool) {
            ctx.start(*r, 1ul << recvCpus[i]);
        } else {
            poolMask |= 1ul << recvCpus[i];
            ctx.addToPool(*r, 1ul << i);
        }
    }
    vector<typename SenderClient<Ctx>::ptr> senders;
    for (uint16_t i = 0; i< sendCpus.size(); ++i) {
        auto s = new SenderClient<Ctx>(ctx, i);
        senders.emplace_back(s);
        if (!usePool) {
            ctx.start(*s, 1ul << sendCpus[i]);
        } else {
            poolMask |= 1ul << sendCpus[i];
            ctx.addToPool(*s, 1ul << (i + recvCpus.size()));
        }
    }
    if (usePool) {
        ctx.start(recvCpus.size() + sendCpus.size(), poolMask);
    }

    os::HandleSignals::onTermIntDo(
        [&ctx]() {
            ctx.stop();
        }
    );
    ctx.join();
}

template <typename Ctx>
void runIpc(bool ifCreate, bool sender, bool receiver) {    
    uint64_t poolMask = 0;
    int ownership = ifCreate?1:-1;
    Ctx ctx(ownership, "hmbdcperf", bufferSizePower2);
    //its is impossible the receiver not reading nayting for 2 sec in this app 
    //the default value is 60 sec
    ctx.setSecondsBetweenPurge(2);
    vector<typename ReceiverClient<Ctx>::ptr> receivers;
    vector<typename SenderClient<Ctx>::ptr> senders;
    if (sender == receiver) {//creator
        ctx.start();
    } else if (sender) {
        for (uint16_t i = 0; i< sendCpus.size(); ++i) {
            auto s = new SenderClient<Ctx>(ctx, i);
            senders.emplace_back(s);
            if (!usePool) {
                ctx.start(*s, 1ul << sendCpus[i]);
            } else {
                poolMask |= 1ul << sendCpus[i];
                ctx.addToPool(*s, 1ul << i);
            }
        }
        if (usePool) {
            ctx.start(sendCpus.size(), poolMask);
        }
    } else {
        for (uint16_t i = 0; i< recvCpus.size(); ++i) {
            auto r = new ReceiverClient<Ctx>(ctx, i);
            receivers.emplace_back(r);
            if (!usePool) {
                ctx.start(*r, 1ul << recvCpus[i]);
            } else {
                poolMask |= 1ul << recvCpus[i];
                ctx.addToPool(*r, 1ul << i);
            }
        }
        if (usePool) {
            ctx.start(recvCpus.size(), poolMask);
        }
    }
    os::HandleSignals::onTermIntDo(
        [&ctx]() {
            ctx.stop();
        }
    );

    cout << "ctrl-c to stop" << endl;
    ctx.join();
}

using PartionContext = hmbdc::app::Context<24, hmbdc::app::context_property::partition, hmbdc::app::context_property::msgless_pool>;
using BroadcastContext = hmbdc::app::Context<24, hmbdc::app::context_property::broadcast<>>;

using IpcCreatorBroadcastContext = hmbdc::app::Context<24
    , hmbdc::app::context_property::broadcast<>
    , hmbdc::app::context_property::ipc_enabled
>;

using IpcReceiverBroadcastContext = hmbdc::app::Context<24
    , hmbdc::app::context_property::broadcast<>
    , hmbdc::app::context_property::ipc_enabled
>;
using IpcSenderBroadcastContext = hmbdc::app::Context<24
    , hmbdc::app::context_property::broadcast<>
    , hmbdc::app::context_property::ipc_enabled
    , hmbdc::app::context_property::msgless_pool
>;

using IpcCreatorPartitionContext = hmbdc::app::Context<24
    , hmbdc::app::context_property::partition
    , hmbdc::app::context_property::ipc_enabled
    , hmbdc::app::context_property::msgless_pool
>;

using IpcAttacherPartitionContext = hmbdc::app::Context<24
    , hmbdc::app::context_property::partition
    , hmbdc::app::context_property::ipc_enabled
    , hmbdc::app::context_property::msgless_pool
>;
using IpcReceiverPartitionContext = IpcAttacherPartitionContext;
using IpcSenderPartitionContext = IpcAttacherPartitionContext;



int main(int argc, char** argv) {
    SingletonGuardian<MyLogger> logGuard(cout);

    namespace po = boost::program_options;
    po::options_description desc("Allowed options");
    auto helpStr = 
    "This program can be used to test hmbdc-free thread and ipc messaging throughput performance among a group of cores."
    "The source code is in example dir: perf-base-thru.cpp"
    "senders cores send messages as fast as possible and receiver cores receive and count them.\n"
    "Example:\nrunning \n$ ./perf-base-thru \n"
    "would start the test using default setting."
    ;
    desc.add_options()
    ("help", helpStr)
    ("partition", po::value<bool>(&::part)->default_value(false), "test using partion context (vs the default broadcast context)")
//    ("sendBatch", po::value<size_t>(&sendBatch)->default_value(20), "the sender sending messages in batch of")
    ("bufferSizePower2,b", po::value<uint16_t>(&bufferSizePower2)->default_value(15u), "2^bufferSizePower2 is the message buffer size")
    ("sendCpus,s", po::value(&sendCpus)->multitoken()->default_value({0}, "0"), "specify the cpu index numbers for each sender")
    ("recvCpus,r", po::value(&recvCpus)->multitoken()->default_value({1}, "1"), "specify the cpu index numbers for each receiver")
    ("usePool", po::value(&usePool)->default_value(false), "put all clients into a pool powered by specified CPUs - not supported when partition=true")
    ("ipc", po::value<char>(&ipcRole)->default_value('n'), 
        "testing ipc, start one ipc creator (c), ipc sender (s) or ipc receiver (r). "
        "only --partition -b and -C are effective and they need to be the same when starting "
        "the creator (c) sender (s) and receiver (r). Only one creator is allowed in a test.");


    po::positional_options_description p;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).
        options(desc).positional(p).run(), vm);
    po::notify(vm);


    if (vm.count("help") || (usePool && part)) {
        cout << desc << "\n";
        return 0;
    }

    switch (ipcRole) {
        case 'n': {
            if (part) run<PartionContext>();
            else run<BroadcastContext>();
        } break;
        case 'c': {
            if (part) runIpc<IpcCreatorPartitionContext>(true, false, false);
            else runIpc<IpcCreatorBroadcastContext>(true, false, false);
        } break;
        case 's': {
            if (part) runIpc<IpcSenderPartitionContext>(false, true, false);
            else runIpc<IpcSenderBroadcastContext>(false, true, false);
        } break;
        case 'r': {
            if (part) runIpc<IpcReceiverPartitionContext>(false, false, true);
            else runIpc<IpcReceiverBroadcastContext>(false, false, true);
        } break;
    }
    return 0;
}
