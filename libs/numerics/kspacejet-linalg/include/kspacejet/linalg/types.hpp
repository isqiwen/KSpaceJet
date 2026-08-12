#pragma once

/// Linear-algebra enums and result types shared by decomposition and solver APIs.

#include "kspacejet/array/array.hpp"

#include <complex>

namespace ksj::linalg {

template <typename T> using Vector = ksj::array::PooledVector<T>;

template <typename T> using Matrix = ksj::array::PooledMatrix<T>;

enum class LeastSquaresSolver {
  qr,
  rank_revealing_qr,
  svd,
  normal_equations,
  normal_equations_cholesky,
};

enum class SvdMode {
  thin,
  full,
};

template <typename T> struct SingularValueDecomposition {
  ksj::array::PooledMatrix<T> u;
  ksj::array::PooledVector<ksj::array::real_scalar_t<T>> singular_values;
  ksj::array::PooledMatrix<T> v_adjoint;
};

template <typename T> struct SelfAdjointEigenDecomposition {
  ksj::array::PooledVector<ksj::array::real_scalar_t<T>> eigenvalues;
  ksj::array::PooledMatrix<T> eigenvectors;
};

template <typename T> struct EigenDecomposition {
  using complex_type = std::complex<ksj::array::real_scalar_t<T>>;

  ksj::array::PooledVector<complex_type> eigenvalues;
  ksj::array::PooledMatrix<complex_type> eigenvectors;
};

enum class PrewhitenCalibrationStatus {
  success,
  invalid_input,
  near_zero_noise,
  cholesky_failed,
  inverse_failed,
};

struct PrewhitenCalibrationResult {
  PrewhitenCalibrationStatus status{PrewhitenCalibrationStatus::success};
  int error_channel_id{};
  float sigma_average{};
  float sigma_min{};
  float sigma_max{};
};

} // namespace ksj::linalg
