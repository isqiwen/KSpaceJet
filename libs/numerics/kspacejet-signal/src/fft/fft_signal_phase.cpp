#include "kspacejet/signal/detail/fft/fft_signal_phase.hpp"

#include "kspacejet/fft/fft.hpp"
#include "kspacejet/special/special_functions.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <type_traits>

namespace ksj::signal::detail::fft {
namespace {

template <typename T>
void copy_real_image_to_complex_matrix(ksj::array::ImageView<const T> input,
                                       ksj::array::MatrixView<std::complex<T>> output) {
  for (std::size_t index = 0; index < input.size(); ++index) {
    output[index] = {input[index], T{}};
  }
}

template <typename T>
void unwrap_phase_laplacian_2d_impl(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) {
  if (input.empty()) {
    return;
  }

  using complex_type = std::complex<T>;
  const auto rows = input.rows();
  const auto cols = input.cols();
  auto laplacian_kernel = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto kernel_spectrum = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto inverse_kernel_spectrum = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto sin_phase = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto cos_phase = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto trig_scratch = ksj::array::make_pooled_image<T>(rows, cols);
  auto sin_spectrum = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto cos_spectrum = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto product_spectrum = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto laplacian_sin = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto laplacian_cos = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto rhs = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto rhs_spectrum = ksj::array::make_pooled_matrix<complex_type>(rows, cols);
  auto phase = ksj::array::make_pooled_matrix<complex_type>(rows, cols);

  std::fill(laplacian_kernel.data(), laplacian_kernel.data() + laplacian_kernel.size(), complex_type{});
  laplacian_kernel(0, 0) = complex_type{};
  if (rows > 1U) {
    laplacian_kernel(0U, 0U) -= complex_type{T{2}, T{}};
    laplacian_kernel(1U, 0U) += complex_type{T{1}, T{}};
    laplacian_kernel(rows - 1U, 0U) += complex_type{T{1}, T{}};
  }
  if (cols > 1U) {
    laplacian_kernel(0U, 0U) -= complex_type{T{2}, T{}};
    laplacian_kernel(0U, 1U) += complex_type{T{1}, T{}};
    laplacian_kernel(0U, cols - 1U) += complex_type{T{1}, T{}};
  }

  auto forward_plan = ksj::fft::Fft2Plan<T>(rows, cols, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
  auto inverse_plan = ksj::fft::Fft2Plan<T>(rows, cols, ksj::fft::Direction::inverse, ksj::fft::Normalization::inverse);

  forward_plan.execute(laplacian_kernel, kernel_spectrum);
  const auto epsilon = static_cast<T>(std::is_same_v<T, float> ? 1.0e-6 : 1.0e-10);
  for (std::size_t index = 0; index < kernel_spectrum.size(); ++index) {
    const auto value = kernel_spectrum.data()[index];
    inverse_kernel_spectrum.data()[index] =
      std::abs(value) > epsilon ? complex_type{T{1}, T{}} / value : complex_type{};
  }

  ksj::special::sin(input, trig_scratch.view());
  copy_real_image_to_complex_matrix<T>(ksj::array::as_const_view(trig_scratch.view()), sin_phase.view());
  ksj::special::cos(input, trig_scratch.view());
  copy_real_image_to_complex_matrix<T>(ksj::array::as_const_view(trig_scratch.view()), cos_phase.view());

  forward_plan.execute(sin_phase, sin_spectrum);
  forward_plan.execute(cos_phase, cos_spectrum);
  for (std::size_t index = 0; index < product_spectrum.size(); ++index) {
    product_spectrum.data()[index] = kernel_spectrum.data()[index] * sin_spectrum.data()[index];
  }
  inverse_plan.execute(product_spectrum, laplacian_sin);
  for (std::size_t index = 0; index < product_spectrum.size(); ++index) {
    product_spectrum.data()[index] = kernel_spectrum.data()[index] * cos_spectrum.data()[index];
  }
  inverse_plan.execute(product_spectrum, laplacian_cos);

  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      rhs(row, col) = cos_phase(row, col) * laplacian_sin(row, col) - sin_phase(row, col) * laplacian_cos(row, col);
    }
  }

  forward_plan.execute(rhs, rhs_spectrum);
  for (std::size_t index = 0; index < product_spectrum.size(); ++index) {
    product_spectrum.data()[index] = inverse_kernel_spectrum.data()[index] * rhs_spectrum.data()[index];
  }
  inverse_plan.execute(product_spectrum, phase);

  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      output(row, col) = phase(row, col).real();
    }
  }
}

} // namespace

void unwrap_phase_laplacian_2d(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output) {
  unwrap_phase_laplacian_2d_impl(input, output);
}

void unwrap_phase_laplacian_2d(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output) {
  unwrap_phase_laplacian_2d_impl(input, output);
}

} // namespace ksj::signal::detail::fft
