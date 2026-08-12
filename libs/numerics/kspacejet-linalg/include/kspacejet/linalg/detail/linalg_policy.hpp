#pragma once

#include "kspacejet/base/types.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace ksj::linalg::detail {

struct LinalgDispatchPolicy {
  static constexpr std::size_t disabled_backend_min_size = std::numeric_limits<std::size_t>::max();
  // Tuned by docs/benchmark_reports/2026-07-24/kspacejet-numerics/xeon-silver-4410y-avx512-linux/benchmark_report.md.
  static constexpr std::size_t intel_matmul_min_flops = 0;
  static constexpr std::size_t intel_hermitian_gram_min_ops = 0;
  static constexpr std::size_t intel_gemv_min_ops = 0;
  static constexpr std::size_t intel_dot_float_min_elements = 32;
  static constexpr std::size_t intel_dot_double_min_elements = 32;
  static constexpr std::size_t intel_complex_dot_min_elements = 64;
  static constexpr std::size_t intel_blas1_float_min_elements = 256;
  static constexpr std::size_t intel_blas1_double_min_elements = 128;
  static constexpr std::size_t intel_complex_blas1_min_elements = 64;
  static constexpr std::size_t intel_whiten_samples_float_min_ops = 256;
  static constexpr std::size_t intel_whiten_samples_double_min_ops = 256;
  static constexpr std::size_t intel_complex_whiten_samples_float_min_ops = 2048;
  static constexpr std::size_t intel_complex_whiten_samples_double_min_ops = 2048;
  static constexpr std::size_t intel_solve_lu_float_min_size = 64;
  static constexpr std::size_t intel_solve_lu_double_min_size = 32;
  static constexpr std::size_t intel_complex_solve_lu_float_min_size = 128;
  static constexpr std::size_t intel_complex_solve_lu_double_min_size = 128;
  static constexpr std::size_t intel_complex_solve_lu_float_workspace_min_size = 4;
  static constexpr std::size_t intel_complex_solve_lu_double_workspace_min_size = 4;
  static constexpr std::size_t intel_solve_lu_matrix_float_min_size = 32;
  static constexpr std::size_t intel_solve_lu_matrix_double_min_size = 16;
  static constexpr std::size_t intel_complex_solve_lu_matrix_float_min_size = 256;
  static constexpr std::size_t intel_complex_solve_lu_matrix_double_min_size = 128;
  static constexpr std::size_t intel_complex_solve_lu_matrix_float_workspace_min_size = 1;
  static constexpr std::size_t intel_complex_solve_lu_matrix_double_workspace_min_size = 1;
  static constexpr std::size_t intel_inverse_float_min_size = disabled_backend_min_size;
  static constexpr std::size_t intel_inverse_double_min_size = disabled_backend_min_size;
  static constexpr std::size_t intel_complex_inverse_float_min_size = 32;
  static constexpr std::size_t intel_complex_inverse_double_min_size = 32;
  static constexpr std::size_t intel_complex_inverse_workspace_min_size = 1;
  static constexpr std::size_t intel_cholesky_lower_min_size = 1;
  static constexpr std::size_t intel_complex_cholesky_lower_min_size = 2;
  static constexpr std::size_t intel_solve_cholesky_min_size = 64;
  static constexpr std::size_t intel_complex_solve_cholesky_float_min_size = 8;
  static constexpr std::size_t intel_complex_solve_cholesky_double_min_size = 16;
  static constexpr std::size_t intel_solve_cholesky_matrix_min_size = 32;
  static constexpr std::size_t intel_complex_solve_cholesky_matrix_min_size = 8;
  static constexpr std::size_t intel_qr_float_min_cols = 16;
  static constexpr std::size_t intel_qr_double_min_cols = 16;
  static constexpr std::size_t intel_complex_qr_float_min_cols = 32;
  static constexpr std::size_t intel_complex_qr_double_min_cols = 32;
  static constexpr std::size_t intel_complex_qr_workspace_min_cols = 8;
  static constexpr std::size_t intel_qr_matrix_float_min_cols = 64;
  static constexpr std::size_t intel_qr_matrix_double_min_cols = 64;
  static constexpr std::size_t intel_complex_qr_matrix_float_min_cols = 64;
  static constexpr std::size_t intel_complex_qr_matrix_double_min_cols = 32;
  static constexpr std::size_t intel_complex_qr_matrix_float_workspace_min_cols = 32;
  static constexpr std::size_t intel_complex_qr_matrix_double_workspace_min_cols = 8;
  static constexpr std::size_t intel_whitening_float_min_size = 8;
  static constexpr std::size_t intel_whitening_double_min_size = 32;
  static constexpr std::size_t intel_complex_whitening_float_min_size = 32;
  static constexpr std::size_t intel_complex_whitening_double_min_size = 16;
  static constexpr std::size_t intel_singular_values_min_size = disabled_backend_min_size;
  static constexpr std::size_t intel_complex_singular_values_min_size = disabled_backend_min_size;
  static constexpr std::size_t intel_svd_min_size = 32;
  static constexpr std::size_t intel_complex_svd_float_min_size = disabled_backend_min_size;
  static constexpr std::size_t intel_complex_svd_double_min_size = 128;
  static constexpr std::size_t intel_full_svd_min_size = intel_svd_min_size;
  static constexpr std::size_t intel_complex_full_svd_float_min_size = intel_complex_svd_float_min_size;
  static constexpr std::size_t intel_complex_full_svd_double_min_size = intel_complex_svd_double_min_size;
  static constexpr std::size_t intel_svd_in_place_min_size = 1;
  static constexpr std::size_t intel_complex_svd_in_place_min_size = 1;
  static constexpr std::size_t intel_svd_workspace_min_size = 1;
  static constexpr std::size_t intel_complex_svd_workspace_min_size = 1;
  static constexpr std::size_t intel_left_singular_vectors_min_size = 1;
  static constexpr std::size_t intel_complex_left_singular_vectors_min_size = 1;
  static constexpr std::size_t intel_self_adjoint_eigen_float_min_size = 32;
  static constexpr std::size_t intel_self_adjoint_eigen_double_min_size = 1;
  static constexpr std::size_t intel_complex_self_adjoint_eigen_min_size = 1;
  static constexpr std::size_t intel_complex_general_eigen_float_min_size = disabled_backend_min_size;
  static constexpr std::size_t intel_complex_general_eigen_double_min_size = disabled_backend_min_size;
  static constexpr std::size_t intel_complex_general_eigen_float_workspace_min_size = disabled_backend_min_size;
  static constexpr std::size_t intel_complex_general_eigen_double_workspace_min_size = disabled_backend_min_size;
  static constexpr std::size_t intel_least_squares_svd_float_min_cols = 32;
  static constexpr std::size_t intel_least_squares_svd_double_min_cols = 32;
  static constexpr std::size_t intel_complex_least_squares_svd_float_min_cols = 16;
  static constexpr std::size_t intel_complex_least_squares_svd_double_min_cols = 16;
  static constexpr std::size_t intel_rank_revealing_least_squares_float_min_cols = disabled_backend_min_size;
  static constexpr std::size_t intel_rank_revealing_least_squares_double_min_cols = disabled_backend_min_size;
  static constexpr std::size_t intel_rank_revealing_least_squares_matrix_min_cols = 128;
  static constexpr std::size_t intel_complex_rank_revealing_least_squares_float_min_cols = disabled_backend_min_size;
  static constexpr std::size_t intel_complex_rank_revealing_least_squares_double_min_cols = disabled_backend_min_size;
  static constexpr std::size_t intel_complex_rank_revealing_least_squares_matrix_double_min_cols = 128;
};

[[nodiscard]] constexpr bool prefer_intel_matmul(const std::size_t rows, const std::size_t inner,
                                                 const std::size_t cols) noexcept {
  return rows * inner * cols * 2U >= LinalgDispatchPolicy::intel_matmul_min_flops;
}

[[nodiscard]] constexpr bool hermitian_gram_ops_at_least(const std::size_t rows, const std::size_t cols,
                                                         const std::size_t minimum_ops) noexcept {
  if (rows == 0U || cols == 0U) {
    return false;
  }
  if (minimum_ops == 0U) {
    return true;
  }

  // A^H*A performs approximately 2*rows*cols^2 scalar operations.  Saturate
  // on overflow: an unrepresentable operation count necessarily exceeds any
  // representable policy threshold.
  constexpr auto max_size = std::numeric_limits<std::size_t>::max();
  if (cols > max_size / cols) {
    return true;
  }
  const auto cols_squared = cols * cols;
  if (rows > max_size / cols_squared) {
    return true;
  }
  const auto row_col_product = rows * cols_squared;
  if (row_col_product > max_size / 2U) {
    return true;
  }
  return row_col_product * 2U >= minimum_ops;
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_hermitian_gram(const std::size_t rows, const std::size_t cols) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<T, ksj::base::cf32> ||
                std::is_same_v<T, ksj::base::cf64>) {
    return hermitian_gram_ops_at_least(rows, cols, LinalgDispatchPolicy::intel_hermitian_gram_min_ops);
  } else {
    return false;
  }
}

[[nodiscard]] constexpr bool prefer_intel_gemv(const std::size_t rows, const std::size_t cols) noexcept {
  return rows * cols * 2U >= LinalgDispatchPolicy::intel_gemv_min_ops;
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_dot(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= LinalgDispatchPolicy::intel_dot_float_min_elements;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= LinalgDispatchPolicy::intel_dot_double_min_elements;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_dot_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_blas1(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= LinalgDispatchPolicy::intel_blas1_float_min_elements;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= LinalgDispatchPolicy::intel_blas1_double_min_elements;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_blas1_min_elements;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_whiten_samples(const std::size_t rows, const std::size_t cols) noexcept {
  const auto ops = rows * cols * cols * 2U;
  if constexpr (std::is_same_v<T, float>) {
    return ops >= LinalgDispatchPolicy::intel_whiten_samples_float_min_ops;
  } else if constexpr (std::is_same_v<T, double>) {
    return ops >= LinalgDispatchPolicy::intel_whiten_samples_double_min_ops;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return ops >= LinalgDispatchPolicy::intel_complex_whiten_samples_float_min_ops;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return ops >= LinalgDispatchPolicy::intel_complex_whiten_samples_double_min_ops;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_solve_lu(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= LinalgDispatchPolicy::intel_solve_lu_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= LinalgDispatchPolicy::intel_solve_lu_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_lu_float_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_lu_double_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_solve_lu_workspace(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_lu_float_workspace_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_lu_double_workspace_min_size;
  } else {
    return prefer_intel_solve_lu<T>(size);
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_solve_lu_matrix(const std::size_t size, const std::size_t rhs_cols) noexcept {
  if (rhs_cols == 0U) {
    return false;
  }
  if constexpr (std::is_same_v<T, float>) {
    return size >= LinalgDispatchPolicy::intel_solve_lu_matrix_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= LinalgDispatchPolicy::intel_solve_lu_matrix_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_lu_matrix_float_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_lu_matrix_double_min_size;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_solve_lu_matrix_workspace(const std::size_t size,
                                                                    const std::size_t rhs_cols) noexcept {
  if (rhs_cols == 0U) {
    return false;
  }
  if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_lu_matrix_float_workspace_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_lu_matrix_double_workspace_min_size;
  } else {
    return prefer_intel_solve_lu_matrix<T>(size, rhs_cols);
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_inverse(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= LinalgDispatchPolicy::intel_inverse_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= LinalgDispatchPolicy::intel_inverse_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= LinalgDispatchPolicy::intel_complex_inverse_float_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_inverse_double_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_inverse_workspace(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_inverse_workspace_min_size;
  } else {
    return prefer_intel_inverse<T>(size);
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_cholesky_lower(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return size >= LinalgDispatchPolicy::intel_cholesky_lower_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_cholesky_lower_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_solve_cholesky(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return size >= LinalgDispatchPolicy::intel_solve_cholesky_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_cholesky_float_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_solve_cholesky_double_min_size;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_solve_cholesky_matrix(const std::size_t size,
                                                                const std::size_t rhs_cols) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return rhs_cols > 0U && size >= LinalgDispatchPolicy::intel_solve_cholesky_matrix_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return rhs_cols > 0U && size >= LinalgDispatchPolicy::intel_complex_solve_cholesky_matrix_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_solve_qr(const std::size_t cols) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return cols >= LinalgDispatchPolicy::intel_qr_float_min_cols;
  } else if constexpr (std::is_same_v<T, double>) {
    return cols >= LinalgDispatchPolicy::intel_qr_double_min_cols;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return cols >= LinalgDispatchPolicy::intel_complex_qr_float_min_cols;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return cols >= LinalgDispatchPolicy::intel_complex_qr_double_min_cols;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_solve_qr_workspace(const std::size_t cols) noexcept {
  if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return cols >= LinalgDispatchPolicy::intel_complex_qr_workspace_min_cols;
  } else {
    return prefer_intel_solve_qr<T>(cols);
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_solve_qr_matrix(const std::size_t cols, const std::size_t rhs_cols) noexcept {
  if (rhs_cols == 0U) {
    return false;
  }
  if constexpr (std::is_same_v<T, float>) {
    return cols >= LinalgDispatchPolicy::intel_qr_matrix_float_min_cols;
  } else if constexpr (std::is_same_v<T, double>) {
    return cols >= LinalgDispatchPolicy::intel_qr_matrix_double_min_cols;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return cols >= LinalgDispatchPolicy::intel_complex_qr_matrix_float_min_cols;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return cols >= LinalgDispatchPolicy::intel_complex_qr_matrix_double_min_cols;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_solve_qr_matrix_workspace(const std::size_t cols,
                                                                    const std::size_t rhs_cols) noexcept {
  if (rhs_cols == 0U) {
    return false;
  }
  if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return cols >= LinalgDispatchPolicy::intel_complex_qr_matrix_float_workspace_min_cols;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return cols >= LinalgDispatchPolicy::intel_complex_qr_matrix_double_workspace_min_cols;
  } else {
    return prefer_intel_solve_qr_matrix<T>(cols, rhs_cols);
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_singular_values(const std::size_t rows, const std::size_t cols) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return rows > 0U && cols > 0U && std::min(rows, cols) >= LinalgDispatchPolicy::intel_singular_values_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return rows > 0U && cols > 0U &&
           std::min(rows, cols) >= LinalgDispatchPolicy::intel_complex_singular_values_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_whitening_matrix(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= LinalgDispatchPolicy::intel_whitening_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= LinalgDispatchPolicy::intel_whitening_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= LinalgDispatchPolicy::intel_complex_whitening_float_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_whitening_double_min_size;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_svd(const std::size_t rows, const std::size_t cols) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return rows > 0U && cols > 0U && std::min(rows, cols) >= LinalgDispatchPolicy::intel_svd_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return rows > 0U && cols > 0U && std::min(rows, cols) >= LinalgDispatchPolicy::intel_complex_svd_float_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return rows > 0U && cols > 0U && std::min(rows, cols) >= LinalgDispatchPolicy::intel_complex_svd_double_min_size;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_svd_in_place(const std::size_t rows, const std::size_t cols) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return rows > 0U && cols > 0U && std::min(rows, cols) >= LinalgDispatchPolicy::intel_svd_in_place_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return rows > 0U && cols > 0U && std::min(rows, cols) >= LinalgDispatchPolicy::intel_complex_svd_in_place_min_size;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_svd_workspace(const std::size_t rows, const std::size_t cols) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return rows > 0U && cols > 0U && std::min(rows, cols) >= LinalgDispatchPolicy::intel_svd_workspace_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return rows > 0U && cols > 0U && std::min(rows, cols) >= LinalgDispatchPolicy::intel_complex_svd_workspace_min_size;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_full_svd(const std::size_t rows, const std::size_t cols) noexcept {
  return prefer_intel_svd<T>(rows, cols);
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_left_singular_vectors(const std::size_t rows,
                                                                const std::size_t cols) noexcept {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return rows > 0U && cols > 0U && std::min(rows, cols) >= LinalgDispatchPolicy::intel_left_singular_vectors_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return rows > 0U && cols > 0U &&
           std::min(rows, cols) >= LinalgDispatchPolicy::intel_complex_left_singular_vectors_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_self_adjoint_eigen(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= LinalgDispatchPolicy::intel_self_adjoint_eigen_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= LinalgDispatchPolicy::intel_self_adjoint_eigen_double_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_self_adjoint_eigen_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_general_eigen(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= LinalgDispatchPolicy::intel_complex_general_eigen_float_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_general_eigen_double_min_size;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_general_eigen_workspace(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return size >= LinalgDispatchPolicy::intel_complex_general_eigen_float_workspace_min_size;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return size >= LinalgDispatchPolicy::intel_complex_general_eigen_double_workspace_min_size;
  } else {
    return prefer_intel_general_eigen<T>(size);
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_least_squares_svd(const std::size_t cols,
                                                            const std::size_t rhs_cols) noexcept {
  if (rhs_cols == 0U) {
    return false;
  }
  if constexpr (std::is_same_v<T, float>) {
    return cols >= LinalgDispatchPolicy::intel_least_squares_svd_float_min_cols;
  } else if constexpr (std::is_same_v<T, double>) {
    return cols >= LinalgDispatchPolicy::intel_least_squares_svd_double_min_cols;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return cols >= LinalgDispatchPolicy::intel_complex_least_squares_svd_float_min_cols;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return cols >= LinalgDispatchPolicy::intel_complex_least_squares_svd_double_min_cols;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_least_squares_rank_revealing_qr(const std::size_t cols,
                                                                          const std::size_t rhs_cols) noexcept {
  if (rhs_cols == 0U) {
    return false;
  }
  if (rhs_cols > 1U) {
    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
      return cols >= LinalgDispatchPolicy::intel_rank_revealing_least_squares_matrix_min_cols;
    } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
      return cols >= LinalgDispatchPolicy::intel_complex_rank_revealing_least_squares_matrix_double_min_cols;
    }
  }
  if constexpr (std::is_same_v<T, float>) {
    return cols >= LinalgDispatchPolicy::intel_rank_revealing_least_squares_float_min_cols;
  } else if constexpr (std::is_same_v<T, double>) {
    return cols >= LinalgDispatchPolicy::intel_rank_revealing_least_squares_double_min_cols;
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return cols >= LinalgDispatchPolicy::intel_complex_rank_revealing_least_squares_float_min_cols;
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return cols >= LinalgDispatchPolicy::intel_complex_rank_revealing_least_squares_double_min_cols;
  } else {
    return false;
  }
}

} // namespace ksj::linalg::detail
