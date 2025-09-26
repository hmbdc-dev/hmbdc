#include "hmbdc/Copyright.hpp"
#pragma once

#include "hmbdc/pattern/MemRingBuffer.hpp"

namespace hmbdc { namespace pattern {

namespace ringbuffer_detail {
    template <typename T, typename Seq>
    uint32_t itemSizePower2Num() {
        uint32_t res = 1ul;
        while ((1ul << res) < sizeof(T) + sizeof(Seq)) res++;
        return res;
    }
}

template<typename T, uint32_t parallel_consumer_count>
class RingBuffer {
    using IMPL = MemRingBuffer<parallel_consumer_count>;
    IMPL impl_;
public:
    enum {PARALLEL_CONSUMER_COUNT = parallel_consumer_count};
    const size_t CAPACITY;
    using value_type = T;
    // struct DeadConsumer : IMPL::DeadConsumer {using typename IMPL::DeadConsumer::DeadConsumer;};
    using DeadConsumer = typename IMPL::DeadConsumer;
///
/// a forward iterator to access elements
///
    struct iterator {
        friend class RingBuffer<T, parallel_consumer_count>;
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

    private:
        using IMPL_IT = typename IMPL::iterator;
        IMPL_IT impl_;

        explicit iterator(IMPL_IT const& impl)
        : impl_(impl){}

    public:
        iterator(){}
        typename IMPL::Sequence seq() const {return impl_.seq();}

        iterator& operator ++() {
            ++impl_;
            return *this;
        }

        iterator operator ++(int) {
            iterator tmp = *this;
            ++*this;
            return tmp;
        }

        iterator operator + (size_t dis) const {
            IMPL_IT tmp = impl_;
            tmp = tmp + dis;
            return iterator(tmp);
        }

        size_t operator - (iterator const& other) const {
            return impl_ - other.impl_;
        }

        explicit operator bool() const {
            return (bool)impl_;
        }

        bool operator == (iterator const& other) const {return impl_ == other.impl_;}
        bool operator != (iterator const& other) const {return impl_ != other.impl_;}
        // value_type const& operator*() const{return *reinterpret_cast<value_type const*>(*impl_);}
        value_type& operator*(){return *reinterpret_cast<value_type *>(*impl_);}
        value_type* operator->(){return reinterpret_cast<value_type *>(*impl_);}
        // value_type const* operator->() const{return reinterpret_cast<value_type const*>(*impl_);}
    };
    RingBuffer(uint32_t ringSizePower2Num)
    : impl_(ringbuffer_detail::itemSizePower2Num<T, typename IMPL::Sequence>(), ringSizePower2Num)
    , CAPACITY(impl_.CAPACITY){}


    void put(T const & item) {impl_.put(&item, sizeof(T));}
    bool tryPut(T const & item) {return impl_.tryPut(&item, sizeof(T));}
    void killPut(T const & item) {impl_.killPut(&item, sizeof(T));} // not safe to use in g++12 or later due to aggressive optimization
    template <typename ApplyToKilled>
    void screezeIn(T const & item, ApplyToKilled f) {impl_.screezeIn(&item, f, sizeof(T));}

    bool isFull() const {return impl_.isFull();}
    iterator claim(){return iterator(impl_.claim());}
    iterator tryClaim(){return iterator(impl_.tryClaim());}
    iterator tryClaim(size_t n){return iterator(impl_.tryClaim(n));}
    iterator claim(size_t n) {return iterator(impl_.claim(n));}
    iterator killClaim() {return iterator(impl_.killClaim());}
    iterator killClaim(size_t n) {return iterator(impl_.killClaim(n));}
    void commit(iterator const& it){impl_.commit(it.impl_);}
    void commit(iterator from, size_t n) {impl_.commit(from.impl_, n);}
    void markDead(uint32_t parallel_consumer_index) {impl_.markDead(parallel_consumer_index);}
    T take(uint32_t PARALLEL_CONSUMER_INDEX) {
        T res;
        impl_.take(PARALLEL_CONSUMER_INDEX, &res, sizeof(res));
        return res;
    }

    T takeReentrant(uint32_t PARALLEL_CONSUMER_INDEX) {
        T res;
        impl_.takeReentrant(PARALLEL_CONSUMER_INDEX, &res, sizeof(res));
        return res;
    }

    iterator peek(uint32_t PARALLEL_CONSUMER_INDEX) const {
        return iterator(impl_.peek(PARALLEL_CONSUMER_INDEX));
    }

    size_t peek(uint32_t PARALLEL_CONSUMER_INDEX, iterator& begin, iterator& end, size_t maxPeekSize = std::numeric_limits<size_t>::max()) const {
        return impl_.peek(PARALLEL_CONSUMER_INDEX, begin.impl_, end.impl_,maxPeekSize);
    }

    void waste(uint32_t PARALLEL_CONSUMER_INDEX, size_t size) { impl_.waste(PARALLEL_CONSUMER_INDEX, size); }
    void wasteAfterPeek(uint32_t PARALLEL_CONSUMER_INDEX, size_t size) { impl_.wasteAfterPeek(PARALLEL_CONSUMER_INDEX, size); }
    // // template <uint32_t PARALLEL_CONSUMER_INDEX>
    // // uint64_t remainingSize() const { return impl_.remainingSize(PARALLEL_CONSUMER_INDEX); }
    size_t remainingSize(uint32_t index) const { return impl_.remainingSize(index); }
    size_t remainingSize() const { return impl_.remainingSize(); }
    void reset(uint32_t PARALLEL_CONSUMER_INDEX) { impl_.reset(PARALLEL_CONSUMER_INDEX); }
    uint64_t purge() { return impl_.purge(); }
};

template<typename T>
class RingBuffer<T, 0> {
    using IMPL = MemRingBuffer<0>;
    IMPL impl_;
public:
    const size_t CAPACITY;
    using value_type = T;
///
/// a forward iterator to access elements
///
    struct iterator {
        friend class RingBuffer<T, 0>;
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

    private:
        using IMPL_IT = typename IMPL::iterator;
        IMPL_IT impl_;

        explicit iterator(IMPL_IT const& impl)
        : impl_(impl){}

    public:
        iterator(){}
        typename IMPL::Sequence seq() const {return impl_.seq();}

        iterator& operator ++() {
            ++impl_;
            return *this;
        }

        iterator operator ++(int) {
            iterator tmp = *this;
            ++*this;
            return tmp;
        }

        iterator operator + (size_t dis) const {
            IMPL_IT tmp = impl_;
            tmp = tmp + dis;
            return iterator(tmp);
        }

        size_t operator - (iterator const& other) const {
            return impl_ - other.impl_;
        }

        explicit operator bool() const {
            return (bool)impl_;
        }

        bool operator == (iterator const& other) const {return impl_ == other.impl_;}
        bool operator != (iterator const& other) const {return impl_ != other.impl_;}
        // value_type const& operator*() const{return *reinterpret_cast<value_type const*>(*impl_);}
        value_type& operator*(){return *reinterpret_cast<value_type *>(*impl_);}
        value_type* operator->(){return reinterpret_cast<value_type *>(*impl_);}
        // value_type const* operator->() const{return reinterpret_cast<value_type const*>(*impl_);}
    };
    RingBuffer(uint32_t ringSizePower2Num)
    : impl_(ringbuffer_detail::itemSizePower2Num<T, typename IMPL::Sequence>(), ringSizePower2Num)
    , CAPACITY(impl_.CAPACITY){}


    void put(T const & item) {impl_.put(&item, sizeof(T));}
    bool tryPut(T const & item) {return impl_.tryPut(&item, sizeof(T));}

    bool isFull() const {return impl_.isFull();}
    iterator claim(){return iterator(impl_.claim());}
    iterator tryClaim(){return iterator(impl_.tryClaim());}
    iterator tryClaim(size_t n){return iterator(impl_.tryClaim(n));}
    iterator claim(size_t n) {return iterator(impl_.claim(n));}
    void commit(iterator const& it){impl_.commit(it.impl_);}
    void commit(iterator from, size_t n) {impl_.commit(from.impl_, n);}
    T take() {
        T res;
        impl_.take(&res, sizeof(res));
        return res;
    }

    bool tryTake(T& res) {
        return impl_.tryTake(&res, sizeof(res));
    }

    iterator peek() {
        return iterator(impl_.peek());
    }

    size_t peek(iterator& begin, iterator& end) {
        return impl_.peek(begin.impl_, end.impl_);
    }

    size_t peekAll(iterator& begin, iterator& end) {
        return impl_.peekAll(begin.impl_, end.impl_);
    }

    void wasteAfterPeek(iterator begin, size_t n, bool incomplete = false) {
        impl_.wasteAfterPeek(begin.impl_, n, incomplete);
    }

    size_t remainingSize() const { return impl_.remainingSize(); }
    void reset() { impl_.reset(); }
};

}} // end namespace hmbdc::pattern

