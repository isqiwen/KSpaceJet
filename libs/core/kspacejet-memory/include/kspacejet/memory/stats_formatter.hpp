#pragma once

#ifndef KSJ_MEMORY_ENABLE_STATS
#define KSJ_MEMORY_ENABLE_STATS 0
#endif

#include <cstddef>
#include <string>

#include "kspacejet/memory/stats.hpp"

namespace ksj::memory {

struct MemoryPoolHistogramOptions {
  std::size_t bar_width{32};
  bool include_empty_size_classes{false};
  bool include_header{true};
};

[[nodiscard]] std::string format_memory_pool_histogram(const MemoryPoolStatsSnapshot& snapshot,
                                                       MemoryPoolHistogramOptions options = {});

} // namespace ksj::memory
