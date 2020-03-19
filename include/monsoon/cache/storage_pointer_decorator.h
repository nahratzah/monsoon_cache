#ifndef MONSOON_CACHE_STORAGE_POINTER_DECORATOR_H
#define MONSOON_CACHE_STORAGE_POINTER_DECORATOR_H

#include <monsoon/cache/builder.h>
#include <memory>
#include <tuple>
#include <type_traits>

namespace monsoon::cache {


// We use a dedicated apply imlementation,
// because:
// - the STL apply returns a copy of the result (which is a problem if the storage pointer is not copyable)
// - we don't need the return value of emplace.
template<typename StoragePointer>
class storage_pointer_variant_emplace_ {
 public:
  template<typename Variant, typename... Args>
  void operator()(Variant& v, std::tuple<Args...>&& args) const noexcept {
    do_(v, std::move(args), std::index_sequence_for<Args...>());
  }

 private:
  template<typename Variant, typename... Args, std::size_t... Idxs>
  static void do_(Variant& v, std::tuple<Args...>&& args, [[maybe_unused]] std::index_sequence<Idxs...> seq) noexcept {
    v.template emplace<StoragePointer>(std::get<Idxs>(std::move(args))...);
  }
};

template<typename VPtr, typename StoragePtr, typename VPtrToStoragePtrFn, typename StoragePtrToVPtrFn>
struct storage_pointer_element_decorator;

/**
 * \brief Allow overriding how element internally stores pointers.
 */
template<typename VPtr, typename StoragePtr, typename VPtrToStoragePtrFn, typename StoragePtrToVPtrFn>
struct storage_pointer_decorator {
  using element_type = typename std::pointer_traits<VPtr>::element_type;
  using pointer_type = typename std::pointer_traits<VPtr>::pointer;
  using weak_type = typename pointer_type::weak_type;
  using storage_pointer_type = StoragePtr;

  class spec
  : private VPtrToStoragePtrFn,
    private StoragePtrToVPtrFn
  {
   public:
    using element_decorator_type = storage_pointer_element_decorator<VPtr, StoragePtr, VPtrToStoragePtrFn, StoragePtrToVPtrFn>;

    explicit spec(const builder_vars_::storage_override_var<void, void, void>& b) {}

    explicit spec(const builder_vars_::storage_override_var<StoragePtr, VPtrToStoragePtrFn, StoragePtrToVPtrFn>& b)
    : VPtrToStoragePtrFn(b.storage_override_cargs_fn()),
      StoragePtrToVPtrFn(b.storage_override_deref_fn())
    {}

    auto init_tuple() const -> std::tuple<const spec&> {
      return { *this };
    }

    template<typename Variant>
    void storage_init_(Variant& v, pointer_type ptr) const noexcept {
      storage_pointer_variant_emplace_<storage_pointer_type> variant_apply;
      const VPtrToStoragePtrFn& vptr_to_storage_fn = *this;
      return variant_apply(v, std::invoke(vptr_to_storage_fn, std::move(ptr)));
    }

    auto storage_deref_(const storage_pointer_type& sptr) const noexcept -> pointer_type {
      const StoragePtrToVPtrFn& storage_to_vptr_fn = *this;
      return std::invoke(storage_to_vptr_fn, sptr);
    }
  };

  template<typename CacheImpl>
  using for_impl_type = spec;
};


template<typename VPtr, typename StoragePtr, typename VPtrToStoragePtrFn, typename StoragePtrToVPtrFn>
struct storage_pointer_element_decorator {
  using element_type         = typename storage_pointer_decorator<VPtr, StoragePtr, VPtrToStoragePtrFn, StoragePtrToVPtrFn>::element_type;
  using pointer_type         = typename storage_pointer_decorator<VPtr, StoragePtr, VPtrToStoragePtrFn, StoragePtrToVPtrFn>::pointer_type;
  using weak_type            = typename storage_pointer_decorator<VPtr, StoragePtr, VPtrToStoragePtrFn, StoragePtrToVPtrFn>::weak_type;
  using storage_pointer_type = typename storage_pointer_decorator<VPtr, StoragePtr, VPtrToStoragePtrFn, StoragePtrToVPtrFn>::storage_pointer_type;
  using spec                 = typename storage_pointer_decorator<VPtr, StoragePtr, VPtrToStoragePtrFn, StoragePtrToVPtrFn>::spec;

  template<typename Alloc, typename... Types>
  constexpr storage_pointer_element_decorator(
      [[maybe_unused]] std::allocator_arg_t tag,
      [[maybe_unused]] Alloc a,
      [[maybe_unused]] const std::tuple<Types...>& init) noexcept
  {}
};


template<typename VPtr>
struct vptr_to_storage_identity_conversion_ {
  auto operator()(const VPtr& vptr) const noexcept -> std::tuple<const VPtr&> {
    return std::forward_as_tuple(vptr);
  }

  auto operator()(VPtr&& vptr) const noexcept -> std::tuple<VPtr&&> {
    return std::forward_as_tuple(std::move(vptr));
  }
};

template<typename VPtr>
struct storage_to_vptr_identity_conversion_ {
  auto operator()(const VPtr& vptr) const noexcept -> VPtr {
    return vptr;
  }
};


///\brief The default storage pointer decorator is one where the stored pointer type matches the external type.
template<typename VPtr>
using default_storage_pointer_decorator = storage_pointer_decorator<VPtr, VPtr, vptr_to_storage_identity_conversion_<VPtr>, storage_to_vptr_identity_conversion_<VPtr>>;


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_STORAGE_POINTER_DECORATOR_H */
