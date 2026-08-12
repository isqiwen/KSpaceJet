#pragma once

#include "kspacejet/memory/topology.hpp"
#include "kspacejet/threading/thread_pool.hpp"

#include <vector>

namespace ksj::threading {

inline void apply_worker_affinity(ksj::memory::TopologySnapshot& topology,
                                  const std::vector<ThreadPoolWorkerInfo>& workers) {
  for (const auto& worker : workers) {
    if (worker.assigned_cpu.has_value()) {
      topology.bind_worker_to_cpu(worker.worker_index, *worker.assigned_cpu);
    }
  }
}

[[nodiscard]] inline ksj::memory::TopologySnapshot
topology_with_worker_affinity(ksj::memory::TopologySnapshot topology,
                              const std::vector<ThreadPoolWorkerInfo>& workers) {
  apply_worker_affinity(topology, workers);
  return topology;
}

} // namespace ksj::threading
