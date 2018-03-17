#ifndef MONSOON_CACHE_EXPIRE_QUEUE_H
#define MONSOON_CACHE_EXPIRE_QUEUE_H

///\file
///\ingroup cache_detail

#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <tuple>
#include <utility>

namespace monsoon::cache {


class base_expire_queue;

/**
 * \brief Element decorator counterpart for base_expire_queue.
 * \ingroup cache_detail
 * \details Maintains pointers for the doubly linked list in base_expire_queue,
 * as well as an indicator of which queue is used.
 */
class expire_link {
  friend class base_expire_queue;

 private:
  ///\brief Flag mask.
  ///\details Both link_succ_ and link_pred_ members will have the flag and will be kept in sync.
  static constexpr std::uintptr_t Q_MASK = 0x1u;
  ///\brief Queue identifier.
  enum class queue_id : std::uintptr_t {
    cold = 0x0u, ///<\brief Indicate that the element is linked on the cold queue.
    hot = 0x1u, ///<\brief Indicate that the element is linked on the hot queue.
    none = std::numeric_limits<std::uintptr_t>::max() ///<\brief Indicate that the element is not linked.
  };

 public:
  ///\brief Element constructor. Ensures this becomes an unlinked element.
  template<typename Alloc, typename... Types>
  expire_link(
      [[maybe_unused]] std::allocator_arg_t aa,
      [[maybe_unused]] const Alloc& a,
      [[maybe_unused]] const std::tuple<Types...>& init)
  : expire_link(queue_id::none)
  {}

#ifndef NDEBUG
  // Assert expire_link is an unlinked element when destroyed, or represents the empty queue.
  ~expire_link() noexcept {
    assert(link_pred_ == 0
        || (link_pred_ & ~Q_MASK) == reinterpret_cast<std::uintptr_t>(this));
    assert(link_succ_ == 0
        || (link_succ_ & ~Q_MASK) == reinterpret_cast<std::uintptr_t>(this));
    assert(link_pred_ == link_succ_); // Also, that they are both the same.
  }
#endif

  expire_link(const expire_link&) = delete;
  expire_link(expire_link&&) = delete;
  expire_link& operator=(const expire_link&) = delete;
  expire_link& operator=(expire_link&&) = delete;

 private:
  /**
   * \brief Constructor.
   * \param[in] qid
   * If \p qid is queue_id::none, this will be unlinked.
   * Otherwise, this will become a head, with the given \ref queue_id "queue ID".
   */
  explicit expire_link(queue_id qid) noexcept
  : link_pred_(qid == queue_id::none
      ? 0u
      : reinterpret_cast<std::uintptr_t>(this) | static_cast<std::uintptr_t>(qid)),
    link_succ_(qid == queue_id::none
      ? 0u
      : reinterpret_cast<std::uintptr_t>(this) | static_cast<std::uintptr_t>(qid))
  {
    // Assert alignment constraint.
    assert((reinterpret_cast<std::uintptr_t>(this) & Q_MASK) == 0u);
  }

  ///\brief Retrieve successor pointer.
  ///\tparam StoreType The type of the successor.
  ///\returns A pointer to the successor. A nullptr is returned if this is unlinked.
  template<typename StoreType = expire_link>
  auto succ() const
  noexcept
  -> StoreType* {
    return static_cast<StoreType*>(reinterpret_cast<expire_link*>(link_succ_ & ~Q_MASK));
  }

  ///\brief Retrieve predecessor pointer.
  ///\tparam StoreType The type of the predecessor.
  ///\returns A pointer to the predecessor. A nullptr is returned if this is unlinked.
  template<typename StoreType = expire_link>
  auto pred() const
  noexcept
  -> StoreType* {
    return static_cast<StoreType*>(reinterpret_cast<expire_link*>(link_pred_ & ~Q_MASK));
  }

  ///\brief Retrieve the \ref queue_id "queue ID".
  auto get_queue_id() const
  noexcept
  -> queue_id {
    if (link_pred_ == 0u) return queue_id::none;
    assert((link_pred_ & Q_MASK) == (link_succ_ & Q_MASK)); // Assert queue ID is consistent.
    return static_cast<queue_id>(link_pred_ & Q_MASK);
  }

  ///\brief Link this after the given element.
  ///\details Undefined behaviour if this is linked, or a queue head.
  auto link_after(expire_link* before)
  noexcept
  -> void {
    assert(before != nullptr); // Cannot link before nullptr.
    assert(before != this); // Cannot link before self.
    assert(link_pred_ == 0 && link_succ_ == 0); // Only allow linking when we're unlinked.

    expire_link* after = before->succ();
    assert(before->get_queue_id() == after->get_queue_id());
    const std::uintptr_t self_ptr = reinterpret_cast<std::uintptr_t>(this) | (before->link_succ_ & Q_MASK);
    link_succ_ = std::exchange(before->link_succ_, self_ptr);
    link_pred_ = std::exchange(after->link_pred_, self_ptr);

    assert(pred() == before && succ() == after);
    assert(before->succ() == this);
    assert(after->pred() == this);
    assert(before->get_queue_id() == get_queue_id());
    assert(after->get_queue_id() == get_queue_id());
  }

  ///\brief Link this before the given element.
  ///\details Undefined behaviour if this is linked, or a queue head.
  auto link_before(expire_link* after)
  noexcept
  -> void {
    assert(after != nullptr); // Cannot link after nullptr.
    assert(after != this); // Cannot link after self.
    assert(link_pred_ == 0 && link_succ_ == 0); // Only allow linking when we're unlinked.

    expire_link* before = after->pred();
    assert(before->get_queue_id() == after->get_queue_id());
    const std::uintptr_t self_ptr = reinterpret_cast<std::uintptr_t>(this) | (after->link_succ_ & Q_MASK);
    link_succ_ = std::exchange(before->link_succ_, self_ptr);
    link_pred_ = std::exchange(after->link_pred_, self_ptr);

    assert(pred() == before && succ() == after);
    assert(before->succ() == this);
    assert(after->pred() == this);
    assert(before->get_queue_id() == get_queue_id());
    assert(after->get_queue_id() == get_queue_id());
  }

  ///\brief Unlink this.
  ///\details Undefined behaviour if this is not linked, or a queue head.
  auto unlink()
  noexcept
  -> void {
    assert(link_pred_ != 0 && link_succ_ != 0); // Only allow unlinking when we're linked.

    expire_link* p = pred();
    expire_link* s = succ();
    assert(p != this && s != this); // Queue head may not be unlinked (and this method is incapable of unlinking it anyway).
    p->link_succ_ = std::exchange(link_succ_, 0);
    s->link_pred_ = std::exchange(link_pred_, 0);
  }

  ///\brief Link to predecessor, with a flag bit to indicate the queue type.
  std::uintptr_t link_pred_ = 0;
  ///\brief Link to successor, with a flag bit to indicate the queue type.
  std::uintptr_t link_succ_ = 0;
};

/**
 * \brief Expire queue for cache elements.
 * \ingroup cache_detail
 * \details
 * The expire queue extends the life time of cache items.
 * This decorator thus allows the cache to cache recently queried items.
 *
 * The implementation uses a hot zone and a cold zone, each of equal size.
 * Newly constructed elements are added to the cold zone.
 * On cache hit, cold elements are promoted to the hot zone.
 * The algorithm favours keeping often queried items in the cache, while
 * allowing substituting elements that are no longer queried for new items.
 */
class base_expire_queue {
 public:
  base_expire_queue(const base_expire_queue&) = delete;
  base_expire_queue(base_expire_queue&&) = delete;
  base_expire_queue& operator=(const base_expire_queue&) = delete;
  base_expire_queue& operator=(base_expire_queue&&) = delete;

  template<typename Builder>
  constexpr base_expire_queue([[maybe_unused]] const Builder& b) noexcept {}

 protected:
  ///\brief Import queue_id, for convenience.
  using queue_id = expire_link::queue_id;

 public:
  /**
   * \brief On-cache-create callback.
   * \details
   * Adds the \ref monsoon::cache::element "element" to the queue,
   * at the top of the cold zone.
   *
   * If \p s is queried later, the resulting
   * \ref base_expire_queue::on_hit "on_hit" callback will promote the
   * \ref monsoon::cache::element "element" to the hot zone, assuming it
   * hasn't been pushed out of the queue in the mean time.
   *
   * By inserting \p s into the cold zone, we prevent it from expiring
   * often requested entries in the hot zone.
   */
  auto on_create(expire_link& s)
  noexcept
  -> void {
    s.link_after(&cold_q);
    ++cold_qlen;

    rebalance_();
  }

  /**
   * \brief On-cache-delete callback.
   * \details
   * Removes the \ref monsoon::cache::element "element" from the queue.
   */
  auto on_delete(expire_link& s)
  noexcept
  -> void {
    switch (s.get_queue_id()) {
      case queue_id::none:
        return;
      case queue_id::hot:
        s.unlink();
        --hot_qlen;
        break;
      case queue_id::cold:
        s.unlink();
        --cold_qlen;
        break;
    }

    rebalance_();
  }

  /**
   * \brief On-cache-hit callback.
   * \details
   * If the element is not on the queue, it is added to the front of the cold
   * side, same as would happen with a call by
   * \ref base_expire_queue::on_create "on_create".
   * In addition, the element will be strengthened, so it's life time will
   * extend as long as it is on the queue.
   *
   * If the element is on the queue, it is promoted to the front of the hot
   * side.
   * This behaviour ensures often accessed elements are more likely to remain
   * in the cache.
   *
   * \param s_arg The \ref monsoon::cache::element "element" for which the
   * cache hit occurred.
   */
  template<typename StoreType>
  auto on_hit(StoreType& s_arg)
  noexcept
  -> void {
    expire_link& s = s_arg; // Prevent us from accidentally invoke store_type methods when we want expire_link methods.

    switch (s.get_queue_id()) {
      case queue_id::none:
        if (!s_arg.strengthen()) return;
        s.link_after(&cold_q);
        ++cold_qlen;
        break;
      case queue_id::hot:
        s.unlink();
        s.link_after(&hot_q);
        break;
      case queue_id::cold:
        s.unlink();
        s.link_after(&hot_q);
        --cold_qlen;
        ++hot_qlen;
        break;
    }

    rebalance_();
  }

  auto size() const
  noexcept
  -> std::uintptr_t {
    return hot_qlen + cold_qlen;
  }

 protected:
  /**
   * \brief Retrieves the coldest element in the queue.
   * \returns A pointer to the coldest element in the queue, or a nullptr if the queue is empty.
   * \tparam StoreType The store_type of the queue.
   */
  template<typename StoreType>
  auto coldest() const
  noexcept
  -> StoreType* {
    if (cold_qlen > 0) return cold_q.pred<StoreType>();
    if (hot_qlen > 0) return hot_q.pred<StoreType>();
    return nullptr;
  }

  /**
   * \brief Shrink this queue to the given size.
   * \details
   * Reduces the size of this queue until it is less than or equal to \p new_size.
   * Does nothing if the queue is smaller than \p new_size.
   *
   * Elements discarded from the queue are weakened, so that they will be
   * automatically released when they expire.
   * \param[in] new_size The new size of the queue.
   */
  template<typename StoreType, typename Impl>
  auto shrink_to_size_(Impl& impl, std::uintptr_t new_size)
  noexcept
  -> void {
    while (cold_qlen > 0 && cold_qlen + hot_qlen > new_size) {
      StoreType* r = cold_q.pred<StoreType>();
      expire_link* r_link = r;

      assert(r_link->get_queue_id() == queue_id::cold);
      r->weaken();
      r_link->unlink();
      --cold_qlen;
      impl.erase_if_expired(*r);
    }

    if (cold_qlen == 0) {
      while (hot_qlen > new_size) {
        StoreType* r = hot_q.pred<StoreType>();
        expire_link* r_link = r;

        assert(r_link->get_queue_id() == queue_id::hot);
        r->weaken();
        r_link->unlink();
        --hot_qlen;
        impl.erase_if_expired(*r);
      }
    }

    rebalance_();
  }

  /**
   * \brief Drop the coldest entries for which \p predicate holds.
   * \details
   * The coldest entries are checked against \p predicate. If the predicate
   * returns true, the entry is removed.
   *
   * The function loops until either the queue is empty or \p predicate
   * returns false.
   * \param[in] predicate The predicate to test.
   */
  template<typename StoreType, typename Impl, typename P>
  auto shrink_while_(Impl& impl, P predicate)
  noexcept
  -> void {
    bool stop = false;
    while (!stop && cold_qlen > 0) {
      StoreType* r = cold_q.pred<StoreType>();
      expire_link* r_link = r;

      stop = !predicate(std::as_const(*r));
      if (!stop) {
        assert(r_link->get_queue_id() == queue_id::cold);
        r->weaken();
        r_link->unlink();
        --cold_qlen;
        impl.erase_if_expired(*r);
      }
    }

    while (!stop && hot_qlen > 0) {
      StoreType* r = hot_q.pred<StoreType>();
      expire_link* r_link = r;

      stop = !predicate(std::as_const(*r));
      if (!stop) {
        assert(r_link->get_queue_id() == queue_id::hot);
        r->weaken();
        r_link->unlink();
        --hot_qlen;
        impl.erase_if_expired(*r);
      }
    }

    rebalance_();
  }

 private:
  /**
   * \brief Keep hot and cold queue at equal size (+/- 1, if size() is odd).
   * \details We shift elements between the back of the hot queue and the
   * front of the cold queue.
   *
   * This method preserves relative order in the elements, if the total queue
   * is seen as the concatenation of hot and cold.
   */
  auto rebalance_()
  noexcept
  -> void {
    while (cold_qlen + 1u < hot_qlen) { // Transfer from hot to cold.
      expire_link* s = hot_q.pred(); // From back of queue.
      assert(s != &hot_q);
      s->unlink();
      s->link_after(&cold_q); // To front of queue.
      --hot_qlen;
      ++cold_qlen;
    }

    while (hot_qlen + 1u < cold_qlen) { // Transfer from cold to hot.
      expire_link* s = cold_q.succ(); // From front of queue.
      assert(s != &cold_q);
      s->unlink();
      s->link_before(&hot_q); // To back of queue.
      --cold_qlen;
      ++hot_qlen;
    }
  }

  std::uintptr_t cold_qlen = 0, hot_qlen = 0;
  expire_link cold_q{ queue_id::cold }, hot_q{ queue_id::hot };
};

///\brief Decorator that maintains a queue of \ref monsoon::cache::element "elements".
///\ingroup cache_detail
///\tparam StoreType The \ref monsoon::cache::element "element" type of the cache.
template<typename ImplType>
class expire_queue
: public base_expire_queue
{
 public:
  using element_decorator_type = expire_link;

  using base_expire_queue::base_expire_queue;

  ///\copydoc base_expire_queue::shrink_to_size_
  auto shrink_to_size(std::uintptr_t new_size)
  noexcept
  -> void {
    shrink_to_size_<typename ImplType::store_type>(static_cast<ImplType&>(*this), new_size);
  }

  ///\copydoc base_expire_queue::shrink_while_
  template<typename P>
  auto shrink_while(P predicate)
  noexcept
  -> void {
    shrink_while_<typename ImplType::store_type>(static_cast<ImplType&>(*this), std::move(predicate));
  }

  template<typename StoreType>
  auto on_create(StoreType& s)
  noexcept
  -> void {
    this->base_expire_queue::on_create(s);
    cleanup_();
  }

  template<typename StoreType>
  auto on_hit(StoreType& s)
  noexcept
  -> void {
    this->base_expire_queue::on_hit(s);
    cleanup_();
  }

 private:
  /**
   * \brief Erases expired elements from the queue.
   * \details
   * This implementation only evaluates the coldest elements,
   * so some expired elements may stay in the queue.
   * Those elements will bubble out eventually.
   */
  auto cleanup_() {
    for (auto ptr = coldest<typename ImplType::store_type>();
        ptr != nullptr;
        ptr = coldest<typename ImplType::store_type>()) {
      // cache_impl::erase_if_expired will invoke the on_delete
      // callback, which does the actual removing from the queue.
      if (!static_cast<ImplType&>(*this).erase_if_expired(*ptr))
        break;
    }
  }
};

///\brief Decorator that installs an expire queue on the cache.
///\ingroup cache_detail
struct cache_expire_queue_decorator {
  template<typename SimpleCacheImpl>
  using for_impl_type = expire_queue<SimpleCacheImpl>;
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_EXPIRE_QUEUE_H */
