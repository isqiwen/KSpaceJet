#include "kspacejet/memory/stats_formatter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>

namespace ksj::memory {

#if KSJ_MEMORY_ENABLE_STATS
namespace {

[[nodiscard]] std::string format_bytes(const std::uint64_t bytes) {
  static constexpr std::array<const char*, 6> kUnits = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};

  double value = static_cast<double>(bytes);
  std::size_t unit_index = 0;
  while (value >= 1024.0 && unit_index + 1 < kUnits.size()) {
    value /= 1024.0;
    ++unit_index;
  }

  std::ostringstream out;
  if (unit_index == 0) {
    out << bytes << ' ' << kUnits[unit_index];
  } else {
    out << std::fixed << std::setprecision(value < 10.0 ? 2 : 1) << value << ' ' << kUnits[unit_index];
  }
  return out.str();
}

[[nodiscard]] std::string format_block_size(const std::size_t bytes) {
  return format_bytes(static_cast<std::uint64_t>(bytes));
}

[[nodiscard]] std::string format_percent(const std::uint64_t numerator, const std::uint64_t denominator) {
  if (denominator == 0) {
    return "0.00%";
  }

  const auto percent =
    (static_cast<long double>(numerator) / static_cast<long double>(denominator)) * static_cast<long double>(100.0);
  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << static_cast<double>(percent) << '%';
  return out.str();
}

[[nodiscard]] std::string make_bar(const std::uint64_t active_blocks, const std::uint64_t total_blocks,
                                   const std::size_t bar_width) {
  const auto width = std::clamp<std::size_t>(bar_width, 1, 128);
  if (total_blocks == 0) {
    return std::string(width, '.');
  }

  auto filled =
    static_cast<std::size_t>((static_cast<long double>(active_blocks) / static_cast<long double>(total_blocks)) *
                             static_cast<long double>(width));
  if (active_blocks > 0 && filled == 0) {
    filled = 1;
  }
  filled = std::min(filled, width);

  std::string bar;
  bar.reserve(width);
  bar.append(filled, '#');
  bar.append(width - filled, '.');
  return bar;
}

[[nodiscard]] bool should_include_size_class(const SizeClassMemoryStats& stats,
                                             const MemoryPoolHistogramOptions& options) noexcept {
  if (options.include_empty_size_classes) {
    return true;
  }
  return stats.slab_count != 0 || stats.allocations != 0 || stats.reuse_hits != 0 || stats.misses != 0;
}

} // namespace
#endif

std::string format_memory_pool_histogram(const MemoryPoolStatsSnapshot& snapshot,
                                         const MemoryPoolHistogramOptions options) {
#if KSJ_MEMORY_ENABLE_STATS
  if (snapshot.numa.empty()) {
    return {};
  }

  std::ostringstream out;
  if (options.include_header) {
    out << "kspacejet-memory pool stats\n";
    out << "requests=" << snapshot.requests << " pool=" << snapshot.pool_allocations
        << " reuse=" << snapshot.pool_reuse_hits << " slabs=" << snapshot.slab_creations
        << " direct=" << snapshot.direct_allocations << " releases=" << snapshot.releases
        << " failed_releases=" << snapshot.failed_releases
        << " remote_release_suspects=" << snapshot.remote_release_suspects
        << " unknown_release_cpus=" << snapshot.unknown_release_cpus
        << " larger_class_spills=" << snapshot.larger_class_spills << " direct_fallbacks=" << snapshot.direct_fallbacks
        << " worker_node_mismatches=" << snapshot.worker_node_mismatches
        << " requested=" << format_bytes(snapshot.requested_bytes)
        << " allocated=" << format_bytes(snapshot.allocated_bytes) << '\n';
  }

  for (const auto& node : snapshot.numa) {
    out << "NUMA node " << node.numa_node << " reserved=" << format_bytes(node.reserved_bytes)
        << " active=" << format_bytes(node.active_bytes) << " cached=" << format_bytes(node.cached_bytes)
        << " slabs=" << node.slab_count << '\n';

    for (const auto& size_class : node.size_classes) {
      if (!should_include_size_class(size_class, options)) {
        continue;
      }

      const auto total_blocks = size_class.active_blocks + size_class.free_blocks;
      const auto free_bytes = size_class.free_blocks * static_cast<std::uint64_t>(size_class.block_size);
      out << "  " << std::setw(9) << format_block_size(size_class.block_size) << " ["
          << make_bar(size_class.active_blocks, total_blocks, options.bar_width) << "]"
          << " util=" << format_percent(size_class.active_blocks, total_blocks)
          << " active=" << size_class.active_blocks << '/' << total_blocks << " free=" << size_class.free_blocks
          << " reserved=" << format_bytes(size_class.reserved_bytes)
          << " active_bytes=" << format_bytes(size_class.active_bytes) << " free_bytes=" << format_bytes(free_bytes)
          << " cached=" << format_bytes(size_class.cached_bytes) << " alloc=" << size_class.allocations
          << " reuse=" << size_class.reuse_hits << " miss=" << size_class.misses << '\n';
    }
  }

  return out.str();
#else
  (void)snapshot;
  (void)options;
  return {};
#endif
}

} // namespace ksj::memory
