#include "kspacejet/memory/topology.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>

#if defined(__linux__)
#include <sched.h>
#endif

namespace ksj::memory {
namespace {

std::optional<std::string> read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return std::nullopt;
  }
  std::string text;
  std::getline(in, text);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  return text;
}

std::optional<std::size_t> parse_size_t(const std::string_view text) {
  std::size_t value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::size_t read_size_t_or(const std::filesystem::path& path, const std::size_t fallback) {
  if (const auto text = read_text_file(path); text.has_value()) {
    if (const auto value = parse_size_t(*text); value.has_value()) {
      return *value;
    }
  }
  return fallback;
}

std::vector<std::size_t> parse_index_list(const std::string_view text) {
  std::vector<std::size_t> values;
  std::size_t start = 0;
  while (start < text.size()) {
    const auto comma = text.find(',', start);
    const auto token = text.substr(start, comma == std::string_view::npos ? text.size() - start : comma - start);
    const auto dash = token.find('-');
    if (dash == std::string_view::npos) {
      if (const auto value = parse_size_t(token); value.has_value()) {
        values.push_back(*value);
      }
    } else {
      const auto left = parse_size_t(token.substr(0, dash));
      const auto right = parse_size_t(token.substr(dash + 1));
      if (left.has_value() && right.has_value() && *left <= *right) {
        for (std::size_t value = *left; value <= *right; ++value) {
          values.push_back(value);
        }
      }
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

std::vector<std::size_t> detect_process_affinity() {
  std::vector<std::size_t> cpus;
#if defined(__linux__)
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (sched_getaffinity(0, sizeof(mask), &mask) == 0) {
    for (std::size_t cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
      if (CPU_ISSET(cpu, &mask)) {
        cpus.push_back(cpu);
      }
    }
  }
#else
  const auto count = std::max(1U, std::thread::hardware_concurrency());
  cpus.reserve(count);
  for (unsigned cpu = 0; cpu < count; ++cpu) {
    cpus.push_back(cpu);
  }
#endif
  return cpus;
}

std::vector<NumaNodeInfo> detect_numa_nodes() {
#if defined(__linux__)
  const std::filesystem::path sysfs_nodes{"/sys/devices/system/node"};
  std::vector<NumaNodeInfo> nodes;
  if (!std::filesystem::exists(sysfs_nodes)) {
    nodes.push_back(NumaNodeInfo{.id = 0, .cpu_ids = {}});
    return nodes;
  }

  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(sysfs_nodes, error)) {
    const auto name = entry.path().filename().string();
    if (name.rfind("node", 0) != 0) {
      continue;
    }
    const auto node_id = parse_size_t(std::string_view(name).substr(4));
    if (!node_id.has_value()) {
      continue;
    }
    NumaNodeInfo node{.id = *node_id, .cpu_ids = {}};
    if (const auto cpulist = read_text_file(entry.path() / "cpulist"); cpulist.has_value()) {
      node.cpu_ids = parse_index_list(*cpulist);
    }
    nodes.push_back(std::move(node));
  }
  if (nodes.empty()) {
    nodes.push_back(NumaNodeInfo{.id = 0, .cpu_ids = {}});
  }
  std::sort(nodes.begin(), nodes.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.id < rhs.id;
  });
  return nodes;
#else
  return {NumaNodeInfo{.id = 0, .cpu_ids = detect_process_affinity()}};
#endif
}

std::vector<CpuCoreInfo> detect_cpu_cores(const std::vector<NumaNodeInfo>& nodes) {
#if defined(__linux__)
  std::unordered_map<std::size_t, std::size_t> cpu_to_node;
  for (const auto& node : nodes) {
    for (const auto cpu_id : node.cpu_ids) {
      cpu_to_node[cpu_id] = node.id;
    }
  }

  std::vector<CpuCoreInfo> cores;
  const std::filesystem::path sysfs_cpu{"/sys/devices/system/cpu"};
  if (!std::filesystem::exists(sysfs_cpu)) {
    cores.push_back(CpuCoreInfo{.id = 0, .socket = 0, .numa_node = nodes.empty() ? 0 : nodes.front().id});
    return cores;
  }

  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(sysfs_cpu, error)) {
    const auto name = entry.path().filename().string();
    if (name.rfind("cpu", 0) != 0 || name.size() <= 3) {
      continue;
    }
    const auto cpu_id = parse_size_t(std::string_view(name).substr(3));
    if (!cpu_id.has_value()) {
      continue;
    }

    const auto topology = entry.path() / "topology";
    CpuCoreInfo info;
    info.id = *cpu_id;
    info.socket = read_size_t_or(topology / "physical_package_id", 0);
    info.core_id = read_size_t_or(topology / "core_id", *cpu_id);
    const auto node_it = cpu_to_node.find(*cpu_id);
    info.numa_node = node_it == cpu_to_node.end() ? (nodes.empty() ? 0 : nodes.front().id) : node_it->second;
    cores.push_back(info);
  }

  std::sort(cores.begin(), cores.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.id < rhs.id;
  });

  std::unordered_map<std::string, std::size_t> smt_counters;
  for (auto& core : cores) {
    const auto key = std::to_string(core.socket) + ":" + std::to_string(core.core_id);
    core.smt_index = smt_counters[key]++;
  }
  if (cores.empty()) {
    cores.push_back(CpuCoreInfo{.id = 0, .socket = 0, .numa_node = nodes.empty() ? 0 : nodes.front().id});
  }
  return cores;
#else
  const auto cpus = nodes.empty() || nodes.front().cpu_ids.empty() ? detect_process_affinity() : nodes.front().cpu_ids;
  std::vector<CpuCoreInfo> cores;
  cores.reserve(cpus.size());
  for (const auto cpu_id : cpus) {
    cores.push_back(CpuCoreInfo{.id = cpu_id, .socket = 0, .numa_node = 0, .core_id = cpu_id, .smt_index = 0});
  }
  if (cores.empty()) {
    cores.push_back(CpuCoreInfo{.id = 0, .socket = 0, .numa_node = 0, .core_id = 0, .smt_index = 0});
  }
  return cores;
#endif
}

} // namespace

std::optional<std::size_t> TopologySnapshot::first_numa_node() const noexcept {
  if (numa_nodes.empty()) {
    return std::nullopt;
  }
  return numa_nodes.front().id;
}

bool TopologySnapshot::has_numa_node(const std::size_t node_id) const noexcept {
  return std::any_of(numa_nodes.begin(), numa_nodes.end(), [&](const auto& node) {
    return node.id == node_id;
  });
}

std::optional<std::size_t> TopologySnapshot::numa_node_for_cpu(const std::size_t cpu_id) const noexcept {
  if (const auto it = cpu_to_numa.find(cpu_id); it != cpu_to_numa.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::size_t> TopologySnapshot::socket_for_cpu(const std::size_t cpu_id) const noexcept {
  if (const auto it = cpu_to_socket.find(cpu_id); it != cpu_to_socket.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::size_t> TopologySnapshot::numa_node_for_worker(const std::size_t worker_index) const noexcept {
  if (const auto it = worker_to_numa.find(worker_index); it != worker_to_numa.end()) {
    return it->second;
  }
  if (const auto it = worker_to_cpu.find(worker_index); it != worker_to_cpu.end()) {
    return numa_node_for_cpu(it->second);
  }
  return std::nullopt;
}

std::optional<std::size_t> TopologySnapshot::socket_for_worker(const std::size_t worker_index) const noexcept {
  if (const auto it = worker_to_socket.find(worker_index); it != worker_to_socket.end()) {
    return it->second;
  }
  if (const auto it = worker_to_cpu.find(worker_index); it != worker_to_cpu.end()) {
    return socket_for_cpu(it->second);
  }
  return std::nullopt;
}

void TopologySnapshot::bind_worker_to_cpu(const std::size_t worker_index, const std::size_t cpu_id) {
  worker_to_cpu[worker_index] = cpu_id;
  if (const auto numa_node = numa_node_for_cpu(cpu_id); numa_node.has_value()) {
    worker_to_numa[worker_index] = *numa_node;
  }
  if (const auto socket = socket_for_cpu(cpu_id); socket.has_value()) {
    worker_to_socket[worker_index] = *socket;
  }
}

void TopologySnapshot::bind_worker_to_numa(const std::size_t worker_index, const std::size_t numa_node) {
  worker_to_numa[worker_index] = numa_node;
}

TopologySnapshot TopologyDiscovery::discover() {
  TopologySnapshot snapshot;
  snapshot.process_cpu_affinity = detect_process_affinity();
  snapshot.numa_nodes = detect_numa_nodes();
  snapshot.cpu_cores = detect_cpu_cores(snapshot.numa_nodes);

  std::set<std::size_t> sockets;
  for (const auto& core : snapshot.cpu_cores) {
    snapshot.cpu_to_numa[core.id] = core.numa_node;
    snapshot.cpu_to_socket[core.id] = core.socket;
    sockets.insert(core.socket);
    auto& nodes = snapshot.socket_to_numa_nodes[core.socket];
    if (std::find(nodes.begin(), nodes.end(), core.numa_node) == nodes.end()) {
      nodes.push_back(core.numa_node);
    }
  }
  snapshot.socket_ids.assign(sockets.begin(), sockets.end());
  for (auto& [_, nodes] : snapshot.socket_to_numa_nodes) {
    std::sort(nodes.begin(), nodes.end());
  }
  return snapshot;
}

} // namespace ksj::memory
