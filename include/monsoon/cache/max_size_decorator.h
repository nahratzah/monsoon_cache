#ifndef MONSOON_CACHE_MAX_SIZE_DECORATOR_H
#define MONSOON_CACHE_MAX_SIZE_DECORATOR_H

///\file
///\ingroup cache_detail

#include <monsoon/cache/expire_queue.h>
#include <monsoon/cache/builder.h>

namespace monsoon::cache {


/**
 * \brief Decorator that keeps the expire queue at a bounded size.
 * \ingroup cache_detail
 * \note Requires cache_expire_queue_decorator to be present.
 */
struct max_size_decorator {
  ///\brief Real decorator type.
  ///\ingroup cache_detail
  template<typename CacheImpl>
  class for_impl_type {
   public:
    constexpr for_impl_type(const cache_builder_vars& b)
    : max_size_(b.max_size().value())
    {}

    template<typename StoreType>
    auto on_create([[maybe_unused]] const StoreType& s)
    noexcept
    -> void {
      apply_(static_cast<CacheImpl&>(*this), max_size_);
    }

    template<typename StoreType>
    auto on_hit([[maybe_unused]] const StoreType& s)
    noexcept
    -> void {
      apply_(static_cast<CacheImpl&>(*this), max_size_);
    }

   private:
    template<typename T>
    static auto apply_(expire_queue<T>& q, std::uintptr_t sz)
    noexcept
    -> void {
      q.shrink_to_size(sz);
    }

    std::uintptr_t max_size_;
  };
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_MAX_SIZE_DECORATOR_H */
