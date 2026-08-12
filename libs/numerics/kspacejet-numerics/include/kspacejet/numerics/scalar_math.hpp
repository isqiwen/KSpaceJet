#pragma once

/// Backend-independent scalar mathematical helpers and stable scalar predicates.

#include <cmath>
#include <limits>
#include <type_traits>

namespace ksj::numerics {

template <typename T> [[nodiscard]] inline auto abs_value(const T& value) {
  if constexpr (std::is_arithmetic_v<T>) {
    return value < T{} ? -value : value;
  } else {
    return std::abs(value);
  }
}

template <typename T> [[nodiscard]] int order(const T data) {
  return static_cast<int>(std::ceil(std::log10(std::abs(data))));
}

template <typename T1, typename T2>
[[nodiscard]] inline auto floor_mod(T1 value, T2 modulus) -> std::common_type_t<T1, T2> {
  using result_type = std::common_type_t<T1, T2>;
  if (modulus == 0) {
    return static_cast<result_type>(value);
  }
  return static_cast<result_type>(value - std::floor(value / static_cast<double>(modulus)) * modulus);
}

template <typename T1, typename T2>
[[nodiscard]] inline auto floor_mod_fix(T1 value, T2 modulus) -> std::common_type_t<T1, T2> {
  using result_type = std::common_type_t<T1, T2>;
  if (modulus == 0) {
    return static_cast<result_type>(value);
  }

  result_type result = static_cast<result_type>(value - std::floor(value / static_cast<double>(modulus)) * modulus);
  result_type magnitude = static_cast<result_type>(modulus);
  if (magnitude < 0) {
    magnitude = -magnitude;
  }

  const result_type cutoff = magnitude - 10 * std::numeric_limits<result_type>::epsilon();
  if (result > cutoff) {
    result -= magnitude;
  } else if (result < -cutoff) {
    result += magnitude;
  }
  return result;
}

template <typename T> [[nodiscard]] inline std::enable_if_t<std::is_floating_point_v<T>, T> round_value(T value) {
  return std::round(value);
}

} // namespace ksj::numerics
