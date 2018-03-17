#ifndef MONSOON_CACHE_THREAD_SAFE_DECORATOR_H
#define MONSOON_CACHE_THREAD_SAFE_DECORATOR_H

///\file
///\ingroup cache_detail

#include <mutex>

namespace monsoon::cache {


/**
 * \brief Cache decorator, implementing the basic lockable concept.
 * \ingroup cache_detail
 * \details This decorator only provides a thread safe implementation,
 * if \p Enabled is true.
 * \tparam Enabled If true, the implementation will use a mutex to provide
 * exclusive locking.
 * Otherwise, the basic lockable concept is implemented as a Noop.
 */
template<bool Enabled>
class thread_safe_decorator {
 public:
  template<typename Builder>
  thread_safe_decorator([[maybe_unused]] const Builder& b) {}

  auto lock() const -> void {
    mtx_.lock();
  }

  auto unlock() const -> void {
    mtx_.unlock();
  }

 private:
  mutable std::mutex mtx_;
};

template<>
class thread_safe_decorator<false> {
 public:
  template<typename Builder>
  constexpr thread_safe_decorator([[maybe_unused]] const Builder& b) {}

  auto lock() const noexcept -> void {}
  auto unlock() const noexcept -> void {}
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_THREAD_SAFE_DECORATOR_H */
