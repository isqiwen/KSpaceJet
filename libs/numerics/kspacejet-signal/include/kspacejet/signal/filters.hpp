#pragma once

/// Window generation and FIR/IIR-style filtering APIs with explicit signal and output Views.

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/types.hpp"
#include "kspacejet/signal/workspace.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_filters.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_filters.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_windows.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_vml_windows.hpp"
#include "kspacejet/signal/detail/signal_policy.hpp"
#include <cstddef>
#include <stdexcept>

namespace ksj::signal {

template <typename T> void window(ksj::array::VectorView<T> output, const WindowKind kind = WindowKind::hann) {
  if (detail::prefer_intel_window<T>(output.size()) && detail::intel::window(output, kind)) {
    return;
  }
  detail::eigen::window(output, kind);
}

template <typename T> void window(ksj::array::PooledVector<T>& output, const WindowKind kind = WindowKind::hann) {
  window(output.view(), kind);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> window(const std::size_t size, const WindowKind kind = WindowKind::hann) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  window(output.view(), kind);
  return output;
}

template <typename T>
void fir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> taps,
                ksj::array::VectorView<T> output, FirFilterWorkspace<T>& workspace) {
  if (detail::prefer_intel_fir_filter<T>(input.size(), taps.size()) &&
      detail::intel::fir_filter(input, taps, output, workspace)) {
    return;
  }

  if (input.data() == output.data()) {
    workspace.temp_output.resize(input.size());
    detail::eigen::fir_filter(input, taps, workspace.temp_output.view());
    ksj::array::copy(ksj::array::as_const_view(workspace.temp_output.view()), output);
    return;
  }

  detail::eigen::fir_filter(input, taps, output);
}

template <typename T>
void fir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> taps,
                ksj::array::VectorView<T> output) {
  FirFilterWorkspace<T> workspace;
  fir_filter(input, taps, output, workspace);
}

template <typename T>
void fir_filter(ksj::array::VectorView<T> input, ksj::array::VectorView<const T> taps, ksj::array::VectorView<T> output,
                FirFilterWorkspace<T>& workspace) {
  fir_filter(ksj::array::as_const_view(input), taps, output, workspace);
}

template <typename T>
void fir_filter(ksj::array::VectorView<T> input, ksj::array::VectorView<const T> taps,
                ksj::array::VectorView<T> output) {
  fir_filter(ksj::array::as_const_view(input), taps, output);
}

template <typename T>
void fir_filter(const ksj::array::PooledVector<T>& input, const ksj::array::PooledVector<T>& taps,
                ksj::array::PooledVector<T>& output, FirFilterWorkspace<T>& workspace) {
  output.resize(input.size());
  fir_filter(input.view(), taps.view(), output.view(), workspace);
}

template <typename T>
void fir_filter(const ksj::array::PooledVector<T>& input, const ksj::array::PooledVector<T>& taps,
                ksj::array::PooledVector<T>& output) {
  output.resize(input.size());
  fir_filter(input.view(), taps.view(), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> fir_filter(ksj::array::VectorView<const T> input,
                                                     ksj::array::VectorView<const T> taps) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  fir_filter(input, taps, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> fir_filter(const ksj::array::PooledVector<T>& input,
                                                     const ksj::array::PooledVector<T>& taps) {
  return fir_filter(input.view(), taps.view());
}

template <typename T>
void iir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> numerator,
                ksj::array::VectorView<const T> denominator, ksj::array::VectorView<T> output,
                IirFilterWorkspace<T>& workspace) {
  if (detail::prefer_intel_iir_filter<T>(input.size()) &&
      detail::intel::iir_filter(input, numerator, denominator, output, workspace)) {
    return;
  }

  if (input.data() == output.data()) {
    workspace.temp_output.resize(input.size());
    detail::eigen::iir_filter(input, numerator, denominator, workspace.temp_output.view());
    ksj::array::copy(ksj::array::as_const_view(workspace.temp_output.view()), output);
    return;
  }

  detail::eigen::iir_filter(input, numerator, denominator, output);
}

template <typename T>
void iir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> numerator,
                ksj::array::VectorView<const T> denominator, ksj::array::VectorView<T> output) {
  IirFilterWorkspace<T> workspace;
  iir_filter(input, numerator, denominator, output, workspace);
}

template <typename T>
void iir_filter(ksj::array::VectorView<T> input, ksj::array::VectorView<const T> numerator,
                ksj::array::VectorView<const T> denominator, ksj::array::VectorView<T> output,
                IirFilterWorkspace<T>& workspace) {
  iir_filter(ksj::array::as_const_view(input), numerator, denominator, output, workspace);
}

template <typename T>
void iir_filter(ksj::array::VectorView<T> input, ksj::array::VectorView<const T> numerator,
                ksj::array::VectorView<const T> denominator, ksj::array::VectorView<T> output) {
  iir_filter(ksj::array::as_const_view(input), numerator, denominator, output);
}

template <typename T>
void iir_filter(const ksj::array::PooledVector<T>& input, const ksj::array::PooledVector<T>& numerator,
                const ksj::array::PooledVector<T>& denominator, ksj::array::PooledVector<T>& output,
                IirFilterWorkspace<T>& workspace) {
  output.resize(input.size());
  iir_filter(input.view(), numerator.view(), denominator.view(), output.view(), workspace);
}

template <typename T>
void iir_filter(const ksj::array::PooledVector<T>& input, const ksj::array::PooledVector<T>& numerator,
                const ksj::array::PooledVector<T>& denominator, ksj::array::PooledVector<T>& output) {
  output.resize(input.size());
  iir_filter(input.view(), numerator.view(), denominator.view(), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> iir_filter(ksj::array::VectorView<const T> input,
                                                     ksj::array::VectorView<const T> numerator,
                                                     ksj::array::VectorView<const T> denominator) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  iir_filter(input, numerator, denominator, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> iir_filter(const ksj::array::PooledVector<T>& input,
                                                     const ksj::array::PooledVector<T>& numerator,
                                                     const ksj::array::PooledVector<T>& denominator) {
  return iir_filter(input.view(), numerator.view(), denominator.view());
}

template <typename T> void tukey_window(ksj::array::VectorView<T> output, const T ratio) {
  detail::eigen::tukey_window(output, ratio);
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> tukey_window(const std::size_t size, const T ratio) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  tukey_window(output.view(), ratio);
  return output;
}

template <typename T>
void triangle_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length) {
  detail::eigen::triangle_filter(output, start, filter_length);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> triangle_filter(const std::size_t size, const int start,
                                                          const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  triangle_filter(output.view(), start, filter_length);
  return output;
}

template <typename T>
void half_hamming_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length) {
  detail::eigen::half_hamming_filter(output, start, filter_length);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> half_hamming_filter(const std::size_t size, const int start,
                                                              const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  half_hamming_filter(output.view(), start, filter_length);
  return output;
}

template <typename T>
void hamming_bandpass_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length) {
  detail::eigen::hamming_bandpass_filter(output, start, filter_length);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> hamming_bandpass_filter(const std::size_t size, const int start,
                                                                  const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  hamming_bandpass_filter(output.view(), start, filter_length);
  return output;
}

template <typename T>
void dual_hamming_bandpass_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length) {
  detail::eigen::dual_hamming_bandpass_filter(output, start, filter_length);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> dual_hamming_bandpass_filter(const std::size_t size, const int start,
                                                                       const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  dual_hamming_bandpass_filter(output.view(), start, filter_length);
  return output;
}

template <typename T>
void half_hann_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length) {
  detail::eigen::half_hann_filter(output, start, filter_length);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> half_hann_filter(const std::size_t size, const int start,
                                                           const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  half_hann_filter(output.view(), start, filter_length);
  return output;
}

template <typename T>
void half_blackman_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length) {
  detail::eigen::half_blackman_filter(output, start, filter_length);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> half_blackman_filter(const std::size_t size, const int start,
                                                               const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  half_blackman_filter(output.view(), start, filter_length);
  return output;
}

template <typename T>
void median_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                   const std::size_t kernel_size, MedianFilterWorkspace<T>& workspace,
                   const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("median_filter output dimension mismatch");
  }
  if (kernel_size == 0U || kernel_size % 2U == 0U) {
    throw std::invalid_argument("median_filter kernel size must be a positive odd value");
  }
  if (input.empty()) {
    return;
  }

  if (border_mode == SignalBorderMode::causal_replicate && detail::prefer_intel_median_filter<T>(input.size())) {
    if (input.data() != output.data()) {
      ksj::array::copy(input, output);
    }
    if (detail::intel::median_filter_in_place(output, kernel_size, border_mode, workspace)) {
      return;
    }
  }

  detail::eigen::median_filter(input, output, kernel_size, border_mode, workspace);
}

template <typename T>
void median_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                   const std::size_t kernel_size, const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  MedianFilterWorkspace<T> workspace;
  median_filter(input, output, kernel_size, workspace, border_mode);
}

template <typename T>
void median_filter(ksj::array::VectorView<T> input, ksj::array::VectorView<T> output, const std::size_t kernel_size,
                   MedianFilterWorkspace<T>& workspace,
                   const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  median_filter(ksj::array::as_const_view(input), output, kernel_size, workspace, border_mode);
}

template <typename T>
void median_filter(ksj::array::VectorView<T> input, ksj::array::VectorView<T> output, const std::size_t kernel_size,
                   const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  median_filter(ksj::array::as_const_view(input), output, kernel_size, border_mode);
}

template <typename T>
void median_filter(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output,
                   const std::size_t kernel_size, MedianFilterWorkspace<T>& workspace,
                   const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  median_filter(input.view(), output.view(), kernel_size, workspace, border_mode);
}

template <typename T>
void median_filter(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output,
                   const std::size_t kernel_size, const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  median_filter(input.view(), output.view(), kernel_size, border_mode);
}

template <typename T>
void median_filter_in_place(ksj::array::VectorView<T> input, const std::size_t kernel_size,
                            MedianFilterWorkspace<T>& workspace,
                            const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  if (border_mode == SignalBorderMode::causal_replicate && detail::prefer_intel_median_filter<T>(input.size()) &&
      detail::intel::median_filter_in_place(input, kernel_size, border_mode, workspace)) {
    return;
  }
  median_filter(ksj::array::as_const_view(input), input, kernel_size, workspace, border_mode);
}

template <typename T>
void median_filter_in_place(ksj::array::VectorView<T> input, const std::size_t kernel_size,
                            const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  MedianFilterWorkspace<T> workspace;
  median_filter_in_place(input, kernel_size, workspace, border_mode);
}

template <typename T>
void median_filter_in_place(ksj::array::PooledVector<T>& input, const std::size_t kernel_size,
                            MedianFilterWorkspace<T>& workspace,
                            const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  median_filter_in_place(input.view(), kernel_size, workspace, border_mode);
}

template <typename T>
void median_filter_in_place(ksj::array::PooledVector<T>& input, const std::size_t kernel_size,
                            const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  median_filter_in_place(input.view(), kernel_size, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T>
median_filter(ksj::array::VectorView<const T> input, const std::size_t kernel_size,
              const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  median_filter(input, output.view(), kernel_size, border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T>
median_filter(const ksj::array::PooledVector<T>& input, const std::size_t kernel_size,
              const SignalBorderMode border_mode = SignalBorderMode::replicate) {
  return median_filter(input.view(), kernel_size, border_mode);
}

template <typename T>
void hbrr_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length) {
  detail::eigen::hbrr_filter(output, start, filter_length);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> hbrr_filter(const std::size_t size, const int start,
                                                      const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  hbrr_filter(output.view(), start, filter_length);
  return output;
}

template <typename T>
void tukey_filter(ksj::array::VectorView<T> output, const T ratio, const int start, const std::size_t filter_length) {
  detail::eigen::tukey_filter(output, ratio, start, filter_length);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> tukey_filter(const std::size_t size, const T ratio, const int start,
                                                       const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  tukey_filter(output.view(), ratio, start, filter_length);
  return output;
}

template <typename T>
void exponential_window(ksj::array::VectorView<T> output, const T alpha, const T exponent = T{2}) {
  if (detail::prefer_intel_exponential_window<T>(output.size()) &&
      detail::intel::exponential_window(output, alpha, exponent)) {
    return;
  }
  detail::eigen::exponential_window(output, alpha, exponent);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> exponential_window(const std::size_t size, const T alpha,
                                                             const T exponent = T{2}) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  exponential_window(output.view(), alpha, exponent);
  return output;
}

template <typename T>
void exponential_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length,
                        const T alpha, const T exponent) {
  detail::eigen::exponential_filter(output, start, filter_length, alpha, exponent);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> exponential_filter(const std::size_t size, const int start,
                                                             const std::size_t filter_length, const T alpha,
                                                             const T exponent) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  exponential_filter(output.view(), start, filter_length, alpha, exponent);
  return output;
}

template <typename T> void fermi_window(ksj::array::VectorView<T> output, const T radius, const T width) {
  detail::eigen::fermi_window(output, radius, width);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> fermi_window(const std::size_t size, const T radius, const T width) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  fermi_window(output.view(), radius, width);
  return output;
}

template <typename T>
void fermi_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length, const T radius,
                  const T width) {
  detail::eigen::fermi_filter(output, start, filter_length, radius, width);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> fermi_filter(const std::size_t size, const int start,
                                                       const std::size_t filter_length, const T radius, const T width) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  fermi_filter(output.view(), start, filter_length, radius, width);
  return output;
}

template <typename T>
void quadratic_exponential_window(ksj::array::VectorView<T> output, const T radius, const T scale_factor) {
  detail::eigen::quadratic_exponential_window(output, radius, scale_factor);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> quadratic_exponential_window(const std::size_t size, const T radius,
                                                                       const T scale_factor) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  quadratic_exponential_window(output.view(), radius, scale_factor);
  return output;
}

template <typename T>
void quadratic_exponential_filter(ksj::array::VectorView<T> output, const T radius, const T scale_factor) {
  detail::eigen::quadratic_exponential_filter(output, radius, scale_factor);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> quadratic_exponential_filter(const std::size_t size, const T radius,
                                                                       const T scale_factor) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  quadratic_exponential_filter(output.view(), radius, scale_factor);
  return output;
}

template <typename T>
void t2_linear_filter(ksj::array::VectorView<T> output, const int start, const T step, const int center) {
  detail::eigen::t2_linear_filter(output, start, step, center);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> t2_linear_filter(const std::size_t size, const int start, const T step,
                                                           const int center) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  t2_linear_filter(output.view(), start, step, center);
  return output;
}

template <typename T>
void t2_exponential_filter(ksj::array::VectorView<T> output, const int start, const T echo_spacing, const int center,
                           const T decay_constant) {
  detail::eigen::t2_exponential_filter(output, start, echo_spacing, center, decay_constant);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> t2_exponential_filter(const std::size_t size, const int start,
                                                                const T echo_spacing, const int center,
                                                                const T decay_constant) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  t2_exponential_filter(output.view(), start, echo_spacing, center, decay_constant);
  return output;
}

template <typename T> void cosine_laplacian_denominator(ksj::array::ImageView<T> output, const std::size_t dimension) {
  detail::eigen::cosine_laplacian_denominator(output, dimension);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cosine_laplacian_denominator(const std::size_t rows, const std::size_t cols,
                                                                      const std::size_t dimension) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  cosine_laplacian_denominator(output.view(), dimension);
  return output;
}

template <typename T>
void fermi_bandpass_window(ksj::array::VectorView<T> output, const T low_radius, const T high_radius, const T width) {
  detail::eigen::fermi_bandpass_window(output, low_radius, high_radius, width);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> fermi_bandpass_window(const std::size_t size, const T low_radius,
                                                                const T high_radius, const T width) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  fermi_bandpass_window(output.view(), low_radius, high_radius, width);
  return output;
}

template <typename T>
void dual_fermi_band_window(ksj::array::VectorView<T> output, const T center_offset, const T radius, const T width) {
  detail::eigen::dual_fermi_band_window(output, center_offset, radius, width);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> dual_fermi_band_window(const std::size_t size, const T center_offset,
                                                                 const T radius, const T width) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  dual_fermi_band_window(output.view(), center_offset, radius, width);
  return output;
}

} // namespace ksj::signal
