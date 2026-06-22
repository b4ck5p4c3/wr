#pragma once

#include "Common.hpp"

#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace wr {

class BumpArena;
fn bump_arena_allocate(BumpArena *arena, usize length, usize alignment)
    -> opaque *;

class Allocator
{
public:
  struct VTable
  {
    opaque *(*alloc)(opaque *context, usize length, usize alignment);
    bool (*resize)(opaque *context, opaque *pointer, usize old_length,
                   usize new_length, usize alignment);
    void (*free)(opaque *context, opaque *pointer, usize length,
                 usize alignment);
  };

  opaque *context;
  const VTable *vtable;

  hot flatten fn raw_alloc(usize length, usize alignment) const -> opaque *
  {
    return vtable->alloc(context, length, alignment);
  }
  fn raw_resize(opaque *pointer, usize old_length, usize new_length,
                usize alignment) const noexcept -> bool
  {
    return vtable->resize(context, pointer, old_length, new_length, alignment);
  }
  flatten fn raw_free(opaque *pointer, usize length,
                      usize alignment) const noexcept -> void
  {
    vtable->free(context, pointer, length, alignment);
  }

  template <class T>
  hot flatten fn alloc_array(usize count) const -> T *
  {
    if (sizeof(T) != 0 && count > (static_cast<usize>(-1) / sizeof(T)))
        [[unlikely]]
    {
      std::abort();
    }
    return static_cast<T *>(raw_alloc(count * sizeof(T), alignof(T)));
  }
  template <class T>
  flatten fn free_array(T *pointer, usize count) const noexcept -> void
  {
    raw_free(pointer, count * sizeof(T), alignof(T));
  }
};

namespace allocators {

hot inline fn bump_alloc(opaque *context, usize length, usize alignment)
    -> opaque *
{
  return bump_arena_allocate(static_cast<BumpArena *>(context), length,
                             alignment);
}
inline fn bump_resize(opaque *context, opaque *pointer, usize old_length,
                      usize new_length, usize alignment) noexcept -> bool
{
  unused(context);
  unused(pointer);
  unused(old_length);
  unused(new_length);
  unused(alignment);
  return false;
}
inline fn bump_free(opaque *context, opaque *pointer, usize length,
                    usize alignment) noexcept -> void
{
  unused(context);
  unused(pointer);
  unused(length);
  unused(alignment);
}

inline constexpr Allocator::VTable BUMP_VTABLE{bump_alloc, bump_resize,
                                               bump_free};

struct heap_pool
{
  static constexpr usize MIN_CLASS_SHIFT =
      4; /* the smallest class is 16 bytes */
  static constexpr usize MAX_CLASS_SHIFT =
      16; /* the largest pooled block is 64 KiB */
  static constexpr usize CLASS_COUNT = MAX_CLASS_SHIFT - MIN_CLASS_SHIFT + 1;
  static constexpr usize MAX_BLOCKS_PER_CLASS = 512;

  struct node
  {
    node *next;
  };

  node *bins[CLASS_COUNT] = {};
  u32 counts[CLASS_COUNT] = {};

  hot static fn class_shift_for(usize length) noexcept -> usize
  {
    let const size = length <= (usize{1} << MIN_CLASS_SHIFT)
                         ? (usize{1} << MIN_CLASS_SHIFT)
                         : length;
    let shift = static_cast<usize>(64 - __builtin_clzll(size - 1));
    if (shift < MIN_CLASS_SHIFT) shift = MIN_CLASS_SHIFT;
    return shift;
  }

  hot fn take(usize length) noexcept -> opaque *
  {
    let const shift = class_shift_for(length);
    if (shift > MAX_CLASS_SHIFT) return std::malloc(length);

    let const class_index = shift - MIN_CLASS_SHIFT;
    if (bins[class_index] != nullptr) {
      let const reused = bins[class_index];
      bins[class_index] = reused->next;
      counts[class_index]--;
      return reused;
    }

    return std::malloc(usize{1} << shift);
  }

  hot fn give(opaque *pointer, usize length) noexcept -> void
  {
    if (pointer == nullptr) return;

    let const shift = class_shift_for(length);
    if (shift > MAX_CLASS_SHIFT) {
      std::free(pointer);
      return;
    }

    let const class_index = shift - MIN_CLASS_SHIFT;
    if (counts[class_index] >= MAX_BLOCKS_PER_CLASS) {
      std::free(pointer);
      return;
    }

    let const recycled = static_cast<node *>(pointer);
    recycled->next = bins[class_index];
    bins[class_index] = recycled;
    counts[class_index]++;
  }
};

hot inline fn heap_pool_instance() noexcept -> heap_pool &
{
  static heap_pool pool;
  return pool;
}

hot inline fn heap_alloc(opaque *context, usize length,
                         usize alignment) noexcept -> opaque *
{
  unused(context);
  if (alignment > alignof(max_align_t)) {
    let const rounded_length = (length + alignment - 1) & ~(alignment - 1);
#if defined(_WIN32)
    return _aligned_malloc(rounded_length, alignment);
#else
    return std::aligned_alloc(alignment, rounded_length);
#endif
  }
  return heap_pool_instance().take(length);
}
inline fn heap_resize(opaque *context, opaque *pointer, usize old_length,
                      usize new_length, usize alignment) noexcept -> bool
{
  unused(context);
  unused(pointer);
  unused(old_length);
  unused(new_length);
  unused(alignment);
  return false;
}
hot inline fn heap_free(opaque *context, opaque *pointer, usize length,
                        usize alignment) noexcept -> void
{
  unused(context);
  if (alignment > alignof(max_align_t)) {
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
    return;
  }
  heap_pool_instance().give(pointer, length);
}

inline constexpr Allocator::VTable HEAP_VTABLE{heap_alloc, heap_resize,
                                               heap_free};

} // namespace allocators

inline fn bump_allocator(BumpArena &arena) noexcept -> Allocator
{
  return Allocator{&arena, &allocators::BUMP_VTABLE};
}

inline fn heap_allocator() noexcept -> Allocator
{
  return Allocator{nullptr, &allocators::HEAP_VTABLE};
}

} // namespace wr
