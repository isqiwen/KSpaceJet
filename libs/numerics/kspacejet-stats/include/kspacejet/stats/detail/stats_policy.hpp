#pragma once

#include "kspacejet/base/types.hpp"

#include <cstddef>
#include <type_traits>

namespace ksj::stats::detail {

struct StatsDispatchPolicy {
  // Tuned by docs/benchmark_reports/2026-07-23/kspacejet-numerics/xeon-silver-4410y-avx512-linux/benchmark_report.md.
  static constexpr std::size_t intel_sum_float_min_size = 16;
  static constexpr std::size_t intel_sum_float_max_size = 65536;
  static constexpr std::size_t intel_sum_double_min_size = 16;
  static constexpr std::size_t intel_sum_double_max_size = 4096;
  static constexpr std::size_t intel_mean_float_min_size = 128;
  static constexpr std::size_t intel_mean_double_min_size = 128;
  static constexpr std::size_t intel_sum_of_squares_float_min_size = 128;
  static constexpr std::size_t intel_sum_of_squares_double_min_size = 128;
  static constexpr std::size_t intel_complex_float_sum_of_squares_min_size = 256;
  static constexpr std::size_t intel_complex_double_sum_of_squares_min_size = 128;
  static constexpr std::size_t intel_root_sum_of_squares_float_min_size = 128;
  static constexpr std::size_t intel_root_sum_of_squares_double_min_size = 128;
  static constexpr std::size_t intel_complex_float_root_sum_of_squares_min_size = 256;
  static constexpr std::size_t intel_complex_double_root_sum_of_squares_min_size = 128;
  static constexpr std::size_t intel_max_abs_float_min_size = 32;
  static constexpr std::size_t intel_max_abs_double_min_size = 32;
  static constexpr std::size_t intel_complex_float_max_abs_min_size = 8;
  static constexpr std::size_t intel_complex_double_max_abs_min_size = 8;
  static constexpr std::size_t intel_l1_distance_float_min_size = 32;
  static constexpr std::size_t intel_l1_distance_double_min_size = 32;
  static constexpr std::size_t intel_complex_float_l1_distance_min_size = 8;
  static constexpr std::size_t intel_complex_double_l1_distance_min_size = 8;
  static constexpr std::size_t intel_l2_distance_float_min_size = 64;
  static constexpr std::size_t intel_l2_distance_double_min_size = 64;
  static constexpr std::size_t intel_complex_float_l2_distance_min_size = 16;
  static constexpr std::size_t intel_complex_double_l2_distance_min_size = 16;
  static constexpr std::size_t intel_linf_distance_float_min_size = 16;
  static constexpr std::size_t intel_linf_distance_double_min_size = 32;
  static constexpr std::size_t intel_complex_float_linf_distance_min_size = 8;
  static constexpr std::size_t intel_complex_double_linf_distance_min_size = 8;
  static constexpr std::size_t intel_sum_abs_float_min_size = 128;
  static constexpr std::size_t intel_sum_abs_double_min_size = 128;
  static constexpr std::size_t intel_complex_sum_abs_min_size = 16;
  static constexpr std::size_t intel_max_index_float_min_size = 64;
  static constexpr std::size_t intel_max_index_double_min_size = 64;
  static constexpr std::size_t intel_min_index_float_min_size = 64;
  static constexpr std::size_t intel_min_index_double_min_size = 64;
  static constexpr std::size_t intel_rmse_diff_float_min_size = 128;
  static constexpr std::size_t intel_rmse_diff_double_min_size = 128;
  static constexpr std::size_t intel_complex_float_rmse_diff_min_size = 128;
  static constexpr std::size_t intel_complex_double_rmse_diff_min_size = 128;
  static constexpr std::size_t intel_rmse_pair_float_min_size = 128;
  static constexpr std::size_t intel_rmse_pair_double_min_size = 128;
  static constexpr std::size_t intel_complex_float_rmse_pair_min_size = 128;
  static constexpr std::size_t intel_complex_double_rmse_pair_min_size = 256;
  static constexpr std::size_t intel_equal_float_min_size = 64;
  static constexpr std::size_t intel_equal_double_min_size = 128;
  static constexpr std::size_t intel_complex_equal_min_size = 16;
  static constexpr std::size_t intel_squared_l2_distance_float_min_size = 128;
  static constexpr std::size_t intel_squared_l2_distance_double_min_size = 128;
  static constexpr std::size_t intel_complex_float_squared_l2_distance_min_size = 64;
  static constexpr std::size_t intel_complex_double_squared_l2_distance_min_size = 32;
};

template <typename T> [[nodiscard]] constexpr bool prefer_intel_sum(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_sum_float_min_size &&
           size <= StatsDispatchPolicy::intel_sum_float_max_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_sum_double_min_size &&
           size <= StatsDispatchPolicy::intel_sum_double_max_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_mean(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_mean_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_mean_double_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_sum_of_squares(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_sum_of_squares_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_sum_of_squares_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= StatsDispatchPolicy::intel_complex_float_sum_of_squares_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_double_sum_of_squares_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_root_sum_of_squares(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_root_sum_of_squares_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_root_sum_of_squares_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= StatsDispatchPolicy::intel_complex_float_root_sum_of_squares_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_double_root_sum_of_squares_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_max_abs(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_max_abs_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_max_abs_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= StatsDispatchPolicy::intel_complex_float_max_abs_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_double_max_abs_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_l1_distance(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_l1_distance_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_l1_distance_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= StatsDispatchPolicy::intel_complex_float_l1_distance_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_double_l1_distance_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_l2_distance(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_l2_distance_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_l2_distance_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= StatsDispatchPolicy::intel_complex_float_l2_distance_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_double_l2_distance_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_linf_distance(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_linf_distance_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_linf_distance_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= StatsDispatchPolicy::intel_complex_float_linf_distance_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_double_linf_distance_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_sum_abs(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_sum_abs_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_sum_abs_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_sum_abs_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_max_index(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_max_index_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_max_index_double_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_min_index(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_min_index_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_min_index_double_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_rmse_diff(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_rmse_diff_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_rmse_diff_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= StatsDispatchPolicy::intel_complex_float_rmse_diff_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_double_rmse_diff_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_rmse_pair(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_rmse_pair_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_rmse_pair_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= StatsDispatchPolicy::intel_complex_float_rmse_pair_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_double_rmse_pair_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_equal(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_equal_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_equal_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_equal_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_squared_l2_distance(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= StatsDispatchPolicy::intel_squared_l2_distance_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= StatsDispatchPolicy::intel_squared_l2_distance_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= StatsDispatchPolicy::intel_complex_float_squared_l2_distance_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= StatsDispatchPolicy::intel_complex_double_squared_l2_distance_min_size;
  } else {
    return false;
  }
}

} // namespace ksj::stats::detail
