#include "hmbdc/Copyright.hpp"
#pragma once
 
#include "hmbdc/app/MessageDispacher.hpp"
#include "hmbdc/MetaUtils.hpp"
#include "hmbdc/Compile.hpp"

#include <iostream>
#include <stdexcept>
#include <tuple>
#include <memory>
#include <type_traits>

namespace hmbdc { namespace app {

namespace client_detail {
template <size_t MAX_MEMORY_ATTACHMENT>
struct InBandMemoryAttachmentProcessor {
    InBandMemoryAttachmentProcessor()
    : att(new(underlyingMessage) hasMemoryAttachment){}
    uint8_t underlyingMessage[MAX_MEMORY_ATTACHMENT];
    template <MessageC Message>
    bool cache(InBandHasMemoryAttachment<Message> const& ibma, uint16_t typeTagIn) {
        static_assert(sizeof(Message) <= MAX_MEMORY_ATTACHMENT, "");
        if (hmbdc_unlikely(att->attachment)) {
            HMBDC_THROW(std::logic_error, "previous InBandMemoryAttachment not concluded");
        }
        typeTag = typeTagIn;

        if constexpr(Message::justBytes) {
            memcpy(underlyingMessage, &ibma.underlyingMessage, sizeof(underlyingMessage));
        } else {
            memcpy(underlyingMessage, &ibma.underlyingMessage, sizeof(Message));
        }

        
        if (hmbdc_unlikely(!att->len)) {
            att->attachment = nullptr;
            att->afterConsumedCleanupFunc = nullptr;
            accSize = 0;
            return true;
        } else {
            att->attachment = malloc(att->len);
            att->afterConsumedCleanupFunc = hasMemoryAttachment::free;
            accSize = 0;
            return false;
        }
    }

    bool accumulate(InBandMemorySeg const& ibms, size_t n) {
        if (accSize < att->len) {
            auto attBytes = ibms.seg;
            auto copySize = std::min(n, att->len - accSize);
            memcpy((char*)att->attachment + accSize, attBytes, copySize);
            accSize += copySize;
            return accSize == att->len;
        }
        return true;
    }
    hasMemoryAttachment * const att;
    uint16_t typeTag = 0;
    size_t accSize = 0;
    std::function<void* (boost::interprocess::managed_shared_memory::handle_t)> 
        hmbdcShmHandleToAddr;
    std::function<void (uint8_t*)> hmbdcShmDeallocator;   
};
}

/**
 * @class single_thread_powered_client
 * @brief a trait class, if a Client can only run on a single specific thread in Pool, 
 * derive the Client from it, hmbdc will check to ensure that is the case
 */
struct single_thread_powered_client{};

/**
 * @class Client<>
 * @brief A Client represents a thread of execution/a task. The execution is managed by 
 * a Context.
 * a Client object could participate in message dispatching as the receiver of specifed 
 * message types
 * @details message is dispatched thru callbacks in the loose 
 * form of void handleMessageCb(Message const& m) or void handleMessageCb(Message& m).
 * some callbacks have default implementations.
 * however, all callbacks are overridable to provide desired effects;
 * When running in a Context, all callbacks (*Cb methods) for a hmbdc Client instance 
 * are garanteed to be called from a single OS thread, ie. no 2 callabacks of an Client 
 * instance are called concurrently, so the Client programmer can assume a 
 * single thread programming model callback-wise. 
 * The above also holds true for timer firing callbacks when a concrete Client deriving 
 * from TimerManager that manages the timers
 * 
 * See in example hmbdc.cpp @snippet hmbdc.cpp write a Client
 * 
 * @tparam CcClient the concrete Client type
 * @tparam typename ... Messages message types that the Client interested
 * if the type of JustBytes is declared, use a special callback see below
 * 
 * @code
 * void handleJustBytesCb(uint16_t tag, uint8_t* bytes, hasMemoryAttachment* att){...}
 * @endcode
 * is expected
 * See in example hmbdc.cpp @snippet hmbdc.cpp use JustBytes instead of Message types
 */
template <typename CcClient, MessageC ... Messages>
class Client {
public:    
    using Interests = std::tuple<Messages ...>;

    enum {
        INTERESTS_SIZE = std::tuple_size<Interests>::value,
        MAX_MEMORY_ATTACHMENT = max_size_in_tuple<
            typename hmbdc::filter_in_tuple_by_base<hasMemoryAttachment, Interests>::type
        >::value,
    };

    /**
     * @brief return the name of thread that runs this client, override if necessary
     * @details this only used when the Client is running in direct mode
     * @return thread name - default to be hmbdc0, hmbdc1 ...
     */
    char const* hmbdcName() const { return "hmbdc-anonymous"; }

    /**
     * @brief an overrideable method.
     * returns the schedule policy and priority, override if necessary
     * priority is only used when policy is "SCHED_RR", or "SCHED_FIFO"
     * @details this is only used when the Client is running in direct mode
     * supported policy are "SCHED_OTHER"(=nullptr), "SCHED_RR", "SCHED_FIFO"
     * @return a tuple made of schedule policy and priority, default to be SCHED_OTHER
     */
    std::tuple<char const*, int> schedSpec() const {
#ifndef _QNX_SOURCE        
        return std::make_tuple<char const*, int>(nullptr, 0);
#else
        return std::make_tuple<char const*, int>(nullptr, 20);
#endif        
    }

   /**
    * @brief an overridable method.
    * client  receives events in batches and the max batch size is controllable when running
    * in direct mode Context. Here is to specify the max size.
    * @details a message could only be reaching one client when using partition Context. In this
    * case, reduce the size (reduce the greediness) might be useful
    * @return the max number of messages when grabbing messages to process
    */
    size_t maxBatchMessageCount() const {return std::numeric_limits<size_t>::max();}


    /**
     * @brief called before any messages got dispatched - only once
     * @details this is the place some preparation code goes to
     * 
     * @param pClientDispatchingStarted pointer to the Client count that has started 
     * displatching within its Context; its value could 
     * change if you repeatedly check - might be used to sync with other Clients
     * within the same Context (particularly IPC Context) at starting up stage of the system.
     * When doing the sync, just be aware the Client might be running in a pool - do
     * not hog the pool thread if possible.
     * Note: Since ipc_creator Context has an implicitly purger Client, this value would be 
     * 1 greater than the user Clients count
     */
    virtual void messageDispatchingStartedCb(std::atomic<size_t> const* pClientDispatchingStarted) {
        //default do nothing
    };

    /**
     * @brief callback called when this Client is taken out of message dispatching
     * @details after this call the Client is still at hook from the Context point of view
     * (until droppedCb is called), so don't delete this Client yet or add it back to 
     * the Context. any exception thrown here is ignored, 
     * 
     * @param e the exception that caused the Client to be taken out of message dispatching
     * e could be thrown by the Client itself in a callback function 
     * to voluntarily take itself out
     */
    virtual void stoppedCb(std::exception const& e) {
        // called when this client is stopped, e.what() might have reason
        std::cerr << "Client " << static_cast<CcClient*>(this)->hmbdcName() 
            << " exited with reason: " << e.what() << std::endl;
    };

    /**
     * @brief callback called after the Client is safely taken out of the Context
     * @details exception thrown here is ignored and return true is assumed
     * @return if false, this Client is added back to the Context to process messages
     * otherwise, no more callback. You could even safely "delete this; return true;"
     */
    virtual bool droppedCb() {
        // called when this client is dropped and free to be deleted
        return true;
    };
    
    /**
     * @brief this callback is called all the time (frequently) - the exact timing is
     * after a batch of messages are dispatched. After this call returns, the previously 
     * dispatched message's addresses are no longer valid, which means if you cache the 
     * event addresses in the previous handleMessageCb()s, you cannot use those after 
     * the return of the next invokeCb function.
     * @details you can collectively process the messages received/cached so far here, or 
     * do something needs to be done all the time like powering another message loop
     * 
     * @param dispatched the number of messages dispatched since last invokedCb called
     */
    virtual void invokedCb(size_t dispatched) {
        // called as frequently as workload allowed
    }
    
    /**
     * @brief trivial
     */
    Client() = default;
    Client(Client const&) = delete;
    Client& operator = (Client const&) = delete;
    virtual ~Client() = default;

protected:
    /**
     * @brief the derived user's Client has the option to stop the current batch of event 
     * dispatching.
     * @details for example, if the user's Client decides it has handled all interested 
     * messages and wants to skip the remaining messages within the CURRENT batch, call
     * this within any callback functions.
     * use with caution, only when it is absolutely certain that there is NO messages of 
     * ANY type the Client need to care are within the current batch.
     */
    void batchDone() {
        batchDone_ = true;
    }

public:
    /**
     * @brief the following are for internal use, don't change or override
     */
    void stopped(std::exception const&e) noexcept {
        try {
            stoppedCb(e);
        } catch (...) {}
    }

    bool dropped() noexcept {
        bool res = true;
        try {
            res = droppedCb();
        } catch (...) {}
        return res;
    }

    template <typename Iterator>
    size_t handleRangeImpl(Iterator it, 
        Iterator end, uint16_t threadId) {
        CcClient& c = static_cast<CcClient&>(*this);
        size_t res = 0;
        for (;hmbdc_likely(!batchDone_ && it != end); ++it) {
            if (MessageDispacher<CcClient, Interests>()(
                c, *static_cast<MessageHead*>(*it))) res++;
        }
        batchDone_ = false;
        return res;
    }

    bool handleImpl(MessageHead& msgHead, uint16_t threadId) {
        CcClient& c = static_cast<CcClient&>(*this);
        return MessageDispacher<CcClient, Interests>()(c, msgHead);
    }


    typename std::conditional<MAX_MEMORY_ATTACHMENT != 0
        , client_detail::InBandMemoryAttachmentProcessor<MAX_MEMORY_ATTACHMENT>
        , std::nullptr_t>::type ibmaProc;

    uint16_t hmbdcNumber = 0xffff;

protected:
    bool batchDone_ = false;  ///not part of API, do not touch
};

/**
 * if you do not have Message paramter pack, just have tuple
 */
template <typename CcClient, typename MessageTuple>
struct client_using_tuple;
template <typename CcClient, MessageC ... Messages>
struct client_using_tuple<CcClient, std::tuple<Messages...>> {
    using type = Client<CcClient, Messages...>;
};
}} //hmbdc::app
