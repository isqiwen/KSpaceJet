#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/types.hpp"
#include "kspacejet/signal/workspace.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::signal::detail::eigen {

template <typename T> void window(ksj::array::VectorView<T> output, const WindowKind kind) {
  if (output.empty()) {
    return;
  }

  const auto size = output.size();
  if (kind == WindowKind::rectangular || size == 1U) {
    ksj::array::fill(output, T{1});
    return;
  }

  const auto phase_scale = T{2} * std::numbers::pi_v<T> / static_cast<T>(size - 1U);
  for (std::size_t index = 0; index < size; ++index) {
    const auto phase = phase_scale * static_cast<T>(index);
    switch (kind) {
      case WindowKind::rectangular:
        output(index) = T{1};
        break;
      case WindowKind::hann:
        output(index) = T{0.5} - T{0.5} * std::cos(phase);
        break;
      case WindowKind::hamming:
        output(index) = static_cast<T>(0.54) - static_cast<T>(0.46) * std::cos(phase);
        break;
      case WindowKind::blackman:
        output(index) =
          static_cast<T>(0.42) - static_cast<T>(0.5) * std::cos(phase) + static_cast<T>(0.08) * std::cos(T{2} * phase);
        break;
    }
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> window(const std::size_t size, const WindowKind kind) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  window(output.view(), kind);
  return output;
}

template <typename T>
void fir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> taps,
                ksj::array::VectorView<T> output) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("fir_filter output dimension mismatch");
  }
  if (taps.empty()) {
    throw std::invalid_argument("fir_filter taps must not be empty");
  }
  if (input.empty()) {
    return;
  }
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_vector<T>(input.size());
    fir_filter(input, taps, temp.view());
    ksj::array::copy(ksj::array::as_const_view(temp.view()), output);
    return;
  }

  if (input.is_contiguous() && taps.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    const auto* taps_data = taps.data();
    auto* output_data = output.data();
    for (std::size_t index = 0; index < input.size(); ++index) {
      T value{};
      const auto max_tap = std::min(index + 1U, taps.size());
      for (std::size_t tap = 0; tap < max_tap; ++tap) {
        value += taps_data[tap] * input_data[index - tap];
      }
      output_data[index] = value;
    }
    return;
  }

  for (std::size_t index = 0; index < input.size(); ++index) {
    T value{};
    const auto max_tap = std::min(index + 1U, taps.size());
    for (std::size_t tap = 0; tap < max_tap; ++tap) {
      value += taps(tap) * input(index - tap);
    }
    output(index) = value;
  }
}

template <typename T>
void iir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> numerator,
                ksj::array::VectorView<const T> denominator, ksj::array::VectorView<T> output) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("iir_filter output dimension mismatch");
  }
  if (numerator.empty()) {
    throw std::invalid_argument("iir_filter numerator must not be empty");
  }
  if (denominator.empty()) {
    throw std::invalid_argument("iir_filter denominator must not be empty");
  }
  if (denominator(0U) == T{}) {
    throw std::invalid_argument("iir_filter denominator[0] must not be zero");
  }
  if (input.empty()) {
    return;
  }
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_vector<T>(input.size());
    iir_filter(input, numerator, denominator, temp.view());
    ksj::array::copy(ksj::array::as_const_view(temp.view()), output);
    return;
  }

  const auto feedback_scale = denominator(0U);
  for (std::size_t index = 0; index < input.size(); ++index) {
    T value{};
    const auto max_feedforward = std::min(index + 1U, numerator.size());
    for (std::size_t tap = 0; tap < max_feedforward; ++tap) {
      value += numerator(tap) * input(index - tap);
    }

    const auto max_feedback = std::min(index + 1U, denominator.size());
    for (std::size_t tap = 1U; tap < max_feedback; ++tap) {
      value -= denominator(tap) * output(index - tap);
    }
    output(index) = value / feedback_scale;
  }
}

inline void validate_filter_length(const std::size_t filter_length) {
  if (filter_length <= 1U) {
    throw std::invalid_argument("filter length must be greater than one");
  }
}

template <typename T>
void median_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                   const std::size_t kernel_size, const SignalBorderMode border_mode,
                   MedianFilterWorkspace<T>& workspace) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("median_filter output dimension mismatch");
  }
  if (kernel_size == 0U || kernel_size % 2U == 0U) {
    throw std::invalid_argument("median_filter kernel size must be a positive odd value");
  }
  if (input.empty()) {
    return;
  }
  if (input.data() == output.data()) {
    workspace.temp_output.resize(input.size());
    ::ksj::signal::detail::eigen::median_filter(input, workspace.temp_output.view(), kernel_size, border_mode,
                                                workspace);
    ksj::array::copy(ksj::array::as_const_view(workspace.temp_output.view()), output);
    return;
  }

  workspace.window_storage.resize(kernel_size);
  auto window = workspace.window_storage.view();
  const auto radius = kernel_size / 2U;
  if (input.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    auto* window_data = window.data();
    for (std::size_t index = 0; index < input.size(); ++index) {
      if (border_mode == SignalBorderMode::causal_replicate) {
        for (std::size_t lag = 0; lag < kernel_size; ++lag) {
          window_data[lag] = input_data[index >= lag ? index - lag : 0U];
        }
      } else {
        for (std::size_t window_index = 0; window_index < kernel_size; ++window_index) {
          const auto padded_index = index + window_index;
          if (border_mode == SignalBorderMode::zero &&
              (padded_index < radius || padded_index >= input.size() + radius)) {
            window_data[window_index] = T{};
            continue;
          }
          const auto source_index = std::clamp(padded_index, radius, input.size() + radius - 1U) - radius;
          window_data[window_index] = input_data[source_index];
        }
      }

      const auto median_index = window_data + static_cast<std::ptrdiff_t>(kernel_size / 2U);
      std::nth_element(window_data, median_index, window_data + kernel_size);
      output_data[index] = *median_index;
    }
    return;
  }

  for (std::size_t index = 0; index < input.size(); ++index) {
    if (border_mode == SignalBorderMode::causal_replicate) {
      for (std::size_t lag = 0; lag < kernel_size; ++lag) {
        window(lag) = input(index >= lag ? index - lag : 0U);
      }
    } else {
      for (std::size_t window_index = 0; window_index < kernel_size; ++window_index) {
        const auto padded_index = index + window_index;
        if (border_mode == SignalBorderMode::zero && (padded_index < radius || padded_index >= input.size() + radius)) {
          window(window_index) = T{};
          continue;
        }
        const auto source_index = std::clamp(padded_index, radius, input.size() + radius - 1U) - radius;
        window(window_index) = input(source_index);
      }
    }

    const auto median_index = window.data() + static_cast<std::ptrdiff_t>(kernel_size / 2U);
    std::nth_element(window.data(), median_index, window.data() + kernel_size);
    output(index) = *median_index;
  }
}

template <typename T>
void median_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                   const std::size_t kernel_size, const SignalBorderMode border_mode) {
  MedianFilterWorkspace<T> workspace;
  median_filter(input, output, kernel_size, border_mode, workspace);
}

template <typename T, typename Function> void generate_by_index(ksj::array::VectorView<T> output, Function&& function) {
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < output.size(); ++index) {
      output_data[index] = static_cast<T>(function(index));
    }
    return;
  }

  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<T>(function(index));
  }
}

template <typename T, typename Function>
void generated_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length,
                      Function&& function) {
  if (output.empty()) {
    return;
  }
  validate_filter_length(filter_length);

  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < output.size(); ++index) {
      output_data[index] = static_cast<T>(function(start + static_cast<int>(index), index));
    }
    return;
  }
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<T>(function(start + static_cast<int>(index), index));
  }
}

template <typename T, typename Function>
[[nodiscard]] ksj::array::PooledVector<T> generated_filter(const std::size_t size, const int start,
                                                           const std::size_t filter_length, Function&& function) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  generated_filter(output.view(), start, filter_length, std::forward<Function>(function));
  return output;
}

template <typename T>
void triangle_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length) {
  const auto peak_index = static_cast<int>(filter_length) / 2;
  const auto denom = static_cast<T>(filter_length - 1U);
  generated_filter<T>(output, start, filter_length, [peak_index, denom](const int generator_index, std::size_t) {
    return (static_cast<T>(peak_index - generator_index) * T{2}) / denom;
  });
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
  const auto denom = static_cast<T>(filter_length - 1U);
  generated_filter<T>(output, start, filter_length, [start, denom](const int generator_index, std::size_t) {
    const auto phase = static_cast<T>(generator_index - start) * std::numbers::pi_v<T> / denom;
    return static_cast<T>(0.54) - static_cast<T>(0.46) * std::cos(phase);
  });
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
  const auto denom = static_cast<T>(filter_length - 1U);
  generated_filter<T>(output, start, filter_length, [start, denom](const int generator_index, std::size_t) {
    const auto phase = static_cast<T>(generator_index - start) * T{2} * std::numbers::pi_v<T> / denom;
    return static_cast<T>(0.54) - static_cast<T>(0.46) * std::cos(phase);
  });
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
  if (filter_length / 2U <= 1U) {
    throw std::invalid_argument("dual hamming bandpass filter length must be greater than two");
  }

  const auto half_length = static_cast<int>(filter_length / 2U);
  const auto denom = static_cast<T>(half_length - 1);
  generated_filter<T>(output, start, filter_length,
                      [start, half_length, denom](const int generator_index, std::size_t) {
                        const int phase_index = generator_index < half_length ? generator_index - start
                                                                              : generator_index - half_length - start;
                        const auto phase = static_cast<T>(phase_index) * T{2} * std::numbers::pi_v<T> / denom;
                        return static_cast<T>(0.54) - static_cast<T>(0.46) * std::cos(phase);
                      });
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
  const auto denom = static_cast<T>(filter_length - 1U);
  generated_filter<T>(output, start, filter_length, [start, denom](const int generator_index, std::size_t) {
    const auto phase = static_cast<T>(generator_index - start) * std::numbers::pi_v<T> / denom;
    return T{0.5} - T{0.5} * std::cos(phase);
  });
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
  const auto denom = static_cast<T>(filter_length - 1U);
  generated_filter<T>(output, start, filter_length, [start, denom](const int generator_index, std::size_t) {
    const auto phase = static_cast<T>(generator_index - start) * std::numbers::pi_v<T> / denom;
    return static_cast<T>(0.42) - static_cast<T>(0.5) * std::cos(phase) + static_cast<T>(0.08) * std::cos(T{2} * phase);
  });
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> half_blackman_filter(const std::size_t size, const int start,
                                                               const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  half_blackman_filter(output.view(), start, filter_length);
  return output;
}

template <typename T>
void hbrr_filter(ksj::array::VectorView<T> output, const int start, const std::size_t filter_length) {
  const auto denom = static_cast<T>(filter_length - 1U);
  generated_filter<T>(output, start, filter_length, [start, denom](const int generator_index, std::size_t) {
    const auto value = std::sin(static_cast<T>(generator_index - start) * std::numbers::pi_v<T> / denom);
    return value * (T{2} - value);
  });
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> hbrr_filter(const std::size_t size, const int start,
                                                      const std::size_t filter_length) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  hbrr_filter(output.view(), start, filter_length);
  return output;
}

template <typename T> void tukey_window(ksj::array::VectorView<T> output, const T ratio) {
  if (output.empty()) {
    return;
  }
  if (ratio <= T{0}) {
    ksj::array::fill(output, T{1});
    return;
  }
  if (ratio >= T{1}) {
    ksj::signal::detail::eigen::window(output, WindowKind::hann);
    return;
  }

  const auto size = output.size();
  const auto length_minus_one = static_cast<T>(size - 1U);
  if (size == 1U) {
    output(0) = T{1};
    return;
  }

  const auto edge_width = ratio * length_minus_one / T{2};
  generate_by_index<T>(output, [size, edge_width, ratio, length_minus_one](const std::size_t index) {
    const auto position = static_cast<T>(index);
    if (position < edge_width) {
      return T{0.5} * (T{1} + std::cos(std::numbers::pi_v<T> * (T{2} * position / (ratio * length_minus_one) - T{1})));
    }
    if (position <= static_cast<T>(size - 1U) - edge_width) {
      return T{1};
    }
    return T{0.5} * (T{1} + std::cos(std::numbers::pi_v<T> *
                                     (T{2} * position / (ratio * length_minus_one) - T{2} / ratio + T{1})));
  });
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> tukey_window(const std::size_t size, const T ratio) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  tukey_window(output.view(), ratio);
  return output;
}

template <typename T>
void tukey_filter(ksj::array::VectorView<T> output, const T ratio, const int start, const std::size_t filter_length) {
  generated_filter<T>(
    output, start, filter_length, [ratio, start, filter_length](const int generator_index, std::size_t) {
      const int index = generator_index - start;
      const int length = static_cast<int>(filter_length);
      if (ratio <= T{0}) {
        return T{1};
      }
      if (ratio >= T{1}) {
        const auto phase = T{2} * std::numbers::pi_v<T> * static_cast<T>(index) / static_cast<T>(length - 1);
        return T{0.5} * (T{1} - std::cos(phase));
      }

      const int taper_low = static_cast<int>(std::floor(ratio / T{2} * static_cast<T>(length - 1))) + 1;
      const int taper_high = length - taper_low + 1;
      if (index < taper_low) {
        const auto phase =
          std::numbers::pi_v<T> / (ratio / T{2}) * (static_cast<T>(index) / static_cast<T>(length - 1) - ratio / T{2});
        return T{0.5} * (T{1} + std::cos(phase));
      }
      if (index >= taper_high) {
        const auto phase = std::numbers::pi_v<T> / (ratio / T{2}) *
                           (static_cast<T>(index) / static_cast<T>(length - 1) - T{1} + ratio / T{2});
        return T{0.5} * (T{1} + std::cos(phase));
      }
      return T{1};
    });
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
  if (output.empty()) {
    return;
  }

  const auto center = static_cast<T>(output.size() - 1U) / T{2};
  const auto denom = output.size() > 1U ? static_cast<T>(output.size() - 1U) : T{1};
  generate_by_index<T>(output, [center, denom, alpha, exponent](const std::size_t index) {
    const auto normalized = (static_cast<T>(index) - center) / denom;
    return std::exp(-alpha * std::pow(std::abs(normalized), exponent));
  });
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
  generated_filter<T>(
    output, start, filter_length, [start, filter_length, alpha, exponent](const int generator_index, std::size_t) {
      const auto normalized = (static_cast<T>(generator_index - start) - static_cast<T>(filter_length) / T{2}) /
                              static_cast<T>(filter_length);
      return std::exp(std::pow(normalized, exponent) * -alpha);
    });
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
  if (width <= T{0}) {
    throw std::invalid_argument("fermi_window width must be positive");
  }

  if (output.empty()) {
    return;
  }

  const auto center = static_cast<T>(output.size() - 1U) / T{2};
  generate_by_index<T>(output, [center, radius, width](const std::size_t index) {
    return T{1} / (T{1} + std::exp((std::abs(static_cast<T>(index) - center) - radius) / width));
  });
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
  if (width <= T{0}) {
    throw std::invalid_argument("fermi filter width must be positive");
  }

  generated_filter<T>(
    output, start, filter_length, [start, filter_length, radius, width](const int generator_index, std::size_t) {
      const auto distance = std::abs(static_cast<T>(generator_index - start) - static_cast<T>(filter_length) / T{2});
      return T{1} / (T{1} + std::exp((distance - radius) / width));
    });
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
  if (output.empty()) {
    return;
  }

  const auto length = static_cast<T>(output.size());
  const auto half_length = length / T{2};
  const auto scale = scale_factor / ((length * radius) * (length * radius));
  generate_by_index<T>(output, [half_length, scale](const std::size_t index) {
    const auto offset = static_cast<T>(index) - half_length;
    return std::exp(offset * offset * scale);
  });
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
  if (output.empty()) {
    return;
  }

  const auto length = static_cast<T>(output.size());
  const auto half_length = length / T{2};
  const auto scale = scale_factor / ((length * radius) * (length * radius));
  generate_by_index<T>(output, [half_length, scale](const std::size_t index) {
    const auto offset = static_cast<T>(index) - half_length;
    return std::exp(offset * offset * scale);
  });
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
  generate_by_index<T>(output, [start, step, center](const std::size_t index) {
    return (static_cast<T>(start + static_cast<int>(index) - center) * step) + T{1};
  });
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
  if (decay_constant == T{}) {
    throw std::invalid_argument("t2 exponential filter decay constant must be non-zero");
  }

  generate_by_index<T>(output, [start, echo_spacing, center, decay_constant](const std::size_t index) {
    return std::exp(static_cast<T>(center - (start + static_cast<int>(index))) * echo_spacing / decay_constant);
  });
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
  if (dimension == 0U) {
    throw std::invalid_argument("cosine laplacian denominator dimension must be non-zero");
  }

  const auto denom = static_cast<T>(dimension);
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t row = 0; row < output.rows(); ++row) {
      const auto row_factor = std::cos(static_cast<T>(row) * std::numbers::pi_v<T> / denom);
      const auto row_offset = row * output.cols();
      for (std::size_t col = 0; col < output.cols(); ++col) {
        const auto col_factor = std::cos(static_cast<T>(col) * std::numbers::pi_v<T> / denom);
        output_data[row_offset + col] = T{2} * (row_factor + col_factor - T{2});
      }
    }
    return;
  }

  for (std::size_t row = 0; row < output.rows(); ++row) {
    const auto row_factor = std::cos(static_cast<T>(row) * std::numbers::pi_v<T> / denom);
    for (std::size_t col = 0; col < output.cols(); ++col) {
      const auto col_factor = std::cos(static_cast<T>(col) * std::numbers::pi_v<T> / denom);
      output(row, col) = T{2} * (row_factor + col_factor - T{2});
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cosine_laplacian_denominator(const std::size_t rows, const std::size_t cols,
                                                                      const std::size_t dimension) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  cosine_laplacian_denominator(output.view(), dimension);
  return output;
}

template <typename T> [[nodiscard]] T fermi_edge(const T distance, const T radius, const T width) {
  return T{1} / (T{1} + std::exp((distance - radius) / width));
}

template <typename T>
void fermi_bandpass_window(ksj::array::VectorView<T> output, const T low_radius, const T high_radius, const T width) {
  if (low_radius < T{} || high_radius <= low_radius) {
    throw std::invalid_argument("fermi_bandpass_window requires 0 <= low_radius < high_radius");
  }
  if (width <= T{}) {
    throw std::invalid_argument("fermi_bandpass_window width must be positive");
  }
  if (output.empty()) {
    return;
  }

  const auto center = static_cast<T>(output.size() - 1U) / T{2};
  generate_by_index<T>(output, [center, low_radius, high_radius, width](const std::size_t index) {
    const auto distance = std::abs(static_cast<T>(index) - center);
    const auto highpass = T{1} / (T{1} + std::exp((low_radius - distance) / width));
    return highpass * fermi_edge(distance, high_radius, width);
  });
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
  if (center_offset < T{} || radius < T{}) {
    throw std::invalid_argument("dual_fermi_band_window center_offset and radius must be non-negative");
  }
  if (width <= T{}) {
    throw std::invalid_argument("dual_fermi_band_window width must be positive");
  }
  if (output.empty()) {
    return;
  }

  const auto center = static_cast<T>(output.size() - 1U) / T{2};
  const auto left_center = center - center_offset;
  const auto right_center = center + center_offset;
  generate_by_index<T>(output, [left_center, right_center, radius, width](const std::size_t index) {
    const auto position = static_cast<T>(index);
    const auto left = fermi_edge(std::abs(position - left_center), radius, width);
    const auto right = fermi_edge(std::abs(position - right_center), radius, width);
    return std::max(left, right);
  });
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> dual_fermi_band_window(const std::size_t size, const T center_offset,
                                                                 const T radius, const T width) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  dual_fermi_band_window(output.view(), center_offset, radius, width);
  return output;
}
} // namespace ksj::signal::detail::eigen
