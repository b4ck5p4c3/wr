#pragma once

#include "Common.hpp"

/* The generic threading interface, modeled on the HTTP and SQL backends. A
   concrete backend implements Mutex and Thread, and the rest of the code holds
   the interface. The pthread backend is in src/Pthread. */

namespace wr {

class Mutex
{
public:
  virtual ~Mutex() = default;

  virtual fn lock() noexcept -> void = 0;
  virtual fn unlock() noexcept -> void = 0;
};

class ScopedLock
{
public:
  explicit ScopedLock(Mutex &mutex) noexcept : m_mutex(mutex)
  {
    m_mutex.lock();
  }
  ~ScopedLock() noexcept { m_mutex.unlock(); }

  ScopedLock(const ScopedLock &) = delete;
  ScopedLock &operator=(const ScopedLock &) = delete;

private:
  Mutex &m_mutex;
};

class Thread
{
public:
  using EntryPoint = opaque *(*) (opaque *);

  virtual ~Thread() = default;

  // The return is zero on success or the backend error code, so the caller
  // translates it through strerror.
  mustuse virtual fn start(EntryPoint entry, opaque *argument) noexcept
      -> int = 0;
  virtual fn join() noexcept -> void = 0;
};

} // namespace wr
