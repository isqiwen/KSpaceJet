#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/fft/detail/fft_shift_algorithms.hpp"
#include "kspacejet/fft/types.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace ksj::fft::detail::algorithms {

template <typename Storage> void copy_storage(const Storage& source, Storage& destination) {
  std::copy(source.data(), source.data() + source.size(), destination.data());
}

[[nodiscard]] inline bool is_smooth_fft_extent(std::size_t value) noexcept {
  constexpr std::size_t factors[] = {2U, 3U, 5U};
  for (const auto factor : factors) {
    while (value > 1U && value % factor == 0U) {
      value /= factor;
    }
  }
  return value == 1U;
}

[[nodiscard]] inline std::size_t next_smooth_fft_extent(const std::size_t minimum) noexcept {
  auto extent = minimum;
  while (!is_smooth_fft_extent(extent)) {
    ++extent;
  }
  return extent;
}

template <typename T>
void validate_fft_convolution_inputs(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel) {
  if (input.empty() || kernel.empty()) {
    throw std::invalid_argument("fft convolve2d/correlate2d input and kernel must not be empty");
  }
}

template <typename T>
void convolve2d_fft_window(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output,
                           const bool correlate, const std::size_t row_offset, const std::size_t col_offset) {
  validate_fft_convolution_inputs(input, kernel);
  const auto full_rows = input.rows() + kernel.rows() - 1U;
  const auto full_cols = input.cols() + kernel.cols() - 1U;
  if (row_offset > full_rows || col_offset > full_cols || output.rows() > full_rows - row_offset ||
      output.cols() > full_cols - col_offset) {
    throw std::invalid_argument("fft convolve2d/correlate2d output window exceeds full result");
  }

  const auto padded_rows = next_smooth_fft_extent(full_rows);
  const auto padded_cols = next_smooth_fft_extent(full_cols);
  auto padded_input = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto padded_kernel = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto input_spectrum = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto kernel_spectrum = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto product_spectrum = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto spatial = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto forward_plan = Fft2Plan<T>(padded_rows, padded_cols, Direction::forward, Normalization::none);
  auto inverse_plan = Fft2Plan<T>(padded_rows, padded_cols, Direction::inverse, Normalization::inverse);

  std::fill(padded_input.data(), padded_input.data() + padded_input.size(), std::complex<T>{});
  std::fill(padded_kernel.data(), padded_kernel.data() + padded_kernel.size(), std::complex<T>{});

  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      padded_input(row, col) = input(row, col);
    }
  }

  for (std::size_t col = 0; col < kernel.cols(); ++col) {
    for (std::size_t row = 0; row < kernel.rows(); ++row) {
      if (correlate) {
        padded_kernel(row, col) = std::conj(kernel(kernel.rows() - 1U - row, kernel.cols() - 1U - col));
      } else {
        padded_kernel(row, col) = kernel(row, col);
      }
    }
  }

  forward_plan.execute(padded_input, input_spectrum);
  forward_plan.execute(padded_kernel, kernel_spectrum);
  for (std::size_t index = 0; index < product_spectrum.size(); ++index) {
    product_spectrum.data()[index] = input_spectrum.data()[index] * kernel_spectrum.data()[index];
  }
  inverse_plan.execute(product_spectrum, spatial);

  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      output(row, col) = spatial(row + row_offset, col + col_offset);
    }
  }
}

template <typename T>
void convolve2d_full_fft_impl(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output,
                              const bool correlate) {
  validate_fft_convolution_inputs(input, kernel);
  const auto full_rows = input.rows() + kernel.rows() - 1U;
  const auto full_cols = input.cols() + kernel.cols() - 1U;
  if (output.rows() != full_rows || output.cols() != full_cols) {
    throw std::invalid_argument("fft convolve2d/correlate2d output dimension mismatch");
  }

  convolve2d_fft_window(input, kernel, output, correlate, 0U, 0U);
}

template <typename T>
void convolve2d_same_fft_impl(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output,
                              const bool correlate) {
  validate_fft_convolution_inputs(input, kernel);
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("fft convolve2d/correlate2d same output dimension mismatch");
  }

  const auto row_offset = (kernel.rows() - 1U) / 2U;
  const auto col_offset = (kernel.cols() - 1U) / 2U;
  convolve2d_fft_window(input, kernel, output, correlate, row_offset, col_offset);
}

template <typename T>
void convolve2d_valid_fft_impl(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output,
                               const bool correlate) {
  validate_fft_convolution_inputs(input, kernel);
  if (input.rows() < kernel.rows() || input.cols() < kernel.cols()) {
    throw std::invalid_argument("fft convolve2d/correlate2d valid requires input dimensions >= kernel dimensions");
  }

  const auto valid_rows = input.rows() - kernel.rows() + 1U;
  const auto valid_cols = input.cols() - kernel.cols() + 1U;
  if (output.rows() != valid_rows || output.cols() != valid_cols) {
    throw std::invalid_argument("fft convolve2d/correlate2d valid output dimension mismatch");
  }

  convolve2d_fft_window(input, kernel, output, correlate, kernel.rows() - 1U, kernel.cols() - 1U);
}

template <typename T>
void convolve2d_full_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output,
                         const bool correlate) {
  if (!output.empty() && (input.data() == output.data() || kernel.data() == output.data())) {
    auto temp = ksj::array::make_pooled_matrix<std::complex<T>>(output.rows(), output.cols());
    convolve2d_full_fft_impl(input, kernel, temp, correlate);
    std::copy(temp.data(), temp.data() + temp.size(), output.data());
    return;
  }

  convolve2d_full_fft_impl(input, kernel, output, correlate);
}

template <typename T>
void convolve2d_same_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output,
                         const bool correlate) {
  if (!output.empty() && (input.data() == output.data() || kernel.data() == output.data())) {
    auto temp = ksj::array::make_pooled_matrix<std::complex<T>>(output.rows(), output.cols());
    convolve2d_same_fft_impl(input, kernel, temp, correlate);
    std::copy(temp.data(), temp.data() + temp.size(), output.data());
    return;
  }

  convolve2d_same_fft_impl(input, kernel, output, correlate);
}

template <typename T>
void convolve2d_valid_fft(const ComplexMatrix<T>& input, const ComplexMatrix<T>& kernel, ComplexMatrix<T>& output,
                          const bool correlate) {
  if (!output.empty() && (input.data() == output.data() || kernel.data() == output.data())) {
    auto temp = ksj::array::make_pooled_matrix<std::complex<T>>(output.rows(), output.cols());
    convolve2d_valid_fft_impl(input, kernel, temp, correlate);
    std::copy(temp.data(), temp.data() + temp.size(), output.data());
    return;
  }

  convolve2d_valid_fft_impl(input, kernel, output, correlate);
}

inline void validate_segment_count(const std::size_t size, const std::size_t segments, const char* operation_name) {
  if (segments == 0U) {
    throw std::invalid_argument(std::string(operation_name) + " segment count must be positive");
  }
  if (size != 0U && size % segments != 0U) {
    throw std::invalid_argument(std::string(operation_name) + " input extent must be divisible by segment count");
  }
}

template <typename T>
void fft_segmented_1d(const ComplexVector<T>& input, ComplexVector<T>& output, const std::size_t segments,
                      const Direction direction, const Normalization normalization, const bool preshift,
                      const bool postshift) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("fft_segmented output dimension mismatch");
  }
  validate_segment_count(input.size(), segments, "fft_segmented");
  if (input.empty()) {
    return;
  }

  const auto segment_size = input.size() / segments;
  auto segment_input = ksj::array::make_pooled_vector<std::complex<T>>(segment_size);
  auto segment_output = ksj::array::make_pooled_vector<std::complex<T>>(segment_size);
  auto segment_scratch =
    (preshift || postshift) ? ksj::array::make_pooled_vector<std::complex<T>>(segment_size) : ComplexVector<T>{};
  auto plan = Fft1Plan<T>(segment_size, direction, normalization);
  for (std::size_t segment = 0; segment < segments; ++segment) {
    const auto offset = segment * segment_size;
    for (std::size_t index = 0; index < segment_size; ++index) {
      segment_input(index) = input(offset + index);
    }
    const auto* transform_input = &segment_input;
    if (preshift) {
      ifftshift(segment_input, segment_scratch);
      transform_input = &segment_scratch;
    }
    plan.execute(*transform_input, segment_output);
    const auto* result = &segment_output;
    if (postshift) {
      fftshift(segment_output, segment_scratch);
      result = &segment_scratch;
    }
    for (std::size_t index = 0; index < segment_size; ++index) {
      output(offset + index) = (*result)(index);
    }
  }
}

template <typename T>
void fft_segmented_2d(const ComplexMatrix<T>& input, ComplexMatrix<T>& output, const ksj::array::Dim dim,
                      const std::size_t segments, const Direction direction, const Normalization normalization,
                      const bool preshift, const bool postshift) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("fft_segmented output dimension mismatch");
  }
  const auto extent = [dim, &input]() -> std::size_t {
    switch (dim) {
      case ksj::array::Dim::dim0:
        return input.rows();
      case ksj::array::Dim::dim1:
        return input.cols();
      default:
        throw std::invalid_argument("fft_segmented supports only matrix dim0 or dim1");
    }
  }();
  validate_segment_count(extent, segments, "fft_segmented");
  if (input.empty()) {
    return;
  }

  const auto segment_size = extent / segments;
  auto segment_input = ksj::array::make_pooled_vector<std::complex<T>>(segment_size);
  auto segment_output = ksj::array::make_pooled_vector<std::complex<T>>(segment_size);
  auto segment_scratch =
    (preshift || postshift) ? ksj::array::make_pooled_vector<std::complex<T>>(segment_size) : ComplexVector<T>{};
  auto plan = Fft1Plan<T>(segment_size, direction, normalization);
  if (dim == ksj::array::Dim::dim1) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      for (std::size_t segment = 0; segment < segments; ++segment) {
        const auto col_offset = segment * segment_size;
        for (std::size_t col = 0; col < segment_size; ++col) {
          segment_input(col) = input(row, col_offset + col);
        }
        const auto* transform_input = &segment_input;
        if (preshift) {
          ifftshift(segment_input, segment_scratch);
          transform_input = &segment_scratch;
        }
        plan.execute(*transform_input, segment_output);
        const auto* result = &segment_output;
        if (postshift) {
          fftshift(segment_output, segment_scratch);
          result = &segment_scratch;
        }
        for (std::size_t col = 0; col < segment_size; ++col) {
          output(row, col_offset + col) = (*result)(col);
        }
      }
    }
    return;
  }

  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t segment = 0; segment < segments; ++segment) {
      const auto row_offset = segment * segment_size;
      for (std::size_t row = 0; row < segment_size; ++row) {
        segment_input(row) = input(row_offset + row, col);
      }
      const auto* transform_input = &segment_input;
      if (preshift) {
        ifftshift(segment_input, segment_scratch);
        transform_input = &segment_scratch;
      }
      plan.execute(*transform_input, segment_output);
      const auto* result = &segment_output;
      if (postshift) {
        fftshift(segment_output, segment_scratch);
        result = &segment_scratch;
      }
      for (std::size_t row = 0; row < segment_size; ++row) {
        output(row_offset + row, col) = (*result)(row);
      }
    }
  }
}

} // namespace ksj::fft::detail::algorithms
