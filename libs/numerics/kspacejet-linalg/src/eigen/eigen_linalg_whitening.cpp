#include "kspacejet/linalg/detail/eigen/eigen_linalg_whitening.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"

#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <span>
#include <stdexcept>

#include <Eigen/Eigenvalues>

namespace ksj::linalg::detail::eigen {
namespace {
using ksj::array::detail::eigen_adapter::as_eigen;
}

template <typename T> [[nodiscard]] T conjugate_if_complex(const T& value) {
  if constexpr (ksj::array::is_complex_v<T>) {
    return std::conj(value);
  } else {
    return value;
  }
}

template <typename T>
void covariance(const ksj::array::PooledMatrix<T>& samples, ksj::array::PooledMatrix<T>& output,
                const bool sample_normalized) {
  if (output.rows() != samples.cols() || output.cols() != samples.cols()) {
    throw std::invalid_argument("covariance output dimension mismatch");
  }
  if (samples.rows() == 0U || samples.cols() == 0U) {
    throw std::invalid_argument("covariance input must not be empty");
  }
  const auto denominator = sample_normalized ? samples.rows() - 1U : samples.rows();
  if (denominator == 0U) {
    throw std::invalid_argument("sample covariance requires at least two samples");
  }

  auto mean = ksj::array::make_pooled_vector<T>(samples.cols());
  ksj::array::fill(mean.view(), T{});
  for (std::size_t col = 0; col < samples.cols(); ++col) {
    for (std::size_t row = 0; row < samples.rows(); ++row) {
      mean(col) += samples(row, col);
    }
    mean(col) /= static_cast<ksj::array::real_scalar_t<T>>(samples.rows());
  }

  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      T sum{};
      for (std::size_t sample = 0; sample < samples.rows(); ++sample) {
        const auto lhs = samples(sample, row) - mean(row);
        const auto rhs = samples(sample, col) - mean(col);
        sum += conjugate_if_complex(lhs) * rhs;
      }
      output(row, col) = sum / static_cast<ksj::array::real_scalar_t<T>>(denominator);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> covariance(const ksj::array::PooledMatrix<T>& samples,
                                                     const bool sample_normalized) {
  auto output = ksj::array::make_pooled_matrix<T>(samples.cols(), samples.cols());
  covariance(samples, output, sample_normalized);
  return output;
}

template <typename T>
void validate_whitening_covariance(const ksj::array::PooledMatrix<T>& covariance,
                                   const ksj::array::real_scalar_t<T> eigenvalue_floor) {
  using real_type = ksj::array::real_scalar_t<T>;
  if (covariance.rows() != covariance.cols()) {
    throw std::invalid_argument("whitening covariance must be square");
  }
  if (covariance.empty()) {
    throw std::invalid_argument("whitening covariance must not be empty");
  }
  if (eigenvalue_floor <= real_type{}) {
    throw std::invalid_argument("whitening eigenvalue floor must be positive");
  }
}

template <typename T>
void whitening_matrix_from_self_adjoint_eigen(const ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& eigenvalues,
                                              const ksj::array::PooledMatrix<T>& eigenvectors,
                                              ksj::array::PooledMatrix<T>& output,
                                              const ksj::array::real_scalar_t<T> eigenvalue_floor) {
  using real_type = ksj::array::real_scalar_t<T>;
  if (eigenvectors.rows() != eigenvectors.cols() || eigenvalues.size() != eigenvectors.rows() ||
      output.rows() != eigenvectors.rows() || output.cols() != eigenvectors.cols()) {
    throw std::invalid_argument("whitening eigendecomposition dimension mismatch");
  }

  auto inverse_sqrt = ksj::array::make_pooled_vector<real_type>(eigenvalues.size());
  for (std::size_t index = 0; index < inverse_sqrt.size(); ++index) {
    inverse_sqrt(index) = real_type{1} / std::sqrt(std::max(eigenvalues(index), eigenvalue_floor));
  }
  as_eigen(output) = as_eigen(eigenvectors) * as_eigen(inverse_sqrt).asDiagonal() * as_eigen(eigenvectors).adjoint();
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T>
whitening_matrix_from_covariance(const ksj::array::PooledMatrix<T>& covariance,
                                 const ksj::array::real_scalar_t<T> eigenvalue_floor) {
  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  using real_type = ksj::array::real_scalar_t<T>;
  validate_whitening_covariance(covariance, eigenvalue_floor);

  const dense_matrix dense_covariance = as_eigen(covariance);
  Eigen::SelfAdjointEigenSolver<dense_matrix> solver(dense_covariance);
  if (solver.info() != Eigen::Success) {
    throw std::invalid_argument("whitening eigensolve failed");
  }

  auto inverse_sqrt = solver.eigenvalues();
  for (Eigen::Index index = 0; index < inverse_sqrt.size(); ++index) {
    inverse_sqrt(index) = real_type{1} / std::sqrt(std::max(inverse_sqrt(index), eigenvalue_floor));
  }

  auto output = ksj::array::make_pooled_matrix<T>(covariance.rows(), covariance.cols());
  as_eigen(output) = solver.eigenvectors() * inverse_sqrt.asDiagonal() * solver.eigenvectors().adjoint();
  return output;
}

template <typename T>
void whiten_samples(const ksj::array::PooledMatrix<T>& samples, const ksj::array::PooledMatrix<T>& whitening_matrix,
                    ksj::array::PooledMatrix<T>& output) {
  if (whitening_matrix.rows() != whitening_matrix.cols() || samples.cols() != whitening_matrix.rows() ||
      output.rows() != samples.rows() || output.cols() != samples.cols()) {
    throw std::invalid_argument("whiten_samples dimension mismatch");
  }
  as_eigen(output) = as_eigen(samples) * as_eigen(whitening_matrix);
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> whiten_samples(const ksj::array::PooledMatrix<T>& samples,
                                                         const ksj::array::PooledMatrix<T>& whitening_matrix) {
  auto output = ksj::array::make_pooled_matrix<T>(samples.rows(), samples.cols());
  whiten_samples(samples, whitening_matrix, output);
  return output;
}

template <typename T>
[[nodiscard]] bool has_valid_prewhiten_shape(const std::span<ksj::array::VectorView<T>> channel_samples,
                                             const ksj::array::MatrixView<T> inverse_cholesky,
                                             const std::span<ksj::array::real_scalar_t<T>> scale_factors,
                                             const std::size_t sample_count) noexcept {
  const auto channel_count = channel_samples.size();
  if (channel_count == 0 || sample_count == 0 || inverse_cholesky.data() == nullptr ||
      inverse_cholesky.rows() < channel_count || inverse_cholesky.cols() < channel_count ||
      scale_factors.data() == nullptr || scale_factors.size() < channel_count) {
    return false;
  }

  for (const auto samples : channel_samples) {
    if (samples.data() == nullptr || samples.size() < sample_count) {
      return false;
    }
  }

  return true;
}

template <typename T>
void remove_channel_mean(const std::span<ksj::array::VectorView<T>> channel_samples, const std::size_t sample_count) {
  for (auto samples : channel_samples) {
    T mean{};
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
      mean += samples(sample);
    }
    mean /= static_cast<ksj::array::real_scalar_t<T>>(sample_count);

    for (std::size_t sample = 0; sample < sample_count; ++sample) {
      samples(sample) -= mean;
    }
  }
}

template <typename T>
[[nodiscard]] bool invert_lower_triangular(const ksj::array::MatrixView<const T> lower,
                                           ksj::array::MatrixView<T> inverse, const std::size_t size) {
  using real_type = ksj::array::real_scalar_t<T>;
  auto inverse_work = ksj::array::make_pooled_matrix<T>(size, size);
  ksj::array::fill(inverse_work.view(), T{});
  for (std::size_t col = 0; col < size; ++col) {
    for (std::size_t row = col; row < size; ++row) {
      const auto diagonal = lower(row, row);
      if (std::abs(diagonal) <= std::numeric_limits<real_type>::epsilon()) {
        return false;
      }

      if (row == col) {
        inverse_work(row, col) = real_type{1} / diagonal;
        continue;
      }

      T sum{};
      for (std::size_t inner = col; inner < row; ++inner) {
        sum += lower(row, inner) * inverse_work(inner, col);
      }
      inverse_work(row, col) = -sum / diagonal;
    }
  }

  for (std::size_t row = 0; row < size; ++row) {
    for (std::size_t col = 0; col < size; ++col) {
      inverse(row, col) = inverse_work(row, col);
    }
  }

  return true;
}

template <typename T>
[[nodiscard]] PrewhitenCalibrationResult cholesky_prewhiten_calibration(
  const std::span<ksj::array::VectorView<T>> channel_samples, ksj::array::MatrixView<T> inverse_cholesky,
  const std::span<ksj::array::real_scalar_t<T>> scale_factors, const std::size_t sample_count) {
  using real_type = ksj::array::real_scalar_t<T>;
  PrewhitenCalibrationResult result{};
  if (!has_valid_prewhiten_shape(channel_samples, inverse_cholesky, scale_factors, sample_count)) {
    result.status = PrewhitenCalibrationStatus::invalid_input;
    return result;
  }

  const auto channel_count = channel_samples.size();
  remove_channel_mean(channel_samples, sample_count);

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

      const real_type real = real_sum / static_cast<real_type>(sample_count);
      const real_type imag = row == col ? real_type{} : imag_sum / static_cast<real_type>(sample_count);
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
  const real_type sigma_average = static_cast<real_type>(result.sigma_average);
  for (std::size_t row = 0; row < channel_count; ++row) {
    for (std::size_t col = 0; col < channel_count; ++col) {
      covariance(row, col) /= sigma_average;
    }
    scale_factors[row] = real_type{1} / std::sqrt(covariance(row, row).real());
  }

  auto lower = ksj::array::make_pooled_matrix<T>(channel_count, channel_count);
  ksj::array::fill(lower.view(), T{});
  for (std::size_t row = 0; row < channel_count; ++row) {
    for (std::size_t col = 0; col <= row; ++col) {
      T sum = covariance(row, col);
      for (std::size_t inner = 0; inner < col; ++inner) {
        sum -= lower(row, inner) * std::conj(lower(col, inner));
      }

      if (row == col) {
        const real_type diagonal = sum.real();
        if (diagonal <= real_type{} || !std::isfinite(diagonal)) {
          result.error_channel_id = 0;
          result.status = PrewhitenCalibrationStatus::cholesky_failed;
          return result;
        }
        lower(row, col) = T{std::sqrt(diagonal), real_type{}};
      } else {
        const auto pivot = lower(col, col);
        if (std::abs(pivot) <= std::numeric_limits<real_type>::epsilon()) {
          result.error_channel_id = 0;
          result.status = PrewhitenCalibrationStatus::cholesky_failed;
          return result;
        }
        lower(row, col) = sum / pivot;
      }
    }
  }

  const ksj::array::MatrixView<const T> lower_view(lower.data(), lower.rows(), lower.cols());
  if (!invert_lower_triangular(lower_view, inverse_cholesky, channel_count)) {
    result.error_channel_id = 0;
    result.status = PrewhitenCalibrationStatus::inverse_failed;
    return result;
  }

  return result;
}

template <typename T>
[[nodiscard]] bool apply_cholesky_prewhiten(const ksj::array::MatrixView<T> inverse_cholesky,
                                            const std::span<ksj::array::VectorView<T>> channel_samples,
                                            const std::size_t sample_count) {
  constexpr std::size_t kStackPrewhitenChannels = 64U;
  const auto channel_count = channel_samples.size();
  if (channel_count == 0 || sample_count == 0 || inverse_cholesky.data() == nullptr ||
      inverse_cholesky.rows() < channel_count || inverse_cholesky.cols() < channel_count) {
    return false;
  }

  bool samples_are_contiguous = true;
  for (const auto samples : channel_samples) {
    if (samples.data() == nullptr || samples.size() < sample_count) {
      return false;
    }
    samples_are_contiguous = samples_are_contiguous && samples.stride() == 1U;
  }

  std::array<T, kStackPrewhitenChannels> stack_mixed_sample{};
  auto heap_mixed_sample = ksj::array::PooledVector<T>();
  T* mixed_sample = stack_mixed_sample.data();
  if (channel_count > stack_mixed_sample.size()) {
    heap_mixed_sample.resize(channel_count);
    mixed_sample = heap_mixed_sample.data();
  }

  const auto* const inverse_data = inverse_cholesky.data();
  const auto inverse_row_stride = inverse_cholesky.row_stride();
  const auto inverse_col_stride = inverse_cholesky.col_stride();

  if (samples_are_contiguous) {
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
      for (std::size_t output_channel = 0; output_channel < channel_count; ++output_channel) {
        const auto* const inverse_row = inverse_data + output_channel * inverse_row_stride;
        T sum{};
        for (std::size_t input_channel = 0; input_channel <= output_channel; ++input_channel) {
          sum += inverse_row[input_channel * inverse_col_stride] * channel_samples[input_channel].data()[sample];
        }
        mixed_sample[output_channel] = sum;
      }

      for (std::size_t channel = 0; channel < channel_count; ++channel) {
        channel_samples[channel].data()[sample] = mixed_sample[channel];
      }
    }
    return true;
  }

  for (std::size_t sample = 0; sample < sample_count; ++sample) {
    for (std::size_t output_channel = 0; output_channel < channel_count; ++output_channel) {
      const auto* const inverse_row = inverse_data + output_channel * inverse_row_stride;
      T sum{};
      for (std::size_t input_channel = 0; input_channel <= output_channel; ++input_channel) {
        sum += inverse_row[input_channel * inverse_col_stride] * channel_samples[input_channel][sample];
      }
      mixed_sample[output_channel] = sum;
    }

    for (std::size_t channel = 0; channel < channel_count; ++channel) {
      channel_samples[channel][sample] = mixed_sample[channel];
    }
  }

  return true;
}

#define KSJ_LINALG_EIGEN_WHITENING_WRAPPERS(T)                                                                         \
  void covariance(const ksj::array::PooledMatrix<T>& samples, ksj::array::PooledMatrix<T>& output, bool sample) {      \
    covariance<T>(samples, output, sample);                                                                            \
  }                                                                                                                    \
  void validate_whitening_covariance(const ksj::array::PooledMatrix<T>& covariance,                                    \
                                     ksj::array::real_scalar_t<T> eigenvalue_floor) {                                  \
    validate_whitening_covariance<T>(covariance, eigenvalue_floor);                                                    \
  }                                                                                                                    \
  void whitening_matrix_from_self_adjoint_eigen(                                                                       \
    const ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& eigenvalues,                                         \
    const ksj::array::PooledMatrix<T>& eigenvectors, ksj::array::PooledMatrix<T>& output,                              \
    ksj::array::real_scalar_t<T> eigenvalue_floor) {                                                                   \
    whitening_matrix_from_self_adjoint_eigen<T>(eigenvalues, eigenvectors, output, eigenvalue_floor);                  \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> whitening_matrix_from_covariance(const ksj::array::PooledMatrix<T>& covariance,          \
                                                               ksj::array::real_scalar_t<T> eigenvalue_floor) {        \
    return whitening_matrix_from_covariance<T>(covariance, eigenvalue_floor);                                          \
  }                                                                                                                    \
  void whiten_samples(const ksj::array::PooledMatrix<T>& samples, const ksj::array::PooledMatrix<T>& whitening_matrix, \
                      ksj::array::PooledMatrix<T>& output) {                                                           \
    whiten_samples<T>(samples, whitening_matrix, output);                                                              \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> whiten_samples(const ksj::array::PooledMatrix<T>& samples,                               \
                                             const ksj::array::PooledMatrix<T>& whitening_matrix) {                    \
    return whiten_samples<T>(samples, whitening_matrix);                                                               \
  }

#define KSJ_LINALG_EIGEN_PREWHITEN_WRAPPERS(T)                                                                         \
  bool has_valid_prewhiten_shape(                                                                                      \
    const std::span<ksj::array::VectorView<T>> channel_samples, ksj::array::MatrixView<T> inverse_cholesky,            \
    const std::span<ksj::array::real_scalar_t<T>> scale_factors, std::size_t sample_count) {                           \
    return has_valid_prewhiten_shape<T>(channel_samples, inverse_cholesky, scale_factors, sample_count);               \
  }                                                                                                                    \
  void remove_channel_mean(const std::span<ksj::array::VectorView<T>> channel_samples, std::size_t sample_count) {     \
    remove_channel_mean<T>(channel_samples, sample_count);                                                             \
  }                                                                                                                    \
  bool invert_lower_triangular(ksj::array::MatrixView<const T> lower, ksj::array::MatrixView<T> output) {              \
    return invert_lower_triangular<T>(lower, output, lower.rows());                                                    \
  }                                                                                                                    \
  PrewhitenCalibrationResult cholesky_prewhiten_calibration(                                                           \
    const std::span<ksj::array::VectorView<T>> channel_samples, ksj::array::MatrixView<T> inverse_cholesky,            \
    const std::span<ksj::array::real_scalar_t<T>> scale_factors, std::size_t sample_count) {                           \
    return cholesky_prewhiten_calibration<T>(channel_samples, inverse_cholesky, scale_factors, sample_count);          \
  }                                                                                                                    \
  bool apply_cholesky_prewhiten(ksj::array::MatrixView<T> inverse_cholesky,                                            \
                                const std::span<ksj::array::VectorView<T>> channel_samples,                            \
                                std::size_t sample_count) {                                                            \
    return apply_cholesky_prewhiten<T>(inverse_cholesky, channel_samples, sample_count);                               \
  }

KSJ_LINALG_EIGEN_WHITENING_WRAPPERS(float)
KSJ_LINALG_EIGEN_WHITENING_WRAPPERS(double)
KSJ_LINALG_EIGEN_WHITENING_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_EIGEN_WHITENING_WRAPPERS(ksj::base::cf64)
KSJ_LINALG_EIGEN_PREWHITEN_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_EIGEN_PREWHITEN_WRAPPERS(ksj::base::cf64)

#undef KSJ_LINALG_EIGEN_WHITENING_WRAPPERS
#undef KSJ_LINALG_EIGEN_PREWHITEN_WRAPPERS

} // namespace ksj::linalg::detail::eigen
