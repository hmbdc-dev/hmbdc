#include "hmbdc/tips/tcpcast/Protocol.hpp"
#include "hmbdc/tips/rmcast/Protocol.hpp"
#include "hmbdc/tips/rnetmap/Protocol.hpp"

#include "hmbdc/tips/SingleNodeDomain.hpp"
#include "hmbdc/tips/Tips.hpp"
#include <boost/program_options.hpp>
#include "hmbdc/numeric/Stat.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/os/Signals.hpp"

#include <iostream>
#include <vector>
#include <memory>
#include <functional>
#include <unistd.h>
#include <sys/mman.h>

using namespace hmbdc::tips;
using namespace hmbdc::app;
using namespace hmbdc::time;
using namespace hmbdc::numeric;
using namespace std;


namespace {
double pumpMaxBlockingTimeSec;
bool logging;
string netprot;
string role;
string netIface, netIface2;
std::vector<uint16_t> cpuIndex;
size_t skipFirst;
uint32_t msgSize;
uint32_t msgPerSec;
bool use0cpy;
uint32_t runTime;
}

struct Ping
: hasTag<1001> {
    enum {
        max_payload = 100,
    };
    Ping(size_t s)
    : size(s)
    , ts(SysTime::now())
    {}

    Ping(Ping const& ping) {
        memcpy((void*)this, &ping, ping.size);
    }

    size_t size;
    SysTime ts;
    char padding[max_payload - 16];

    template<typename NoUse> 
    static void init(NoUse&){}
    static void fini(){}
    static constexpr const char* type = "Ping";
};

struct Pong
: hasTag<1002> {
    Pong(Ping const& ping) {
        memcpy((void*)this, &ping, ping.size);
    }
    Pong(Pong const& pong) {
        memcpy((void*)this, &pong, pong.size);
    }

    size_t size;
    SysTime ts;
    char padding[Ping::max_payload - 16];
};

struct Payload0cpy {
    Payload0cpy(size_t payloadLen) {
        for (auto i = 0u; i < payloadLen; ++i) {
            meat[i] = (i & 0xff);
        }
    }
    size_t hmbdc0cpyShmRefCount = 0;
    char meat[1];               /// open ended
};

struct Ping0cpy
: hasSharedPtrAttachment<Ping0cpy, Payload0cpy>
, hasTag<1003> {
    Ping0cpy(size_t payloadLen) {
        assert(payloadLen == toSend_s.len - sizeof(size_t));
        *this = toSend_s;
        ts = SysTime::now();
    }
    SysTime ts;
    static Ping0cpy toSend_s;
    static constexpr const char* type = "Ping0cpy";

    template<typename Alloc> 
    static void init(Alloc& alloc) {
        alloc.allocateInShmFor0cpy(toSend_s, msgSize + sizeof(size_t), msgSize);
    }

    static void fini() {
        /// this is needed because when alloc is out of scope, all the allocated mmeory is freed
        /// need to explictly call this before that
        toSend_s.attachmentSp.reset();
    }

private:
    Ping0cpy(){};
};
Ping0cpy Ping0cpy::toSend_s;

struct Pong0cpy
: hasSharedPtrAttachment<Pong0cpy, Payload0cpy>
, hasTag<1004> {
    Pong0cpy(Ping0cpy const& ping) {
        attachmentSp = ping.attachmentSp;
        len = ping.len;
        ts = ping.ts;
    }
    SysTime ts;
};

struct PingGT1K
: hasSharedPtrAttachment<PingGT1K, uint8_t[]>
, hasTag<1005> {
    PingGT1K(size_t payloadLen)
    : PingGT1K::hasSharedPtrAttachment(attachment_s, payloadLen)
    , ts(SysTime::now()) {
    }
    SysTime ts;
    static std::shared_ptr<uint8_t[]> attachment_s;
    static constexpr const char* type = "PingGT1K";

    template<typename NoUse> 
    static void init(NoUse&){
        PingGT1K::attachment_s.reset(new uint8_t[msgSize]);
        for (size_t i = 0; i < msgSize; ++i) {
            PingGT1K::attachment_s[i] = i & 0xff;
        }
    }
    static void fini(){}
};

std::shared_ptr<uint8_t[]> PingGT1K::attachment_s;


struct PongGT1K
: hasSharedPtrAttachment<PongGT1K, uint8_t[]>
, hasTag<1006> {
    PongGT1K(PingGT1K const& ping)
    : PongGT1K::hasSharedPtrAttachment(ping.attachmentSp, ping.len)
    , ts(ping.ts) {
    }
    SysTime ts;
};

template <typename Ping, typename Pong>
struct Pinger 
: Node<Pinger<Ping, Pong>
    , std::tuple<Pong>
    , std::tuple<Ping>
> {
    char const* hmbdcName() const { 
        return "pinger"; 
    } 

    void messageDispatchingStartedCb(size_t const*) override {
        cout << "Started Pinger with the first " << skipped_ 
            << " values ignored(x), press ctrl-c to get results" << endl;
    };

    void handleMessageCb(Pong const& m) {
        auto now = SysTime::now();
        auto lat = now - m.ts;
        if (!skipped_) { 
            stat_.add(lat);
        } else {
            --skipped_;
        }
    }

    void invokedCb(size_t) override {
        if (hmbdc_unlikely(rater_.check())) {
            if (++periodicPingCount_ == msgPerSec_) {
                cout << (skipped_?'x':'.') << flush;
                periodicPingCount_ = 0;
            }
            Ping p(msgSize_);
            this->publish(p);
            rater_.commit();
            if (!skipped_) pingCount_++;
        }
    }

    void stoppedCb(std::exception const& e) override {
        cerr << e.what() << endl;
    }

    void finalReport() {
        cout << "\n" << Ping::type << " : msgSize=" << msgSize_ << " msgPerSec=" << msgPerSec_ 
            << "\nround trip time (sec):(" << stat_.sampleSize() << '/' << pingCount_ << "):";
        cout << stat_ << endl;
        
        if (!stat_.sampleSize()) {
            cout << "Nothing comes back - multicast might be disabled (need to disable firewall)";
            cout << endl;
        }
    }

private:
    Rater rater_{Duration::seconds(1), msgPerSec, msgPerSec};
    Stat<Duration> stat_;
    size_t periodicPingCount_ = 0;
    size_t pingCount_ = 0;
    size_t skipped_ = skipFirst;
    uint32_t msgPerSec_ = msgPerSec;
    uint32_t msgSize_ = msgSize;
};


struct Ponger 
: Node<Ponger
    , std::tuple<Ping, PingGT1K, Ping0cpy>
    , std::tuple<Pong, PongGT1K, Pong0cpy>
> {
    char const* hmbdcName() const { 
        return "ponger"; 
    }

    void messageDispatchingStartedCb(size_t const*) override {
        cout << "Started Ponger, press ctrl-c to stop" << endl;
    };

    void handleMessageCb(Ping const& m) {
        auto now = SysTime::now();
        this->publish(Pong(m));
        auto lat = now - m.ts;
        if (!skipped_) { 
            stat_.add(lat);
        } else {
            --skipped_;
        }
    }

    void handleMessageCb(PingGT1K const& m) {
        auto now = SysTime::now();
        this->publish(PongGT1K(m));
        auto lat = now - m.ts;
        if (!skipped_) { 
            stat_.add(lat);
        } else {
            --skipped_;
        }
    }

    void handleMessageCb(Ping0cpy const& m) {
        auto now = SysTime::now();
        this->publish(Pong0cpy(m));
        auto lat = now - m.ts;
        if (!skipped_) { 
            stat_.add(lat);
        } else {
            --skipped_;
        }
    }

    void stoppedCb(std::exception const& e) override {
        cerr << e.what() << endl;
    }

    void finalReport() {
        cout << "\nif clock is synced, one way latency (sec):\n";
        cout << stat_;
        cout << endl;
    }

private:
    Stat<Duration> stat_;
    size_t skipped_ = skipFirst;
};

template <typename NetProt, typename Ping, typename Pong>
int 
runPingInDomain(Config const& config, uint16_t pingCpu) {
    SingletonGuardian<NetProt> g;
    Domain<std::tuple<Pong>, ipc_property<>, net_property<NetProt>> domain{config};
    Pinger<Ping, Pong> pinger;
    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });

    Ping::init(domain);

    domain.start(pinger
        , 1024 * 128
        , hmbdc::time::Duration::milliseconds(0)
        , 1ul << pingCpu);

    if (runTime) {
        sleep(runTime);
        domain.stop();
    }
    domain.join();
    Ping::fini();
    pinger.finalReport();
    return 0;
}

template <typename NetProt>
int 
runPongInDomain(Config const& config, uint16_t pongCpu) {
    SingletonGuardian<NetProt> g;
    Domain<std::tuple<Ping, PingGT1K, Ping0cpy>, ipc_property<>, net_property<NetProt>>
        domain{config};
    Ponger ponger;
    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });
    if (runTime) {
        sleep(runTime);
        domain.stop();
    }

    domain.start(ponger
        , 1024 * 128
        , hmbdc::time::Duration::milliseconds(0)
        , 1ul << pongCpu);
    domain.join();
    ponger.finalReport();
    return 0;
}

template <typename NetProt, typename Ping, typename Pong>
int 
runPingInSingleNodeDomain(Config const& config) {
    SingletonGuardian<NetProt> g;
    SingleNodeDomain<Pinger<Ping, Pong>
        , std::tuple<Pong>, ipc_property<>, net_property<NetProt>> domain{config};
    Pinger<Ping, Pong> pinger;
    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });

    Ping::init(domain);
    domain.start(pinger);

    if (runTime) {
        sleep(runTime);
        domain.stop();
    }
    domain.join();
    Ping::fini();
    pinger.finalReport();
    return 0;
}

template <typename NetProt>
int 
runPongInSingleNodeDomain(Config const& config) {
    SingletonGuardian<NetProt> g;
    SingleNodeDomain<Ponger
        , std::tuple<Ping, PingGT1K, Ping0cpy>, ipc_property<>, net_property<NetProt>>
        domain{config};
    Ponger ponger;
    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });
    domain.start(ponger);

    if (runTime) {
        sleep(runTime);
        domain.stop();
    }
    domain.join();
    ponger.finalReport();
    return 0;
}

template <typename NetProt, typename Ping, typename Pong>
int 
runPingPong(Config const& config, uint16_t pingCpu, uint16_t pongCpu) {
    SingletonGuardian<NetProt> g;
    Pinger<Ping, Pong> pinger;
    Ponger ponger;
    Domain<typename aggregate_recv_msgs<decltype(pinger), decltype(ponger)>::type
        , ipc_property<>, net_property<NetProt>> domain{config};

    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });

    Ping::init(domain);
    domain.start(ponger
        , 1024 * 128
        , hmbdc::time::Duration::milliseconds(0)
        , 1ul << pingCpu);
    domain.start(pinger
        , 1024 * 128
        , hmbdc::time::Duration::seconds(1)
        , 1ul << pongCpu);
        
    if (runTime) {
        sleep(runTime);
        domain.stop();
    }
    domain.join();
    Ping::fini();
    ponger.finalReport();
    pinger.finalReport();
    return 0;
}

int 
main(int argc, char** argv) {
    using namespace std;

    namespace po = boost::program_options;
    po::options_description desc("Allowed options");
    auto helpStr = 
    "Warning: hmbdc TIPS pub/sub uses multicast functions and it is imperative that multicast is enabled (stop the firewall?) on your network for this test to work."
    "This program can be used to test round trip hmbdc TIPS pub/sub message latency among a group of Nodes(in the same process or on the same or different hosts)."
    "Half or the round trip latency can be interpreted as the onw way latency."
    "\nUsage example: \nponger1$ ./tips-ping-pong -I 192.168.2.101\nponger2$ ./tips-ping-pong -I 192.168.2.102"
    "\npinger$ ./tips-ping-pong -r ping -I 192.168.2.100"
    "\nwould start the test with two pongers on two hosts then a pinger on the third."
    "\nctrl-c to stop them in the reverse order as they were started."
    ;
    desc.add_options()
    ("help", helpStr)
    ("netprot,n", po::value<string>(&netprot)->default_value("tcpcast"), "tcpcast, rmcast, rnetmap or nonet(localhost)")
    ("role,r", po::value<string>(&role)->default_value("pong"), "ping (sender process), pong (echoer process) or both (sender and echoer in the same process")
    ("use0cpy", po::value<bool>(&use0cpy)->default_value(true), "use 0cpy IPC for intra-host communications when msgSize > 1000B")
    ("msgSize", po::value<uint32_t>(&msgSize)->default_value(16), "msg size in bytes, 16B-100MB - the limit is specific to the test, hmbdc does not put limits")
    ("msgPerSec", po::value<uint32_t>(&msgPerSec)->default_value(500), "msg per second, the test uses default config - if you see bad_alloc exception, reduce the rate")
    ("netIface,I", po::value<string>(&netIface)->default_value("127.0.0.1"), "interface to use, for example: 192.168.0.101 or 192.168.1.0/24.")
    ("netIface2", po::value<string>(&netIface2)->default_value(""), "if netprot uses a second (backup) interface (rmcast, rnetmap) - specify here, otherwise it uses the netIface.")
    ("cpuIndex", po::value(&cpuIndex)->multitoken()->default_value({0}, "0"), "specify the cpu index numbers to use - net results can benefit from 2 CPUs (pump, ping/pong), expect exact 3 CPUs when role=both (pump, pinger, ponger)")
    ("skipFirst", po::value<size_t>(&skipFirst)->default_value(1u), "skipp the first N results (buffering & warming up stage) when collecting latency stats")
    ("runTime", po::value<uint32_t>(&runTime)->default_value(0), "how many seconds does the test last before exit. By default it runs forever")
    ("pumpMaxBlockingTimeSec", po::value<double>(&pumpMaxBlockingTimeSec)->default_value(0), "for low latency results use 0 - for low CPU utilization make it bigger - like 0.00001 sec")
    ("logging", po::value<bool>(&logging)->default_value(false), "turn on hmbdc internal logging - to stdout")
;

    po::positional_options_description p;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).
        options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << desc << "\n";
        return 0;
    }
    Config config;


    config.put("ipcMaxMessageSizeRuntime", sizeof(Ping));
    config.put("netMaxMessageSizeRuntime", sizeof(Ping));
    config.put("pumpMaxBlockingTimeSec", pumpMaxBlockingTimeSec);
    config.put("pumpCpuAffinityHex", 1ul << cpuIndex[0]);
    config.put("ipcMessageQueueSizePower2Num", 21);
    if (netIface2.size()) config.put("tcpIfaceAddr", netIface2);  
    msgSize = max<uint32_t>(msgSize, 16);

    auto run = [&](auto* netProt) {
        std::optional<SingletonGuardian<SyncLogger>> logGuard;
        if (logging) logGuard.emplace(std::cout);
        using Protocol = typename std::decay<decltype(*netProt)>::type;
        if (role == "ping" ) {
            if (cpuIndex.size() == 1) {
                if (msgSize < Ping::max_payload) {
                    return runPingInSingleNodeDomain<Protocol, Ping, Pong>(config);
                } else if (use0cpy) {
                    return runPingInSingleNodeDomain<Protocol, Ping0cpy, Pong0cpy>(config);
                } else {
                    return runPingInSingleNodeDomain<Protocol, PingGT1K, PongGT1K>(config);
                }
            } else {
                if (msgSize < Ping::max_payload) {
                    return runPingInDomain<Protocol, Ping, Pong>(config, cpuIndex[1]);
                } else if (use0cpy) {
                    return runPingInDomain<Protocol, Ping0cpy, Pong0cpy>(config, cpuIndex[1]);
                } else {
                    return runPingInDomain<Protocol, PingGT1K, PongGT1K>(config, cpuIndex[1]);
                }
            }
        } else if (role == "pong") {
            if (cpuIndex.size() == 1) {
                return runPongInSingleNodeDomain<Protocol>(config);
            } else {
                return runPongInDomain<Protocol>(config, cpuIndex[1]);
            }
        } else if (role == "both") {
            if (cpuIndex.size() != 3) {
                cerr << "3 cpuIndex, for pump, ping and pong expected" << "\n";
                return -1;
             }
            if (msgSize < Ping::max_payload) {
                return runPingPong<Protocol, Ping, Pong>(config, cpuIndex[1], cpuIndex[2]);
            } else if (use0cpy) {
                return runPingPong<Protocol, Ping0cpy, Pong0cpy>(config, cpuIndex[1], cpuIndex[2]);
            } else {
                return runPingPong<Protocol, PingGT1K, PongGT1K>(config, cpuIndex[1], cpuIndex[2]);
            }
        } else {
            cerr << desc << "\n";
            return -2;
        }
    };
    if (netprot == "tcpcast") {
        config.put("ifaceAddr", netIface);
        config.put("outBufferSizePower2", 18);
        return run((tcpcast::Protocol*)nullptr);
    } else if (netprot == "rmcast") {
        config.put("ifaceAddr", netIface);
        config.put("outBufferSizePower2", 18);  /// rmcast and rnetmap need this much to handle 100MB
        return run((rmcast::Protocol*)nullptr);
    } else if (netprot == "rnetmap") {
        config.put("netmapPort", netIface);
        config.put("outBufferSizePower2", 18);  /// rmcast and rnetmap need this much to handle 100MB
        return run((rnetmap::Protocol*)nullptr);
    } if (netprot == "nonet") {
        return run((NoProtocol*)nullptr);
    } else {
        cerr << desc << "\n";
        return -3;
    }
}
