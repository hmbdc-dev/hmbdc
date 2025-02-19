#pragma once 
#include "hmbdc/Compile.hpp"

#include <type_traits>
#include <tuple>
#include <string>
#include <string_view>
#include <array>
#include <utility>
#include <optional>

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
constexpr bool is_in_tuple_v = index_in_tuple<T, Tuple>::value < std::tuple_size_v<Tuple>;


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


namespace metautils_detail {
template <typename T, typename... Ts>
struct unique { using type = T; };

template <typename... Ts, typename U, typename... Us>
struct unique<std::tuple<Ts...>, U, Us...>
    : std::conditional_t<(std::is_same_v<U, Ts> || ...)
                       , unique<std::tuple<Ts...>, Us...>
                       , unique<std::tuple<Ts..., U>, Us...>> {};
} // metautils_detail

template <typename Tuple> struct remove_duplicate;
template <typename... Ts>
struct remove_duplicate<std::tuple<Ts...>> {
    using type = typename metautils_detail::unique<std::tuple<>, Ts...>::type;
};

template <typename Tuple1, typename Tuple2>
struct cat_tuple_unique;

template <typename ...T1, typename ...T2>
struct cat_tuple_unique<std::tuple<T1...>, std::tuple<T2...>> {
    using type = typename remove_duplicate<std::tuple<T1..., T2...>>::type;
};

template <typename ...Tuples>
struct merge_tuple_unique;

template <typename Tuple>
struct merge_tuple_unique<Tuple> {
    using type = typename remove_duplicate<Tuple>::type;
};

template <typename Tuple1, typename Tuple2, typename ...Tuples>
struct merge_tuple_unique<Tuple1, Tuple2, Tuples...> {
    using type = typename merge_tuple_unique<
        typename cat_tuple_unique<Tuple1, Tuple2>::type
        , Tuples...
    >::type;
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
constexpr bool is_template_v = false;

template <template <class> class target_template, typename N>
constexpr bool is_template_v<target_template, target_template<N>> = true;

template<template<class...> class T, class U>
class is_derived_from_type_template
{
private:
    template<class ...V>
    static decltype(static_cast<const T<V...>&>(std::declval<U>()), std::true_type{})
    test(const T<V...>&);    
    
    template<class ...V>
    static decltype(static_cast<const T<V...>&>(std::declval<U>()), std::declval<T<V...>>())
    base(const T<V...>&);

    static std::false_type test(...);
    static std::false_type& base(...);
public:
    static constexpr bool value = decltype(is_derived_from_type_template::test(std::declval<U>()))::value;
    using base_type = std::remove_reference_t<decltype(is_derived_from_type_template::base(std::declval<U>()))>;
};
template<template<class...> class T, class U>
constexpr bool is_derived_from_type_template_v = is_derived_from_type_template<T, U>::value;

template<typename N, template<N...> class T, class U>
class is_derived_from_non_type_template
{
private:
    template<N ...v>
    static decltype(static_cast<const T<v...>&>(std::declval<U>()), std::true_type{})
    test(const T<v...>&);

    template<N ...v>
    static decltype(static_cast<const T<v...>&>(std::declval<U>()), std::declval<T<v...>>())
    base(const T<v...>&);

    static std::false_type test(...);
    static std::false_type& base(...);
public:
    static constexpr bool value = decltype(is_derived_from_non_type_template::test(std::declval<U>()))::value;
    using base_type = std::remove_reference_t<decltype(is_derived_from_non_type_template::base(std::declval<U>()))>;
};

template<typename N, template<N...> class T, class U>
constexpr bool is_derived_from_non_type_template_v = is_derived_from_non_type_template<N, T, U>::value;

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

namespace metautils_detail {
template <std::size_t...Idxs>
constexpr auto substring_as_array(std::string_view str, std::index_sequence<Idxs...>)
{
  return std::array{str[Idxs]..., '\n'};
}

template <typename T>
constexpr auto type_name_array()
{
#if defined(__clang__)
  constexpr auto prefix   = std::string_view{"[T = "};
  constexpr auto suffix   = std::string_view{"]"};
  constexpr auto function = std::string_view{__PRETTY_FUNCTION__};
#elif defined(__GNUC__)
  constexpr auto prefix   = std::string_view{"with T = "};
  constexpr auto suffix   = std::string_view{"]"};
  constexpr auto function = std::string_view{__PRETTY_FUNCTION__};
#elif defined(_MSC_VER)
  constexpr auto prefix   = std::string_view{"type_name_array<"};
  constexpr auto suffix   = std::string_view{">(void)"};
  constexpr auto function = std::string_view{__FUNCSIG__};
#else
# error Unsupported compiler
#endif

  constexpr auto start = function.find(prefix) + prefix.size();
  constexpr auto end = function.rfind(suffix);

  static_assert(start < end);

  constexpr auto name = function.substr(start, (end - start));
  return substring_as_array(name, std::make_index_sequence<name.size()>{});
}

template <typename T>
struct type_name_holder {
  static inline constexpr auto value = type_name_array<T>();
};

template <typename F, typename Tuple, std::size_t... I>
void expand_tuple(F func, Tuple&& tuple, std::index_sequence<I...>) {
    if constexpr (std::is_rvalue_reference_v<decltype(tuple)>)
        func(std::move(std::get<I>(tuple))...);
    else
        func(std::get<I>(tuple)...);
}

template <typename F, typename Tuple, std::size_t... I>
auto expand_tuple_ret(F func, Tuple&& tuple, std::index_sequence<I...>) {
    if constexpr (std::is_rvalue_reference_v<decltype(tuple)>)
        return func(std::move(std::get<I>(tuple))...);
    else
        return func(std::get<I>(tuple)...);
}

template <typename T, typename F, typename Tuple, std::size_t... I>
void expand_member_tuple(T* ptr, F func, Tuple&& tuple, std::index_sequence<I...>) {
    if constexpr (std::is_rvalue_reference_v<decltype(tuple)>)
        (ptr->*func)(std::move(std::get<I>(tuple))...);
    else
        (ptr->*func)(std::get<I>(tuple)...);
}

template <typename T, typename F, typename Tuple, std::size_t... I>
auto expand_member_tuple_ret(T* ptr, F func, Tuple&& tuple, std::index_sequence<I...>) {
    if constexpr (std::is_rvalue_reference_v<decltype(tuple)>)
        return (ptr->*func)(std::move(std::get<I>(tuple))...);
    else 
        return (ptr->*func)(std::get<I>(tuple)...);
}

template <typename OptionalTuple, typename Func, std::size_t... I>
void tuple_for_each_impl(Func f, std::index_sequence<I...>) {
    (void)std::initializer_list<int>{(f(*std::get<I>(OptionalTuple{})), 0)...};
}
} // metautils_detail

template <typename T>
constexpr auto type_name() -> std::string_view {
  constexpr auto& value = metautils_detail::type_name_holder<T>::value;
  return std::string_view{value.data(), value.size() - 1};
}

template <typename F, typename Tuple>
void call_in_arg_pack(F func, Tuple&& pack_args_tuple) {
    using RT = std::decay_t<Tuple>;
    metautils_detail::expand_tuple(func, std::forward<Tuple>(pack_args_tuple)
        , std::make_index_sequence<std::tuple_size_v<RT>>{});
}

template <typename F, typename Tuple>
auto call_in_arg_pack_ret(F func, Tuple&& pack_args_tuple) {
    using RT = std::decay_t<Tuple>;
    return metautils_detail::expand_tuple_ret(func, std::forward<Tuple>(pack_args_tuple)
        , std::make_index_sequence<std::tuple_size_v<RT>>{});
}

template <typename T, typename F, typename Tuple>
void call_member_in_arg_pack(T* ptr, F func, Tuple&& pack_args_tuple) {
    using RT = std::decay_t<Tuple>;
    metautils_detail::expand_member_tuple(ptr, func, std::forward<Tuple>(pack_args_tuple)
        , std::make_index_sequence<std::tuple_size_v<RT>>{});
}

template <typename T, typename F, typename Tuple>
auto call_member_in_arg_pack_ret(T* ptr, F func, Tuple&& pack_args_tuple) {
    using RT = std::decay_t<Tuple>;
    return metautils_detail::expand_member_tuple_ret(ptr, func, std::forward<Tuple>(pack_args_tuple)
        , std::make_index_sequence<std::tuple_size_v<RT>>{});
}

template <typename Tuple, typename Func>
void tuple_for_each(Func f) {
    metautils_detail::tuple_for_each_impl<
        typename apply_template_on<std::optional, Tuple>::type
    >(f, std::make_index_sequence<std::tuple_size_v<Tuple>>());
}
}
