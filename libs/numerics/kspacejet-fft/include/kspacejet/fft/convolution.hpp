#pragma once

/// FFT-backed convolution APIs and reusable plans for large dense signals and images.

#include "kspacejet/fft/transforms.hpp"
#include "kspacejet/fft/detail/fft_algorithms.hpp"

#include <stdexcept>

namespace ksj::fft {

template <typename T>
void convolve2d_full_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output) {
  detail::algorithms::convolve2d_full_fft(input, kernel, output, false);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> convolve2d_full_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel) {
  detail::algorithms::validate_fft_convolution_inputs(input, kernel);
  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows() + kernel.rows() - 1U,
                                                                input.cols() + kernel.cols() - 1U);
  convolve2d_full_fft(input, kernel, output);
  return output;
}

template <typename T>
void convolve2d_same_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output) {
  detail::algorithms::convolve2d_same_fft(input, kernel, output, false);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> convolve2d_same_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel) {
  detail::algorithms::validate_fft_convolution_inputs(input, kernel);
  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
  convolve2d_same_fft(input, kernel, output);
  return output;
}

template <typename T>
void convolve2d_valid_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output) {
  detail::algorithms::convolve2d_valid_fft(input, kernel, output, false);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> convolve2d_valid_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel) {
  detail::algorithms::validate_fft_convolution_inputs(input, kernel);
  if (input.rows() < kernel.rows() || input.cols() < kernel.cols()) {
    throw std::invalid_argument("fft convolve2d_valid input dimensions must be >= kernel dimensions");
  }

  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows() - kernel.rows() + 1U,
                                                                input.cols() - kernel.cols() + 1U);
  convolve2d_valid_fft(input, kernel, output);
  return output;
}

template <typename T>
void correlate2d_full_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output) {
  detail::algorithms::convolve2d_full_fft(input, kernel, output, true);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> correlate2d_full_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel) {
  detail::algorithms::validate_fft_convolution_inputs(input, kernel);
  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows() + kernel.rows() - 1U,
                                                                input.cols() + kernel.cols() - 1U);
  correlate2d_full_fft(input, kernel, output);
  return output;
}

template <typename T>
void correlate2d_same_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output) {
  detail::algorithms::convolve2d_same_fft(input, kernel, output, true);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> correlate2d_same_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel) {
  detail::algorithms::validate_fft_convolution_inputs(input, kernel);
  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
  correlate2d_same_fft(input, kernel, output);
  return output;
}

template <typename T>
void correlate2d_valid_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output) {
  detail::algorithms::convolve2d_valid_fft(input, kernel, output, true);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> correlate2d_valid_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel) {
  detail::algorithms::validate_fft_convolution_inputs(input, kernel);
  if (input.rows() < kernel.rows() || input.cols() < kernel.cols()) {
    throw std::invalid_argument("fft correlate2d_valid input dimensions must be >= kernel dimensions");
  }

  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows() - kernel.rows() + 1U,
                                                                input.cols() - kernel.cols() + 1U);
  correlate2d_valid_fft(input, kernel, output);
  return output;
}

} // namespace ksj::fft
