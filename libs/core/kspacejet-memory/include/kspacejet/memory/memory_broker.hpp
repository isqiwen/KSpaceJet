#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "kspacejet/memory/memory_pool.hpp"
#include "kspacejet/memory/placement.hpp"

namespace ksj::memory {

class MemoryBroker {
public:
  // Must be called before instance() is first used. Returns false if the global
  // broker has already been constructed.
  [[nodiscard]] static bool configure_instance(MemoryPoolOptions options);
  [[nodiscard]] static MemoryBroker& instance();

  MemoryBroker(const MemoryBroker&) = delete;
  MemoryBroker& operator=(const MemoryBroker&) = delete;
  MemoryBroker(MemoryBroker&&) = delete;
  MemoryBroker& operator=(MemoryBroker&&) = delete;

#if KSJ_MEMORY_ENABLE_TEST_ACCESS
  [[nodiscard]] static MemoryBroker create_for_testing(MemoryPoolOptions options) { return MemoryBroker(options); }

  [[nodiscard]] static MemoryBroker create_for_testing(TopologySnapshot topology, MemoryPoolOptions options) {
    return MemoryBroker(std::move(topology), options);
  }
#endif

  [[nodiscard]] MemoryLease acquire(const AllocationRequest& request);
  // Reclaim empty slabs when all users of the broker are quiescent.
  void trim() noexcept;

  [[nodiscard]] const TopologySnapshot& topology() const noexcept { return placement_.topology(); }
  [[nodiscard]] MemoryPoolStatsSnapshot stats_snapshot() const { return pool_->stats_snapshot(); }
  [[nodiscard]] std::vector<OutstandingMemoryAllocation> outstanding_allocations() const {
    return pool_->outstanding_allocations();
  }
  [[nodiscard]] bool check_no_leaks() const { return pool_->check_no_leaks(); }

private:
  explicit MemoryBroker(MemoryPoolOptions options);
  MemoryBroker(TopologySnapshot topology, MemoryPoolOptions options);

  PlacementPolicy placement_;
  std::shared_ptr<MemoryPool> pool_;
};

} // namespace ksj::memory
