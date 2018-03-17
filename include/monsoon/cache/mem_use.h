#ifndef MONSOON_CACHE_MEM_USE_H
#define MONSOON_CACHE_MEM_USE_H

///\file
///\ingroup cache_detail

#include <atomic>
#include <monsoon/cache/allocator.h>

namespace monsoon::cache {


/**
 * \brief Memory usage tracking.
 * \details
 * Tracks memory usage by allocator.
 */
class mem_use
: public cache_alloc_dealloc_observer
{
 public:
  mem_use() = default;
  mem_use(const mem_use&) = delete;
  mem_use(mem_use&&) = delete;
  mem_use& operator=(const mem_use&) = delete;
  mem_use& operator=(mem_use&&) = delete;

  auto add_mem_use(std::uintptr_t n, std::uintptr_t sz)
  noexcept
  -> void
  override {
    mem_used_.fetch_add(n * sz, std::memory_order_relaxed);
  }

  auto subtract_mem_use(std::uintptr_t n, std::uintptr_t sz)
  noexcept
  -> void
  override {
    mem_used_.fetch_sub(n * sz, std::memory_order_relaxed);
  }

  auto get() const
  noexcept
  -> std::uintptr_t {
    return mem_used_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<std::uintptr_t> mem_used_{ 0u };
};

/**
 * \brief Decorator that enforces max_memory.
 * \ingroup cache_internal
 * \details
 * The decorator functions by shrinking the cache to a size that
 * probably is fine to keep memory usage within bounds.
 *
 * The implementation functions by reducing the expire queue size
 * proportionally down.
 *
 * \note
 * Memory usage by the cache is the sum of all elements
 * in the cache, plus all live elements that were ever constructed
 * by the cache.
 * Therefore, a large pool of outside, live cache items may pressure
 * the cache size down by a lot.
 * However, this may be exactly what you want.
 */
struct cache_max_mem_decorator {
  ///\brief Implementation of cache_max_mem_decorator.
  ///\ingroup cache_internal
  template<typename ImplType>
  class for_impl_type {
   public:
    for_impl_type(const cache_builder_vars& v)
    : max_mem_use_(v.max_memory().value())
    {}

    template<typename StoreType>
    auto on_create([[maybe_unused]] const StoreType& s)
    noexcept
    -> void {
      maintenance_();
    }

    template<typename StoreType>
    auto on_hit([[maybe_unused]] const StoreType& s)
    noexcept
    -> void {
      maintenance_();
    }

    // Filled in by cache_builder.
    auto set_mem_use(std::shared_ptr<const mem_use> m)
    noexcept
    -> void {
      mem_use_ = std::move(m);
    }

   private:
    auto maintenance_()
    noexcept
    -> void {
      if (mem_use_ == nullptr) return;

      expire_queue<ImplType>& q = static_cast<ImplType&>(*this);
      const std::uintptr_t cur_mem_use = mem_use_->get();
      if (cur_mem_use < max_mem_use_) return;

      float multiplier = float(max_mem_use_) / float(cur_mem_use);
      std::uintptr_t shrink_sz = static_cast<std::uintptr_t>(q.size() * multiplier);
      q.shrink_to_size(shrink_sz);
    }

    std::shared_ptr<const mem_use> mem_use_;
    std::uintptr_t max_mem_use_;
  };
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_MEM_USE_H */
