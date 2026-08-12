#pragma once

#include <cstddef>
#include <string_view>

#include "kspacejet/memory/allocation_properties.hpp"

namespace ksj::memory {

enum class RawAllocationKind {
  numa_alloc,
  mmap_hugepage,
};

struct RawAllocation {
  std::byte* data{nullptr};
  std::size_t bytes{0};
  std::size_t mapped_bytes{0};
  std::size_t numa_node{0};
  RawAllocationKind kind{RawAllocationKind::numa_alloc};
  bool locked{false};
};

class NumaHostSpace {
public:
  explicit NumaHostSpace(std::size_t numa_node = 0) noexcept;

  [[nodiscard]] std::size_t numa_node() const noexcept { return numa_node_; }
  [[nodiscard]] static const char* name() noexcept;
  [[nodiscard]] static bool available() noexcept;
  [[nodiscard]] static std::size_t cache_line_size() noexcept;

  [[nodiscard]] RawAllocation allocate(std::string_view label, std::size_t bytes, std::size_t alignment = 64,
                                       PagePolicy page_policy = PagePolicy::normal, bool lock_pages = false) const;
  void deallocate(RawAllocation allocation) const noexcept;

private:
  std::size_t numa_node_{0};
};

} // namespace ksj::memory
