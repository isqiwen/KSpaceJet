#pragma once

#include <memory>
#include <mutex>

namespace ksj::process_runtime {

class ChannelSynchronization {
public:
  class ScopedLock {
  public:
    ScopedLock(ScopedLock&&) noexcept = default;
    ScopedLock& operator=(ScopedLock&&) noexcept = default;
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;

  private:
    friend class ChannelSynchronization;

    explicit ScopedLock(std::mutex& mutex) : lock_(mutex) {}

    std::unique_lock<std::mutex> lock_;
  };

  ChannelSynchronization() : mutex_(std::make_shared<std::mutex>()) {}

  [[nodiscard]] ScopedLock acquire() const { return ScopedLock(*mutex_); }

private:
  std::shared_ptr<std::mutex> mutex_;
};

} // namespace ksj::process_runtime
