#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "PackedStringKey.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace wr {

/* An open-addressing hash table from a string key to a Value over an explicit
   allocator. Each slot caches a PackedStringKey of the key's leading bytes, so
   a probe rejects a mismatch with a block compare before the full byte compare.
   Linear probing, power-of-two capacity, grows past a load of three quarters,
   and counts tombstones toward the load so an insert is never dropped. */
template <class Value = String>
class StringMap
{
public:
  explicit StringMap(Allocator allocator) : m_allocator(allocator) {}

  cold StringMap(const StringMap &other) : m_allocator(other.m_allocator)
  {
    /* An empty source allocates no bucket array, so the copy stays free until
       the first insert grows it. */
    if (other.m_count == 0) return;
    rehash(other.m_capacity);
    for (usize i = 0; i < other.m_capacity; i++) {
      if (other.m_slots[i].state == slot::Occupied)
        set_value(other.m_slots[i].key.view(), Value{other.m_slots[i].value});
    }
  }

  mustuse cold fn clone() const -> StringMap { return StringMap{*this}; }

  StringMap(StringMap &&other) noexcept
      : m_allocator(other.m_allocator), m_slots(other.m_slots),
        m_capacity(other.m_capacity), m_count(other.m_count),
        m_tombstones(other.m_tombstones)
  {
    other.m_slots = nullptr;
    other.m_capacity = 0;
    other.m_count = 0;
    other.m_tombstones = 0;
  }
  fn operator=(StringMap &&other) noexcept -> StringMap &
  {
    if (this != &other) {
      destroy_all();
      m_allocator = other.m_allocator;
      m_slots = other.m_slots;
      m_capacity = other.m_capacity;
      m_count = other.m_count;
      m_tombstones = other.m_tombstones;
      other.m_slots = nullptr;
      other.m_capacity = 0;
      other.m_count = 0;
      other.m_tombstones = 0;
    }
    return *this;
  }
  cold fn operator=(const StringMap &other)->StringMap &
  {
    if (this != &other) {
      StringMap copy{other};
      *this = steal(copy);
    }
    return *this;
  }

  ~StringMap() { destroy_all(); }

  mustuse pure fn count() const noexcept -> usize { return m_count; }

  /* Grow the table up front to hold at least expected_count entries without a
     later rehash. The capacity is sized for the three-quarter load and rounded
     up to a power of two. */
  cold fn reserve(usize expected_count) -> void
  {
    let const needed = (expected_count * 4 / 3) + 1;
    usize new_capacity = m_capacity == 0 ? 16 : m_capacity;
    while (new_capacity < needed)
      new_capacity *= 2;

    if (new_capacity > m_capacity) rehash(new_capacity);
  }

  /* The value for the key, or nullptr when absent. The pointer is stable until
     the next set that grows the table. */
  hot mustuse pure fn find(StringView key) const noexcept -> const Value *
  {
    if (m_capacity == 0) return nullptr;
    let const wanted = PackedStringKey::from_view(key);
    let const mask = m_capacity - 1;
    let i = hash_bytes(key) & mask;
    for (usize probe = 0; probe < m_capacity; probe++) {
      let const &slot = m_slots[i];
      if (slot.state == slot::Empty) return nullptr;
      /* The packed compare holds the leading zero-filled bytes, so a key inside
         the capacity proves equality through the pack and the length alone, and
         a longer key still confirms through the byte compare. */
      if (slot.state == slot::Occupied && slot.packed == wanted &&
          (key.count() <= PackedStringKey::BYTE_CAPACITY
               ? slot.key.count() == key.count()
               : slot.key == key)) [[likely]]
      {
        return &slot.value;
      }
      i = (i + 1) & mask;
    }
    return nullptr;
  }

  hot flatten mustuse fn find(StringView key) noexcept -> Value *
  {
    return const_cast<Value *>(static_cast<const StringMap *>(this)->find(key));
  }

  mustuse pure fn allocator() const noexcept -> Allocator
  {
    return m_allocator;
  }

  hot fn set(StringView key, Value value) -> void
  {
    set_value(key, steal(value));
  }

  /* The value for a key, inserting the supplied default when the key is absent,
     then returning a mutable reference valid until the next set that grows. */
  hot fn get_or_create(StringView key, Value default_value) -> Value &
  {
    if (const Value *existing = find(key))
      return *const_cast<Value *>(existing);
    return *set_value(key, steal(default_value));
  }

  /* Store a String value built from a view. An existing slot reuses its buffer,
     so a tight reassignment loop pays no per-turn allocation. A slot that once
     held a large value and now takes a far smaller one is rebuilt at the right
     size so the old value does not pin its memory. */
  hot fn set(StringView key, StringView value) -> void
  {
    if (Value *existing = find(key)) {
      let const buffer_is_wasteful =
          existing->count() > 256 && value.length < existing->count() / 2;
      if (!buffer_is_wasteful) {
        existing->clear();
        existing->append(value);
        return;
      }
    }
    set_value(key, String{m_allocator, value});
  }

  hot fn erase(StringView key) -> void
  {
    if (m_capacity == 0) return;
    let const wanted = PackedStringKey::from_view(key);
    let const mask = m_capacity - 1;
    let i = hash_bytes(key) & mask;
    for (usize probe = 0; probe < m_capacity; probe++) {
      let &slot = m_slots[i];
      if (slot.state == slot::Empty) return;
      if (slot.state == slot::Occupied && slot.packed == wanted &&
          (key.count() <= PackedStringKey::BYTE_CAPACITY
               ? slot.key.count() == key.count()
               : slot.key == key))
      {
        /* The slot objects are kept alive, so a later insert into this
           tombstone assigns into a live object. */
        slot.key = String{m_allocator};
        slot.value = Value{};
        slot.state = slot::Tombstone;
        m_count--;
        m_tombstones++;
        return;
      }
      i = (i + 1) & mask;
    }
  }

  template <class Fn>
  fn for_each(Fn callback) const -> void
  {
    for (usize i = 0; i < m_capacity; i++) {
      if (m_slots[i].state == slot::Occupied)
        callback(m_slots[i].key.view(), m_slots[i].value);
    }
  }

  fn clear() noexcept -> void { destroy_all(); }

private:
  struct slot
  {
    enum State : u8
    {
      Empty,
      Occupied,
      Tombstone,
    };
    State state{Empty};
    PackedStringKey packed{};
    String key{};
    Value value{};
  };

  hot fn set_value(StringView key, Value value) -> Value *
  {
    /* Tombstones count toward the load, so the table rehashes before a probe
       chain fills with deleted slots. An Empty slot stays reachable on every
       chain and an insert is never dropped. */
    if (m_count + m_tombstones + 1 > (m_capacity >> 1) + (m_capacity >> 2))
        [[unlikely]]
      rehash(m_capacity == 0 ? 16 : m_capacity * 2);

    let const wanted = PackedStringKey::from_view(key);
    let const mask = m_capacity - 1;
    let i = hash_bytes(key) & mask;
    let first_tombstone = m_capacity;
    for (usize probe = 0; probe < m_capacity; probe++) {
      let &slot = m_slots[i];
      if (slot.state == slot::Occupied && slot.packed == wanted &&
          (key.count() <= PackedStringKey::BYTE_CAPACITY
               ? slot.key.count() == key.count()
               : slot.key == key))
      {
        slot.value = steal(value);
        return &slot.value;
      }
      if (slot.state == slot::Empty) {
        let const target = first_tombstone != m_capacity ? first_tombstone : i;
        if (first_tombstone != m_capacity) m_tombstones--;
        return place(target, key, wanted, steal(value));
      }
      if (slot.state == slot::Tombstone && first_tombstone == m_capacity) {
        first_tombstone = i;
      }
      i = (i + 1) & mask;
    }

    /* The tombstone-aware load above prevents a full chain, but a found
       tombstone is reused rather than the insertion lost. */
    if (first_tombstone != m_capacity) {
      m_tombstones--;
      return place(first_tombstone, key, wanted, steal(value));
    }
    unreachable();
  }

  fn place(usize index, StringView key, const PackedStringKey &packed,
           Value value) -> Value *
  {
    let &slot = m_slots[index];
    slot.key = String{m_allocator, key};
    slot.packed = packed;
    slot.value = steal(value);
    slot.state = slot::Occupied;
    m_count++;
    return &slot.value;
  }

  cold fn rehash(usize new_capacity) -> void
  {
    let old_slots = m_slots;
    let const old_capacity = m_capacity;

    m_slots = m_allocator.alloc_array<slot>(new_capacity);
    for (usize i = 0; i < new_capacity; i++)
      new (&m_slots[i]) slot{};
    m_capacity = new_capacity;
    m_count = 0;
    m_tombstones = 0;

    for (usize i = 0; i < old_capacity; i++) {
      if (old_slots[i].state == slot::Occupied)
        set_value(old_slots[i].key.view(), steal(old_slots[i].value));
      old_slots[i].~slot();
    }
    if (old_slots != nullptr) m_allocator.free_array(old_slots, old_capacity);
  }

  cold fn destroy_all() noexcept -> void
  {
    for (usize i = 0; i < m_capacity; i++)
      m_slots[i].~slot();
    if (m_slots != nullptr) m_allocator.free_array(m_slots, m_capacity);
    m_slots = nullptr;
    m_capacity = 0;
    m_count = 0;
    m_tombstones = 0;
  }

  Allocator m_allocator;
  slot *m_slots{nullptr};
  usize m_capacity{0};
  usize m_count{0};
  usize m_tombstones{0};
};

} // namespace wr
