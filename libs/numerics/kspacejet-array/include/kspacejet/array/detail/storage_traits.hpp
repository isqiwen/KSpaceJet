#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace ksj::array::detail {

template <typename T>
inline constexpr bool pooled_storage_scalar_v =
  std::is_object_v<T> && !std::is_const_v<T> && std::is_trivially_destructible_v<T>;

[[nodiscard]] inline std::size_t checked_count(const std::size_t lhs, const std::size_t rhs) {
  if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    throw std::length_error("pooled array element count overflows size_t");
  }
  return lhs * rhs;
}

inline void validate_reshape_count(const std::size_t current_count, const std::size_t requested_count,
                                   const char* message) {
  if (current_count != requested_count) {
    throw std::invalid_argument(message);
  }
}

} // namespace ksj::array::detail
