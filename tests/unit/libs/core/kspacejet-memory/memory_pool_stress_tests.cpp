#include "kspacejet/memory/memory.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

[[nodiscard]] ksj::memory::AllocationRequest stress_request(const std::size_t bytes, const std::size_t numa_node,
                                                            const std::size_t worker_index) {
  ksj::memory::AllocationRequest request;
  request.bytes = bytes;
  request.worker_index = worker_index;
  request.properties.label = "stress";
  request.properties.locality = ksj::memory::Locality::explicit_numa;
  request.properties.numa_node = numa_node;
  request.properties.alignment = 1;
  return request;
}

[[nodiscard]] ksj::memory::MemoryPoolOptions test_pool_options() {
  ksj::memory::MemoryPoolOptions options;
  options.size_classes = {
    64ULL * 1024ULL,
    1ULL * 1024ULL * 1024ULL,
    2ULL * 1024ULL * 1024ULL,
    4ULL * 1024ULL * 1024ULL,
    8ULL * 1024ULL * 1024ULL,
    16ULL * 1024ULL * 1024ULL,
    32ULL * 1024ULL * 1024ULL,
    64ULL * 1024ULL * 1024ULL,
    128ULL * 1024ULL * 1024ULL,
    256ULL * 1024ULL * 1024ULL,
    512ULL * 1024ULL * 1024ULL,
  };
  options.size_class_block_counts = {1024, 64, 32, 16, 8, 4, 2, 1, 1, 1, 1};
  return options;
}

} // namespace

TEST(KSpaceJetMemoryStress, ConcurrentMixedSizeAcquireRelease) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  constexpr std::size_t kThreads = 6;
  constexpr std::size_t kIterations = 512;
  constexpr std::array<std::size_t, 6> kSizes = {128, 1024, 4096, 16 * 1024, 64 * 1024, 1 * 1024 * 1024};

  std::atomic<bool> failed{false};
  std::array<std::thread, kThreads> threads;

  for (std::size_t thread_index = 0; thread_index < kThreads; ++thread_index) {
    threads[thread_index] = std::thread([&, thread_index] {
      for (std::size_t i = 0; i < kIterations; ++i) {
        const auto size = kSizes[(thread_index + i) % kSizes.size()];
        auto lease = broker.acquire(stress_request(size, node, thread_index));
        if (!lease || lease.size() != size ||
            (reinterpret_cast<std::uintptr_t>(lease.data()) % ksj::memory::NumaHostSpace::cache_line_size()) != 0) {
          failed.store(true, std::memory_order_relaxed);
          return;
        }
        std::memset(lease.data(), static_cast<int>((thread_index + i) & 0xFFU), lease.size());
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_FALSE(failed.load(std::memory_order_relaxed));
}

TEST(KSpaceJetMemoryStress, CrossThreadReleaseAndQuiescentTrim) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  constexpr std::size_t kLeaseCount = 256;
  constexpr std::size_t kThreads = 4;
  std::vector<ksj::memory::MemoryLease> leases;
  leases.reserve(kLeaseCount);

  for (std::size_t i = 0; i < kLeaseCount; ++i) {
    auto lease = broker.acquire(stress_request(2048, node, i % kThreads));
    ASSERT_TRUE(lease);
    leases.push_back(std::move(lease));
  }

  std::array<std::thread, kThreads> threads;
  for (std::size_t thread_index = 0; thread_index < kThreads; ++thread_index) {
    threads[thread_index] = std::thread([&, thread_index] {
      for (std::size_t i = thread_index; i < leases.size(); i += kThreads) {
        leases[i].release();
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  for (const auto& lease : leases) {
    EXPECT_FALSE(lease);
  }

  broker.trim();

  auto lease = broker.acquire(stress_request(2048, node, 0));
  ASSERT_TRUE(lease);
  EXPECT_EQ(2048U, lease.size());
}
