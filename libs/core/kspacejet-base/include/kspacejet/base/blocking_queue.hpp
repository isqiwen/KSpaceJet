#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

namespace ksj::base {

template <typename T> class BlockingQueue {
public:
  void push(T value) {
    {
      std::lock_guard lock(mutex_);
      queue_.push(std::move(value));
    }
    condition_variable_.notify_one();
  }

  [[nodiscard]] bool empty() const {
    std::lock_guard lock(mutex_);
    return queue_.empty();
  }

  [[nodiscard]] bool try_pop(T& value) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
      return false;
    }

    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  void wait_and_pop(T& value) {
    std::unique_lock lock(mutex_);
    condition_variable_.wait(lock, [this] {
      return !queue_.empty();
    });

    value = std::move(queue_.front());
    queue_.pop();
  }

  template <typename Rep, typename Period>
  [[nodiscard]] bool wait_for_and_pop(T& value, const std::chrono::duration<Rep, Period>& timeout) {
    std::unique_lock lock(mutex_);
    if (!condition_variable_.wait_for(lock, timeout, [this] {
          return !queue_.empty();
        })) {
      return false;
    }

    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  void clear() {
    std::lock_guard lock(mutex_);
    std::queue<T> empty_queue;
    queue_.swap(empty_queue);
  }

private:
  std::queue<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable condition_variable_;
};

} // namespace ksj::base
