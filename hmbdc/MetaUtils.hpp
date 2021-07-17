#pragma once 
#include "hmbdc/Compile.hpp"

#include <type_traits>
#include <tuple>

#define HMBDC_CLASS_HAS_DECLARE(member_name)                                    \
    template <typename T>                                                       \
    class has_##member_name                                                     \
    {                                                                           \
        typedef char yes_type;                                                  \
        typedef long no_type;                                                   \
        template <typename U> static yes_type test(decltype(&U::member_name));  \
        template <typename U> static no_type  test(...);                        \
    public:                                                                     \
        enum {value = sizeof(test<T>(0)) == sizeof(yes_type)};                  \
    }

namespace hmbdc {
template <typename T>
struct function_traits: function_traits<decltype(&T::operator())>
{};

template <typename ClassType, typename ReturnType, typename ...Args>
struct function_traits<ReturnType(ClassType::*)(Args...) const> {
    enum { arity = sizeof...(Args) };
    typedef ReturnType result_type;

    template <size_t i>
    struct arg {
        using type = typename std::tuple_element<i, std::tuple<Args...>>::type;
    };
};

template <typename ClassType, typename ReturnType, typename ...Args>
struct function_traits<ReturnType(ClassType::*)(Args...)> {
    enum { arity = sizeof...(Args) };

    typedef ReturnType result_type;

    template <size_t i>
    struct arg {
        using type = typename std::tuple_element<i, std::tuple<Args...>>::type;
    };
};

template <typename T, typename Tuple>
struct index_in_tuple;

template <typename T, typename ...Types>
struct index_in_tuple<T, std::tuple<T, Types...>> {
    static constexpr std::size_t value = 0;
};

template <typename T>
struct index_in_tuple<T, std::tuple<>> {
    static constexpr std::size_t value = 0;
};

template <typename T, typename U, typename ...Types>
struct index_in_tuple<T, std::tuple<U, Types...>> {
    static constexpr std::size_t value = 1 + index_in_tuple<T, std::tuple<Types...>>::value;
};


template <typename T, typename Tuple>
struct add_if_not_in_tuple {
    using type = std::tuple<T>;
};

template <typename T, typename ...Types>
struct add_if_not_in_tuple<T, std::tuple<Types ...>> {
private:
    using Tuple = std::tuple<Types ...>;
public:    
    using type = typename std::conditional<
        index_in_tuple<T, Tuple>::value == sizeof...(Types)
        , std::tuple<Types ..., T>
        , Tuple
        >::type;
};

template <typename Tuple1, typename Tuple2>
struct merge_tuple_unique;

template <typename Tuple1>
struct merge_tuple_unique<Tuple1, std::tuple<>> {
    using type = Tuple1;
};

template <typename Tuple1, typename T, typename ...Types>
struct merge_tuple_unique<Tuple1, std::tuple<T, Types...>> {
private:
    using step1 = typename add_if_not_in_tuple<T, Tuple1>::type;
public:
    using type = typename merge_tuple_unique<step1, std::tuple<Types...>>::type;
};

template <typename Tuple>
struct max_size_in_tuple {
    static constexpr std::size_t value = 0;
};

template <typename T, typename ...Ts>
struct max_size_in_tuple<std::tuple<T, Ts...>> {
private:
    static constexpr std::size_t step1 = max_size_in_tuple<std::tuple<Ts...>>::value;
public:
    static constexpr std::size_t value = step1 > sizeof(T) ? step1 : sizeof(T);
};
template <typename Tuple0, typename Tuple1>
struct concat_tuple;

template <typename ...T, typename ...U>
struct concat_tuple<std::tuple<T...>, std::tuple<U...>> {
    using type = std::tuple<T..., U...>;
};

template <template <class, class> class pred, typename T, typename ...Ts>
struct insert_in_ordered_tuple {
    using type = std::tuple<T>;
};
template <template <class, class> class pred, typename T
    , typename T0, typename ...Ts>
struct insert_in_ordered_tuple<pred, T, std::tuple<T0, Ts...>> {
    using type = typename std::conditional<
        pred<T, T0>::value
        , std::tuple<T, T0, Ts...>
        , typename concat_tuple<
            std::tuple<T0>
            , typename insert_in_ordered_tuple<
                pred, T, std::tuple<Ts...>
            >::type
        >::type
    >::type;
};

template <template <class, class> class pred, typename Tuple>
struct sort_tuple;

template <template <class, class> class pred>
struct sort_tuple<pred, std::tuple<>> {
    using type = std::tuple<>;
};

template <template <class, class> class pred, typename T, typename ...Ts>
struct sort_tuple<pred, std::tuple<T, Ts...>> {
    using type = typename insert_in_ordered_tuple<
        pred
        , T
        , typename sort_tuple<pred, std::tuple<Ts...>>::type
    >::type;
};

template <typename Tuple, size_t from = 0, size_t to = std::tuple_size<Tuple>::value>
struct bsearch_tuple;

template <typename ...Ts, size_t from, size_t to>
struct bsearch_tuple<std::tuple<Ts...>, from, to> {
    template <typename comp>
    bool operator()(comp&& c) {
        if constexpr (from >= to) {
            return false;
        } else {
            constexpr auto at = (from + to) / 2;
            using atType = typename std::tuple_element<at, std::tuple<Ts...>>::type;
            int comp_res = 
                c((atType*)nullptr);
            if (comp_res == 0) 
                return true;
            else if (comp_res < 0) 
                return bsearch_tuple<std::tuple<Ts...>, from, at>()(
                    std::forward<comp>(c));
            else
                return bsearch_tuple<std::tuple<Ts...>, at + 1, to>()(
                    std::forward<comp>(c));
        }
    }
};

template <template <class> class target_template, typename M>
constexpr bool is_template = false;

template <template <class> class target_template, typename N>
constexpr bool is_template<target_template, target_template<N>> = true;

template <template <class> class target_template, typename Tuple>
struct templatized_aggregator {
    using type = std::tuple<>;
};

template <template <class> class target_template, typename T, typename ...Ts>
struct templatized_aggregator<target_template, std::tuple<T, Ts...>> {
    using type = 
        typename templatized_aggregator<target_template, std::tuple<Ts...>>::type;
};

template <template <class> class target_template, typename T, typename ...Ts> 
struct templatized_aggregator<target_template, std::tuple<target_template<T>, Ts...>> {
    using type = typename merge_tuple_unique<std::tuple<target_template<T>>
        , typename templatized_aggregator<target_template, std::tuple<Ts...>>::type
    >::type;
};

template <template <class> class target_template, typename Tuple>
struct templatized_subtractor {
    using type = std::tuple<>;
};

template <template <class> class target_template, typename T, typename ...Ts>
struct templatized_subtractor<target_template, std::tuple<T, Ts...>> {
    using type = typename merge_tuple_unique<std::tuple<T>
        , typename templatized_subtractor<target_template, std::tuple<Ts...>>::type
    >::type;
};

template <template <class> class target_template, typename T, typename ...Ts>
struct templatized_subtractor<target_template, std::tuple<target_template<T>, Ts...>> {
    using type = typename templatized_subtractor<target_template, std::tuple<Ts...>>::type;
};

template<typename T, size_t sz>
size_t length_of(T (&)[sz]) { return sz; }

template <typename Base, typename MTuple>
struct filter_in_tuple_by_base {
    using type = std::tuple<>;
};

template <typename Base, typename M, typename ...Ms>
struct filter_in_tuple_by_base<Base, std::tuple<M, Ms...>> {
    using type = typename std::conditional<std::is_base_of<Base, M>::value
        , typename add_if_not_in_tuple<M, typename filter_in_tuple_by_base<Base, std::tuple<Ms...>>::type>::type
        , typename filter_in_tuple_by_base<Base, std::tuple<Ms...>>::type
    >::type;
};

template <typename Base, typename MTuple>
struct filter_out_tuple_by_base {
    using type = std::tuple<>;
};

template <typename Base, typename M, typename ...Ms>
struct filter_out_tuple_by_base<Base, std::tuple<M, Ms...>> {
    using type = typename std::conditional<!std::is_base_of<Base, M>::value
        , typename add_if_not_in_tuple<M, typename filter_out_tuple_by_base<Base, std::tuple<Ms...>>::type>::type
        , typename filter_out_tuple_by_base<Base, std::tuple<Ms...>>::type
    >::type;
};

template <template <class> class target_pred_template, typename MTuple>
struct filter_in_tuple {
    using type = std::tuple<>;
};

template <template <class> class target_pred_template, typename M, typename ...Ms>
struct filter_in_tuple<target_pred_template, std::tuple<M, Ms...>> {
    using type = typename std::conditional<target_pred_template<M>::value
        , typename add_if_not_in_tuple<M, typename filter_in_tuple<target_pred_template, std::tuple<Ms...>>::type>::type
        , typename filter_in_tuple<target_pred_template, std::tuple<Ms...>>::type
    >::type;
};

template <template <class> class target_pred_template, typename MTuple>
struct filter_out_tuple {
    using type = std::tuple<>;
};

template <template <class> class target_pred_template, typename M, typename ...Ms>
struct filter_out_tuple<target_pred_template, std::tuple<M, Ms...>> {
    using type = typename std::conditional<target_pred_template<M>::value
        , typename filter_out_tuple<target_pred_template, std::tuple<Ms...>>::type
        , typename add_if_not_in_tuple<M, typename filter_out_tuple<target_pred_template, std::tuple<Ms...>>::type>::type
    >::type;
};

template <template <class> class apply, typename Tuple>
struct apply_template_on {
    using type = std::tuple<>;
};

template <template <class> class apply, typename T, typename ...Ts>
struct apply_template_on<apply, std::tuple<T, Ts...>> {
    using type = typename concat_tuple<std::tuple<apply<T>>
        , typename apply_template_on<apply, std::tuple<Ts...>>::type
    >::type;
};

template <typename Tuple>
struct remove_duplicate {
    using type = typename merge_tuple_unique<Tuple, std::tuple<>>::type;
};

template <typename TupleSub, typename TupleSup>
struct is_subset {
    enum {
        value = std::tuple_size<
            typename remove_duplicate<TupleSup>::type>::value
            == std::tuple_size<
                typename merge_tuple_unique<TupleSub, TupleSup>::type>::value 
            ? 1 : 0,    
    };
};
}