#include "cache_.h"

TEST(cache_expire) {
  cache<int, int> c = cache<int, int>::builder()
      .not_thread_safe()
      .build(mock_int_to_int_fn());

  auto ptr = c(4);
  REQUIRE CHECK_EQUAL(ptr, c(4));

  c.expire(4);
  CHECK(ptr != c(4)); // Reload must be performed.
}
