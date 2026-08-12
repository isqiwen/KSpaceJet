#pragma once

#include "benchmark_common.hpp"
#include "kspacejet/linalg/detail/eigen/eigen_linalg_solvers.hpp"
#include "kspacejet/linalg/detail/intel/intel_linalg_solvers.hpp"
#include "kspacejet/linalg/linalg.hpp"
#include "kspacejet/stats/stats.hpp"

#include <complex>
#include <cstddef>
#include <string>
#include <string_view>

namespace ksj::benchmarks::linalg_benchmarks {

inline constexpr std::size_t kMaxStatisticsSize = 256;
inline constexpr std::size_t kMaxDecompositionSize = 256;
inline constexpr std::size_t kMaxFullSvdSize = 128;
inline constexpr std::size_t kMaxGeneralEigenSize = 128;
inline constexpr std::size_t kMaxLeastSquaresVariantSize = 128;
inline constexpr std::size_t kMatrixRhsCols = 4;

[[nodiscard]] inline ksj::benchmarks::RowMetadata linalg_benchmark_row_metadata(const std::string_view case_name,
                                                                                const std::string_view backend) {
  const auto scalar_result = case_name == "dot" || case_name == "complex_dot" || case_name == "squared_norm";
  const auto output_reuse = backend.starts_with("intel_") || backend.find("output") != std::string_view::npos ||
                            backend.find("workspace") != std::string_view::npos || backend == "manual_output" ||
                            backend == "array_linear_combination";
  const auto timing_scope = scalar_result  ? std::string_view{"scalar_result"}
                            : output_reuse ? std::string_view{"output_reuse"}
                                           : std::string_view{"allocating"};
  if (backend.starts_with("eigen") || backend.starts_with("manual")) {
    return ksj::benchmarks::reference_row(case_name, timing_scope);
  }
  return ksj::benchmarks::candidate_row(case_name, timing_scope);
}

inline void print_linalg_benchmark_row(const std::string_view case_name, const std::string_view backend,
                                       const std::string_view type_name, const std::size_t size,
                                       const ksj::benchmarks::Config& config,
                                       const ksj::benchmarks::Measurement& measurement, const double checksum) {
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum,
                             linalg_benchmark_row_metadata(case_name, backend));
}

inline void print_linalg_benchmark_row(const std::string_view case_name, const std::string_view backend,
                                       const std::string_view type_name, const std::size_t size,
                                       const ksj::benchmarks::Config& config,
                                       const ksj::benchmarks::Measurement& measurement, const double checksum,
                                       const ksj::benchmarks::RowMetadata& metadata) {
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum, metadata);
}

template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledMatrix<T>& matrix);
template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledVector<T>& vector);
template <typename Real> [[nodiscard]] double checksum(const ksj::array::PooledMatrix<std::complex<Real>>& matrix);
template <typename Real> [[nodiscard]] double checksum(const ksj::array::PooledVector<std::complex<Real>>& vector);

template <typename T>
[[nodiscard]] ksj::array::MatrixView<const T> const_matrix_view(const ksj::array::PooledMatrix<T>& matrix) {
  return ksj::array::as_const_view(matrix.view());
}

template <typename T>
[[nodiscard]] ksj::array::VectorView<const T> const_vector_view(const ksj::array::PooledVector<T>& vector) {
  return ksj::array::as_const_view(vector.view());
}

template <typename T>
void run_small_solve_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  auto matrix = ksj::array::make_pooled_matrix<T>(2, 2);
  auto rhs = ksj::array::make_pooled_vector<T>(2);
  ksj::benchmarks::require_pooled_storage("small_matrix", matrix);
  ksj::benchmarks::require_pooled_storage("small_rhs", rhs);
  matrix(0, 0) = static_cast<T>(4);
  matrix(1, 0) = static_cast<T>(2);
  matrix(0, 1) = static_cast<T>(2);
  matrix(1, 1) = static_cast<T>(3);
  rhs(0) = static_cast<T>(6);
  rhs(1) = static_cast<T>(5);

  auto small_solution = ksj::linalg::solve_small(matrix, rhs);
  const auto small_ns = ksj::benchmarks::measure(config, [&] {
    small_solution = ksj::linalg::solve_small(matrix, rhs);
    ksj::benchmarks::do_not_optimize(small_solution.data()[0]);
  });
  print_linalg_benchmark_row("solve_small_2x2", "manual_2x2", type_name, 2U, config, small_ns,
                             checksum(small_solution));

  auto lu_solution = ksj::linalg::solve(matrix, rhs);
  const auto lu_ns = ksj::benchmarks::measure(config, [&] {
    lu_solution = ksj::linalg::solve(matrix, rhs);
    ksj::benchmarks::do_not_optimize(lu_solution.data()[0]);
  });
  print_linalg_benchmark_row("solve_small_2x2", "eigen_lu", type_name, 2U, config, lu_ns, checksum(lu_solution));
}

template <typename T> void fill_matrix(ksj::array::PooledMatrix<T>& matrix) {
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      const auto row_value = static_cast<double>(row + 1U);
      const auto col_value = static_cast<double>(col + 1U);
      matrix(row, col) = static_cast<T>(row_value * 0.25 + col_value * 0.125);
    }
  }
}

template <typename Real> void fill_complex_matrix(ksj::array::PooledMatrix<std::complex<Real>>& matrix) {
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      const auto row_value = static_cast<Real>(row + 1U);
      const auto col_value = static_cast<Real>(col + 1U);
      const auto real = (row == col ? static_cast<Real>(matrix.rows() + 1U) : Real{}) +
                        static_cast<Real>(0.001) * static_cast<Real>((row + col) % 7U + 1U);
      const auto imag = static_cast<Real>(0.0007) * static_cast<Real>((row * 3U + col * 5U) % 11U);
      matrix(row, col) =
        std::complex<Real>{real + row_value * static_cast<Real>(0.0001), imag - col_value * static_cast<Real>(0.0002)};
    }
  }
}

template <typename Real> void fill_complex_vector(ksj::array::PooledVector<std::complex<Real>>& vector) {
  for (std::size_t index = 0; index < vector.size(); ++index) {
    const auto value = static_cast<Real>(index + 1U);
    vector(index) = std::complex<Real>{value * static_cast<Real>(0.5), value * static_cast<Real>(-0.25)};
  }
}

template <typename Real> void fill_hermitian_matrix(ksj::array::PooledMatrix<std::complex<Real>>& matrix) {
  as_eigen(matrix).setZero();
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    matrix(row, row) = std::complex<Real>{static_cast<Real>(matrix.rows() + row + 1U), Real{}};
  }
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = col + 1U; row < matrix.rows(); ++row) {
      const auto real = static_cast<Real>(0.001) * static_cast<Real>((row + col) % 5U + 1U);
      const auto imag = static_cast<Real>(0.0005) * static_cast<Real>((row * 7U + col * 3U) % 9U + 1U);
      matrix(row, col) = std::complex<Real>{real, imag};
      matrix(col, row) = std::conj(matrix(row, col));
    }
  }
}

template <typename T> void fill_well_conditioned_matrix(ksj::array::PooledMatrix<T>& matrix) {
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      matrix(row, col) = row == col ? static_cast<T>(matrix.rows() + 1U)
                                    : static_cast<T>(0.001 * static_cast<double>((row + col) % 7U + 1U));
    }
  }
}

template <typename T> void fill_samples(ksj::array::PooledMatrix<T>& samples) {
  for (std::size_t col = 0; col < samples.cols(); ++col) {
    for (std::size_t row = 0; row < samples.rows(); ++row) {
      const auto folded = static_cast<double>((row * 17U + col * 31U) % 257U);
      const auto trend = static_cast<double>((row + 1U) * (col + 3U)) * 0.0005;
      samples(row, col) = static_cast<T>((folded * 0.01) + trend);
    }
  }
}

template <typename T> void fill_least_squares_matrix(ksj::array::PooledMatrix<T>& matrix) {
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      const auto jitter = static_cast<double>((row * 11U + col * 23U) % 29U) * 0.001;
      const auto diagonal = (row % matrix.cols()) == col ? 1.0 : 0.0;
      matrix(row, col) = static_cast<T>(diagonal + jitter);
    }
  }
}

template <typename T> void fill_rank_deficient_least_squares_matrix(ksj::array::PooledMatrix<T>& matrix) {
  fill_least_squares_matrix(matrix);
  if (matrix.cols() < 2U) {
    return;
  }
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    matrix(row, matrix.cols() - 1U) = static_cast<T>(2) * matrix(row, 0);
  }
}

template <typename Real>
void fill_rank_deficient_least_squares_matrix(ksj::array::PooledMatrix<std::complex<Real>>& matrix) {
  fill_complex_matrix(matrix);
  if (matrix.cols() < 2U) {
    return;
  }
  const auto factor = std::complex<Real>{static_cast<Real>(2), static_cast<Real>(-0.5)};
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    matrix(row, matrix.cols() - 1U) = factor * matrix(row, 0);
  }
}

template <typename T> void fill_vector(ksj::array::PooledVector<T>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    vector(i) = static_cast<T>(static_cast<double>(i + 1U) * 0.5);
  }
}

template <typename T> void fill_rhs_vector(ksj::array::PooledVector<T>& vector) {
  fill_vector(vector);
}

template <typename Real> void fill_rhs_vector(ksj::array::PooledVector<std::complex<Real>>& vector) {
  fill_complex_vector(vector);
}

template <typename T> void fill_rhs_matrix(ksj::array::PooledMatrix<T>& matrix) {
  fill_matrix(matrix);
}

template <typename Real> void fill_rhs_matrix(ksj::array::PooledMatrix<std::complex<Real>>& matrix) {
  fill_complex_matrix(matrix);
}

template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledMatrix<T>& matrix) {
  return static_cast<double>(as_eigen(matrix).sum());
}

template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledVector<T>& vector) {
  return static_cast<double>(as_eigen(vector).sum());
}

template <typename Real> [[nodiscard]] double checksum(const ksj::array::PooledMatrix<std::complex<Real>>& matrix) {
  return static_cast<double>(as_eigen(matrix).array().abs().sum());
}

template <typename Real> [[nodiscard]] double checksum(const ksj::array::PooledVector<std::complex<Real>>& vector) {
  return static_cast<double>(as_eigen(vector).array().abs().sum());
}

template <typename T>
void run_rank_deficient_least_squares_benchmarks(std::string_view case_prefix, std::string_view type_name,
                                                 const std::size_t size, const std::size_t sample_rows,
                                                 const std::size_t rhs_cols, const ksj::benchmarks::Config& config) {
  if (size < 2U) {
    return;
  }

  const auto vector_case = std::string(case_prefix) + "_vector";
  const auto matrix_case = std::string(case_prefix) + "_matrix";
  const auto vector_case_name = std::string_view{vector_case.data(), vector_case.size()};
  const auto matrix_case_name = std::string_view{matrix_case.data(), matrix_case.size()};

  auto matrix = ksj::array::make_pooled_matrix<T>(sample_rows, size);
  auto rhs = ksj::array::make_pooled_vector<T>(sample_rows);
  auto rhs_matrix = ksj::array::make_pooled_matrix<T>(sample_rows, rhs_cols);
  ksj::benchmarks::require_pooled_storage("rank_deficient_matrix", matrix);
  ksj::benchmarks::require_pooled_storage("rank_deficient_rhs", rhs);
  ksj::benchmarks::require_pooled_storage("rank_deficient_rhs_matrix", rhs_matrix);
  fill_rank_deficient_least_squares_matrix(matrix);
  fill_rhs_vector(rhs);
  fill_rhs_matrix(rhs_matrix);

  auto eigen_rrqr =
    ksj::linalg::detail::eigen::solve_least_squares(matrix, rhs, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
  const auto eigen_rrqr_ns = ksj::benchmarks::measure(config, [&] {
    eigen_rrqr =
      ksj::linalg::detail::eigen::solve_least_squares(matrix, rhs, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
    ksj::benchmarks::do_not_optimize(eigen_rrqr.data()[0]);
  });
  print_linalg_benchmark_row(vector_case_name, "eigen_complete_orthogonal_decomposition", type_name, size, config,
                             eigen_rrqr_ns, checksum(eigen_rrqr));

  auto eigen_svd = ksj::linalg::detail::eigen::solve_least_squares(matrix, rhs, ksj::linalg::LeastSquaresSolver::svd);
  const auto eigen_svd_ns = ksj::benchmarks::measure(config, [&] {
    eigen_svd = ksj::linalg::detail::eigen::solve_least_squares(matrix, rhs, ksj::linalg::LeastSquaresSolver::svd);
    ksj::benchmarks::do_not_optimize(eigen_svd.data()[0]);
  });
  print_linalg_benchmark_row(vector_case_name, "eigen_jacobi_svd", type_name, size, config, eigen_svd_ns,
                             checksum(eigen_svd));

  auto intel_rrqr = ksj::array::make_pooled_vector<T>(size);
  ksj::benchmarks::require_pooled_storage("intel_rank_deficient_rrqr", intel_rrqr);
  if (ksj::linalg::detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs, intel_rrqr)) {
    const auto intel_rrqr_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::linalg::detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs, intel_rrqr);
      ksj::benchmarks::do_not_optimize(intel_rrqr.data()[0]);
    });
    print_linalg_benchmark_row(vector_case_name, "intel_lapacke_gelsy", type_name, size, config, intel_rrqr_ns,
                               checksum(intel_rrqr));
  }

  auto intel_svd = ksj::array::make_pooled_vector<T>(size);
  ksj::benchmarks::require_pooled_storage("intel_rank_deficient_svd", intel_svd);
  if (ksj::linalg::detail::intel::solve_least_squares_svd(matrix, rhs, intel_svd)) {
    const auto intel_svd_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::linalg::detail::intel::solve_least_squares_svd(matrix, rhs, intel_svd);
      ksj::benchmarks::do_not_optimize(intel_svd.data()[0]);
    });
    print_linalg_benchmark_row(vector_case_name, "intel_lapacke_gelss", type_name, size, config, intel_svd_ns,
                               checksum(intel_svd));
  }

  auto public_rrqr = ksj::linalg::solve_least_squares(matrix, rhs, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
  const auto public_rrqr_ns = ksj::benchmarks::measure(config, [&] {
    public_rrqr = ksj::linalg::solve_least_squares(matrix, rhs, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
    ksj::benchmarks::do_not_optimize(public_rrqr.data()[0]);
  });
  print_linalg_benchmark_row(vector_case_name, "public_rank_revealing_policy", type_name, size, config, public_rrqr_ns,
                             checksum(public_rrqr));

  auto eigen_matrix_rrqr = ksj::linalg::detail::eigen::solve_least_squares(
    matrix, rhs_matrix, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
  const auto eigen_matrix_rrqr_ns = ksj::benchmarks::measure(config, [&] {
    eigen_matrix_rrqr = ksj::linalg::detail::eigen::solve_least_squares(
      matrix, rhs_matrix, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
    ksj::benchmarks::do_not_optimize(eigen_matrix_rrqr.data()[0]);
  });
  print_linalg_benchmark_row(matrix_case_name, "eigen_complete_orthogonal_decomposition", type_name, size, config,
                             eigen_matrix_rrqr_ns, checksum(eigen_matrix_rrqr));

  auto eigen_matrix_svd =
    ksj::linalg::detail::eigen::solve_least_squares(matrix, rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
  const auto eigen_matrix_svd_ns = ksj::benchmarks::measure(config, [&] {
    eigen_matrix_svd =
      ksj::linalg::detail::eigen::solve_least_squares(matrix, rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
    ksj::benchmarks::do_not_optimize(eigen_matrix_svd.data()[0]);
  });
  print_linalg_benchmark_row(matrix_case_name, "eigen_jacobi_svd", type_name, size, config, eigen_matrix_svd_ns,
                             checksum(eigen_matrix_svd));

  auto intel_matrix_rrqr = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
  ksj::benchmarks::require_pooled_storage("intel_rank_deficient_matrix_rrqr", intel_matrix_rrqr);
  if (ksj::linalg::detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs_matrix, intel_matrix_rrqr)) {
    const auto intel_matrix_rrqr_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::linalg::detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs_matrix, intel_matrix_rrqr);
      ksj::benchmarks::do_not_optimize(intel_matrix_rrqr.data()[0]);
    });
    print_linalg_benchmark_row(matrix_case_name, "intel_lapacke_gelsy", type_name, size, config, intel_matrix_rrqr_ns,
                               checksum(intel_matrix_rrqr));
  }

  auto intel_matrix_svd = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
  ksj::benchmarks::require_pooled_storage("intel_rank_deficient_matrix_svd", intel_matrix_svd);
  if (ksj::linalg::detail::intel::solve_least_squares_svd(matrix, rhs_matrix, intel_matrix_svd)) {
    const auto intel_matrix_svd_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::linalg::detail::intel::solve_least_squares_svd(matrix, rhs_matrix, intel_matrix_svd);
      ksj::benchmarks::do_not_optimize(intel_matrix_svd.data()[0]);
    });
    print_linalg_benchmark_row(matrix_case_name, "intel_lapacke_gelss", type_name, size, config, intel_matrix_svd_ns,
                               checksum(intel_matrix_svd));
  }

  auto public_matrix_rrqr =
    ksj::linalg::solve_least_squares(matrix, rhs_matrix, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
  const auto public_matrix_rrqr_ns = ksj::benchmarks::measure(config, [&] {
    public_matrix_rrqr =
      ksj::linalg::solve_least_squares(matrix, rhs_matrix, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
    ksj::benchmarks::do_not_optimize(public_matrix_rrqr.data()[0]);
  });
  print_linalg_benchmark_row(matrix_case_name, "public_rank_revealing_policy", type_name, size, config,
                             public_matrix_rrqr_ns, checksum(public_matrix_rrqr));
}

void run_real_benchmarks_float(const ksj::benchmarks::Config& config);
void run_real_benchmarks_double(const ksj::benchmarks::Config& config);

void run_complex_benchmarks_float(const ksj::benchmarks::Config& config);
void run_complex_benchmarks_double(const ksj::benchmarks::Config& config);
void run_complex_policy_gate_benchmarks_float(const ksj::benchmarks::Config& config);
void run_complex_policy_gate_benchmarks_double(const ksj::benchmarks::Config& config);
void run_ecalib_svd_benchmarks_complex_float(const ksj::benchmarks::Config& config);

} // namespace ksj::benchmarks::linalg_benchmarks
