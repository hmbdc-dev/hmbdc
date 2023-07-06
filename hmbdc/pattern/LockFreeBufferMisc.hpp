#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/os/Allocators.hpp"
#include "hmbdc/Compile.hpp"
#include "hmbdc/Config.hpp"
#include "hmbdc/Exception.hpp"

#include <utility>
#include <stdexcept>
#include <cstddef>
#include <functional>
#include <atomic>

#include <stdint.h>
#include <stddef.h>
#include <malloc.h>
#include <stdlib.h>
#include <memory.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

namespace hmbdc { namespace pattern {
namespace lf_misc {

struct DeadConsumer : std::runtime_error {
    DeadConsumer(const std::string& what_arg) 
    : std::runtime_error(what_arg){}
};

template <typename Seq>
struct chunk_base_ptr {
    template<typename Allocator = os::DefaultAllocator>
    chunk_base_ptr(uint32_t valueTypeSizePower2Num, uint32_t ringSizePower2Num
        , Allocator& allocator = os::DefaultAllocator::instance())
    : space_((char*)allocator.memalign(SMP_CACHE_BYTES, (1ul << valueTypeSizePower2Num) * (1ul << ringSizePower2Num))
            - (char*)this)
    , MASK((1ul << ringSizePower2Num) - 1)
    , valueTypeSizePower2Num_(valueTypeSizePower2Num)
    , freer_([this, &allocator](){allocator.free(((char* const)this) + space_);}) {
        if (valueTypeSizePower2Num + ringSizePower2Num > 32) {
            HMBDC_THROW(std::out_of_range, "failed to allocated space valueTypeSizePower2Num=" << valueTypeSizePower2Num 
                << " ringSizePower2Num=" << ringSizePower2Num);
        }
    }

    static size_t footprint(uint32_t valueTypeSizePower2Num, uint32_t ringSizePower2Num) {
        return sizeof(chunk_base_ptr) + SMP_CACHE_BYTES * 2
            + (1u << valueTypeSizePower2Num) * (1u << ringSizePower2Num);
    }

    ~chunk_base_ptr() {
        freer_();
    }

    void* operator + (size_t index) const HMBDC_RESTRICT {
        return (((char* const)this) + space_) + (index << valueTypeSizePower2Num_) + sizeof(Seq);
    }

    std::atomic<Seq>* getSeq(size_t index) const HMBDC_RESTRICT  {
        static_assert(sizeof(std::atomic<Seq>) == sizeof(Seq));
        return reinterpret_cast<std::atomic<Seq>*>((((char* const)this) + space_) + (index << valueTypeSizePower2Num_));
    }

    friend 
    size_t operator - (void const* HMBDC_RESTRICT from, chunk_base_ptr<Seq> const& HMBDC_RESTRICT start) {
        return (static_cast<char const*>(from) - sizeof(Seq) - (((char* const)&start) + start.space_)) 
            >> start.valueTypeSizePower2Num_;
    }
    std::ptrdiff_t const space_;
    Seq const MASK;
    uint32_t const valueTypeSizePower2Num_;
    std::function<void()> freer_;
    chunk_base_ptr(chunk_base_ptr<Seq> const&) = delete;
    chunk_base_ptr& operator = (chunk_base_ptr<Seq> const&) = delete;
    
    // template <typename BinaryOStream>
    // friend:
    // BinaryOStream& 
    // operator << (BinaryOStream& ofs, chunk_base_ptr const& c) {
    //     ofs.write(((char* const)&c) + c.space_, (1ul << valueTypeSizePower2Num) * (1ul << ringSizePower2Num));
    //     return ofs;
    // }

    // template <typename BinaryIStream>
    // BinaryIStream& 
    // operator >> (BinaryIStream& ifs, chunk_base_ptr & c) {
    //     ifs.read(((char* const)&c) + c.space_, (1ul << valueTypeSizePower2Num) * (1ul << ringSizePower2Num));
    //     return ifs;
    // }
};

template <typename Seq>
struct iterator {
    using Sequence = Seq;
    using ChunkBasePtr = chunk_base_ptr<Seq>;
    ChunkBasePtr const * HMBDC_RESTRICT start_;
    Seq seq_;

    //this ctor is only used by LockFreeBuffer itself
    iterator(ChunkBasePtr const& HMBDC_RESTRICT start, Seq seq)
        : start_(&start), seq_(seq){}
    iterator() : start_(nullptr), seq_(0){}
    Seq seq() const {return seq_;}
    void clear() {start_ = nullptr;}

    iterator& operator ++() HMBDC_RESTRICT {
        ++seq_;
        return *this;
    }

    iterator operator ++(int) HMBDC_RESTRICT {
        iterator tmp = *this;
        ++*this;
        return tmp;
    }

    iterator operator + (size_t dis) const HMBDC_RESTRICT {
        iterator tmp = *this;
        tmp.seq_ += dis;
        return tmp;
    }

    iterator& operator += (size_t dis) HMBDC_RESTRICT {
        seq_ += dis;
        return *this;
    }

    size_t operator - (iterator const& other) const HMBDC_RESTRICT {
        return seq_ - other.seq_;
    }

    explicit operator bool() const HMBDC_RESTRICT {
        return start_;
    }

    bool operator < (iterator const& other) const HMBDC_RESTRICT {
        return seq_ < other.seq_;
    }

    bool operator == (iterator const& other) const HMBDC_RESTRICT {return seq_ == other.seq_;}
    bool operator != (iterator const& other) const HMBDC_RESTRICT {return seq_ != other.seq_;}
    void* operator*() const HMBDC_RESTRICT {return *start_ + (seq_ & start_->MASK);}
    template <typename T> T& get() const HMBDC_RESTRICT { return *static_cast<T*>(**this); }
    template <typename T>
    T* operator->() HMBDC_RESTRICT {return static_cast<T*>(*start_ + (seq_ & start_->MASK));}
};

} //lf_misc
}}

#pragma GCC diagnostic pop
