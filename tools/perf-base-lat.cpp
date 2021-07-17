#include "hmbdc/app/Base.hpp"
#include "hmbdc/app/BlockingContext.hpp"
#include "hmbdc/pattern/MonoLockFreeBuffer.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/os/Signals.hpp"
#include "hmbdc/numeric/StatHistogram.hpp"

#include <boost/program_options.hpp>
#include <boost/range/combine.hpp>
#include <iostream>
#include <functional>
#include <memory>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>

using namespace hmbdc;
using namespace hmbdc::app;
using namespace hmbdc::time;
using namespace hmbdc::numeric;
using namespace std;

struct Message
: hasTag<1001> {
    Message() 
    : sendTs(SysTime::now())
    {}
    time::SysTime sendTs;
};

namespace {
    char ctxType;
    vector<uint16_t> sendCpus;
    vector<uint16_t> recvCpus;
    vector<uint16_t> poolCpus;
    uint16_t poolSendCount;
    uint16_t poolRecvCount;
    uint16_t dumbPoolRecvCount;
    size_t skipFirst;
    uint16_t bufferSizePower2;
    size_t msgPerSec;
    char ipcRole;
}

template <typename Context>
struct SenderClient 
: Client<SenderClient<Context>>
, TimerManager
, ReoccuringTimer {
    using ptr = std::shared_ptr<SenderClient>;
    SenderClient(Context& ctx, uint16_t id, size_t msgPerSec)
    : ReoccuringTimer(Duration::seconds(1u))
    , ctx_(ctx)
    , id_(id)
    , periodicMessageCount_(0)
    , rater_(Duration::seconds(1u), msgPerSec, msgPerSec, msgPerSec != 0) {
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
        if (rater_.check()) {
            ctx_.template trySendInPlace<Message>();
            rater_.commit();
            periodicMessageCount_++;
        }
    }

    void stoppedCb(std::exception const& e) override {
        std::cout << (e.what()) << std::endl;
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
    Rater rater_;
};


using Hist = StatHistogram<Duration, false>;
template <typename Context>
struct ReceiverClient 
: Client<ReceiverClient<Context>, Message>
, TimerManager
, ReoccuringTimer {
    using ptr = std::shared_ptr<ReceiverClient>;

    ReceiverClient(Context& ctx, uint16_t id)
    : ReoccuringTimer(Duration::seconds(1u))
    , ctx_(ctx)
    , id_(id)
    , periodicMessageCount_(0)
    , hist_(Duration::microseconds(0), Duration::microseconds(1000), 10000) {
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
        if (!skipFirst) {
            hist_.add(SysTime::now() - r.sendTs);
        } else {
            skipFirst--;
        }
    }

    void stoppedCb(std::exception const& e) override {
        std::cout << (e.what()) << std::endl;
    };

    void report() {
        HMBDC_LOG_n("receiver ", id_, ": mps=", periodicMessageCount_
        );

        periodicMessageCount_ = 0u;
    }

    void showHist() {
        cout << "\nlatencies receiver " << id_ << ':';
        hist_.display(cout, {0, 1, 10, 50, 75, 90, 99, 99.9, 99.99, 100});
        cout << endl;
    }

    Context& ctx_;
    uint16_t id_;
    size_t periodicMessageCount_;
    Hist hist_;
};

struct DumbReceiverClient
: Client<DumbReceiverClient, Flush> {
    using ptr = std::shared_ptr<DumbReceiverClient>;
    void handleMessageCb(Flush const&){}
};


template <typename Ctx>
void run() {    
    Ctx ctx(bufferSizePower2, dumbPoolRecvCount + poolSendCount + poolRecvCount);

    vector<typename ReceiverClient<Ctx>::ptr> receivers;
    for (uint16_t i = 0; i< recvCpus.size(); ++i) {
        auto r = new ReceiverClient<Ctx>(ctx, i);
        receivers.emplace_back(r);
        ctx.start(*r, 1ul << recvCpus[i]);
    }
    vector<typename SenderClient<Ctx>::ptr> senders;
    for (uint16_t i = 0; i< sendCpus.size(); ++i) {
        auto s = new SenderClient<Ctx>(ctx, i, msgPerSec);
        senders.emplace_back(s);
        ctx.start(*s, 1ul << sendCpus[i]);
    }

    ctx.start(poolCpus.size(), numeric::fromSetBitIndexes<uint64_t>(poolCpus));
    for (uint16_t i = recvCpus.size(); poolCpus.size() && i < recvCpus.size() + poolRecvCount; ++i) {
        auto r = new ReceiverClient<Ctx>(ctx, i);
        receivers.emplace_back(r);
        ctx.addToPool(*r);
    }
    for (uint16_t i = sendCpus.size(); poolCpus.size() && i < sendCpus.size() + poolSendCount; ++i) {
        auto s = new SenderClient<Ctx>(ctx, i, msgPerSec);
        senders.emplace_back(s);
        ctx.addToPool(*s);
    }

    vector<typename DumbReceiverClient::ptr> dumbReceivers;
    for (uint16_t i = 0; i < dumbPoolRecvCount; ++i) {
        auto r = new DumbReceiverClient;
        dumbReceivers.emplace_back(r);
        ctx.addToPool(*r);
    }

    os::HandleSignals::onTermIntDo(
        [&ctx]() {
        ctx.stop();
    });

    cout << "ctrl-c to stop" << endl;
    ctx.join();

    for (auto r : receivers) {
        r->showHist();
    }
}

template <typename Ctx>
void runBlocking() {        
    Ctx ctx;
    vector<typename ReceiverClient<Ctx>::ptr> receivers;
    for (uint16_t i = 0; i< recvCpus.size(); ++i) {
        auto r = new ReceiverClient<Ctx>(ctx, i);
        receivers.emplace_back(r);
        ctx.start(*r, 1ul << recvCpus[i]);
    }
    vector<typename SenderClient<Ctx>::ptr> senders;
    for (uint16_t i = 0; i< sendCpus.size(); ++i) {
        auto s = new SenderClient<Ctx>(ctx, i, msgPerSec);
        senders.emplace_back(s);
        ctx.start(*s, 1ul << sendCpus[i]);
    }

    auto poolCpuMask = numeric::fromSetBitIndexes<uint64_t>(poolCpus);
    for (uint16_t i = recvCpus.size(); poolCpus.size() && i < recvCpus.size() + poolRecvCount; ++i) {
        auto r = new ReceiverClient<Ctx>(ctx, i);
        receivers.emplace_back(r);
        ctx.start(*r, poolCpuMask);
    }
    for (uint16_t i = sendCpus.size(); poolCpus.size() && i < sendCpus.size() + poolSendCount; ++i) {
        auto s = new SenderClient<Ctx>(ctx, i, msgPerSec);
        senders.emplace_back(s);
        ctx.start(*s, poolCpuMask);
    }

    vector<typename DumbReceiverClient::ptr> dumbReceivers;
    for (uint16_t i = 0; i < dumbPoolRecvCount; ++i) {
        auto r = new DumbReceiverClient;
        dumbReceivers.emplace_back(r);
        ctx.start(*r);
    }

    os::HandleSignals::onTermIntDo(
        [&ctx]() {
        ctx.stop();
    });

    cout << "ctrl-c to stop" << endl;
    ctx.join();

    for (auto r : receivers) {
        r->showHist();
    }
}

template <typename Ctx>
void runIpc(bool ifCreate, bool sender, bool receiver) {
    int ownership = ifCreate? 1 : -1;
    Ctx ctx(ownership, "hmbdcperf", bufferSizePower2);
    //its is impossible the receiver not reading anything for 2 sec in this app 
    //the default value is 60 sec
    ctx.setSecondsBetweenPurge(2);

    vector<typename ReceiverClient<Ctx>::ptr> receivers;
    vector<typename SenderClient<Ctx>::ptr> senders;
    if (sender == receiver) {//creator
        ctx.start();
    } else if (sender) {
        for (uint16_t i = 0; i< sendCpus.size(); ++i) {
            auto s = new SenderClient<Ctx>(ctx, i, msgPerSec);
            senders.emplace_back(s);
            ctx.start(*s, 1ul << sendCpus[i]);
        }
    } else {
        for (uint16_t i = 0; i< recvCpus.size(); ++i) {
            auto r = new ReceiverClient<Ctx>(ctx, i);
            receivers.emplace_back(r);
            ctx.start(*r, 1ul << recvCpus[i]);
        }
    }

    ctx.start();
    os::HandleSignals::onTermIntDo(
        [&ctx]() {
        ctx.stop();
    });
    
    cout << "ctrl-c to stop" << endl;
    ctx.join();
    for (auto r : receivers) {
        r->showHist();
    }
}

using PartionContext = hmbdc::app::Context<24, hmbdc::app::context_property::partition, hmbdc::app::context_property::msgless_pool>;
using BroadcastContext = hmbdc::app::Context<24, hmbdc::app::context_property::broadcast<>>;

using IpcCreatorBroadcastContext = hmbdc::app::Context<24
    , hmbdc::app::context_property::broadcast<>
    , hmbdc::app::context_property::ipc_enabled
>;

using IpcAttacherBroadcastContext = hmbdc::app::Context<24
    , hmbdc::app::context_property::broadcast<>
    , hmbdc::app::context_property::ipc_enabled
>;
using IpcReceiverBroadcastContext = IpcAttacherBroadcastContext;
using IpcSenderBroadcastContext = IpcAttacherBroadcastContext;

using IpcCreatorPartitionContext = hmbdc::app::Context<24
    , hmbdc::app::context_property::partition
    , hmbdc::app::context_property::ipc_enabled
>;

using IpcAttacherPartitionContext = hmbdc::app::Context<24
    , hmbdc::app::context_property::partition
    , hmbdc::app::context_property::ipc_enabled
>;
using IpcReceiverPartitionContext = IpcAttacherPartitionContext;
using IpcSenderPartitionContext = IpcAttacherPartitionContext;
using BlockingCtx = hmbdc::app::BlockingContext<
        ReceiverClient<void*>::Interests
        , DumbReceiverClient::Interests
>;

int main(int argc, char** argv) {
    SingletonGuardian<SyncLogger> logGuard(std::cout);

    namespace po = boost::program_options;
    po::options_description desc("Allowed options");
    auto helpStr = 
    "This program can be used to test hmbdc-free thread and ipc messaging latency performance among a group of cores."
    "sender cores send messages at a specified rate and receiver cores receive and measure message latencies\n"
    "Note: if the rate specified is too high, the results could be impacted by the message processing speed and not accurate.\n"
    "The source code is in example dir: perf-base-lat.cpp"

    "Example:\nrunning \n$ ./perf-base-lat \n"
    "would start the test using default setting.";
    desc.add_options()
    ("help", helpStr)
    ("ctxType", po::value<char>(&ctxType)->default_value('b'), "x-blocking, b-broadcast, p-partition - see context_property")
    ("msgPerSec", po::value<size_t>(&msgPerSec)->default_value(0), "the sender sending messages per second - 0 means no rate control")
    ("bufferSizePower2,b", po::value<uint16_t>(&bufferSizePower2)->default_value(10u), "2^bufferSizePower2 is the message buffer size")
    ("sendCpus,s", po::value(&sendCpus)->multitoken(), "specify the cpu index numbers for each sender in direct mode")
    ("recvCpus,r", po::value(&recvCpus)->multitoken(), "specify the cpu index numbers for each receiver in direct mode")
    ("poolCpus,p", po::value(&poolCpus)->multitoken(), "specify the cpu index numbers powering the pool")
    ("poolSendCount", po::value(&poolSendCount)->default_value(0), "sender count in pool")
    ("poolRecvCount", po::value(&poolRecvCount)->default_value(0), "receiver count in pool")
    ("dumbPoolRecvCount", po::value(&dumbPoolRecvCount)->default_value(0), "dumb receiver (ones that not interested in the message type in test) count in pool")
    ("skipFirst", po::value<size_t>(&skipFirst)->default_value(1u), "skipp the first N results (buffering & warming up stage) when collecting latency stats")
    ("ipc", po::value<char>(&ipcRole)->default_value('n'), 
        "testing ipc, start one ipc creator (c), ipc sender (s) or ipc receiver (r). "
        "only --partition -b and -C are effective and --partition and -b settings need to be the same when starting "
        "the creator (c), sender (s), and receiver (r). Only one creator is allowed in a test.");

    po::positional_options_description p;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).
        options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << desc << "\n";
        return 0;
    }
    if (poolRecvCount && dumbPoolRecvCount && ctxType == 'p' && poolCpus.size()) {
        cout << "partition pool Clients cannot receive messages." << "\n";
        return 1;
    }

    switch (ipcRole) {
        case 'n': {
            if (ctxType == 'p') run<PartionContext>();
            else if (ctxType == 'b') run<BroadcastContext>();
            else runBlocking<BlockingCtx>();
        } break;
        case 'c': {
            if (ctxType == 'p') runIpc<IpcCreatorPartitionContext>(true, false, false);
            else runIpc<IpcCreatorBroadcastContext>(true, false, false);
        } break;
        case 's': {
            if (ctxType == 'p') runIpc<IpcSenderPartitionContext>(false, true, false);
            else runIpc<IpcSenderBroadcastContext>(false, true, false);
        } break;
        case 'r': {
            if (ctxType == 'p') runIpc<IpcReceiverPartitionContext>(false, false, true);
            else runIpc<IpcReceiverBroadcastContext>(false, false, true);
        } break;
    }
    return 0;
}
