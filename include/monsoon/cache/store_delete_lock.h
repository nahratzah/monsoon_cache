#ifndef MONSOON_CACHE_STORE_DELETE_LOCK_H
#define MONSOON_CACHE_STORE_DELETE_LOCK_H

///\file
///\ingroup cache_detail

#include <atomic>
#include <cassert>
#include <utility>

namespace monsoon::cache {


///\brief Scoped lock to prevent a store type from being deleted.
///\ingroup cache_detail
///\note This lock may only be acquired while the cache is locked.
///It may however be released without the cache being locked.
template<typename S>
class store_delete_lock {
 public:
  constexpr store_delete_lock() noexcept = default;

  explicit store_delete_lock(S* ptr) noexcept
  : ptr_(ptr)
  {
    if (ptr_ != nullptr)
      ptr_->use_count.fetch_add(1u, std::memory_order_acquire);
  }

  store_delete_lock(const store_delete_lock& other)
  noexcept
  : store_delete_lock(other.ptr_)
  {}

  store_delete_lock(store_delete_lock&& other)
  noexcept
  : ptr_(std::exchange(other.ptr_, nullptr))
  {}

  auto operator=(const store_delete_lock& other)
  noexcept
  -> store_delete_lock& {
    if (&other != this) {
      reset();
      ptr_ = other.ptr_;
      if (ptr_ != nullptr)
        ptr_->use_count.fetch_add(1u, std::memory_order_acquire);
    }
    return *this;
  }

  auto operator=(store_delete_lock&& other)
  noexcept
  -> store_delete_lock& {
    if (&other != this) {
      reset();
      ptr_ = std::exchange(other.ptr_, nullptr);
    }
    return *this;
  }

  ~store_delete_lock() noexcept {
    reset();
  }

  explicit operator bool() const noexcept {
    return ptr_ != nullptr;
  }

  auto operator*() const
  noexcept
  -> S& {
    assert(ptr_ != nullptr);
    return *ptr_;
  }

  auto operator->() const
  noexcept
  -> S* {
    assert(ptr_ != nullptr);
    return ptr_;
  }

  auto get() const
  noexcept
  -> S* {
    return ptr_;
  }

  auto reset()
  noexcept
  -> void {
    if (ptr_ != nullptr)
      std::exchange(ptr_, nullptr)->use_count.fetch_sub(1u, std::memory_order_release);
  }

 private:
  S* ptr_ = nullptr;
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_STORE_DELETE_LOCK_H */
