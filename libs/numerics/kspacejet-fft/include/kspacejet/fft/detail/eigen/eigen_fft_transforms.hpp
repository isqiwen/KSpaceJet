#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/fft/types.hpp"

#include <complex>
#include <stdexcept>
#include <type_traits>

namespace ksj::fft::detail::eigen {

void fft_1d(ksj::array::VectorView<const std::complex<float>> input, ksj::array::VectorView<std::complex<float>> output,
            Direction direction, Normalization normalization);
void fft_1d(ksj::array::VectorView<const std::complex<double>> input,
            ksj::array::VectorView<std::complex<double>> output, Direction direction, Normalization normalization);

template <typename T>
void fft_1d(ksj::array::VectorView<const std::complex<T>>, ksj::array::VectorView<std::complex<T>>, Direction,
            Normalization) {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                "Eigen FFT backend supports float and double complex data");
}

template <typename T>
void fft_1d(const ksj::array::PooledVector<std::complex<T>>& input, ksj::array::PooledVector<std::complex<T>>& output,
            const Direction direction, const Normalization normalization) {
  fft_1d(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

void fft_2d(ksj::array::MatrixView<const std::complex<float>> input, ksj::array::MatrixView<std::complex<float>> output,
            Direction direction, Normalization normalization);
void fft_2d(ksj::array::MatrixView<const std::complex<double>> input,
            ksj::array::MatrixView<std::complex<double>> output, Direction direction, Normalization normalization);

template <typename T>
void fft_2d(ksj::array::MatrixView<const std::complex<T>>, ksj::array::MatrixView<std::complex<T>>, Direction,
            Normalization) {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                "Eigen FFT backend supports float and double complex data");
}

template <typename T>
void fft_2d(const ksj::array::PooledMatrix<std::complex<T>>& input, ksj::array::PooledMatrix<std::complex<T>>& output,
            const Direction direction, const Normalization normalization) {
  fft_2d(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

void fft_2d_batch(const ksj::array::PooledCube<std::complex<float>>& input,
                  ksj::array::PooledCube<std::complex<float>>& output, Direction direction,
                  Normalization normalization);
void fft_2d_batch(const ksj::array::PooledCube<std::complex<double>>& input,
                  ksj::array::PooledCube<std::complex<double>>& output, Direction direction,
                  Normalization normalization);

template <typename T>
void fft_2d_batch(const ksj::array::PooledCube<std::complex<T>>&, ksj::array::PooledCube<std::complex<T>>&, Direction,
                  Normalization) {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                "Eigen FFT backend supports float and double complex data");
}

void fft_3d(ksj::array::CubeView<const std::complex<float>> input, ksj::array::CubeView<std::complex<float>> output,
            Direction direction, Normalization normalization);
void fft_3d(ksj::array::CubeView<const std::complex<double>> input, ksj::array::CubeView<std::complex<double>> output,
            Direction direction, Normalization normalization);

template <typename T>
void fft_3d(ksj::array::CubeView<const std::complex<T>>, ksj::array::CubeView<std::complex<T>>, Direction,
            Normalization) {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                "Eigen FFT backend supports float and double complex data");
}

template <typename T>
void fft_3d(const ksj::array::PooledCube<std::complex<T>>& input, ksj::array::PooledCube<std::complex<T>>& output,
            const Direction direction, const Normalization normalization) {
  fft_3d(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

void fft_3d_batch(const ksj::array::PooledArray4D<std::complex<float>>& input,
                  ksj::array::PooledArray4D<std::complex<float>>& output, Direction direction,
                  Normalization normalization);
void fft_3d_batch(const ksj::array::PooledArray4D<std::complex<double>>& input,
                  ksj::array::PooledArray4D<std::complex<double>>& output, Direction direction,
                  Normalization normalization);

template <typename T>
void fft_3d_batch(const ksj::array::PooledArray4D<std::complex<T>>&, ksj::array::PooledArray4D<std::complex<T>>&,
                  Direction, Normalization) {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                "Eigen FFT backend supports float and double complex data");
}

} // namespace ksj::fft::detail::eigen
