#ifndef MONSOON_CACHE_IMPL_H
#define MONSOON_CACHE_IMPL_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <monsoon/cache/builder.h>
#include <monsoon/cache/cache_impl.h>
#include <monsoon/cache/access_expire_decorator.h>
#include <monsoon/cache/expire_queue.h>
#include <monsoon/cache/max_age_decorator.h>
#include <monsoon/cache/max_size_decorator.h>
#include <monsoon/cache/mem_use.h>
#include <monsoon/cache/stats.h>
#include <monsoon/cache/thread_safe_decorator.h>
#include <monsoon/cache/weaken_decorator.h>
#include <monsoon/cache/element.h>
#include <instrumentation/gauge.h>
#include <instrumentation/engine.h>
#include <instrumentation/path.h>
#include <instrumentation/tags.h>

namespace monsoon::cache::builder_detail {


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


template<typename Cache, typename = void>
struct has_set_mem_use_
: std::false_type {};

template<typename Cache>
struct has_set_mem_use_<Cache, std::void_t<decltype(std::declval<Cache&>().set_mem_use(std::declval<std::shared_ptr<const mem_use>>()))>>
: std::true_type {};

template<typename Cache>
constexpr bool has_set_mem_use = has_set_mem_use_<Cache>::value;


template<typename CDS, typename T, typename... D>
struct add_all_except_;

template<typename CDS, typename T>
struct add_all_except_<CDS, T> {
  using type = CDS;
};

template<typename CDS, typename T, typename D0, typename... D>
struct add_all_except_<CDS, T, D0, D...> {
  using type = typename std::conditional_t<
      std::is_same_v<T, D0>,
      add_all_except_<CDS, T, D...>,
      add_all_except_<typename CDS::template add<D0>, T, D...>>::type;
};

///\brief List of decorators.
template<typename... T> struct decorator_list;

template<typename CDS, typename List> struct add_all_;

template<typename CDS>
struct add_all_<CDS, decorator_list<>> {
  using type = CDS;
};

template<typename CDS, typename T0, typename... T>
struct add_all_<CDS, decorator_list<T0, T...>>
: add_all_<typename CDS::template add<T0>, decorator_list<T...>>
{};

template<typename CDS, typename List> struct remove_all_;

template<typename CDS>
struct remove_all_<CDS, decorator_list<>> {
  using type = CDS;
};

template<typename CDS, typename T0, typename... T>
struct remove_all_<CDS, decorator_list<T0, T...>>
: remove_all_<typename CDS::template remove<T0>, decorator_list<T...>>
{};

///\brief Keep track of all decorators that are to be applied to the cache.
///\ingroup cache_detail
///\tparam D All decorators that are to be passed to the cache.
template<typename... D>
struct cache_decorator_set {
  ///\brief Specialize the type of the cache.
  ///\tparam Alloc The allocator for the cache.
  template<typename Alloc>
  using cache_type = cache_impl<Alloc, D...>;

  ///\brief Add type T to the decorator set.
  ///\details Does nothing if T is already part of the decorator set, or void.
  ///\returns A decorator set with all decorators in this, and with those in L added.
  ///\tparam T The decorator to add to the set.
  template<typename T>
  using add = std::conditional_t<
      std::disjunction_v<std::is_void<T>, std::is_same<D, T>...>,
      cache_decorator_set<D...>,
      cache_decorator_set<D..., T>>;

  ///\brief Add a list of types to the decorator set.
  ///\details Void decorators are ignored.
  ///\returns A decorator set with all decorators in this, and with T.
  ///\tparam L The decorator list to add to the set.
  template<typename L>
  using add_all = typename add_all_<cache_decorator_set, L>::type;

  ///\brief Remove type T from the decorator set.
  ///\details Does nothing if T is not part of the decorator set.
  ///\returns A decorator set with all decorators in this, except T.
  ///\tparam T The decorator to remove from the set.
  template<typename T>
  using remove = typename add_all_except_<cache_decorator_set<>, T, D...>::type;

  ///\brief Remove a list of types from the decorator set.
  ///\details If a decorator isn't part of the set (or is the void type), it is ignored.
  ///\returns A decorator set with all decorators in this, except those in L.
  ///\tparam L The decorator list to add to the set.
  template<typename L>
  using remove_all = typename remove_all_<cache_decorator_set, L>::type;
};


template<typename CDS, typename... Apply> struct make_cache_decorator_set_;

template<typename CDS>
struct make_cache_decorator_set_<CDS> {
  using type = CDS;
};

template<typename CDS, typename Apply0, typename... Apply>
struct make_cache_decorator_set_<CDS, Apply0, Apply...>
: make_cache_decorator_set_<
    typename CDS::template remove_all<typename Apply0::remove>::template add_all<typename Apply0::add>,
    Apply...>
{};

///\brief Take multiple decorators and add them to the decorator set.
///\note It's fine if the decorator is the void type.
template<typename... Apply>
using make_cache_decorator_set = typename make_cache_decorator_set_<cache_decorator_set<>, Apply...>::type;


/*
 * Decorator selectors start here.
 * These select a decorator based on the builder configuration.
 */


template<typename Builder, typename = void> struct apply_stats_;

template<typename Builder>
struct apply_stats_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::stats_var<true>, Builder>>> {
  using add = decorator_list<stats_decorator>;
  using remove = decorator_list<>;
};

template<typename Builder>
struct apply_stats_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::stats_var<false>, Builder>>> {
  using add = decorator_list<>;
  using remove = decorator_list<>;
};

///\brief Stats decorator selection.
template<typename Builder>
using apply_stats_t = apply_stats_<Builder>;


template<typename Builder, typename = void> struct apply_thread_safe_;

template<typename Builder>
struct apply_thread_safe_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::thread_safe_var<true>, Builder>>> {
  using add = decorator_list<thread_safe_decorator<true>>;
  using remove = decorator_list<>;
};

template<typename Builder>
struct apply_thread_safe_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::thread_safe_var<false>, Builder>>> {
  using add = decorator_list<thread_safe_decorator<false>>;
  using remove = decorator_list<>;
};

///\brief Thread-safe decorator selection.
template<typename Builder>
using apply_thread_safe_t = apply_thread_safe_<Builder>;


template<typename Builder>
struct key_type_selector_ {
  private:
  template<typename Type>
  static auto op_(const builder_vars_::key_var<Type>&) -> builder_vars_::key_var<Type>;

  public:
  using type = decltype(op_(std::declval<const Builder&>()));
};

template<typename T>
struct make_cache_key_decorator_ {
  using type = cache_key_decorator<T>;
};
template<>
struct make_cache_key_decorator_<void> {
  using type = void;
};

template<typename Builder, typename = void> struct apply_key_type_;

template<typename Builder>
struct apply_key_type_<Builder, std::void_t<typename key_type_selector_<Builder>::type>> {
  using key_var = typename key_type_selector_<Builder>::type;
  using add = decorator_list<typename make_cache_key_decorator_<typename key_var::key_or_void_type>::type>;
  using remove = decorator_list<>;
};

///\brief Key-type decorator selection.
template<typename Builder>
using apply_key_type_t = apply_key_type_<Builder>;


template<typename Builder, typename = void> struct apply_access_expire_;

template<typename Builder>
struct apply_access_expire_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::access_expire_var<true>, Builder>>> {
  using add = decorator_list<cache_expire_queue_decorator, access_expire_decorator>;
  using remove = decorator_list<weaken_decorator>;
};

template<typename Builder>
struct apply_access_expire_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::access_expire_var<false>, Builder>>> {
  using add = decorator_list<>;
  using remove = decorator_list<>;
};

///\brief Access-expire decorator selection.
template<typename Builder>
using apply_access_expire_t = apply_access_expire_<Builder>;


template<typename Builder, typename = void> struct apply_max_age_;

template<typename Builder>
struct apply_max_age_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::max_age_var<true>, Builder>>> {
  using add = decorator_list<max_age_decorator>;
  using remove = decorator_list<>;
};

template<typename Builder>
struct apply_max_age_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::max_age_var<false>, Builder>>> {
  using add = decorator_list<>;
  using remove = decorator_list<>;
};

///\brief Max-age decorator selection.
template<typename Builder>
using apply_max_age_t = apply_max_age_<Builder>;


template<typename Builder, typename = void> struct apply_async_;

template<typename Builder>
struct apply_async_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::async_var<true>, Builder>>> {
  static_assert(!std::is_void_v<typename Builder::key_or_void_type>,
      "identity cache can not be async");

  using add = decorator_list<cache_async_decorator>;
  using remove = decorator_list<>;
};

template<typename Builder>
struct apply_async_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::async_var<false>, Builder>>> {
  using add = decorator_list<>;
  using remove = decorator_list<>;
};

///\brief Max-age decorator selection.
template<typename Builder>
using apply_async_t = apply_async_<Builder>;


template<typename Builder, typename = void> struct apply_max_size_;

template<typename Builder>
struct apply_max_size_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::max_size_var<true>, Builder>>> {
  using add = decorator_list<cache_expire_queue_decorator, max_size_decorator>;
  using remove = decorator_list<weaken_decorator>;
};

template<typename Builder>
struct apply_max_size_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::max_size_var<false>, Builder>>> {
  using add = decorator_list<>;
  using remove = decorator_list<>;
};

///\brief Max-size decorator selection.
template<typename Builder>
using apply_max_size_t = apply_max_size_<Builder>;


template<typename Builder, typename = void> struct apply_max_mem_;

template<typename Builder>
struct apply_max_mem_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::max_memory_var<true>, Builder>>> {
  using add = decorator_list<cache_expire_queue_decorator, cache_max_mem_decorator>;
  using remove = decorator_list<weaken_decorator>;
};

template<typename Builder>
struct apply_max_mem_<Builder, std::enable_if_t<std::is_base_of_v<builder_vars_::max_memory_var<false>, Builder>>> {
  using add = decorator_list<>;
  using remove = decorator_list<>;
};

///\brief Max-memory decorator selection.
template<typename Builder>
using apply_max_mem_t = apply_max_mem_<Builder>;


template<typename Builder>
struct storage_override_selector_ {
  private:
  template<typename StoragePtr, typename StoragePtrCArgs, typename StoragePtrDeref>
  static auto op_(const builder_vars_::storage_override_var<StoragePtr, StoragePtrCArgs, StoragePtrDeref>&) -> builder_vars_::storage_override_var<StoragePtr, StoragePtrCArgs, StoragePtrDeref>;

  public:
  using type = decltype(op_(std::declval<const Builder&>()));
};

template<typename T, typename VPtr, typename StoragePtr = typename T::storage_pointer, typename StoragePtrCArgs = typename T::storage_pointer_ctor_args, typename StoragePtrDeref = typename T::storage_pointer_deref>
struct make_storage_override_decorator_ {
  using type = storage_pointer_decorator<VPtr, StoragePtr, StoragePtrCArgs, StoragePtrDeref>;
};

template<typename T, typename VPtr>
struct make_storage_override_decorator_<T, VPtr, void, void, void> {
  using type = default_storage_pointer_decorator<VPtr>;
};

template<typename Builder, typename = void> struct apply_storage_override_;

template<typename Builder>
struct apply_storage_override_<Builder, std::void_t<typename storage_override_selector_<Builder>::type>> {
  using var = typename storage_override_selector_<Builder>::type;
  using add = decorator_list<typename make_storage_override_decorator_<var, typename Builder::pointer>::type>;
  using remove = decorator_list<>;
};

template<typename Builder>
using apply_storage_override_t = apply_storage_override_<Builder>;


///\brief Weaken decorator.
///\note This type is unconditionally added at the start and
///removed if/when appropriate by other appliers.
struct apply_weaken_decorator_t {
  using add = decorator_list<weaken_decorator>;
  using remove = decorator_list<>;
};


template<typename Impl, typename = void>
class stats_impl {
 public:
  stats_impl([[maybe_unused]] const builder_vars_::stats_var<false>& vars) noexcept {}

  auto set_stats([[maybe_unused]] Impl& impl)
  noexcept
  -> void {
    /* SKIP */
  }

  auto add_mem_use(const std::shared_ptr<const mem_use>& mptr)
  noexcept
  -> void {
    /* SKIP */
  }
};

template<typename Impl>
class stats_impl<Impl, std::enable_if_t<std::is_base_of_v<stats_decorator, Impl>>>
: public stats_record
{
 public:
  stats_impl(const builder_vars_::stats_var<true>& vars)
  : stats_record(vars),
    mem_use_gauge_(
        instrumentation::engine::global().new_gauge_cb(
            instrumentation::path("monsoon.cache.memory"),
            instrumentation::tags().with("name", vars.stats()->name),
            [weak_vector=std::weak_ptr<const std::vector<std::shared_ptr<const mem_use>>>(this->mem_use_)]() -> std::int64_t {
              auto vector = weak_vector.lock();
              if (vector != nullptr) return compute_mem_use_(*vector);
              return 0;
            }
        )
    )
  {}

  stats_impl(const stats_impl&) = delete;

  auto set_stats(stats_decorator& impl)
  noexcept
  -> void {
    impl.set_stats_record(this);
  }

  auto add_mem_use(const std::shared_ptr<const mem_use>& mptr)
  -> void {
    mem_use_->push_back(mptr);
  }

 private:
  static auto compute_mem_use_(const std::vector<std::shared_ptr<const mem_use>>& mem_use_vector)
  noexcept
  -> std::int64_t {
    std::uintptr_t sigma = 0;
    for (const auto& mptr : mem_use_vector)
      sigma += mptr->get();
    if (sigma > std::numeric_limits<std::int64_t>::max())
      sigma = std::numeric_limits<std::int64_t>::max();
    return std::int64_t(sigma);
  }

  std::shared_ptr<std::vector<std::shared_ptr<const mem_use>>> mem_use_ = std::make_shared<std::vector<std::shared_ptr<const mem_use>>>();
  std::shared_ptr<void> mem_use_gauge_;
};


} /* namespace monsoon::cache::builder_detail::<unnamed> */


template<typename K, typename VPtr, typename Impl, typename Hash, typename Eq, typename Create>
class wrapper final
: public extended_cache_intf<K, VPtr, Hash, Eq, typename Impl::alloc_t, Create>,
  public stats_impl<Impl>,
  public Impl
{
 public:
  using key_type = typename extended_cache_intf<K, VPtr, Hash, Eq, typename Impl::alloc_t, Create>::key_type;
  using pointer = typename extended_cache_intf<K, VPtr, Hash, Eq, typename Impl::alloc_t, Create>::pointer;
  using store_type = typename Impl::store_type;

  template<typename... Vars, typename CreateArg>
  wrapper(const builder_vars_::builder_impl<Vars...>& b,
      CreateArg&& create,
      typename Impl::alloc_t alloc)
  : extended_cache_intf<K, VPtr, Hash, Eq, typename Impl::alloc_t, Create>(
      b.hash(),
      b.equality(),
      std::forward<CreateArg>(create)),
    stats_impl<Impl>(b),
    Impl(b, alloc)
  {
    this->stats_impl<Impl>::set_stats(*this);
  }

  ~wrapper() noexcept {}

  auto set_mem_use(std::shared_ptr<mem_use>&& mptr)
  noexcept
  -> void {
    this->stats_impl<Impl>::add_mem_use(mptr);
    if constexpr(has_set_mem_use<Impl>)
      this->Impl::set_mem_use(std::move(mptr));
  }

  auto expire(const key_type& k)
  -> bool
  override {
    return this->Impl::expire(
        this->hash(k),
        this->template bind_eq_<store_type>(k));
  }

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


template<typename K, typename VPtr, typename Impl, typename Hash, typename Eq, typename Alloc, typename Create>
class sharded_wrapper final
: public extended_cache_intf<K, VPtr, Hash, Eq, typename Impl::alloc_t, Create>,
  public stats_impl<Impl>
{
 public:
  using key_type = typename extended_cache_intf<K, VPtr, Hash, Eq, typename Impl::alloc_t, Create>::key_type;
  using pointer = typename extended_cache_intf<K, VPtr, Hash, Eq, typename Impl::alloc_t, Create>::pointer;
  using store_type = typename Impl::store_type;
  static constexpr std::size_t hash_multiplier =
      (sizeof(std::size_t) <= 4
       ? 0x1001000fU // 257 * 1044751
       : 0x100010000001000fULL); // 3 * 3 * 18311 * 6996032116657

  sharded_wrapper(const sharded_wrapper&) = delete;
  sharded_wrapper(sharded_wrapper&&) = delete;
  sharded_wrapper& operator=(const sharded_wrapper&) = delete;
  sharded_wrapper& operator=(sharded_wrapper&&) = delete;

  template<typename... Vars, typename CreateArg>
  sharded_wrapper(builder_vars_::builder_impl<Vars...> b,
      unsigned int shards,
      CreateArg&& create,
      Alloc alloc)
  : extended_cache_intf<K, VPtr, Hash, Eq, typename Impl::alloc_t, Create>(
      b.hash(),
      b.equality(),
      std::forward<CreateArg>(create)),
    stats_impl<Impl>(b),
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
        this->stats_impl<Impl>::add_mem_use(mem_tracking);
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

    for (auto* i = shards_; i != shards_ + num_shards_; ++i)
      this->stats_impl<Impl>::set_stats(*i);
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

  auto set_mem_use(std::shared_ptr<mem_use>&& mptr)
  noexcept
  -> void {
    this->stats_impl<Impl>::add_mem_use(mptr);
    if constexpr(has_set_mem_use<Impl>)
      shards_[0].set_mem_use(std::move(mptr));
  }

  auto expire(const key_type& k)
  -> bool
  override {
    std::size_t hash_code = this->hash(k);

    return shards_[hash_multiplier * hash_code % num_shards_].expire(
        hash_code,
        this->template bind_eq_<store_type>(k));
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


namespace monsoon::cache::builder_vars_ {


template<typename... Vars>
template<typename Fn>
auto builder_impl<Vars...>::build(Fn&& fn) const
-> extended_cache<
    typename builder_impl::key_or_void_type,
    typename builder_impl::mapped_type,
    typename builder_impl::hash_type,
    typename builder_impl::equality_type,
    typename builder_impl::allocator_type,
    std::decay_t<Fn>,
    typename builder_impl::pointer> {
  using namespace builder_detail;

  using extended_cache_type = extended_cache<
      typename builder_impl::key_or_void_type,
      typename builder_impl::mapped_type,
      typename builder_impl::hash_type,
      typename builder_impl::equality_type,
      typename builder_impl::allocator_type,
      std::decay_t<Fn>,
      typename builder_impl::pointer>;

  auto alloc = this->allocator();
  std::shared_ptr<mem_use> mem_tracking = create_mem_tracking(alloc);

  const unsigned int shards = (!this->thread_safe()
      ? 0u
      : (this->concurrency() == 0u ? std::thread::hardware_concurrency() : this->concurrency()));

  using decorator_set = make_cache_decorator_set<
      apply_weaken_decorator_t,
      apply_stats_t<builder_impl>,
      apply_async_t<builder_impl>,
      apply_max_age_t<builder_impl>,
      apply_access_expire_t<builder_impl>,
      apply_key_type_t<builder_impl>,
      apply_thread_safe_t<builder_impl>,
      apply_max_size_t<builder_impl>,
      apply_max_mem_t<builder_impl>,
      apply_storage_override_t<builder_impl>>;
  using basic_type = typename decorator_set::template cache_type<typename builder_impl::allocator_type>;
  using wrapper_type = wrapper<
      typename builder_impl::key_or_void_type,
      typename builder_impl::pointer,
      basic_type,
      typename builder_impl::hash_type,
      typename builder_impl::equality_type,
      std::decay_t<Fn>>;
  using sharded_wrapper_type = sharded_wrapper<
      typename builder_impl::key_or_void_type,
      typename builder_impl::pointer,
      basic_type,
      typename builder_impl::hash_type,
      typename builder_impl::equality_type,
      typename builder_impl::allocator_type,
      std::decay_t<Fn>>;

  if (shards > 1u) {
    auto impl = std::allocate_shared<sharded_wrapper_type>(
        alloc,
        *this, shards, std::forward<Fn>(fn), alloc);
    impl->set_mem_use(std::move(mem_tracking));
    return extended_cache_type(std::move(impl));
  } else {
    auto impl = std::allocate_shared<wrapper_type>(
        alloc,
        *this, std::forward<Fn>(fn), alloc);
    impl->set_mem_use(std::move(mem_tracking));
    return extended_cache_type(std::move(impl));
  }
};


} /* namespace monsoon::cache::builder_vars_ */

#endif /* MONSOON_CACHE_IMPL_H */
