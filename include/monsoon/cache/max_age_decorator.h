#ifndef MONSOON_CACHE_MAX_AGE_DECORATOR_H
#define MONSOON_CACHE_MAX_AGE_DECORATOR_H

///\file
///\ingroup cache_detail

#include <chrono>
#include <memory>

namespace monsoon::cache {


/**
 * \brief Decorator that enforces the max_age property.
 * \ingroup cache_detail
 */
struct max_age_decorator {
  using clock_type = std::chrono::steady_clock;
  using time_point = std::chrono::time_point<clock_type>;

  struct init_arg {
    time_point expire;
  };

  max_age_decorator(const cache_builder_vars& b)
  : duration(b.max_age().value())
  {}

  auto init_tuple() const
  noexcept
  -> std::tuple<init_arg> {
    return std::make_tuple(init_arg{ clock_type::now() + duration });
  }

  ///\brief Element decorator counterpart.
  class element_decorator_type {
   public:
    template<typename Alloc, typename... Types>
    element_decorator_type(
        [[maybe_unused]] std::allocator_arg_t aa,
        [[maybe_unused]] Alloc a,
        const std::tuple<Types...>& init)
    : max_age_expire_(std::get<init_arg>(init).expire)
    {}

    bool is_expired() const noexcept {
      return clock_type::now() > max_age_expire_;
    }

   private:
    time_point max_age_expire_;
  };

  std::chrono::seconds duration;
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_MAX_AGE_DECORATOR_H */
