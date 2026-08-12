#pragma once

#ifndef KSJ_MEMORY_ENABLE_STATS
#define KSJ_MEMORY_ENABLE_STATS 0
#endif

#ifndef KSJ_MEMORY_ENABLE_NUMA_DIAGNOSTICS
#define KSJ_MEMORY_ENABLE_NUMA_DIAGNOSTICS 0
#endif

#ifndef KSJ_MEMORY_ENABLE_LEAK_TRACKING
#define KSJ_MEMORY_ENABLE_LEAK_TRACKING 0
#endif

#ifndef KSJ_MEMORY_ENABLE_TEST_ACCESS
#define KSJ_MEMORY_ENABLE_TEST_ACCESS 0
#endif

#include <cstddef>
#include <memory>
#if KSJ_MEMORY_ENABLE_STATS
#include <optional>
#endif
#if KSJ_MEMORY_ENABLE_LEAK_TRACKING
#include <atomic>
#include <mutex>
#endif
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "kspacejet/memory/allocation_properties.hpp"
#include "kspacejet/memory/detail/bitset_slab.hpp"
#include "kspacejet/memory/detail/size_class.hpp"
#include "kspacejet/memory/memory_lease.hpp"
#include "kspacejet/memory/placement.hpp"
#include "kspacejet/memory/stats.hpp"

namespace ksj::memory {

struct MemoryPoolOptions {
  bool pooling_enabled{true};
  bool direct_fallback{true};
  bool leak_tracking{false};
  // Must be configured explicitly when pooling_enabled is true.
  std::vector<std::size_t> size_classes;
  // Must match size_classes one-to-one when pooling_enabled is true.
  std::vector<std::size_t> size_class_block_counts;
};

struct AllocationRequest {
  AllocationProperties properties;
  std::size_t bytes{0};
  std::size_t worker_index{0};
  std::string_view shard_key{};
};

class MemoryBroker;

class MemoryPool : public std::enable_shared_from_this<MemoryPool> {
public:
  ~MemoryPool();

  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;

#if KSJ_MEMORY_ENABLE_TEST_ACCESS
  [[nodiscard]] static std::shared_ptr<MemoryPool> create_for_testing(TopologySnapshot topology,
                                                                      MemoryPoolOptions options);
  [[nodiscard]] MemoryLease acquire_for_testing(const AllocationRequest& request, PlacementDecision placement);
  [[nodiscard]] MemoryPoolStatsSnapshot stats_snapshot_for_testing() const;
#endif

private:
  friend class MemoryBroker;
  friend class MemoryLease;

  class SizeClassPool;
  class NumaPool;

  [[nodiscard]] static std::shared_ptr<MemoryPool> create(TopologySnapshot topology, MemoryPoolOptions options);
  MemoryPool(TopologySnapshot topology, MemoryPoolOptions options);

  [[nodiscard]] MemoryLease acquire(const AllocationRequest& request, PlacementDecision placement);
  void release(AllocationRecord record) noexcept;
  // Cold-path compaction. Call only when all pool users are quiescent.
  void trim_empty_slabs() noexcept;

  [[nodiscard]] const TopologySnapshot& topology() const noexcept { return topology_; }
  [[nodiscard]] MemoryPoolStatsSnapshot stats_snapshot() const;
  [[nodiscard]] std::vector<OutstandingMemoryAllocation> outstanding_allocations() const;
  [[nodiscard]] bool check_no_leaks() const;

  [[nodiscard]] MemoryLease acquire_direct(const AllocationRequest& request, std::size_t numa_node, bool fallback);
  [[nodiscard]] std::vector<NumaMemoryStats> collect_numa_stats() const;
#if KSJ_MEMORY_ENABLE_STATS
  struct ReleaseNumaProbe {
    bool remote_suspect{false};
    bool unknown_cpu{false};
  };

  [[nodiscard]] std::optional<std::size_t> current_thread_numa_node() const noexcept;
  [[nodiscard]] ReleaseNumaProbe probe_release_numa(const AllocationRecord& record) const noexcept;
  void record_worker_node_mismatch(const AllocationRequest& request, std::size_t placement_numa_node) noexcept;
#endif
  void track_allocation(const AllocationRecord& record, const AllocationRequest& request);
  void untrack_allocation(const AllocationRecord& record) noexcept;

  TopologySnapshot topology_;
  MemoryPoolOptions options_;
  std::vector<std::size_t> size_classes_;
  std::vector<std::size_t> size_class_block_counts_;
  MemoryPoolStats stats_;
#if KSJ_MEMORY_ENABLE_LEAK_TRACKING
  std::atomic<std::uint64_t> leak_sequence_{0};
  mutable std::mutex leak_mutex_;
  std::unordered_map<std::byte*, OutstandingMemoryAllocation> outstanding_allocations_;
#endif
  std::unordered_map<std::size_t, std::unique_ptr<NumaPool>> numa_pools_;
};

#if KSJ_MEMORY_ENABLE_TEST_ACCESS
inline std::shared_ptr<MemoryPool> MemoryPool::create_for_testing(TopologySnapshot topology,
                                                                  MemoryPoolOptions options) {
  return create(std::move(topology), options);
}

inline MemoryLease MemoryPool::acquire_for_testing(const AllocationRequest& request, PlacementDecision placement) {
  return acquire(request, placement);
}

inline MemoryPoolStatsSnapshot MemoryPool::stats_snapshot_for_testing() const {
  return stats_snapshot();
}
#endif

} // namespace ksj::memory
