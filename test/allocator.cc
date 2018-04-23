#include "cache_.h"

class observer
: public cache_alloc_dealloc_observer
{
 public:
  void add_mem_use(std::uintptr_t, std::uintptr_t) noexcept override {}
  void subtract_mem_use(std::uintptr_t, std::uintptr_t) noexcept override {}
};

TEST(allocator_stats_propagation) {
  std::shared_ptr<observer> obs = std::make_shared<observer>();
  cache_allocator<std::allocator<int>> alloc;
  cache_alloc_dealloc_observer::maybe_set_stats(alloc, obs);

  REQUIRE CHECK_EQUAL(obs, alloc.stats());

  cache_allocator<std::allocator<long>> other_alloc = alloc;
  CHECK_EQUAL(obs, other_alloc.stats());
}

TEST(allocator_stats_dont_propagate_on_copy_selection) {
  std::shared_ptr<observer> obs = std::make_shared<observer>();
  cache_allocator<std::allocator<int>> alloc;
  cache_alloc_dealloc_observer::maybe_set_stats(alloc, obs);

  REQUIRE CHECK_EQUAL(obs, alloc.stats());

  cache_allocator<std::allocator<long>> other_alloc = alloc.select_on_container_copy_construction();
  CHECK_EQUAL(std::shared_ptr<observer>(nullptr), other_alloc.stats());
}
