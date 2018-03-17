#ifndef MONSOON_CACHE_CREATE_HANDLER_H
#define MONSOON_CACHE_CREATE_HANDLER_H

///\file
///\ingroup cache_detail

#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace monsoon::cache {
namespace {
///\brief Various functions and data types, to adapt the result of a create function.
///\ingroup cache_detail
namespace create_ops {


template<typename T>
auto squash_future_(T v)
-> T {
  return v;
}

template<typename T>
auto squash_future_(std::shared_future<T> fut)
-> decltype(std::declval<std::shared_future<T>>().get()) {
  return std::move(fut).get();
}

template<typename T>
auto squash_future_(std::future<T> fut)
-> decltype(std::declval<std::future<T>>().get()) {
  return std::move(fut).get();
}

template<typename Alloc, typename T>
auto make_shared_ptr_(Alloc alloc, T v)
-> std::shared_ptr<T> {
  return std::allocate_shared<T>(alloc, std::move(v));
}

template<typename Alloc, typename T>
auto make_shared_ptr_(Alloc alloc, std::shared_ptr<T> v)
-> std::shared_ptr<T> {
  return std::move(v);
}

template<typename T>
struct is_future_
: std::false_type
{};

template<typename T>
struct is_future_<std::shared_future<T>>
: std::true_type
{};

template<typename T>
struct is_future_<std::future<T>>
: std::true_type
{};

template<typename T>
using is_future = typename is_future_<std::decay_t<T>>::type;

template<typename T>
constexpr bool is_future_v = is_future<T>::value;

struct async_invoke_wrapper_ {
  template<typename Alloc, typename Fn, typename... Args>
  auto operator()(Alloc alloc, Fn&& fn, Args&&... args)
  -> decltype(auto) {
    return make_shared_ptr_(
        alloc,
        squash_future_(
            fn(alloc, std::forward<Args>(args)...)));
  }
};

template<typename Alloc, typename T>
auto future_as_pointer_future_(Alloc&& alloc, std::future<T> fut)
-> decltype(auto) {
  return std::async(
      std::launch::deferred,
      [](std::decay_t<Alloc> alloc, std::future<T> fut) {
        return make_shared_ptr_(alloc, fut.get());
      },
      std::forward<Alloc>(alloc),
      std::move(fut));
}

template<typename Alloc, typename T>
auto future_as_pointer_future_(Alloc&& alloc, std::shared_future<T> fut)
-> decltype(auto) {
  return std::async(
      std::launch::deferred,
      [](std::decay_t<Alloc> alloc, std::shared_future<T> fut) {
        return make_shared_ptr_(alloc, fut.get());
      },
      std::forward<Alloc>(alloc),
      std::move(fut));
}


}} /* namespace monsoon::cache::<unnamed>::create_ops */


/**
 * \brief Wraps create function so it emits the proper type.
 * \ingroup cache_detail
 *
 * \details
 * Invokes \p Fn, with an allocator and a given set of arguments.
 *
 * The return value of Fn is then wrapped, such that:
 * - any future will be resolved.
 * - any value will become a shared pointer to value.
 *
 * \tparam Fn Functor to be invoked.
 * \tparam Async If set, a future to shared pointer will be returned.
 *    Otherwise, a shared pointer will be returned.
 */
template<typename Fn, bool Async>
class create_handler {
 public:
  create_handler(Fn&& fn)
  noexcept(std::is_nothrow_move_constructible_v<Fn>)
  : fn_(fn)
  {}

  create_handler(const Fn& fn)
  noexcept(std::is_nothrow_copy_constructible_v<Fn>)
  : fn_(fn)
  {}

  template<typename Alloc, typename... Args>
  auto operator()(Alloc alloc, Args&&... args) const
  -> decltype(auto) {
    using namespace create_ops;

    return make_shared_ptr_(
        alloc,
        squash_future_(
            fn_(alloc, std::forward<Args>(args)...)));
  }

 private:
  Fn fn_;
};

/**
 * \brief Wraps create function so it emits the proper type.
 * \ingroup cache_detail
 *
 * \details
 * Invokes \p Fn, with an allocator and a given set of arguments.
 *
 * The return value of Fn is then wrapped, such that:
 * - any value will become a shared pointer to value.
 * - if Fn does not return a future, the invocation will be wrapped inside a call to std::async.
 *
 * The call to std::async uses the std::launch::deferred policy.
 *
 * \tparam Fn Functor to be invoked.
 * \tparam Async If set, a future to shared pointer will be returned.
 *    Otherwise, a shared pointer will be returned.
 */
template<typename Fn>
class create_handler<Fn, true> { // Async case.
 public:
  create_handler(Fn&& fn)
  noexcept(std::is_nothrow_move_constructible_v<Fn>)
  : fn_(fn)
  {}

  create_handler(const Fn& fn)
  noexcept(std::is_nothrow_copy_constructible_v<Fn>)
  : fn_(fn)
  {}

  template<typename Alloc, typename... Args>
  auto operator()(Alloc alloc, Args&&... args) const
  -> decltype(auto) {
    using namespace create_ops;

    using raw_result_type = decltype(fn_(alloc, std::forward<Args>(args)...));
    if constexpr(is_future_v<raw_result_type>) {
      return future_as_pointer_future_(
          alloc,
          fn_(alloc, std::forward<Args>(args)...));
    } else {
      return std::async(
          std::launch::deferred,
          async_invoke_wrapper_(),
          alloc,
          fn_,
          std::forward<Args>(args)...).share();
    }
  }

 private:
  Fn fn_;
};

///\brief Returns a create handler for the given create function.
template<bool Async, typename Fn>
auto make_create_handler(Fn&& fn)
-> create_handler<std::decay_t<Fn>, Async> {
  return std::forward<Fn>(fn);
}


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_CREATE_HANDLER_H */
