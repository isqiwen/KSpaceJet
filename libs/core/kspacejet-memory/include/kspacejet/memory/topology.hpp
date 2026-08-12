#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ksj::memory {

struct CpuCoreInfo {
  std::size_t id{0};
  std::size_t socket{0};
  std::size_t numa_node{0};
  std::size_t core_id{0};
  std::size_t smt_index{0};
};

struct NumaNodeInfo {
  std::size_t id{0};
  std::vector<std::size_t> cpu_ids;
};

struct TopologySnapshot {
  std::vector<NumaNodeInfo> numa_nodes;
  std::vector<CpuCoreInfo> cpu_cores;
  std::vector<std::size_t> process_cpu_affinity;
  std::unordered_map<std::size_t, std::size_t> cpu_to_numa;
  std::unordered_map<std::size_t, std::size_t> cpu_to_socket;
  std::unordered_map<std::size_t, std::size_t> worker_to_cpu;
  std::unordered_map<std::size_t, std::size_t> worker_to_numa;
  std::unordered_map<std::size_t, std::size_t> worker_to_socket;
  std::vector<std::size_t> socket_ids;
  std::unordered_map<std::size_t, std::vector<std::size_t>> socket_to_numa_nodes;

  [[nodiscard]] std::optional<std::size_t> first_numa_node() const noexcept;
  [[nodiscard]] bool has_numa_node(std::size_t node_id) const noexcept;
  [[nodiscard]] std::optional<std::size_t> numa_node_for_cpu(std::size_t cpu_id) const noexcept;
  [[nodiscard]] std::optional<std::size_t> socket_for_cpu(std::size_t cpu_id) const noexcept;
  [[nodiscard]] std::optional<std::size_t> numa_node_for_worker(std::size_t worker_index) const noexcept;
  [[nodiscard]] std::optional<std::size_t> socket_for_worker(std::size_t worker_index) const noexcept;
  void bind_worker_to_cpu(std::size_t worker_index, std::size_t cpu_id);
  void bind_worker_to_numa(std::size_t worker_index, std::size_t numa_node);
};

class TopologyDiscovery {
public:
  [[nodiscard]] static TopologySnapshot discover();
};

} // namespace ksj::memory
