#include "Arena.hpp"

#include "Allocator.hpp"
#include "Trace.hpp"

namespace wr {

hot fn bump_arena_allocate(BumpArena *arena, usize length, usize alignment)
    -> opaque *
{
  return arena->allocate(length, alignment);
}

BumpArena::~BumpArena()
{
  for (const block &entry : m_blocks)
    std::free(entry.base);
}

cold fn BumpArena::add_block(usize minimum_size) noexcept -> void
{
  let size = DEFAULT_BLOCK_SIZE;
  if (minimum_size > size) size = minimum_size;

  let const base = static_cast<u8 *>(std::malloc(size));
  if (base == nullptr) std::abort();

  LOG(All, "mapping a new arena block of %zu bytes", size);

  m_blocks.push(block{base, size, 0});
}

hot fn BumpArena::allocate(usize size, usize alignment) noexcept -> opaque *
{
  loop
  {
    if (!m_blocks.is_empty()) {
      let &entry = m_blocks.back();
      let const aligned = (entry.used + (alignment - 1)) & ~(alignment - 1);

      if (aligned + size <= entry.size) [[likely]] {
        let const pointer = entry.base + aligned;
        entry.used = aligned + size;
        return pointer;
      }
    }

    add_block(size + alignment);
  }
}

cold fn BumpArena::reset() noexcept -> void
{
  LOG(All, "resetting the arena holding %zu blocks", m_blocks.count());

  while (m_blocks.count() > 1) {
    std::free(m_blocks.back().base);
    m_blocks.pop_back();
  }

  if (m_blocks.is_empty()) return;

  let &entry = m_blocks.front();
  if (entry.size > DEFAULT_BLOCK_SIZE) {
    std::free(entry.base);
    m_blocks.pop_back();
  } else {
    entry.used = 0;
  }
}

} // namespace wr
