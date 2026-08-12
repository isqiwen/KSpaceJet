#pragma once

#include <cstddef>
#include <string_view>

#include "kspacejet/memory/allocation_properties.hpp"
#include "kspacejet/memory/topology.hpp"

namespace ksj::memory {

struct PlacementDecision {
  std::size_t numa_node{0};
  Locality locality{Locality::worker_local};
};

class PlacementPolicy {
public:
  PlacementPolicy();
  explicit PlacementPolicy(TopologySnapshot topology);

  [[nodiscard]] const TopologySnapshot& topology() const noexcept { return topology_; }
  [[nodiscard]] PlacementDecision decide(const AllocationProperties& properties, std::size_t worker_index = 0,
                                         std::string_view shard_key = {}) const;

private:
  [[nodiscard]] std::size_t first_node() const noexcept;
  [[nodiscard]] std::size_t worker_node(std::size_t worker_index) const noexcept;
  [[nodiscard]] std::size_t shard_node(std::string_view shard_key) const noexcept;
  [[nodiscard]] std::size_t socket_node(std::size_t worker_index) const noexcept;
  [[nodiscard]] std::size_t interleaved_node(std::size_t worker_index) const noexcept;

  TopologySnapshot topology_{};
};

} // namespace ksj::memory
