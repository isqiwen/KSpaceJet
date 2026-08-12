#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::signal::detail::eigen {

[[nodiscard]] inline double cubic_resample_weight(double offset) noexcept {
  constexpr double a = -0.75;
  offset = std::abs(offset);
  if (offset < 1.0) {
    return ((a + 2.0) * offset - (a + 3.0)) * offset * offset + 1.0;
  }
  if (offset < 2.0) {
    return (((a * offset - 5.0 * a) * offset + 8.0 * a) * offset) - 4.0 * a;
  }
  return 0.0;
}

[[nodiscard]] inline double mitchell_resample_weight(double offset) noexcept {
  constexpr double b = 1.0 / 3.0;
  constexpr double c = 1.0 / 3.0;
  offset = std::abs(offset);
  if (offset < 1.0) {
    return ((12.0 - 9.0 * b - 6.0 * c) * offset * offset * offset + (-18.0 + 12.0 * b + 6.0 * c) * offset * offset +
            (6.0 - 2.0 * b)) /
           6.0;
  }
  if (offset < 2.0) {
    return ((-b - 6.0 * c) * offset * offset * offset + (6.0 * b + 30.0 * c) * offset * offset +
            (-12.0 * b - 48.0 * c) * offset + (8.0 * b + 24.0 * c)) /
           6.0;
  }
  return 0.0;
}

[[nodiscard]] inline double normalized_sinc(const double value) noexcept {
  if (std::abs(value) < 1.0e-12) {
    return 1.0;
  }
  const auto phase = std::numbers::pi * value;
  return std::sin(phase) / phase;
}

[[nodiscard]] inline double lanczos3_resample_weight(double offset) noexcept {
  constexpr double radius = 3.0;
  offset = std::abs(offset);
  if (offset >= radius) {
    return 0.0;
  }
  return normalized_sinc(offset) * normalized_sinc(offset / radius);
}

template <typename T>
[[nodiscard]] const T& sample_vector_replicate(ksj::array::VectorView<const T> input, long index) {
  index = std::clamp(index, 0L, static_cast<long>(input.size() - 1U));
  return input(static_cast<std::size_t>(index));
}

template <typename T>
[[nodiscard]] const T& sample_vector_replicate(const T* input, const std::size_t size, long index) {
  index = std::clamp(index, 0L, static_cast<long>(size - 1U));
  return input[static_cast<std::size_t>(index)];
}

template <typename T>
void resample(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output, const ResampleKernel kernel) {
  if (output.empty()) {
    return;
  }
  if (input.empty()) {
    throw std::invalid_argument("resample input must not be empty");
  }
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_vector<T>(output.size());
    ksj::signal::detail::eigen::resample(input, temp.view(), kernel);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (output.size() == 1U || input.size() == 1U) {
    output(0) = input(0);
    for (std::size_t index = 1; index < output.size(); ++index) {
      output(index) = input(input.size() - 1U);
    }
    return;
  }

  const auto scale = static_cast<double>(input.size() - 1U) / static_cast<double>(output.size() - 1U);
  if (input.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0; index < output.size(); ++index) {
      const auto source = static_cast<double>(index) * scale;
      if (kernel == ResampleKernel::nearest) {
        const auto source_index = std::min(static_cast<std::size_t>(std::floor(source + 0.5)), input.size() - 1U);
        output_data[index] = input_data[source_index];
        continue;
      }
      if (kernel == ResampleKernel::cubic) {
        const auto base_index = static_cast<long>(std::floor(source));
        T sum{};
        for (long offset = -1L; offset <= 2L; ++offset) {
          const auto weight = static_cast<ksj::array::real_scalar_t<T>>(
            cubic_resample_weight(source - static_cast<double>(base_index + offset)));
          sum += sample_vector_replicate(input_data, input.size(), base_index + offset) * weight;
        }
        output_data[index] = sum;
        continue;
      }
      if (kernel == ResampleKernel::mitchell) {
        const auto base_index = static_cast<long>(std::floor(source));
        T sum{};
        double weight_sum = 0.0;
        for (long offset = -1L; offset <= 2L; ++offset) {
          const auto weight = mitchell_resample_weight(source - static_cast<double>(base_index + offset));
          if (weight == 0.0) {
            continue;
          }
          sum += sample_vector_replicate(input_data, input.size(), base_index + offset) *
                 static_cast<ksj::array::real_scalar_t<T>>(weight);
          weight_sum += weight;
        }
        if (std::abs(weight_sum) > 1.0e-12) {
          output_data[index] = sum / static_cast<ksj::array::real_scalar_t<T>>(weight_sum);
        } else {
          const auto source_index = std::min(static_cast<std::size_t>(std::floor(source + 0.5)), input.size() - 1U);
          output_data[index] = input_data[source_index];
        }
        continue;
      }
      if (kernel == ResampleKernel::lanczos3) {
        constexpr long radius = 3L;
        const auto base_index = static_cast<long>(std::floor(source));
        T sum{};
        double weight_sum = 0.0;
        for (long source_index = base_index - radius + 1L; source_index <= base_index + radius; ++source_index) {
          const auto weight = lanczos3_resample_weight(source - static_cast<double>(source_index));
          if (weight == 0.0) {
            continue;
          }
          sum += sample_vector_replicate(input_data, input.size(), source_index) *
                 static_cast<ksj::array::real_scalar_t<T>>(weight);
          weight_sum += weight;
        }
        if (std::abs(weight_sum) > 1.0e-12) {
          output_data[index] = sum / static_cast<ksj::array::real_scalar_t<T>>(weight_sum);
        } else {
          const auto source_index = std::min(static_cast<std::size_t>(std::floor(source + 0.5)), input.size() - 1U);
          output_data[index] = input_data[source_index];
        }
        continue;
      }
      if (kernel != ResampleKernel::linear) {
        throw std::invalid_argument("unsupported resample kernel");
      }

      const auto left_index = static_cast<std::size_t>(std::floor(source));
      const auto right_index = std::min(left_index + 1U, input.size() - 1U);
      const auto weight = static_cast<ksj::array::real_scalar_t<T>>(source - static_cast<double>(left_index));
      output_data[index] =
        input_data[left_index] * (ksj::array::real_scalar_t<T>{1} - weight) + input_data[right_index] * weight;
    }
    return;
  }

  for (std::size_t index = 0; index < output.size(); ++index) {
    const auto source = static_cast<double>(index) * scale;
    if (kernel == ResampleKernel::nearest) {
      const auto source_index = std::min(static_cast<std::size_t>(std::floor(source + 0.5)), input.size() - 1U);
      output(index) = input(source_index);
      continue;
    }
    if (kernel == ResampleKernel::cubic) {
      const auto base_index = static_cast<long>(std::floor(source));
      T sum{};
      for (long offset = -1L; offset <= 2L; ++offset) {
        const auto weight = static_cast<ksj::array::real_scalar_t<T>>(
          cubic_resample_weight(source - static_cast<double>(base_index + offset)));
        sum += sample_vector_replicate(input, base_index + offset) * weight;
      }
      output(index) = sum;
      continue;
    }
    if (kernel == ResampleKernel::mitchell) {
      const auto base_index = static_cast<long>(std::floor(source));
      T sum{};
      double weight_sum = 0.0;
      for (long offset = -1L; offset <= 2L; ++offset) {
        const auto weight = mitchell_resample_weight(source - static_cast<double>(base_index + offset));
        if (weight == 0.0) {
          continue;
        }
        sum += sample_vector_replicate(input, base_index + offset) * static_cast<ksj::array::real_scalar_t<T>>(weight);
        weight_sum += weight;
      }
      if (std::abs(weight_sum) > 1.0e-12) {
        output(index) = sum / static_cast<ksj::array::real_scalar_t<T>>(weight_sum);
      } else {
        const auto source_index = std::min(static_cast<std::size_t>(std::floor(source + 0.5)), input.size() - 1U);
        output(index) = input(source_index);
      }
      continue;
    }
    if (kernel == ResampleKernel::lanczos3) {
      constexpr long radius = 3L;
      const auto base_index = static_cast<long>(std::floor(source));
      T sum{};
      double weight_sum = 0.0;
      for (long source_index = base_index - radius + 1L; source_index <= base_index + radius; ++source_index) {
        const auto weight = lanczos3_resample_weight(source - static_cast<double>(source_index));
        if (weight == 0.0) {
          continue;
        }
        sum += sample_vector_replicate(input, source_index) * static_cast<ksj::array::real_scalar_t<T>>(weight);
        weight_sum += weight;
      }
      if (std::abs(weight_sum) > 1.0e-12) {
        output(index) = sum / static_cast<ksj::array::real_scalar_t<T>>(weight_sum);
      } else {
        const auto source_index = std::min(static_cast<std::size_t>(std::floor(source + 0.5)), input.size() - 1U);
        output(index) = input(source_index);
      }
      continue;
    }
    if (kernel != ResampleKernel::linear) {
      throw std::invalid_argument("unsupported resample kernel");
    }

    const auto left_index = static_cast<std::size_t>(std::floor(source));
    const auto right_index = std::min(left_index + 1U, input.size() - 1U);
    const auto weight = static_cast<ksj::array::real_scalar_t<T>>(source - static_cast<double>(left_index));
    output(index) = input(left_index) * (ksj::array::real_scalar_t<T>{1} - weight) + input(right_index) * weight;
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> resample(const ksj::array::PooledVector<T>& input, const std::size_t size,
                                                   const ResampleKernel kernel) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  resample(ksj::array::as_const_view(input.view()), output.view(), kernel);
  return output;
}
} // namespace ksj::signal::detail::eigen
