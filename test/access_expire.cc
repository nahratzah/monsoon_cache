#include "cache_.h"

TEST(cache_access_expire) {
  using clock_type = std::chrono::steady_clock;

  cache<int, int> c = cache<int, int>::builder()
      .not_thread_safe()
      .access_expire(std::chrono::seconds(2))
      .build(mock_int_to_int_fn());

  const auto tp0 = clock_type::now();
  std::weak_ptr<int> ptr = c(4);

  std::this_thread::sleep_for(std::chrono::seconds(1));

  const auto tp1 = clock_type::now();
  {
    auto still_valid = c(4);
    REQUIRE CHECK(clock_type::now() - tp0 <= std::chrono::seconds(2));
    CHECK_EQUAL(ptr.lock(), still_valid);
  }

  std::this_thread::sleep_for(std::chrono::seconds(1));
  {
    auto still_valid = c(4);
    REQUIRE CHECK(clock_type::now() - tp1 <= std::chrono::seconds(2));
    CHECK_EQUAL(ptr.lock(), still_valid);
  }
}
