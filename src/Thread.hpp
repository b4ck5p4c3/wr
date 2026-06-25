#pragma once

#include "Common.hpp"

#include <pthread.h>

/* The only place pthread is named. A port to another threading backend swaps
   this header and nothing else, since the rest of the code talks to Mutex,
   ScopedLock, and Thread. */

namespace wr {

class Mutex
{
public:
  Mutex() noexcept { pthread_mutex_init(&m_handle, nullptr); }
  ~Mutex() noexcept { pthread_mutex_destroy(&m_handle); }

  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;

  fn lock() noexcept -> void { pthread_mutex_lock(&m_handle); }
  fn unlock() noexcept -> void { pthread_mutex_unlock(&m_handle); }

private:
  pthread_mutex_t m_handle{};
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

  Thread() noexcept = default;
  ~Thread() noexcept = default;

  Thread(const Thread &) = delete;
  Thread &operator=(const Thread &) = delete;

  // The return is zero on success or the pthread error code, so the caller
  // translates it through strerror.
  mustuse fn start(EntryPoint entry, opaque *argument) noexcept -> int
  {
    let const code = pthread_create(&m_handle, nullptr, entry, argument);
    if (code == 0) m_is_started = true;
    return code;
  }

  fn join() noexcept -> void
  {
    if (!m_is_started) return;
    pthread_join(m_handle, nullptr);
    m_is_started = false;
  }

private:
  pthread_t m_handle{};
  bool m_is_started{false};
};

} // namespace wr
