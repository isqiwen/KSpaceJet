#include "kspacejet/memory/memory_pool.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#if KSJ_MEMORY_ENABLE_LEAK_TRACKING
#include <mutex>
#endif
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#if KSJ_MEMORY_ENABLE_LEAK_TRACKING
#include <string>
#include <thread>
#endif
#include <utility>
#include <vector>

#if KSJ_MEMORY_ENABLE_NUMA_DIAGNOSTICS && defined(__linux__)
#include <sched.h>
#endif

namespace ksj::memory {

namespace {

[[nodiscard]] std::size_t worker_hint_count_for(const TopologySnapshot& topology) noexcept {
  std::size_t count = std::max(topology.process_cpu_affinity.size(), topology.cpu_cores.size());

  for (const auto& [worker_index, _] : topology.worker_to_cpu) {
    count = std::max(count, worker_index + 1U);
  }
  for (const auto& [worker_index, _] : topology.worker_to_numa) {
    count = std::max(count, worker_index + 1U);
  }
  for (const auto& [worker_index, _] : topology.worker_to_socket) {
    count = std::max(count, worker_index + 1U);
  }

  return count;
}

[[nodiscard]] std::vector<std::size_t> normalized_size_classes(const std::vector<std::size_t>& configured) {
  if (configured.empty()) {
    throw std::invalid_argument("memory pool size classes must be configured explicitly");
  }
  auto size_classes = configured;
  for (std::size_t index = 0; index < size_classes.size(); ++index) {
    if (size_classes[index] == 0) {
      throw std::invalid_argument("memory pool size classes must be greater than zero");
    }
    if (index != 0 && size_classes[index] <= size_classes[index - 1]) {
      throw std::invalid_argument("memory pool size classes must be strictly increasing");
    }
  }
  return size_classes;
}

[[nodiscard]] std::vector<std::size_t> normalized_size_class_block_counts(const std::vector<std::size_t>& configured,
                                                                          std::span<const std::size_t> size_classes) {
  if (configured.empty()) {
    throw std::invalid_argument("memory pool size class block counts must be configured explicitly");
  }

  if (configured.size() != size_classes.size()) {
    throw std::invalid_argument("memory pool size class block count entries must match size class entries");
  }
  for (const std::size_t block_count : configured) {
    if (block_count == 0) {
      throw std::invalid_argument("memory pool size class block counts must be greater than zero");
    }
  }
  return configured;
}

} // namespace

class MemoryPool::SizeClassPool {
public:
  struct AcquireResult {
    std::byte* data{nullptr};
    detail::BitsetSlab* slab{nullptr};
    bool reused{false};
    bool grew{false};
  };

  SizeClassPool(const std::size_t numa_node, const std::size_t block_size, const std::size_t block_count,
                const std::size_t worker_hint_count)
      : numa_node_(numa_node), block_size_(block_size), block_count_(block_count),
        worker_hint_count_(worker_hint_count) {}

  [[nodiscard]] AcquireResult acquire(std::string_view label, std::size_t alignment, PagePolicy page_policy,
                                      std::size_t worker_index) {
    if (auto result = acquire_existing(worker_index); result.data != nullptr) {
      result.reused = true;
      record_success(result.reused);
      return result;
    }

    if (auto* created = create_slab(label, alignment, page_policy); created != nullptr) {
      if (auto* data = created->try_acquire(worker_index); data != nullptr) {
        record_success(false);
        return AcquireResult{
          .data = data,
          .slab = created,
          .reused = false,
          .grew = true,
        };
      }
    }

    if (auto result = acquire_existing(worker_index); result.data != nullptr) {
      result.reused = true;
      record_success(result.reused);
      return result;
    }

#if KSJ_MEMORY_ENABLE_STATS
    misses_.fetch_add(1, std::memory_order_relaxed);
#endif
    return {};
  }

  void trim_empty() noexcept {
    if (slab_lifecycle_.test_and_set(std::memory_order_acquire)) {
      return;
    }

    auto* slab = slab_.load(std::memory_order_acquire);
    if (slab != nullptr && slab->empty()) {
      slab_.store(nullptr, std::memory_order_release);
      owned_slab_.reset();
    }
    slab_lifecycle_.clear(std::memory_order_release);
  }

  [[nodiscard]] SizeClassMemoryStats stats() const {
    SizeClassMemoryStats stats;
    stats.block_size = block_size_;
    stats.block_count = block_count_;
#if KSJ_MEMORY_ENABLE_STATS
    stats.allocations = allocations_.load(std::memory_order_relaxed);
    stats.reuse_hits = reuse_hits_.load(std::memory_order_relaxed);
    stats.misses = misses_.load(std::memory_order_relaxed);
#endif

    const auto* slab = slab_.load(std::memory_order_acquire);
    if (slab != nullptr) {
      const auto capacity = slab->capacity_bytes();
      const auto active = slab->active_bytes();
      const auto active_blocks = slab->active_blocks();
      stats.slab_count = 1;
      stats.active_blocks = active_blocks;
      stats.free_blocks = block_count_ - active_blocks;
      stats.reserved_bytes += capacity;
      stats.active_bytes += active;
      stats.cached_bytes += capacity - active;
    }
    return stats;
  }

private:
  [[nodiscard]] AcquireResult acquire_existing(const std::size_t worker_index) noexcept {
    auto* slab = slab_.load(std::memory_order_acquire);
    if (slab != nullptr) {
      if (auto* data = slab->try_acquire(worker_index); data != nullptr) {
        return AcquireResult{
          .data = data,
          .slab = slab,
        };
      }
    }
    return {};
  }

  [[nodiscard]] detail::BitsetSlab* create_slab(std::string_view label, std::size_t alignment, PagePolicy page_policy) {
    if (slab_.load(std::memory_order_acquire) != nullptr) {
      return nullptr;
    }
    if (slab_lifecycle_.test_and_set(std::memory_order_acquire)) {
      return nullptr;
    }

    try {
      if (slab_.load(std::memory_order_acquire) != nullptr) {
        slab_lifecycle_.clear(std::memory_order_release);
        return nullptr;
      }

      auto slab = std::make_unique<detail::BitsetSlab>(numa_node_, block_size_, block_count_, alignment, page_policy,
                                                       worker_hint_count_, label);
      auto* raw = slab.get();
      owned_slab_ = std::move(slab);
      slab_.store(raw, std::memory_order_release);
      slab_lifecycle_.clear(std::memory_order_release);
      return raw;
    } catch (...) {
      slab_lifecycle_.clear(std::memory_order_release);
      throw;
    }
  }

  void record_success(const bool reused) noexcept {
#if KSJ_MEMORY_ENABLE_STATS
    allocations_.fetch_add(1, std::memory_order_relaxed);
    if (reused) {
      reuse_hits_.fetch_add(1, std::memory_order_relaxed);
    }
#else
    (void)reused;
#endif
  }

  std::size_t numa_node_{0};
  std::size_t block_size_{0};
  std::size_t block_count_{0};
  std::size_t worker_hint_count_{0};
#if KSJ_MEMORY_ENABLE_STATS
  std::atomic<std::uint64_t> allocations_{0};
  std::atomic<std::uint64_t> reuse_hits_{0};
  std::atomic<std::uint64_t> misses_{0};
#endif
  std::atomic<detail::BitsetSlab*> slab_{nullptr};
  std::unique_ptr<detail::BitsetSlab> owned_slab_{};
  std::atomic_flag slab_lifecycle_ = ATOMIC_FLAG_INIT;
};

class MemoryPool::NumaPool {
public:
  explicit NumaPool(const std::size_t numa_node, std::span<const std::size_t> size_classes,
                    std::span<const std::size_t> size_class_block_counts, const std::size_t worker_hint_count)
      : numa_node_(numa_node) {
    if (size_classes.size() != size_class_block_counts.size()) {
      throw std::invalid_argument("memory pool size class block count entries must match size class entries");
    }
    pools_.reserve(size_classes.size());
    for (std::size_t index = 0; index < size_classes.size(); ++index) {
      pools_.push_back(std::make_unique<SizeClassPool>(numa_node, size_classes[index], size_class_block_counts[index],
                                                       worker_hint_count));
    }
  }

  [[nodiscard]] SizeClassPool& pool(std::size_t class_index) noexcept { return *pools_[class_index]; }

  void trim_empty() noexcept {
    for (auto& pool : pools_) {
      pool->trim_empty();
    }
  }

  [[nodiscard]] NumaMemoryStats stats() const {
    NumaMemoryStats stats;
    stats.numa_node = numa_node_;
    stats.size_classes.reserve(pools_.size());
    for (const auto& pool : pools_) {
      auto size_class_stats = pool->stats();
      stats.reserved_bytes += size_class_stats.reserved_bytes;
      stats.active_bytes += size_class_stats.active_bytes;
      stats.cached_bytes += size_class_stats.cached_bytes;
      stats.slab_count += size_class_stats.slab_count;
      stats.size_classes.push_back(std::move(size_class_stats));
    }
    return stats;
  }

private:
  std::size_t numa_node_{0};
  std::vector<std::unique_ptr<SizeClassPool>> pools_;
};

MemoryPool::MemoryPool(TopologySnapshot topology, MemoryPoolOptions options)
    : topology_(std::move(topology)), options_(std::move(options)) {
  if (topology_.numa_nodes.empty()) {
    topology_.numa_nodes.push_back(NumaNodeInfo{.id = 0, .cpu_ids = {}});
  }
  if (!options_.pooling_enabled) {
    return;
  }
  size_classes_ = normalized_size_classes(options_.size_classes);
  size_class_block_counts_ = normalized_size_class_block_counts(options_.size_class_block_counts, size_classes_);
  const auto worker_hint_count = worker_hint_count_for(topology_);
  for (const auto& node : topology_.numa_nodes) {
    numa_pools_.emplace(
      node.id, std::make_unique<NumaPool>(node.id, size_classes_, size_class_block_counts_, worker_hint_count));
  }
}

std::shared_ptr<MemoryPool> MemoryPool::create(TopologySnapshot topology, MemoryPoolOptions options) {
  return std::shared_ptr<MemoryPool>(new MemoryPool(std::move(topology), options));
}

MemoryPool::~MemoryPool() = default;

MemoryLease MemoryPool::acquire(const AllocationRequest& request, const PlacementDecision placement) {
  if (request.bytes == 0) {
    return {};
  }
  switch (request.properties.space_kind) {
    case MemorySpaceKind::numa_host:
      break;
    case MemorySpaceKind::pinned_host:
      if (request.properties.allocator != AllocatorKind::host_direct) {
        throw std::invalid_argument("pinned_host currently requires host_direct allocator");
      }
      break;
    case MemorySpaceKind::device:
    case MemorySpaceKind::unified:
      throw std::invalid_argument("unsupported memory space kind");
  }
  if (request.properties.allocator == AllocatorKind::host_direct) {
    return acquire_direct(request, placement.numa_node, false);
  }
  if (request.properties.space_kind != MemorySpaceKind::numa_host) {
    throw std::invalid_argument("pooled allocations currently require numa_host memory space");
  }
  if (!options_.pooling_enabled) {
    return acquire_direct(request, placement.numa_node, false);
  }

  const auto class_index = detail::size_class_index_for(size_classes_, request.bytes);
  if (!class_index.has_value()) {
    if (!options_.direct_fallback) {
      throw std::bad_alloc();
    }
    return acquire_direct(request, placement.numa_node, true);
  }

  auto pool_it = numa_pools_.find(placement.numa_node);
  if (pool_it == numa_pools_.end()) {
    throw std::invalid_argument("placement NUMA node is not present in memory pool");
  }

  const auto last_index = request.properties.allow_larger_class ? size_classes_.size() - 1 : *class_index;
  for (auto index = *class_index; index <= last_index; ++index) {
    auto result = pool_it->second->pool(index).acquire(request.properties.label, request.properties.alignment,
                                                       request.properties.page_policy, request.worker_index);
    if (result.data == nullptr) {
      continue;
    }
    if (request.properties.initialization == Initialization::zero) {
      std::memset(result.data, 0, request.bytes);
    }
    if (result.grew) {
      stats_.record_slab_creation();
    }
    const auto larger_class_spill = index != *class_index;
    stats_.record_pool_allocation(request.bytes, size_classes_[index], result.reused, larger_class_spill);
#if KSJ_MEMORY_ENABLE_STATS
    record_worker_node_mismatch(request, placement.numa_node);
#endif
    AllocationRecord record{
      .data = result.data,
      .requested_bytes = request.bytes,
      .capacity_bytes = size_classes_[index],
      .mapped_bytes = size_classes_[index],
      .numa_node = placement.numa_node,
      .size_class_bytes = size_classes_[index],
      .slab = result.slab,
      .direct = false,
      .reused = result.reused,
    };
    try {
      track_allocation(record, request);
    } catch (...) {
      release(record);
      throw;
    }
    return MemoryLease(shared_from_this(), record);
  }

  if (!options_.direct_fallback) {
    throw std::bad_alloc();
  }
  return acquire_direct(request, placement.numa_node, true);
}

MemoryLease MemoryPool::acquire_direct(const AllocationRequest& request, const std::size_t numa_node,
                                       const bool fallback) {
  const auto lock_pages = request.properties.space_kind == MemorySpaceKind::pinned_host;
  auto allocation = NumaHostSpace(numa_node).allocate(
    request.properties.label, request.bytes, request.properties.alignment, request.properties.page_policy, lock_pages);
  if (request.properties.initialization == Initialization::zero && allocation.data != nullptr) {
    std::memset(allocation.data, 0, request.bytes);
  }
  const auto direct_capacity = allocation.mapped_bytes == 0 ? allocation.bytes : allocation.mapped_bytes;
  stats_.record_direct_allocation(request.bytes, direct_capacity, fallback);
#if KSJ_MEMORY_ENABLE_STATS
  record_worker_node_mismatch(request, numa_node);
#endif
  AllocationRecord record{
    .data = allocation.data,
    .requested_bytes = request.bytes,
    .capacity_bytes = direct_capacity,
    .mapped_bytes = allocation.mapped_bytes,
    .numa_node = numa_node,
    .size_class_bytes = 0,
    .slab = nullptr,
    .allocation_kind = allocation.kind,
    .locked = allocation.locked,
    .direct = true,
    .reused = false,
  };
  try {
    track_allocation(record, request);
  } catch (...) {
    release(record);
    throw;
  }
  return MemoryLease(shared_from_this(), record);
}

void MemoryPool::release(AllocationRecord record) noexcept {
  if (record.data == nullptr) {
    return;
  }

  bool ok = true;
  if (record.direct) {
    NumaHostSpace(record.numa_node)
      .deallocate(RawAllocation{
        .data = record.data,
        .bytes = record.requested_bytes,
        .mapped_bytes = record.mapped_bytes,
        .numa_node = record.numa_node,
        .kind = record.allocation_kind,
        .locked = record.locked,
      });
  } else if (record.slab != nullptr) {
    ok = record.slab->release(record.data);
  } else {
    ok = false;
  }
#if KSJ_MEMORY_ENABLE_STATS
  const auto release_probe = probe_release_numa(record);
  stats_.record_release(ok, release_probe.remote_suspect, release_probe.unknown_cpu);
#else
  stats_.record_release(ok, false, false);
#endif
  if (ok) {
    untrack_allocation(record);
  }
}

#if KSJ_MEMORY_ENABLE_STATS
std::optional<std::size_t> MemoryPool::current_thread_numa_node() const noexcept {
#if KSJ_MEMORY_ENABLE_NUMA_DIAGNOSTICS && defined(__linux__)
  const auto cpu = ::sched_getcpu();
  if (cpu < 0) {
    return std::nullopt;
  }
  return topology_.numa_node_for_cpu(static_cast<std::size_t>(cpu));
#else
  return std::nullopt;
#endif
}

MemoryPool::ReleaseNumaProbe MemoryPool::probe_release_numa(const AllocationRecord& record) const noexcept {
  const auto current_node = current_thread_numa_node();
  if (!current_node.has_value()) {
    return ReleaseNumaProbe{.unknown_cpu = true};
  }
  return ReleaseNumaProbe{.remote_suspect = *current_node != record.numa_node};
}

void MemoryPool::record_worker_node_mismatch(const AllocationRequest& request,
                                             const std::size_t placement_numa_node) noexcept {
  if (request.properties.locality != Locality::worker_local || request.properties.numa_node.has_value()) {
    return;
  }
  const auto expected_node = topology_.numa_node_for_worker(request.worker_index);
  if (expected_node.has_value() && *expected_node != placement_numa_node) {
    stats_.record_worker_node_mismatch();
  }
}
#endif

void MemoryPool::track_allocation(const AllocationRecord& record, const AllocationRequest& request) {
#if KSJ_MEMORY_ENABLE_LEAK_TRACKING
  if (!options_.leak_tracking || record.data == nullptr) {
    return;
  }

  OutstandingMemoryAllocation allocation{
    .address = reinterpret_cast<std::uintptr_t>(record.data),
    .sequence_id = leak_sequence_.fetch_add(1, std::memory_order_relaxed) + 1,
    .requested_bytes = record.requested_bytes,
    .capacity_bytes = record.capacity_bytes,
    .numa_node = record.numa_node,
    .size_class_bytes = record.size_class_bytes,
    .worker_index = request.worker_index,
    .label = std::string(request.properties.label),
    .shard_key = std::string(request.shard_key),
    .thread_id = std::this_thread::get_id(),
    .direct = record.direct,
  };

  std::lock_guard lock(leak_mutex_);
  outstanding_allocations_.insert_or_assign(record.data, std::move(allocation));
#else
  (void)record;
  (void)request;
#endif
}

void MemoryPool::untrack_allocation(const AllocationRecord& record) noexcept {
#if KSJ_MEMORY_ENABLE_LEAK_TRACKING
  if (!options_.leak_tracking || record.data == nullptr) {
    return;
  }

  std::lock_guard lock(leak_mutex_);
  outstanding_allocations_.erase(record.data);
#else
  (void)record;
#endif
}

void MemoryPool::trim_empty_slabs() noexcept {
  for (auto& [_, pool] : numa_pools_) {
    pool->trim_empty();
  }
}

std::vector<NumaMemoryStats> MemoryPool::collect_numa_stats() const {
  std::vector<NumaMemoryStats> stats;
  stats.reserve(numa_pools_.size());
  for (const auto& [_, pool] : numa_pools_) {
    stats.push_back(pool->stats());
  }
  std::sort(stats.begin(), stats.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.numa_node < rhs.numa_node;
  });
  return stats;
}

MemoryPoolStatsSnapshot MemoryPool::stats_snapshot() const {
#if KSJ_MEMORY_ENABLE_STATS
  return stats_.snapshot(collect_numa_stats());
#else
  return {};
#endif
}

std::vector<OutstandingMemoryAllocation> MemoryPool::outstanding_allocations() const {
#if KSJ_MEMORY_ENABLE_LEAK_TRACKING
  if (!options_.leak_tracking) {
    return {};
  }

  std::vector<OutstandingMemoryAllocation> allocations;
  std::lock_guard lock(leak_mutex_);
  allocations.reserve(outstanding_allocations_.size());
  for (const auto& [_, allocation] : outstanding_allocations_) {
    allocations.push_back(allocation);
  }
  std::sort(allocations.begin(), allocations.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.sequence_id < rhs.sequence_id;
  });
  return allocations;
#else
  return {};
#endif
}

bool MemoryPool::check_no_leaks() const {
#if KSJ_MEMORY_ENABLE_LEAK_TRACKING
  if (!options_.leak_tracking) {
    return true;
  }

  std::lock_guard lock(leak_mutex_);
  return outstanding_allocations_.empty();
#else
  return true;
#endif
}

} // namespace ksj::memory
