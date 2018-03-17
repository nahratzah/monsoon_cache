#ifndef MONSOON_CACHE_SIMPLE_CACHE_IMPL_H
#define MONSOON_CACHE_SIMPLE_CACHE_IMPL_H

///\file
///\ingroup cache_detail

#include <monsoon/cache/bucket.h>
#include <monsoon/cache/cache.h>
#include <monsoon/cache/create_handler.h>
#include <monsoon/cache/key_decorator.h>
#include <monsoon/cache/cache_query.h>
#include <monsoon/cache/store_delete_lock.h>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace monsoon::cache {
namespace {


template<typename CacheDecorator, typename = void>
struct decorator_for_ {
  using type = void;
};

template<typename CacheDecorator>
struct decorator_for_<CacheDecorator, std::void_t<typename CacheDecorator::element_decorator_type>> {
  using type = typename CacheDecorator::element_decorator_type;
};


template<typename Elem, typename... Args> struct bucket_add_nonvoid_types_;

template<typename... ElemArgs, typename Arg0, typename... Args>
struct bucket_add_nonvoid_types_<bucket<ElemArgs...>, Arg0, Args...> {
  using type = typename bucket_add_nonvoid_types_<bucket<ElemArgs..., Arg0>, Args...>::type;
};

template<typename... ElemArgs, typename... Args>
struct bucket_add_nonvoid_types_<bucket<ElemArgs...>, void, Args...> {
  using type = typename bucket_add_nonvoid_types_<bucket<ElemArgs...>, Args...>::type;
};

template<typename... ElemArgs>
struct bucket_add_nonvoid_types_<bucket<ElemArgs...>> {
  using type = bucket<ElemArgs...>;
};


template<typename Bucket, typename... CacheDecorators>
struct decorate_bucket_ {
  using type = typename bucket_add_nonvoid_types_<Bucket, typename decorator_for_<CacheDecorators>::type...>::type;
};

template<typename Elem, typename... CacheDecorators>
using decorate_bucket_t = typename decorate_bucket_<Elem, CacheDecorators...>::type;


template<typename S, typename D, typename = void>
struct decorator_on_create_ {
  static auto apply(S& s, D& d) {}
};

template<typename S, typename D>
struct decorator_on_create_<S, D, std::void_t<decltype(std::declval<D&>().on_create(std::declval<S&>()))>> {
  static_assert(noexcept(std::declval<D&>().on_create(std::declval<S&>())),
      "on_create must be a noexcept function.");
  static auto apply(S& s, D& d) { d.on_create(s); }
};

template<typename S, typename... D>
struct decorators_on_create_;

template<typename S>
struct decorators_on_create_<S> {
  template<typename T>
  static auto apply(S& s, T& v) {}
};

template<typename S, typename D0, typename... D>
struct decorators_on_create_<S, D0, D...> {
  template<typename T>
  static auto apply(S& s, T& v) {
    decorator_on_create_<S, D0>::apply(s, v);
    decorators_on_create_<S, D...>::apply(s, v);
  }
};


template<typename S, typename D, typename = void>
struct decorator_on_delete_ {
  static auto apply(S& s, D& d) {}
};

template<typename S, typename D>
struct decorator_on_delete_<S, D, std::void_t<decltype(std::declval<D&>().on_delete(std::declval<S&>()))>> {
  static_assert(noexcept(std::declval<D&>().on_delete(std::declval<S&>())),
      "on_create must be a noexcept function.");
  static auto apply(S& s, D& d) { d.on_delete(s); }
};

template<typename S, typename... D>
struct decorators_on_delete_;

template<typename S>
struct decorators_on_delete_<S> {
  template<typename T>
  static auto apply(S& s, T& v) {}
};

template<typename S, typename D0, typename... D>
struct decorators_on_delete_<S, D0, D...> {
  template<typename T>
  static auto apply(S& s, T& v) {
    decorator_on_delete_<S, D0>::apply(s, v);
    decorators_on_delete_<S, D...>::apply(s, v);
  }
};


template<typename S, typename D, typename = void>
struct decorator_on_hit_ {
  static auto apply(S& s, D& d) -> void {}
};

template<typename S, typename D>
struct decorator_on_hit_<S, D, std::void_t<decltype(std::declval<D&>().on_hit(std::declval<S&>()))>> {
  static_assert(noexcept(std::declval<D&>().on_hit(std::declval<S&>())),
      "on_hit must be a noexcept function.");
  static auto apply(S& s, D& d) -> void { d.on_hit(s); }
};

template<typename S, typename... D>
struct decorators_on_hit_;

template<typename S>
struct decorators_on_hit_<S> {
  template<typename T>
  static auto apply(S& s, T& v) noexcept {}
};

template<typename S, typename D0, typename... D>
struct decorators_on_hit_<S, D0, D...> {
  template<typename T>
  static auto apply(S& s, T& v) noexcept -> void {
    decorator_on_hit_<S, D0>::apply(s, v);
    decorators_on_hit_<S, D...>::apply(s, v);
  }
};


template<typename D, typename = void>
struct cache_decorator_tpl_ {
  static auto apply(D& d) -> decltype(auto) {
    return std::tuple<>();
  }
};

template<typename D>
struct cache_decorator_tpl_<D, std::void_t<decltype(std::declval<D&>().init_tuple())>> {
  static auto apply(D& d) -> decltype(auto) {
    return d.init_tuple();
  }
};


template<typename D, typename ImplType, typename = void>
struct select_decorator_type_ {
  using type = D;
};

template<typename D, typename ImplType>
struct select_decorator_type_<D, ImplType, std::void_t<typename D::template for_impl_type<ImplType>>> {
  using type = typename D::template for_impl_type<ImplType>;
};

template<typename D, typename ImplType>
using select_decorator_type = typename select_decorator_type_<D, ImplType>::type;


} /* namespace monsoon::cache::<unnamed> */

/**
 * \brief Implementation of all the internal cache logic.
 * \ingroup cache_detail
 * \details This class combines all decorators and performs lookups,
 * as well as cache maintenance tasks.
 *
 * \msc
 * wrapper, cache_impl, bucket, decorator[ label="cache decorators" ];
 *
 * wrapper => cache_impl [ label="lookup_or_create()", URL="\ref cache_impl::lookup_or_create" ] ;
 * cache_impl -> cache_impl [ label="lock()" ] ;
 * cache_impl box cache_impl [ label="select bucket" ] ;
 * cache_impl => bucket [ label="lookup_or_create()", URL="\ref bucket::lookup_or_create" ] ;
 * bucket box bucket [ label="search for element" ] ;
 * --- [ label="remove expired elements during search" ] ;
 * bucket =>> cache_impl [ label="on_delete(expired)", URL="\ref cache_impl::on_delete" ] ;
 * cache_impl => decorator [ label="on_delete(expired)" ];
 * --- [ label="cache hit" ] ;
 * bucket =>> cache_impl [ label="on_hit(element)", URL="\ref cache_impl::on_hit" ] ;
 * cache_impl => decorator [ label="on_hit(element)" ] ;
 * bucket >> cache_impl [ label="return element" ] ;
 * cache_impl -> cache_impl [ label="unlock()" ] ;
 * cache_impl note cache_impl [ label="Only in async case: future.get()\n(unlocked)" ] ;
 * cache_impl >> wrapper [ label="return value" ] ;
 * --- [ label="cache miss" ] ;
 * bucket => decorator [ label="init_tuple()" ] ;
 * bucket box bucket [ label="create new element (using init_tuple() from all decorators)" ] ;
 * bucket >> cache_impl [ label="return new element" ] ;
 * cache_impl note cache_impl [ label="Only in async case: resolve future\n(unlocked)", URL="\ref cache_impl::resolve_" ] ;
 * cache_impl =>> cache_impl [ label="on_create(element)", URL="\ref cache_impl::on_create" ] ;
 * cache_impl => decorator [ label="on_create(element)" ] ;
 * cache_impl box cache_impl [ label="rehash", URL="\ref cache_impl::maybe_rehash_" ] ;
 * cache_impl -> cache_impl [ label="unlock()" ] ;
 * cache_impl >> wrapper [ label="return value" ] ;
 * \endmsc
 *
 * \tparam T The type of elements in the cache.
 * \tparam Alloc The allocator used for the cache data and elements.
 * \tparam CacheDecorators Decorators that affect the behaviour of the cache.
 */
template<typename T, typename Alloc, typename... CacheDecorators>
class cache_impl
: public select_decorator_type<CacheDecorators, cache_impl<T, Alloc, CacheDecorators...>>...
{
  template<typename, typename...> friend class bucket;

 public:
  ///\brief Size type of the cache.
  using size_type = std::uintptr_t;

 private:
  ///\brief Implementation type of the bucket.
  using bucket_type = decorate_bucket_t<bucket<T>, select_decorator_type<CacheDecorators, cache_impl>...>;
  ///\brief Vector of buckets.
  using bucket_vector = std::vector<bucket_type, typename std::allocator_traits<Alloc>::template rebind_alloc<bucket_type>>;
  ///\brief Result type of bucket::lookup_or_create and bucket::lookup_if_present
  using lookup_type = typename bucket_type::lookup_type;

 public:
  ///\brief Allocator used by the cache.
  using alloc_t = Alloc; // Not allocator_type, to prevent accidents with std::uses_allocator.
  ///\brief Element type used by the cache.
  using store_type = typename bucket_type::store_type;
  ///\brief Pointer type generated by the cache.
  ///\details This is a std::shared_ptr<T>.
  using pointer = typename store_type::pointer;

 private:
  ///\brief Initial number of buckets.
  ///\note Must be at least 1.
  static constexpr unsigned int init_bucket_count = 3;
  ///\brief Divider in growth ratio.
  static constexpr unsigned int growth_div = 7;
  ///\brief Numerator in growth ratio.
  static constexpr unsigned int growth_mul = 9;

 public:
  ///\brief Constructor for the cache algorithms.
  ///\note \p alloc is passed in separately, so that the builder can decorate
  ///the allocator.
  template<typename Key, typename Hash, typename Eq>
  cache_impl(const cache_builder<Key, T, Hash, Eq, Alloc>& b, Alloc alloc);

  ///\brief Destructor.
  ~cache_impl() noexcept;

  ///\brief Get the current load factor.
  auto load_factor() const noexcept -> float;
  ///\brief Get the max load factor.
  ///\details If the cache exceeds this load factor, it will rehash.
  auto max_load_factor() const noexcept -> float;
  ///\brief Set the load factor.
  auto max_load_factor(float fl) -> void;
  ///\brief Returns the number of entries in the cache.
  ///\note This counts the number of store_type instances.
  ///Each store_type instance may, or may not, hold a value.
  auto size() const noexcept -> size_type;
  /**
   * \brief Remove s, if it is expired.
   * \details If s is expired and not locked against delete, it is removed.
   * \returns True if the element was removed, false otherwise.
   */
  auto erase_if_expired(store_type& s) noexcept -> bool;

  /**
   * \brief Find element if it is present in the cache.
   * \details Tries to find the element.
   *
   * Irrespective of wether the lookup succeeds or fails, the cache will not
   * expire entries or perform maintenance tasks.
   * Even a cache hit will not update on_hit callbacks (and thus not count as
   * a hit).
   *
   * \param[in] hash_code Hash code for the object to find.
   * \param[in] predicate Predicate matcher for the object to find.
   */
  template<typename Predicate>
  auto lookup_if_present(std::size_t hash_code, Predicate predicate) noexcept -> pointer;

  /**
   * \brief Find an element in the cache.
   * \details Tries to find the element.
   *
   * If the search is successful, the on_hit method on decorators will be
   * invoked, and the result is returned.
   *
   * If the search fails, a new element will be created.
   * The on_create method on decorators will be invoked, and the new value
   * returned.
   *
   * This function may cause cache maintenance, including deletion of
   * \ref element "store_type elements".
   */
  template<typename Predicate, typename TplBuilder, typename Create>
  auto lookup_or_create(const cache_query<Predicate, TplBuilder, Create>& q) -> pointer;

 private:
  ///\brief Adapter around create functor.
  ///\ingroup cache_detail
  ///\details Adapts a create functor from what cache_impl accepted, to what
  ///bucket can accept.
  template<typename CacheQuery> class create_fn;

  ///\brief Convenience method to make a create_fn.
  template<typename CacheQuery>
  auto make_create_fn(CacheQuery& q) noexcept -> create_fn<CacheQuery>;

  /**
   * \brief Handle resolution of shared future component of lookup type.
   * \details This function is a noop if the lookup_type doesn't have a shared future component.
   *
   * If the lookup type was based on a shared future,
   * \ref element::resolve() "created->resolve()" will be called to ensure
   * the \ref element "store type" is updated properly.
   * \note After this call, it is undefined wether lck may will be in a locked state, or unlocked state.
   * \param[in] lck The unique lock locking this.
   * \param[in] l The result of a lookup operation.
   * \param[created] A pointer to the created store_type. Nullptr if no store type was created.
   * \returns Pointer from the lookup type.
   */
  static auto resolve_(std::unique_lock<const cache_impl>& lck,
      lookup_type&& l,
      store_type* created = nullptr)
  -> pointer;

  ///\brief Compute number of target buckets that is needed.
  auto compute_target_buckets_() const noexcept -> typename bucket_vector::size_type;
  ///\brief Perform rehashing, if load factor constraint requires it.
  auto maybe_rehash_() noexcept -> void;

  ///\brief Invoke on_create method for each cache decorator that implements it.
  auto on_create(store_type& s) noexcept -> void;
  ///\brief Invoke on_hit method for each cache decorator that implements it.
  auto on_hit(store_type& s) noexcept -> void;
  ///\brief Invoke on_delete method for each cache decorator that implements it.
  auto on_delete(store_type& s) noexcept -> void;

  ///\brief List of the buckets.
  bucket_vector buckets_;
  ///\brief Allocator for cache elements.
  Alloc alloc_;
  ///\brief Load factor.
  float lf_ = 1.0; ///<\brief Target load factor.
  ///\brief Number of store_type instances in the cache, irrespective of their state/validity.
  size_type size_ = 0;
};


template<typename T, typename Alloc, typename... CacheDecorators>
template<typename CacheQuery>
class cache_impl<T, Alloc, CacheDecorators...>::create_fn {
 public:
  ///Rebind allocator traits to store_type.
  using alloc_traits = typename std::allocator_traits<alloc_t>::template rebind_traits<store_type>;
  ///Rebind allocator to store_type.
  using alloc_type = typename std::allocator_traits<alloc_t>::template rebind_alloc<store_type>;

  ///\brief Constructor.
  ///\param[in] owner The cache_impl invoking the create method.
  ///Used to populate initialization tuple.
  ///\param[in] q The lookup_or_create query.
  explicit create_fn(cache_impl& owner, CacheQuery& q) noexcept;

  ///\brief Create a new store type, based on the cache_impl and query.
  ///\details This method implements the 'create' portion of
  ///\ref cache_impl::lookup_or_create.
  ///\param[in] hint Allocation hint provided by the bucket. Points to the
  ///last store_type that is unlikely to go away soon.
  ///May be nullptr.
  auto operator()(void* hint) -> store_type*;

 private:
  ///\brief Create the initialization tuple for \ref element "store_type".
  auto build_init_()
  -> decltype(auto) {
    return std::tuple_cat(
        q.tpl_builder(),
        cache_decorator_tpl_<select_decorator_type<CacheDecorators, cache_impl>>::apply(owner)...);
  }

  ///\brief Exception recovery function, that prevents nested exceptions from
  ///throwing.
  ///\bug I'm not sure what to do if the recovery path throws.
  ///Maybe rethrow with nested?
  ///Either way, I don't think swallowing is right, so the function is
  ///currently noexcept, so that the compiler will inject an abort method.
  auto undo_allocation_(alloc_type& alloc, store_type* new_store)
  noexcept // Exception recovery path is not supposed to throw...
  -> void {
    alloc_traits::deallocate(alloc, new_store, 1);
  }

  ///\brief Cache implementation.
  cache_impl& owner;
  ///\brief Query for lookup_or_create.
  CacheQuery& q;
};


template<typename T, typename Alloc, typename... CacheDecorators>
template<typename Key, typename Hash, typename Eq>
cache_impl<T, Alloc, CacheDecorators...>::cache_impl(
    const cache_builder<Key, T, Hash, Eq, Alloc>& b,
    Alloc alloc)
: select_decorator_type<CacheDecorators, cache_impl>(b)...,
  buckets_(b.allocator()),
  alloc_(alloc),
  lf_(b.load_factor())
{
  buckets_.resize(init_bucket_count); // May not be zero, or we'll get (unchecked) division by zero.
}

template<typename T, typename Alloc, typename... CacheDecorators>
cache_impl<T, Alloc, CacheDecorators...>::~cache_impl() noexcept {
  for (auto& b : buckets_)
    b.erase_all([this](store_type& s) { on_delete(s); });
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::load_factor() const
noexcept
-> float {
  std::lock_guard<const cache_impl> lck{ *this };

  return std::double_t(size_) / std::double_t(buckets_.size());
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::max_load_factor() const
noexcept
-> float {
  std::lock_guard<const cache_impl> lck{ *this };

  return lf_;
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::max_load_factor(float lf)
-> void {
  std::lock_guard<const cache_impl> lck{ *this };

  if (lf <= 0.0 || !std::isfinite(lf))
    throw std::invalid_argument("invalid load factor");
  lf_ = lf;
  maybe_rehash_();
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::size() const noexcept
-> size_type {
  std::lock_guard<const cache_impl> lck{ *this };
  return size_;
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::erase_if_expired(store_type& s)
noexcept
-> bool {
  if (s.use_count.load(std::memory_order_acquire) == 0 && s.is_expired()) {
    buckets_[s.hash() % buckets_.size()].erase(
        &s,
        [this](store_type& s) { on_delete(s); });
    return true;
  } else {
    return false;
  }
}

template<typename T, typename A, typename... D>
template<typename Predicate>
auto cache_impl<T, A, D...>::lookup_if_present(
    std::size_t hash_code,
    Predicate predicate)
noexcept
-> pointer {
  // Acquire lock on the cache.
  // One of the decorators is to supply lock() and unlock() methods,
  // that can be called on a const-reference of this.
  std::unique_lock<const cache_impl> lck{ *this };

  // Execute query.
  assert(buckets_.size() > 0);
  return resolve_(lck, buckets_[hash_code % buckets_.size()]
      .lookup_if_present(hash_code, std::move(predicate)));
}

template<typename T, typename A, typename... D>
template<typename Predicate, typename TplBuilder, typename Create>
auto cache_impl<T, A, D...>::lookup_or_create(
    const cache_query<Predicate, TplBuilder, Create>& q)
-> pointer {
  // Acquire lock on the cache.
  // One of the decorators is to supply lock() and unlock() methods,
  // that can be called on a const-reference of this.
  std::unique_lock<const cache_impl> lck{ *this };

  // Execute query.
  store_delete_lock<store_type> created;
  assert(buckets_.size() > 0);
  lookup_type lookup_result = buckets_[q.hash_code % buckets_.size()]
      .lookup_or_create(*this, q.hash_code, std::ref(q.predicate), make_create_fn(q), created);
  assert(!store_type::is_nil(lookup_result));
  pointer result = resolve_(lck, std::move(lookup_result), created.get()); // lck may be unlocked
  assert(result != nullptr);

  // If newly created, call post processing hooks.
  if (created) {
    if (!lck.owns_lock()) lck.lock(); // Relock.
    maybe_rehash_();
  }

  return result;
}

template<typename T, typename A, typename... D>
template<typename CacheQuery>
auto cache_impl<T, A, D...>::make_create_fn(CacheQuery& q)
noexcept
-> create_fn<CacheQuery> {
  return create_fn<CacheQuery>(*this, q);
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::resolve_(
    std::unique_lock<const cache_impl>& lck,
    lookup_type&& l,
    store_type* created)
-> pointer {
  if constexpr(std::is_same_v<pointer, lookup_type>) {
    return std::move(l);
  } else {
    return std::visit(
        [&lck, created](auto&& arg) -> pointer {
          if constexpr(std::is_same_v<pointer, std::decay_t<decltype(arg)>>) {
            return std::move(arg);
          } else {
            std::shared_future<pointer> fut = std::move(arg);

            lck.unlock(); // Don't hold the lock during future resolution!
            if (created == nullptr)
              return fut.get();

            // Resolve future with the cache unlocked.
            // This allows:
            // - other threads to access the cache without blocking on
            //   our create function,
            // - the create function to recurse into the cache,
            // - the allocators involved in the create function,
            //   to perform cache maintenance routines.
            fut.wait();

            // Relock before calling resolve,
            // because element mutation is not thread safe.
            lck.lock();

            return created->resolve(); // Installs result of future.
          }
        },
        std::move(l));
  }
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::compute_target_buckets_() const
noexcept
-> typename bucket_vector::size_type {
  const std::double_t target_buckets_dbl = std::ceil(std::double_t(size_) / lf_);
  typename bucket_vector::size_type target_buckets = buckets_.max_size();
  if (target_buckets_dbl < target_buckets)
    target_buckets = target_buckets_dbl;
  return target_buckets;
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::maybe_rehash_()
noexcept // Allocation exception is swallowed.
-> void {
  // Check if we require rehashing.
  if (buckets_.size() >= compute_target_buckets_())
    return; // No rehash required.

  // Full maintenance: erasing all expired elements.
  for (auto& i : buckets_) i.erase_all_expired(*this);

  // Recheck if we require rehashing, as erase_all_expired() may have reduced number of elements.
  auto target_buckets = compute_target_buckets_();
  if (buckets_.size() >= target_buckets) return; // No rehash required.

  // Compute new number of buckets.
  {
    const auto b_div = buckets_.size() / growth_div + 1;
    const auto b_mul = b_div * growth_mul;
    if (b_mul / growth_mul != b_div) { // Overflow.
      target_buckets = buckets_.max_size();
    } else {
      target_buckets = std::min(b_mul, buckets_.max_size());
    }
    if (buckets_.size() >= target_buckets)
      return; // No rehash required.
  }

  // Perform rehashing operation.
  const typename bucket_vector::size_type orig_size = buckets_.size();
  try {
    buckets_.resize(target_buckets);
  } catch (...) {
    // Swallow exception.
    return;
  }

  {
    typename bucket_vector::size_type b_idx = 0;
    for (auto i = buckets_.begin(), i_end = buckets_.begin() + orig_size;
        i != i_end;
        ++i, ++b_idx) {
      i->rehash(
          [this](const std::size_t hash_code) -> bucket_type& {
            return buckets_[hash_code % buckets_.size()];
          });
    }
  }
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::on_create(store_type& s)
noexcept
-> void {
  ++size_;
  decorators_on_create_<store_type, select_decorator_type<D, cache_impl>...>::apply(s, *this);
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::on_hit(store_type& s)
noexcept
-> void {
  decorators_on_hit_<store_type, select_decorator_type<D, cache_impl>...>::apply(s, *this);
}

template<typename T, typename A, typename... D>
auto cache_impl<T, A, D...>::on_delete(store_type& s)
noexcept
-> void {
  --size_;
  decorators_on_delete_<store_type, select_decorator_type<D, cache_impl>...>::apply(s, *this);

  using alloc_traits = typename std::allocator_traits<alloc_t>::template rebind_traits<store_type>;
  typename std::allocator_traits<alloc_t>::template rebind_alloc<store_type> alloc = alloc_;
  alloc_traits::destroy(alloc, &s);
  alloc_traits::deallocate(alloc, &s, 1);
}


template<typename T, typename A, typename... D>
template<typename CacheQuery>
cache_impl<T, A, D...>::create_fn<CacheQuery>::create_fn(
    cache_impl& owner,
    CacheQuery& q)
noexcept
: owner(owner),
  q(q)
{}

template<typename T, typename A, typename... D>
template<typename CacheQuery>
auto cache_impl<T, A, D...>::create_fn<CacheQuery>::operator()(void* hint)
-> store_type* {
  // Wrap create in a handler that fixes the return type.
  auto ch = make_create_handler<store_type::is_async>(q.create);
  alloc_type alloc = owner.alloc_;

  // Allocate storage.
  store_type* new_store = alloc_traits::allocate(alloc, 1, hint);
  assert(new_store != nullptr); // We expect allocator to throw on failure.
  try {
    // Create init tuple in advance, so that create function is free to
    // move its arguments.
    auto init = build_init_();
    // Create element, invoking create function as one of its constructor
    // arguments.
    alloc_traits::construct(alloc, new_store,
        std::allocator_arg,
        alloc, ch(alloc),
        q.hash_code,
        std::move(init));
  } catch (...) {
    undo_allocation_(alloc, new_store);
    throw;
  }
  return new_store;
}


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_SIMPLE_CACHE_IMPL_H */
