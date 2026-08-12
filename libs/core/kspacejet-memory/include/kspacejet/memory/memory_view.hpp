#pragma once

#include <cstddef>
#include <span>

namespace ksj::memory {

class MemoryView {
public:
  MemoryView() = default;
  MemoryView(std::byte* data, std::size_t bytes, std::size_t numa_node) noexcept
      : data_(data), bytes_(bytes), numa_node_(numa_node) {}

  [[nodiscard]] std::byte* data() const noexcept { return data_; }
  [[nodiscard]] std::size_t size() const noexcept { return bytes_; }
  [[nodiscard]] std::size_t numa_node() const noexcept { return numa_node_; }
  [[nodiscard]] bool empty() const noexcept { return bytes_ == 0; }
  [[nodiscard]] std::span<std::byte> span() const noexcept { return {data_, bytes_}; }

private:
  std::byte* data_{nullptr};
  std::size_t bytes_{0};
  std::size_t numa_node_{0};
};

} // namespace ksj::memory
