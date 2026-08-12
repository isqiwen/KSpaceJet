#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace ksj::memory::detail {

[[nodiscard]] inline std::optional<std::size_t> size_class_index_for(std::span<const std::size_t> size_classes,
                                                                     const std::size_t bytes) noexcept {
  for (std::size_t i = 0; i < size_classes.size(); ++i) {
    if (bytes <= size_classes[i]) {
      return i;
    }
  }
  return std::nullopt;
}

} // namespace ksj::memory::detail
