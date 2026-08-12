#pragma once

#ifndef KSJ_MEMORY_ENABLE_STATS
#define KSJ_MEMORY_ENABLE_STATS 0
#endif

#ifndef KSJ_MEMORY_ENABLE_LEAK_TRACKING
#define KSJ_MEMORY_ENABLE_LEAK_TRACKING 0
#endif

#if KSJ_MEMORY_ENABLE_STATS
#include <atomic>
#endif

#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ksj::memory {

struct SizeClassMemoryStats {
  std::size_t block_size{0};
  std::size_t block_count{0};
  std::uint64_t slab_count{0};
  std::uint64_t active_blocks{0};
  std::uint64_t free_blocks{0};
  std::uint64_t reserved_bytes{0};
  std::uint64_t active_bytes{0};
  std::uint64_t cached_bytes{0};
  std::uint64_t allocations{0};
  std::uint64_t reuse_hits{0};
  std::uint64_t misses{0};
};

struct NumaMemoryStats {
  std::size_t numa_node{0};
  std::uint64_t reserved_bytes{0};
  std::uint64_t active_bytes{0};
  std::uint64_t cached_bytes{0};
  std::uint64_t slab_count{0};
  std::vector<SizeClassMemoryStats> size_classes;
};

struct MemoryPoolStatsSnapshot {
  std::uint64_t requests{0};
  std::uint64_t pool_allocations{0};
  std::uint64_t pool_reuse_hits{0};
  std::uint64_t slab_creations{0};
  std::uint64_t direct_allocations{0};
  std::uint64_t releases{0};
  std::uint64_t failed_releases{0};
  std::uint64_t remote_release_suspects{0};
  std::uint64_t unknown_release_cpus{0};
  std::uint64_t larger_class_spills{0};
  std::uint64_t direct_fallbacks{0};
  std::uint64_t worker_node_mismatches{0};
  std::uint64_t requested_bytes{0};
  std::uint64_t allocated_bytes{0};
  std::vector<NumaMemoryStats> numa;
};

struct OutstandingMemoryAllocation {
  std::uintptr_t address{0};
  std::uint64_t sequence_id{0};
  std::size_t requested_bytes{0};
  std::size_t capacity_bytes{0};
  std::size_t numa_node{0};
  std::size_t size_class_bytes{0};
  std::size_t worker_index{0};
  std::string label;
  std::string shard_key;
  std::thread::id thread_id{};
  bool direct{false};
};

class MemoryPoolStats {
public:
#if KSJ_MEMORY_ENABLE_STATS
  void record_pool_allocation(std::uint64_t requested, std::uint64_t allocated, bool reused,
                              bool larger_class_spill) noexcept {
    requests_.fetch_add(1, std::memory_order_relaxed);
    pool_allocations_.fetch_add(1, std::memory_order_relaxed);
    if (reused) {
      pool_reuse_hits_.fetch_add(1, std::memory_order_relaxed);
    }
    if (larger_class_spill) {
      larger_class_spills_.fetch_add(1, std::memory_order_relaxed);
    }
    requested_bytes_.fetch_add(requested, std::memory_order_relaxed);
    allocated_bytes_.fetch_add(allocated, std::memory_order_relaxed);
  }

  void record_slab_creation() noexcept { slab_creations_.fetch_add(1, std::memory_order_relaxed); }

  void record_direct_allocation(std::uint64_t requested, std::uint64_t allocated, bool fallback) noexcept {
    requests_.fetch_add(1, std::memory_order_relaxed);
    direct_allocations_.fetch_add(1, std::memory_order_relaxed);
    if (fallback) {
      direct_fallbacks_.fetch_add(1, std::memory_order_relaxed);
    }
    requested_bytes_.fetch_add(requested, std::memory_order_relaxed);
    allocated_bytes_.fetch_add(allocated, std::memory_order_relaxed);
  }

  void record_worker_node_mismatch() noexcept { worker_node_mismatches_.fetch_add(1, std::memory_order_relaxed); }

  void record_release(bool ok, bool remote_suspect, bool unknown_cpu) noexcept {
    releases_.fetch_add(1, std::memory_order_relaxed);
    if (!ok) {
      failed_releases_.fetch_add(1, std::memory_order_relaxed);
    }
    if (ok && remote_suspect) {
      remote_release_suspects_.fetch_add(1, std::memory_order_relaxed);
    }
    if (ok && unknown_cpu) {
      unknown_release_cpus_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  [[nodiscard]] MemoryPoolStatsSnapshot snapshot(std::vector<NumaMemoryStats> numa = {}) const {
    return MemoryPoolStatsSnapshot{
      .requests = requests_.load(std::memory_order_relaxed),
      .pool_allocations = pool_allocations_.load(std::memory_order_relaxed),
      .pool_reuse_hits = pool_reuse_hits_.load(std::memory_order_relaxed),
      .slab_creations = slab_creations_.load(std::memory_order_relaxed),
      .direct_allocations = direct_allocations_.load(std::memory_order_relaxed),
      .releases = releases_.load(std::memory_order_relaxed),
      .failed_releases = failed_releases_.load(std::memory_order_relaxed),
      .remote_release_suspects = remote_release_suspects_.load(std::memory_order_relaxed),
      .unknown_release_cpus = unknown_release_cpus_.load(std::memory_order_relaxed),
      .larger_class_spills = larger_class_spills_.load(std::memory_order_relaxed),
      .direct_fallbacks = direct_fallbacks_.load(std::memory_order_relaxed),
      .worker_node_mismatches = worker_node_mismatches_.load(std::memory_order_relaxed),
      .requested_bytes = requested_bytes_.load(std::memory_order_relaxed),
      .allocated_bytes = allocated_bytes_.load(std::memory_order_relaxed),
      .numa = std::move(numa),
    };
  }
#else
  void record_pool_allocation(std::uint64_t, std::uint64_t, bool, bool) noexcept {}
  void record_slab_creation() noexcept {}
  void record_direct_allocation(std::uint64_t, std::uint64_t, bool) noexcept {}
  void record_worker_node_mismatch() noexcept {}
  void record_release(bool, bool, bool) noexcept {}
  [[nodiscard]] MemoryPoolStatsSnapshot snapshot(std::vector<NumaMemoryStats> = {}) const { return {}; }
#endif

private:
#if KSJ_MEMORY_ENABLE_STATS
  std::atomic<std::uint64_t> requests_{0};
  std::atomic<std::uint64_t> pool_allocations_{0};
  std::atomic<std::uint64_t> pool_reuse_hits_{0};
  std::atomic<std::uint64_t> slab_creations_{0};
  std::atomic<std::uint64_t> direct_allocations_{0};
  std::atomic<std::uint64_t> releases_{0};
  std::atomic<std::uint64_t> failed_releases_{0};
  std::atomic<std::uint64_t> remote_release_suspects_{0};
  std::atomic<std::uint64_t> unknown_release_cpus_{0};
  std::atomic<std::uint64_t> larger_class_spills_{0};
  std::atomic<std::uint64_t> direct_fallbacks_{0};
  std::atomic<std::uint64_t> worker_node_mismatches_{0};
  std::atomic<std::uint64_t> requested_bytes_{0};
  std::atomic<std::uint64_t> allocated_bytes_{0};
#endif
};

} // namespace ksj::memory
