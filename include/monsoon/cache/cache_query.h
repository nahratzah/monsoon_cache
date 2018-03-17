#ifndef MONSOON_CACHE_CACHE_QUERY_H
#define MONSOON_CACHE_CACHE_QUERY_H

///\file
///\ingroup cache_detail

#include <cstddef>
#include <type_traits>
#include <utility>

namespace monsoon::cache {


/**
 * \brief Internal query description for the cache.
 * \ingroup cache_detail
 */
template<typename Predicate, typename TplBuilder, typename Create>
struct cache_query {
  cache_query(std::size_t hash_code, Predicate predicate, TplBuilder tpl_builder, Create create)
  noexcept(std::is_nothrow_move_constructible_v<Predicate>
      && std::is_nothrow_move_constructible_v<TplBuilder>
      && std::is_nothrow_move_constructible_v<Create>)
  : hash_code(hash_code),
    predicate(std::move(predicate)),
    tpl_builder(std::move(tpl_builder)),
    create(std::move(create))
  {}

  std::size_t hash_code;
  Predicate predicate;
  TplBuilder tpl_builder;
  Create create;
};

/**
 * \brief Create a cache_query from the given arguments.
 * \ingroup cache_detail
 * \param[in] hash_code The hash code of the cache key.
 * \param[in] predicate An equality predicate for the cache key.
 * \param[in] tpl_builder Functor that retrieves the initialization tuple for the key.
 * \param[in] create Constructor functor that creates the mapped type of the cache.
 * \returns A cache query, which is used by cache_impl to perform lookups.
 */
template<typename Predicate, typename TplBuilder, typename Create>
auto make_cache_query(std::size_t hash_code, Predicate&& predicate, TplBuilder&& tpl_builder, Create&& create)
-> cache_query<std::decay_t<Predicate>, std::decay_t<TplBuilder>, std::decay_t<Create>> {
  return {
    hash_code,
    std::forward<Predicate>(predicate),
    std::forward<TplBuilder>(tpl_builder),
    std::forward<Create>(create)
  };
}


} /* namespace monsoon::cache */

#endif /* MONSOON_CACHE_CACHE_QUERY_H */
