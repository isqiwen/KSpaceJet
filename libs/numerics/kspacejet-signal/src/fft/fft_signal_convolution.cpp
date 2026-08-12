#include "kspacejet/signal/detail/fft/fft_signal_convolution.hpp"

#include "kspacejet/fft/fft.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>

namespace ksj::signal::detail::fft {
namespace {

[[nodiscard]] bool is_smooth_fft_extent(std::size_t value) noexcept {
  constexpr std::size_t factors[] = {2U, 3U, 5U};
  for (const auto factor : factors) {
    while (value > 1U && value % factor == 0U) {
      value /= factor;
    }
  }
  return value == 1U;
}

[[nodiscard]] std::size_t next_smooth_fft_extent(const std::size_t minimum) noexcept {
  auto extent = minimum;
  while (!is_smooth_fft_extent(extent)) {
    ++extent;
  }
  return extent;
}

template <typename T>
void correlate2d_same_impl(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                           ksj::array::ImageView<T> output) {
  const auto padded_rows = next_smooth_fft_extent(input.rows() + kernel.rows() - 1U);
  const auto padded_cols = next_smooth_fft_extent(input.cols() + kernel.cols() - 1U);
  auto padded_input = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto padded_kernel = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto input_spectrum = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto kernel_spectrum = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto product_spectrum = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto spatial = ksj::array::make_pooled_matrix<std::complex<T>>(padded_rows, padded_cols);
  auto forward_plan =
    ksj::fft::Fft2Plan<T>(padded_rows, padded_cols, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
  auto inverse_plan =
    ksj::fft::Fft2Plan<T>(padded_rows, padded_cols, ksj::fft::Direction::inverse, ksj::fft::Normalization::inverse);

  std::fill(padded_input.data(), padded_input.data() + padded_input.size(), std::complex<T>{});
  std::fill(padded_kernel.data(), padded_kernel.data() + padded_kernel.size(), std::complex<T>{});

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      padded_input(row, col) = static_cast<std::complex<T>>(input(row, col));
    }
  }

  for (std::size_t row = 0; row < kernel.rows(); ++row) {
    for (std::size_t col = 0; col < kernel.cols(); ++col) {
      padded_kernel(row, col) =
        static_cast<std::complex<T>>(kernel(kernel.rows() - 1U - row, kernel.cols() - 1U - col));
    }
  }

  forward_plan.execute(padded_input, input_spectrum);
  forward_plan.execute(padded_kernel, kernel_spectrum);

  for (std::size_t index = 0; index < product_spectrum.size(); ++index) {
    product_spectrum.data()[index] = input_spectrum.data()[index] * kernel_spectrum.data()[index];
  }

  inverse_plan.execute(product_spectrum, spatial);

  const auto row_offset = kernel.rows() - 1U - kernel.rows() / 2U;
  const auto col_offset = kernel.cols() - 1U - kernel.cols() / 2U;
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = spatial(row + row_offset, col + col_offset).real();
    }
  }
}

} // namespace

void correlate2d_same(ksj::array::ImageView<const float> input, ksj::array::ImageView<const float> kernel,
                      ksj::array::ImageView<float> output) {
  correlate2d_same_impl(input, kernel, output);
}

void correlate2d_same(ksj::array::ImageView<const double> input, ksj::array::ImageView<const double> kernel,
                      ksj::array::ImageView<double> output) {
  correlate2d_same_impl(input, kernel, output);
}

} // namespace ksj::signal::detail::fft
