#include "hmbdc/Copyright.hpp"
#pragma once


#include "hmbdc/Exception.hpp"
#include "hmbdc/Compile.hpp"
#include "hmbdc/Config.hpp"
#include <stdexcept>
#include <utility>
#include <inttypes.h>
#include <limits>


namespace hmbdc { namespace pattern { 

namespace seqarb_detail {
namespace {

template <bool THREADSAFE>
inline  __attribute__ ((always_inline))
void 
my__sync_synchronize() {
    if (THREADSAFE) __sync_synchronize();
}

template <bool THREADSAFE, typename T>
inline  __attribute__ ((always_inline))
bool
my__sync_bool_compare_and_swap(volatile T* var, T compVal, T newVal) {
    if (THREADSAFE) return __sync_bool_compare_and_swap(var, compVal, newVal);
    if (*var == compVal) {
        *var = newVal;
        return true;
    }
    return false;
}

template <bool THREADSAFE, typename T>
inline  __attribute__ ((always_inline))
T
my__sync_val_compare_and_swap(volatile T* var, T compVal, T newVal) {
    if (THREADSAFE) return __sync_val_compare_and_swap(var, compVal, newVal);
    auto res = *var;
    if (*var == compVal) {
        *var = newVal;
    }
    return res;
}
}

template <uint16_t PARTICIPANT_COUNT, typename Seq = uint64_t, bool THREADSAFE = true>
struct SeqArb {
    explicit SeqArb(Seq startSeq = 0)
    : missedInit_(~((1ul << PARTICIPANT_COUNT) - 1u))
    , gapFuncLock_(true)
    , seq_(startSeq) {
        static_assert(PARTICIPANT_COUNT <= 64u, "too many participants");
    }

    SeqArb(SeqArb const&) = delete;
    SeqArb& operator = (SeqArb const&) = delete;

    template <typename HitFunc, typename GapFunc>
    inline  __attribute__ ((always_inline))
    bool operator()(uint16_t participantIndex, Seq seq, HitFunc&& h, GapFunc&& g) HMBDC_RESTRICT {
        j_[participantIndex].seq = seq;

        auto old = my__sync_val_compare_and_swap<THREADSAFE>(&seq_, seq, std::numeric_limits<Seq>::max());

        if (old == seq) {
            h();
            my__sync_synchronize<THREADSAFE>();
            seq_ = seq + 1;
            return true; //arbitrated (utilized or discarded)
        } else if (old == std::numeric_limits<Seq>::max()) {
            return false ;
        } else if (seq < old) {
            return true; //arbitrated (discard)
        } else { //seq > old
            auto low = jLow();
            if (hmbdc_unlikely((low > old && seq_ == old && 
                my__sync_bool_compare_and_swap<THREADSAFE>(&seq_, old, std::numeric_limits<Seq>::max())))) {
                g(low - old);
                my__sync_synchronize<THREADSAFE>();
                seq_ = low;
            }
            return false; //cannot decide and ask me later
        }
    }

    template <typename SeqGen, typename HitFunc, typename GapFunc>
    inline  __attribute__ ((always_inline))
    size_t operator()(uint16_t participantIndex, SeqGen&& seqGen
        , size_t seqSize, HitFunc&& h, GapFunc&& g) HMBDC_RESTRICT {
        auto seq = seqGen();
        j_[participantIndex].seq = seq;

        auto old = my__sync_val_compare_and_swap<THREADSAFE>(
            &seq_, seq, std::numeric_limits<Seq>::max());

        if (old == seq) {
            h();
            auto preSeq = seq;
            auto s = 1ul;
            for (; s < seqSize; ++s) {
                seq = seqGen();
                if (seq - 1 == preSeq) {
                    preSeq = seq;
                    h();
                } else {
                    break;
                }
            }
            j_[participantIndex].seq = seq;
            my__sync_synchronize<THREADSAFE>();
            seq_ = seq + s;
            return s;
        } else if (old == std::numeric_limits<Seq>::max()) {
            return 0;
        } else if (seq < old) {
            return 1ul; //arbitrated (discard)
        } else { //seq > old
            auto low = jLow();
            if (hmbdc_unlikely((low > old && seq_ == old && 
                my__sync_bool_compare_and_swap<THREADSAFE>(
                    &seq_, old, std::numeric_limits<Seq>::max())))) {
                g(low - old);
                my__sync_synchronize<THREADSAFE>();
                seq_ = low;
            }
            return 0; //cannot decide and ask me later
        }
    }

    volatile Seq const& expectingSeq() const {
        return seq_;
    }

    volatile Seq& expectingSeq() {
        return seq_;
    }

private:
    inline  __attribute__ ((always_inline))
    Seq jLow() const HMBDC_RESTRICT {
        auto res = j_[0].seq;
        for (auto i = 1u; i < PARTICIPANT_COUNT; ++i) 
            if (res > j_[i].seq) res = j_[i].seq;
        return res;
    }

    uint64_t const missedInit_;
    bool gapFuncLock_;
    volatile Seq seq_ __attribute__((__aligned__(SMP_CACHE_BYTES)));
    uint64_t missed_ __attribute__((__aligned__(SMP_CACHE_BYTES)));
    struct J {
        J() : seq(0u){}
        Seq seq;
    } __attribute__((__aligned__(SMP_CACHE_BYTES)));
    J j_[PARTICIPANT_COUNT];
};

template <uint16_t PARTICIPANT_COUNT, typename Seq = uint64_t>
struct SingleThreadSeqArb {
    SingleThreadSeqArb(Seq startSeq = 0)
    : arb_(startSeq){}
    
    template <typename GapFunc>
    int operator()(uint16_t participantIndex, Seq seq, GapFunc&& gapFunc) {
        int res = -1;
        if (!arb_(participantIndex, seq
                , [&res]() {
                    res = 1;
                }
                , std::forward<GapFunc>(gapFunc)
            )
        ) {
            res = 0;
        }
        return res;
    }

    volatile Seq& expectingSeq() {
        return arb_.expectingSeq();
    }

    volatile Seq const& expectingSeq() const {
        return arb_.expectingSeq();
    }

private:
    SeqArb<PARTICIPANT_COUNT, Seq, false> arb_;
};

} //seqarb_detail

template <uint16_t PARTICIPANT_COUNT, typename Seq = uint64_t, bool THREADSAFE = true>
using SeqArb = seqarb_detail::SeqArb<PARTICIPANT_COUNT, Seq, THREADSAFE>;

template <uint16_t PARTICIPANT_COUNT, typename Seq = uint64_t>
using SingleThreadSeqArb = seqarb_detail::SingleThreadSeqArb<PARTICIPANT_COUNT, Seq>;

}}
