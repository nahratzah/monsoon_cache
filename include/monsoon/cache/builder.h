#ifndef MONSOON_CACHE_BUILDER_H
#define MONSOON_CACHE_BUILDER_H

///\file
///\ingroup cache

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace monsoon::cache {


template<typename T, typename U, typename Hash, typename Eq, typename Alloc, typename Create, typename VPtr = std::shared_ptr<U>>
class extended_cache;

struct stats_vars {
  stats_vars(std::string name, bool tls = false)
  : name(std::move(name)),
    tls(tls)
  {}

  std::string name;
  bool tls = false;
};


} /* namespace monsoon::cache */

namespace monsoon::cache::builder_vars_ {


template<typename... Vars> class builder_impl;


namespace {


template<typename...> struct listtype;


template<typename... Types>
struct listtype {
  template<typename T, typename U>
  using replace = listtype<
      std::conditional_t<
          std::is_same_v<Types, T>,
          U,
          Types>...>;

  template<template<typename...> class Template>
  using apply = Template<Types...>;
};


template<typename Builder, typename T, typename U> struct builder_rewrite;

template<typename... Vars, typename T, typename U>
struct builder_rewrite<builder_impl<Vars...>, T, U> {
  using type = typename listtype<Vars...>::template replace<T, U>::template apply<builder_impl>;
};

template<typename Builder, typename T, typename U>
using builder_rewrite_t = typename builder_rewrite<Builder, T, U>::type;


} /* namespace monsoon::cache::builder_vars_::<unnamed> */


template<typename Type> class key_var;
template<typename Type> class mapped_ptr_var;
template<bool Enabled> class max_memory_var;
template<bool Enabled> class max_size_var;
template<bool Enabled> class max_age_var;
template<bool Enabled> class access_expire_var;
template<bool Enabled> class thread_safe_var;
class concurrency_var;
template<bool Enabled> class async_var;
template<bool Enabled> class stats_var;
template<typename Hash> class hash_var;
template<typename Eq> class equality_var;
template<typename Alloc> class allocator_var;


template<template<bool> class Var>
struct is_bool_var_ {
  template<typename T, typename = void>
  struct matcher
  : std::false_type
  {};

  template<typename T>
  struct matcher<T, decltype(accept_(std::declval<const T*>()))>
  : std::true_type
  {};

  template<bool B>
  static void accept_(const Var<B>*);
};

template<template<typename> class Var>
struct is_type_var_ {
  template<typename T, typename = void>
  struct matcher
  : std::false_type
  {};

  template<typename T>
  struct matcher<T, decltype(accept_(std::declval<const T*>()))>
  : std::true_type
  {};

  template<typename T>
  static void accept_(const Var<T>*);
};

template<template<bool> class Var, typename T>
constexpr bool is_bool_var = is_bool_var_<Var>::template matcher<T>::value;

template<template<typename> class Var, typename T>
constexpr bool is_type_var = is_type_var_<Var>::template matcher<T>::value;


template<typename Type, typename Builder>
struct key_var_impl
: public key_var<Type>
{
  constexpr key_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr key_var_impl([[maybe_unused]] const OtherBuilder& b, key_var<Type>&& var) noexcept
  : key_var<Type>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_type_var<key_var, std::decay_t<Var>>>>
  constexpr key_var_impl(const key_var<Type>& b, Var&& var) noexcept
  : key_var<Type>(b)
  {}

  protected:
  ~key_var_impl() noexcept = default;
};

template<typename Type>
class key_var {
  public:
  template<typename Builder>
  using impl = key_var_impl<Type, Builder>;

  using key_or_void_type = Type;

  protected:
  ~key_var() noexcept = default;
};


template<typename Type, typename Builder>
struct mapped_ptr_var_impl
: public mapped_ptr_var<Type>
{
  constexpr mapped_ptr_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr mapped_ptr_var_impl([[maybe_unused]] const OtherBuilder& b, mapped_ptr_var<Type>&& var) noexcept
  : mapped_ptr_var<Type>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_type_var<mapped_ptr_var, std::decay_t<Var>>>>
  constexpr mapped_ptr_var_impl(const mapped_ptr_var<Type>& b, Var&& var) noexcept
  : mapped_ptr_var<Type>(b)
  {}

  protected:
  ~mapped_ptr_var_impl() noexcept = default;
};

template<typename Type>
class mapped_ptr_var {
  public:
  template<typename Builder>
  using impl = mapped_ptr_var_impl<Type, Builder>;

  using pointer = typename std::pointer_traits<Type>::pointer;
  using mapped_type = typename std::pointer_traits<Type>::element_type;

  protected:
  ~mapped_ptr_var() noexcept = default;
};


template<bool Enabled, typename Builder> struct max_memory_var_impl;

template<>
class max_memory_var<true> {
  template<bool, typename> friend struct max_memory_var_impl;

  public:
  template<typename Builder> using impl = max_memory_var_impl<true, Builder>;

  constexpr max_memory_var(std::uintptr_t v) noexcept : v(v) {}

  /**
   * \brief Limit on the amount of memory the cache will use.
   * \details
   * The cache will attempt to keep the memory usage below this value.
   *
   * \note
   * Objecs that were created by the cache, but no longer in the cache,
   * count towards the memory usage of the cache.
   *
   * \note
   * The cache is not able to track memory, unless a cache_allocator is used.
   *
   * \bug The cache should probably fail construction, or warn, when max_memory
   * is set, but the allocator isn't a cache_allocator.
   * Currently, it silently allows this.
   */
  constexpr auto max_memory() const noexcept -> std::optional<std::uintptr_t> {
    return std::optional<std::uintptr_t>(v);
  }

  protected:
  ~max_memory_var() noexcept = default;

  private:
  std::uintptr_t v;
};
template<>
class max_memory_var<false> {
  template<bool, typename> friend struct max_memory_var_impl;

  public:
  template<typename Builder> using impl = max_memory_var_impl<false, Builder>;

  constexpr max_memory_var() noexcept = default;

  /**
   * \brief Limit on the amount of memory the cache will use.
   * \details
   * The cache will attempt to keep the memory usage below this value.
   *
   * \note
   * Objecs that were created by the cache, but no longer in the cache,
   * count towards the memory usage of the cache.
   *
   * \note
   * The cache is not able to track memory, unless a cache_allocator is used.
   *
   * \bug The cache should probably fail construction, or warn, when max_memory
   * is set, but the allocator isn't a cache_allocator.
   * Currently, it silently allows this.
   */
  constexpr auto max_memory() const noexcept -> std::optional<std::uintptr_t> {
    return std::optional<std::uintptr_t>();
  }

  protected:
  ~max_memory_var() noexcept = default;
};

template<bool Enabled, typename Builder>
struct max_memory_var_impl
: public max_memory_var<Enabled>
{
  constexpr max_memory_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr max_memory_var_impl([[maybe_unused]] const OtherBuilder& b, max_memory_var<Enabled>&& var) noexcept
  : max_memory_var<Enabled>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_bool_var<max_memory_var, std::decay_t<Var>>>>
  constexpr max_memory_var_impl(const max_memory_var<Enabled>& b, Var&& var) noexcept
  : max_memory_var<Enabled>(b)
  {}

  using max_memory_var<Enabled>::max_memory;

  constexpr auto max_memory(std::uintptr_t v) const noexcept -> builder_rewrite_t<Builder, max_memory_var<Enabled>, max_memory_var<true>> {
    return builder_rewrite_t<Builder, max_memory_var<Enabled>, max_memory_var<true>>(
        static_cast<const Builder&>(*this),
        max_memory_var<true>(v));
  }

  constexpr auto no_max_memory() const noexcept -> builder_rewrite_t<Builder, max_memory_var<Enabled>, max_memory_var<false>> {
    return builder_rewrite_t<Builder, max_memory_var<Enabled>, max_memory_var<false>>(
        static_cast<const Builder&>(*this),
        max_memory_var<false>());
  }

  protected:
  ~max_memory_var_impl() noexcept = default;
};


template<bool Enabled, typename Builder> struct max_size_var_impl;

template<>
class max_size_var<true> {
  template<bool, typename> friend struct max_size_var_impl;

  public:
  template<typename Builder> using impl = max_size_var_impl<true, Builder>;

  constexpr max_size_var(std::uintptr_t v) noexcept : v(v) {}

  /**
   * \brief Limit the amount of items in the cache.
   * \details
   * The cache will attempt to keep the number of items below this value.
   */
  constexpr auto max_size() const noexcept -> std::optional<std::uintptr_t> {
    return std::optional<std::uintptr_t>(v);
  }

  protected:
  ~max_size_var() noexcept = default;

  private:
  std::uintptr_t v;
};
template<>
class max_size_var<false> {
  template<bool, typename> friend struct max_size_var_impl;

  public:
  template<typename Builder> using impl = max_size_var_impl<false, Builder>;

  constexpr max_size_var() noexcept = default;

  /**
   * \brief Limit the amount of items in the cache.
   * \details
   * The cache will attempt to keep the number of items below this value.
   */
  constexpr auto max_size() const noexcept -> std::optional<std::uintptr_t> {
    return std::optional<std::uintptr_t>();
  }

  protected:
  ~max_size_var() noexcept = default;
};

template<bool Enabled, typename Builder>
struct max_size_var_impl
: public max_size_var<Enabled>
{
  constexpr max_size_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr max_size_var_impl([[maybe_unused]] const OtherBuilder& b, max_size_var<Enabled>&& var) noexcept
  : max_size_var<Enabled>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_bool_var<max_size_var, std::decay_t<Var>>>>
  constexpr max_size_var_impl(const max_size_var<Enabled>& b, Var&& var) noexcept
  : max_size_var<Enabled>(b)
  {}

  using max_size_var<Enabled>::max_size;

  constexpr auto max_size(std::uintptr_t v) const noexcept -> builder_rewrite_t<Builder, max_size_var<Enabled>, max_size_var<true>> {
    return builder_rewrite_t<Builder, max_size_var<Enabled>, max_size_var<true>>(
        static_cast<const Builder&>(*this),
        max_size_var<true>(v));
  }

  constexpr auto no_max_size() const noexcept -> builder_rewrite_t<Builder, max_size_var<Enabled>, max_size_var<false>> {
    return builder_rewrite_t<Builder, max_size_var<Enabled>, max_size_var<false>>(
        static_cast<const Builder&>(*this),
        max_size_var<false>());
  }

  protected:
  ~max_size_var_impl() noexcept = default;
};


template<bool Enabled, typename Builder> struct max_age_var_impl;

template<>
class max_age_var<true> {
  template<bool, typename> friend struct max_age_var_impl;

  public:
  template<typename Builder> using impl = max_age_var_impl<true, Builder>;

  constexpr max_age_var(std::chrono::seconds v) noexcept : v(v) {}

  /**
   * \brief Put a hard limit on the age of items returned by the cache.
   * \details
   * When set, the cache will never return an item that was constructed more
   * than max_age in the past.
   *
   * \note
   * Element age is measured from the start of creation.
   * If an async cache resolve takes a long time, the element can expire prior
   * to being created.
   * In this case, any cache::get() request that joined after the creation
   * started, but before the object expired, will return the expired object.
   */
  constexpr auto max_age() const noexcept -> std::optional<std::chrono::seconds> {
    return std::optional<std::chrono::seconds>(v);
  }

  protected:
  ~max_age_var() noexcept = default;

  private:
  std::chrono::seconds v;
};
template<>
class max_age_var<false> {
  template<bool, typename> friend struct max_age_var_impl;

  public:
  template<typename Builder> using impl = max_age_var_impl<false, Builder>;

  constexpr max_age_var() noexcept = default;

  /**
   * \brief Put a hard limit on the age of items returned by the cache.
   * \details
   * When set, the cache will never return an item that was constructed more
   * than max_age in the past.
   *
   * \note
   * Element age is measured from the start of creation.
   * If an async cache resolve takes a long time, the element can expire prior
   * to being created.
   * In this case, any cache::get() request that joined after the creation
   * started, but before the object expired, will return the expired object.
   */
  constexpr auto max_age() const noexcept -> std::optional<std::chrono::seconds> {
    return std::optional<std::chrono::seconds>();
  }

  protected:
  ~max_age_var() noexcept = default;
};

template<bool Enabled, typename Builder>
struct max_age_var_impl
: public max_age_var<Enabled>
{
  constexpr max_age_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr max_age_var_impl([[maybe_unused]] const OtherBuilder& b, max_age_var<Enabled>&& var) noexcept
  : max_age_var<Enabled>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_bool_var<max_age_var, std::decay_t<Var>>>>
  constexpr max_age_var_impl(const max_age_var<Enabled>& b, Var&& var) noexcept
  : max_age_var<Enabled>(b)
  {}

  using max_age_var<Enabled>::max_age;

  constexpr auto max_age(std::chrono::seconds v) const -> builder_rewrite_t<Builder, max_age_var<Enabled>, max_age_var<true>> {
    if (v <= std::chrono::seconds::zero()) throw std::invalid_argument("negative/zero expiry");
    return builder_rewrite_t<Builder, max_age_var<Enabled>, max_age_var<true>>(
        static_cast<const Builder&>(*this),
        max_age_var<true>(v));
  }

  constexpr auto no_max_age() const noexcept -> builder_rewrite_t<Builder, max_age_var<Enabled>, max_age_var<false>> {
    return builder_rewrite_t<Builder, max_age_var<Enabled>, max_age_var<false>>(
        static_cast<const Builder&>(*this),
        max_age_var<false>());
  }

  protected:
  ~max_age_var_impl() noexcept = default;
};


template<bool Enabled, typename Builder> struct access_expire_var_impl;

template<>
class access_expire_var<true> {
  template<bool, typename> friend struct access_expire_var_impl;

  public:
  template<typename Builder> using impl = access_expire_var_impl<true, Builder>;

  constexpr access_expire_var(std::chrono::seconds v) noexcept : v(v) {}

  /**
   * \brief Try to keep items in the cache for the given duration after access.
   * \details
   * The cache will keep the accessed item around for this duration,
   * irrespective of if it is actively used outside the cache.
   *
   * \note
   * The item may still be removed from the cache, if forced by other
   * constraints, such as
   * \ref cache_builder_vars::max_age "max_age()" or
   * \ref cache_builder_vars::max_size "max_size()".
   */
  constexpr auto access_expire() const noexcept -> std::optional<std::chrono::seconds> {
    return std::optional<std::chrono::seconds>(v);
  }

  protected:
  ~access_expire_var() noexcept = default;

  private:
  std::chrono::seconds v;
};
template<>
class access_expire_var<false> {
  template<bool, typename> friend struct access_expire_var_impl;

  public:
  template<typename Builder> using impl = access_expire_var_impl<false, Builder>;

  constexpr access_expire_var() noexcept = default;

  /**
   * \brief Try to keep items in the cache for the given duration after access.
   * \details
   * The cache will keep the accessed item around for this duration,
   * irrespective of if it is actively used outside the cache.
   *
   * \note
   * The item may still be removed from the cache, if forced by other
   * constraints, such as
   * \ref cache_builder_vars::max_age "max_age()" or
   * \ref cache_builder_vars::max_size "max_size()".
   */
  constexpr auto access_expire() const noexcept -> std::optional<std::chrono::seconds> {
    return std::optional<std::chrono::seconds>();
  }

  protected:
  ~access_expire_var() noexcept = default;
};

template<bool Enabled, typename Builder>
struct access_expire_var_impl
: public access_expire_var<Enabled>
{
  constexpr access_expire_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr access_expire_var_impl([[maybe_unused]] const OtherBuilder& b, access_expire_var<Enabled>&& var) noexcept
  : access_expire_var<Enabled>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_bool_var<access_expire_var, std::decay_t<Var>>>>
  constexpr access_expire_var_impl(const access_expire_var<Enabled>& b, Var&& var) noexcept
  : access_expire_var<Enabled>(b)
  {}

  using access_expire_var<Enabled>::access_expire;

  constexpr auto access_expire(std::chrono::seconds v) const -> builder_rewrite_t<Builder, access_expire_var<Enabled>, access_expire_var<true>> {
    if (v <= std::chrono::seconds::zero()) throw std::invalid_argument("negative/zero expiry");
    return builder_rewrite_t<Builder, access_expire_var<Enabled>, access_expire_var<true>>(
        static_cast<const Builder&>(*this),
        access_expire_var<true>(v));
  }

  constexpr auto no_access_expire() const noexcept -> builder_rewrite_t<Builder, access_expire_var<Enabled>, access_expire_var<false>> {
    return builder_rewrite_t<Builder, access_expire_var<Enabled>, access_expire_var<false>>(
        static_cast<const Builder&>(*this),
        access_expire_var<false>());
  }

  protected:
  ~access_expire_var_impl() noexcept = default;
};


template<bool Enabled, typename Builder> struct thread_safe_var_impl;

template<bool Enabled>
class thread_safe_var {
  template<bool, typename> friend struct thread_safe_var_impl;

  public:
  template<typename Builder> using impl = thread_safe_var_impl<Enabled, Builder>;

  constexpr thread_safe_var() noexcept = default;

  /**
   * \brief Make the cache thread safe.
   * \details
   * Adds a mutex to the cache that ensures the cache can be safely used by
   * multiple threads.
   *
   * If \ref cache_builder_vars::async "async()" is enabled, the cache will
   * allow other threads to access the cache during element resolution.
   */
  constexpr auto thread_safe() const noexcept -> bool {
    return Enabled;
  }

  protected:
  ~thread_safe_var() noexcept = default;
};

template<bool Enabled, typename Builder>
struct thread_safe_var_impl
: public thread_safe_var<Enabled>
{
  constexpr thread_safe_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr thread_safe_var_impl([[maybe_unused]] const OtherBuilder& b, thread_safe_var<Enabled>&& var) noexcept
  : thread_safe_var<Enabled>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_bool_var<thread_safe_var, std::decay_t<Var>>>>
  constexpr thread_safe_var_impl(const thread_safe_var<Enabled>& b, Var&& var) noexcept
  : thread_safe_var<Enabled>(b)
  {}

  using thread_safe_var<Enabled>::thread_safe;

  constexpr auto thread_safe(std::uintptr_t v) const noexcept -> builder_rewrite_t<Builder, thread_safe_var<Enabled>, thread_safe_var<true>> {
    return builder_rewrite_t<Builder, thread_safe_var<Enabled>, thread_safe_var<true>>(
        static_cast<const Builder&>(*this),
        thread_safe_var<true>());
  }

  constexpr auto not_thread_safe() const noexcept -> builder_rewrite_t<Builder, thread_safe_var<Enabled>, thread_safe_var<false>> {
    return builder_rewrite_t<Builder, thread_safe_var<Enabled>, thread_safe_var<false>>(
        static_cast<const Builder&>(*this),
        thread_safe_var<false>());
  }

  protected:
  ~thread_safe_var_impl() noexcept = default;
};


template<typename Builder> struct concurrency_var_impl;

class concurrency_var {
  template<typename> friend struct concurrency_var_impl;

  public:
  template<typename Builder> using impl = concurrency_var_impl<Builder>;

  constexpr concurrency_var() noexcept : v(0) {}
  constexpr concurrency_var(unsigned int v) noexcept : v(v) {}

  /**
   * \brief Make the cache thread safe.
   * \details
   * Adds a mutex to the cache that ensures the cache can be safely used by
   * multiple threads.
   *
   * If \ref cache_builder_vars::async "async()" is enabled, the cache will
   * allow other threads to access the cache during element resolution.
   */
  constexpr auto concurrency() const noexcept -> unsigned int {
    return v;
  }

  protected:
  ~concurrency_var() noexcept = default;

  private:
  unsigned int v;
};

template<typename Builder>
struct concurrency_var_impl
: public concurrency_var
{
  constexpr concurrency_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr concurrency_var_impl([[maybe_unused]] const OtherBuilder& b, concurrency_var&& var) noexcept
  : concurrency_var(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!std::is_base_of_v<concurrency_var, std::decay_t<Var>>>>
  constexpr concurrency_var_impl(const concurrency_var& b, Var&& var) noexcept
  : concurrency_var(b)
  {}

  using concurrency_var::concurrency;

  constexpr auto concurrency(unsigned int v) const noexcept -> builder_rewrite_t<Builder, concurrency_var, concurrency_var> {
    return builder_rewrite_t<Builder, concurrency_var, concurrency_var>(
        static_cast<const Builder&>(*this),
        concurrency_var(v));
  }

  constexpr auto no_concurrency() const noexcept -> builder_rewrite_t<Builder, concurrency_var, concurrency_var> {
    return concurrency(1);
  }

  protected:
  ~concurrency_var_impl() noexcept = default;
};


template<typename Builder> struct load_factor_var_impl;

class load_factor_var {
  template<typename> friend struct load_factor_var_impl;

  public:
  template<typename Builder> using impl = load_factor_var_impl<Builder>;

  constexpr load_factor_var() noexcept : v(1.0) {}
  constexpr load_factor_var(float v) noexcept : v(v) {}

  /**
   * \brief Assign a load factor for the cache.
   * \details
   * The cache is implemented using a hash map.
   * The load factor determines the number of items per bucket of the hash map.
   *
   * \note
   * Since the cache only does maintenance on a single bucket, during a
   * mutable operation, a higher load factor will enable the cache to do
   * more maintenance.
   * A value that is too low will likely disable any maintenance functionality
   * of the cache.
   */
  constexpr auto load_factor() const noexcept -> float {
    return v;
  }

  protected:
  ~load_factor_var() noexcept = default;

  private:
  float v;
};

template<typename Builder>
struct load_factor_var_impl
: public load_factor_var
{
  constexpr load_factor_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr load_factor_var_impl([[maybe_unused]] const OtherBuilder& b, load_factor_var&& var) noexcept
  : load_factor_var(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!std::is_base_of_v<load_factor_var, std::decay_t<Var>>>>
  constexpr load_factor_var_impl(const load_factor_var& b, Var&& var) noexcept
  : load_factor_var(b)
  {}

  using load_factor_var::load_factor;

  constexpr auto load_factor(float v) const -> builder_rewrite_t<Builder, load_factor_var, load_factor_var> {
    if (v <= 0.0 || !std::isfinite(v)) throw std::invalid_argument("invalid load factor");
    return builder_rewrite_t<Builder, load_factor_var, load_factor_var>(
        static_cast<const Builder&>(*this),
        load_factor_var(v));
  }

  protected:
  ~load_factor_var_impl() noexcept = default;
};


template<bool Enabled, typename Builder> struct async_var_impl;

template<bool Enabled>
class async_var {
  template<bool, typename> friend struct async_var_impl;

  public:
  template<typename Builder> using impl = async_var_impl<Enabled, Builder>;

  constexpr async_var() noexcept = default;

  /**
   * \brief Use asynchronous resolution of cache values.
   * \details
   * If enabled, the cache will perform its value construction asynchronously.
   * During resolving of the value, the cache can be accessed by other threads.
   *
   * The create function may, but is not required to, return a std::future or
   * std::shared_future.
   * Doing so will enable more control over the async resolution.
   *
   * For example, if using a connection pool that limits the number of
   * concurrent requests to a server, the cache may be kept in a locked state
   * for very long, if the server is unavailable or too many concurrent
   * requests are in progress.
   * By setting async, the cache will unlock during resolution and thus
   * minimize the impact on other threads.
   *
   * Regardless of wether the cache is, or is not, async, it's get() method
   * will block until resolution completes.
   *
   * It is recommended not to use async resolution, if all the cache does is
   * a fast computation (for instance when using the cache to reduce many
   * copies of data).
   * In this case, the unlock and relock of the cache may be more overhead
   * than simply blocking the other threads.
   *
   * \note
   * Identity caches (with a key type of void) can never be async, and this
   * property will be ignored.
   */
  constexpr auto async() const noexcept -> bool {
    return Enabled;
  }

  protected:
  ~async_var() noexcept = default;
};

template<bool Enabled, typename Builder>
struct async_var_impl
: public async_var<Enabled>
{
  constexpr async_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr async_var_impl([[maybe_unused]] const OtherBuilder& b, async_var<Enabled>&& var) noexcept
  : async_var<Enabled>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_bool_var<async_var, std::decay_t<Var>>>>
  constexpr async_var_impl(const async_var<Enabled>& b, Var&& var) noexcept
  : async_var<Enabled>(b)
  {}

  using async_var<Enabled>::async;

  constexpr auto enable_async() const noexcept -> builder_rewrite_t<Builder, async_var<Enabled>, async_var<true>> {
    return builder_rewrite_t<Builder, async_var<Enabled>, async_var<true>>(
        static_cast<const Builder&>(*this),
        async_var<true>());
  }

  constexpr auto disable_async() const noexcept -> builder_rewrite_t<Builder, async_var<Enabled>, async_var<false>> {
    return builder_rewrite_t<Builder, async_var<Enabled>, async_var<false>>(
        static_cast<const Builder&>(*this),
        async_var<false>());
  }

  protected:
  ~async_var_impl() noexcept = default;
};


template<bool Enabled, typename Builder> struct stats_var_impl;

template<>
class stats_var<true> {
  template<bool, typename> friend struct stats_var_impl;

  public:
  template<typename Builder> using impl = stats_var_impl<true, Builder>;

  stats_var(stats_vars v) noexcept : v(std::move(v)) {}

  /**
   * \brief Limit on the amount of memory the cache will use.
   * \details
   * The cache will attempt to keep the memory usage below this value.
   *
   * \note
   * Objecs that were created by the cache, but no longer in the cache,
   * count towards the memory usage of the cache.
   *
   * \note
   * The cache is not able to track memory, unless a cache_allocator is used.
   *
   * \bug The cache should probably fail construction, or warn, when stats
   * is set, but the allocator isn't a cache_allocator.
   * Currently, it silently allows this.
   */
  auto stats() const noexcept -> std::optional<stats_vars> {
    return std::optional<stats_vars>(v);
  }

  protected:
  ~stats_var() noexcept = default;

  private:
  stats_vars v;
};
template<>
class stats_var<false> {
  template<bool, typename> friend struct stats_var_impl;

  public:
  template<typename Builder> using impl = stats_var_impl<false, Builder>;

  constexpr stats_var() noexcept = default;

  /**
   * \brief Limit on the amount of memory the cache will use.
   * \details
   * The cache will attempt to keep the memory usage below this value.
   *
   * \note
   * Objecs that were created by the cache, but no longer in the cache,
   * count towards the memory usage of the cache.
   *
   * \note
   * The cache is not able to track memory, unless a cache_allocator is used.
   *
   * \bug The cache should probably fail construction, or warn, when stats
   * is set, but the allocator isn't a cache_allocator.
   * Currently, it silently allows this.
   */
  auto stats() const noexcept -> std::optional<stats_vars> {
    return std::optional<stats_vars>();
  }

  protected:
  ~stats_var() noexcept = default;
};

template<bool Enabled, typename Builder>
struct stats_var_impl
: public stats_var<Enabled>
{
  constexpr stats_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr stats_var_impl([[maybe_unused]] const OtherBuilder& b, stats_var<Enabled>&& var) noexcept
  : stats_var<Enabled>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_bool_var<stats_var, std::decay_t<Var>>>>
  constexpr stats_var_impl(const stats_var<Enabled>& b, Var&& var) noexcept
  : stats_var<Enabled>(b)
  {}

  using stats_var<Enabled>::stats;

  auto stats(stats_vars v) const noexcept -> builder_rewrite_t<Builder, stats_var<Enabled>, stats_var<true>> {
    return builder_rewrite_t<Builder, stats_var<Enabled>, stats_var<true>>(
        static_cast<const Builder&>(*this),
        stats_var<true>(std::move(v)));
  }

  auto stats(std::string name, bool tls = false) const noexcept -> builder_rewrite_t<Builder, stats_var<Enabled>, stats_var<true>> {
    return stats(stats_vars(std::move(name), tls));
  }

  constexpr auto no_stats() const noexcept -> builder_rewrite_t<Builder, stats_var<Enabled>, stats_var<false>> {
    return builder_rewrite_t<Builder, stats_var<Enabled>, stats_var<false>>(
        static_cast<const Builder&>(*this),
        stats_var<false>());
  }

  protected:
  ~stats_var_impl() noexcept = default;
};


template<typename Hash, typename Builder>
struct hash_var_impl
: public hash_var<Hash>
{
  constexpr hash_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr hash_var_impl([[maybe_unused]] const OtherBuilder& b, hash_var<Hash>&& var) noexcept
  : hash_var<Hash>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_type_var<hash_var, std::decay_t<Var>>>>
  constexpr hash_var_impl(const hash_var<Hash>& b, Var&& var) noexcept
  : hash_var<Hash>(b)
  {}

  using hash_var<Hash>::hash;

  template<typename NewHash>
  constexpr auto hash(NewHash&& v) const noexcept -> builder_rewrite_t<Builder, hash_var<Hash>, hash_var<std::decay_t<NewHash>>> {
    static_assert(std::is_nothrow_invocable_v<std::decay_t<NewHash>, const typename Builder::key_type&>,
        "Hash function must accept a key_type parameter.");

    return builder_rewrite_t<Builder, hash_var<Hash>, hash_var<std::decay_t<NewHash>>>(
        static_cast<const Builder&>(*this),
        hash_var<std::decay_t<NewHash>>(std::forward<NewHash>(v)));
  }

  template<typename NewHash>
  [[deprecated("please use hash(v) instead")]]
  constexpr auto with_hash(NewHash&& v) const noexcept -> builder_rewrite_t<Builder, hash_var<Hash>, hash_var<std::decay_t<NewHash>>> {
    return hash(std::forward<NewHash>(v));
  }

  protected:
  ~hash_var_impl() noexcept = default;
};

template<typename Hash>
class hash_var {
  template<typename, typename> friend struct hash_var_impl;

  public:
  template<typename Builder> using impl = hash_var_impl<Hash, Builder>;
  using hash_type = Hash;

  constexpr hash_var() noexcept(std::is_nothrow_default_constructible_v<Hash>) = default;
  constexpr hash_var(Hash&& v) noexcept(std::is_nothrow_move_constructible_v<Hash>) : v(std::move(v)) {}
  constexpr hash_var(const Hash& v) noexcept(std::is_nothrow_copy_constructible_v<Hash>) : v(v) {}

  constexpr auto hash() const noexcept -> Hash {
    return v;
  }

  protected:
  ~hash_var() noexcept = default;

  private:
  Hash v;
};


template<typename Eq, typename Builder>
struct equality_var_impl
: public equality_var<Eq>
{
  constexpr equality_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr equality_var_impl([[maybe_unused]] const OtherBuilder& b, equality_var<Eq>&& var) noexcept
  : equality_var<Eq>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_type_var<equality_var, std::decay_t<Var>>>>
  constexpr equality_var_impl(const equality_var<Eq>& b, Var&& var) noexcept
  : equality_var<Eq>(b)
  {}

  using equality_var<Eq>::equality;

  template<typename NewEq>
  constexpr auto equality(NewEq&& v) const noexcept -> builder_rewrite_t<Builder, equality_var<Eq>, equality_var<std::decay_t<NewEq>>> {
    static_assert(std::is_nothrow_invocable_v<std::decay_t<NewEq>, const typename Builder::key_type&, const typename Builder::key_type&>,
        "Equality function must accept a key_type parameter.");

    return builder_rewrite_t<Builder, equality_var<Eq>, equality_var<std::decay_t<NewEq>>>(
        static_cast<const Builder&>(*this),
        equality_var<std::decay_t<NewEq>>(std::forward<NewEq>(v)));
  }

  template<typename NewEq>
  [[deprecated("please use equality(v) instead")]]
  constexpr auto with_equality(NewEq&& v) const noexcept -> builder_rewrite_t<Builder, equality_var<Eq>, equality_var<std::decay_t<NewEq>>> {
    return equality(std::forward<NewEq>(v));
  }

  protected:
  ~equality_var_impl() noexcept = default;
};

template<typename Eq>
class equality_var {
  template<typename, typename> friend struct equality_var_impl;

  public:
  template<typename Builder> using impl = equality_var_impl<Eq, Builder>;
  using equality_type = Eq;

  constexpr equality_var() noexcept(std::is_nothrow_default_constructible_v<Eq>) = default;
  constexpr equality_var(Eq&& v) noexcept(std::is_nothrow_move_constructible_v<Eq>) : v(std::move(v)) {}
  constexpr equality_var(const Eq& v) noexcept(std::is_nothrow_copy_constructible_v<Eq>) : v(v) {}

  constexpr auto equality() const noexcept -> Eq {
    return v;
  }

  protected:
  ~equality_var() noexcept = default;

  private:
  Eq v;
};


template<typename Alloc, typename Builder>
struct allocator_var_impl
: public allocator_var<Alloc>
{
  constexpr allocator_var_impl() noexcept = default;

  template<typename OtherBuilder>
  constexpr allocator_var_impl([[maybe_unused]] const OtherBuilder& b, allocator_var<Alloc>&& var) noexcept
  : allocator_var<Alloc>(std::move(var))
  {}

  template<typename Var, typename = std::enable_if_t<!is_type_var<allocator_var, std::decay_t<Var>>>>
  constexpr allocator_var_impl(const allocator_var<Alloc>& b, Var&& var) noexcept
  : allocator_var<Alloc>(b)
  {}

  using allocator_var<Alloc>::allocator;

  template<typename NewAlloc>
  constexpr auto allocator(NewAlloc&& v) const noexcept -> builder_rewrite_t<Builder, allocator_var<Alloc>, allocator_var<std::decay_t<NewAlloc>>> {
    return builder_rewrite_t<Builder, allocator_var<Alloc>, allocator_var<std::decay_t<NewAlloc>>>(
        static_cast<const Builder&>(*this),
        allocator_var<std::decay_t<NewAlloc>>(std::forward<NewAlloc>(v)));
  }

  template<typename NewAlloc>
  [[deprecated("please use allocator(v) instead")]]
  constexpr auto with_allocator(NewAlloc&& v) const noexcept -> builder_rewrite_t<Builder, allocator_var<Alloc>, allocator_var<std::decay_t<NewAlloc>>> {
    return allocator(std::forward<NewAlloc>(v));
  }

  protected:
  ~allocator_var_impl() noexcept = default;
};

template<typename Alloc>
class allocator_var {
  template<typename, typename> friend struct allocator_var_impl;

  public:
  template<typename Builder> using impl = allocator_var_impl<Alloc, Builder>;
  using allocator_type = Alloc;

  constexpr allocator_var() noexcept(std::is_nothrow_default_constructible_v<Alloc>) = default;
  constexpr allocator_var(Alloc&& v) noexcept(std::is_nothrow_move_constructible_v<Alloc>) : v(std::move(v)) {}
  constexpr allocator_var(const Alloc& v) noexcept(std::is_nothrow_copy_constructible_v<Alloc>) : v(v) {}

  constexpr auto allocator() const noexcept -> Alloc {
    return v;
  }

  protected:
  ~allocator_var() noexcept = default;

  private:
  Alloc v;
};


///\brief Type agnostic data in \ref cache_builder "cache builder".
///\ingroup cache
template<typename... Vars>
class builder_impl
: public Vars::template impl<builder_impl<Vars...>>...
{
  template<typename...> friend class builder_impl;

  public:
  using key_type = std::conditional_t<std::is_void_v<typename builder_impl::key_or_void_type>, typename builder_impl::mapped_type, typename builder_impl::key_or_void_type>;

  constexpr builder_impl() = default;

  template<typename... OtherVars, typename ReplacementVar>
  builder_impl(const builder_impl<OtherVars...>& other, ReplacementVar&& replacement_var)
  : Vars::template impl<builder_impl<Vars...>>(other, std::forward<ReplacementVar>(replacement_var))...
  {}

  constexpr auto no_expire() const noexcept {
    return (*this).no_max_age().no_access_expire();
  }

  /**
   * \brief Build the cache described by this builder.
   *
   * \details
   * The function is used to create cache elements.
   * Its signature must be one of:
   * \code
   * U fn(Alloc alloc, Args&&... args);
   * std::shared_ptr<U> fn(Alloc alloc, Args&&... args);
   * std::future<U> fn(Alloc alloc, Args&&... args);
   * std::shared_future<U> fn(Alloc alloc, Args&&... args);
   * std::future<std::shared_ptr<U>> fn(Alloc alloc, Args&&... args);
   * std::shared_future<std::shared_ptr<U>> fn(Alloc alloc, Args&&... args);
   * \endcode
   * Where \p U is the mapped type of the cache,
   * \p alloc is the allocator of the cache,
   * and \p args is an argument pack passed to the
   * \ref cache::get "cache::get()" or
   * \ref extended_cache::get "extended_cache::get()" methods.
   * (Contrary to the other functors for a cache,
   * \p fn is given rvalue references if possible.)
   *
   * The code will do the right thing regardless of which implementation is
   * provided.
   * If async() is set, the \p fn will resolve with the cache lock released.
   * \note You must include <monsoon/cache/impl.h> if you call this function,
   * since that header contains the implementation.
   * \returns A new cache described by the specification in this.
   */
  template<typename Fn>
  auto build(Fn&& fn) const
  -> extended_cache<
      typename builder_impl::key_or_void_type,
      typename builder_impl::mapped_type,
      typename builder_impl::hash_type,
      typename builder_impl::equality_type,
      typename builder_impl::allocator_type,
      std::decay_t<Fn>,
      typename builder_impl::pointer>;
};


} /* namespace monsoon::cache::builder_vars_ */

namespace monsoon::cache {


template<typename T, typename UPtr,
    // These variables should be moved out of the template list,
    // and instead be set using the ::hash, ::equality, and ::allocator setters.
    typename Hash = std::hash<T>,
    typename Eq = std::equal_to<T>,
    typename Alloc = std::allocator<typename std::pointer_traits<UPtr>::element_type>>
using cache_builder = builder_vars_::builder_impl<
    builder_vars_::key_var<T>,
    builder_vars_::mapped_ptr_var<UPtr>,
    builder_vars_::max_memory_var<false>,
    builder_vars_::max_size_var<false>,
    builder_vars_::max_age_var<false>,
    builder_vars_::access_expire_var<false>,
    builder_vars_::thread_safe_var<true>,
    builder_vars_::concurrency_var,
    builder_vars_::load_factor_var,
    builder_vars_::async_var<false>,
    builder_vars_::stats_var<false>,
    builder_vars_::hash_var<Hash>,
    builder_vars_::equality_var<Eq>,
    builder_vars_::allocator_var<Alloc>
>;


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_BUILDER_H */
