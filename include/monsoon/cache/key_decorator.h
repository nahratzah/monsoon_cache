#ifndef MONSOON_CACHE_KEY_DECORATOR_H
#define MONSOON_CACHE_KEY_DECORATOR_H

///\file
///\ingroup cache_detail

#include <cassert>
#include <memory>
#include <type_traits>

namespace monsoon::cache {


///\brief Element decorator, for storing a key.
///\ingroup cache_detail
template<typename T>
struct key_decorator {
  static_assert(!std::is_const_v<T> && !std::is_volatile_v<T> && !std::is_reference_v<T>,
      "Expected plain type.");
  using key_type = T;

  key_decorator() noexcept = delete;

 private:
  struct alloc_last_ {};
  struct alloc_tagged_ {};
  struct alloc_ignored_ {};

  template<typename Alloc, typename... Types>
  key_decorator(
      Alloc& alloc,
      const std::tuple<Types...>& init,
      [[maybe_unused]] alloc_last_ discriminant)
  noexcept(std::is_nothrow_constructible_v<key_type, const key_type&, Alloc>)
  : key(std::get<key_type>(init), alloc)
  {}

  template<typename Alloc, typename... Types>
  key_decorator(
      Alloc& alloc,
      const std::tuple<Types...>& init,
      [[maybe_unused]] alloc_tagged_ discriminant)
  noexcept(std::is_nothrow_constructible_v<key_type, std::allocator_arg_t, Alloc, const key_type&>)
  : key(std::allocator_arg, alloc, std::get<key_type>(init))
  {}

  template<typename Alloc, typename... Types>
  key_decorator(
      [[maybe_unused]] Alloc& alloc,
      const std::tuple<Types...>& init,
      [[maybe_unused]] alloc_ignored_ discriminant)
  noexcept(std::is_nothrow_copy_constructible_v<key_type>)
  : key(std::get<key_type>(init))
  {}

 public:
  template<typename Alloc, typename... Types>
  key_decorator(
      [[maybe_unused]] std::allocator_arg_t tag,
      Alloc& alloc,
      const std::tuple<Types...>& init)
  : key_decorator(
      alloc,
      init,
      std::conditional_t<std::uses_allocator_v<key_type, Alloc>,
          std::conditional_t<std::is_constructible_v<key_type, const key_type&, Alloc>,
              alloc_last_,
              alloc_tagged_>,
          alloc_ignored_>())
  {}

  key_type key;
};


///\brief Cache decorator, that informs element to have a \ref key_decorator "key decorator".
///\ingroup cache_detail
template<typename T>
struct cache_key_decorator {
  using element_decorator_type = key_decorator<T>;

  template<typename Builder>
  constexpr cache_key_decorator([[maybe_unused]] const Builder& b) {}
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_KEY_DECORATOR_H */
