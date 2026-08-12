#pragma once

#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/types.hpp"

#include <optional>
#include <span>
#include <type_traits>

namespace ksj::linalg::detail::intel {

template <typename T> [[nodiscard]] constexpr ksj::array::real_scalar_t<T> default_eigenvalue_floor() noexcept {
  if constexpr (std::is_same_v<ksj::array::real_scalar_t<T>, float>) {
    return 1.0e-12F;
  } else {
    return 1.0e-12;
  }
}

#define KSJ_LINALG_INTEL_WHITENING_DECLS(T)                                                                            \
  bool covariance_centered_product(const ksj::array::PooledMatrix<T>& samples, ksj::array::PooledMatrix<T>& output,    \
                                   bool sample);                                                                       \
  bool whiten_samples(const ksj::array::PooledMatrix<T>& samples, const ksj::array::PooledMatrix<T>& whitening_matrix, \
                      ksj::array::PooledMatrix<T>& output);                                                            \
  bool whitening_matrix_from_covariance(                                                                               \
    const ksj::array::PooledMatrix<T>& covariance, ksj::array::PooledMatrix<T>& output,                                \
    ksj::array::real_scalar_t<T> eigenvalue_floor = default_eigenvalue_floor<T>());

#define KSJ_LINALG_INTEL_PREWHITEN_DECLS(T)                                                                            \
  std::optional<PrewhitenCalibrationResult> cholesky_prewhiten_calibration(                                            \
    const std::span<ksj::array::VectorView<T>> channel_samples, ksj::array::MatrixView<T> inverse_cholesky,            \
    const std::span<ksj::array::real_scalar_t<T>> scale_factors, std::size_t sample_count);

KSJ_LINALG_INTEL_WHITENING_DECLS(float)
KSJ_LINALG_INTEL_WHITENING_DECLS(double)
KSJ_LINALG_INTEL_WHITENING_DECLS(ksj::base::cf32)
KSJ_LINALG_INTEL_WHITENING_DECLS(ksj::base::cf64)

KSJ_LINALG_INTEL_PREWHITEN_DECLS(ksj::base::cf32)
KSJ_LINALG_INTEL_PREWHITEN_DECLS(ksj::base::cf64)

#undef KSJ_LINALG_INTEL_WHITENING_DECLS
#undef KSJ_LINALG_INTEL_PREWHITEN_DECLS

} // namespace ksj::linalg::detail::intel
