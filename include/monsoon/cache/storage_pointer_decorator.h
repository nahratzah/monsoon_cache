#ifndef MONSOON_CACHE_STORAGE_POINTER_DECORATOR_H
#define MONSOON_CACHE_STORAGE_POINTER_DECORATOR_H

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
    spec() {} // XXX delete
    template<typename Builder> spec(const Builder& b) {} // XXX implement

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


#if 0

///\brief The default storage pointer decorator is one where the stored pointer type matches the external type.
template<typename VPtr>
using default_storage_pointer_decorator = storage_pointer_decorator<VPtr, VPtr, vptr_to_storage_identity_conversion_<VPtr>, storage_to_vptr_identity_conversion_<VPtr>>;

#else

template<typename VPtr>
struct funny_v_to_s {
  auto operator()(const VPtr& ptr) const noexcept -> std::tuple<int, VPtr> {
    return { 17, ptr };
  }
};

template<typename VPtr>
struct funny_s_to_v {
  auto operator()(const std::tuple<int, VPtr>& tpl) const noexcept -> const VPtr& {
    return std::get<1>(tpl);
  }
};

// For debug, we force storage type to be different.
template<typename VPtr>
using default_storage_pointer_decorator = storage_pointer_decorator<VPtr, std::tuple<int, VPtr>, funny_v_to_s<VPtr>, funny_s_to_v<VPtr>>;

#endif


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_STORAGE_POINTER_DECORATOR_H */
