#include "hmbdc/tips/tcpcast/Protocol.hpp"
#include "hmbdc/tips/rmcast/Protocol.hpp"

#ifndef HMBDC_NO_NETMAP        
#include "hmbdc/tips/rnetmap/Protocol.hpp"
#endif

#include "hmbdc/tips/SingleNodeDomain.hpp"
#include "hmbdc/tips/Tips.hpp"
#include "hmbdc/numeric/Stat.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/os/Signals.hpp"

#include <boost/program_options.hpp>

#include <sstream>
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
vector<uint16_t> cpuIndex;
size_t skipFirst;
uint32_t msgSize;
uint32_t msgPerSec;
bool use0cpy;
uint32_t runTime;
bool show;
string showSection;
vector<string> additionalCfg;
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
    char meat[1];               /// open ended
};

struct Ping0cpy
: hasSharedPtrAttachment<Ping0cpy, Payload0cpy, true>
, hasTag<1003> {
    Ping0cpy(size_t payloadLen) {
        assert(payloadLen == toSend_s.len);
        *this = toSend_s;
        ts = SysTime::now();
    }
    SysTime ts;
    static Ping0cpy toSend_s;
    static constexpr const char* type = "Ping0cpy";

    template<typename Alloc> 
    static void init(Alloc& alloc) {
        alloc.allocateInShmFor0cpy(toSend_s, msgSize, msgSize);
    }

    static void fini() {
        /// this is needed because when alloc is out of scope, all the allocated mmeory is freed
        /// need to explictly call this before that
        toSend_s.reset();
    }

private:
    Ping0cpy(){};
};
Ping0cpy Ping0cpy::toSend_s;

struct Pong0cpy
: hasSharedPtrAttachment<Pong0cpy, Payload0cpy, true>
, hasTag<1004> {
    Pong0cpy(Ping0cpy const& ping) {
        reset(ping.isAttInShm, ping.attachmentSp, ping.len);
        ts = ping.ts;
    }
    SysTime ts;
};

struct PingGT1K
: hasSharedPtrAttachment<PingGT1K, uint8_t[]>
, hasTag<1005> {
    PingGT1K(size_t payloadLen)
    : ts(SysTime::now()) {
        hasSharedPtrAttachment::reset(false, attachment_s, payloadLen);
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
    : ts(ping.ts) {
        hasSharedPtrAttachment::reset(false, ping.attachmentSp, ping.len);
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
            << " values ignored(x), good(.), congested(C), press ctrl-c to get results" << endl;
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
            if (triedPingCount_++ % msgPerSec_ == 0) {
                cout << (skipped_?'x': contentions_ ? 'C' : '.') << flush;
                contentions_ = false;
            }
            Ping p(msgSize_);
            /// we do not want to block here even it merans we cannot send it out
            /// because the channel is temporarily saturated - we will try later
            if (this->tryPublish(p)) {
                rater_.commit();
                if (!skipped_) pingCount_++;
            } else {
                contentions_ = true;
            }
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
    size_t triedPingCount_ = 0;
    size_t pingCount_ = 0;
    size_t skipped_ = skipFirst;
    uint32_t msgPerSec_ = msgPerSec;
    uint32_t msgSize_ = msgSize;
    bool contentions_ = false;
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

static
void printAdditionalConfig(Config const& config) {
    auto content = config.content();
    namespace po = boost::program_options;
    po::options_description desc("Default configs: " + showSection);
    string noUse;
    for (auto it = content.begin(); it != content.end();) {
        auto paramIt = it;
        auto commentIt = ++it;
        desc.add_options()(paramIt->first.c_str()
            , po::value<string>(&noUse)->default_value(paramIt->second), commentIt->second.c_str());
        it++;
    }
    cout << desc << "\n";
}

template <typename NetProt, typename Ping, typename Pong>
int 
runPingInDomain(Config const& config, uint16_t pingCpu) {
    using MyDomain = Domain<std::tuple<Pong>, ipc_property<>, net_property<NetProt>>;
    if (show) { printAdditionalConfig(MyDomain::getDftConfig(showSection.c_str())); return 0; }
    SingletonGuardian<NetProt> g;
    MyDomain domain{config};

    Pinger<Ping, Pong> pinger;
    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });

    Ping::init(domain);

    domain.add(pinger
        , 1024 * 128
        , hmbdc::time::Duration::milliseconds(0)
        , 1ul << pingCpu);

    domain.startPumping();
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
    using MyDomain =
        Domain<std::tuple<Ping, PingGT1K, Ping0cpy>, ipc_property<>, net_property<NetProt>>;
    if (show) { printAdditionalConfig(MyDomain::getDftConfig(showSection.c_str())); return 0;}
    SingletonGuardian<NetProt> g;
    MyDomain domain{config};
    
    Ponger ponger;
    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });
    if (runTime) {
        sleep(runTime);
        domain.stop();
    }

    domain.add(ponger
        , 1024 * 128
        , hmbdc::time::Duration::milliseconds(0)
        , 1ul << pongCpu);

    domain.startPumping();
    domain.join();
    ponger.finalReport();
    return 0;
}

template <typename NetProt, typename Ping, typename Pong>
int 
runPingInSingleNodeDomain(Config const& config) {
    using MyDomain = SingleNodeDomain<Pinger<Ping, Pong>, ipc_property<>, net_property<NetProt>>;
    if (show) { printAdditionalConfig(MyDomain::getDftConfig(showSection.c_str())); return 0;}
    SingletonGuardian<NetProt> g;
    MyDomain domain{config};
    Pinger<Ping, Pong> pinger;
    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });

    Ping::init(domain);
    domain.add(pinger).startPumping();
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
    using MyDomain = SingleNodeDomain<Ponger, ipc_property<>, net_property<NetProt>>;
    if (show) { printAdditionalConfig(MyDomain::getDftConfig(showSection.c_str())); return 0;}
    SingletonGuardian<NetProt> g;
    MyDomain domain{config};
    Ponger ponger;
    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });
    domain.add(ponger).startPumping();

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
    using MyDomain = Domain<typename aggregate_recv_msgs<Pinger<Ping, Pong>, Ponger>::type
        , ipc_property<>, net_property<NetProt>>;
    if (show) { printAdditionalConfig(MyDomain::getDftConfig(showSection.c_str())); return 0;}
    SingletonGuardian<NetProt> g;
    Pinger<Ping, Pong> pinger;
    Ponger ponger;
    MyDomain domain{config};

    hmbdc::os::HandleSignals::onTermIntDo(
        [&domain]() {
        domain.stop();
    });

    Ping::init(domain);
    domain.add(ponger
        , 1024 * 128
        , hmbdc::time::Duration::milliseconds(0)
        , 1ul << pingCpu);
    domain.add(pinger
        , 1024 * 128
        , hmbdc::time::Duration::seconds(1)
        , 1ul << pongCpu);
    
    domain.startPumping();    
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
    "Warning: hmbdc TIPS pub/sub uses multicast functions and it is imperative that multicast is enabled (stop the firewall?) on your network for this test to work. "
    "If multicast isn't avaliable in you env, please contact us for the proxy solution (tcpcast). "
    "This program can be used to test round trip hmbdc TIPS pub/sub message latency among a group of Nodes(in the same process or on the same or different hosts). "
    "Half or the round trip latency can be interpreted as the onw way latency. "
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
    ("msgPerSec", po::value<uint32_t>(&msgPerSec)->default_value(500), "try send msg per second - would skip if blocking is expected, the test uses default config - if you see poor performance, reduce the rate or msgSize of increase the default config via --additional options")
    ("netIface,I", po::value<string>(&netIface)->default_value("127.0.0.1"), "interface to use, for example: 192.168.0.101 or 192.168.1.0/24.")
    ("netIface2", po::value<string>(&netIface2)->default_value(""), "if netprot uses a second (backup) interface (rmcast, rnetmap) - specify here, otherwise it uses the netIface.")
    ("cpuIndex", po::value(&cpuIndex)->multitoken()->default_value({0}, "0"), "specify the cpu index numbers to use - net results can benefit from 2 CPUs (pump, ping/pong), expect exact 3 CPUs when role=both (pump, pinger, ponger)")
    ("skipFirst", po::value<size_t>(&skipFirst)->default_value(1u), "skipp the first N results (buffering & warming up stage) when collecting latency stats")
    ("runTime", po::value<uint32_t>(&runTime)->default_value(0), "how many seconds does the test last before exit. By default it runs forever")
    ("pumpMaxBlockingTimeSec", po::value<double>(&pumpMaxBlockingTimeSec)->default_value(0), "for low latency results use 0 - for low CPU utilization make it bigger - like 0.00001 sec")
    ("show", po::value<string>(&showSection)->default_value("")->implicit_value(""), "empty, 'tx' or 'rx', display additional configs, network config has tx and rx sections")
    ("additional", po::value(&additionalCfg)->multitoken()->default_value({"outBufferSizePower2=18", "ipcMessageQueueSizePower2Num=21"}, "outBufferSizePower2=18 ipcMessageQueueSizePower2Num=21"), "specify the additional configs, example: '--additional mtu=64000 ipcTransportOwnership=own'. run --show option, or check out DefaultUserConfigure.hpp")
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
    show = showSection.size();

    Config config;
    config.put("ipcMaxMessageSizeRuntime", sizeof(Ping));
    config.put("netMaxMessageSizeRuntime", sizeof(Ping));
    config.put("pumpMaxBlockingTimeSec", pumpMaxBlockingTimeSec);
    config.put("pumpCpuAffinityHex", 1ul << cpuIndex[0]);
    if (netIface2.size()) config.put("tcpIfaceAddr", netIface2);  
    msgSize = max<uint32_t>(msgSize, 16);

    for (auto& param : additionalCfg) {
        std::string name, val;
        std::stringstream ss(param);
        std::getline(ss, name, '=');
        std::getline(ss, val);
        if (!ss) {
            cerr << "format error: " << param << endl;
            return -4;
        }
        config.put(name, val);
    }

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
                cerr << "3 cpuIndex, for pump, ping and pong expected, see --cpuIndex" << "\n";
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
        return run((tcpcast::Protocol*)nullptr);
    } else if (netprot == "rmcast") {
        config.put("ifaceAddr", netIface);
        return run((rmcast::Protocol*)nullptr);

#ifndef HMBDC_NO_NETMAP                
    } else if (netprot == "rnetmap") {
        config.put("netmapPort", netIface);
        return run((rnetmap::Protocol*)nullptr);
#endif        

    } else if (netprot == "nonet") {
        return run((NoProtocol*)nullptr);
    } else {
        cerr << desc << "\n";
        return -3;
    }
}
