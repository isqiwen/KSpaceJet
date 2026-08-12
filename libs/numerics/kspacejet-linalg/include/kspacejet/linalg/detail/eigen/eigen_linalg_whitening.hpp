#pragma once

#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/types.hpp"

#include <cstddef>
#include <span>

namespace ksj::linalg::detail::eigen {

#define KSJ_LINALG_EIGEN_WHITENING_DECLS(T)                                                                            \
  void covariance(const ksj::array::PooledMatrix<T>& samples, ksj::array::PooledMatrix<T>& output, bool sample);       \
  void validate_whitening_covariance(const ksj::array::PooledMatrix<T>& covariance,                                    \
                                     ksj::array::real_scalar_t<T> eigenvalue_floor);                                   \
  void whitening_matrix_from_self_adjoint_eigen(                                                                       \
    const ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& eigenvalues,                                         \
    const ksj::array::PooledMatrix<T>& eigenvectors, ksj::array::PooledMatrix<T>& output,                              \
    ksj::array::real_scalar_t<T> eigenvalue_floor);                                                                    \
  ksj::array::PooledMatrix<T> whitening_matrix_from_covariance(const ksj::array::PooledMatrix<T>& covariance,          \
                                                               ksj::array::real_scalar_t<T> eigenvalue_floor);         \
  void whiten_samples(const ksj::array::PooledMatrix<T>& samples, const ksj::array::PooledMatrix<T>& whitening_matrix, \
                      ksj::array::PooledMatrix<T>& output);                                                            \
  ksj::array::PooledMatrix<T> whiten_samples(const ksj::array::PooledMatrix<T>& samples,                               \
                                             const ksj::array::PooledMatrix<T>& whitening_matrix);

#define KSJ_LINALG_EIGEN_PREWHITEN_DECLS(T)                                                                            \
  bool has_valid_prewhiten_shape(                                                                                      \
    const std::span<ksj::array::VectorView<T>> channel_samples, ksj::array::MatrixView<T> inverse_cholesky,            \
    const std::span<ksj::array::real_scalar_t<T>> scale_factors, std::size_t sample_count);                            \
  void remove_channel_mean(const std::span<ksj::array::VectorView<T>> channel_samples, std::size_t sample_count);      \
  bool invert_lower_triangular(ksj::array::MatrixView<const T> lower, ksj::array::MatrixView<T> output);               \
  PrewhitenCalibrationResult cholesky_prewhiten_calibration(                                                           \
    const std::span<ksj::array::VectorView<T>> channel_samples, ksj::array::MatrixView<T> inverse_cholesky,            \
    const std::span<ksj::array::real_scalar_t<T>> scale_factors, std::size_t sample_count);                            \
  bool apply_cholesky_prewhiten(ksj::array::MatrixView<T> inverse_cholesky,                                            \
                                const std::span<ksj::array::VectorView<T>> channel_samples, std::size_t sample_count);

KSJ_LINALG_EIGEN_WHITENING_DECLS(float)
KSJ_LINALG_EIGEN_WHITENING_DECLS(double)
KSJ_LINALG_EIGEN_WHITENING_DECLS(ksj::base::cf32)
KSJ_LINALG_EIGEN_WHITENING_DECLS(ksj::base::cf64)

KSJ_LINALG_EIGEN_PREWHITEN_DECLS(ksj::base::cf32)
KSJ_LINALG_EIGEN_PREWHITEN_DECLS(ksj::base::cf64)

#undef KSJ_LINALG_EIGEN_WHITENING_DECLS
#undef KSJ_LINALG_EIGEN_PREWHITEN_DECLS

} // namespace ksj::linalg::detail::eigen
