#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "PackedStringKey.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace wr {

/* An open-addressing hash table from a string key to a Value over an explicit
   allocator. A slot caches a PackedStringKey, so a probe rejects a mismatch
   before the full byte compare. Tombstones count toward the load. */
template <class Value = String>
class StringMap
{
public:
  explicit StringMap(Allocator allocator) : m_allocator(allocator) {}

  cold StringMap(const StringMap &other) : m_allocator(other.m_allocator)
  {
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
    other.forget_storage();
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
      other.forget_storage();
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

  cold fn reserve(usize expected_count) -> void
  {
    let const needed = (expected_count * 4 / 3) + 1;
    usize new_capacity = m_capacity == 0 ? 16 : m_capacity;
    while (new_capacity < needed)
      new_capacity *= 2;

    if (new_capacity > m_capacity) rehash(new_capacity);
  }

  /* nullptr when absent. The pointer is stable until the next growing set. */
  hot mustuse pure fn find(StringView key) const noexcept -> const Value *
  {
    if (m_capacity == 0) return nullptr;
    let const wanted = PackedStringKey::from_view(key);
    let const mask = m_capacity - 1;
    let i = hash_bytes(key) & mask;
    for (usize probe = 0; probe < m_capacity; probe++) {
      let const &slot = m_slots[i];
      if (slot.state == slot::Empty) return nullptr;
      /* A key inside the capacity is proven equal by the pack and the length, a
         longer key confirms through the byte compare. */
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

  hot fn get_or_create(StringView key, Value default_value) -> Value &
  {
    if (const Value *existing = find(key); existing != nullptr)
      return *const_cast<Value *>(existing);
    return *set_value(key, steal(default_value));
  }

  /* An existing slot reuses its buffer, so a reassignment loop pays no
     allocation. A slot holding a far larger value is rebuilt so it does not pin
     memory. */
  hot fn set(StringView key, StringView value) -> void
  {
    if (Value *existing = find(key); existing != nullptr) {
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
        /* The slot objects stay alive, so a later insert assigns into them. */
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
    /* Tombstones count toward the load, so a rehash happens before a probe
       chain fills with deleted slots and an insert is never dropped. */
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
    forget_storage();
  }

  fn forget_storage() noexcept -> void
  {
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
