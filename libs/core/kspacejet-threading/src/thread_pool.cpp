#include "kspacejet/threading/thread_pool.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

#if defined(__linux__)
#include <sched.h>
#endif

namespace ksj::threading {

namespace {

std::size_t normalize_worker_count(std::size_t worker_count) noexcept {
  return std::max<std::size_t>(1, worker_count);
}

thread_local std::optional<std::size_t> tls_worker_index;

[[nodiscard]] std::optional<std::size_t> current_cpu() noexcept {
#if defined(__linux__)
  const auto cpu = ::sched_getcpu();
  if (cpu < 0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(cpu);
#else
  return std::nullopt;
#endif
}

void bind_current_thread_to_cpu(const std::size_t cpu_id) noexcept {
#if defined(__linux__)
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu_id, &mask);
  static_cast<void>(::sched_setaffinity(0, sizeof(mask), &mask));
#else
  (void)cpu_id;
#endif
}

} // namespace

ThreadPool::ThreadPool(std::size_t worker_count, ThreadPoolOptions options) : options_(std::move(options)) {
  std::lock_guard lock(mutex_);
  add_workers_locked(normalize_worker_count(worker_count));
}

ThreadPool::~ThreadPool() {
  shutdown();
}

std::size_t ThreadPool::default_worker_count() noexcept {
  const auto worker_count = std::thread::hardware_concurrency();
  return worker_count == 0 ? 1 : worker_count;
}

std::optional<std::size_t> ThreadPool::current_worker_index() noexcept {
  return tls_worker_index;
}

bool ThreadPool::enqueue(Task task) {
  if (!task) {
    return false;
  }

  {
    std::lock_guard lock(mutex_);
    if (!accepting_tasks_) {
      return false;
    }

    tasks_.push_back(std::move(task));
  }

  task_available_.notify_one();
  return true;
}

void ThreadPool::resize(std::size_t worker_count) {
  std::vector<std::jthread> removed_workers;
  worker_count = normalize_worker_count(worker_count);

  {
    std::lock_guard lock(mutex_);
    if (!accepting_tasks_ || shutdown_requested_) {
      return;
    }

    const auto current_worker_count = workers_.size();
    if (worker_count > current_worker_count) {
      add_workers_locked(worker_count - current_worker_count);
      return;
    }

    if (worker_count < current_worker_count) {
      const auto first_removed = workers_.begin() + static_cast<std::ptrdiff_t>(worker_count);
      std::vector<std::size_t> removed_worker_indices;
      for (auto it = first_removed; it != workers_.end(); ++it) {
        it->thread.request_stop();
        removed_worker_indices.push_back(it->worker_index);
      }
      for (auto it = first_removed; it != workers_.end(); ++it) {
        removed_workers.push_back(std::move(it->thread));
      }
      workers_.erase(first_removed, workers_.end());
      worker_infos_.erase(std::remove_if(worker_infos_.begin(), worker_infos_.end(),
                                         [&removed_worker_indices](const auto& info) {
                                           return std::find(removed_worker_indices.begin(),
                                                            removed_worker_indices.end(),
                                                            info.worker_index) != removed_worker_indices.end();
                                         }),
                          worker_infos_.end());
    }
  }

  task_available_.notify_all();
}

void ThreadPool::clear_pending() {
  {
    std::lock_guard lock(mutex_);
    tasks_.clear();
    if (idle_locked()) {
      idle_.notify_all();
    }
  }
}

void ThreadPool::wait() const {
  std::unique_lock lock(mutex_);
  idle_.wait(lock, [this] {
    return idle_locked();
  });
}

bool ThreadPool::wait_until(std::chrono::steady_clock::time_point deadline) const {
  std::unique_lock lock(mutex_);
  return idle_.wait_until(lock, deadline, [this] {
    return idle_locked();
  });
}

void ThreadPool::shutdown(ShutdownPolicy policy) noexcept {
  std::vector<std::jthread> workers;

  {
    std::lock_guard lock(mutex_);
    if (shutdown_requested_ && workers_.empty()) {
      return;
    }

    accepting_tasks_ = false;
    if (policy == ShutdownPolicy::discard_pending || workers_.empty()) {
      tasks_.clear();
    }
  }

  if (policy == ShutdownPolicy::finish_pending) {
    wait();
  }

  {
    std::lock_guard lock(mutex_);
    shutdown_requested_ = true;
    if (policy == ShutdownPolicy::discard_pending) {
      tasks_.clear();
    }
    workers.reserve(workers_.size());
    for (auto& worker : workers_) {
      workers.push_back(std::move(worker.thread));
    }
    workers_.clear();
    worker_infos_.clear();
  }

  for (auto& worker : workers) {
    worker.request_stop();
  }
  task_available_.notify_all();
}

std::size_t ThreadPool::worker_count() const {
  std::lock_guard lock(mutex_);
  return workers_.size();
}

std::size_t ThreadPool::active_count() const {
  std::lock_guard lock(mutex_);
  return active_;
}

std::size_t ThreadPool::queued_count() const {
  std::lock_guard lock(mutex_);
  return tasks_.size();
}

bool ThreadPool::idle() const {
  std::lock_guard lock(mutex_);
  return idle_locked();
}

bool ThreadPool::accepting_tasks() const {
  std::lock_guard lock(mutex_);
  return accepting_tasks_;
}

std::size_t ThreadPool::unhandled_exception_count() const {
  std::lock_guard lock(mutex_);
  return unhandled_exception_count_;
}

std::vector<ThreadPoolWorkerInfo> ThreadPool::worker_infos() const {
  std::lock_guard lock(mutex_);
  return worker_infos_;
}

void ThreadPool::worker_loop(std::stop_token stop_token, const std::size_t worker_index,
                             const std::optional<std::size_t> assigned_cpu) {
  tls_worker_index = worker_index;
  if (options_.bind_workers_to_cpu && assigned_cpu.has_value()) {
    bind_current_thread_to_cpu(*assigned_cpu);
  }

  {
    std::lock_guard lock(mutex_);
    refresh_worker_info_locked(worker_index, std::this_thread::get_id(), current_cpu());
  }

  while (true) {
    Task task;
    {
      std::unique_lock lock(mutex_);
      task_available_.wait(lock, stop_token, [this] {
        return shutdown_requested_ || !tasks_.empty();
      });

      if (tasks_.empty()) {
        if (shutdown_requested_ || stop_token.stop_requested()) {
          break;
        }
        continue;
      }

      if (stop_token.stop_requested()) {
        break;
      }

      task = std::move(tasks_.front());
      tasks_.pop_front();
      ++active_;
      refresh_worker_info_locked(worker_index, std::this_thread::get_id(), current_cpu());
    }

    bool task_failed = false;
    try {
      task();
    } catch (...) {
      task_failed = true;
    }

    {
      std::lock_guard lock(mutex_);
      refresh_worker_info_locked(worker_index, std::this_thread::get_id(), current_cpu());
      if (task_failed) {
        ++unhandled_exception_count_;
      }
      --active_;
      if (idle_locked()) {
        idle_.notify_all();
      }
    }
  }

  {
    std::lock_guard lock(mutex_);
    if (idle_locked()) {
      idle_.notify_all();
    }
  }
}

void ThreadPool::add_workers_locked(std::size_t worker_count) {
  workers_.reserve(workers_.size() + worker_count);
  worker_infos_.reserve(worker_infos_.size() + worker_count);
  for (std::size_t i = 0; i < worker_count; ++i) {
    const auto worker_index = next_worker_index_++;
    std::optional<std::size_t> assigned_cpu;
    if (!options_.worker_cpu_affinity.empty()) {
      assigned_cpu = options_.worker_cpu_affinity[worker_index % options_.worker_cpu_affinity.size()];
    }
    worker_infos_.push_back(ThreadPoolWorkerInfo{
      .worker_index = worker_index,
      .assigned_cpu = assigned_cpu,
    });
    workers_.push_back(WorkerSlot{
      .worker_index = worker_index,
      .thread = std::jthread([this, worker_index, assigned_cpu](std::stop_token stop_token) {
        worker_loop(stop_token, worker_index, assigned_cpu);
      }),
    });
  }
}

bool ThreadPool::idle_locked() const noexcept {
  return active_ == 0 && tasks_.empty();
}

void ThreadPool::refresh_worker_info_locked(const std::size_t worker_index, const std::thread::id thread_id,
                                            const std::optional<std::size_t> cpu_id) {
  const auto info = std::find_if(worker_infos_.begin(), worker_infos_.end(), [worker_index](const auto& item) {
    return item.worker_index == worker_index;
  });
  if (info == worker_infos_.end()) {
    return;
  }

  info->thread_id = thread_id;
  info->current_cpu = cpu_id;
}

} // namespace ksj::threading
