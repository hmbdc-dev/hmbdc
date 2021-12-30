#include "hmbdc/Copyright.hpp"
#pragma once
#include "hmbdc/tips/Messages.hpp"
#include "hmbdc/Exception.hpp"
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace hmbdc { namespace tips {

namespace typetagset_detail {
template <typename T, app::MessageTupleC MessageTuple>
struct adder {
    T& t;
    void operator()(uint16_t mod, uint16_t res) {}
};

template <typename T, app::MessageC Message, app::MessageC ...Messages>
struct adder<T, std::tuple<Message, Messages...>> {
    T& t;
    void operator()(uint16_t mod, uint16_t res) {
        if constexpr (!Message::hasRange) {
            if (Message::typeTag % mod == res) {
                t.add(Message::typeTag);
            }
        } else {
            for (uint16_t i =  0; i < Message::typeTagRange; ++i) {
                auto tag = Message::typeTagStart + i;
                if (tag % mod == res) {
                    t.add(tag);
                }
            }
        }
        adder<T, std::tuple<Messages...>>{t}(mod, res);
    }
};

// template <typename T, app::MessageTupleC MessageTuple>
// struct eraser {
//     T& t;
//     bool operator()() { return true; }
// };

// template <typename T, app::MessageC Message, app::MessageC ...Messages>
// struct eraser<T, std::tuple<Message, Messages...>> {
//     T& t;
//     bool operator()() {
//         if constexpr (!Message::hasRange) {
//             return t.erase(Message::typeTag) != 0xff &&
//                 eraser<T, std::tuple<Messages...>>{t}();
//         } else {
//             return eraser<T, std::tuple<Messages...>>{t}();
//         }
//     }
// };

template <app::MessageTupleC MessageTuple, typename CcNode, typename TypeTagSet>
struct subscribe_for {
    void operator()(CcNode const&, TypeTagSet&, std::function<void(uint16_t)>
        , uint16_t mod, uint16_t res){}
};

template <app::MessageC Message, app::MessageC ...Messages, typename CcNode, typename TypeTagSet >
struct subscribe_for<std::tuple<Message, Messages...>, CcNode, TypeTagSet>{
    void operator()(CcNode const& node, TypeTagSet& tts
        , std::function<void(uint16_t)> afterAddTag, uint16_t mod, uint16_t res) {
        if constexpr (Message::hasRange) {
            node.addTypeTagRangeSubsForCfg((Message*)nullptr
                , [&tts, mod, res, &afterAddTag](uint16_t offsetInRange) {
                    if (offsetInRange >= Message::typeTagRange) {
                        HMBDC_THROW(std::out_of_range, offsetInRange << " as offset is out of the range of " 
                            << Message::typeTagStart << "_" << Message::typeTagRange);
                    }
                    auto tag = Message::typeTagStart + offsetInRange;
                    if (tag % mod == res) {
                        if (tts.set(tag)) {
                            afterAddTag(tag);
                        }
                    }
                }
            );
        } else if constexpr (std::is_same<app::JustBytes, Message>::value) {
            node.addJustBytesSubsForCfg(
                [&tts, mod, res, &afterAddTag](uint16_t tag) {
                    if (tag % mod == res) {
                        if (tts.set(tag)) {
                            afterAddTag(tag);
                        }
                    }
                });
        } else if (Message::typeTag % mod == res) {
            auto tag = Message::typeTag;
            if (tts.set(tag)) {
                afterAddTag(tag);
            }
        }
        using next = subscribe_for<std::tuple<Messages...>, CcNode, TypeTagSet>;
        next{}(node, tts, afterAddTag, mod, res);
    }
};

template <app::MessageTupleC MessageTuple, typename CcNode, typename TypeTagSet>
struct advertise_for {
    void operator()(CcNode const&, TypeTagSet&, uint16_t mod, uint16_t res){}
};

template <app::MessageC Message, app::MessageC ...Messages, typename CcNode, typename TypeTagSet >
struct advertise_for<std::tuple<Message, Messages...>, CcNode, TypeTagSet>{
    void operator()(CcNode const& node, TypeTagSet& tts, uint16_t mod, uint16_t res) {
        if constexpr (Message::hasRange) {
            node.addTypeTagRangePubsForCfg((Message*)nullptr
                , [&tts, mod, res](uint16_t offsetInRange) {
                    if (offsetInRange >= Message::typeTagRange) {
                        HMBDC_THROW(std::out_of_range, offsetInRange << " as offset is out of the range of " 
                            << Message::typeTagStart << "_" << Message::typeTagRange);
                    }
                    if ((Message::typeTagStart + offsetInRange) % mod == res) {
                        tts.set(Message::typeTagStart + offsetInRange);
                    }
                }
            );
        } else if constexpr (std::is_same<app::JustBytes, Message>::value) {
            node.addJustBytesPubsForCfg(
                [&tts, mod, res](uint16_t tag) {
                    if (tag % mod == res) {
                        tts.set(tag);
                    }
                });
        } else if (Message::typeTag % mod == res) {
            tts.set(Message::typeTag);
        }
        using next = advertise_for<std::tuple<Messages...>, CcNode, TypeTagSet>;
        next{}(node, tts, mod, res);
    }
};
} //typetagset_detail

struct TypeTagSet {
    using TagType = uint16_t;
    enum {
        capacity = 1u << (sizeof(TagType) * 8),
    };

    template <app::MessageTupleC MessageTuple, typename CcNode>
    void markSubsFor(CcNode const& node, uint16_t mod, uint16_t res
        , std::function<void(uint16_t)> afterAddTag) {
        typetagset_detail::subscribe_for<MessageTuple, CcNode, TypeTagSet> marker;
        marker(node, *this, afterAddTag, mod, res);
    }

    template <app::MessageTupleC MessageTuple>
    void addAll(uint16_t mod = 1, uint16_t res = 0) {
        typetagset_detail::adder<TypeTagSet, MessageTuple>{*this}(mod, res);
    }

    uint8_t add(TagType tag) {
        return ++subCounts_[tag];
    }

    bool set(TagType tag) {
        if (subCounts_[tag]) return false;
        return (subCounts_[tag] = 1);
    }

    bool unset(TagType tag) {
        if (!subCounts_[tag]) return false;
        subCounts_[tag] = 0;
        return true;
    }

    // template <app::MessageTupleC MessageTuple>
    // bool erase() {
    //     return typetagset_detail::eraser<TypeTagSet, MessageTuple>{*this}();
    // }

    uint8_t sub(TagType tag) {
        return --subCounts_[tag];
    }

    uint8_t check(TagType tag) const {
        return subCounts_[tag];
    }

    template <typename TagRecver>
    void exportTo(TagRecver&& r) const {
        for (uint32_t i = 0; i < capacity; i++) {
            auto c = (uint8_t)subCounts_[i];
            if (c) {
                r((uint16_t)i, c);            
            }
        }
    }

    private:
    std::atomic<TagType> subCounts_[capacity] = {0};
};

struct TypeTagSetST {
    using TagType = uint16_t;
    enum {
        capacity = 1u << (sizeof(TagType) * 8),
    };

    template <app::MessageTupleC MessageTuple, typename CcNode>
    void markSubsFor(CcNode const& node, uint16_t mod, uint16_t res) {
        typetagset_detail::subscribe_for<MessageTuple, CcNode, TypeTagSetST> marker;
        marker(node, *this, mod, res);
    }

    template <app::MessageTupleC MessageTuple, typename CcNode>
    void markPubsFor(CcNode const& node, uint16_t mod, uint16_t res) {
        typetagset_detail::advertise_for<MessageTuple, CcNode, TypeTagSetST> marker;
        marker(node, *this, mod, res);
    }

    template <app::MessageTupleC MessageTuple>
    void addAll(uint16_t mod = 1, uint16_t res = 0) {
        typetagset_detail::adder<TypeTagSetST, MessageTuple>{*this}(mod, res);
    }

    uint8_t add(TagType tag) {
        auto res = ++subCounts_[tag];
        if (res == 0xffu) {
            HMBDC_THROW(std::out_of_range, "too many subscriptions - abort");
        }
        return res;
    }

    bool set(TagType tag) {
        auto it = subCounts_.find(tag);
        if (it != subCounts_.end() && it->second) return false;
        return (subCounts_[tag] = 1);
    }

    bool unset(TagType tag) {
        return subCounts_.erase(tag) == 1;
    }

    // template <app::MessageTupleC MessageTuple>
    // bool erase() {
    //     return typetagset_detail::eraser<TypeTagSetST, MessageTuple>{*this}();
    // }

    uint8_t sub(TagType tag) {
        return --subCounts_[tag];
    }

    uint8_t check(TagType tag) const {
        auto it = subCounts_.find(tag);
        if (it == subCounts_.end()) return false;
        return it->second;
    }

    template <typename TagRecver>
    void exportTo(TagRecver&& r) const {
        for (auto const& t : subCounts_) {
            if (t.second) {
                r(t.first, t.second);
            }
        }
    }

    std::mutex lock;
    private:
    std::unordered_map<TagType, uint8_t> subCounts_;
};
}}
