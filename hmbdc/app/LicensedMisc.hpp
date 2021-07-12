#include "hmbdc/Copyright.hpp"
#pragma once 

#include "hmbdc/app/Base.hpp"
#include "hmbdc/pattern/LockFreeBufferMisc.hpp"
#include "hmbdc/pattern/BlockingBuffer.hpp"

namespace hmbdc { namespace app {
namespace licensed_misc_detail {
template <typename Iterator>
struct reverse_iterator;

template <>
struct reverse_iterator<pattern::lf_misc::iterator<uint64_t>> {
    using Sequence = uint64_t;
    pattern::lf_misc::chunk_base_ptr<Sequence> const * HMBDC_RESTRICT start_;
    Sequence seq_;
    using iterator = pattern::lf_misc::iterator<Sequence>;

    reverse_iterator(iterator it)
        : start_(it.start_), seq_(it.seq_ - 1){}
    reverse_iterator() : start_(nullptr), seq_(0){}
    Sequence seq() const {return seq_;}
    void clear() {start_ = nullptr;}

    reverse_iterator& operator ++() HMBDC_RESTRICT {
        --seq_;
        return *this;
    }

    reverse_iterator operator ++(int) HMBDC_RESTRICT {
        reverse_iterator tmp = *this;
        ++*this;
        return tmp;
    }

    reverse_iterator operator + (size_t dis) const HMBDC_RESTRICT {
        reverse_iterator tmp = *this;
        tmp.seq_ -= dis;
        return tmp;
    }

    reverse_iterator& operator += (size_t dis) HMBDC_RESTRICT {
        seq_ -= dis;
        return *this;
    }

    size_t operator - (reverse_iterator const& other) const HMBDC_RESTRICT {
        return other.seq_ - seq_;
    }

    explicit operator bool() const HMBDC_RESTRICT {
        return start_;
    }

    bool operator < (reverse_iterator const& other) const HMBDC_RESTRICT {
        return seq_ > other.seq_;
    }

    bool operator == (reverse_iterator const& other) const HMBDC_RESTRICT {
        return seq_ == other.seq_;
    }

    bool operator != (reverse_iterator const& other) const HMBDC_RESTRICT {
        return seq_ != other.seq_;}

    void* operator*() const HMBDC_RESTRICT {return *start_ + (seq_ & start_->MASK);
    }

    template <typename T> T& get() const HMBDC_RESTRICT { return *static_cast<T*>(**this); }
    template <typename T>
    T* operator->() HMBDC_RESTRICT {return static_cast<T*>(*start_ + (seq_ & start_->MASK));}
};


template <>
struct reverse_iterator<pattern::BlockingBuffer::iterator> {
    using iterator = pattern::BlockingBuffer::iterator;
    using Sequence = iterator::Sequence;
    reverse_iterator(iterator it)
        : buf_(it.buf_), seq_(it.seq_ - 1){}
    reverse_iterator() : buf_(nullptr), seq_(0){}

    reverse_iterator& operator ++() HMBDC_RESTRICT {
        --seq_;
        return *this;
    }

    reverse_iterator operator ++(int) HMBDC_RESTRICT {
        reverse_iterator tmp = *this;
        ++*this;
        return tmp;
    }

    reverse_iterator operator + (size_t dis) const HMBDC_RESTRICT {
        reverse_iterator tmp = *this;
        tmp.seq_ -= dis;
        return tmp;
    }

    reverse_iterator& operator += (size_t dis) HMBDC_RESTRICT {
        seq_ -= dis;
        return *this;
    }

    size_t operator - (reverse_iterator const& other) const HMBDC_RESTRICT {
        return other.seq_ - seq_;
    }

    explicit operator bool() const HMBDC_RESTRICT {
        return buf_;
    }

    bool operator < (reverse_iterator const& other) const HMBDC_RESTRICT {
        return seq_ > other.seq_;
    }

    bool operator == (reverse_iterator const& other) const HMBDC_RESTRICT {return seq_ == other.seq_;}
    bool operator != (reverse_iterator const& other) const HMBDC_RESTRICT {return seq_ != other.seq_;}
    void* operator*() const HMBDC_RESTRICT {return buf_->getItem(seq_);}
    template <typename T> T& get() const HMBDC_RESTRICT {return *static_cast<T*>(**this);}
    template <typename T>
    T* operator->() HMBDC_RESTRICT {return static_cast<T*>(buf_->getItem(seq_));}
private:
    pattern::BlockingBuffer* buf_;
    Sequence seq_;
};
} //licensed_misc_detail


/**
 * @class RClient<>
 * @brief The only difference from an app::Client is while Client receives messages strictly
 * in order, a RClient receives the most recent
 * message first and oldest message last - within a batch of messages
 * 
 * @tparam CcClient the concrete Client type
 * @tparam typename ... Messages message types that the Client interested
 * 
 */    
template <typename CcClient, MessageC ... Messages>
struct RClient
: Client<CcClient, Messages ...> {
    template <typename Iterator>
    size_t handleRangeImpl(Iterator it, Iterator end, uint16_t threadId) {
        CcClient& c = static_cast<CcClient&>(*this);
        using rit = licensed_misc_detail::reverse_iterator<Iterator>;
        auto rIt = rit(end);
        auto rEnd = rit(it);
        size_t res = 0;
        for (;hmbdc_likely(!this->batchDone_ && rIt != rEnd); ++rIt) {
            if (MessageDispacher<CcClient, typename Client<CcClient, Messages ...>::Interests>()(
                c, *static_cast<MessageHead*>(*rIt))) res++;
        }
        this->batchDone_ = false;
        return res;
    }
};
}
}



