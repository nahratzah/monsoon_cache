#ifndef MONSOON_CACHE_ACCESS_EXPIRE_DECORATOR_H
#define MONSOON_CACHE_ACCESS_EXPIRE_DECORATOR_H

///\file
///\ingroup cache_detail

#include <chrono>
#include <memory>
#include <monsoon/cache/expire_queue.h>

namespace monsoon::cache {


/**
 * \brief Cache decorator that handles access expire.
 * \ingroup cache_detail
 */
struct access_expire_decorator {
  using clock_type = std::chrono::steady_clock;
  using time_point = std::chrono::time_point<clock_type>;

  struct access_init {
    time_point expire;
  };

  ///\brief Element decorator counterpart.
  class element_decorator_type {
    friend struct access_expire_decorator;

   public:
    template<typename Alloc, typename... Types>
    element_decorator_type(
        [[maybe_unused]] std::allocator_arg_t aa,
        [[maybe_unused]] Alloc a,
        const std::tuple<Types...>& init)
    : access_expire_(std::get<access_init>(init).expire)
    {}

   private:
    time_point access_expire_;
  };

  ///\brief Implementation of access_expire_decorator for a given cache.
  template<typename ImplType>
  struct for_impl_type {
    using element_decorator_type = access_expire_decorator::element_decorator_type;

    for_impl_type(const cache_builder_vars& b)
    : duration(b.access_expire().value())
    {}

    auto init_tuple() const
    noexcept
    -> std::tuple<access_init> {
      return std::make_tuple(access_init{ clock_type::now() + duration });
    }

    auto on_create(element_decorator_type& elem)
    noexcept
    -> void {
      maintenance_(clock_type::now());
    }

    auto on_hit(element_decorator_type& elem)
    noexcept
    -> void {
      time_point now = clock_type::now();
      elem.access_expire_ = now + duration;

      maintenance_(now);
    }

   private:
    std::chrono::seconds duration;

    ///\brief Maintenance loop for access_expire.
    ///\details Removes items from the expire_queue, which exceed their
    ///access_expire timer.
    auto maintenance_(time_point now)
    noexcept
    -> void {
      expire_queue<ImplType>& q = static_cast<ImplType&>(*this);
      q.shrink_while(
          [&now](const element_decorator_type& s) {
            return s.access_expire_ < now;
          });
    }
  };
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_ACCESS_EXPIRE_DECORATOR_H */
