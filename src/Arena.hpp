#pragma once

#include "Allocator.hpp"
#include "ArrayList.hpp"
#include "Common.hpp"

namespace wr {

/* A bump allocator for the request-to-reply cycle. Bytes are handed out from a
   growing list of blocks, and reset reclaims them all at once, so the per-reply
   scratch never takes and gives individual blocks from the pooled heap. The
   single-threaded event loop owns one arena and resets it between requests. */
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
