#pragma once

/// Batched and segmented Fourier transforms over independently transformed dense regions.

#include "kspacejet/fft/transforms.hpp"
#include "kspacejet/fft/detail/fft_algorithms.hpp"

#include <cstddef>

namespace ksj::fft {

template <typename T> void fftshift(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output);

template <typename T> void ifftshift(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output);

template <typename T>
void fft_segmented(const ComplexVector<T>& input, ComplexVector<T>& output, const std::size_t segments,
                   const Direction direction = Direction::forward,
                   const Normalization normalization = Normalization::none, const bool preshift = false,
                   const bool postshift = false) {
  detail::algorithms::fft_segmented_1d(input, output, segments, direction, normalization, preshift, postshift);
}

template <typename T>
void ifft_segmented(const ComplexVector<T>& input, ComplexVector<T>& output, const std::size_t segments,
                    const Normalization normalization = Normalization::inverse, const bool preshift = false,
                    const bool postshift = false) {
  fft_segmented(input, output, segments, Direction::inverse, normalization, preshift, postshift);
}

template <typename T>
[[nodiscard]] ComplexVector<T> fft_segmented(const ComplexVector<T>& input, const std::size_t segments,
                                             const Direction direction = Direction::forward,
                                             const Normalization normalization = Normalization::none,
                                             const bool preshift = false, const bool postshift = false) {
  auto output = ksj::array::make_pooled_vector<std::complex<T>>(input.size());
  fft_segmented(input, output, segments, direction, normalization, preshift, postshift);
  return output;
}

template <typename T>
[[nodiscard]] ComplexVector<T> ifft_segmented(const ComplexVector<T>& input, const std::size_t segments,
                                              const Normalization normalization = Normalization::inverse,
                                              const bool preshift = false, const bool postshift = false) {
  return fft_segmented(input, segments, Direction::inverse, normalization, preshift, postshift);
}

template <typename T>
void fft_segmented(const ComplexMatrix<T>& input, ComplexMatrix<T>& output, const ksj::array::Dim dim,
                   const std::size_t segments, const Direction direction = Direction::forward,
                   const Normalization normalization = Normalization::none, const bool preshift = false,
                   const bool postshift = false) {
  detail::algorithms::fft_segmented_2d(input, output, dim, segments, direction, normalization, preshift, postshift);
}

template <typename T>
void ifft_segmented(const ComplexMatrix<T>& input, ComplexMatrix<T>& output, const ksj::array::Dim dim,
                    const std::size_t segments, const Normalization normalization = Normalization::inverse,
                    const bool preshift = false, const bool postshift = false) {
  fft_segmented(input, output, dim, segments, Direction::inverse, normalization, preshift, postshift);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> fft_segmented(const ComplexMatrix<T>& input, const ksj::array::Dim dim,
                                             const std::size_t segments, const Direction direction = Direction::forward,
                                             const Normalization normalization = Normalization::none,
                                             const bool preshift = false, const bool postshift = false) {
  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
  fft_segmented(input, output, dim, segments, direction, normalization, preshift, postshift);
  return output;
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> ifft_segmented(const ComplexMatrix<T>& input, const ksj::array::Dim dim,
                                              const std::size_t segments,
                                              const Normalization normalization = Normalization::inverse,
                                              const bool preshift = false, const bool postshift = false) {
  return fft_segmented(input, dim, segments, Direction::inverse, normalization, preshift, postshift);
}

} // namespace ksj::fft
