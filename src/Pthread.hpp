#pragma once

#include "Common.hpp"
#include "Thread.hpp"

#include <pthread.h>

/* The pthread backend of the threading interface, the only place pthread is
   named. */

namespace wr {

class PthreadMutex final : public Mutex
{
public:
  PthreadMutex() noexcept { pthread_mutex_init(&m_handle, nullptr); }
  ~PthreadMutex() noexcept override { pthread_mutex_destroy(&m_handle); }

  PthreadMutex(const PthreadMutex &) = delete;
  PthreadMutex &operator=(const PthreadMutex &) = delete;

  fn lock() noexcept -> void override { pthread_mutex_lock(&m_handle); }
  fn unlock() noexcept -> void override { pthread_mutex_unlock(&m_handle); }

private:
  pthread_mutex_t m_handle{};
};

class PthreadThread final : public Thread
{
public:
  PthreadThread() noexcept = default;
  ~PthreadThread() noexcept override = default;

  PthreadThread(const PthreadThread &) = delete;
  PthreadThread &operator=(const PthreadThread &) = delete;

  mustuse fn start(EntryPoint entry, opaque *argument) noexcept -> int override
  {
    let const code = pthread_create(&m_handle, nullptr, entry, argument);
    if (code == 0) m_is_started = true;
    return code;
  }

  fn join() noexcept -> void override
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
