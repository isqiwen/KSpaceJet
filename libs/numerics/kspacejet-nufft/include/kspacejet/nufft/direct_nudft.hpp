#pragma once

/// Direct non-uniform discrete Fourier transform reference APIs for correctness and small problems.

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/nufft/types.hpp"

#include <complex>

namespace ksj::nufft {

void direct_nudft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                           ksj::array::MatrixView<const float> trajectory,
                           ksj::array::VectorView<ksj::base::cf32> output);

void direct_nudft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                           ksj::array::MatrixView<const double> trajectory,
                           ksj::array::VectorView<ksj::base::cf64> output);

inline void direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf32> image,
                                  ksj::array::MatrixView<float> trajectory,
                                  ksj::array::VectorView<ksj::base::cf32> output) {
  direct_nudft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory), output);
}

inline void direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf64> image,
                                  ksj::array::MatrixView<double> trajectory,
                                  ksj::array::VectorView<ksj::base::cf64> output) {
  direct_nudft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory), output);
}

[[nodiscard]] inline ksj::array::PooledVector<ksj::base::cf32>
direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                      ksj::array::MatrixView<const float> trajectory) {
  auto output = ksj::array::PooledVector<ksj::base::cf32>(trajectory.rows());
  direct_nudft2_forward(grid, image, trajectory, output.view());
  return output;
}

[[nodiscard]] inline ksj::array::PooledVector<ksj::base::cf64>
direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                      ksj::array::MatrixView<const double> trajectory) {
  auto output = ksj::array::PooledVector<ksj::base::cf64>(trajectory.rows());
  direct_nudft2_forward(grid, image, trajectory, output.view());
  return output;
}

[[nodiscard]] inline ksj::array::PooledVector<ksj::base::cf32>
direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf32> image,
                      ksj::array::MatrixView<float> trajectory) {
  return direct_nudft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory));
}

[[nodiscard]] inline ksj::array::PooledVector<ksj::base::cf64>
direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf64> image,
                      ksj::array::MatrixView<double> trajectory) {
  return direct_nudft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory));
}

void direct_nudft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                           ksj::array::MatrixView<const float> trajectory,
                           ksj::array::MatrixView<ksj::base::cf32> image);

void direct_nudft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                           ksj::array::MatrixView<const double> trajectory,
                           ksj::array::MatrixView<ksj::base::cf64> image);

inline void direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf32> samples,
                                  ksj::array::MatrixView<float> trajectory,
                                  ksj::array::MatrixView<ksj::base::cf32> image) {
  direct_nudft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory), image);
}

inline void direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf64> samples,
                                  ksj::array::MatrixView<double> trajectory,
                                  ksj::array::MatrixView<ksj::base::cf64> image) {
  direct_nudft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory), image);
}

[[nodiscard]] inline ksj::array::PooledMatrix<ksj::base::cf32>
direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                      ksj::array::MatrixView<const float> trajectory) {
  auto image = ksj::array::PooledMatrix<ksj::base::cf32>(grid.rows, grid.cols);
  direct_nudft2_adjoint(grid, samples, trajectory, image.view());
  return image;
}

[[nodiscard]] inline ksj::array::PooledMatrix<ksj::base::cf64>
direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                      ksj::array::MatrixView<const double> trajectory) {
  auto image = ksj::array::PooledMatrix<ksj::base::cf64>(grid.rows, grid.cols);
  direct_nudft2_adjoint(grid, samples, trajectory, image.view());
  return image;
}

[[nodiscard]] inline ksj::array::PooledMatrix<ksj::base::cf32>
direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf32> samples,
                      ksj::array::MatrixView<float> trajectory) {
  return direct_nudft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory));
}

[[nodiscard]] inline ksj::array::PooledMatrix<ksj::base::cf64>
direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf64> samples,
                      ksj::array::MatrixView<double> trajectory) {
  return direct_nudft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory));
}

template <typename T>
void direct_nudft2_forward(const Grid2D grid, const ksj::array::PooledMatrix<std::complex<T>>& image,
                           const ksj::array::PooledMatrix<T>& trajectory,
                           ksj::array::PooledVector<std::complex<T>>& output) {
  direct_nudft2_forward(grid, image.view(), trajectory.view(), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::complex<T>>
direct_nudft2_forward(const Grid2D grid, const ksj::array::PooledMatrix<std::complex<T>>& image,
                      const ksj::array::PooledMatrix<T>& trajectory) {
  auto output = ksj::array::make_pooled_vector<std::complex<T>>(trajectory.rows());
  direct_nudft2_forward(grid, image, trajectory, output);
  return output;
}

template <typename T>
void direct_nudft2_adjoint(const Grid2D grid, const ksj::array::PooledVector<std::complex<T>>& samples,
                           const ksj::array::PooledMatrix<T>& trajectory,
                           ksj::array::PooledMatrix<std::complex<T>>& image) {
  direct_nudft2_adjoint(grid, samples.view(), trajectory.view(), image.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<std::complex<T>>
direct_nudft2_adjoint(const Grid2D grid, const ksj::array::PooledVector<std::complex<T>>& samples,
                      const ksj::array::PooledMatrix<T>& trajectory) {
  auto image = ksj::array::make_pooled_matrix<std::complex<T>>(grid.rows, grid.cols);
  direct_nudft2_adjoint(grid, samples, trajectory, image);
  return image;
}

} // namespace ksj::nufft
