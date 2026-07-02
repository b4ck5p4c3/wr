#pragma once

#include "Allocator.hpp"
#include "ArrayList.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace wr {

template <class Handle>
class StatementCache
{
public:
  explicit StatementCache(Allocator allocator)
      : m_allocator(allocator), m_entries(allocator)
  {}

  template <class Create, class Destroy>
  mustuse fn acquire(StringView sql, Create do_create, Destroy do_destroy)
      -> ErrorOr<Handle>
  {
    m_use_count++;

    for (entry &cached : m_entries)
      if (cached.sql.view() == sql) {
        cached.last_used_count = m_use_count;
        return cached.handle;
      }

    let const handle = TRY(do_create(sql));

    if (m_entries.count() < CAPACITY) {
      m_entries.push(entry{
          String{m_allocator, sql},
          handle, m_use_count
      });
      return handle;
    }

    usize evicted_index = 0;
    for (usize i = 1; i < m_entries.count(); i++)
      if (m_entries[i].last_used_count <
          m_entries[evicted_index].last_used_count)
        evicted_index = i;

    do_destroy(m_entries[evicted_index].handle);
    m_entries[evicted_index] = entry{
        String{m_allocator, sql},
        handle, m_use_count
    };
    return handle;
  }

  template <class Destroy>
  fn clear(Destroy do_destroy) noexcept -> void
  {
    for (entry &cached : m_entries)
      do_destroy(cached.handle);

    m_entries.clear();
  }

private:
  static constexpr usize CAPACITY = 32;

  struct entry
  {
    String sql;
    Handle handle;
    u64 last_used_count;
  };

  Allocator m_allocator;
  ArrayList<entry> m_entries;
  u64 m_use_count{0};
};

} // namespace wr
