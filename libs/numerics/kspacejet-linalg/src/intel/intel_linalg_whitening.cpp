#include "kspacejet/linalg/detail/intel/intel_linalg_whitening.hpp"
#include "intel_linalg_common.hpp"

#include "kspacejet/linalg/detail/eigen/eigen_linalg_whitening.hpp"
#include "kspacejet/linalg/detail/intel/intel_linalg_decompositions.hpp"

namespace ksj::linalg::detail::intel {

template <typename T>
[[nodiscard]] bool covariance_centered_product(const ksj::array::PooledMatrix<T>& samples,
                                               ksj::array::PooledMatrix<T>& output, const bool sample_normalized) {
  if constexpr (lapack_solve_scalar_v<T>) {
    using real_type = ksj::array::real_scalar_t<T>;
    if (output.rows() != samples.cols() || output.cols() != samples.cols() || samples.rows() == 0U ||
        samples.cols() == 0U || !fits_lapack_int(samples.rows()) || !fits_lapack_int(samples.cols())) {
      return false;
    }
    const auto denominator = sample_normalized ? samples.rows() - 1U : samples.rows();
    if (denominator == 0U) {
      return false;
    }

    auto mean = ksj::array::make_pooled_vector<T>(samples.cols());
    ksj::array::fill(mean.view(), T{});
    for (std::size_t col = 0; col < samples.cols(); ++col) {
      for (std::size_t row = 0; row < samples.rows(); ++row) {
        mean(col) += samples(row, col);
      }
      mean(col) /= static_cast<real_type>(samples.rows());
    }

    auto centered = ksj::array::make_pooled_matrix<T>(samples.rows(), samples.cols());
    for (std::size_t col = 0; col < samples.cols(); ++col) {
      for (std::size_t row = 0; row < samples.rows(); ++row) {
        centered(row, col) = samples(row, col) - mean(col);
      }
    }

    gram(centered.data(), output.data(), centered.rows(), centered.cols(),
         real_type{1} / static_cast<real_type>(denominator));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool whiten_samples(const ksj::array::PooledMatrix<T>& samples,
                                  const ksj::array::PooledMatrix<T>& whitening_matrix,
                                  ksj::array::PooledMatrix<T>& output) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (whitening_matrix.rows() != whitening_matrix.cols() || samples.cols() != whitening_matrix.rows() ||
        output.rows() != samples.rows() || output.cols() != whitening_matrix.cols() ||
        !fits_lapack_int(samples.rows()) || !fits_lapack_int(samples.cols()) ||
        !fits_lapack_int(whitening_matrix.cols())) {
      return false;
    }
    gemm(samples.data(), whitening_matrix.data(), output.data(), samples.rows(), samples.cols(),
         whitening_matrix.cols());
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] std::optional<PrewhitenCalibrationResult> cholesky_prewhiten_calibration(
  const std::span<ksj::array::VectorView<T>> channel_samples, ksj::array::MatrixView<T> inverse_cholesky,
  const std::span<ksj::array::real_scalar_t<T>> scale_factors, const std::size_t sample_count) {
  if constexpr (lapack_complex_scalar_v<T>) {
    using real_type = ksj::array::real_scalar_t<T>;
    PrewhitenCalibrationResult result{};
    if (!ksj::linalg::detail::eigen::has_valid_prewhiten_shape(channel_samples, inverse_cholesky, scale_factors,
                                                               sample_count)) {
      result.status = PrewhitenCalibrationStatus::invalid_input;
      return result;
    }

    const auto channel_count = channel_samples.size();
    if (!fits_lapack_int(channel_count)) {
      return std::nullopt;
    }

    ksj::linalg::detail::eigen::remove_channel_mean(channel_samples, sample_count);

    auto covariance = ksj::array::make_pooled_matrix<T>(channel_count, channel_count);
    ksj::array::fill(covariance.view(), T{});
    for (std::size_t row = 0; row < channel_count; ++row) {
      for (std::size_t col = 0; col <= row; ++col) {
        real_type real_sum{};
        real_type imag_sum{};
        for (std::size_t sample = 0; sample < sample_count; ++sample) {
          const auto lhs = channel_samples[row](sample);
          const auto rhs = channel_samples[col](sample);
          real_sum += lhs.real() * rhs.real() + lhs.imag() * rhs.imag();
          imag_sum += lhs.real() * rhs.imag() - lhs.imag() * rhs.real();
        }

        const auto real = real_sum / static_cast<real_type>(sample_count);
        const auto imag = row == col ? real_type{} : imag_sum / static_cast<real_type>(sample_count);
        covariance(row, col) = T{real, -imag};
        covariance(col, row) = T{real, imag};

        if (row == col) {
          if (real < static_cast<real_type>(1e-5)) {
            result.error_channel_id = static_cast<int>(row);
            result.status = PrewhitenCalibrationStatus::near_zero_noise;
            return result;
          }

          result.sigma_average += static_cast<float>(real);
          if (row == 0) {
            result.sigma_min = static_cast<float>(real);
            result.sigma_max = static_cast<float>(real);
          }
          result.sigma_min = std::min(result.sigma_min, static_cast<float>(real));
          result.sigma_max = std::max(result.sigma_max, static_cast<float>(real));
        }
      }
    }

    result.sigma_average /= static_cast<float>(channel_count);
    const auto sigma_average = static_cast<real_type>(result.sigma_average);
    for (std::size_t row = 0; row < channel_count; ++row) {
      for (std::size_t col = 0; col < channel_count; ++col) {
        covariance(row, col) /= sigma_average;
      }
      scale_factors[row] = real_type{1} / std::sqrt(covariance(row, row).real());
    }

    const auto n = static_cast<lapack_int>(channel_count);
    if (potrf(n, lapack_data(covariance.data()), n) != 0) {
      result.error_channel_id = 0;
      result.status = PrewhitenCalibrationStatus::cholesky_failed;
      return result;
    }

    for (std::size_t row = 0; row < channel_count; ++row) {
      for (std::size_t col = row + 1U; col < channel_count; ++col) {
        covariance(row, col) = T{};
      }
    }

    if (trtri(n, lapack_data(covariance.data()), n) != 0) {
      result.error_channel_id = 0;
      result.status = PrewhitenCalibrationStatus::inverse_failed;
      return result;
    }

    for (std::size_t row = 0; row < channel_count; ++row) {
      for (std::size_t col = 0; col < channel_count; ++col) {
        inverse_cholesky(row, col) = covariance(row, col);
      }
    }

    return result;
  } else {
    return std::nullopt;
  }
}

template <typename T>
[[nodiscard]] bool whitening_matrix_from_covariance(const ksj::array::PooledMatrix<T>& covariance,
                                                    ksj::array::PooledMatrix<T>& output,
                                                    const ksj::array::real_scalar_t<T> eigenvalue_floor) {
  using real_type = ksj::array::real_scalar_t<T>;
  if (covariance.rows() != covariance.cols() || covariance.empty() || output.rows() != covariance.rows() ||
      output.cols() != covariance.cols() || eigenvalue_floor <= real_type{}) {
    return false;
  }

  auto eigenvalues = ksj::array::make_pooled_vector<real_type>(covariance.rows());
  auto eigenvectors = ksj::array::make_pooled_matrix<T>(covariance.rows(), covariance.cols());
  if (!self_adjoint_eigen_decomposition(covariance, eigenvalues, eigenvectors)) {
    return false;
  }

  eigen::whitening_matrix_from_self_adjoint_eigen(eigenvalues, eigenvectors, output, eigenvalue_floor);
  return true;
}

#define KSJ_LINALG_INTEL_WHITENING_WRAPPERS(T)                                                                         \
  bool covariance_centered_product(const ksj::array::PooledMatrix<T>& samples, ksj::array::PooledMatrix<T>& output,    \
                                   bool sample) {                                                                      \
    return covariance_centered_product<T>(samples, output, sample);                                                    \
  }                                                                                                                    \
  bool whiten_samples(const ksj::array::PooledMatrix<T>& samples, const ksj::array::PooledMatrix<T>& whitening_matrix, \
                      ksj::array::PooledMatrix<T>& output) {                                                           \
    return whiten_samples<T>(samples, whitening_matrix, output);                                                       \
  }                                                                                                                    \
  bool whitening_matrix_from_covariance(const ksj::array::PooledMatrix<T>& covariance,                                 \
                                        ksj::array::PooledMatrix<T>& output,                                           \
                                        ksj::array::real_scalar_t<T> eigenvalue_floor) {                               \
    return whitening_matrix_from_covariance<T>(covariance, output, eigenvalue_floor);                                  \
  }

#define KSJ_LINALG_INTEL_PREWHITEN_WRAPPERS(T)                                                                         \
  std::optional<PrewhitenCalibrationResult> cholesky_prewhiten_calibration(                                            \
    const std::span<ksj::array::VectorView<T>> channel_samples, ksj::array::MatrixView<T> inverse_cholesky,            \
    const std::span<ksj::array::real_scalar_t<T>> scale_factors, std::size_t sample_count) {                           \
    return cholesky_prewhiten_calibration<T>(channel_samples, inverse_cholesky, scale_factors, sample_count);          \
  }

KSJ_LINALG_INTEL_WHITENING_WRAPPERS(float)
KSJ_LINALG_INTEL_WHITENING_WRAPPERS(double)
KSJ_LINALG_INTEL_WHITENING_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_INTEL_WHITENING_WRAPPERS(ksj::base::cf64)
KSJ_LINALG_INTEL_PREWHITEN_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_INTEL_PREWHITEN_WRAPPERS(ksj::base::cf64)

#undef KSJ_LINALG_INTEL_WHITENING_WRAPPERS
#undef KSJ_LINALG_INTEL_PREWHITEN_WRAPPERS

} // namespace ksj::linalg::detail::intel
