#ifndef MONSOON_CACHE_BUILDER_H
#define MONSOON_CACHE_BUILDER_H

///\file
///\ingroup cache

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

namespace monsoon::cache {


template<typename T, typename U, typename Hash, typename Eq, typename Alloc, typename Create>
class extended_cache;


///\brief Type agnostic data in \ref cache_builder "cache builder".
///\ingroup cache
class cache_builder_vars {
 public:
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
  constexpr auto max_memory() const noexcept -> std::optional<std::uintptr_t>;
  /**
   * \brief Limit the amount of items in the cache.
   * \details
   * The cache will attempt to keep the number of items below this value.
   */
  constexpr auto max_size() const noexcept -> std::optional<std::uintptr_t>;
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
  constexpr auto max_age() const noexcept -> std::optional<std::chrono::seconds>;
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
  constexpr auto access_expire() const noexcept -> std::optional<std::chrono::seconds>;
  /**
   * \brief Make the cache thread safe.
   * \details
   * Adds a mutex to the cache that ensures the cache can be safely used by
   * multiple threads.
   *
   * If \ref cache_builder_vars::async "async()" is enabled, the cache will
   * allow other threads to access the cache during element resolution.
   */
  constexpr auto thread_safe() const noexcept -> bool;
  /**
   * \brief Optimize the cache for concurrent access by this many threads.
   * \details
   * The cache is sharded into N shards, based on the hash of the key
   * (a transformation is applied, in order to not unbalance the buckets).
   *
   * A value of 0 (zero) represents that the builder should determine the
   * number automatically, in which case it shards according to the value
   * returned by std::thread::hardware_concurrency().
   *
   * If the cache is not thread safe, the value is ignored.
   *
   * \note
   * std::thread::hardware_concurrency() is allowed to return 0, if it
   * cannot determine how many CPUs are present on the system.
   * In this case, the cache defaults to unsharded (i.e., a single shard).
   */
  constexpr auto concurrency() const noexcept -> unsigned int;
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
  constexpr auto load_factor() const noexcept -> float;
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
   * that simply blocking the other threads.
   *
   * \note
   * Identity caches (with a key type of void) can never be async, and this
   * property will be ignored.
   */
  constexpr auto async() const noexcept -> bool;

 protected:
  std::optional<std::uintptr_t> max_memory_, max_size_;
  std::optional<std::chrono::seconds> max_age_, access_expire_;
  bool thread_safe_ = true;
  unsigned int concurrency_ = 0; // zero signifies to use std::hardware_concurrency
  float lf_ = 1.0;
  bool async_ = false;
};

/**
 * \brief Cache builder configures a cache.
 * \ingroup cache
 * \details Contains all parameters to build a cache.
 * \tparam T The key type of the cache.
 * \tparam U The mapped type of the cache.
 * \tparam Hash Hash function on the key type.
 *    For details, see \ref cache_builder::with_hash "with_hash()".
 * \tparam Eq Equality predicate on the key type.
 *    For details, see \ref cache_builder::with_equality "with_equality()".
 * \tparam Alloc Allocator to use for cache elements.
 *    For details, see \ref cache_builder::with_allocator "with_allocator()".
 */
template<typename T, typename U,
    typename Hash = std::hash<T>,
    typename Eq = std::equal_to<T>,
    typename Alloc = std::allocator<U>>
class cache_builder
: public cache_builder_vars
{
 public:
  using key_type = T;
  using mapped_type = U;

  /**
   * \brief Create a cache_builder.
   * \param[in] hash The hash function to use.
   * \param[in] eq The equality predicate to use.
   * \param[in] alloc The allocator to use.
   */
  constexpr explicit cache_builder(
      Hash hash = Hash(), Eq eq = Eq(), Alloc alloc = Alloc());

  /**
   * \brief Create a cache_builder from another cache builder.
   * \param[in] other Configuration of values from another cache builder.
   * \param[in] hash The hash function to use.
   * \param[in] eq The equality predicate to use.
   * \param[in] alloc The allocator to use.
   */
  constexpr explicit cache_builder(
      const cache_builder_vars& other,
      Hash hash = Hash(), Eq eq = Eq(), Alloc alloc = Alloc());

  using cache_builder_vars::max_memory;
  ///\brief Set the max_memory property.
  ///\param[in] v The amount of memory, in bytes.
  ///\sa \ref cache_builder_vars::max_memory
  constexpr auto max_memory(std::uintptr_t v) noexcept -> cache_builder&;
  ///\brief Clear the max_memory property.
  ///\sa \ref cache_builder_vars::max_memory
  constexpr auto no_max_memory() noexcept -> cache_builder&;

  using cache_builder_vars::max_size;
  ///\brief Set the maximum size property.
  ///\param[in] v The size limit.
  ///\sa \ref cache_builder_vars::max_size
  constexpr auto max_size(std::uintptr_t v) noexcept -> cache_builder&;
  ///\brief Clear the maximum size property.
  ///\sa \ref cache_builder_vars::max_size
  constexpr auto no_max_size() noexcept -> cache_builder&;

  using cache_builder_vars::max_age;
  ///\brief Set the maximum age of cache elements.
  ///\sa \ref cache_builder_vars::max_age
  constexpr auto max_age(std::chrono::seconds d) -> cache_builder&;
  ///\brief Don't limit the age of cache elements.
  ///\sa \ref cache_builder_vars::max_age
  constexpr auto no_max_age() noexcept -> cache_builder&;

  using cache_builder_vars::access_expire;
  ///\brief Allow cache elements to linger after creation/access.
  ///\sa \ref cache_builder_vars::access_expire
  constexpr auto access_expire(std::chrono::seconds d) -> cache_builder&;
  ///\brief Clear the access_expire property.
  ///\sa \ref cache_builder_vars::access_expire
  constexpr auto no_access_expire() noexcept -> cache_builder&;

  ///\brief Clear both the access expire and max age properties.
  ///\sa \ref cache_builder_vars::max_age
  ///\sa \ref cache_builder_vars::access_expire
  constexpr auto no_expire() noexcept -> cache_builder&;

  using cache_builder_vars::thread_safe;
  ///\brief Make the cache thread safe.
  ///\sa \ref cache_builder_vars::thread_safe
  constexpr auto thread_safe() noexcept -> cache_builder&;
  ///\brief Make the cache not thread safe.
  ///\sa \ref cache_builder_vars::thread_safe
  constexpr auto not_thread_safe() noexcept -> cache_builder&;

  using cache_builder_vars::concurrency;
  ///\brief Optimize the cache for concurrent access.
  ///\sa \ref cache_builder_vars::concurrency
  constexpr auto with_concurrency(unsigned int n = 0) noexcept -> cache_builder&;
  ///\brief Don't optimize the cache for concurrent access.
  ///\sa \ref cache_builder_vars::concurrency
  constexpr auto no_concurrency() noexcept -> cache_builder&;

  using cache_builder_vars::load_factor;
  ///\brief Change the load factor of the hash map backing the cache.
  ///\sa \ref cache_builder_vars::load_factor
  constexpr auto load_factor(float lf) -> cache_builder&;

  using cache_builder_vars::async;
  ///\brief Enable or disable asynchronous resolution.
  ///\sa \ref cache_builder_vars::async
  constexpr auto async(bool async) noexcept -> cache_builder&;

  /**
   * \brief Use the specified hash function for the cache.
   *
   * \details
   * The functor signature must be:
   * \code
   * std::size_t hash(const Args&... args)
   * \endcode
   * where \p args is the arguments passed to
   * to the cache::get() or extended_cache::get() functions,
   * after ensuring they are const.
   * \param[in] hash A hash function for the cache key_type.
   * \note This method creates a new copy of the builder, due to the type of the builder changing.
   */
  template<typename NewHash>
  constexpr auto with_hash(NewHash hash) const
  -> cache_builder<T, U, NewHash, Eq, Alloc>;

  /**
   * \brief Use the specified equality predicate for the cache.
   *
   * \details
   * The functor signature must be:
   * \code
   * std::size_t hash(const key_type& k, const Args& args...)
   * \endcode
   * where \p args is the arguments passed to
   * to the cache::get() or extended_cache::get() functions,
   * after ensuring they are const.
   * The parameter \p k is the key_type of the cache element
   * (which for identity cached is the mapped value).
   * \param[in] eq An equality predicate for the cache key_type.
   * \note This method creates a new copy of the builder, due to the type of the builder changing.
   */
  template<typename NewEq>
  constexpr auto with_equality(NewEq eq) const
  -> cache_builder<T, U, Hash, NewEq, Alloc>;

  /**
   * \brief Use the specified allocator for the cache.
   * \details
   * The given allocator will be used to allocate both
   * data structures internal to the cache, as well as values.
   *
   * If the allocator is a std::scoped_allocator_adaptor, normal
   * scoped allocator rules apply.
   *
   * If the allocator is a cache_allocator,
   * the \ref cache_builder_vars::max_memory max_memory() property will be
   * able to function.
   * \param[in] alloc An allocator for the cache.
   * \note This method creates a new copy of the builder, due to the type of the builder changing.
   */
  template<typename NewAlloc>
  constexpr auto with_allocator(NewAlloc alloc) const
  -> cache_builder<T, U, Hash, Eq, NewAlloc>;

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
   *
   * \bug Despite my attempts, the build function does not eliminate branches
   * based on constexpr. Therefore, all possible paths are compiled by the
   * compiler. Result of this is code bloat, but performance impact should be
   * negligable, if any.
   */
  template<typename Fn>
  auto build(Fn&& fn) const
  -> extended_cache<T, U, Hash, Eq, Alloc, std::decay_t<Fn>>;

  ///\brief Retrieves the hash function of the cache.
  ///\sa \ref cache_builder::with_hash
  constexpr auto hash() const noexcept -> Hash;
  ///\brief Retrieves the hash function of the cache.
  ///\sa \ref cache_builder::with_equality
  constexpr auto equality() const noexcept -> Eq;
  ///\brief Retrieves the hash function of the cache.
  ///\sa \ref cache_builder::with_allocator
  constexpr auto allocator() const noexcept -> Alloc;

 private:
  Hash hash_;
  Eq eq_;
  Alloc alloc_;
};

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr cache_builder<T, U, Hash, Eq, Alloc>::cache_builder(Hash hash, Eq eq, Alloc alloc)
: hash_(std::move(hash)),
  eq_(std::move(eq)),
  alloc_(std::move(alloc))
{}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr cache_builder<T, U, Hash, Eq, Alloc>::cache_builder(
    const cache_builder_vars& other,
    Hash hash, Eq eq, Alloc alloc)
: cache_builder_vars(other),
  hash_(std::move(hash)),
  eq_(std::move(eq)),
  alloc_(std::move(alloc))
{}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::max_memory(std::uintptr_t v)
noexcept
-> cache_builder& {
  max_memory_ = v;
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::no_max_memory()
noexcept
-> cache_builder& {
  max_memory_.reset();
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::max_size(std::uintptr_t v)
noexcept
-> cache_builder& {
  max_size_ = v;
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::no_max_size()
noexcept
-> cache_builder& {
  max_size_.reset();
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::max_age(std::chrono::seconds d)
-> cache_builder& {
  if (d <= std::chrono::seconds::zero())
    throw std::invalid_argument("negative/zero expiry");
  max_age_ = d;
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::no_max_age()
noexcept
-> cache_builder& {
  max_age_.reset();
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::access_expire(std::chrono::seconds d)
-> cache_builder& {
  if (d <= std::chrono::seconds::zero())
    throw std::invalid_argument("negative/zero expiry");
  access_expire_ = d;
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::no_access_expire()
noexcept
-> cache_builder& {
  access_expire_.reset();
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::no_expire()
noexcept
-> cache_builder& {
  return (*this)
      .no_max_age()
      .no_access_expire();
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::thread_safe()
noexcept
-> cache_builder& {
  thread_safe_ = true;
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::not_thread_safe()
noexcept
-> cache_builder& {
  thread_safe_ = false;
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::with_concurrency(unsigned int n)
noexcept
-> cache_builder& {
  concurrency_ = n;
  return this->thread_safe();
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::no_concurrency()
noexcept
-> cache_builder& {
  this->concurrency_ = 1;
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::load_factor(float lf)
-> cache_builder& {
  if (lf <= 0.0 || !std::isfinite(lf))
    throw std::invalid_argument("invalid load factor");
  lf_ = lf;
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::async(bool async)
noexcept
-> cache_builder& {
  this->async_ = async;
  return *this;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
template<typename NewHash>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::with_hash(NewHash hash) const
-> cache_builder<T, U, NewHash, Eq, Alloc> {
  return cache_builder<T, U, NewHash, Eq, Alloc>(*this, hash, eq_, alloc_);
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
template<typename NewEq>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::with_equality(NewEq eq) const
-> cache_builder<T, U, Hash, NewEq, Alloc> {
  return cache_builder<T, U, Hash, NewEq, Alloc>(*this, hash_, eq, alloc_);
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
template<typename NewAlloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::with_allocator(NewAlloc alloc) const
-> cache_builder<T, U, Hash, Eq, NewAlloc> {
  return cache_builder<T, U, Hash, Eq, NewAlloc>(*this, hash_, eq_, alloc);
}

constexpr auto cache_builder_vars::max_memory() const
noexcept
-> std::optional<std::uintptr_t> {
  return max_memory_;
}

constexpr auto cache_builder_vars::max_size() const
noexcept
-> std::optional<std::uintptr_t> {
  return max_size_;
}

constexpr auto cache_builder_vars::max_age() const
noexcept
-> std::optional<std::chrono::seconds> {
  return max_age_;
}

constexpr auto cache_builder_vars::access_expire() const
noexcept
-> std::optional<std::chrono::seconds> {
  return access_expire_;
}

constexpr auto cache_builder_vars::thread_safe() const
noexcept
-> bool {
  return thread_safe_;
}

constexpr auto cache_builder_vars::concurrency() const
noexcept
-> unsigned int {
  return concurrency_;
}

constexpr auto cache_builder_vars::load_factor() const
noexcept
-> float {
  return lf_;
}

constexpr auto cache_builder_vars::async() const
noexcept
-> bool {
  return async_;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::hash() const
noexcept
-> Hash {
  return hash_;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::equality() const
noexcept
-> Eq {
  return eq_;
}

template<typename T, typename U, typename Hash, typename Eq, typename Alloc>
constexpr auto cache_builder<T, U, Hash, Eq, Alloc>::allocator() const
noexcept
-> Alloc {
  return alloc_;
}


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_BUILDER_H */
