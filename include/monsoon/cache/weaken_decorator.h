#ifndef MONSOON_CACHE_WEAKEN_DECORATOR_H
#define MONSOON_CACHE_WEAKEN_DECORATOR_H

///\file
///\ingroup cache_detail

#include <monsoon/cache/element.h>

namespace monsoon::cache {


/**
 * \brief Decorator, that ensures newly created elements are weakened.
 * \ingroup cache_detail
 * Weakened elements use a weak pointer to their data, which will enable
 * expiring as soon as no data structures outside the cache are referencing
 * the mapped type.
 */
struct weaken_decorator {
  template<typename Builder>
  constexpr weaken_decorator([[maybe_unused]] const Builder& b) noexcept
  {}

  template<typename T, typename... D>
  auto on_create(element<T, D...>& elem) const noexcept
  -> void {
    elem.weaken();
  }
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_WEAKEN_DECORATOR_H */
