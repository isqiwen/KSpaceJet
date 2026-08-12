#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kspacejet/memory/allocation_properties.hpp"
#include "kspacejet/memory/memory_broker.hpp"
#include "kspacejet/memory/memory_lease.hpp"
#include "kspacejet/memory/memory_space.hpp"

namespace ksj::memory {

namespace detail {

template <typename T>
inline constexpr bool pool_buffer_element_v =
  std::is_object_v<T> && !std::is_const_v<T> && std::is_trivially_destructible_v<T>;

template <typename T> [[nodiscard]] inline std::size_t checked_array_bytes(const std::size_t count) {
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
    throw std::length_error("typed allocation byte size overflows size_t");
  }
  return count * sizeof(T);
}

template <typename T>
[[nodiscard]] inline std::size_t typed_alignment(const AllocationProperties& properties) noexcept {
  return std::max(properties.alignment, std::max<std::size_t>(alignof(T), NumaHostSpace::cache_line_size()));
}

} // namespace detail

[[nodiscard]] inline MemoryLease allocate_bytes(MemoryBroker& broker, const std::size_t bytes,
                                                AllocationProperties properties = {},
                                                const std::size_t worker_index = 0, std::string_view shard_key = {}) {
  properties.alignment = std::max(properties.alignment, NumaHostSpace::cache_line_size());

  AllocationRequest request;
  request.properties = std::move(properties);
  request.bytes = bytes;
  request.worker_index = worker_index;
  request.shard_key = shard_key;
  return broker.acquire(request);
}

[[nodiscard]] inline MemoryLease allocate_bytes(const std::size_t bytes, AllocationProperties properties = {},
                                                const std::size_t worker_index = 0, std::string_view shard_key = {}) {
  return allocate_bytes(MemoryBroker::instance(), bytes, std::move(properties), worker_index, shard_key);
}

template <typename T> class PooledBuffer {
public:
  static_assert(detail::pool_buffer_element_v<T>,
                "PooledBuffer<T> stores raw typed memory and requires a non-const trivially destructible object type");

  using element_type = T;
  using value_type = T;

  PooledBuffer() = default;
  PooledBuffer(MemoryLease lease, std::size_t count) noexcept : lease_(std::move(lease)), count_(count) {}

  PooledBuffer(const PooledBuffer&) = delete;
  PooledBuffer& operator=(const PooledBuffer&) = delete;

  PooledBuffer(PooledBuffer&& other) noexcept : lease_(std::move(other.lease_)), count_(other.count_) {
    other.count_ = 0;
  }

  PooledBuffer& operator=(PooledBuffer&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    lease_ = std::move(other.lease_);
    count_ = other.count_;
    other.count_ = 0;
    return *this;
  }

  [[nodiscard]] T* data() noexcept { return reinterpret_cast<T*>(lease_.data()); }
  [[nodiscard]] const T* data() const noexcept { return reinterpret_cast<const T*>(lease_.data()); }
  [[nodiscard]] std::size_t size() const noexcept { return count_; }
  [[nodiscard]] std::size_t size_bytes() const noexcept { return count_ * sizeof(T); }
  [[nodiscard]] std::size_t capacity() const noexcept { return lease_.capacity() / sizeof(T); }
  [[nodiscard]] std::size_t capacity_bytes() const noexcept { return lease_.capacity(); }
  [[nodiscard]] std::size_t numa_node() const noexcept { return lease_.numa_node(); }
  [[nodiscard]] bool direct() const noexcept { return lease_.direct(); }
  [[nodiscard]] bool reused() const noexcept { return lease_.reused(); }
  [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(lease_); }

  [[nodiscard]] std::span<T> span() noexcept { return {data(), count_}; }
  [[nodiscard]] std::span<const T> span() const noexcept { return {data(), count_}; }

  void resize_count(const std::size_t count) {
    if (count > capacity()) {
      throw std::length_error("pooled buffer logical size exceeds capacity");
    }
    count_ = count;
  }

  void clear() noexcept { count_ = 0; }

  void release() noexcept {
    lease_.release();
    count_ = 0;
  }

  [[nodiscard]] MemoryLease& lease() noexcept { return lease_; }
  [[nodiscard]] const MemoryLease& lease() const noexcept { return lease_; }

private:
  MemoryLease lease_{};
  std::size_t count_{0};
};

template <typename T>
[[nodiscard]] PooledBuffer<T> allocate_array(MemoryBroker& broker, const std::size_t count,
                                             AllocationProperties properties = {}, const std::size_t worker_index = 0,
                                             std::string_view shard_key = {}) {
  static_assert(detail::pool_buffer_element_v<T>,
                "allocate_array<T> returns unconstructed raw typed memory; use a container for non-trivial objects");

  properties.alignment = detail::typed_alignment<T>(properties);
  auto lease =
    allocate_bytes(broker, detail::checked_array_bytes<T>(count), std::move(properties), worker_index, shard_key);
  return PooledBuffer<T>(std::move(lease), count);
}

template <typename T>
[[nodiscard]] PooledBuffer<T> allocate_array(const std::size_t count, AllocationProperties properties = {},
                                             const std::size_t worker_index = 0, std::string_view shard_key = {}) {
  return allocate_array<T>(MemoryBroker::instance(), count, std::move(properties), worker_index, shard_key);
}

} // namespace ksj::memory
