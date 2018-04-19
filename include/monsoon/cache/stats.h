#ifndef MONSOON_CACHE_STATS_H
#define MONSOON_CACHE_STATS_H

///\file
///\ingroup cache_detail

#include <instrumentation/group.h>
#include <instrumentation/counter.h>
#include <instrumentation/tags.h>
#include <monsoon/cache/builder.h>
#include <cassert>
#include <string>
#include <sstream>

namespace monsoon::cache {


class stats_record {
 public:
  stats_record(const cache_builder_vars& vars)
  : group_name_(vars.stats()->name),
    instrumentation_group(this->group_name_, vars.stats()->parent),
    hits_("hit", this->instrumentation_group, make_tags(vars)),
    misses_("miss", this->instrumentation_group, make_tags(vars)),
    deletes_("delete", this->instrumentation_group, make_tags(vars))
  {}

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
  -> instrumentation::tag_map {
    if (vars.stats()->tls) {
      const std::string tid = (std::ostringstream() << std::this_thread::get_id()).str();
      return { {instrumentation::tls_entry_key, std::move(tid)} };
    }
    return {};
  }

 private:
  const std::string group_name_;

 protected:
  instrumentation::tagged_group<0> instrumentation_group;

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
