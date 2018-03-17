#ifndef MONSOON_CACHE_ALLOCATOR_H
#define MONSOON_CACHE_ALLOCATOR_H

///\file
///\ingroup cache

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <scoped_allocator>
#include <type_traits>

namespace monsoon::cache {


template<typename Allocator, typename... Allocators> class cache_allocator;

/**
 * \brief Interface used by cache_allocator to record changes in memory usage.
 * \ingroup cache_detail
 */
class cache_alloc_dealloc_observer {
 public:
  ///\brief Inform cache of memory having been allocated.
  ///\param[in] n The number of items.
  ///\param[in] sz The size of items.
  virtual void add_mem_use(std::uintptr_t n, std::uintptr_t sz) noexcept = 0;
  ///\brief Inform cache of memory having been deallocated.
  ///\param[in] n The number of items.
  ///\param[in] sz The size of items.
  virtual void subtract_mem_use(std::uintptr_t n, std::uintptr_t sz) noexcept = 0;

  template<typename... Allocators>
  static void maybe_set_stats(
      cache_allocator<Allocators...>& ca,
      std::weak_ptr<cache_alloc_dealloc_observer> stats) noexcept {
    ca.stats(stats);
  }

  template<typename Outer, typename... Inner>
  static void maybe_set_stats(
      std::scoped_allocator_adaptor<Outer, Inner...>& sa,
      std::weak_ptr<cache_alloc_dealloc_observer> stats) noexcept {
    maybe_set_stats(sa.outer_allocator(), stats);
    if constexpr(sizeof...(Inner) > 0)
      maybe_set_stats(sa.inner_allocator(), stats);
  }

  template<typename Alloc>
  static void maybe_set_stats(
      Alloc& alloc,
      const std::weak_ptr<cache_alloc_dealloc_observer>& stats,
      ...) noexcept
  {
    static_assert(!std::is_const_v<Alloc> && !std::is_volatile_v<Alloc> && !std::is_reference_v<Alloc>,
        "Overload selected for wrong reasons: const/volatile/reference.");
  }
};


template<typename X, typename = void>
struct can_exhaust_outer_allocator_
: std::false_type
{
  using result_type = X;
};

template<typename X>
struct can_exhaust_outer_allocator_<X, std::void_t<decltype(std::declval<X>().outer_allocator())>>
: std::true_type
{
  using result_type = typename can_exhaust_outer_allocator_<decltype(std::declval<X>().outer_allocator())>::result_type;
};

///\brief Call outer_allocator on \p a, and recursively call it on its result.
///\ingroup cache_detail
///\param[in] a An allocator on which to resolve the outermost allocator.
///\returns The outermost allocator for \p a, by invoking outer_allocator().
///   If \p a does not have a method outer_allocator(), \p a is returned.
template<typename Alloc>
auto exhaust_outer_allocator_(Alloc& a)
-> typename can_exhaust_outer_allocator_<Alloc&>::result_type {
  if constexpr(can_exhaust_outer_allocator_<Alloc&>::value)
    return exhaust_outer_allocator_(a.outer_allocator());
  else
    return a;
}


///\brief Strip cache_allocator wrapping from tail allocators.
template<typename T>
struct tail_allocator_cleanup_ {
  using type = T;
};

///\brief Strip cache_allocator wrapping from tail allocators.
///\details Specialization for single nested allocator.
///\tparam T Head type of the cache allocator.
template<typename T>
struct tail_allocator_cleanup_<cache_allocator<T>> {
  using type = typename tail_allocator_cleanup_<T>::type;
};

///\brief Strip cache_allocator wrapping from tail allocators.
///\details Specialization for multiple wrapped allocators.
///We wrap the set of types into scoped_allocator_adaptor, as it is
///able to store multiple allocators with minimal overhead.
///\tparam T Types of allocators.
template<typename... T>
struct tail_allocator_cleanup_<cache_allocator<T...>> {
  using type = std::scoped_allocator_adaptor<typename tail_allocator_cleanup_<T>::type...>;
};


/**
 * \brief Inner allocator logic for cache_allocator.
 * \ingroup cache_detail
 * \tparam Self The cache allocator, for template recursion.
 * \tparam Tail Tail list of allocators.
 */
template<typename Self, typename... Tail>
class cache_alloc_tail {
 private:
  ///\brief Convenience type to keep track of tuple indices.
  using indices = std::index_sequence_for<Tail...>;

  ///\brief Initializing constructor.
  template<typename... TailArg,
      typename = std::enable_if_t<sizeof...(TailArg) == sizeof...(Tail)>>
  explicit cache_alloc_tail(TailArg&&... a)
  : tail_(std::forward<TailArg>(a)...)
  {}

  ///\brief Inner allocator type.
  using inner_allocator_type = cache_allocator<Tail...>;

  ///\brief Returns the inner allocator.
  ///\note Returns a copy, not a reference.
  ///\returns Cache allocator, created from tail elements.
  auto inner_allocator() const
  noexcept
  -> cache_allocator<Tail...> {
    return inner_allocator_(indices());
  }

 private:
  ///\brief Implementation of inner_allocator().
  ///\tparam Idx Indices for the tuple.
  ///\param indices Type tagging, to get \p Idx assigned.
  template<std::size_t... Idx>
  auto inner_allocator_([[maybe_unused]] std::index_sequence<Idx...> indices) const
  noexcept
  -> cache_allocator<Tail...> {
    // Construct cache allocator.
    cache_allocator<Tail...> result = cache_allocator<Tail...>(std::get<Idx>(tail_)...);
    // Ensure stats are shared.
    result.stats(static_cast<const Self&>(*this).stats());
    return result;
  }

  std::tuple<typename tail_allocator_cleanup_<Tail>::type...> tail_;
};

/**
 * \brief Inner allocator logic for cache_allocator.
 * \ingroup cache_detail
 * \details This specialization handles the case where not tail allocators are present.
 * \tparam Self The cache allocator, for template recursion.
 */
template<typename Self>
class cache_alloc_tail<Self> {
 public:
  ///\brief Inner allocator type.
  using inner_allocator_type = Self;

  ///\brief Returns the inner allocator.
  ///\returns *this
  auto inner_allocator()
  noexcept
  -> inner_allocator_type& {
    return static_cast<Self&>(*this);
  }

  ///\brief Returns the inner allocator.
  ///\returns *this
  auto inner_allocator() const
  noexcept
  -> const inner_allocator_type& {
    return static_cast<Self&>(*this);
  }
};


/**
 * \brief Allocator wrapper that enables cache memory use tracking.
 * \ingroup cache
 *
 * \details
 * Cache allocator maintains an internal weak reference to the cache.
 * Whenever an allocation/deallocation occurs, this reference (if not nullptr)
 * is used to inform the cache of memory consumption changes.
 *
 * This specialization handles the case of a single allocator implementation.
 *
 * Its allocate() and deallocate() methods forward to the parent allocator.
 */
template<typename Allocator, typename... Tail>
class cache_allocator
: public Allocator,
  public cache_alloc_tail<cache_allocator<Allocator, Tail...>, Tail...>
{
  template<typename, typename...> friend class cache_allocator;

 private:
  ///\brief Easier name for the stats type.
  using stats_type = cache_alloc_dealloc_observer;

 public:
  ///\brief The parent allocator type for the cache allocator.
  using outer_allocator_type = Allocator;

 private:
  ///\brief Allocator traits for the parent allocator.
  using outer_traits = std::allocator_traits<outer_allocator_type>;

 public:
  ///\brief Value type.
  ///\details Inherited from parent allocator.
  using value_type = typename outer_traits::value_type;
  ///\brief Pointer type.
  ///\details Inherited from parent allocator.
  using pointer = typename outer_traits::pointer;
  ///\brief Const pointer type.
  ///\details Inherited from parent allocator.
  using const_pointer = typename outer_traits::const_pointer;
  ///\brief Void pointer type.
  ///\details Inherited from parent allocator.
  using void_pointer = typename outer_traits::void_pointer;
  ///\brief Const void pointer type.
  ///\details Inherited from parent allocator.
  using const_void_pointer = typename outer_traits::const_void_pointer;
  ///\brief Difference type.
  ///\details Inherited from parent allocator.
  using difference_type = typename outer_traits::difference_type;
  ///\brief Size type.
  ///\details Inherited from parent allocator.
  using size_type = typename outer_traits::size_type;
  ///\brief Container copy propagation.
  ///\details This is false, to ensure copied into/out-of collections
  ///don't attribute to the wrong cache.
  using propagate_on_container_copy_assignment = std::false_type;
  ///\brief Container move propagation.
  ///\details This is false, to ensure copied into/out-of collections
  ///don't attribute to the wrong cache.
  using propagate_on_container_move_assignment = std::false_type;
  ///\brief Container swap propagation.
  ///\details This is false, to ensure copied into/out-of collections
  ///don't attribute to the wrong cache.
  using propagate_on_container_swap = std::false_type;
  ///\brief Equality depends on stats.
  using is_always_equal = std::false_type;

  template<typename T>
  struct rebind {
    using other = cache_allocator<typename outer_traits::template rebind_alloc<T>, Tail...>;
  };

  ///\brief Default constructor.
  cache_allocator() = default;

  ///\brief Conversion constructor.
  template<typename OtherAllocator,
      typename = std::enable_if_t<std::is_constructible_v<Allocator, const OtherAllocator&>>>
  cache_allocator(const OtherAllocator& a)
  : Allocator(std::forward<OtherAllocator>(a))
  {}

  ///\brief Conversion constructor.
  template<typename OtherAllocator, typename... OtherTail,
      typename = std::enable_if_t<std::is_constructible_v<Allocator, const cache_allocator<OtherAllocator, OtherTail...>&>>>
  cache_allocator(const cache_allocator<OtherAllocator, Tail...>& a)
  : Allocator(a),
    stats_(a.stats_)
  {}

  ///\brief Initializing constructor.
  cache_allocator(const Allocator& a, const Tail&... tail)
  : Allocator(a),
    cache_alloc_tail<cache_allocator, Tail...>(tail...)
  {}

  ///\brief Initializing constructor.
  template<typename OtherAllocator, typename... OtherTail,
      typename = std::enable_if_t<std::is_constructible_v<Allocator, const OtherAllocator&>>>
  cache_allocator(const OtherAllocator& a, const OtherTail&... tail)
  : Allocator(a),
    cache_alloc_tail<cache_allocator, Tail...>(tail...)
  {}

  ///\brief Returns the parent allocator.
  ///\returns *this
  auto outer_allocator()
  noexcept
  -> outer_allocator_type& {
    return *this;
  }

  ///\brief Returns the parent allocator.
  ///\returns *this
  auto outer_allocator() const
  noexcept
  -> const outer_allocator_type& {
    return *this;
  }

  ///\brief Set the stats used by this allocator.
  ///\param[in] stats_ptr The stats to be used by this allocator.
  auto stats(std::weak_ptr<stats_type> stats_ptr)
  noexcept
  -> void {
    stats_ = stats_ptr;
  }

  /**
   * \brief Retrieve stats used by this allocator.
   * \note Stats are weak referenced by this allocator,
   * therefore, an invocation of this function that returns
   * a non-nullptr value may yield a nullptr on the next invocation.
   * \returns The stats of this allocator.
   * May be a nullptr if this allocator has no stats.
   */
  auto stats() const
  noexcept
  -> std::shared_ptr<stats_type> {
    return stats_.lock();
  }

  /**
   * \brief Allocation function.
   * \details If the allocation succeeds, memory usage is updated.
   * \param[in] n The number of items to allocate.
   * \returns The result of invoking the parent allocator's allocate method.
   */
  [[nodiscard]]
  auto allocate(size_type n)
  -> pointer {
    pointer p = outer_traits::allocate(outer_allocator(), n);
    if (p != nullptr) add_mem_use(n);
    return p;
  }

  /**
   * \brief Allocation function.
   * \details If the allocation succeeds, memory usage is updated.
   * \param[in] n The number of items to allocate.
   * \param[in] hint A pointer previously returned by the allocator,
   *    to use as allocator hint.
   * \returns The result of invoking the parent allocator's allocate method.
   */
  [[nodiscard]]
  auto allocate(size_type n, const_void_pointer hint)
  -> pointer {
    pointer p = outer_traits::allocate(outer_allocator(), n, hint);
    if (p != nullptr) add_mem_use(n);
    return p;
  }

  /**
   * \brief Deallocation function.
   * \details If the deallocation succeeds, memory usage is updated.
   * \param[in] p A pointer returned by a previous call to allocate.
   * \param[in] n The number of items used when allocating \p p.
   */
  auto deallocate(pointer p, size_type n)
  noexcept(
      noexcept(outer_traits::deallocate(
              std::declval<outer_allocator_type&>(),
              std::declval<pointer>(),
              std::declval<size_type>())))
  -> void {
    outer_traits::deallocate(outer_allocator(), p, n);
    if (p != nullptr) this->subtract_mem_use(n);
  }

  /**
   * \brief Returns a cache allocator without stats.
   * \returns Allocator that is to be used for copy constructing a container.
   */
  auto select_on_container_copy_construction() const
  -> cache_allocator {
    return cache_allocator(outer_traits::select_on_container_copy_construction(outer_allocator()));
  }

  ///\brief Allow conversion to scoped allocator adaptor.
  ///\note Other conversions to scoped allocator adaptor are handled by scoped allocator adaptor constructor.
  template<typename... T,
      typename = std::enable_if_t<
          std::is_constructible_v<std::scoped_allocator_adaptor<T...>, cache_allocator, typename cache_allocator::inner_allocator_type>>>
  operator std::scoped_allocator_adaptor<T...>() const {
    return std::scoped_allocator_adaptor<T...>(*this, this->inner_allocator());
  }

  /**
   * \brief Constructor.
   * \details
   * In place construction of value_type, at the address \p p.
   *
   * Constructs the value_type as:
   * \code
   * T(std::allocator_arg, inner_allocator(), std::forward<Args>(args)...); // If allocator aware and constructible,
   *                                                                        // or else:
   * T(std::forward<Args>(args)..., inner_allocator());                     // if allocator aware,
   *                                                                        // or:
   * T(std::forward<Args>(args)...);                                        // if not allocator aware,
   * \endcode
   *
   * \param[in,out] p The address at where to construct the type.
   * \param[in] args Arguments passed to constructor.
   */
  template<typename T, typename... Args>
  auto construct(T* p, Args&&... args)
  -> void {
    using outermost_type = typename can_exhaust_outer_allocator_<cache_allocator&>::result_type;
    using outermost_traits = std::allocator_traits<std::remove_cv_t<std::remove_reference_t<outermost_type>>>;
    outermost_type outermost = exhaust_outer_allocator_(*this);

    if constexpr(!std::uses_allocator_v<value_type, typename cache_allocator::inner_allocator_type>)
      outermost_traits::construct(outermost, p, std::forward<Args>(args)...);
    else if constexpr(std::is_constructible_v<value_type, std::allocator_arg_t&, typename cache_allocator::inner_allocator_type&, Args...>)
      outermost_traits::construct(outermost, p, std::allocator_arg, this->inner_allocator(), std::forward<Args>(args)...);
    else
      outermost_traits::construct(outermost, p, std::forward<Args>(args)..., this->inner_allocator());
  }

  ///\brief Pair constructor.
  ///\details Implemented the same as scoped_allocator_adaptor.
  template<typename T1, typename T2, typename... Args1, typename... Args2>
  auto construct(std::pair<T1, T2>* p, std::piecewise_construct_t, std::tuple<Args1...> x, std::tuple<Args2...> y)
  -> void {
    using outermost_type = typename can_exhaust_outer_allocator_<cache_allocator&>::result_type;
    using outermost_traits = std::allocator_traits<std::remove_cv_t<std::remove_reference_t<outermost_type>>>;
    outermost_type outermost = exhaust_outer_allocator_(*this);

    outermost_traits::construct(outermost, p,
        std::piecewise_construct,
        construct_handle_tuple_<T1>(std::move(x)),
        construct_handle_tuple_<T2>(std::move(y)));
  }

  ///\brief Pair constructor.
  ///\details Implemented the same as scoped_allocator_adaptor.
  template<typename T1, typename T2>
  auto construct(std::pair<T1, T2>* p)
  -> void {
    using outermost_type = typename can_exhaust_outer_allocator_<cache_allocator&>::result_type;
    using outermost_traits = std::allocator_traits<std::remove_cv_t<std::remove_reference_t<outermost_type>>>;
    outermost_type outermost = exhaust_outer_allocator_(*this);

    outermost_traits::construct(outermost, p,
        std::piecewise_construct,
        construct_handle_tuple_<T1>(std::tuple<>()),
        construct_handle_tuple_<T2>(std::tuple<>()));
  }

  ///\brief Pair constructor.
  ///\details Implemented the same as scoped_allocator_adaptor.
  template<typename T1, typename T2, typename X, typename Y>
  auto construct(std::pair<T1, T2>* p, X&& x, Y&& y)
  -> void {
    using outermost_type = typename can_exhaust_outer_allocator_<cache_allocator&>::result_type;
    using outermost_traits = std::allocator_traits<std::remove_cv_t<std::remove_reference_t<outermost_type>>>;
    outermost_type outermost = exhaust_outer_allocator_(*this);

    outermost_traits::construct(outermost, p,
        std::piecewise_construct,
        construct_handle_tuple_<T1>(std::forward_as_tuple(std::forward<X>(x))),
        construct_handle_tuple_<T2>(std::forward_as_tuple(std::forward<Y>(y))));
  }

  ///\brief Pair constructor.
  ///\details Implemented the same as scoped_allocator_adaptor.
  template<typename T1, typename T2, typename U1, typename U2>
  auto construct(std::pair<T1, T2>* p, const std::pair<U1, U2>& arg)
  -> void {
    using outermost_type = typename can_exhaust_outer_allocator_<cache_allocator&>::result_type;
    using outermost_traits = std::allocator_traits<std::remove_cv_t<std::remove_reference_t<outermost_type>>>;
    outermost_type outermost = exhaust_outer_allocator_(*this);

    outermost_traits::construct(outermost, p,
        std::piecewise_construct,
        construct_handle_tuple_<T1>(std::forward_as_tuple(arg.first)),
        construct_handle_tuple_<T2>(std::forward_as_tuple(arg.second)));
  }

  ///\brief Pair constructor.
  ///\details Implemented the same as scoped_allocator_adaptor.
  template<typename T1, typename T2, typename U1, typename U2>
  auto construct(std::pair<T1, T2>* p, std::pair<U1, U2>&& arg)
  -> void {
    using outermost_type = typename can_exhaust_outer_allocator_<cache_allocator&>::result_type;
    using outermost_traits = std::allocator_traits<std::remove_cv_t<std::remove_reference_t<outermost_type>>>;
    outermost_type outermost = exhaust_outer_allocator_(*this);

    outermost_traits::construct(outermost, p,
        std::piecewise_construct,
        construct_handle_tuple_<T1>(std::forward_as_tuple(std::forward<U1>(arg.first))),
        construct_handle_tuple_<T2>(std::forward_as_tuple(std::forward<U2>(arg.second))));
  }

  ///\brief Destructor.
  ///\param[in] p Pointer for which to invoke destroy.
  template<typename T>
  auto destroy(T* p)
  -> void {
    using outermost_type = typename can_exhaust_outer_allocator_<cache_allocator&>::result_type;
    using outermost_traits = std::allocator_traits<std::remove_cv_t<std::remove_reference_t<outermost_type>>>;
    outermost_type outermost = exhaust_outer_allocator_(*this);

    outermost_traits::destroy(outermost, p);
  }

 private:
  ///\brief Decorate an argument tuple with appropriate std::allocator_arg and inner_allocator.
  template<typename T, typename... Args>
  auto construct_handle_tuple_(std::tuple<Args...>&& tpl)
  -> std::conditional_t<
      std::uses_allocator_v<T, typename cache_allocator::inner_allocator_type>,
      std::conditional_t<
          std::is_constructible_v<T, std::allocator_arg_t&, typename cache_allocator::inner_allocator_type, Args...>,
          std::tuple<std::allocator_arg_t, typename cache_allocator::inner_allocator_type, Args...>,
          std::tuple<Args..., typename cache_allocator::inner_allocator_type>>,
      std::tuple<Args...>&&> {
    if constexpr(!std::uses_allocator_v<T, typename cache_allocator::inner_allocator_type>)
      return std::move(tpl);
    else if constexpr(std::is_constructible_v<T, std::allocator_arg_t&, typename cache_allocator::inner_allocator_type, Args...>)
      return std::tuple_cat(
          std::tuple<std::allocator_arg_t, typename cache_allocator::inner_allocator_type>(std::allocator_arg, this->inner_allocator()),
          std::move(tpl));
    else
      return std::tuple_cat(
          std::move(tpl),
          std::tuple<typename cache_allocator::inner_allocator_type>(this->inner_allocator()));
  }

  ///\brief Inform stats of an allocation.
  ///\param[in] n The number of items that was allocated.
  auto add_mem_use(size_type n)
  noexcept
  -> void {
    auto stats_ptr = stats();
    if (stats_ptr != nullptr)
      stats_ptr->add_mem_use(n, sizeof(value_type));
  }

  ///\brief Inform stats of a deallocation.
  ///\param[in] n The number of items that was deallocated.
  auto subtract_mem_use(size_type n)
  noexcept
  -> void {
    auto stats_ptr = stats();
    if (stats_ptr != nullptr)
      stats_ptr->subtract_mem_use(n, sizeof(value_type));
  }

  std::weak_ptr<stats_type> stats_;
};

///\brief Specialization that squashes scoped allocator.
template<typename... ScopedAllocs>
class cache_allocator<std::scoped_allocator_adaptor<ScopedAllocs...>>
: public cache_allocator<ScopedAllocs...>
{
 public:
  using cache_allocator<ScopedAllocs...>::cache_allocator;
  using cache_allocator<ScopedAllocs...>::operator=;
};

///\brief Specialization that squashes cache_allocator<cache_allocator<...>>.
template<typename... ScopedAllocs>
class cache_allocator<cache_allocator<ScopedAllocs...>>
: public cache_allocator<ScopedAllocs...>
{
 public:
  using cache_allocator<ScopedAllocs...>::cache_allocator;
  using cache_allocator<ScopedAllocs...>::operator=;
};


} /* namespace monsoon::cache */

namespace monsoon {


using cache::cache_allocator;


} /* namespace monsoon */

#endif /* MONSOON_CACHE_ALLOCATOR_H */
