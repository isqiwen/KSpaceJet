#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::threading {

struct ThreadPoolOptions {
  std::vector<std::size_t> worker_cpu_affinity;
  bool bind_workers_to_cpu{false};
};

struct ThreadPoolWorkerInfo {
  std::size_t worker_index{0};
  std::thread::id thread_id{};
  std::optional<std::size_t> assigned_cpu{};
  std::optional<std::size_t> current_cpu{};
};

// A small general-purpose pool for core/background work.
//
// Use post() for fire-and-forget work, submit() when the caller needs a future,
// wait() to drain queued and active tasks, and shutdown() to stop accepting work.
class ThreadPool {
public:
  enum class ShutdownPolicy {
    finish_pending,
    discard_pending,
  };

  // The pool always owns at least one worker. Passing 0 to the constructor or
  // resize() is normalized to 1 so posted work cannot be left unserviceable.
  explicit ThreadPool(std::size_t worker_count = default_worker_count(), ThreadPoolOptions options = {});
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  [[nodiscard]] static std::size_t default_worker_count() noexcept;
  [[nodiscard]] static std::optional<std::size_t> current_worker_index() noexcept;

  template <typename Function, typename... Args> [[nodiscard]] bool post(Function&& function, Args&&... args) {
    using Callable = std::decay_t<Function>;
    static_assert(std::is_invocable_v<Callable&, std::decay_t<Args>...>,
                  "ThreadPool::post requires a callable invocable with the provided arguments");

    return enqueue(Task([function = std::forward<Function>(function), ... args = std::forward<Args>(args)]() mutable {
      std::invoke(function, std::move(args)...);
    }));
  }

  template <typename Function, typename... Args>
  [[nodiscard]] auto submit(Function&& function, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>> {
    using Result = std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>;

    std::packaged_task<Result()> task(
      [function = std::forward<Function>(function), ... args = std::forward<Args>(args)]() mutable -> Result {
        if constexpr (std::is_void_v<Result>) {
          std::invoke(std::move(function), std::move(args)...);
        } else {
          return std::invoke(std::move(function), std::move(args)...);
        }
      });

    auto future = task.get_future();
    if (!post([task = std::move(task)]() mutable {
          task();
        })) {
      throw std::runtime_error("ThreadPool is not accepting tasks");
    }
    return future;
  }

  void resize(std::size_t worker_count);
  // Discards queued tasks that have not started. Futures returned by submit()
  // for discarded tasks will throw std::future_error(future_errc::broken_promise).
  void clear_pending();
  void wait() const;

  template <typename Rep, typename Period>
  [[nodiscard]] bool wait_for(std::chrono::duration<Rep, Period> timeout) const {
    return wait_until(std::chrono::steady_clock::now() + timeout);
  }

  [[nodiscard]] bool wait_until(std::chrono::steady_clock::time_point deadline) const;
  // finish_pending drains queued and active tasks before stopping. discard_pending
  // drops queued tasks first; futures for dropped submit() tasks become broken promises.
  void shutdown(ShutdownPolicy policy = ShutdownPolicy::finish_pending) noexcept;

  [[nodiscard]] std::size_t worker_count() const;
  [[nodiscard]] std::size_t active_count() const;
  [[nodiscard]] std::size_t queued_count() const;
  [[nodiscard]] bool idle() const;
  [[nodiscard]] bool accepting_tasks() const;
  [[nodiscard]] std::size_t unhandled_exception_count() const;
  [[nodiscard]] std::vector<ThreadPoolWorkerInfo> worker_infos() const;

private:
  class Task {
  public:
    Task() = default;

    template <typename Function>
    explicit Task(Function&& function)
        : callable_(std::make_unique<Model<std::decay_t<Function>>>(std::forward<Function>(function))) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) noexcept = default;
    Task& operator=(Task&&) noexcept = default;

    explicit operator bool() const noexcept { return callable_ != nullptr; }
    void operator()() { callable_->run(); }

  private:
    struct Concept {
      virtual ~Concept() = default;
      virtual void run() = 0;
    };

    template <typename Function> struct Model final : Concept {
      template <typename SourceFunction>
      explicit Model(SourceFunction&& source_function) : function(std::forward<SourceFunction>(source_function)) {}

      void run() override { std::invoke(function); }

      Function function;
    };

    std::unique_ptr<Concept> callable_;
  };

  struct WorkerSlot {
    std::size_t worker_index{0};
    std::jthread thread;
  };

  [[nodiscard]] bool enqueue(Task task);
  void worker_loop(std::stop_token stop_token, std::size_t worker_index, std::optional<std::size_t> assigned_cpu);
  void add_workers_locked(std::size_t worker_count);
  [[nodiscard]] bool idle_locked() const noexcept;
  void refresh_worker_info_locked(std::size_t worker_index, std::thread::id thread_id,
                                  std::optional<std::size_t> current_cpu);

  mutable std::mutex mutex_;
  mutable std::condition_variable_any task_available_;
  mutable std::condition_variable idle_;
  std::deque<Task> tasks_;
  std::vector<WorkerSlot> workers_;
  std::vector<ThreadPoolWorkerInfo> worker_infos_;
  ThreadPoolOptions options_;
  std::size_t next_worker_index_{0};
  std::size_t active_{0};
  std::size_t unhandled_exception_count_{0};
  bool accepting_tasks_{true};
  bool shutdown_requested_{false};
};

} // namespace ksj::threading
