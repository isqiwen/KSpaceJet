#include "kspacejet/memory/memory.hpp"
#include "kspacejet/memory/detail/bitset_slab.hpp"
#include "kspacejet/threading/memory_affinity.hpp"
#include "kspacejet/threading/thread_pool.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#if KSJ_MEMORY_ENABLE_STATS
#include <sched.h>
#endif

namespace {

[[nodiscard]] ksj::memory::TopologySnapshot fake_dual_numa_topology() {
  ksj::memory::TopologySnapshot topology;
  topology.numa_nodes = {
    ksj::memory::NumaNodeInfo{.id = 0, .cpu_ids = {0, 1}},
    ksj::memory::NumaNodeInfo{.id = 1, .cpu_ids = {2, 3}},
  };
  topology.cpu_cores = {
    ksj::memory::CpuCoreInfo{.id = 0, .socket = 0, .numa_node = 0, .core_id = 0},
    ksj::memory::CpuCoreInfo{.id = 1, .socket = 0, .numa_node = 0, .core_id = 1},
    ksj::memory::CpuCoreInfo{.id = 2, .socket = 1, .numa_node = 1, .core_id = 0},
    ksj::memory::CpuCoreInfo{.id = 3, .socket = 1, .numa_node = 1, .core_id = 1},
  };
  topology.process_cpu_affinity = {2, 3, 0, 1};
  for (const auto& core : topology.cpu_cores) {
    topology.cpu_to_numa[core.id] = core.numa_node;
    topology.cpu_to_socket[core.id] = core.socket;
  }
  topology.socket_ids = {0, 1};
  topology.socket_to_numa_nodes[0] = {0};
  topology.socket_to_numa_nodes[1] = {1};
  return topology;
}

[[nodiscard]] ksj::memory::AllocationRequest request_for(std::size_t bytes, std::size_t numa_node) {
  ksj::memory::AllocationRequest request;
  request.bytes = bytes;
  request.properties.label = "test";
  request.properties.locality = ksj::memory::Locality::explicit_numa;
  request.properties.numa_node = numa_node;
  request.properties.alignment = 64;
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

TEST(KSpaceJetMemoryTopology, DiscoveryFindsAtLeastOneNumaNode) {
  const auto topology = ksj::memory::TopologyDiscovery::discover();
  EXPECT_FALSE(topology.numa_nodes.empty());
  EXPECT_TRUE(topology.first_numa_node().has_value());
}

TEST(KSpaceJetMemoryPlacement, MapsWorkersAndSocketsFromTopology) {
  ksj::memory::PlacementPolicy policy(fake_dual_numa_topology());

  ksj::memory::AllocationProperties properties;
  properties.locality = ksj::memory::Locality::worker_local;
  EXPECT_EQ(1U, policy.decide(properties, 0).numa_node);
  EXPECT_EQ(1U, policy.decide(properties, 1).numa_node);
  EXPECT_EQ(0U, policy.decide(properties, 2).numa_node);

  properties.locality = ksj::memory::Locality::socket_local;
  EXPECT_EQ(0U, policy.decide(properties, 0).numa_node);
  EXPECT_EQ(1U, policy.decide(properties, 1).numa_node);

  properties.locality = ksj::memory::Locality::explicit_numa;
  properties.numa_node = 1;
  EXPECT_EQ(1U, policy.decide(properties, 0).numa_node);
}

TEST(KSpaceJetMemoryPlacement, WorkerBindingsOverrideAffinityOrder) {
  auto topology = fake_dual_numa_topology();
  topology.bind_worker_to_numa(0, 0);
  topology.bind_worker_to_cpu(1, 2);
  ksj::memory::PlacementPolicy policy(topology);

  ksj::memory::AllocationProperties properties;
  properties.locality = ksj::memory::Locality::worker_local;
  EXPECT_EQ(0U, policy.decide(properties, 0).numa_node);
  EXPECT_EQ(1U, policy.decide(properties, 1).numa_node);

  properties.locality = ksj::memory::Locality::socket_local;
  EXPECT_EQ(1U, policy.decide(properties, 1).numa_node);
}

TEST(KSpaceJetMemoryPlacement, ConsumesThreadPoolWorkerAffinityMetadata) {
  ksj::threading::ThreadPoolOptions options;
  options.worker_cpu_affinity = {0, 2};
  ksj::threading::ThreadPool pool(2, options);

  auto topology = ksj::threading::topology_with_worker_affinity(fake_dual_numa_topology(), pool.worker_infos());

  ksj::memory::PlacementPolicy policy(topology);
  ksj::memory::AllocationProperties properties;
  properties.locality = ksj::memory::Locality::worker_local;

  EXPECT_EQ(0U, policy.decide(properties, 0).numa_node);
  EXPECT_EQ(1U, policy.decide(properties, 1).numa_node);
}

TEST(KSpaceJetMemoryPool, AllocatesAlignedZeroInitializedMemory) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());
  auto request = request_for(4096, node);
  request.properties.alignment = 1;
  request.properties.initialization = ksj::memory::Initialization::zero;

  auto lease = broker.acquire(request);

  ASSERT_TRUE(lease);
  EXPECT_EQ(4096U, lease.size());
  EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(lease.data()) % ksj::memory::NumaHostSpace::cache_line_size());
  EXPECT_EQ(node, lease.numa_node());
  EXPECT_FALSE(lease.direct());
  for (auto byte : lease.span()) {
    EXPECT_EQ(std::byte{0}, byte);
  }
}

TEST(KSpaceJetMemoryPool, RejectsMissingExplicitConfiguration) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());

  EXPECT_THROW(static_cast<void>(ksj::memory::MemoryBroker::create_for_testing(topology, {})), std::invalid_argument);
}

TEST(KSpaceJetMemoryPool, UsesConfiguredSizeClasses) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  ksj::memory::MemoryPoolOptions options;
  options.size_classes = {1024, 4096};
  options.size_class_block_counts = {8, 4};
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, options);

  auto lease = broker.acquire(request_for(1500, node));

  ASSERT_TRUE(lease);
  EXPECT_EQ(1500U, lease.size());
  EXPECT_EQ(4096U, lease.capacity());
  EXPECT_FALSE(lease.direct());
}

TEST(KSpaceJetMemoryPool, UsesConfiguredSizeClassBlockCounts) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  ksj::memory::MemoryPoolOptions options;
  options.size_classes = {1024};
  options.size_class_block_counts = {2};
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, options);

  auto first = broker.acquire(request_for(512, node));
  auto second = broker.acquire(request_for(512, node));
  auto third = broker.acquire(request_for(512, node));

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_TRUE(third);
  EXPECT_FALSE(first.direct());
  EXPECT_FALSE(second.direct());
  EXPECT_TRUE(third.direct());
}

TEST(KSpaceJetMemoryPool, SeparatesNumaBuckets) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  if (topology.numa_nodes.size() < 2) {
    GTEST_SKIP() << "requires at least two NUMA nodes";
  }
  const auto node0_id = topology.numa_nodes[0].id;
  const auto node1_id = topology.numa_nodes[1].id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  auto node0 = broker.acquire(request_for(1024, node0_id));
  auto node1 = broker.acquire(request_for(1024, node1_id));

  ASSERT_TRUE(node0);
  ASSERT_TRUE(node1);
  EXPECT_EQ(node0_id, node0.numa_node());
  EXPECT_EQ(node1_id, node1.numa_node());
  EXPECT_NE(node0.data(), node1.data());
}

TEST(KSpaceJetMemoryPool, PoolBlocksStayCacheLineAligned) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());
  std::vector<ksj::memory::MemoryLease> leases;
  constexpr std::size_t kLeaseCount = 16;
  leases.reserve(kLeaseCount);

  for (std::size_t i = 0; i < kLeaseCount; ++i) {
    auto request = request_for(1024 + i, node);
    request.properties.alignment = 1;
    auto lease = broker.acquire(request);

    ASSERT_TRUE(lease);
    EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(lease.data()) % ksj::memory::NumaHostSpace::cache_line_size());
    leases.push_back(std::move(lease));
  }
}

TEST(KSpaceJetMemoryPooledBuffer, AllocatesTypedCacheLineAlignedArray) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  ksj::memory::AllocationProperties properties;
  properties.label = "typed-buffer-test";
  properties.locality = ksj::memory::Locality::explicit_numa;
  properties.numa_node = node;
  properties.alignment = 1;
  properties.initialization = ksj::memory::Initialization::zero;

  auto buffer = ksj::memory::allocate_array<float>(broker, 2 * 3 * 4, properties);

  ASSERT_TRUE(buffer);
  EXPECT_EQ(24U, buffer.size());
  EXPECT_EQ(24U * sizeof(float), buffer.size_bytes());
  EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(buffer.data()) % ksj::memory::NumaHostSpace::cache_line_size());
  EXPECT_EQ(node, buffer.numa_node());
  for (const auto value : buffer.span()) {
    EXPECT_EQ(0.0F, value);
  }
}

TEST(KSpaceJetMemoryPooledBuffer, RejectsTypedAllocationOverflow) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  ksj::memory::AllocationProperties properties;
  properties.label = "typed-buffer-overflow-test";
  properties.locality = ksj::memory::Locality::explicit_numa;
  properties.numa_node = node;

  const auto too_many = std::numeric_limits<std::size_t>::max() / sizeof(float) + 1U;
  EXPECT_THROW((void)ksj::memory::allocate_array<float>(broker, too_many, properties), std::length_error);
}

TEST(KSpaceJetMemoryPool, BitsetSlabKeepsPerWorkerScanCursors) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  ksj::memory::detail::BitsetSlab slab(node, ksj::memory::NumaHostSpace::cache_line_size(), 8,
                                       ksj::memory::NumaHostSpace::cache_line_size(), ksj::memory::PagePolicy::normal,
                                       4, "worker-hint-test");

  EXPECT_EQ(4U, slab.worker_hint_count());
  auto* first = slab.try_acquire(0);
  auto* second = slab.try_acquire(1);
  ASSERT_NE(nullptr, first);
  ASSERT_NE(nullptr, second);
  EXPECT_NE(first, second);
  EXPECT_TRUE(slab.release(first));
  EXPECT_TRUE(slab.release(second));
}

TEST(KSpaceJetMemoryPool, ReusesExistingSlab) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  {
    auto first = broker.acquire(request_for(1024, node));
    ASSERT_TRUE(first);
    EXPECT_FALSE(first.reused());
  }

  auto second = broker.acquire(request_for(1024, node));
  ASSERT_TRUE(second);
  EXPECT_TRUE(second.reused());
}

TEST(KSpaceJetMemoryPool, StatsExposeSizeClassHistogramData) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  auto first = broker.acquire(request_for(1024, node));
  auto second = broker.acquire(request_for(2048, node));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  const auto stats = broker.stats_snapshot();
#if KSJ_MEMORY_ENABLE_STATS
  const auto numa = std::find_if(stats.numa.begin(), stats.numa.end(), [node](const auto& item) {
    return item.numa_node == node;
  });
  ASSERT_NE(stats.numa.end(), numa);

  constexpr std::uint64_t kSmallClass = 64ULL * 1024ULL;
  const auto size_class = std::find_if(numa->size_classes.begin(), numa->size_classes.end(), [](const auto& item) {
    return item.block_size == kSmallClass;
  });
  ASSERT_NE(numa->size_classes.end(), size_class);
  EXPECT_EQ(1024U, size_class->block_count);
  EXPECT_EQ(1U, size_class->slab_count);
  EXPECT_EQ(2U, size_class->active_blocks);
  EXPECT_EQ(size_class->block_count - 2U, size_class->free_blocks);
  EXPECT_EQ(2U * kSmallClass, size_class->active_bytes);
  EXPECT_EQ(64ULL * 1024ULL * 1024ULL, size_class->reserved_bytes);
  EXPECT_EQ(2U, size_class->allocations);
  EXPECT_EQ(1U, size_class->reuse_hits);
  EXPECT_EQ(0U, stats.larger_class_spills);
  EXPECT_EQ(0U, stats.direct_fallbacks);
  EXPECT_EQ(0U, stats.worker_node_mismatches);

  ksj::memory::MemoryPoolHistogramOptions histogram_options;
  histogram_options.bar_width = 12;
  const auto histogram = ksj::memory::format_memory_pool_histogram(stats, histogram_options);
  EXPECT_NE(std::string::npos, histogram.find("kspacejet-memory pool stats"));
  EXPECT_NE(std::string::npos, histogram.find("larger_class_spills=0"));
  EXPECT_NE(std::string::npos, histogram.find("direct_fallbacks=0"));
  EXPECT_NE(std::string::npos, histogram.find("NUMA node"));
  EXPECT_NE(std::string::npos, histogram.find("64.0 KiB"));
  EXPECT_NE(std::string::npos, histogram.find("[#"));
  EXPECT_NE(std::string::npos, histogram.find("util=0.20%"));
  EXPECT_NE(std::string::npos, histogram.find("active=2/1024"));
  EXPECT_NE(std::string::npos, histogram.find("free_bytes=63.9 MiB"));
#else
  EXPECT_TRUE(stats.numa.empty());
  EXPECT_EQ(0U, stats.requests);
  EXPECT_EQ(0U, stats.pool_allocations);
  EXPECT_TRUE(ksj::memory::format_memory_pool_histogram(stats).empty());
#endif
}

TEST(KSpaceJetMemoryPool, StatsCanDetectRemoteReleaseSuspects) {
  auto discovered = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(discovered.numa_nodes.empty());
  const auto allocation_node = discovered.numa_nodes.front().id;

#if KSJ_MEMORY_ENABLE_STATS
  const auto cpu = ::sched_getcpu();
  if (cpu < 0) {
    GTEST_SKIP() << "sched_getcpu is unavailable";
  }

  const auto remote_node = allocation_node == 0 ? 1U : 0U;
  ksj::memory::TopologySnapshot topology;
  topology.numa_nodes = {
    ksj::memory::NumaNodeInfo{.id = allocation_node, .cpu_ids = {}},
    ksj::memory::NumaNodeInfo{.id = remote_node, .cpu_ids = {static_cast<std::size_t>(cpu)}},
  };
  topology.cpu_cores = {
    ksj::memory::CpuCoreInfo{
      .id = static_cast<std::size_t>(cpu),
      .socket = remote_node,
      .numa_node = remote_node,
      .core_id = 0,
    },
  };
  topology.process_cpu_affinity = {static_cast<std::size_t>(cpu)};
  topology.cpu_to_numa[static_cast<std::size_t>(cpu)] = remote_node;
  topology.cpu_to_socket[static_cast<std::size_t>(cpu)] = remote_node;
  topology.socket_ids = {remote_node};
  topology.socket_to_numa_nodes[remote_node] = {remote_node};

  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());
  {
    auto lease = broker.acquire(request_for(4096, allocation_node));
    ASSERT_TRUE(lease);
  }

  const auto stats = broker.stats_snapshot();
  EXPECT_EQ(1U, stats.remote_release_suspects);
  EXPECT_EQ(0U, stats.unknown_release_cpus);
#else
  auto broker = ksj::memory::MemoryBroker::create_for_testing(discovered, test_pool_options());
  {
    auto lease = broker.acquire(request_for(4096, allocation_node));
    ASSERT_TRUE(lease);
  }
  const auto stats = broker.stats_snapshot();
  EXPECT_EQ(0U, stats.remote_release_suspects);
#endif
}

TEST(KSpaceJetMemoryPool, StatsCanDetectUnknownReleaseCpu) {
  auto discovered = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(discovered.numa_nodes.empty());
  const auto allocation_node = discovered.numa_nodes.front().id;
  ksj::memory::TopologySnapshot topology;
  topology.numa_nodes = {ksj::memory::NumaNodeInfo{.id = allocation_node, .cpu_ids = {}}};
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  {
    auto lease = broker.acquire(request_for(4096, allocation_node));
    ASSERT_TRUE(lease);
  }

  const auto stats = broker.stats_snapshot();
#if KSJ_MEMORY_ENABLE_STATS
  EXPECT_EQ(1U, stats.unknown_release_cpus);
  EXPECT_EQ(0U, stats.remote_release_suspects);
#else
  EXPECT_EQ(0U, stats.unknown_release_cpus);
#endif
}

TEST(KSpaceJetMemoryPool, StatsCanDetectWorkerNodeMismatch) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  topology.bind_worker_to_numa(7, node + 100000U);

  auto pool = ksj::memory::MemoryPool::create_for_testing(topology, test_pool_options());
  auto request = request_for(4096, node);
  request.properties.locality = ksj::memory::Locality::worker_local;
  request.properties.numa_node = std::nullopt;
  request.worker_index = 7;

  auto lease = pool->acquire_for_testing(request, ksj::memory::PlacementDecision{
                                                    .numa_node = node,
                                                    .locality = ksj::memory::Locality::worker_local,
                                                  });
  ASSERT_TRUE(lease);

  const auto stats = pool->stats_snapshot_for_testing();
#if KSJ_MEMORY_ENABLE_STATS
  EXPECT_EQ(1U, stats.worker_node_mismatches);
#else
  EXPECT_EQ(0U, stats.worker_node_mismatches);
#endif
}

TEST(KSpaceJetMemoryPool, UsesLargerClassInsteadOfGrowingCurrentClass) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto options = test_pool_options();
  options.direct_fallback = false;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, options);
  std::vector<ksj::memory::MemoryLease> leases;
  leases.reserve(1025);

  for (std::size_t i = 0; i < 1024; ++i) {
    auto lease = broker.acquire(request_for(1024, node));
    ASSERT_TRUE(lease);
    EXPECT_EQ(64ULL * 1024ULL, lease.capacity());
    leases.push_back(std::move(lease));
  }

  auto strict_request = request_for(1024, node);
  strict_request.properties.allow_larger_class = false;
  EXPECT_THROW(static_cast<void>(broker.acquire(strict_request)), std::bad_alloc);

  auto spill_request = request_for(1024, node);
  auto spill = broker.acquire(spill_request);
  ASSERT_TRUE(spill);
  EXPECT_EQ(1ULL * 1024ULL * 1024ULL, spill.capacity());
  EXPECT_FALSE(spill.direct());

#if KSJ_MEMORY_ENABLE_STATS
  EXPECT_EQ(1U, broker.stats_snapshot().larger_class_spills);
#endif
}

TEST(KSpaceJetMemoryPool, UsesDirectFallbackWhenExactClassIsFullAndLargerClassIsDisabled) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());
  std::vector<ksj::memory::MemoryLease> leases;
  leases.reserve(1024);

  for (std::size_t i = 0; i < 1024; ++i) {
    auto lease = broker.acquire(request_for(1024, node));
    ASSERT_TRUE(lease);
    EXPECT_FALSE(lease.direct());
    leases.push_back(std::move(lease));
  }

  auto request = request_for(1024, node);
  request.properties.allow_larger_class = false;
  auto fallback = broker.acquire(request);

  ASSERT_TRUE(fallback);
  EXPECT_TRUE(fallback.direct());
  EXPECT_EQ(1024U, fallback.capacity());

#if KSJ_MEMORY_ENABLE_STATS
  const auto stats = broker.stats_snapshot();
  EXPECT_EQ(1U, stats.direct_fallbacks);
  EXPECT_EQ(1U, stats.direct_allocations);
#endif
}

TEST(KSpaceJetMemoryPool, DirectAllocatesWhenRequested) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  auto request = request_for(4096, node);
  request.properties.allocator = ksj::memory::AllocatorKind::host_direct;
  request.properties.alignment = 1;
  auto lease = broker.acquire(request);

  ASSERT_TRUE(lease);
  EXPECT_TRUE(lease.direct());
  EXPECT_EQ(request.bytes, lease.size());
  EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(lease.data()) % ksj::memory::NumaHostSpace::cache_line_size());
}

TEST(KSpaceJetMemoryPool, DirectAllocatesPooledRequestsWhenPoolingIsDisabled) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto options = test_pool_options();
  options.pooling_enabled = false;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, options);

  auto lease = broker.acquire(request_for(4096, node));

  ASSERT_TRUE(lease);
  EXPECT_TRUE(lease.direct());
  EXPECT_EQ(4096U, lease.size());
  EXPECT_EQ(4096U, lease.capacity());
#if KSJ_MEMORY_ENABLE_STATS
  const auto stats = broker.stats_snapshot();
  EXPECT_EQ(1U, stats.direct_allocations);
  EXPECT_EQ(0U, stats.direct_fallbacks);
#endif
}

TEST(KSpaceJetMemoryPool, DirectAllocatesWithTransparentHugepageHint) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  auto request = request_for(4096, node);
  request.properties.allocator = ksj::memory::AllocatorKind::host_direct;
  request.properties.page_policy = ksj::memory::PagePolicy::transparent_hugepage;
  auto lease = broker.acquire(request);

  ASSERT_TRUE(lease);
  EXPECT_TRUE(lease.direct());
  EXPECT_EQ(request.bytes, lease.capacity());
}

TEST(KSpaceJetMemoryPool, RejectsUnsupportedMemorySpacesAtPoolBoundary) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  auto request = request_for(4096, node);
  request.properties.space_kind = ksj::memory::MemorySpaceKind::device;
  EXPECT_THROW(static_cast<void>(broker.acquire(request)), std::invalid_argument);

  request.properties.space_kind = ksj::memory::MemorySpaceKind::unified;
  EXPECT_THROW(static_cast<void>(broker.acquire(request)), std::invalid_argument);

  request.properties.space_kind = ksj::memory::MemorySpaceKind::pinned_host;
  request.properties.allocator = ksj::memory::AllocatorKind::host_pool;
  EXPECT_THROW(static_cast<void>(broker.acquire(request)), std::invalid_argument);
}

TEST(KSpaceJetMemoryPool, LeakTrackingIsOptIn) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());

  auto lease = broker.acquire(request_for(4096, node));

  ASSERT_TRUE(lease);
  EXPECT_TRUE(broker.check_no_leaks());
  EXPECT_TRUE(broker.outstanding_allocations().empty());
}

TEST(KSpaceJetMemoryPool, LeakTrackingReportsOutstandingAllocations) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto options = test_pool_options();
  options.leak_tracking = true;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, options);

  auto request = request_for(4096, node);
  request.properties.label = "leak-tracked-buffer";
  request.worker_index = 7;
  request.shard_key = "shard-a";
  auto lease = broker.acquire(request);

  ASSERT_TRUE(lease);
#if KSJ_MEMORY_ENABLE_LEAK_TRACKING
  EXPECT_FALSE(broker.check_no_leaks());

  auto outstanding = broker.outstanding_allocations();
  ASSERT_EQ(1U, outstanding.size());
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(lease.data()), outstanding.front().address);
  EXPECT_EQ(4096U, outstanding.front().requested_bytes);
  EXPECT_EQ(64ULL * 1024ULL, outstanding.front().capacity_bytes);
  EXPECT_EQ(node, outstanding.front().numa_node);
  EXPECT_EQ(64ULL * 1024ULL, outstanding.front().size_class_bytes);
  EXPECT_EQ(7U, outstanding.front().worker_index);
  EXPECT_EQ("leak-tracked-buffer", outstanding.front().label);
  EXPECT_EQ("shard-a", outstanding.front().shard_key);
  EXPECT_FALSE(outstanding.front().direct);

  lease.release();
  EXPECT_TRUE(broker.check_no_leaks());
  EXPECT_TRUE(broker.outstanding_allocations().empty());

  request.properties.allocator = ksj::memory::AllocatorKind::host_direct;
  request.properties.label = "direct-leak-tracked-buffer";
  auto direct = broker.acquire(request);

  ASSERT_TRUE(direct);
  outstanding = broker.outstanding_allocations();
  ASSERT_EQ(1U, outstanding.size());
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(direct.data()), outstanding.front().address);
  EXPECT_EQ(request.bytes, outstanding.front().requested_bytes);
  EXPECT_EQ(request.bytes, outstanding.front().capacity_bytes);
  EXPECT_EQ(0U, outstanding.front().size_class_bytes);
  EXPECT_EQ("direct-leak-tracked-buffer", outstanding.front().label);
  EXPECT_TRUE(outstanding.front().direct);

  direct.release();
  EXPECT_TRUE(broker.check_no_leaks());
#else
  EXPECT_TRUE(broker.check_no_leaks());
  EXPECT_TRUE(broker.outstanding_allocations().empty());
  lease.release();

  request.properties.allocator = ksj::memory::AllocatorKind::host_direct;
  auto direct = broker.acquire(request);
  ASSERT_TRUE(direct);
  EXPECT_TRUE(broker.check_no_leaks());
  EXPECT_TRUE(broker.outstanding_allocations().empty());
#endif
}

TEST(KSpaceJetMemoryPool, RejectsOversizedPoolRequestWhenDirectFallbackIsDisabled) {
  auto options = test_pool_options();
  options.direct_fallback = false;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(fake_dual_numa_topology(), options);

  auto request = request_for(513ULL * 1024ULL * 1024ULL, 0);

  EXPECT_THROW(static_cast<void>(broker.acquire(request)), std::bad_alloc);
}

TEST(KSpaceJetMemoryPool, ConcurrentAcquiresAreUniqueWhileLive) {
  auto topology = ksj::memory::TopologyDiscovery::discover();
  ASSERT_FALSE(topology.numa_nodes.empty());
  const auto node = topology.numa_nodes.front().id;
  auto broker = ksj::memory::MemoryBroker::create_for_testing(topology, test_pool_options());
  std::mutex mutex;
  std::unordered_set<std::byte*> in_flight;
  std::atomic<bool> duplicate{false};
  constexpr std::size_t kThreads = 8;
  constexpr std::size_t kIterations = 256;
  std::array<std::thread, kThreads> threads;

  for (std::size_t thread_index = 0; thread_index < kThreads; ++thread_index) {
    threads[thread_index] = std::thread([&broker, &mutex, &in_flight, &duplicate, node, thread_index]() {
      for (std::size_t i = 0; i < kIterations; ++i) {
        auto request = request_for(1024 + ((thread_index + i) % 128), node);
        request.worker_index = thread_index;
        auto lease = broker.acquire(request);
        if (!lease) {
          duplicate.store(true, std::memory_order_relaxed);
          return;
        }
        {
          std::lock_guard lock(mutex);
          if (!in_flight.insert(lease.data()).second) {
            duplicate.store(true, std::memory_order_relaxed);
            return;
          }
        }
        std::memset(lease.data(), 0xA5, lease.size());
        {
          std::lock_guard lock(mutex);
          in_flight.erase(lease.data());
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_FALSE(duplicate.load(std::memory_order_relaxed));
  EXPECT_TRUE(in_flight.empty());

  const auto stats = broker.stats_snapshot();
#if KSJ_MEMORY_ENABLE_STATS
  constexpr std::uint64_t kExpectedOperations = 8ULL * 256ULL;
  EXPECT_GE(stats.pool_allocations, kExpectedOperations);
  EXPECT_GE(stats.releases, kExpectedOperations);
#else
  EXPECT_EQ(0U, stats.pool_allocations);
  EXPECT_EQ(0U, stats.releases);
#endif
}
