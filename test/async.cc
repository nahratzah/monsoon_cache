#include "cache_.h"

TEST(cache_async) {
  std::promise<std::shared_ptr<int>> p;
  std::shared_future<std::shared_ptr<int>> p_future = p.get_future();
  std::mutex mtx;
  std::condition_variable signal;
  int visits = 0;

  cache<int, int> c = cache<int, int>::builder()
      .enable_async()
      .build(
          [&]([[maybe_unused]] auto alloc, int i) {
            REQUIRE CHECK_EQUAL(1, i);
            std::lock_guard<std::mutex> lck{ mtx };
            visits++;
            signal.notify_all();
            return p_future;
          });

  // Start 2 async threads that query the cache.
  std::unique_lock<std::mutex> lck{ mtx };
  auto f1 = std::async(std::launch::async, c, 1);
  auto f2 = std::async(std::launch::async, c, 1);
  std::this_thread::sleep_for(std::chrono::seconds(1)); // Allow threads to spin up.
  // Wait until both threads have acquired the future of p.
  signal.wait(lck, [&]() { return visits != 0; });

  // Publish result, now that both cache access are blocked on future.
  auto result = std::make_shared<int>(17);
  p.set_value(result);

  // Check that outcomes of async threads match up.
  CHECK_EQUAL(result, f1.get());
  CHECK_EQUAL(result, f2.get());
  CHECK_EQUAL(1, visits);
}
