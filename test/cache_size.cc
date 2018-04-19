#include "cache_.h"

TEST(cache_size) {
  cache<int, int> c = cache<int, int>::builder()
      .not_thread_safe()
      .max_size(4)
      .build(mock_int_to_int_fn());

  c(1);
  c(2);
  c(3);
  c(4);
  c(5); // Expire '1' due to cache size limitation.

  CHECK_EQUAL(std::shared_ptr<int>(nullptr), c.get_if_present(1));
  CHECK(c.get_if_present(2) != nullptr);
  CHECK(c.get_if_present(3) != nullptr);
  CHECK(c.get_if_present(4) != nullptr);
  CHECK(c.get_if_present(5) != nullptr);
}
