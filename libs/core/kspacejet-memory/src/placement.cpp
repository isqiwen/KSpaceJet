#include "kspacejet/memory/placement.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

namespace ksj::memory {

PlacementPolicy::PlacementPolicy() : PlacementPolicy(TopologyDiscovery::discover()) {}

PlacementPolicy::PlacementPolicy(TopologySnapshot topology) : topology_(std::move(topology)) {
  if (topology_.numa_nodes.empty()) {
    topology_.numa_nodes.push_back(NumaNodeInfo{.id = 0, .cpu_ids = {}});
  }
}

std::size_t PlacementPolicy::first_node() const noexcept {
  return topology_.first_numa_node().value_or(0);
}

std::size_t PlacementPolicy::worker_node(const std::size_t worker_index) const noexcept {
  if (const auto node = topology_.numa_node_for_worker(worker_index); node.has_value()) {
    return *node;
  }
  if (!topology_.process_cpu_affinity.empty()) {
    const auto cpu_id = topology_.process_cpu_affinity[worker_index % topology_.process_cpu_affinity.size()];
    if (const auto it = topology_.cpu_to_numa.find(cpu_id); it != topology_.cpu_to_numa.end()) {
      return it->second;
    }
  }
  if (!topology_.cpu_cores.empty()) {
    return topology_.cpu_cores[worker_index % topology_.cpu_cores.size()].numa_node;
  }
  return first_node();
}

std::size_t PlacementPolicy::shard_node(const std::string_view shard_key) const noexcept {
  if (topology_.numa_nodes.empty() || shard_key.empty()) {
    return first_node();
  }
  const auto index = std::hash<std::string_view>{}(shard_key) % topology_.numa_nodes.size();
  return topology_.numa_nodes[index].id;
}

std::size_t PlacementPolicy::socket_node(const std::size_t worker_index) const noexcept {
  if (const auto socket = topology_.socket_for_worker(worker_index); socket.has_value()) {
    const auto it = topology_.socket_to_numa_nodes.find(*socket);
    if (it != topology_.socket_to_numa_nodes.end() && !it->second.empty()) {
      return it->second.front();
    }
  }
  if (topology_.socket_ids.empty()) {
    return worker_node(worker_index);
  }
  const auto socket_id = topology_.socket_ids[worker_index % topology_.socket_ids.size()];
  const auto it = topology_.socket_to_numa_nodes.find(socket_id);
  if (it == topology_.socket_to_numa_nodes.end() || it->second.empty()) {
    return worker_node(worker_index);
  }
  return it->second.front();
}

std::size_t PlacementPolicy::interleaved_node(const std::size_t worker_index) const noexcept {
  if (topology_.numa_nodes.empty()) {
    return 0;
  }
  return topology_.numa_nodes[worker_index % topology_.numa_nodes.size()].id;
}

PlacementDecision PlacementPolicy::decide(const AllocationProperties& properties, const std::size_t worker_index,
                                          const std::string_view shard_key) const {
  PlacementDecision decision;
  decision.locality = properties.locality;
  if (properties.numa_node.has_value()) {
    if (!topology_.has_numa_node(*properties.numa_node)) {
      throw std::invalid_argument("requested NUMA node is not present in topology");
    }
    decision.numa_node = *properties.numa_node;
    return decision;
  }

  switch (properties.locality) {
    case Locality::explicit_numa:
      decision.numa_node = first_node();
      break;
    case Locality::global:
      decision.numa_node = first_node();
      break;
    case Locality::worker_local:
      decision.numa_node = worker_node(worker_index);
      break;
    case Locality::shard_local:
      decision.numa_node = shard_node(shard_key);
      break;
    case Locality::socket_local:
      decision.numa_node = socket_node(worker_index);
      break;
    case Locality::interleaved:
      decision.numa_node = interleaved_node(worker_index);
      break;
  }
  return decision;
}

} // namespace ksj::memory
