#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>

namespace ksj::sparse::detail {

// Tuned by docs/benchmark_reports/2026-07-23/kspacejet-sparse/xeon-silver-4410y/benchmark_report.md.
template <typename T> struct SparseDispatchPolicy {
  static constexpr std::size_t disabled = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t spmv_min_rows = disabled;
  static constexpr std::size_t planned_spmv_diagonal_min_rows = disabled;
  static constexpr std::size_t planned_spmv_general_min_rows = disabled;
  static constexpr std::size_t spmm_single_column_min_rows = disabled;
  static constexpr std::size_t spmm_medium_min_rows = disabled;
  static constexpr std::size_t spmm_wide_min_rows = disabled;
  static constexpr std::size_t planned_spmm_single_column_min_rows = disabled;
  static constexpr std::size_t planned_spmm_multi_column_min_rows = disabled;
  static constexpr std::size_t spsm_min_rows = disabled;
  static constexpr std::size_t planned_spsm_min_rows = disabled;
};

template <> struct SparseDispatchPolicy<float> {
  static constexpr std::size_t spmv_min_rows = 2048U;
  static constexpr std::size_t planned_spmv_diagonal_min_rows = 512U;
  static constexpr std::size_t planned_spmv_general_min_rows = 512U;
  static constexpr std::size_t spmm_single_column_min_rows = 1024U;
  static constexpr std::size_t spmm_medium_min_rows = 64U;
  static constexpr std::size_t spmm_wide_min_rows = 16U;
  static constexpr std::size_t planned_spmm_single_column_min_rows = 128U;
  static constexpr std::size_t planned_spmm_multi_column_min_rows = 16U;
  static constexpr std::size_t spsm_min_rows = 64U;
  static constexpr std::size_t planned_spsm_min_rows = 32U;
};

template <> struct SparseDispatchPolicy<double> {
  static constexpr std::size_t spmv_min_rows = 2048U;
  static constexpr std::size_t planned_spmv_diagonal_min_rows = 256U;
  static constexpr std::size_t planned_spmv_general_min_rows = 512U;
  static constexpr std::size_t spmm_single_column_min_rows = 512U;
  static constexpr std::size_t spmm_medium_min_rows = 64U;
  static constexpr std::size_t spmm_wide_min_rows = 16U;
  static constexpr std::size_t planned_spmm_single_column_min_rows = 128U;
  static constexpr std::size_t planned_spmm_multi_column_min_rows = 16U;
  static constexpr std::size_t spsm_min_rows = 64U;
  static constexpr std::size_t planned_spsm_min_rows = 32U;
};

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_spmv(const std::size_t rows, const std::size_t) noexcept {
  return rows >= SparseDispatchPolicy<T>::spmv_min_rows;
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_planned_spmv(const std::size_t rows, const std::size_t nonzeros) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    if constexpr (SparseDispatchPolicy<T>::planned_spmv_diagonal_min_rows ==
                  SparseDispatchPolicy<T>::planned_spmv_general_min_rows) {
      return rows >= SparseDispatchPolicy<T>::planned_spmv_general_min_rows;
    } else {
      const auto threshold = nonzeros == rows ? SparseDispatchPolicy<T>::planned_spmv_diagonal_min_rows
                                              : SparseDispatchPolicy<T>::planned_spmv_general_min_rows;
      return rows >= threshold;
    }
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_spmm(const std::size_t rows, const std::size_t,
                                               const std::size_t dense_columns) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    const auto threshold = dense_columns >= 16U
                             ? SparseDispatchPolicy<T>::spmm_wide_min_rows
                             : (dense_columns >= 4U ? SparseDispatchPolicy<T>::spmm_medium_min_rows
                                                    : SparseDispatchPolicy<T>::spmm_single_column_min_rows);
    return rows >= threshold;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_planned_spmm(const std::size_t rows, const std::size_t,
                                                       const std::size_t dense_columns) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    const auto threshold = dense_columns > 1U ? SparseDispatchPolicy<T>::planned_spmm_multi_column_min_rows
                                              : SparseDispatchPolicy<T>::planned_spmm_single_column_min_rows;
    return rows >= threshold;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_sparse_transform(const std::size_t) noexcept {
  return false;
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_sparse_triangular_vector_solve(const std::size_t,
                                                                         const std::size_t) noexcept {
  return false;
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_sparse_triangular_matrix_solve(const std::size_t rows, const std::size_t,
                                                                         const std::size_t rhs_columns) noexcept {
  return rhs_columns >= 8U && rows >= SparseDispatchPolicy<T>::spsm_min_rows;
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_planned_triangular_vector_solve(const std::size_t,
                                                                          const std::size_t) noexcept {
  return false;
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_planned_triangular_matrix_solve(const std::size_t rows, const std::size_t,
                                                                          const std::size_t rhs_columns) noexcept {
  return rhs_columns >= 8U && rows >= SparseDispatchPolicy<T>::planned_spsm_min_rows;
}

} // namespace ksj::sparse::detail
