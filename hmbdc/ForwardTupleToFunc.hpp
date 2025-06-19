#include "hmbdc/Copyright.hpp"
#pragma once

#include <tuple>

namespace hmbdc {

namespace forwardtupletofunc_detail {
template<unsigned...> struct index_tuple{};
template<unsigned I, typename IndexTuple, typename... Types>
struct make_indices_impl;

template<unsigned I, unsigned... Indexes, typename T, typename... Types>
struct make_indices_impl<I, index_tuple<Indexes...>, T, Types...> {
  typedef typename 
    make_indices_impl<I + 1, 
                      index_tuple<Indexes..., I>, 
                      Types...>::type type;
};

template<unsigned I, unsigned... Indexes>
struct make_indices_impl<I, index_tuple<Indexes...> > {
  typedef index_tuple<Indexes...> type;
};

template<typename... Types>
struct make_indices 
  : make_indices_impl<0, index_tuple<>, Types...>
{};

template <unsigned... Indexes, class... Args, class Ret>
Ret forward_impl(index_tuple<Indexes...>,
                 std::tuple<Args...> tuple,
                 Ret (*func) (Args...)) {
  return func(std::forward<Args>(std::get<Indexes>(tuple))...);
}

} //forwardtupletofunc_detail

/**
 * @brief perfect forward a tuple into a function
 * @details the tuple value types need to match the func signature
 * 
 * @param tuple tuple containing args for the function
 * @param func func accepting the args to execute
 * @tparam Args arg types
 * @return return of the executiong
 */
template<class... Args, class Ret>
Ret forward_tuple_to_func(std::tuple<Args...>& tuple, Ret (*func) (Args...)) {
   typedef typename forwardtupletofunc_detail::make_indices<Args...>::type Indexes;
   return forwardtupletofunc_detail::forward_impl(Indexes(), tuple, func);
}


/**
 * @brief perfect forward a tuple into a function
 * @details the tuple value types need to match the func signature
 * 
 * @param tuple r reference tuple containing args for the function
 * @param func func accepting the args to execute
 * @tparam Args arg types
 * @return return of the executiong
 */
template<class... Args, class Ret>
Ret forward_tuple_to_func(std::tuple<Args...>&& tuple, Ret (*func) (Args...)) {
   typedef typename forwardtupletofunc_detail::make_indices<Args...>::type Indexes;
   return forwardtupletofunc_detail::forward_impl(Indexes(), std::move(tuple), func);
}

}
