#include "kspacejet/process_runtime/message_loop.hpp"

#include "kspacejet/crash/handler.hpp"

#include <chrono>
#include <limits>
#include <utility>

namespace ksj::process_runtime {

MessageLoop::~MessageLoop() {
  (void)stop(std::chrono::seconds(10));
}

void MessageLoop::set_title(std::string title) {
  std::lock_guard lock(mutex_);
  title_ = std::move(title);
}

const std::string& MessageLoop::title() const noexcept {
  return title_;
}

bool MessageLoop::start() {
  std::lock_guard lock(mutex_);
  if (running_ || worker_.joinable()) {
    return false;
  }

  worker_ = std::jthread([this](std::stop_token stop_token) {
    run(stop_token);
  });
  return true;
}

bool MessageLoop::stop(std::chrono::milliseconds timeout) {
  {
    std::lock_guard lock(mutex_);
    if (worker_.joinable()) {
      worker_.request_stop();
    }
  }
  cv_.notify_all();

  auto deadline = std::chrono::steady_clock::now() + timeout;
  std::unique_lock lock(mutex_);
  while (running_) {
    if (stopped_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
      return false;
    }
  }
  lock.unlock();

  if (worker_.joinable()) {
    worker_.join();
  }
  return true;
}

void MessageLoop::request_stop() noexcept {
  {
    std::lock_guard lock(mutex_);
    if (worker_.joinable()) {
      worker_.request_stop();
    }
  }
  cv_.notify_all();
}

bool MessageLoop::is_running() const noexcept {
  std::lock_guard lock(mutex_);
  return running_;
}

bool MessageLoop::stop_requested() const noexcept {
  std::lock_guard lock(mutex_);
  return worker_.joinable() && worker_.get_stop_token().stop_requested();
}

bool MessageLoop::wait_until_idle(std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::unique_lock lock(mutex_);
  return idle_cv_.wait_until(lock, deadline, [this] {
    return queue_.empty() && active_message_count_ == 0;
  });
}

bool MessageLoop::enqueue(std::unique_ptr<QueuedMessageBase> message) {
  {
    std::lock_guard lock(mutex_);
    queue_.push_back(std::move(message));
  }
  cv_.notify_one();
  return true;
}

int MessageLoop::queued_message_count() const {
  std::lock_guard lock(mutex_);
  if (queue_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(queue_.size());
}

int MessageLoop::processed_message_count() const noexcept {
  std::lock_guard lock(mutex_);
  return processed_message_count_;
}

bool MessageLoop::handle_queued_message(std::type_index, void*) {
  return true;
}

void MessageLoop::run(std::stop_token stop_token) {
  std::string thread_title;
  {
    std::lock_guard lock(mutex_);
    thread_title = title_;
    running_ = true;
  }
  ksj::crash::ScopedThreadRegistration crash_thread(thread_title);
  stopped_cv_.notify_all();
  on_loop_started();

  for (;;) {
    std::unique_ptr<QueuedMessageBase> queued;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this, &stop_token] {
        return stop_token.stop_requested() || !queue_.empty();
      });
      if (stop_token.stop_requested() && queue_.empty()) {
        break;
      }
      queued = std::move(queue_.front());
      queue_.pop_front();
      ++active_message_count_;
    }

    const bool handled = queued->dispatch(*this);
    {
      std::lock_guard lock(mutex_);
      ++processed_message_count_;
      --active_message_count_;
      if (queue_.empty() && active_message_count_ == 0) {
        idle_cv_.notify_all();
      }
    }
    if (queued->completion) {
      queued->completion->set_value(handled);
    }
    if (!handled) {
      request_stop();
    }
  }

  on_loop_stopped();
  {
    std::lock_guard lock(mutex_);
    running_ = false;
  }
  stopped_cv_.notify_all();
}

} // namespace ksj::process_runtime
