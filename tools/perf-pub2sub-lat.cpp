#include "hmbdc/tips/Tips.hpp"
#include "hmbdc/tips/tcpcast/Protocol.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/time/Timers.hpp"
#include "hmbdc/time/Rater.hpp"
#include "hmbdc/os/Signals.hpp"
#include "hmbdc/numeric/Stat.hpp"

#include <boost/program_options.hpp>
#include <iostream>

using namespace hmbdc;

namespace {
size_t msgSize;
size_t msgRate;
std::string pumpCpuMask;
size_t reportPeriod;
bool do0cpy;
bool pumpRecvDirectly;
}

struct Payload0cpy {
    Payload0cpy(size_t payloadLen) {
        for (auto i = 0u; i < payloadLen; ++i) {
            meat[i] = (i & 0xff);
        }
    }
    size_t hmbdc0cpyShmRefCount;
    char meat[1];               /// open ended
};

struct Message0cpy
: tips::hasSharedPtrAttachment<Message0cpy, Payload0cpy>
, app::hasTag<1001> {
    time::SysTime pubTs;
};

struct MessageGT1K
: tips::hasSharedPtrAttachment<MessageGT1K, uint8_t[]>
, app::hasTag<1002> {
    MessageGT1K(size_t payloadLen) {
        len = payloadLen;
        attachmentSp.reset(new uint8_t[len]);
    }
    time::SysTime pubTs;
};

struct Receiver
: tips::Node<Receiver, std::tuple<Message0cpy, MessageGT1K>>
, time::TimerManager
, time::ReoccuringTimer {
    Receiver()
    : time::ReoccuringTimer(hmbdc::time::Duration::seconds(reportPeriod)) {
        setCallback([this](auto&& ...) {
            report();
        });
        schedule(hmbdc::time::SysTime::now(), *this);
    }

    void handleMessageCb(Message0cpy const& m) {
        lat_.add(time::SysTime::now() - m.pubTs);
        msgSize_ = m.len - sizeof(size_t);
    }

    void handleMessageCb(MessageGT1K const& m) {
        lat_.add(time::SysTime::now() - m.pubTs);
        msgSize_ = m.len;
    }

    void report() {
        HMBDC_LOG_n("msgSize=", msgSize_, " latencies ", lat_);
        lat_ = decltype(lat_){}; // reset
    }

    numeric::Stat<hmbdc::time::Duration> lat_;
    size_t msgSize_ = 0;
};

template <typename...>
struct pumpRecvDirectlyContext {
    template <typename ...Args>
    pumpRecvDirectlyContext(Args&& ...args){}

    Receiver* recv_ = nullptr;
    template <typename ...Args>
    void start(Receiver& recv, Args&&...) {
        recv_ = &recv;
    }

    template <typename Message>
    void send(Message&& m) {
        recv_->handleMessageCb(std::forward<Message>(m));
    }

    ~pumpRecvDirectlyContext() {
        recv_->report();
    }

    void stop(){}
    void join(){}
};

int main(int argc, char** argv) {
    pattern::SingletonGuardian<app::SyncLogger> logGuard(std::cout);

    namespace po = boost::program_options;
    po::options_description desc("Allowed options");
    auto helpStr = 
    "This program can be used to test hmbdc TIPS zero copy pub/sub latencies."
    ;
    desc.add_options()
    ("help", helpStr)
    ("msgSize", po::value<size_t>(&::msgSize)->default_value(1000000), "published message size")
    ("msgRate", po::value<size_t>(&msgRate)->default_value(0), "how many messages to publish per sec, 0 means it is a subscriber")
    ("pumpCpuMask", po::value(&pumpCpuMask)->default_value("0x1"), "specify the cpu mask for pumps")
    ("reportPeriod", po::value(&reportPeriod)->default_value(1), "report latencies every N seconds - only effective when pumpRecvDirectly is false")
    ("pumpRecvDirectly", po::value(&pumpRecvDirectly)->default_value(false), "it is possible to use pump to pump the receiver directly to improve latency")
    ("do0cpy", po::value(&do0cpy)->default_value(true), "demo zero copy IPC (effective on publisher side only)");



    po::positional_options_description p;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).
        options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    app::Config cfg;
    cfg.put("pumpCpuAffinityHex", pumpCpuMask);

    using NetProt = tips::tcpcast::Protocol;
    pattern::SingletonGuardian<NetProt> ncGuard;

    if (msgRate) { /// publisher
        using MyDomain = tips::Domain<std::tuple<>
            , tips::ipc_property<64, 64>
            , tips::net_property<NetProt>>;
        cfg.put("ipcTransportOwnership", "own");
        
        MyDomain domain(cfg);
        domain.addPub<std::tuple<Message0cpy, MessageGT1K>>();

        MessageGT1K msg{msgSize};
        Message0cpy msg0cpy;
        domain.allocateInShmFor0cpy(msg0cpy, sizeof(size_t) + msgSize, msgSize);
        time::Rater rater(time::Duration::seconds(1), msgRate, msgRate);

        bool stopped = false;
        /// handle ctrl-c
        hmbdc::os::HandleSignals::onTermIntDo([&](){
            stopped = true;
            domain.stop();
        });

        std::cout << "start all receivers, then press enter to start ..." << std::endl;
        std::string line;
        getline(std::cin, line);
        std::cout << "started ..." << std::endl;
        while (!stopped) {
            usleep(1000);
            while (rater.check()) {
                if (do0cpy) {
                    msg0cpy.pubTs = time::SysTime::now();
                    domain.publish(msg0cpy);
                } else {
                    msg.pubTs = time::SysTime::now();
                    domain.publish(msg);
                }
                rater.commit();
            }
        }
        domain.join();
    } else if (pumpRecvDirectly) { /// subscriber
        using MyDomain = tips::Domain<Receiver::Interests
            , tips::ipc_property<64, 64>
            , tips::net_property<NetProt>
            , pumpRecvDirectlyContext>;
        app::Config cfg;
        cfg.put("ipcTransportOwnership", "attach");
        cfg.put("pumpMaxBlockingTimeSec", "0.000001");
        MyDomain domain(cfg);
        Receiver recver;
        domain.start(recver);
        std::cout << "pumpRecvDirectly, press ctrl-c to see the whole latency report" << std::endl;

        /// handle ctrl-c
        hmbdc::os::HandleSignals::onTermIntDo([&](){domain.stop();});
        
        domain.join();
    } else {/// subscriber
        using MyDomain = tips::Domain<Receiver::Interests
            , tips::ipc_property<64, 64>
            , tips::net_property<NetProt>>;
        app::Config cfg;
        cfg.put("ipcTransportOwnership", "attach");
        cfg.put("pumpMaxBlockingTimeSec", "0.000001");
        MyDomain domain(cfg);
        Receiver recver;
        domain.start(recver);

        /// handle ctrl-c
        hmbdc::os::HandleSignals::onTermIntDo([&](){domain.stop();});
        
        domain.join();
    }
    return 0;
}
