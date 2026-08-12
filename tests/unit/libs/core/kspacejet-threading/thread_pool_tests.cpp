#include "kspacejet/threading/thread_pool.hpp"
#include "kspacejet/threading/threading_service.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#if defined(__linux__)
#include <sched.h>
#endif

namespace {

using namespace std::chrono_literals;

#if defined(__linux__)
std::vector<std::size_t> current_affinity_cpus() {
  std::vector<std::size_t> cpus;
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (::sched_getaffinity(0, sizeof(mask), &mask) != 0) {
    return cpus;
  }
  for (std::size_t cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &mask)) {
      cpus.push_back(cpu);
    }
  }
  return cpus;
}
#endif

TEST(KSpaceJetThreadPool, ExecutesPostedTasksAndWaits) {
  ksj::threading::ThreadPool pool(2);
  std::atomic<int> count{0};

  for (int i = 0; i < 32; ++i) {
    ASSERT_TRUE(pool.post([&count]() {
      count.fetch_add(1, std::memory_order_relaxed);
    }));
  }

  pool.wait();

  EXPECT_EQ(32, count.load(std::memory_order_relaxed));
  EXPECT_EQ(0U, pool.active_count());
  EXPECT_EQ(0U, pool.queued_count());
  EXPECT_TRUE(pool.idle());
}

TEST(KSpaceJetThreadPool, SubmitReturnsFutureResult) {
  ksj::threading::ThreadPool pool(1);

  auto result = pool.submit(
    [](int lhs, int rhs) {
      return lhs + rhs;
    },
    19, 23);

  EXPECT_EQ(42, result.get());
  pool.wait();
}

TEST(KSpaceJetThreadPool, SubmitPropagatesTaskExceptionThroughFuture) {
  ksj::threading::ThreadPool pool(1);

  auto result = pool.submit([]() -> int {
    throw std::runtime_error("boom");
  });

  EXPECT_THROW((void)result.get(), std::runtime_error);
  pool.wait();
  EXPECT_EQ(0U, pool.unhandled_exception_count());
}

TEST(KSpaceJetThreadPool, PostedTaskExceptionDoesNotStopWorkers) {
  ksj::threading::ThreadPool pool(1);
  std::atomic<int> count{0};

  ASSERT_TRUE(pool.post([]() {
    throw std::runtime_error("boom");
  }));
  pool.wait();

  EXPECT_EQ(1U, pool.unhandled_exception_count());

  ASSERT_TRUE(pool.post([&count]() {
    count.fetch_add(1, std::memory_order_relaxed);
  }));
  pool.wait();

  EXPECT_EQ(1, count.load(std::memory_order_relaxed));
}

TEST(KSpaceJetThreadPool, PostAcceptsMoveOnlyCallable) {
  ksj::threading::ThreadPool pool(1);
  std::atomic<int> result{0};
  auto value = std::make_unique<int>(42);

  ASSERT_TRUE(pool.post([value = std::move(value), &result]() {
    result.store(*value, std::memory_order_relaxed);
  }));
  pool.wait();

  EXPECT_EQ(42, result.load(std::memory_order_relaxed));
}

TEST(KSpaceJetThreadPool, PostBindsArguments) {
  ksj::threading::ThreadPool pool(1);
  std::atomic<int> result{0};

  ASSERT_TRUE(pool.post(
    [&result](int lhs, int rhs) {
      result.store(lhs + rhs, std::memory_order_relaxed);
    },
    19, 23));
  pool.wait();

  EXPECT_EQ(42, result.load(std::memory_order_relaxed));
}

TEST(KSpaceJetThreadPool, PostMovesArguments) {
  ksj::threading::ThreadPool pool(1);
  std::atomic<int> result{0};

  ASSERT_TRUE(pool.post(
    [&result](std::unique_ptr<int> value) {
      result.store(*value, std::memory_order_relaxed);
    },
    std::make_unique<int>(42)));
  pool.wait();

  EXPECT_EQ(42, result.load(std::memory_order_relaxed));
}

TEST(KSpaceJetThreadPool, ClearPendingDiscardsQueuedTasksOnly) {
  ksj::threading::ThreadPool pool(1);
  std::promise<void> started;
  std::promise<void> release;
  auto release_future = release.get_future().share();
  std::atomic<int> count{0};

  ASSERT_TRUE(pool.post([&started, release_future]() {
    started.set_value();
    release_future.wait();
  }));

  ASSERT_EQ(std::future_status::ready, started.get_future().wait_for(1s));

  for (int i = 0; i < 16; ++i) {
    ASSERT_TRUE(pool.post([&count]() {
      count.fetch_add(1, std::memory_order_relaxed);
    }));
  }

  EXPECT_FALSE(pool.wait_for(10ms));
  EXPECT_GE(pool.queued_count(), 1U);

  pool.clear_pending();
  EXPECT_EQ(0U, pool.queued_count());

  release.set_value();
  pool.wait();

  EXPECT_EQ(0, count.load(std::memory_order_relaxed));
  EXPECT_TRUE(pool.idle());
}

TEST(KSpaceJetThreadPool, ClearPendingBreaksFutureForQueuedSubmitTask) {
  ksj::threading::ThreadPool pool(1);
  std::promise<void> started;
  std::promise<void> release;
  auto release_future = release.get_future().share();

  ASSERT_TRUE(pool.post([&started, release_future]() {
    started.set_value();
    release_future.wait();
  }));
  ASSERT_EQ(std::future_status::ready, started.get_future().wait_for(1s));

  auto result = pool.submit([] {
    return 42;
  });

  pool.clear_pending();
  release.set_value();
  pool.wait();

  EXPECT_THROW((void)result.get(), std::future_error);
}

TEST(KSpaceJetThreadPool, ShutdownDiscardPendingBreaksFutureForQueuedSubmitTask) {
  ksj::threading::ThreadPool pool(1);
  std::promise<void> started;
  std::promise<void> release;
  auto release_future = release.get_future().share();

  ASSERT_TRUE(pool.post([&started, release_future] {
    started.set_value();
    release_future.wait();
  }));
  ASSERT_EQ(std::future_status::ready, started.get_future().wait_for(1s));

  auto result = pool.submit([] {
    return 42;
  });

  auto shutdown = std::async(std::launch::async, [&pool] {
    pool.shutdown(ksj::threading::ThreadPool::ShutdownPolicy::discard_pending);
  });
  ASSERT_EQ(std::future_status::timeout, shutdown.wait_for(10ms));

  release.set_value();
  shutdown.get();

  EXPECT_THROW((void)result.get(), std::future_error);
}

TEST(KSpaceJetThreadPool, ResizeChangesWorkerCount) {
  ksj::threading::ThreadPool pool(1);

  EXPECT_EQ(1U, pool.worker_count());
  pool.resize(3);
  EXPECT_EQ(3U, pool.worker_count());
  pool.resize(1);
  EXPECT_EQ(1U, pool.worker_count());
  pool.resize(0);
  EXPECT_EQ(1U, pool.worker_count());
}

TEST(KSpaceJetThreadPool, ExposesWorkerIdentityWhileTasksRun) {
  ksj::threading::ThreadPool pool(2);
  std::mutex mutex;
  std::condition_variable started_cv;
  std::condition_variable release_cv;
  std::vector<std::size_t> observed_workers;
  std::size_t started = 0;
  bool release = false;

  for (std::size_t i = 0; i < 2; ++i) {
    ASSERT_TRUE(pool.post([&] {
      {
        std::lock_guard lock(mutex);
        if (const auto worker = ksj::threading::ThreadPool::current_worker_index(); worker.has_value()) {
          observed_workers.push_back(*worker);
        }
        ++started;
      }
      started_cv.notify_one();

      std::unique_lock lock(mutex);
      release_cv.wait(lock, [&] {
        return release;
      });
    }));
  }

  {
    std::unique_lock lock(mutex);
    ASSERT_TRUE(started_cv.wait_for(lock, 1s, [&] {
      return started == 2;
    }));
    release = true;
  }
  release_cv.notify_all();
  pool.wait();

  EXPECT_EQ(2U, observed_workers.size());
  auto infos = pool.worker_infos();
  ASSERT_EQ(2U, infos.size());
  for (const auto& info : infos) {
    EXPECT_NE(std::thread::id{}, info.thread_id);
  }
}

TEST(KSpaceJetThreadPool, RecordsConfiguredWorkerCpuAffinity) {
#if defined(__linux__)
  const auto cpus = current_affinity_cpus();
  if (cpus.empty()) {
    GTEST_SKIP() << "no sched_getaffinity CPU set available";
  }

  ksj::threading::ThreadPoolOptions options;
  options.worker_cpu_affinity = {cpus.front()};
  options.bind_workers_to_cpu = true;
  ksj::threading::ThreadPool pool(1, options);

  auto ran_on_cpu = pool.submit([] {
    const auto cpu = ::sched_getcpu();
    return cpu < 0 ? std::optional<std::size_t>{} : std::optional<std::size_t>{static_cast<std::size_t>(cpu)};
  });

  const auto cpu = ran_on_cpu.get();
  ASSERT_TRUE(cpu.has_value());
  if (*cpu != cpus.front()) {
    GTEST_SKIP() << "sched_setaffinity did not bind this worker in the current environment";
  }

  auto infos = pool.worker_infos();
  ASSERT_EQ(1U, infos.size());
  ASSERT_TRUE(infos.front().assigned_cpu.has_value());
  EXPECT_EQ(cpus.front(), *infos.front().assigned_cpu);
#else
  GTEST_SKIP() << "CPU affinity binding is only available on Linux";
#endif
}

TEST(KSpaceJetThreadPool, ZeroWorkerConstructorUsesOneWorker) {
  ksj::threading::ThreadPool pool(0);

  EXPECT_EQ(1U, pool.worker_count());
  auto result = pool.submit([] {
    return 7;
  });

  EXPECT_EQ(7, result.get());
}

TEST(KSpaceJetThreadPool, ShutdownStopsAcceptingTasks) {
  ksj::threading::ThreadPool pool(1);

  pool.shutdown();

  EXPECT_FALSE(pool.accepting_tasks());
  EXPECT_FALSE(pool.post([]() {}));
  EXPECT_THROW((void)pool.submit([]() {}), std::runtime_error);
}

TEST(KSpaceJetThreadingService, LeaseRunsTasksAndReleasesWorkers) {
  ksj::threading::ThreadingService service(4);
  std::atomic<int> count{0};

  {
    auto lease = service.acquire({
      .name = "test",
      .preferred_workers = 2,
      .min_workers = 1,
      .max_workers = 4,
    });

    EXPECT_EQ("test", lease.name());
    EXPECT_EQ(2U, lease.worker_count());
    EXPECT_EQ(1U, service.active_lease_count());
    EXPECT_EQ(2U, service.active_worker_count());

    for (int i = 0; i < 16; ++i) {
      ASSERT_TRUE(lease.post([&count] {
        count.fetch_add(1, std::memory_order_relaxed);
      }));
    }
    lease.wait();
  }

  EXPECT_EQ(16, count.load(std::memory_order_relaxed));
  EXPECT_EQ(0U, service.active_lease_count());
  EXPECT_EQ(0U, service.active_worker_count());
}

TEST(KSpaceJetThreadingService, WorkerRequestIsClampedByRequestAndServiceLimits) {
  ksj::threading::ThreadingService service(3);

  auto lease = service.acquire({
    .name = "clamped",
    .preferred_workers = 8,
    .min_workers = 2,
    .max_workers = 6,
  });

  EXPECT_EQ(3U, lease.worker_count());
}

TEST(KSpaceJetThreadingService, LeaseExposesConfiguredWorkerAffinity) {
  ksj::threading::ThreadingService service(2);
  ksj::threading::WorkerRequest request;
  request.name = "affinity";
  request.preferred_workers = 2;
  request.pool_options.worker_cpu_affinity = {0, 1};

  auto lease = service.acquire(std::move(request));
  const auto infos = lease.worker_infos();

  ASSERT_EQ(2U, infos.size());
  ASSERT_TRUE(infos[0].assigned_cpu.has_value());
  ASSERT_TRUE(infos[1].assigned_cpu.has_value());
  EXPECT_EQ(0U, *infos[0].assigned_cpu);
  EXPECT_EQ(1U, *infos[1].assigned_cpu);
}

TEST(KSpaceJetThreadingService, ShutdownAllStopsActiveLeases) {
  ksj::threading::ThreadingService service(2);
  auto lease = service.acquire({
    .name = "shutdown",
    .preferred_workers = 1,
  });

  EXPECT_TRUE(lease.accepting_tasks());
  service.shutdown_all(ksj::threading::ThreadPool::ShutdownPolicy::finish_pending);

  EXPECT_FALSE(lease.accepting_tasks());
  EXPECT_FALSE(lease.post([] {}));
}

} // namespace
