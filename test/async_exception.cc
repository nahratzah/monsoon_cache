#include "cache_.h"

struct async_shared_exception {};

TEST(cache_async_shares_exceptions) {
  std::promise<std::shared_ptr<int>> p;
  std::shared_future<std::shared_ptr<int>> p_future = p.get_future();
  std::mutex mtx;
  std::condition_variable signal;
  int visits = 0;

  cache<int, int> c = cache<int, int>::builder()
      .async(true)
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
  try {
    throw async_shared_exception();
  } catch (...) {
    p.set_exception(std::current_exception());
  }

  bool f1_exception = false;
  try {
    f1.get();
  } catch (const async_shared_exception& e) {
    f1_exception = true;
  }

  bool f2_exception = false;
  try {
    f2.get();
  } catch (const async_shared_exception& e) {
    f2_exception = true;
  }

  CHECK(f1_exception);
  CHECK(f2_exception);
  CHECK_EQUAL(1, visits);
}
