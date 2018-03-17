#ifndef MONSOON_CACHE_IMPL_H
#define MONSOON_CACHE_IMPL_H

///\file
///\ingroup cache

#include <cassert>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <monsoon/cache/builder.h>
#include <monsoon/cache/cache_impl.h>
#include <monsoon/cache/thread_safe_decorator.h>
#include <monsoon/cache/weaken_decorator.h>
#include <monsoon/cache/access_expire_decorator.h>
#include <monsoon/cache/max_age_decorator.h>
#include <monsoon/cache/expire_queue.h>
#include <monsoon/cache/max_size_decorator.h>
#include <monsoon/cache/element.h>
#include <monsoon/cache/mem_use.h>

namespace monsoon::cache {
///\brief Helpers for the builder::build() method.
///\ingroup cache_detail
namespace builder_detail {


struct cache_async_decorator {
  using element_decorator_type = async_element_decorator;

  template<typename Builder>
  constexpr cache_async_decorator([[maybe_unused]] const Builder& b) {}
};


///\brief Create memory tracking data and install it on the allocator.
template<typename Alloc>
auto create_mem_tracking(Alloc& alloc)
-> std::shared_ptr<mem_use> {
  // We use a temporary mem_use to track how much memory we allocate for mem_use.
  auto tmp_mem_use = std::make_shared<mem_use>();
  cache_alloc_dealloc_observer::maybe_set_stats(alloc, tmp_mem_use);

  // Create memory usage tracker.
  std::shared_ptr<mem_use> mem_tracking = std::allocate_shared<mem_use>(alloc);
  // Install memory usage tracker.
  cache_alloc_dealloc_observer::maybe_set_stats(alloc, mem_tracking);
  // Move memory usage as measured by temporary allocator,
  // to account for overhead from mem_tracking initialization.
  mem_tracking->add_mem_use(1, tmp_mem_use->get());

  return mem_tracking;
}


namespace {


template<typename CDS, typename T, typename... D>
struct add_all_except_;

template<typename CDS, typename T>
struct add_all_except_<CDS, T> {
  using type = CDS;
};

template<typename CDS, typename T, typename D0, typename... D>
struct add_all_except_<CDS, T, D0, D...> {
  using type = typename std::conditional_t<std::is_same_v<T, D0>,
        add_all_except_<CDS, T, D...>,
        add_all_except_<decltype(std::declval<CDS>().template add<D0>()), T, D...>>::type;
};

///\brief Keep track of all decorators that are to be applied to the cache.
///\ingroup cache_detail
///\tparam D All decorators that are to be passed to the cache.
template<typename... D>
struct cache_decorator_set {
  ///\brief Specialize the type of the cache.
  ///\tparam T The mapped type of the cache.
  ///\tparam Alloc The allocator for the cache.
  template<typename T, typename Alloc>
  using cache_type = cache_impl<T, Alloc, D...>;

  ///\brief Add type T to the decorator set.
  ///\details Does nothing if T is already part of the decorator set.
  ///\returns A decorator set with all decorators in this, and with T.
  ///\tparam T The decorator to add to the set.
  template<typename T>
  constexpr auto add() const noexcept
  -> std::conditional_t<
      std::disjunction_v<std::is_same<D, T>...>,
      cache_decorator_set<D...>,
      cache_decorator_set<D..., T>> {
    return {};
  }

  ///\brief Remove type T from the decorator set.
  ///\details Does nothing if T is not part of the decorator set.
  ///\returns A decorator set with all decorators in this, except T.
  ///\tparam T The decorator to remove from the set.
  template<typename T>
  constexpr auto remove() const noexcept
  -> typename add_all_except_<cache_decorator_set<>, T, D...>::type {
    return {};
  }
};


template<typename NextApply>
struct apply_thread_safe_ {
  template<typename... D>
  auto operator()(cache_decorator_set<D...> d)
  -> decltype(auto) {
    if (b.thread_safe())
      return next(d.template add<thread_safe_decorator<true>>());
    else
      return next(d.template add<thread_safe_decorator<false>>());
  }

  const cache_builder_vars& b;
  NextApply next;
};

template<typename NextApply>
constexpr auto apply_thread_safe(const cache_builder_vars& b, NextApply&& next)
-> apply_thread_safe_<std::decay_t<NextApply>> {
  return { b, std::forward<NextApply>(next) };
}


template<typename KeyType, typename NextApply>
struct apply_key_type_ {
  template<typename... D>
  auto operator()(cache_decorator_set<D...> d)
  -> decltype(auto) {
    if constexpr(std::is_same_v<void, KeyType>)
      return next(d);
    else
      return next(d.template add<cache_key_decorator<KeyType>>());
  }

  NextApply next;
};

template<typename Builder, typename NextApply>
constexpr auto apply_key_type(const Builder& b, NextApply&& next)
-> apply_key_type_<typename Builder::key_type, std::decay_t<NextApply>> {
  return { std::forward<NextApply>(next) };
}


template<typename NextApply>
struct apply_access_expire_ {
  template<typename... D>
  auto operator()(cache_decorator_set<D...> d)
  -> decltype(auto) {
    if (b.access_expire().has_value())
      return next(d
          .template remove<weaken_decorator>()
          .template add<cache_expire_queue_decorator>()
          .template add<access_expire_decorator>());
    else
      return next(d);
  }

  const cache_builder_vars& b;
  NextApply next;
};

template<typename NextApply>
constexpr auto apply_access_expire(const cache_builder_vars& b, NextApply&& next)
-> apply_access_expire_<std::decay_t<NextApply>> {
  return { b, std::forward<NextApply>(next) };
}


template<typename NextApply>
struct apply_max_age_ {
  template<typename... D>
  auto operator()(cache_decorator_set<D...> d)
  -> decltype(auto) {
    if (b.max_age().has_value())
      return next(d.template add<max_age_decorator>());
    else
      return next(d);
  }

  const cache_builder_vars& b;
  NextApply next;
};

template<typename NextApply>
constexpr auto apply_max_age(const cache_builder_vars& b, NextApply&& next)
-> apply_max_age_<std::decay_t<NextApply>> {
  return { b, std::forward<NextApply>(next) };
}


template<typename KeyType, typename NextApply>
struct apply_async_ {
  template<typename... D>
  auto operator()(cache_decorator_set<D...> d)
  -> decltype(auto) {
    if (b.async())
      return next(d.template add<cache_async_decorator>());
    else
      return next(d);
  }

  const cache_builder_vars& b;
  NextApply next;
};

template<typename NextApply>
struct apply_async_<void, NextApply> {
  template<typename... D>
  auto operator()(cache_decorator_set<D...> d)
  -> decltype(auto) {
    return next(d);
  }

  const cache_builder_vars& b;
  NextApply next;
};

template<typename Builder, typename NextApply>
constexpr auto apply_async(const Builder& b, NextApply&& next)
-> apply_async_<typename Builder::key_type, std::decay_t<NextApply>> {
  return { b, std::forward<NextApply>(next) };
}


template<typename NextApply>
struct apply_max_size_ {
  template<typename... D>
  auto operator()(cache_decorator_set<D...> d)
  -> decltype(auto) {
    if (b.max_size().has_value())
      return next(d
          .template remove<weaken_decorator>()
          .template add<cache_expire_queue_decorator>()
          .template add<max_size_decorator>());
    else
      return next(d);
  }

  const cache_builder_vars& b;
  NextApply next;
};

template<typename NextApply>
constexpr auto apply_max_size(const cache_builder_vars& b, NextApply&& next)
-> apply_max_size_<std::decay_t<NextApply>> {
  return { b, std::forward<NextApply>(next) };
}


template<typename NextApply>
struct apply_max_mem_ {
  template<typename... D>
  auto operator()(cache_decorator_set<D...> d)
  -> decltype(auto) {
    if (b.max_memory().has_value())
      return next(d
          .template remove<weaken_decorator>()
          .template add<cache_expire_queue_decorator>()
          .template add<cache_max_mem_decorator>());
    else
      return next(d);
  }

  const cache_builder_vars& b;
  NextApply next;
};

template<typename NextApply>
constexpr auto apply_max_mem(const cache_builder_vars& b, NextApply&& next)
-> apply_max_mem_<std::decay_t<NextApply>> {
  return { b, std::forward<NextApply>(next) };
}


template<typename Cache, typename = void>
struct has_set_mem_use_
: std::false_type {};

template<typename Cache>
struct has_set_mem_use_<Cache, std::void_t<decltype(std::declval<Cache&>().set_mem_use(std::declval<std::shared_ptr<const mem_use>>()))>>
: std::true_type {};

template<typename Cache>
constexpr bool has_set_mem_use = has_set_mem_use_<Cache>::value;


} /* namespace monsoon::cache::builder_detail::<unnamed> */


template<typename K, typename V, typename Impl, typename Hash, typename Eq, typename Create>
class wrapper final
: public extended_cache_intf<K, V, Hash, Eq, typename Impl::alloc_t, Create>,
  public Impl
{
 public:
  using key_type = typename extended_cache_intf<K, V, Hash, Eq, typename Impl::alloc_t, Create>::key_type;
  using pointer = typename extended_cache_intf<K, V, Hash, Eq, typename Impl::alloc_t, Create>::pointer;
  using store_type = typename Impl::store_type;

  template<typename CreateArg>
  wrapper(const cache_builder<K, V, Hash, Eq, typename Impl::alloc_t>& b,
      CreateArg&& create,
      typename Impl::alloc_t alloc)
  : extended_cache_intf<K, V, Hash, Eq, typename Impl::alloc_t, Create>(
      b.hash(),
      b.equality(),
      std::forward<CreateArg>(create)),
    Impl(b, alloc)
  {}

  ~wrapper() noexcept {}

  auto get_if_present(const key_type& k)
  -> pointer
  override {
    return this->lookup_if_present(
        this->hash(k),
        this->template bind_eq_<store_type>(k));
  }

  auto get(const key_type& k)
  -> pointer
  override {
    return this->lookup_or_create(
        make_cache_query(
            this->hash(k),
            this->template bind_eq_<store_type>(k),
            this->bind_tpl_(k),
            this->bind_create_(k)));
  }

  auto get(key_type&& k)
  -> pointer
  override {
    return this->lookup_or_create(
        make_cache_query(
            this->hash(std::as_const(k)),
            this->template bind_eq_<store_type>(k),
            this->bind_tpl_(k),
            this->bind_create_(std::move(k))));
  }

  auto get(const typename wrapper::extended_query_type& q)
  -> pointer
  override {
    return this->lookup_or_create(
        make_cache_query(
            q.hash_code,
            this->template wrap_ext_predicate_<store_type>(q.predicate),
            q.tpl_builder,
            q.create));
  }
};


template<typename K, typename V, typename Impl, typename Hash, typename Eq, typename Alloc, typename Create>
class sharded_wrapper final
: public extended_cache_intf<K, V, Hash, Eq, typename Impl::alloc_t, Create>
{
 public:
  using key_type = typename extended_cache_intf<K, V, Hash, Eq, typename Impl::alloc_t, Create>::key_type;
  using pointer = typename extended_cache_intf<K, V, Hash, Eq, typename Impl::alloc_t, Create>::pointer;
  using store_type = typename Impl::store_type;
  static constexpr std::size_t hash_multiplier =
      (sizeof(std::size_t) <= 4
       ? 0x1001000fU // 257 * 1044751
       : 0x100010000001000fULL); // 3 * 3 * 18311 * 6996032116657

  sharded_wrapper(const sharded_wrapper&) = delete;
  sharded_wrapper(sharded_wrapper&&) = delete;
  sharded_wrapper& operator=(const sharded_wrapper&) = delete;
  sharded_wrapper& operator=(sharded_wrapper&&) = delete;

  template<typename CreateArg>
  sharded_wrapper(cache_builder<K, V, Hash, Eq, Alloc> b,
      unsigned int shards,
      CreateArg&& create,
      Alloc alloc)
  : extended_cache_intf<K, V, Hash, Eq, typename Impl::alloc_t, Create>(
      b.hash(),
      b.equality(),
      std::forward<CreateArg>(create)),
    alloc_(alloc)
  {
    assert(shards > 1);
    using traits = typename std::allocator_traits<Alloc>::template rebind_traits<Impl>;
    using impl_alloc_t = typename std::allocator_traits<Alloc>::template rebind_alloc<Impl>;

    // We grab a copy of the cache builder, so we can adjust parameters based
    // on the number of shards.
    if (b.max_memory().has_value())
      b.max_memory(*b.max_memory() / shards);
    if (b.max_size().has_value())
      b.max_size(*b.max_size() / shards);

    impl_alloc_t impl_alloc = alloc_;
    shards_ = traits::allocate(impl_alloc, shards);
    try {
      // Shard overhead is billed on shard_[0].
      traits::construct(impl_alloc, shards_ + num_shards_, b, alloc);
      ++num_shards_;

      while (num_shards_ < shards) {
        impl_alloc_t alloc_copy = impl_alloc;
        auto mem_tracking = create_mem_tracking(alloc_copy);
        traits::construct(impl_alloc, shards_ + num_shards_, b, alloc_copy);
        if constexpr(has_set_mem_use<Impl>)
          shards_[num_shards_].set_mem_use(mem_tracking);
        ++num_shards_;
      }
    } catch (...) {
      while (num_shards_ > 0) {
        --num_shards_;
        traits::destroy(impl_alloc, shards_ + num_shards_);
      }
      traits::deallocate(impl_alloc, shards_, shards);
      throw;
    }
  }

  ~sharded_wrapper() noexcept {
    using traits = typename std::allocator_traits<Alloc>::template rebind_traits<Impl>;

    unsigned int shards = num_shards_;
    while (num_shards_ > 0) {
      --num_shards_;
      traits::destroy(alloc_, shards_ + num_shards_);
    }
    traits::deallocate(alloc_, shards_, shards);
  }

  template<bool Enable = has_set_mem_use<Impl>>
  auto set_mem_use(std::shared_ptr<mem_use>&& mptr)
  noexcept
  -> std::enable_if_t<Enable> {
    shards_[0].set_mem_use(std::move(mptr));
  }

  auto get_if_present(const key_type& k)
  -> pointer
  override {
    std::size_t hash_code = this->hash(k);

    return shards_[hash_multiplier * hash_code % num_shards_].lookup_if_present(
        hash_code,
        this->template bind_eq_<store_type>(k));
  }

  auto get(const key_type& k)
  -> pointer
  override {
    std::size_t hash_code = this->hash(k);
    return shards_[hash_multiplier * hash_code % num_shards_].lookup_or_create(
        make_cache_query(
            hash_code,
            this->template bind_eq_<store_type>(k),
            this->bind_tpl_(k),
            this->bind_create_(k)));
  }

  auto get(key_type&& k)
  -> pointer
  override {
    std::size_t hash_code = this->hash(std::as_const(k));
    return shards_[hash_multiplier * hash_code % num_shards_].lookup_or_create(
        make_cache_query(
            hash_code,
            this->template bind_eq_<store_type>(k),
            this->bind_tpl_(k),
            this->bind_create_(std::move(k))));
  }

  auto get(const typename sharded_wrapper::extended_query_type& q)
  -> pointer
  override {
    return shards_[hash_multiplier * q.hash_code % num_shards_].lookup_or_create(
        make_cache_query(
            q.hash_code,
            this->template wrap_ext_predicate_<store_type>(q.predicate),
            q.tpl_builder,
            q.create));
  }

 private:
  typename std::allocator_traits<Alloc>::template rebind_alloc<Impl> alloc_;
  Impl* shards_ = nullptr;
  unsigned int num_shards_ = 0;
};


} /* namespace monsoon::cache::builder_detail */


template<typename K, typename V, typename Hash, typename Eq, typename Alloc>
template<typename Fn>
auto cache_builder<K, V, Hash, Eq, Alloc>::build(Fn&& fn) const
-> extended_cache<K, V, Hash, Eq, Alloc, std::decay_t<Fn>> {
  using namespace builder_detail;

  auto alloc = allocator();
  std::shared_ptr<mem_use> mem_tracking = create_mem_tracking(alloc);

  const unsigned int shards = (!thread_safe()
      ? 0u
      : (concurrency() == 0u ? std::thread::hardware_concurrency() : concurrency()));

  auto builder_impl =
      apply_async(*this,
          apply_max_age(*this,
              apply_access_expire(*this,
                  apply_key_type(*this,
                      apply_thread_safe(*this,
                          apply_max_size(*this,
                              apply_max_mem(*this,
                                  [this, &fn, shards, &alloc, &mem_tracking](auto decorators) -> std::shared_ptr<extended_cache_intf<K, V, Hash, Eq, Alloc, std::decay_t<Fn>>> {
                                    using basic_type = typename decltype(decorators)::template cache_type<V, Alloc>;
                                    using wrapper_type = wrapper<K, V, basic_type, Hash, Eq, std::decay_t<Fn>>;
                                    using sharded_wrapper_type = sharded_wrapper<K, V, basic_type, Hash, Eq, Alloc, std::decay_t<Fn>>;

                                    if (shards > 1u) {
                                      auto impl = std::allocate_shared<sharded_wrapper_type>(
                                          alloc,
                                          *this, shards, std::forward<Fn>(fn), alloc);
                                      if constexpr(has_set_mem_use<basic_type>)
                                        impl->set_mem_use(std::move(mem_tracking));
                                      return impl;
                                    } else {
                                      auto impl = std::allocate_shared<wrapper_type>(
                                          alloc,
                                          *this, std::forward<Fn>(fn), alloc);
                                      if constexpr(has_set_mem_use<basic_type>)
                                        impl->set_mem_use(std::move(mem_tracking));
                                      return impl;
                                    }
                                  })))))));

  return extended_cache<K, V, Hash, Eq, Alloc, std::decay_t<Fn>>(
      builder_impl(cache_decorator_set<>().template add<weaken_decorator>()));
}


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_IMPL_H */
