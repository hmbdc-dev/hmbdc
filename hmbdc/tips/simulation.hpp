/**
 * @file simulation.hpp
 * @brief It's very simple to selectively play a pre-recorded messages bag and simualte the Nodes running in the real-time
 * environment. 
 * - The first step is to #define HMBDC_SIMULATION in the project. No need to change the Node's code.
 * - define a SimulationDomain instance to specify the bag and the messages filtering
 * - Add the Nodes into the above domain, then call domain.simulate() to play the messages
 * - domain.stop() and domain.join() - see the example code later in this file
 */
#include "hmbdc/time/Time.hpp"
#include "hmbdc/tips/Bag.hpp"
#include "hmbdc/tips/Tips.hpp"

#include <boost/mpl/for_each.hpp>

#include <optional>
#include <future>
#include <unordered_map>

#ifndef HMBDC_SIMULATION
static_assert(false, "HMBDC_SIMULATION is not defined");
#endif
namespace hmbdc::tips {

namespace sim_detail {
template <app::MessageTupleC FilterinMessages>
class BagReplayNode
: public Node<BagReplayNode<FilterinMessages>, std::tuple<>, FilterinMessages, will_schedule_timer> {
    using ipcable_filterin_messages = typename domain_detail::recv_ipc_converted<FilterinMessages>::type;

    template <std::size_t Index>
    void publishIpcable(uint16_t toPlayTag, std::unique_ptr<uint8_t[]>&& toPlay
        , size_t toPlayLen, app::hasMemoryAttachment* toPlayAtt) {
        if constexpr (Index < std::tuple_size_v<ipcable_filterin_messages>) {
            using IpcableMessage = std::tuple_element_t<Index, ipcable_filterin_messages>;
            bool hit = false;
            if constexpr (IpcableMessage::hasRange) 
                hit = toPlayTag >= IpcableMessage::typeTagStart && toPlayTag < IpcableMessage::typeTagStart + IpcableMessage::typeTagRange;
            else
                hit = toPlayTag == IpcableMessage::typeTagInSpec;
            if (hit) {
                auto& msg = *reinterpret_cast<IpcableMessage*>(toPlay.get());
                using hasMemoryAttachment = app::hasMemoryAttachment;
                if constexpr (has_fromHmbdcIpcable<IpcableMessage>::value) {
                    static_assert(std::is_same<IpcableMessage
                        , decltype(msg.fromHmbdcIpcable().toHmbdcIpcable())>::value
                        , "mising or wrong toHmbdcIpcable() func - cannot serialize");
                    this->publish(msg.fromHmbdcIpcable());
                } else {
                    this->publish(msg);
                }
                if constexpr(std::is_base_of<hasMemoryAttachment, IpcableMessage>::value) {
                    msg.hasMemoryAttachment::attachment = nullptr;
                    msg.hasMemoryAttachment::len = 0;
                }
            } else {
                publishIpcable<Index + 1>(toPlayTag, std::move(toPlay), toPlayLen, toPlayAtt);
            }
        }
    }

    public:
    BagReplayNode(char const* bagFile
        , std::optional<std::unordered_set<int16_t>> const& runtimeFilterinTags
        , std::optional<std::pair<time::SysTime, time::Duration>> bagPlayRange)
    : bagPlayRange{bagPlayRange, }
    , runtimeFilterinTags(runtimeFilterinTags)
    , playBag_{bagFile, runtimeFilterinTags, bagPlayRange} {
        if (runtimeFilterinTags) {
            auto tags = std::list<uint16_t>{runtimeFilterinTags->begin(), runtimeFilterinTags->end()};
            tuple_for_each<FilterinMessages>(
                [&tags](auto const& msg) mutable {
                    using RawMsg = std::decay_t<decltype(msg)>;
                    tags.erase(std::remove_if(tags.begin(), tags.end(), [](auto&& tag) {
                        return app::typeTagMatch<RawMsg>(tag);
                    }), tags.end());
                }
            );
            if (tags.size()) {
                for (const auto& tag : tags) {
                    HMBDC_LOG_W(tag, " is ineffective since it is in runtimeFilterinTags but not possible in FilterinMessages");
                }
            }
        }
    }

    void startSimulationClock() {
        static bool called = false;
        if (called) HMBDC_THROW(std::logic_error, "cannot start the clock twice");
        if (bagPlayRange) {
            time::SysTime::setNow(bagPlayRange->first);
        } else {
            time::SysTime::setNow(playBag_.bagHead().createdTs);
        }
        this->schedule(time::SysTime::now(), playBagFireTimer_);
        called = true;
    }

    std::optional<std::pair<time::SysTime, time::Duration>> const bagPlayRange;
    std::optional<std::unordered_set<int16_t>> const runtimeFilterinTags;

    void join() {
        bagPlayDoneFuture_.get();
    }

    private:
    InputBag playBag_;
    std::promise<bool> bagPlayDonePromise_;
    std::future<bool> bagPlayDoneFuture_{bagPlayDonePromise_.get_future()};

    // vars used in the Timer only
    uint16_t toPlayTag = 0;
    size_t toPlayLen{playBag_.bufWidth()};
    std::unique_ptr<uint8_t[]> toPlay{new uint8_t[toPlayLen]};
    app::hasMemoryAttachment* toPlayAtt = nullptr;
    time::ReoccuringTimer playBagFireTimer_{time::Duration::seconds(0)
        , [this] (auto&& tm, auto&& firedAt) mutable {
            if (toPlayTag) {
                publishIpcable<0>(toPlayTag, std::move(toPlay), toPlayLen, toPlayAtt);
            }
            while (true) {
                toPlayTag = 0;
                toPlayAtt = nullptr;
                auto bmh = playBag_.play(toPlayTag, toPlay.get(), toPlayAtt);
                if (playBag_.eof()) {
                    bagPlayDonePromise_.set_value(true);
                    break;
                }
                if (time::SysTime::now() >= bmh.createdTs) {
                    publishIpcable<0>(toPlayTag, std::move(toPlay), toPlayLen, toPlayAtt);
                } else {
                    playBagFireTimer_.resetInterval(bmh.createdTs - firedAt);
                    break;
                }
            }
        }
    };
};
} // sim_detail
/**
 * @brief specialized Domain used for simulation only. The simulation is defined by playing a previously recorded bag (see console)
 * as if those messages are published in this Domain, all Nodes added into this Domain will experience the same pub/sub behavior 
 * just like in the real world. The timing is simulated as real-time as when the bag was recorded. If the bag time spans 10min, the 
 * simulation will also take 10mins - To simulate the real world running, all the Nodes are running as if their current time is 
 * when the bag messages were recorded. All timers scheduled are also fired based on the simualted time. 
 * It is recommended Node uses Timers, SysTime and Duration defined in hmbdc to take advantages of the real-time simulation support
 * Example code for running a single Node(BurgerFactory) in simnulation:
 *   tips::SimulationDomain<BurgerFactory::RecvMessageTuple> domain{"./burger.bag"}; // only BurgerFactory::RecvMessageTuple messages will play in sim
 *   BurgerFactory burgerFactory;
 *   domain.add(burgerFactory); // can add more nodes in sim, they will intereact just like when the bag was recorded
 *   domain.simulate(); // playing the bag, burgerFactory runs in sim and gets all the messages it subscribes
 *   domain.stop();     // play done - stop the domain
 *   domain.join();     // join the threads
 * @tparam FilterinMessages The Message type tuple that are to be played from the bag in simuation
 */
template <app::MessageTupleC FilterinMessages>
class SimulationDomain : private Domain<FilterinMessages> {
    using Base = Domain<FilterinMessages>;
    sim_detail::BagReplayNode<FilterinMessages> replayNode_;

    public:
    /**
     * @brief Construct a new Simulation Domain object
     * 
     * @param bagFile What bag to use
     * @param runtimeFilterinTags selectively only playing those tags - this is used in combine with FilterinMessages
     * If the inconsistency is seen, a runtime WARNING is logged about the tags involved
     * @param bagPlayRange an optional pair specifying the time range in the bag to play in the simulation
     */
    SimulationDomain(char const* bagFile
        , std::optional<std::unordered_set<int16_t>> const& runtimeFilterinTags = std::nullopt
        , std::optional<std::pair<time::SysTime, time::Duration>> bagPlayRange = std::nullopt)
    : Base{hmbdc::app::Config{}}
    , replayNode_{bagFile, runtimeFilterinTags, bagPlayRange} {
        replayNode_.startSimulationClock();
    }

    /**
     * @brief add a Node into the Domain - see Domain.hpp
     * 
     */
    using Base::add;

    /**
     * @brief start simulation by playing the bag - before this, the user is exected to already add all the Nodes into the Domain
     * returns only when the bag is finished playing
     */
    void simulate() {
        this->add(replayNode_, 1024, time::Duration::seconds(0));
        Base::startPumping();
        replayNode_.join();
    }

    /**
     * @brief See Domain
     * 
     */
    using Base::stop;

    /**
     * @brief See Domain
     * 
     */
    using Base::join;
};
}

