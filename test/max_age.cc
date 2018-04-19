#include "cache_.h"

TEST(cache_max_age) {
  cache<int, int> c = cache<int, int>::builder()
      .not_thread_safe()
      .max_age(std::chrono::seconds(1))
      .build(mock_int_to_int_fn());

  auto ptr = c(4);
  REQUIRE CHECK_EQUAL(ptr, c(4));

  std::this_thread::sleep_for(std::chrono::seconds(2));
  CHECK(ptr != c(4)); // Reload must be performed.
}
