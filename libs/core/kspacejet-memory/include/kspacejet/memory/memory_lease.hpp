#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include "kspacejet/memory/allocation_properties.hpp"
#include "kspacejet/memory/memory_space.hpp"
#include "kspacejet/memory/memory_view.hpp"

namespace ksj::memory {

namespace detail {
class BitsetSlab;
}

class MemoryPool;

struct AllocationRecord {
  std::byte* data{nullptr};
  std::size_t requested_bytes{0};
  std::size_t capacity_bytes{0};
  std::size_t mapped_bytes{0};
  std::size_t numa_node{0};
  std::size_t size_class_bytes{0};
  detail::BitsetSlab* slab{nullptr};
  RawAllocationKind allocation_kind{RawAllocationKind::numa_alloc};
  bool locked{false};
  bool direct{false};
  bool reused{false};
};

class MemoryLease {
public:
  MemoryLease() = default;
  MemoryLease(std::shared_ptr<MemoryPool> pool, AllocationRecord record) noexcept;
  MemoryLease(const MemoryLease&) = delete;
  MemoryLease& operator=(const MemoryLease&) = delete;
  MemoryLease(MemoryLease&& other) noexcept;
  MemoryLease& operator=(MemoryLease&& other) noexcept;
  ~MemoryLease();

  void release() noexcept;

  [[nodiscard]] std::byte* data() noexcept { return record_.data; }
  [[nodiscard]] const std::byte* data() const noexcept { return record_.data; }
  [[nodiscard]] std::size_t size() const noexcept { return record_.requested_bytes; }
  [[nodiscard]] std::size_t capacity() const noexcept { return record_.capacity_bytes; }
  [[nodiscard]] std::size_t numa_node() const noexcept { return record_.numa_node; }
  [[nodiscard]] bool direct() const noexcept { return record_.direct; }
  [[nodiscard]] bool reused() const noexcept { return record_.reused; }
  [[nodiscard]] explicit operator bool() const noexcept { return record_.data != nullptr; }
  [[nodiscard]] std::span<std::byte> span() noexcept { return {record_.data, record_.requested_bytes}; }
  [[nodiscard]] std::span<const std::byte> span() const noexcept { return {record_.data, record_.requested_bytes}; }
  [[nodiscard]] MemoryView view() noexcept { return {record_.data, record_.requested_bytes, record_.numa_node}; }

private:
  std::shared_ptr<MemoryPool> pool_{};
  AllocationRecord record_{};
};

} // namespace ksj::memory
