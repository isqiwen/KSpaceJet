#pragma once

#include "kspacejet/threading/thread_pool.hpp"

#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::threading {

struct WorkerRequest {
  std::string name;
  std::size_t preferred_workers{ThreadPool::default_worker_count()};
  std::size_t min_workers{1};
  std::size_t max_workers{ThreadPool::default_worker_count()};
  ThreadPool::ShutdownPolicy release_policy{ThreadPool::ShutdownPolicy::finish_pending};
  ThreadPoolOptions pool_options{};
};

class ThreadPoolLease {
public:
  ThreadPoolLease() = default;
  ~ThreadPoolLease();

  ThreadPoolLease(const ThreadPoolLease&) = delete;
  ThreadPoolLease& operator=(const ThreadPoolLease&) = delete;
  ThreadPoolLease(ThreadPoolLease&& other) noexcept;
  ThreadPoolLease& operator=(ThreadPoolLease&& other) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
  [[nodiscard]] std::string_view name() const noexcept;

  template <typename Function, typename... Args> [[nodiscard]] bool post(Function&& function, Args&&... args) {
    auto state = state_;
    if (!state || state->released()) {
      return false;
    }

    return state->pool.post(std::forward<Function>(function), std::forward<Args>(args)...);
  }

  template <typename Function, typename... Args>
  [[nodiscard]] auto submit(Function&& function, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>> {
    auto state = state_;
    if (!state || state->released()) {
      throw std::runtime_error("ThreadPoolLease is not accepting tasks");
    }

    return state->pool.submit(std::forward<Function>(function), std::forward<Args>(args)...);
  }

  void clear_pending();
  void wait() const;

  template <typename Rep, typename Period>
  [[nodiscard]] bool wait_for(std::chrono::duration<Rep, Period> timeout) const {
    auto state = state_;
    return !state || state->pool.wait_for(timeout);
  }

  [[nodiscard]] bool wait_until(std::chrono::steady_clock::time_point deadline) const;
  void shutdown(ThreadPool::ShutdownPolicy policy) noexcept;
  void release() noexcept;

  [[nodiscard]] std::size_t worker_count() const;
  [[nodiscard]] std::size_t active_count() const;
  [[nodiscard]] std::size_t queued_count() const;
  [[nodiscard]] bool idle() const;
  [[nodiscard]] bool accepting_tasks() const;
  [[nodiscard]] std::vector<ThreadPoolWorkerInfo> worker_infos() const;

private:
  friend class ThreadingService;

  struct LeaseState {
    LeaseState(std::string lease_name, std::size_t workers, ThreadPool::ShutdownPolicy policy,
               ThreadPoolOptions options);
    ~LeaseState();

    [[nodiscard]] bool released() const;
    void shutdown(ThreadPool::ShutdownPolicy policy) noexcept;

    std::string name;
    ThreadPool pool;
    ThreadPool::ShutdownPolicy release_policy;

  private:
    mutable std::mutex mutex;
    bool is_released{false};
  };

  explicit ThreadPoolLease(std::shared_ptr<LeaseState> state);

  std::shared_ptr<LeaseState> state_;
};

class ThreadingService {
public:
  explicit ThreadingService(std::size_t max_workers_per_lease = ThreadPool::default_worker_count());
  ~ThreadingService();

  ThreadingService(const ThreadingService&) = delete;
  ThreadingService& operator=(const ThreadingService&) = delete;
  ThreadingService(ThreadingService&&) = delete;
  ThreadingService& operator=(ThreadingService&&) = delete;

  [[nodiscard]] ThreadPoolLease acquire(WorkerRequest request);
  void shutdown_all(ThreadPool::ShutdownPolicy policy = ThreadPool::ShutdownPolicy::finish_pending) noexcept;

  [[nodiscard]] std::size_t active_lease_count() const;
  [[nodiscard]] std::size_t active_worker_count() const;
  [[nodiscard]] std::size_t max_workers_per_lease() const noexcept { return max_workers_per_lease_; }

private:
  [[nodiscard]] std::size_t resolve_worker_count(const WorkerRequest& request) const noexcept;
  [[nodiscard]] std::vector<std::shared_ptr<ThreadPoolLease::LeaseState>> active_states() const;

  mutable std::mutex mutex_;
  mutable std::vector<std::weak_ptr<ThreadPoolLease::LeaseState>> leases_;
  std::size_t max_workers_per_lease_;
};

} // namespace ksj::threading
