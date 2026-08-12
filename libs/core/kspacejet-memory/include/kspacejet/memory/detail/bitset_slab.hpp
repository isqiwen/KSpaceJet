#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "kspacejet/memory/allocation_properties.hpp"
#include "kspacejet/memory/memory_space.hpp"

namespace ksj::memory::detail {

class BitsetSlab {
public:
  struct alignas(64) ScanCursor {
    std::atomic<std::uint64_t> value{0};
  };

  BitsetSlab(std::size_t numa_node, std::size_t block_size, std::size_t block_count, std::size_t alignment,
             PagePolicy page_policy, std::size_t worker_hint_count, std::string_view label);
  ~BitsetSlab();

  BitsetSlab(const BitsetSlab&) = delete;
  BitsetSlab& operator=(const BitsetSlab&) = delete;
  BitsetSlab(BitsetSlab&&) = delete;
  BitsetSlab& operator=(BitsetSlab&&) = delete;

  [[nodiscard]] std::byte* try_acquire(std::size_t worker_index = 0) noexcept;
  [[nodiscard]] bool release(std::byte* ptr) noexcept;
  [[nodiscard]] bool contains(const void* ptr) const noexcept;
  [[nodiscard]] bool empty() const noexcept { return used_count_.load(std::memory_order_acquire) == 0; }
  [[nodiscard]] std::size_t active_blocks() const noexcept { return used_count_.load(std::memory_order_relaxed); }
  [[nodiscard]] std::size_t block_size() const noexcept { return block_size_; }
  [[nodiscard]] std::size_t block_count() const noexcept { return block_count_; }
  [[nodiscard]] std::size_t worker_hint_count() const noexcept { return worker_hint_count_; }
  [[nodiscard]] std::size_t capacity_bytes() const noexcept { return block_size_ * block_count_; }
  [[nodiscard]] std::size_t active_bytes() const noexcept { return active_blocks() * block_size_; }
  [[nodiscard]] std::size_t numa_node() const noexcept { return allocation_.numa_node; }

private:
  [[nodiscard]] std::uint64_t valid_mask(std::size_t word_index) const noexcept;
  [[nodiscard]] std::size_t block_index_from_ptr(const std::byte* ptr) const noexcept;
  [[nodiscard]] std::uint64_t next_scan_start(std::size_t worker_index) noexcept;

  RawAllocation allocation_{};
  std::size_t block_size_{0};
  std::size_t block_count_{0};
  std::size_t word_count_{0};
  std::unique_ptr<std::atomic<std::uint64_t>[]> words_{};
  alignas(64) std::atomic<std::size_t> used_count_{0};
  alignas(64) std::atomic<std::uint64_t> hint_{0};
  std::size_t worker_hint_count_{0};
  std::unique_ptr<ScanCursor[]> worker_hints_{};
};

} // namespace ksj::memory::detail
