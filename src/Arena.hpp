#pragma once

#include "Allocator.hpp"
#include "ArrayList.hpp"
#include "Common.hpp"

namespace wr {

class BumpArena
{
public:
  BumpArena() = default;
  ~BumpArena();

  BumpArena(const BumpArena &) = delete;
  BumpArena &operator=(const BumpArena &) = delete;

  hot fn allocate(usize size, usize alignment) noexcept -> opaque *;
  cold fn reset() noexcept -> void;

private:
  struct block
  {
    u8 *base;
    usize size;
    usize used;
  };

  static constexpr usize DEFAULT_BLOCK_SIZE = usize{64} * 1024;

  ArrayList<block> m_blocks{heap_allocator()};

  cold fn add_block(usize minimum_size) noexcept -> void;
};

} // namespace wr
