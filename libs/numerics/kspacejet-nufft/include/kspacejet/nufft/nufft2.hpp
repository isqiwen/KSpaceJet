#pragma once

/// Two-dimensional NUFFT planning and execution APIs with explicit trajectory and dense output semantics.

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/nufft/types.hpp"
#include "kspacejet/nufft/workspace.hpp"

#include <complex>

namespace ksj::nufft {

[[nodiscard]] bool bart_backend_available() noexcept;

void nufft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::VectorView<ksj::base::cf32> output,
                    Nufft2Options options = {});

void nufft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::VectorView<ksj::base::cf64> output,
                    Nufft2Options options = {});

void nufft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::VectorView<ksj::base::cf32> output,
                    Nufft2Workspace<float>& workspace, Nufft2Options options = {});

void nufft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::VectorView<ksj::base::cf64> output,
                    Nufft2Workspace<double>& workspace, Nufft2Options options = {});

inline void nufft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf32> image,
                           ksj::array::MatrixView<float> trajectory, ksj::array::VectorView<ksj::base::cf32> output,
                           const Nufft2Options options = {}) {
  nufft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory), output, options);
}

inline void nufft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf64> image,
                           ksj::array::MatrixView<double> trajectory, ksj::array::VectorView<ksj::base::cf64> output,
                           const Nufft2Options options = {}) {
  nufft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory), output, options);
}

inline void nufft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf32> image,
                           ksj::array::MatrixView<float> trajectory, ksj::array::VectorView<ksj::base::cf32> output,
                           Nufft2Workspace<float>& workspace, const Nufft2Options options = {}) {
  nufft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory), output, workspace,
                 options);
}

inline void nufft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf64> image,
                           ksj::array::MatrixView<double> trajectory, ksj::array::VectorView<ksj::base::cf64> output,
                           Nufft2Workspace<double>& workspace, const Nufft2Options options = {}) {
  nufft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory), output, workspace,
                 options);
}

[[nodiscard]] inline ksj::array::PooledVector<ksj::base::cf32>
nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
               ksj::array::MatrixView<const float> trajectory, const Nufft2Options options = {}) {
  auto output = ksj::array::PooledVector<ksj::base::cf32>(trajectory.rows());
  nufft2_forward(grid, image, trajectory, output.view(), options);
  return output;
}

[[nodiscard]] inline ksj::array::PooledVector<ksj::base::cf64>
nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
               ksj::array::MatrixView<const double> trajectory, const Nufft2Options options = {}) {
  auto output = ksj::array::PooledVector<ksj::base::cf64>(trajectory.rows());
  nufft2_forward(grid, image, trajectory, output.view(), options);
  return output;
}

[[nodiscard]] inline ksj::array::PooledVector<ksj::base::cf32>
nufft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf32> image,
               ksj::array::MatrixView<float> trajectory, const Nufft2Options options = {}) {
  return nufft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory), options);
}

[[nodiscard]] inline ksj::array::PooledVector<ksj::base::cf64>
nufft2_forward(const Grid2D grid, ksj::array::MatrixView<ksj::base::cf64> image,
               ksj::array::MatrixView<double> trajectory, const Nufft2Options options = {}) {
  return nufft2_forward(grid, ksj::array::as_const_view(image), ksj::array::as_const_view(trajectory), options);
}

void nufft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::MatrixView<ksj::base::cf32> image,
                    Nufft2Options options = {});

void nufft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::MatrixView<ksj::base::cf64> image,
                    Nufft2Options options = {});

void nufft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::MatrixView<ksj::base::cf32> image,
                    Nufft2Workspace<float>& workspace, Nufft2Options options = {});

void nufft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::MatrixView<ksj::base::cf64> image,
                    Nufft2Workspace<double>& workspace, Nufft2Options options = {});

inline void nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf32> samples,
                           ksj::array::MatrixView<float> trajectory, ksj::array::MatrixView<ksj::base::cf32> image,
                           const Nufft2Options options = {}) {
  nufft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory), image, options);
}

inline void nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf64> samples,
                           ksj::array::MatrixView<double> trajectory, ksj::array::MatrixView<ksj::base::cf64> image,
                           const Nufft2Options options = {}) {
  nufft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory), image, options);
}

inline void nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf32> samples,
                           ksj::array::MatrixView<float> trajectory, ksj::array::MatrixView<ksj::base::cf32> image,
                           Nufft2Workspace<float>& workspace, const Nufft2Options options = {}) {
  nufft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory), image, workspace,
                 options);
}

inline void nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf64> samples,
                           ksj::array::MatrixView<double> trajectory, ksj::array::MatrixView<ksj::base::cf64> image,
                           Nufft2Workspace<double>& workspace, const Nufft2Options options = {}) {
  nufft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory), image, workspace,
                 options);
}

[[nodiscard]] inline ksj::array::PooledMatrix<ksj::base::cf32>
nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
               ksj::array::MatrixView<const float> trajectory, const Nufft2Options options = {}) {
  auto image = ksj::array::PooledMatrix<ksj::base::cf32>(grid.rows, grid.cols);
  nufft2_adjoint(grid, samples, trajectory, image.view(), options);
  return image;
}

[[nodiscard]] inline ksj::array::PooledMatrix<ksj::base::cf64>
nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
               ksj::array::MatrixView<const double> trajectory, const Nufft2Options options = {}) {
  auto image = ksj::array::PooledMatrix<ksj::base::cf64>(grid.rows, grid.cols);
  nufft2_adjoint(grid, samples, trajectory, image.view(), options);
  return image;
}

[[nodiscard]] inline ksj::array::PooledMatrix<ksj::base::cf32>
nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf32> samples,
               ksj::array::MatrixView<float> trajectory, const Nufft2Options options = {}) {
  return nufft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory), options);
}

[[nodiscard]] inline ksj::array::PooledMatrix<ksj::base::cf64>
nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<ksj::base::cf64> samples,
               ksj::array::MatrixView<double> trajectory, const Nufft2Options options = {}) {
  return nufft2_adjoint(grid, ksj::array::as_const_view(samples), ksj::array::as_const_view(trajectory), options);
}

template <typename T>
void nufft2_forward(const Grid2D grid, const ksj::array::PooledMatrix<std::complex<T>>& image,
                    const ksj::array::PooledMatrix<T>& trajectory, ksj::array::PooledVector<std::complex<T>>& output,
                    const Nufft2Options options = {}) {
  nufft2_forward(grid, image.view(), trajectory.view(), output.view(), options);
}

template <typename T>
void nufft2_forward(const Grid2D grid, const ksj::array::PooledMatrix<std::complex<T>>& image,
                    const ksj::array::PooledMatrix<T>& trajectory, ksj::array::PooledVector<std::complex<T>>& output,
                    Nufft2Workspace<T>& workspace, const Nufft2Options options = {}) {
  nufft2_forward(grid, image.view(), trajectory.view(), output.view(), workspace, options);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::complex<T>>
nufft2_forward(const Grid2D grid, const ksj::array::PooledMatrix<std::complex<T>>& image,
               const ksj::array::PooledMatrix<T>& trajectory, const Nufft2Options options = {}) {
  auto output = ksj::array::make_pooled_vector<std::complex<T>>(trajectory.rows());
  nufft2_forward(grid, image, trajectory, output, options);
  return output;
}

template <typename T>
void nufft2_adjoint(const Grid2D grid, const ksj::array::PooledVector<std::complex<T>>& samples,
                    const ksj::array::PooledMatrix<T>& trajectory, ksj::array::PooledMatrix<std::complex<T>>& image,
                    const Nufft2Options options = {}) {
  nufft2_adjoint(grid, samples.view(), trajectory.view(), image.view(), options);
}

template <typename T>
void nufft2_adjoint(const Grid2D grid, const ksj::array::PooledVector<std::complex<T>>& samples,
                    const ksj::array::PooledMatrix<T>& trajectory, ksj::array::PooledMatrix<std::complex<T>>& image,
                    Nufft2Workspace<T>& workspace, const Nufft2Options options = {}) {
  nufft2_adjoint(grid, samples.view(), trajectory.view(), image.view(), workspace, options);
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<std::complex<T>>
nufft2_adjoint(const Grid2D grid, const ksj::array::PooledVector<std::complex<T>>& samples,
               const ksj::array::PooledMatrix<T>& trajectory, const Nufft2Options options = {}) {
  auto image = ksj::array::make_pooled_matrix<std::complex<T>>(grid.rows, grid.cols);
  nufft2_adjoint(grid, samples, trajectory, image, options);
  return image;
}

} // namespace ksj::nufft
