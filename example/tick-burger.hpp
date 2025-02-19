#pragma once

#include "hmbdc/tips/Domain.hpp"
#include "hmbdc/tips/TickNode.hpp"

using hmbdc::app::hasTag;
using hmbdc::tips::Domain;
using hmbdc::tips::TickNode;
using hmbdc::tips::Node;
using hmbdc::tips::hasSharedPtrAttachment;
using hmbdc::time::SysTime;
using hmbdc::time::Duration;
using hmbdc::time::ReoccuringTimer;

auto TARGET_BURGER_COUNT = 100u;

/// @brief  define all messages here

/// to make things interesting, make it use attachment for Descriptions
struct BreadDesc {
    bool organic = false;
    bool wholeWheat = false;
};
struct Bread
: hasSharedPtrAttachment<Bread, BreadDesc>
, hasTag<1001> {
    Bread()
    : hasSharedPtrAttachment{SP{new BreadDesc}} {
        if (prodTime.nsecSinceEpoch() % 2 == 0) {
            hasSharedPtrAttachment::attachmentSp->organic = true;
            hasSharedPtrAttachment::attachmentSp->wholeWheat = true;
        }
    }
    SysTime prodTime{SysTime::now()}; 
};

struct BeefDesc {
    bool wagyu = false;
};
struct Beef : hasTag<1002> {    
    Beef() {
        if (prodTime.nsecSinceEpoch() % 3 == 0) {
            desc.wagyu = true;
        }
    }
    SysTime prodTime{SysTime::now()}; 
    BeefDesc desc;
};

struct BurgerDesc {
    bool premium = false;
};
struct Burger
: hasSharedPtrAttachment<Burger, BurgerDesc>
, hasTag<1003> {
    Burger(Beef beef, Bread bread)
    : hasSharedPtrAttachment{SP{new BurgerDesc}}  {
        // we perform the time check on when they were made
        if (bread.prodTime - beef.prodTime > Duration::milliseconds(5)
            || beef.prodTime - bread.prodTime > Duration::milliseconds(5)) {
            HMBDC_LOG_w("freshness=", beef.prodTime - bread.prodTime
                , " a little off due to the time spent on delivery is fast, but not 0");
        }
        if (beef.desc.wagyu && bread.attachmentSp->organic) {
            attachmentSp->premium = true;
        }
    }
};
struct Stop : hasTag<1004> {};

/// @brief  a regular Node that makes Bread every N milliseconds
struct BreadFactory : Node<BreadFactory
    , std::tuple<Burger>    // listen to Burger 
    , std::tuple<Bread>     // publish Bread
    , hmbdc::tips::will_schedule_timer   // production timer
> {
    uint64_t burgerCount{0};
    /// timer to produce a Bread every 21 ms
    ReoccuringTimer productionTimer{Duration::milliseconds(21)
        , [this](auto&&, auto&&) { publish(Bread{}); }
    };
    void messageDispatchingStartedCb(std::atomic<size_t> const*) override {
        HMBDC_LOG_n("Starting making Bread");
        schedule(SysTime::now(), productionTimer);
    }
    void handleMessageCb(Burger const&) {
        if (++burgerCount == TARGET_BURGER_COUNT) throw hmbdc::ExitCode{0};
    }
};

/// @brief  a regular Node that makes Beef every M milliseconds
struct BeefFactory : Node<BeefFactory
    , std::tuple<Burger>
    , std::tuple<Beef, Stop>
    , hmbdc::tips::will_schedule_timer
> {
    uint64_t burgerCount{0};
    uint64_t premiumBurgerCount{0};

    ReoccuringTimer productionTimer{Duration::milliseconds(31)
        , [this](auto&&, auto&&) { publish(Beef{}); }
    };
    void messageDispatchingStartedCb(std::atomic<size_t> const*) override {
        HMBDC_LOG_n("Starting making Beef");
        schedule(SysTime::now(), productionTimer);
    }

    void handleMessageCb(Burger const& burger) {
        HMBDC_LOG_n("burger count = ", ++burgerCount);
        if (burger.attachmentSp->premium) HMBDC_LOG_n("premium burger count = ", ++premiumBurgerCount);
        if (burgerCount == TARGET_BURGER_COUNT) {
            publish(Stop{});
            throw hmbdc::ExitCode{0};
        }
    }
};

/// @brief  a TickNode that listen to both Bread and Beef messages to make Burgers
struct BurgerFactory : TickNode<BurgerFactory
    , std::tuple<Bread, Beef>   // tick triggered by receiving a combo of Bread and Beef
    , std::tuple<Stop>          // additionallly listen to Stop
    , std::tuple<Burger>        // publishes Burger made from the Bread and Beef
> {
    uint64_t burgerCount{0};
    uint64_t premiumBurgerCount{0};

    /// @brief  if the Bread and Beef received wihin 5 millisec, we consider
    /// we can tick - user can customize the tick pred logic - see TickNode's ctor
    BurgerFactory() : TickNode{Duration::milliseconds(5)} {}
    void messageDispatchingStartedCb(std::atomic<size_t> const*) override {
        HMBDC_LOG_n("Starting making Burger");
    }

    void nontickCb(Stop const&) {
        HMBDC_LOG_n("Stop");
        throw hmbdc::ExitCode{0};
    }

    /// @brief bread and beef 
    void tickCb(Bread const& bread, Beef beef) {
        HMBDC_LOG_n("Made a burger freshness=", beef.prodTime - bread.prodTime);
        auto burger = Burger{beef, bread};
        publish(burger);
        ++burgerCount;
        if (burger.attachmentSp->premium) ++premiumBurgerCount;
    }
};
using IntraHostDomain = Domain<std::tuple<Bread, Beef, Burger, Stop>, hmbdc::tips::ipc_property<64, 0>>;