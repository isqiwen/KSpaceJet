#pragma once

/// FFT direction, normalization, and plan-related public value types.

#include "kspacejet/array/array.hpp"
#include "kspacejet/fft/fft_constants.hpp"

#include <complex>
#include <cstddef>

namespace ksj::fft {

enum class Direction {
  forward,
  inverse,
};

enum class Normalization {
  none,
  forward,
  inverse,
  orthonormal,
};

[[nodiscard]] constexpr ksj::array::Dim dim_from_ksj_fft_mode(const int mode) noexcept {
  return (mode == kRowFft) ? ksj::array::Dim::dim1 : ksj::array::Dim::dim0;
}

template <typename T> using ComplexVector = ksj::array::PooledVector<std::complex<T>>;

template <typename T> using ComplexMatrix = ksj::array::PooledMatrix<std::complex<T>>;

template <typename T> using ComplexCube = ksj::array::PooledCube<std::complex<T>>;

template <typename T> using ComplexArray4D = ksj::array::PooledArray4D<std::complex<T>>;

} // namespace ksj::fft
