#include "cache_.h"

TEST(cache_memory) {
  cache<int, int> c = cache<int, int>::builder()
      .allocator(cache_allocator<std::allocator<int>>())
      .not_thread_safe()
      .max_memory(500 * sizeof(int))
      .build(mock_int_to_int_fn());

  for (int i = 0; i < 1000; ++i)
    c(i);

  CHECK(c.get_if_present(999) != nullptr);
  CHECK(c.get_if_present(0) == nullptr);
}
