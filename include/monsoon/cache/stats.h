#ifndef MONSOON_CACHE_STATS_H
#define MONSOON_CACHE_STATS_H

///\file
///\ingroup cache_detail

#include <instrumentation/counter.h>
#include <instrumentation/tags.h>
#include <monsoon/cache/builder.h>
#include <cassert>
#include <string>
#include <sstream>

namespace monsoon::cache {


class stats_record {
 public:
  stats_record(const cache_builder_vars& vars) {
    if (!vars.stats()->name.empty()) {
      hits_ = instrumentation::counter("monsoon.cache.hit", make_tags(vars));
      misses_ = instrumentation::counter("monsoon.cache.miss", make_tags(vars));
      deletes_ = instrumentation::counter("monsoon.cache.delete", make_tags(vars));
    }
  }

  auto on_hit()
  noexcept
  -> void {
    ++hits_;
  }

  auto on_miss()
  noexcept
  -> void {
    ++misses_;
  }

  auto on_delete()
  noexcept
  -> void {
    ++deletes_;
  }

  static auto make_tags(const cache_builder_vars& vars)
  -> instrumentation::tags {
    instrumentation::tags t;
    t.with("name", vars.stats()->name);

    if (vars.stats()->tls) {
      std::ostringstream tid_stream;
      tid_stream << std::this_thread::get_id();
      t.with("thread", tid_stream.str());
    }
    return t;
  }

 private:
  instrumentation::counter hits_, misses_, deletes_;
};

class stats_decorator {
 public:
  stats_decorator(const cache_builder_vars& vars) noexcept {}

  auto set_stats_record(stats_record* record)
  noexcept
  -> void {
    record_ = record;
  }

  template<typename T>
  auto on_hit([[maybe_unused]] const T& elem)
  noexcept
  -> void {
    assert(record_ != nullptr);
    record_->on_hit();
  }

  template<typename T>
  auto on_create([[maybe_unused]] const T& elem)
  noexcept
  -> void {
    assert(record_ != nullptr);
    record_->on_miss();
  }

  template<typename T>
  auto on_delete([[maybe_unused]] const T& elem)
  noexcept
  -> void {
    assert(record_ != nullptr);
    record_->on_delete();
  }

 private:
  stats_record* record_ = nullptr;
};


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_STATS_H */
