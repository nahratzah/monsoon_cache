#include <monsoon/cache/cache.h>
#include <monsoon/cache/impl.h>
#include <chrono>
#include <memory>
#include <vector>
#include <future>
#include <thread>
#include "UnitTest++/UnitTest++.h"

using namespace monsoon::cache;

struct mock_int_to_int_fn {
  template<typename Alloc>
  auto operator()([[maybe_unused]] Alloc alloc, int i) const
  noexcept
  -> int {
    return 2 * i;
  }
};

// HACKS
namespace std {

template<typename T, typename A>
auto operator<<(std::ostream& o, const std::vector<T, A>& v)
-> std::ostream& {
  bool first = true;
  for (const auto& e : v)
    o << (std::exchange(first, false) ? "[ " : ", ") << e;
  return o << (std::exchange(first, false) ? "[]" : " ]");
}

}

int main() {
  return UnitTest::RunAllTests();
}
