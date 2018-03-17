#ifndef MONSOOON_CACHE_H
#define MONSOOON_CACHE_H

///\file
///\ingroup cache

#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>
#include <monsoon/cache/builder.h>
#include <monsoon/cache/key_decorator.h>
#include <monsoon/cache/cache_query.h>

namespace monsoon::cache {


/**
 * \brief Simple key-value interface of cache.
 * \ingroup cache_detail
 * \details The simple interface omits the variable arguments interface
 * of the \ref extended_cache_intf "extended cache".
 * \tparam K The key type of the cache.
 * \tparam V The mapped type of the cache.
 * \sa \ref cache<K,V>
 */
template<typename K, typename V>
class cache_intf {
 public:
  using key_type = K;
  using pointer = std::shared_ptr<V>;

  virtual ~cache_intf() noexcept {}

  virtual auto get_if_present(const key_type& key)
  -> pointer = 0;

  virtual auto get(const key_type& key)
  -> pointer = 0;

  virtual auto get(key_type&& key)
  -> pointer = 0;
};

/**
 * \brief Simple identity interface of cache.
 * \ingroup cache_detail
 * \details The simple interface omits the variable arguments interface
 * of the \ref extended_cache_intf "extended cache".
 * \tparam V The mapped type of the cache.
 * \sa \ref cache<K,V>
 */
template<typename V>
class cache_intf<void, V> {
 public:
  using key_type = std::decay_t<V>;
  using pointer = std::shared_ptr<V>;

  virtual ~cache_intf() noexcept {}

  virtual auto get_if_present(const key_type& key)
  -> pointer = 0;

  virtual auto get(const key_type& key)
  -> pointer = 0;

  virtual auto get(key_type&& key)
  -> pointer = 0;
};

/**
 * \brief Extended cache interface.
 * \ingroup cache_detail
 * \details The extended interface allows for querying a cache using a set
 * of arguments.
 * \tparam K The key type of the cache.
 * \tparam V The mapped type of the cache.
 * \tparam Hash A hash function for the key.
 * \tparam Eq An equality predicate for the key.
 * \tparam Alloc Allocator used by the cache.
 * \tparam Create Mapped type construction function.
 * \sa \ref extended_cache<K,V>
 */
template<typename K, typename V, typename Hash, typename Eq, typename Alloc, typename Create>
class extended_cache_intf
: public cache_intf<K, V>
{
 public:
  using key_type = typename cache_intf<K, V>::key_type;
  using pointer = typename cache_intf<K, V>::pointer;
  using create_result_type =
      std::decay_t<decltype(std::declval<const Create&>()(std::declval<Alloc&>(), std::declval<const key_type&>()))>;
  using extended_query_type =
      std::conditional_t<std::is_same_v<void, K>,
          cache_query< // Identity case.
              std::function<bool(const key_type&)>,
              std::function<std::tuple<>()>,
              std::function<create_result_type(Alloc)>>,
          cache_query< // Key case.
              std::function<bool(const key_type&)>,
              std::function<std::tuple<key_type>()>,
              std::function<create_result_type(Alloc)>>>;

  template<typename HashArg, typename EqArg, typename CreateArg>
  extended_cache_intf(
      HashArg&& hash,
      EqArg&& eq,
      CreateArg&& create)
  : hash(std::forward<HashArg>(hash)),
    eq(std::forward<EqArg>(eq)),
    create(std::forward<CreateArg>(create))
  {}

  ~extended_cache_intf() noexcept override {}

  using cache_intf<K, V>::get_if_present;
  using cache_intf<K, V>::get;

  virtual auto get(const extended_query_type& q) -> pointer = 0;

  template<typename StoreType, typename... Args>
  auto bind_eq_(const Args&... k) const
  -> decltype(auto) {
    static_assert(!std::is_same_v<void, K>
        || std::is_same_v<pointer, typename StoreType::ptr_return_type>,
        "Identity cache may not be async!");

    if constexpr(!std::is_same_v<void, K>) {
      return [&](const StoreType& s) {
        return eq(s.key, k...);
      };
    } else {
      return [&](const StoreType& s) {
        pointer s_ptr = s.ptr();
        return s_ptr != nullptr && eq(std::as_const(*s_ptr), k...);
      };
    }
  }

  template<typename StoreType>
  auto wrap_ext_predicate_(const std::function<bool(const key_type&)>& predicate) const
  -> decltype(auto) {
    static_assert(!std::is_same_v<void, K>
        || std::is_same_v<pointer, typename StoreType::ptr_return_type>,
        "Identity cache may not be async!");

    if constexpr(!std::is_same_v<void, K>) {
      return [&](const StoreType& s) {
        return predicate(s.key);
      };
    } else {
      return [&](const StoreType& s) {
        pointer s_ptr = s.ptr();
        return s_ptr != nullptr && predicate(std::as_const(*s_ptr));
      };
    }
  }

  ///\bug Should use allocator type, to enable move construction of key.
  template<typename... Args>
  auto bind_tpl_(const Args&... k) const
  -> decltype(auto) {
    if constexpr(std::is_same_v<void, K>) {
      // No key storage, so no key initializers needed.
      return []() {
        return std::tuple<>();
      };
    } else {
      // Make a copy, since the returned tuple won't be used until /after/ the
      // create function has been invoked.
      return [&]() {
        return std::make_tuple(key_type(k...));
      };
    }
  }

  template<typename... Args>
  auto bind_create_(Args&&... args) const
  -> decltype(auto) {
    // Forwarding arguments, since create_ is always the last of the bind_*
    // functions to be called (and because we made sure to copy in bind_tpl_).
    return [&](Alloc alloc) {
      return create(alloc, std::forward<Args>(args)...);
    };
  }

  Hash hash;
  Eq eq;
  Create create;
};

/**
 * \brief An in-memory key-value cache.
 * \ingroup cache
 * \details This cache allows looking up values, by a given key.
 *
 * Unlike \ref extended_cache "extended_cache", it only allows looking up by a
 * specific key type.
 *
 * \tparam K The key type of the cache.
 *    If \p K is void, the cache is an identity cache.
 * \tparam V The mapped type of the cache.
 */
template<typename K, typename V>
class cache {
  // Extended cache calls our constructor.
  template<typename OtherK, typename OtherV, typename Hash, typename Eq, typename Alloc, typename Create>
  friend class extended_cache;

 public:
  ///\brief Key type of the cache.
  ///\details
  ///For identity caches, this is the mapped type of the cache.
  using key_type = typename cache_intf<K, V>::key_type;
  ///\brief Pointer returned by the cache.
  ///\details Shared pointer to the mapped type.
  using pointer = typename cache_intf<K, V>::pointer;

  ///\bug Should we enable this, seeing as that cache is copyable?
  ///Current behaviour sends a clear message when you miss initialization,
  ///though.
  ///(Fix should be applied to both cache and extended_cache.)
  cache() = delete; // Use builder.

  ///\brief Convenience method to create a builder that is compatible with this cache.
  static constexpr auto builder()
  -> cache_builder<K, V> {
    return cache_builder<K, V>();
  }

  ///\brief Convenience method to create a builder that is compatible with this cache.
  ///\param[in] alloc The allocator used by the cache builder.
  template<typename Alloc>
  static constexpr auto builder(Alloc alloc)
  -> cache_builder<K, V, std::hash<K>, std::equal_to<K>, Alloc> {
    return cache_builder<K, V, std::hash<K>, std::equal_to<K>, Alloc>(
        std::hash<K>(), std::equal_to<K>(), std::move(alloc));
  }

 private:
  ///\brief Constructor used by cache builder to initialize the cache.
  ///\param[in] impl A pointer to the implementation of the cache.
  explicit cache(std::shared_ptr<cache_intf<K, V>>&& impl) noexcept
  : impl_(std::move(impl))
  {}

 public:
  /**
   * \brief Retrieve a value if it is present in the cache.
   * \param[in] key The key of the entry that is to be found.
   * \returns The mapped value corresponding to the \p key,
   * or a nullptr if the cache does not contain a (valid) mapping.
   * \note This does not count as a cache hit and will not update expiry
   * information.
   */
  auto get_if_present(const key_type& key) const
  -> pointer {
    return impl_->get_if_present(key);
  }

  /**
   * \brief Retrieve a value if it is present in the cache.
   * \param[in] key The key of the entry that is to be found.
   * \returns The mapped value corresponding to the \p key,
   * creating a mapping if needed.
   */
  auto get(const key_type& key) const
  -> pointer {
    return impl_->get(key);
  }

  /**
   * \brief Retrieve a value if it is present in the cache.
   * \details This version allows the cache to use move semantics on the key,
   * if it needs to construct the value.
   * \param[in] key The key of the entry that is to be found.
   * \returns The mapped value corresponding to the \p key,
   * creating a mapping if needed.
   */
  auto get(key_type&& key) const
  -> pointer {
    return impl_->get(std::move(key));
  }

  ///\brief Functional interface to the cache.
  ///\param[in] key The key of the entry that is to be found.
  ///\returns The mapped value corresponding to the \p key,
  ///creating a mapping if needed.
  auto operator()(const key_type& key) const
  -> pointer {
    return get(key);
  }

  ///\brief Functional interface to the cache.
  ///\param[in] key The key of the entry that is to be found.
  ///   If the mapped value is to be constructed, the key will be
  ///   moved during construction.
  ///\returns The mapped value corresponding to the \p key,
  ///creating a mapping if needed.
  auto operator()(key_type&& key) const
  -> pointer {
    return get(std::move(key));
  }

 private:
  ///\brief Pointer to the implementation.
  std::shared_ptr<cache_intf<K, V>> impl_;
};

/**
 * \brief An in-memory cache that allows lookup by argument pack.
 * \ingroup cache
 * \details This cache allows looking up values, by a given argument pack.
 *
 * In addition to the functionality in the
 * (non extended) \ref monsoon::cache::cache "cache", it allows
 * variadic argument lookups.
 * Due to this, it is a slightly leaky abstraction, as it
 * needs access to the Hash, Eq, Alloc, and Create types.
 * \tparam K The key type of the cache.
 *    If \p K is void, the cache is an identity cache.
 * \tparam V The mapped type of the cache.
 * \tparam Hash A hash function for the key.
 * \tparam Eq An equality predicate for the key.
 * \tparam Alloc Allocator used by the cache.
 * \tparam Create Mapped type construction function.
 */
template<typename K, typename V, typename Hash, typename Eq, typename Alloc, typename Create>
class extended_cache {
  // Cache builder will use private constructor to instantiate cache.
  template<typename OtherK, typename OtherV, typename OtherHash, typename OtherEq, typename OtherAlloc>
  friend class cache_builder;

 private:
  using extended_cache_type = extended_cache_intf<K, V, Hash, Eq, Alloc, Create>;
  using extended_query_type = typename extended_cache_type::extended_query_type;

 public:
  ///\brief Key type of the cache.
  ///\details
  ///For identity caches, this is the mapped type of the cache.
  using key_type = typename cache<K, V>::key_type;
  ///\brief Pointer returned by the cache.
  ///\details Shared pointer to the mapped type.
  using pointer = typename cache<K, V>::pointer;

  ///\bug Should we enable this, seeing as that cache is copyable?
  ///Current behaviour sends a clear message when you miss initialization,
  ///though.
  ///(Fix should be applied to both cache and extended_cache.)
  extended_cache() = delete; // Use builder.

  ///\copydoc cache::builder()
  static constexpr auto builder()
  -> cache_builder<K, V, Hash, Eq, Alloc> {
    return cache_builder<K, V, Hash, Eq, Alloc>();
  }

  ///\copydoc cache::builder(Alloc)
  static constexpr auto builder(Alloc alloc)
  -> cache_builder<K, V, Hash, Eq, Alloc> {
    return cache_builder<K, V, Hash, Eq, Alloc>(
        Hash(), Eq(), std::move(alloc));
  }

 private:
  ///\copydoc cache::cache(std::shared_ptr<cache_intf<K, V>>&&)
  extended_cache(std::shared_ptr<extended_cache_type>&& impl) noexcept
  : impl_(std::move(impl))
  {}

 public:
  /**
   * \brief Retrieve a value if it is present in the cache.
   * \param[in] key The key of the entry that is to be found.
   * \returns The mapped value corresponding to the \p key,
   * or a nullptr if the cache does not contain a (valid) mapping.
   * \note This does not count as a cache hit and will not update expiry
   * information.
   */
  auto get_if_present(const key_type& key) const
  -> pointer {
    return impl_->get_if_present(key);
  }

  /**
   * \brief Retrieve a value if it is present in the cache.
   * \param[in] key The key of the entry that is to be found.
   * \returns The mapped value corresponding to the \p key,
   * creating a mapping if needed.
   */
  auto get(const key_type& key) const
  -> pointer {
    return impl_->get(key);
  }

  /**
   * \brief Retrieve a value if it is present in the cache.
   * \details This version allows the cache to use move semantics on the key,
   * if it needs to construct the value.
   * \param[in] key The key of the entry that is to be found.
   * \returns The mapped value corresponding to the \p key,
   * creating a mapping if needed.
   */
  auto get(key_type&& key) const
  -> pointer {
    return impl_->get(std::move(key));
  }

  ///\brief Functional interface to the cache.
  ///\param[in] key The key of the entry that is to be found.
  ///\returns The mapped value corresponding to the \p key,
  ///creating a mapping if needed.
  auto operator()(const key_type& key) const
  -> pointer {
    return get(key);
  }

  ///\brief Functional interface to the cache.
  ///\param[in] key The key of the entry that is to be found.
  ///   If the mapped value is to be constructed, the key will be
  ///   moved during construction.
  ///\returns The mapped value corresponding to the \p key,
  ///creating a mapping if needed.
  auto operator()(key_type&& key) const
  -> pointer {
    return get(std::move(key));
  }

  /**
   * \brief Variadic interface to the cache.
   * \details
   * This function requires that the hash function, equality predicate,
   * and create function are all invocable using the argument pack (the latter
   * by perfect forwarding, the rest by const reference).
   *
   * \param[in] args Arguments to use during a lookup.
   * \returns The mapped value corresponding to \p args,
   * creating a mapping if needed.
   * \sa \ref cache_builder::with_hash
   * \sa \ref cache_builder::with_equality
   * \sa \ref cache_builder::build
   */
  template<typename... Args>
  auto get(Args&&... args) const
  -> pointer {
    Eq& eq = impl_->eq; // Move pointer resolution outside of lambda below.
    return impl_->get(
        extended_query_type(
            impl_->hash(std::as_const(args)...),
            [&eq, &args...](const key_type& k) { return eq(k, args...); },
            impl_->bind_tpl_(args...),
            impl_->bind_create_(std::forward<Args>(args)...)));
  }

  /**
   * \brief Variadic functional interface to the cache.
   * \details
   * This function requires that the hash function, equality predicate,
   * and create function are all invocable using the argument pack (the latter
   * by perfect forwarding, the rest by const reference).
   *
   * \param[in] args Arguments to use during a lookup.
   * \returns The mapped value corresponding to \p args,
   * creating a mapping if needed.
   * \sa \ref cache_builder::with_hash
   * \sa \ref cache_builder::with_equality
   * \sa \ref cache_builder::build
   */
  template<typename... Args>
  auto operator()(Args&&... args) const
  -> pointer {
    return get(std::forward<Args>(args)...);
  }

  ///\brief This is implicitly convertible to a (non extended) cache.
  operator cache<K, V>() const & {
    return cache<K, V>(impl_);
  }

  ///\brief This is implicitly convertible to a (non extended) cache.
  operator cache<K, V>() && {
    return cache<K, V>(std::move(impl_));
  }

 private:
  ///\brief Pointer to the implementation.
  std::shared_ptr<extended_cache_type> impl_;
};


} /* namespace monsoon::cache */

#endif /* MONSOOON_CACHE_H */
