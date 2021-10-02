#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/app/Message.hpp"
#include "hmbdc/app/Logger.hpp"
#include "hmbdc/app/Context.hpp"
#include "hmbdc/time/Timers.hpp"
#include "hmbdc/time/Time.hpp"
#include "hmbdc/pattern/BlockingBuffer.hpp"
#include "hmbdc/os/Thread.hpp"
#include "hmbdc/MetaUtils.hpp"
#include "hmbdc/Exception.hpp"

#include <type_traits>
#include <atomic>
#include <tuple>
#include <vector>
#include <thread>
#include <mutex>
#include <stdexcept>

namespace hmbdc { namespace app {
namespace blocking_context_detail {
HMBDC_CLASS_HAS_DECLARE(hmbdc_ctx_queued_ts);

template <typename Interests>
struct RealInterests {
    private:
    using no_trivial = typename filter_out_tuple<std::is_trivially_destructible
        , Interests
    >::type;
    public:
    /// avoid double delivery when JustBytes is involved
    using type = typename std::conditional<
        index_in_tuple<JustBytes, Interests>::value == std::tuple_size<Interests>::value
        , Interests
        , typename merge_tuple_unique<std::tuple<JustBytes>, no_trivial>::type
    >::type;
};

template <typename... MessageTuples>
struct context_property_aggregator {
    using Interests = std::tuple<>;
};

template <typename MessageTuple, typename... MessageTuples>
struct context_property_aggregator<MessageTuple, MessageTuples ...> {
    using Interests = typename hmbdc::merge_tuple_unique<
        MessageTuple, typename context_property_aggregator<MessageTuples ...>::Interests
    >::type;
};


template <typename CcClient>
auto waitDuration(CcClient const& c, time::Duration maxBlockingTime) {
    if constexpr(std::is_base_of<time::TimerManagerTrait, CcClient>::value) {
        return std::min(c.untilNextFire(), maxBlockingTime);
    } else {
        return maxBlockingTime;
    }
}

template <typename CcClient>
bool runOnceImpl(std::atomic<bool>& stopped
    , pattern::BlockingBuffer* HMBDC_RESTRICT buf
    , CcClient& HMBDC_RESTRICT c, time::Duration maxBlockingTime) {
    pattern::BlockingBuffer::iterator begin, end;
    try {
        if constexpr(std::is_base_of<time::TimerManagerTrait, CcClient>::value) {
            c.checkTimers(time::SysTime::now());
        }

        const bool clientParticipateInMessaging = 
            std::decay<CcClient>::type::INTERESTS_SIZE != 0;
        if (clientParticipateInMessaging && buf) {
            uint64_t count = buf->peek(begin, end);
            auto b = begin;
            c.CcClient::invokedCb(c.CcClient::handleRangeImpl(b, end, 0xffff));
            buf->wasteAfterPeek(count);
            if (!count) {
                buf->waitItem(waitDuration(c, maxBlockingTime));
            }
        } else {
            c.CcClient::invokedCb(0);
            auto d = waitDuration(c, maxBlockingTime);
            if (d) usleep(d.microseconds());
        }
    } catch (std::exception const& e) {
        if (!stopped) {
            c.stopped(e);
            return !c.dropped();
        }
    } catch (int code) {
        if (!stopped) {
            c.stopped(ExitCode(code));
            return !c.dropped();
        }
    } catch (...) {
        if (!stopped) {
            c.stopped(UnknownException());
            return !c.dropped();
        }
    }
    return true;
}


template <typename CcClient>
bool runOnceLoadSharingImpl(std::atomic<bool>& stopped
    , pattern::BlockingBuffer* HMBDC_RESTRICT buf
    , CcClient& HMBDC_RESTRICT c, time::Duration maxBlockingTime) {
    pattern::BlockingBuffer::iterator begin, end;
    try {
        if constexpr(std::is_base_of<time::TimerManagerTrait, CcClient>::value) {
            c.checkTimers(time::SysTime::now());
        }

        const bool clientParticipateInMessaging = 
            std::decay<CcClient>::type::INTERESTS_SIZE != 0;
        if (clientParticipateInMessaging && buf) {
            uint8_t msgWrapped[buf->maxItemSize()] __attribute__((aligned (8)));
            void* tmp = msgWrapped;
            bool disp = false;
            if (buf->tryTake(tmp, 0, waitDuration(c, maxBlockingTime))) {
                disp = MessageDispacher<CcClient, typename CcClient::Interests>()(
                    c, *static_cast<MessageHead*>(tmp));
            }
            c.CcClient::invokedCb(disp?1:0);
        } else {
            c.CcClient::invokedCb(0);
            auto d = waitDuration(c, maxBlockingTime);
            if (d) usleep(d.microseconds());
        }
    } catch (std::exception const& e) {
        if (!stopped) {
            c.stopped(e);
            return !c.dropped();
        }
    } catch (int code) {
        if (!stopped) {
            c.stopped(ExitCode(code));
            return !c.dropped();
        }
    } catch (...) {
        if (!stopped) {
            c.stopped(UnknownException());
            return !c.dropped();
        }
    }
    return true;
}

struct Transport {
    Transport(size_t maxItemSize, size_t capacity)
    : buffer(maxItemSize + sizeof(MessageHead), capacity){}
    pattern::BlockingBuffer buffer;
};
    
template <typename Message>
struct TransportEntry {
    std::shared_ptr<Transport> transport;
    std::function<bool (Message const&)> deliverPred;
    std::function<bool (uint16_t, uint8_t const*)> deliverJustBytesPred;
};

template <>
struct TransportEntry<JustBytes> {
    std::shared_ptr<Transport> transport;
    std::function<bool (uint16_t, uint8_t const*)> deliverPred;
    std::function<bool (uint16_t, uint8_t const*)> deliverJustBytesPred;
};

} //blocking_context_detail

/**
 * @class BlockingContext<>
 * @brief A BlockingContext is like a media object that facilitates the communications 
 * for the Clients that it is holding. Each Client is powered by a single OS thread.
 * a Client needs to be started once and only once to a single BlockingContext 
 * before any messages sending happens - typically in the initialization stage in main(),
 * undefined behavior otherwise.
 * @details a Client running in such a BlockingContext utilizing OS's blocking mechanism
 * and takes less CPU time. The Client's responding time scales better when the number of 
 * Clients greatly exceeds the availlable CPUs in the system and the effective message rate
 * for a Client tends to be low.
 * @tparam MessageTuples std tuple capturing the Messages that the Context is supposed to
 * deliver. Messages that not listed here are silently dropped to ensure loose coupling
 * between senders and receivers
 */
template <MessageTupleC... MessageTuples>
struct BlockingContext {
private:
    using cpa = blocking_context_detail::context_property_aggregator<MessageTuples ...>;
    template <typename Message> struct can_handle;
public:
    /**
     * @class Interests
     * @brief a std tuple holding messages types it can dispatch
     * @details will not compile if using the Context to deliver a message not in this tuple
     * 
     */
    using Interests = typename cpa::Interests;

    struct deliverAll {
        template <typename T>
        bool operator()(T&&) {return true;}
        bool operator()(uint16_t, uint8_t const*) {return true;}
    };

    /**
     * @brief trivial ctor
     */
    BlockingContext()
    : stopped_(false) {}

    BlockingContext(BlockingContext const&) = delete;
    BlockingContext& operator = (BlockingContext const&) = delete;

    /**
     * @brief start a client within the Context. The client is powered by a single OS thread.
     * @details it is ok if a Client is blocking, if its own buffer is not full, it
     * doesn't affect other Client's capabilities of receiving Messages
     * 
     * @tparam Client actual Client type
     * @tparam DeliverPred a condition functor deciding if a message should be delivered to a
     * Client, which provides filtering before the Message type filtering. It improves performance
     * in the way it could potentially reduce the unblocking times of thread
     * 
     * Usage example: 
     * //the following Pred verifies srcId matches for Response, and let all other Messages types
     * //deliver
     * @code
     * struct Pred {
     * Pred(uint16_t srcId)
     *     : srcId(srcId){}
     *    
     *     bool operator()(Response const& resp) {
     *         return resp.srcId == srcId;
     *     }
     *     template <typename M>
     *     bool operator()(M const&) {return true;}
     * 
     *     uint16_t srcId;
     * };
     * 
     * @endcode
     * 
     * @param c Client
     * @param capacity the maximum messages this Client can buffer
     * @param maxItemSize the max size of a message - when the Client is interetsed
     * in JustBytes, this value MUST be big enough to hold the max size message to
     * avoid truncation
     * @param cpuAffinity cpu affinity mask for this Client's thread
     * @param maxBlockingTime it is recommended to limit the duration a Client blocks due to 
     * no messages to handle, so it can respond to things like Context is stopped, or generate
     * heartbeats if applicable.
     * @param pred see DeliverPred documentation
     */
	template <typename Client, typename DeliverPred = deliverAll>
    void start(Client& c
        , size_t capacity = 1024
        , size_t maxItemSize = max_size_in_tuple<typename Client::Interests>::value
        , uint64_t cpuAffinity = 0
        , time::Duration maxBlockingTime = time::Duration::seconds(1)
        , DeliverPred&& pred = DeliverPred()) {
        auto t = registerToRun(c, capacity, maxItemSize, std::forward<DeliverPred>(pred));
        auto thrd = kickOffClientThread(blocking_context_detail::runOnceImpl<Client>
            , c, t.use_count() == 1?nullptr:&t->buffer, cpuAffinity, maxBlockingTime);
        threads_.push_back(move(thrd));
        while (dispStartCount_ != threads_.size()) {
            std::this_thread::yield();
        }
    }

    private: using Transport = blocking_context_detail::Transport;
public:
    using ClientRegisterHandle = std::shared_ptr<Transport>;
    /**
     * @brief same as start as to the paramater arguments, except it just register 
     * the Client with the context without actually starting the Client thread.
     * The user is epxected to use runOnce() to manully power the thread
     * 
     * @tparam Client           see start() above
     * @tparam DeliverPred see start() above
     * @param c see start() above
     * @param capacity see start() above
     * @param maxItemSize see start() above
     * @param pred see start() above
     * @return a handle that is to be passed back in the runOnce
     */
    template <typename Client, typename DeliverPred = deliverAll>
    ClientRegisterHandle registerToRun(Client& c
        , size_t capacity = 1024
        , size_t maxItemSize = max_size_in_tuple<typename Client::Interests>::value
        , DeliverPred&& pred = DeliverPred()) {
        if (maxItemSize < max_size_in_tuple<typename Client::Interests>::value)
            HMBDC_THROW(std::out_of_range, "maxItemSize is too small");
    	std::shared_ptr<Transport> t(new Transport(maxItemSize, capacity));
        using RI = typename
            blocking_context_detail::RealInterests<typename Client::Interests>::type;
		setupConduit<MsgConduits, DeliverPred, RI>()(
            msgConduits_, t, std::forward<DeliverPred>(pred));
        return t;
    }

    /**
     * @brief run the Client previous registered to process exsiting Messages
     * 
     * @tparam CcClient 
     * @param t the handle returned from registerToRun
     * @param c the Client see start() above
     * @param maxBlockingTime see start() above
     * @return true when the Client did not terminate itself by throwing an exeption
     * @return false otherwise
     */
    template <typename CcClient>
    bool runOnce(ClientRegisterHandle& t, CcClient& c
        , time::Duration maxBlockingTime = time::Duration::seconds(1)) {
        std::atomic<bool> stopped = false;
        return runOnceImpl(stopped, &t->buffer, c, maxBlockingTime);
    }

    /**
     * @brief start a group of clients within the Context that collectively processing 
     * messages in a load sharing manner. Each client is powered by a single OS thread
     * @details it is ok if a Client is blocking, if its own buffer is not full, it
     * doesn't affect other Client's capabilities of receiving Messages
     * 
     * @tparam LoadSharingClientPtrIt iterator to a Client pointer
     * @tparam DeliverPred a condition functor deciding if a message should be delivered to these
     * Clients, which provides filtering before the Message type filtering. It improves performance
     * in the way it could potentially reduce the unblocking times of threads.
     * An exception is for Client that are interested in JustBytes (basically every message),
     * this filter does not apply - no filtering for these Clients
     * 
     * @param begin an iterator, **begin should produce a Client& for the first Client
     * @param end an end iterator, [begin, end) is the range for Clients
     * @param capacity the maximum messages this Client can buffer
     * @param maxItemSize the max size of a message - when the Client is interetsed
     * in JustBytes, this value MUST be big enough to hold the max size message to
     * avoid truncation
     * @param cpuAffinity cpu affinity mask for this Client's thread
     * @param maxBlockingTime it is recommended to limit the duration a Client blocks due to 
     * no messages to handle, so it can respond to things like Context is stopped, or generate
     * heartbeats if applicable.
     * @param pred see DeliverPred documentation
     */
    template <typename LoadSharingClientPtrIt, typename DeliverPred = deliverAll>
    void start(LoadSharingClientPtrIt begin, LoadSharingClientPtrIt end
        , size_t capacity = 1024
        , size_t maxItemSize = max_size_in_tuple<
            typename std::decay<decltype(**LoadSharingClientPtrIt())>::type::Interests
        >::value
        , uint64_t cpuAffinity = 0
        , time::Duration maxBlockingTime = time::Duration::seconds(1)
        , DeliverPred&& pred = DeliverPred()) { //=[](auto&&){return true;}
        using Client = typename std::decay<
                decltype(**LoadSharingClientPtrIt())
            >::type;
        if (maxItemSize < max_size_in_tuple<typename Client::Interests>::value) 
            HMBDC_THROW(std::out_of_range, "maxItemSize is too small");
        if (begin == end) return;
        std::shared_ptr<Transport> t(new Transport(maxItemSize, capacity));

        using RI = typename
            blocking_context_detail::RealInterests<typename Client::Interests>::type;
        setupConduit<MsgConduits, DeliverPred, RI>()(
            msgConduits_, t, std::forward<DeliverPred>(pred));
        while(begin != end) {
            auto thrd = kickOffClientThread(blocking_context_detail::runOnceLoadSharingImpl<Client>
                , **begin++, t.use_count() == 1?nullptr:&t->buffer, cpuAffinity, maxBlockingTime);
            threads_.push_back(std::move(thrd));
        }
    }

    /**
     * @brief stop the message dispatching - asynchronously
     * @details asynchronously means not garanteed message dispatching 
     * stops immidiately after this non-blocking call
     */
    void stop() {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        stopped_ = true;
        // std::apply(
        //     [](auto&&... args) {
        //         (std::for_each(args.begin(), args.end(), [](auto&& entry) {
        //             entry.transport->buffer.reset();
        //         }), ...);
        //     }
        //     , msgConduits_
        // );
    }

    /**
     * @brief wait until all threads of the Context exit
     * @details blocking call
     */
    void join() {
        for (auto& t : threads_) {
            t.join();
        }
        threads_.clear();
    }

    /**
     * @brief send a message to the BlockingContext to dispatch
     * @details only the Clients that handles the Message will get it
     * This function is threadsafe, which means you can call it anywhere in the code
     * 
     * @tparam Message type
     * @param m message
     */
    template <MessageC Message>
    void send(Message&& m) {
        if constexpr (can_handle<Message>::match) {
            justBytesSend(m);
            using M = typename std::decay<Message>::type;
            auto constexpr i = index_in_tuple<TransportEntries<M>, MsgConduits>::value;
            static_assert(i < std::tuple_size<MsgConduits>::value, "unexpected message type");
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];
                if (e.deliverPred(m)) {
                    if constexpr (blocking_context_detail::has_hmbdc_ctx_queued_ts<M>::value) {
                        auto& tmp = const_cast<M&>(m);
                        tmp.hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
                    }

                    if (i == entries.size() - 1) {
                        e.transport->buffer
                            .template putInPlace<MessageWrap<M>>(std::forward<Message>(m));
                    } else {
                        e.transport->buffer.template putInPlace<MessageWrap<M>>(m);
                    }
                }
            }
        } else {
            justBytesSend(std::forward<Message>(m));
            if (!can_handle<Message>::justbytes) {
                HMBDC_LOG_ONCE(HMBDC_LOG_D("unhandled message sent typeTag="
                    , m.getTypeTag());)
            }
        }
    }
    
    /**
     * @brief send a message by directly constructing the message in receiving message queue
     * @details since the message only exists after being deleivered, the DeliverPred
     * is not called to decide if it should be delivered
     * 
     * @param args ctor args
     * @tparam Message type
     * @tparam typename ... Args args
     */
    template <MessageC Message, typename ... Args>
    void sendInPlace(Args&&... args) {
        static_assert(!Message::justBytes, "use sendJustBytesInPlace");
        if constexpr (can_handle<Message>::match) {
            justBytesSendInPlace<Message>(args...);
            using M = typename std::decay<Message>::type;
            auto constexpr i = index_in_tuple<TransportEntries<M>, MsgConduits>::value;
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];
                if (i == entries.size() - 1) {
                    e.transport->buffer
                        .template putInPlace<MessageWrap<M>>(std::forward<Args>(args)...);
                } else {
                    e.transport->buffer.template putInPlace<MessageWrap<M>>(args...);
                }
            }
        } else {
            justBytesSendInPlace<Message>(std::forward<Args>(args)...);
            if (!can_handle<Message>::justbytes) {
                HMBDC_LOG_ONCE(HMBDC_LOG_D("unhandled message sent typeTag="
                    , std::decay<Message>::type::typeTag);)
            }
        }
    }

    void sendJustBytesInPlace(uint16_t tag, void const* bytes, size_t len, hasMemoryAttachment* att) {
        std::apply(
            [=](auto&&... args) {
                (args.applyJustBytes(tag, bytes, len, att), ...)    ;
            }
            , msgConduits_
        );
    }

    /**
     * @brief best effort to send a message to via the BlockingContext
     * @details this method is not recommended if more than one recipients are accepting
     * this message since there is no garantee each one will receive it once and only once.
     * this call does not block - return false when deliver doesn't reach all
     * intended recipients
     * This method is threadsafe, which means you can call it anywhere in the code
     * 
     * @param m message
     * @param timeout return false if cannot deliver in the specified time
     * @return true if send successfully to every intended receiver
     */
    template <MessageC Message>
    bool trySend(Message&& m, time::Duration timeout = time::Duration::seconds(0)) {
        if constexpr (can_handle<Message>::match) {
            if (!justBytesTrySend(m)) return false;
            using M = typename std::decay<Message>::type;
            auto constexpr i = index_in_tuple<TransportEntries<M>, MsgConduits>::value;
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];
                if (e.deliverPred(m)) {
                    if constexpr (blocking_context_detail::has_hmbdc_ctx_queued_ts<M>::value) {
                        auto& tmp = const_cast<M&>(m);
                        tmp.hmbdc_ctx_queued_ts = hmbdc::time::SysTime::now();
                    }
                    if (i == entries.size() - 1) {
                        if (!e.transport->buffer
                            .tryPut(MessageWrap<M>(std::forward<Message>(m)), timeout)) {
                            return false;
                        }
                    } else {
                        if (!e.transport->buffer.tryPut(MessageWrap<M>(m), timeout)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        } else {
            if (!can_handle<Message>::justbytes) {
                HMBDC_LOG_ONCE(HMBDC_LOG_D("unhandled message sent typeTag="
                    , std::decay<Message>::type::typeTag);)
            }

            return justBytesTrySend(std::forward<Message>(m));
        }
    }
   
    /**
     * @brief best effort to send a message by directly constructing the message in 
     * receiving message queue
     * @details since the message only exists after being deleivered, the DeliverPred
     * is not called to decide if it should be delivered
     * this method is not recommended if more than one recipients are accepting
     * this message since it is hard to ensure each message is delivered once and only once.
     * this call does not block - return false when delivery doesn't reach ALL
     * intended recipients
     * This method is threadsafe, which means you can call it anywhere in the code
     * 
     * @return true if send successfully to every intended receiver
     */
    template <MessageC Message, typename ... Args>
    bool trySendInPlace(Args&&... args) {
        if constexpr (can_handle<Message>::match) {
            if (!justBytesTrySendInPlace<Message>(args...)) return false;
            using M = typename std::decay<Message>::type;
            auto constexpr i = index_in_tuple<TransportEntries<M>, MsgConduits>::value;
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];
                if (i == entries.size() - 1) {
                    if (!e.transport->buffer.template 
                        tryPutInPlace<MessageWrap<M>>(std::forward<Args>(args)...)) {
                        return false;
                    }
                } else {
                    if (!e.transport->buffer.template 
                        tryPutInPlace<MessageWrap<M>>(args...)) {
                        return false;
                    }
                }
            }
            return true;
        } else {
            if (!can_handle<Message>::justbytes) {
                HMBDC_LOG_ONCE(HMBDC_LOG_D("unhandled message sent typeTag="
                    , std::decay<Message>::type::typeTag);)
            }

            return justBytesTrySendInPlace<Message>(args...);
        }
    }

   /**
     * @brief send a range of messages via the BlockingContext 
     * - only checking with deliverPred using the first message
     * @details only the Clients that handles the Message will get it of course
     * This function is threadsafe, which means you can call it anywhere in the code
     * 
     * @param begin a forward iterator point at the start of the range 
     * @param n length of the range
     */
    template <MessageForwardIterC ForwardIt>
    void send(ForwardIt begin, size_t n) {
        if constexpr (can_handle<decltype(*(ForwardIt()))>::match) {
            if (!n) return;
            justBytesSend(begin, n);
            using Message = decltype(*begin);
            using M = typename std::decay<Message>::type;
            auto constexpr i = index_in_tuple<TransportEntries<M>, MsgConduits>::value;
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];
                if (e.deliverPred(*begin)) {
                    while (!e.transport->buffer.template 
                        tryPutBatchInPlace<MessageWrap<M>>(begin, n)) {}
                }
            }
        } else {
            if (!can_handle<decltype(*(ForwardIt()))>::justbytes) {
                HMBDC_LOG_ONCE(HMBDC_LOG_D("unhandled message sent typeTag="
                    , std::decay<decltype(*(ForwardIt()))>::type::typeTag);)
            }
            justBytesSend(begin, n);
        }
    }

private:
    void start(){}
    template <typename Message>
    using TransportEntry = blocking_context_detail::TransportEntry<Message>;

    template <typename Message>
    struct TransportEntries : std::vector<TransportEntry<Message>> {
        // using vector::vector;
        void applyJustBytes(uint16_t tag, void const* bytes
            , size_t len, hasMemoryAttachment* att) {
            if (typeTagMatch<Message>(tag) 
                && std::is_trivially_destructible<Message>::value) {
                auto& entries = *this;
                for (auto i = 0u; i < entries.size(); ++i) {
                    auto& e = entries[i];
                    // bool pred = true;
                    // if constexpr (Message::justBytes) {
                    //     pred = e.deliverPred(tag, (uint8_t*)bytes);
                    // }
                    if (e.deliverJustBytesPred(tag, (uint8_t const*)bytes)) {
                        len = std::min(len
                            , e.transport->buffer.maxItemSize() - sizeof(MessageHead));
                        e.transport->buffer.template 
                            putInPlace<MessageWrap<JustBytes>>(tag, bytes, len, att);
                    }
                }
            }
        }
    };

    template <typename Tuple> struct MCGen;
    template <typename ...Messages>
    struct MCGen<std::tuple<Messages ...>> {
        using type = std::tuple<TransportEntries<Messages> ...>;
    };

    using MsgConduits = typename MCGen<Interests>::type;

    MsgConduits msgConduits_;
    using Threads = std::vector<std::thread>;
    Threads threads_;
    std::atomic<bool> stopped_;

    template <typename MsgConduits, typename DeliverPred, typename Tuple>
    struct setupConduit {
        void operator()(MsgConduits&, std::shared_ptr<Transport>, DeliverPred&&){}
    };

    struct createEntry {
        template <bool careMsg, size_t i, typename M, typename MsgConduits, typename DeliverPred>
        typename std::enable_if<careMsg, void>::type
        doOrNot(MsgConduits& msgConduits, std::shared_ptr<Transport> t, DeliverPred&& pred) {
            auto& entries = std::get<i>(msgConduits);
            if constexpr (std::is_trivially_destructible<M>::value) {
                entries.emplace_back(TransportEntry<M>{t, pred, pred});
            } else {
                entries.emplace_back(TransportEntry<M>{t, pred});
            }
        }

        template <bool careMsg, size_t i, typename M, typename MsgConduits, typename DeliverPred>
        typename std::enable_if<!careMsg, void>::type
        doOrNot(MsgConduits&, std::shared_ptr<Transport>, DeliverPred&&) {
        }
    };

    template <typename MsgConduits, typename DeliverPred, typename M, typename ...Messages>
    struct setupConduit<MsgConduits, DeliverPred, std::tuple<M, Messages...>> {
        void operator()(MsgConduits& msgConduits, std::shared_ptr<Transport> t, DeliverPred&& pred){
            auto constexpr i = index_in_tuple<M, Interests>::value;
            createEntry().template doOrNot<i != std::tuple_size<Interests>::value, i, M>(
                msgConduits, t, pred);
        	setupConduit<MsgConduits, DeliverPred, std::tuple<Messages...>>()
                (msgConduits,t, std::forward<DeliverPred>(pred));
        }
    };

    template <typename Message>
    struct can_handle {
        private:
            using M = typename std::decay<Message>::type;
        public:
            enum {
                index = index_in_tuple<TransportEntries<M>, MsgConduits>::value,
                match = index < std::tuple_size<MsgConduits>::value,
                justbytes_index = index_in_tuple<TransportEntries<JustBytes>, MsgConduits>::value,
                justbytes = justbytes_index < std::tuple_size<MsgConduits>::value
                    && std::is_trivially_destructible<M>::value? 1 :0,
            };
    };

	template <typename Client, typename RunOnceFunc>
    auto kickOffClientThread(RunOnceFunc runOnceFunc, Client& c, pattern::BlockingBuffer* buffer
        , uint64_t mask, time::Duration maxBlockingTime) {
        if (buffer == nullptr) {
            HMBDC_LOG_ONCE(HMBDC_LOG_D("starting a Client that cannot receive messages, Client typid()="
                , typeid(Client).name());)
        }
        std::thread thrd([
            this
            , &c
            , buffer
            , mask
            , maxBlockingTime
            , runOnceFunc]() {
            std::string name;
            char const* schedule;
            int priority;
            
            if (c.hmbdcName()) {
                name = c.hmbdcName();
            } else {
                name = "hmbdc-b";
            }
            try {
                auto cpuAffinityMask = mask; 
                std::tie(schedule, priority) = c.schedSpec();

                if (!schedule) schedule = "SCHED_OTHER";

                os::configureCurrentThread(name.c_str(), cpuAffinityMask
                    , schedule, priority);
                dispStartCount_++;
                static_assert(sizeof(dispStartCount_) == sizeof(size_t));
                c.messageDispatchingStartedCb((size_t const*)&dispStartCount_);
            } catch (std::exception const& e) {
                c.stopped(e);
                return;
            } catch (int code) {
                c.stopped(ExitCode(code));
                return;
            } catch (...) {
                c.stopped(UnknownException());
                return;
            }

            bool stillIn = true;
            while(!stopped_ && (stillIn = runOnceFunc(this->stopped_, buffer, c, maxBlockingTime))) {
            }
            if (stopped_) {
                if (buffer) { /// drain all to release sending party
                    pattern::BlockingBuffer::iterator begin, end;
                    size_t count;
                    do {
                        usleep(10000);
                        count = buffer->peek(begin, end);
                        buffer->wasteAfterPeek(count);
                    } while (count);
                }
                if (stillIn) {
                    c.dropped();
                }
            }
        }
        );

        return thrd;
    }

    template <MessageC Message>
    void justBytesSend(Message&& m) {
        if constexpr (can_handle<Message>::justbytes) {
            using M = typename std::decay<Message>::type;
            auto constexpr i = can_handle<Message>::justbytes_index;
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];
                if (e.transport->buffer.maxItemSize() >= sizeof(MessageWrap<M>)) {
                    if (e.deliverPred(m.getTypeTag(), (uint8_t*)&m)) {
                        e.transport->buffer.template putInPlace<MessageWrap<M>>(m);
                    }
                } else {
                    HMBDC_THROW(std::out_of_range
                        , "need to increase maxItemSize in start() to >= "
                        << sizeof(MessageWrap<M>));
                }
            }
        }
    }

    template <MessageC Message, typename ... Args>
    void justBytesSendInPlace(Args&& ...args) {
        if constexpr (can_handle<Message>::justbytes) {
            using M = typename std::decay<Message>::type;
            static_assert(!std::is_same<JustBytes, M>::value, "use sendJustBytesInPlace"); 
            auto constexpr i = can_handle<Message>::justbytes_index;
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];
                if (e.transport->buffer.maxItemSize() >= sizeof(MessageWrap<M>)) {
                    e.transport->buffer.template putInPlace<MessageWrap<M>>(args...);
                } else {
                    HMBDC_THROW(std::out_of_range
                        , "need to increase maxItemSize in start() to >= "
                        << sizeof(MessageWrap<M>));
                }
            }
        }
    }

    template <MessageC Message>
    bool justBytesTrySend(Message&& m, time::Duration timeout) {
        if constexpr (can_handle<Message>::justbytes) {
            using M = typename std::decay<Message>::type;
            static_assert(!std::is_same<JustBytes, M>::value, "wrong args"); 
            auto constexpr i = can_handle<Message>::justbytes_index;
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];
                if (e.transport->buffer.maxItemSize() >= sizeof(MessageWrap<M>)) {
                    if (e.deliverPred(m.getTypeTag(), (uint8_t*)&m)) {
                        if (!e.transport->buffer.tryPut(MessageWrap<M>(m), timeout)) {
                            return false;
                        }
                    }
                } else {
                    HMBDC_THROW(std::out_of_range
                        , "need to increase maxItemSize in start() to >= "
                        << sizeof(MessageWrap<M>));
                }
            }
        }
        return true;
    }

    template <MessageC Message, typename ... Args>
    bool justBytesTrySendInPlace(Args&&... args) {
        if constexpr (can_handle<Message>::justbytes) {
            using M = typename std::decay<Message>::type;
            auto constexpr i = can_handle<Message>::justbytes_index;
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];
                if (e.transport->buffer.maxItemSize() >= sizeof(MessageWrap<M>)) {
                    if (!e.transport->buffer.template 
                        tryPutInPlace<MessageWrap<M>>(args...)) {
                        return false;
                    }
                } else {
                    HMBDC_THROW(std::out_of_range
                        , "need to increase maxItemSize in start() to >= "
                        << sizeof(MessageWrap<M>));
                }
            }
        }
        return true;
    }

    template <MessageForwardIterC ForwardIt>
    void justBytesSend(ForwardIt begin, size_t n) {
        if constexpr (can_handle<decltype(*(ForwardIt()))>::justbytes) {
            using Message = decltype(*begin);
            using M = typename std::decay<Message>::type;
            auto constexpr i = can_handle<decltype(*(ForwardIt()))>::justbytes_index;
            auto& entries = std::get<i>(msgConduits_);
            for (auto i = 0u; i < entries.size(); ++i) {
                auto& e = entries[i];

                if (e.transport->buffer.maxItemSize() >= sizeof(MessageWrap<M>)) {
                    if (e.deliverPred(begin->getTypeTag(), (uint8_t*)&*begin)) {
                        while (!e.transport->buffer.template tryPutBatchInPlace<MessageWrap<M>>(begin, n)) {}
                    }
                } else {
                    HMBDC_THROW(std::out_of_range
                        , "need to increase maxItemSize in start() to >= "
                        << sizeof(MessageWrap<M>));
                }
            }
        }
    }

    private:
    std::atomic<size_t> dispStartCount_ = 0;
    

};
}}
