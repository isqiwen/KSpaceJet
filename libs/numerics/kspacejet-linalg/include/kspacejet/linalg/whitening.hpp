#pragma once

/// Whitening and covariance-normalization operations for dense real and complex data.

#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/detail/eigen/eigen_linalg_whitening.hpp"
#include "kspacejet/linalg/detail/intel/intel_linalg_whitening.hpp"
#include "kspacejet/linalg/detail/linalg_types.hpp"
#include "kspacejet/linalg/detail/linalg_policy.hpp"
#include "kspacejet/linalg/types.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace ksj::linalg {

// Materializes a whitening matrix from a caller-owned self-adjoint
// eigendecomposition.  This is the allocation-free counterpart to
// whitening_matrix_from_covariance(): eigenvalues/eigenvectors originate from
// an explicit workspace decomposition, and `output` is the caller-owned
// destination.  The eigenvector columns and eigenvalues must be paired.
template <typename T>
void whitening_matrix_from_self_adjoint_eigen_with_workspace(
  const ksj::array::VectorView<const ksj::array::real_scalar_t<T>> eigenvalues,
  const ksj::array::MatrixView<const T> eigenvectors, const ksj::array::MatrixView<T> output,
  const ksj::array::real_scalar_t<T> eigenvalue_floor = static_cast<ksj::array::real_scalar_t<T>>(1.0e-12)) {
  detail::require_supported_linalg_scalar<T>();
  using real_type = ksj::array::real_scalar_t<T>;
  if (eigenvalues.empty() || eigenvectors.rows() != eigenvectors.cols() || eigenvectors.rows() != eigenvalues.size() ||
      output.rows() != eigenvectors.rows() || output.cols() != eigenvectors.cols() || eigenvalue_floor <= real_type{} ||
      eigenvalues.data() == nullptr || eigenvectors.data() == nullptr || output.data() == nullptr ||
      output.data() == eigenvectors.data()) {
    throw std::invalid_argument("whitening self-adjoint eigendecomposition dimension mismatch");
  }
  for (std::size_t row = 0U; row < output.rows(); ++row) {
    for (std::size_t column = 0U; column < output.cols(); ++column) {
      T sum{};
      for (std::size_t eigenvector = 0U; eigenvector < eigenvalues.size(); ++eigenvector) {
        const auto inverse_sqrt = real_type{1} / std::sqrt(std::max(eigenvalues(eigenvector), eigenvalue_floor));
        if constexpr (ksj::array::is_complex_v<T>) {
          sum += eigenvectors(row, eigenvector) * inverse_sqrt * std::conj(eigenvectors(column, eigenvector));
        } else {
          sum += eigenvectors(row, eigenvector) * inverse_sqrt * eigenvectors(column, eigenvector);
        }
      }
      output(row, column) = sum;
    }
  }
}

template <typename T>
[[nodiscard]] Matrix<T> whitening_matrix_from_covariance(
  const Matrix<T>& covariance,
  const ksj::array::real_scalar_t<T> eigenvalue_floor = static_cast<ksj::array::real_scalar_t<T>>(1.0e-12)) {
  detail::require_supported_linalg_scalar<T>();
  detail::eigen::validate_whitening_covariance(covariance, eigenvalue_floor);
  auto output = ksj::array::make_pooled_matrix<T>(covariance.rows(), covariance.cols());
  if (detail::prefer_intel_whitening_matrix<T>(covariance.rows()) &&
      detail::intel::whitening_matrix_from_covariance(covariance, output, eigenvalue_floor)) {
    return output;
  }

  return detail::eigen::whitening_matrix_from_covariance(covariance, eigenvalue_floor);
}

template <typename T>
void whiten_samples(const Matrix<T>& samples, const Matrix<T>& whitening_matrix, Matrix<T>& output) {
  detail::require_supported_linalg_scalar<T>();
  if (whitening_matrix.rows() != whitening_matrix.cols() || samples.cols() != whitening_matrix.rows() ||
      output.rows() != samples.rows() || output.cols() != samples.cols()) {
    throw std::invalid_argument("whiten_samples dimension mismatch");
  }

  if (detail::prefer_intel_whiten_samples<T>(samples.rows(), samples.cols()) &&
      detail::intel::whiten_samples(samples, whitening_matrix, output)) {
    return;
  }

  detail::eigen::whiten_samples(samples, whitening_matrix, output);
}

template <typename T>
[[nodiscard]] Matrix<T> whiten_samples(const Matrix<T>& samples, const Matrix<T>& whitening_matrix) {
  auto output = ksj::array::make_pooled_matrix<T>(samples.rows(), samples.cols());
  whiten_samples(samples, whitening_matrix, output);
  return output;
}

template <typename T>
[[nodiscard]] PrewhitenCalibrationResult
cholesky_prewhiten_calibration(std::span<ksj::array::VectorView<T>> channel_samples,
                               ksj::array::MatrixView<T> inverse_cholesky,
                               std::span<ksj::array::real_scalar_t<T>> scale_factors, std::size_t sample_count) {
  detail::require_supported_linalg_scalar<T>();
  static_assert(ksj::array::is_complex_v<T>, "cholesky_prewhiten_calibration requires complex samples");
  const auto channel_count = channel_samples.size();
  if (channel_count == 0U || sample_count == 0U || inverse_cholesky.data() == nullptr ||
      inverse_cholesky.rows() < channel_count || inverse_cholesky.cols() < channel_count ||
      scale_factors.data() == nullptr || scale_factors.size() < channel_count) {
    return PrewhitenCalibrationResult{PrewhitenCalibrationStatus::invalid_input};
  }
  for (const auto samples : channel_samples) {
    if (samples.data() == nullptr || samples.size() < sample_count) {
      return PrewhitenCalibrationResult{PrewhitenCalibrationStatus::invalid_input};
    }
  }

  if (auto intel_result =
        detail::intel::cholesky_prewhiten_calibration(channel_samples, inverse_cholesky, scale_factors, sample_count)) {
    return *intel_result;
  }

  return detail::eigen::cholesky_prewhiten_calibration(channel_samples, inverse_cholesky, scale_factors, sample_count);
}

template <typename T>
[[nodiscard]] bool apply_cholesky_prewhiten(ksj::array::MatrixView<T> inverse_cholesky,
                                            std::span<ksj::array::VectorView<T>> channel_samples,
                                            std::size_t sample_count) {
  detail::require_supported_linalg_scalar<T>();
  static_assert(ksj::array::is_complex_v<T>, "apply_cholesky_prewhiten requires complex samples");
  const auto channel_count = channel_samples.size();
  if (channel_count == 0U || sample_count == 0U || inverse_cholesky.data() == nullptr ||
      inverse_cholesky.rows() < channel_count || inverse_cholesky.cols() < channel_count) {
    return false;
  }
  for (const auto samples : channel_samples) {
    if (samples.data() == nullptr || samples.size() < sample_count) {
      return false;
    }
  }

  return detail::eigen::apply_cholesky_prewhiten(inverse_cholesky, channel_samples, sample_count);
}

} // namespace ksj::linalg
