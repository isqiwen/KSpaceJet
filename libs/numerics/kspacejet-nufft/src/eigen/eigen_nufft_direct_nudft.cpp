#include "kspacejet/base/types.hpp"
#include "kspacejet/nufft/detail/eigen/eigen_nufft_direct_nudft.hpp"

#include <cmath>
#include <complex>
#include <stdexcept>

namespace ksj::nufft::detail::eigen {
namespace {

template <typename T>
void validate_forward_inputs(const Grid2D grid, ksj::array::MatrixView<const std::complex<T>> image,
                             ksj::array::MatrixView<const T> trajectory,
                             ksj::array::VectorView<std::complex<T>> output) {
  if (grid.rows == 0U || grid.cols == 0U) {
    throw std::invalid_argument("NUDFT grid must be non-empty");
  }
  if (image.rows() != grid.rows || image.cols() != grid.cols) {
    throw std::invalid_argument("NUDFT image dimensions do not match the grid");
  }
  if (trajectory.cols() != 2U) {
    throw std::invalid_argument("NUDFT 2D trajectory must have two columns");
  }
  if (trajectory.rows() != output.size()) {
    throw std::invalid_argument("NUDFT output size must match trajectory sample count");
  }
}

template <typename T>
void validate_adjoint_inputs(const Grid2D grid, ksj::array::VectorView<const std::complex<T>> samples,
                             ksj::array::MatrixView<const T> trajectory,
                             ksj::array::MatrixView<std::complex<T>> image) {
  if (grid.rows == 0U || grid.cols == 0U) {
    throw std::invalid_argument("NUDFT grid must be non-empty");
  }
  if (image.rows() != grid.rows || image.cols() != grid.cols) {
    throw std::invalid_argument("NUDFT image dimensions do not match the grid");
  }
  if (trajectory.cols() != 2U) {
    throw std::invalid_argument("NUDFT 2D trajectory must have two columns");
  }
  if (trajectory.rows() != samples.size()) {
    throw std::invalid_argument("NUDFT sample count must match trajectory sample count");
  }
}

template <typename T> [[nodiscard]] std::complex<T> complex_exponential(const T phase) {
  return {std::cos(phase), std::sin(phase)};
}

template <typename T>
void direct_forward_impl(const Grid2D grid, ksj::array::MatrixView<const std::complex<T>> image,
                         ksj::array::MatrixView<const T> trajectory, ksj::array::VectorView<std::complex<T>> output) {
  validate_forward_inputs(grid, image, trajectory, output);

  const auto row_origin = static_cast<T>(grid.row_origin);
  const auto col_origin = static_cast<T>(grid.col_origin);
  for (std::size_t sample = 0; sample < trajectory.rows(); ++sample) {
    const auto row_frequency = trajectory(sample, 0U);
    const auto col_frequency = trajectory(sample, 1U);
    std::complex<T> sum{};

    for (std::size_t row = 0; row < grid.rows; ++row) {
      const auto row_position = static_cast<T>(row) - row_origin;
      for (std::size_t col = 0; col < grid.cols; ++col) {
        const auto col_position = static_cast<T>(col) - col_origin;
        const auto phase = -(row_frequency * row_position + col_frequency * col_position);
        sum += image(row, col) * complex_exponential(phase);
      }
    }

    output(sample) = sum;
  }
}

template <typename T>
void direct_adjoint_impl(const Grid2D grid, ksj::array::VectorView<const std::complex<T>> samples,
                         ksj::array::MatrixView<const T> trajectory, ksj::array::MatrixView<std::complex<T>> image) {
  validate_adjoint_inputs(grid, samples, trajectory, image);

  const auto row_origin = static_cast<T>(grid.row_origin);
  const auto col_origin = static_cast<T>(grid.col_origin);
  for (std::size_t row = 0; row < grid.rows; ++row) {
    const auto row_position = static_cast<T>(row) - row_origin;
    for (std::size_t col = 0; col < grid.cols; ++col) {
      const auto col_position = static_cast<T>(col) - col_origin;
      std::complex<T> sum{};

      for (std::size_t sample = 0; sample < trajectory.rows(); ++sample) {
        const auto phase = trajectory(sample, 0U) * row_position + trajectory(sample, 1U) * col_position;
        sum += samples(sample) * complex_exponential(phase);
      }

      image(row, col) = sum;
    }
  }
}

} // namespace

void direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                           ksj::array::MatrixView<const float> trajectory,
                           ksj::array::VectorView<ksj::base::cf32> output) {
  direct_forward_impl(grid, image, trajectory, output);
}

void direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                           ksj::array::MatrixView<const double> trajectory,
                           ksj::array::VectorView<ksj::base::cf64> output) {
  direct_forward_impl(grid, image, trajectory, output);
}

void direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                           ksj::array::MatrixView<const float> trajectory,
                           ksj::array::MatrixView<ksj::base::cf32> image) {
  direct_adjoint_impl(grid, samples, trajectory, image);
}

void direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                           ksj::array::MatrixView<const double> trajectory,
                           ksj::array::MatrixView<ksj::base::cf64> image) {
  direct_adjoint_impl(grid, samples, trajectory, image);
}

} // namespace ksj::nufft::detail::eigen
