#ifndef MONSOON_CACHE_ELEMENT_H
#define MONSOON_CACHE_ELEMENT_H

///\file
///\ingroup cache_detail

#include <monsoon/cache/storage_pointer_decorator.h>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace monsoon::cache {
namespace {

///\brief Invoke the is_expired() method on the decorator.
///\ingroup cache_detail
template<typename D, typename = void>
struct decorator_is_expired_ {
  static auto apply(const D& v) noexcept -> bool { return false; }
};

template<typename D>
struct decorator_is_expired_<D, std::void_t<decltype(std::declval<const D&>().is_expired())>> {
  static auto apply(const D& v) noexcept -> bool { return v.is_expired(); }
};


///\brief Invoke the is_expired() method on each of the decorators.
///\ingroup cache_detail
template<typename... Decorators> struct decorators_is_expired_;

template<>
struct decorators_is_expired_<> {
  template<typename T>
  static auto apply(const T& v) noexcept -> bool { return false; }
};

template<typename D, typename... Tail>
struct decorators_is_expired_<D, Tail...> {
  template<typename T>
  static auto apply(const T& v) noexcept -> bool {
    return decorator_is_expired_<D>::apply(v)
        || decorators_is_expired_<Tail...>::apply(v);
  }
};


template<typename D, typename... Tail>
struct select_storage_pointer_decorator_
: select_storage_pointer_decorator_<Tail...>
{};

template<typename VPtr, typename StoragePtr, typename VPtrToStoragePtrFn, typename StoragePtrToVPtrFn, typename... Tail>
struct select_storage_pointer_decorator_<
    storage_pointer_element_decorator<VPtr, StoragePtr, VPtrToStoragePtrFn, StoragePtrToVPtrFn>,
    Tail...>
{
  using type = storage_pointer_element_decorator<VPtr, StoragePtr, VPtrToStoragePtrFn, StoragePtrToVPtrFn>;
};


} /* namespace monsoon::cache::<unnamed> */


/**
 * \brief Decorator that, when used on an element, indicates the element should
 * support asynchronous operations.
 * \ingroup cache_detail
 *
 * \details Instructs the element to add a shared_future<pointer> as input to
 * its constructor, as well as returning it from its \ref element::ptr "ptr()"
 * method.
 */
struct async_element_decorator {
  template<typename Alloc, typename... Types>
  constexpr async_element_decorator(
      std::allocator_arg_t aa [[maybe_unused]],
      const Alloc& a [[maybe_unused]],
      const std::tuple<Types...>& init [[maybe_unused]])
  noexcept
  {}
};

/**
 * \brief Base class for element.
 * \ingroup cache_detail
 *
 * \details This holds the cache mapped value information only.
 *
 * Separating it from decorators should make the generated code a bit more
 * compact, or faster to compile.
 *
 * \tparam PointerDecorator A storage pointer decorator.
 * \tparam Async If true, indicates that the basic_element may hold futures.
 */
template<typename TPtr, bool Async> class basic_element;

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
class basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async> {
 public:
  using storage_spec = typename storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>::spec;
  using type = typename storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>::element_type;
  using pointer = typename storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>::pointer_type;
  using simple_pointer = std::add_pointer_t<type>;
  using const_reference = std::add_lvalue_reference_t<std::add_const_t<type>>;
  using weak_pointer = typename storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>::weak_type;
  using storage_pointer = typename storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>::storage_pointer_type;
  static constexpr bool is_async = Async;
  using future_type = std::conditional_t<is_async,
      std::shared_future<pointer>,
      void>;
  using ptr_return_type = std::conditional_t<is_async,
      std::variant<pointer, std::shared_future<pointer>>,
      pointer>;

 private:
  struct async_type {
    explicit async_type(future_type fut)
    : fut(std::move(fut))
    {}

    future_type fut;
    bool strong = true;
    bool expired = false;
  };

  using internal_ptr_type = std::conditional_t<is_async,
      std::variant<std::monostate, weak_pointer, storage_pointer, async_type>,
      std::variant<std::monostate, weak_pointer, storage_pointer>>;

 public:
  static auto is_nil(const ptr_return_type& p) noexcept -> bool;

  basic_element() = delete;
  basic_element(const basic_element&) = delete;
  basic_element(basic_element&&) = delete;
  basic_element& operator=(const basic_element&) = delete;
  basic_element& operator=(basic_element&&) = delete;

#ifndef NDEBUG
  ~basic_element() noexcept {
    assert(use_count == 0u);
  }
#endif

  /**
   * \brief Create an element pointing at the given init pointer.
   * \details This will be a strong reference to the pointee.
   * \param[in] init Pointer to the element to hold.
   * \param[in] hash The hashcode of the pointee.
   * \param[in] decorator_ctx Context information used by decorators.
   *    Decorators are expected to retrieve their information using
   *    std::get<type> on the \p decorator_ctx.
   */
  explicit basic_element(const storage_spec& spec, pointer init, std::size_t hash) noexcept;

  /**
   * \brief Constructor for the async case of element.
   * \details This will be a strong reference to the pointee.
   * \param[in] fut Future containing pointer value.
   * \param[in] hash The hashcode of the pointee.
   * \param[in] decorator_ctx Context information used by decorators.
   *    Decorators are expected to retrieve their information using
   *    std::get<type> on the \p decorator_ctx.
   */
  template<bool Enable = is_async>
  explicit basic_element(const storage_spec& spec, std::enable_if_t<Enable, future_type> init, std::size_t hash) noexcept;

  ///\brief Returns the hash code of the underlying pointer.
  ///\details The hash code remains the same, irresepective of wether the
  ///  pointer has expired.
  ///\returns The hash code of the underlying pointer.
  auto hash() const noexcept -> std::size_t;

 protected:
  ///\brief Returns a pointer to the held object.
  ///\returns A shared pointer to the object, if the object is live.
  ///   Otherwise, a nullptr is returned.
  ///   If the element \ref is_async "is async", the return type is a variant
  ///   of pointer or shared future to pointer.
  auto ptr() const noexcept -> ptr_return_type;

 public:
  ///\brief Check if the raw pointer stored in the object is the same.
  auto is_ptr(simple_pointer pp) const noexcept -> bool;

 protected:
  ///\brief Check if the object pointed to is live.
  ///\returns True if the call to ptr() would yield a nullptr.
  auto is_expired() const noexcept -> bool;

 public:
  ///\brief Resolves the async case.
  ///\details Acquires the value from the shared_future and assigns it.
  ///If this doesn't hold a shared_future, this is a noop.
  ///\returns Pointer, as if by calling ptr() when this \ref is_async "is not async".
  ///\throws Exception from future resolution, in which case this element is expired.
  auto resolve() -> pointer;

  /**
   * \brief Change the strong reference to a weak reference.
   *
   * \details
   * When the element holds a weak reference, the pointee is allowed to expire
   * once its life time outside the cache ends.
   *
   * \returns The element, by lvalue reference.
   */
  auto weaken() noexcept -> void;

  /**
   * \brief Change the weak reference to a strong reference.
   *
   * \details
   * When the element holds a weak reference, the pointee is allowed to expire
   * once its life time outside the cache ends.
   *
   * \returns The true if the value was strengthened, false otherwise.
   */
  auto strengthen() noexcept -> bool;

  /**
   * \brief Mark the element as expired.
   *
   * \details
   * Changes the element to hold the monotype, indicating it holds no valid data.
   */
  auto expire() noexcept -> void;

 private:
  std::size_t hash_ = 0;
  internal_ptr_type ptr_;
  simple_pointer plain_ptr_ = nullptr;

 public:
  ///\brief Counter, used to prevent element from being destroyed, when a lock is released.
  ///\note We use an atomic, so we don't have to think about the cache lock when releasing.
  ///\bug Optimization: use count should only be an atomic if the cache is thread safe.
  std::atomic<unsigned int> use_count{ 0u };

 private:
  const storage_spec& spec;
};

/**
 * \brief Cache element.
 * \ingroup cache_detail
 *
 * \details
 * An element holds information of an object pointed to by a shared ptr.
 *
 * \note The pointer held in the cache element is either a shared pointer, or a weak pointer.
 * If the pointer is a shared pointer, the element will not expire.
 * If the pointer is a weak pointer, the element will expire once the life time
 * of the pointee expires.
 *
 * \tparam Decorators Zero or more decorators to add additional information to
 *    the element.
 */
template<typename... Decorators>
class element
: public basic_element<typename select_storage_pointer_decorator_<Decorators...>::type, std::disjunction_v<std::is_base_of<async_element_decorator, Decorators>...>>,
  public Decorators...
{
 public:
  using storage_spec = typename basic_element<typename select_storage_pointer_decorator_<Decorators...>::type, std::disjunction_v<std::is_base_of<async_element_decorator, Decorators>...>>::storage_spec;
  using type = typename basic_element<typename select_storage_pointer_decorator_<Decorators...>::type, std::disjunction_v<std::is_base_of<async_element_decorator, Decorators>...>>::type;
  using future_type = typename basic_element<typename select_storage_pointer_decorator_<Decorators...>::type, std::disjunction_v<std::is_base_of<async_element_decorator, Decorators>...>>::future_type;
  using ptr_return_type = typename basic_element<typename select_storage_pointer_decorator_<Decorators...>::type, std::disjunction_v<std::is_base_of<async_element_decorator, Decorators>...>>::ptr_return_type;
  using pointer = typename basic_element<typename select_storage_pointer_decorator_<Decorators...>::type, std::disjunction_v<std::is_base_of<async_element_decorator, Decorators>...>>::pointer;

  /**
   * \brief Create an element pointing at the given init pointer.
   * \details This will be a strong reference to the pointee.
   * \param[in] init Pointer to the element to hold.
   * \param[in] hash The hashcode of the pointee.
   * \param[in] decorator_ctx Context information used by decorators.
   *    Decorators are expected to retrieve their information using
   *    std::get<type> on the \p decorator_ctx.
   */
  template<typename Alloc, typename... DecoratorCtx>
  explicit element(std::allocator_arg_t tag, Alloc alloc,
      pointer init, std::size_t hash,
      std::tuple<DecoratorCtx...> decorator_ctx) noexcept;

  /**
   * \brief Constructor for the async case of element.
   * \details This will be a strong reference to the pointee.
   * \param[in] fut Future containing pointer value.
   * \param[in] hash The hashcode of the pointee.
   * \param[in] decorator_ctx Context information used by decorators.
   *    Decorators are expected to retrieve their information using
   *    std::get<type> on the \p decorator_ctx.
   */
  template<typename Alloc, typename... DecoratorCtx, bool Enable = element::is_async>
  explicit element(std::allocator_arg_t aa [[maybe_unused]], Alloc alloc,
      std::enable_if_t<Enable, future_type> init, std::size_t hash,
      std::tuple<DecoratorCtx...> decorator_ctx) noexcept;

  ///\copydoc basic_element::ptr
  auto ptr() const noexcept -> ptr_return_type;
  ///\copydoc basic_element::is_expired
  auto is_expired() const noexcept -> bool;
};


template<typename... D>
template<typename Alloc, typename... DecoratorCtx>
element<D...>::element(std::allocator_arg_t tag, Alloc alloc,
    pointer init, std::size_t hash,
    std::tuple<DecoratorCtx...> decorator_ctx) noexcept
: basic_element<typename select_storage_pointer_decorator_<D...>::type, std::disjunction_v<std::is_base_of<async_element_decorator, D>...>>(std::get<const storage_spec&>(decorator_ctx), std::move(init), hash),
  D(tag, alloc, decorator_ctx)...
{}

template<typename... D>
template<typename Alloc, typename... DecoratorCtx, bool Enable>
element<D...>::element(std::allocator_arg_t tag, Alloc alloc,
    std::enable_if_t<Enable, future_type> init, std::size_t hash,
    std::tuple<DecoratorCtx...> decorator_ctx) noexcept
: basic_element<typename select_storage_pointer_decorator_<D...>::type, std::disjunction_v<std::is_base_of<async_element_decorator, D>...>>(std::get<const storage_spec&>(decorator_ctx), std::move(init), hash),
  D(tag, alloc, decorator_ctx)...
{}

template<typename... D>
auto element<D...>::ptr() const
noexcept
-> ptr_return_type {
  ptr_return_type p = this->basic_element<typename select_storage_pointer_decorator_<D...>::type, element::is_async>::ptr();
  if (!this->is_nil(p)
      && decorators_is_expired_<D...>::apply(*this))
    p = nullptr;
  return p;
}

template<typename... D>
auto element<D...>::is_expired() const
noexcept
-> bool {
  if (decorators_is_expired_<D...>::apply(*this))
    return true;
  return this->basic_element<typename select_storage_pointer_decorator_<D...>::type, element::is_async>::is_expired();
}


template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
auto basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::is_nil(const ptr_return_type& p)
noexcept
-> bool {
  if constexpr(is_async) {
    return std::holds_alternative<pointer>(p)
        && std::get<pointer>(p) == nullptr;
  } else {
    return p == nullptr;
  }
}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::basic_element(const storage_spec& spec, pointer init, std::size_t hash) noexcept
: hash_(hash),
  ptr_(std::in_place_type<std::monostate>),
  plain_ptr_(init.get()),
  spec(spec)
{
  spec.storage_init_(ptr_, std::move(init));
}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
template<bool Enable>
basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::basic_element(const storage_spec& spec, std::enable_if_t<Enable, future_type> init, std::size_t hash) noexcept
: hash_(hash),
  ptr_(std::in_place_type<async_type>, std::move(init)),
  plain_ptr_(nullptr),
  spec(spec)
{}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
auto basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::hash() const
noexcept
-> std::size_t {
  return hash_;
}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
auto basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::ptr() const
noexcept
-> ptr_return_type {
  if constexpr(is_async) {
    if (std::holds_alternative<async_type>(ptr_))
      return std::get<async_type>(ptr_).fut;
  }

  pointer p = nullptr;
  if (std::holds_alternative<weak_pointer>(ptr_))
    p = std::get<weak_pointer>(ptr_).lock();
  else if (std::holds_alternative<storage_pointer>(ptr_))
    p = spec.storage_deref_(std::get<storage_pointer>(ptr_));

  return p;
}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
auto basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::is_ptr(simple_pointer pp) const
noexcept
-> bool {
  return plain_ptr_ == pp;
}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
auto basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::is_expired() const
noexcept
-> bool {
  if constexpr(is_async) {
    if (std::holds_alternative<async_type>(ptr_))
      return false;
  }

  if (std::holds_alternative<weak_pointer>(ptr_))
    return std::get<weak_pointer>(ptr_).expired();
  else if (std::holds_alternative<storage_pointer>(ptr_))
    return spec.storage_deref_(std::get<storage_pointer>(ptr_)) == nullptr;
  else
    return true;
}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
auto basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::resolve()
-> pointer {
  if constexpr(is_async) {
    if (std::holds_alternative<async_type>(ptr_)) {
      // Resolve future.
      pointer ptr;
      try {
        ptr = std::get<async_type>(ptr_).fut.get();
      } catch (...) {
        // Invalidate on exception, so that future hits will not consider this.
        ptr_.template emplace<std::monostate>();
        throw;
      }

      // Update plain pointer.
      plain_ptr_ = ptr.get();
      // Update ptr_ with resolved pointer value.
      if (std::get<async_type>(ptr_).expired)
        ptr_.template emplace<std::monostate>();
      else if (std::get<async_type>(ptr_).strong)
        spec.storage_init_(ptr_, ptr);
      else
        ptr_.template emplace<weak_pointer>(ptr);
      return ptr;
    }

    return std::get<pointer>(ptr());
  } else { // !is_async case
    return ptr();
  }
}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
auto basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::weaken()
noexcept
-> void {
  if (std::holds_alternative<storage_pointer>(ptr_)) {
    auto ptr = spec.storage_deref_(std::get<storage_pointer>(std::move(ptr_)));
    ptr_.template emplace<weak_pointer>(std::move(ptr));
  } else {
    if constexpr(is_async) {
      if (std::holds_alternative<async_type>(ptr_))
        std::get<async_type>(ptr_).strong = false;
    }
  }
}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
auto basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::strengthen()
noexcept
-> bool {
  if (std::holds_alternative<weak_pointer>(ptr_)) {
    auto ptr = std::get<weak_pointer>(ptr_).lock();
    if (ptr != nullptr)
      spec.storage_init_(ptr_, std::move(ptr));
  } else {
    if constexpr(is_async) {
      if (std::holds_alternative<async_type>(ptr_))
        std::get<async_type>(ptr_).strong = true;
    }
  }
  return !std::holds_alternative<weak_pointer>(ptr_);
}

template<typename TPtr, typename SPtr, typename VToS, typename SToV, bool Async>
auto basic_element<storage_pointer_element_decorator<TPtr, SPtr, VToS, SToV>, Async>::expire()
noexcept
-> void {
  if constexpr(is_async) {
    if (std::holds_alternative<async_type>(ptr_)) {
      std::get<async_type>(ptr_).expired = true;
      return;
    }
  }

  if (std::holds_alternative<weak_pointer>(ptr_) || std::holds_alternative<storage_pointer>(ptr_))
    ptr_.template emplace<std::monostate>();
}


} /* namespace monsoon::cache */

namespace std {


///\brief Specialize uses_allocator to suppress allocator acceptance.
///\note Cache implementation explicitly forwards an allocator.
template<typename TPtr, typename... D, typename Alloc>
struct uses_allocator<monsoon::cache::element<TPtr, D...>, Alloc>
: false_type
{};


} /* namespace std */

#endif /* MONSOON_CACHE_ELEMENT_H */
