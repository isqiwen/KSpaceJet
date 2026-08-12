#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ksj::memory {

enum class AllocatorKind : std::uint8_t {
  host_pool,
  host_direct,
};

enum class MemorySpaceKind : std::uint8_t {
  numa_host,
  pinned_host,
  device,
  unified,
};

enum class Initialization : std::uint8_t {
  none,
  zero,
};

enum class PagePolicy : std::uint8_t {
  normal,
  transparent_hugepage,
  explicit_hugepage,
};

enum class Locality : std::uint8_t {
  global,
  worker_local,
  shard_local,
  socket_local,
  explicit_numa,
  interleaved,
};

struct AllocationProperties {
  std::string label;
  AllocatorKind allocator{AllocatorKind::host_pool};
  MemorySpaceKind space_kind{MemorySpaceKind::numa_host};
  Locality locality{Locality::worker_local};
  std::optional<std::size_t> numa_node{};
  std::size_t alignment{64};
  Initialization initialization{Initialization::none};
  PagePolicy page_policy{PagePolicy::normal};
  bool allow_larger_class{true};
};

} // namespace ksj::memory
