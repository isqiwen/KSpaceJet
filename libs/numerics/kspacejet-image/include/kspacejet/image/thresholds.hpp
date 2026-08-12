#pragma once

/// Image threshold estimation and threshold-application operations for masks and labels.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/common.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_thresholds.hpp"
#include "kspacejet/image/detail/image_policy.hpp"
#include "kspacejet/image/detail/intel/intel_image_thresholds.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_thresholds.hpp"
#include "kspacejet/image/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace ksj::image {

template <typename T>
void threshold(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const T threshold_value,
               const T low_value = T{}, const T high_value = T{1}) {
  detail::validate_image_shape(input, output, "threshold output dimension mismatch");
  if (detail::prefer_intel_threshold<T>(input.size()) &&
      detail::intel::threshold(input, output, threshold_value, low_value, high_value)) {
    return;
  }
  if (detail::prefer_opencv_threshold<T>(input.size()) &&
      detail::opencv::threshold(input, output, threshold_value, low_value, high_value)) {
    return;
  }

  if (input.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output_data[index] = input_data[index] >= threshold_value ? high_value : low_value;
    }
    return;
  }
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      output(row, col) = input(row, col) >= threshold_value ? high_value : low_value;
    }
  }
}

template <typename T>
void threshold(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const T threshold_value,
               const T low_value = T{}, const T high_value = T{1}) {
  threshold(ksj::array::as_const_view(input), output, threshold_value, low_value, high_value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> threshold(ksj::array::ImageView<const T> input, const T threshold_value,
                                                   const T low_value = T{}, const T high_value = T{1}) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  threshold(input, output.view(), threshold_value, low_value, high_value);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> threshold(ksj::array::ImageView<T> input, const T threshold_value,
                                                   const T low_value = T{}, const T high_value = T{1}) {
  return threshold(ksj::array::as_const_view(input), threshold_value, low_value, high_value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> threshold(const ksj::array::PooledImage<T>& input, const T threshold_value,
                                                   const T low_value = T{}, const T high_value = T{1}) {
  return threshold(input.view(), threshold_value, low_value, high_value);
}

template <typename T> void normalize_minmax(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) {
  detail::validate_image_shape(input, output, "normalize_minmax output dimension mismatch");
  if (input.empty()) {
    throw std::invalid_argument("normalize_minmax input must not be empty");
  }
  if (detail::prefer_intel_normalize_minmax<T>(input.size()) && detail::intel::normalize_minmax(input, output)) {
    return;
  }
  if (detail::prefer_opencv_normalize_minmax<T>(input.size()) && detail::opencv::normalize_minmax(input, output)) {
    return;
  }

  auto min_value = input(0U, 0U);
  auto max_value = input(0U, 0U);
  if (input.is_contiguous()) {
    const auto* input_data = input.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      const auto value = input_data[index];
      min_value = std::min(min_value, value);
      max_value = std::max(max_value, value);
    }
  } else {
    for (std::size_t row = 0U; row < input.rows(); ++row) {
      for (std::size_t col = 0U; col < input.cols(); ++col) {
        const auto value = input(row, col);
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
      }
    }
  }

  const auto range = max_value - min_value;
  if (range == T{}) {
    ksj::array::fill(output, T{});
    return;
  }

  if (input.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output_data[index] = (input_data[index] - min_value) / range;
    }
    return;
  }
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      output(row, col) = (input(row, col) - min_value) / range;
    }
  }
}

template <typename T> void normalize_minmax(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output) {
  normalize_minmax(ksj::array::as_const_view(input), output);
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> normalize_minmax(ksj::array::ImageView<const T> input) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  normalize_minmax(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> normalize_minmax(ksj::array::ImageView<T> input) {
  return normalize_minmax(ksj::array::as_const_view(input));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> normalize_minmax(const ksj::array::PooledImage<T>& input) {
  return normalize_minmax(input.view());
}

template <typename T> [[nodiscard]] T otsu_threshold(ksj::array::ImageView<const T> input) {
  if (input.empty()) {
    throw std::invalid_argument("otsu_threshold input must not be empty");
  }

  constexpr std::size_t bin_count = 256U;
  auto min_value = input(0U, 0U);
  auto max_value = input(0U, 0U);
  if (input.is_contiguous()) {
    const auto* input_data = input.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      const auto value = input_data[index];
      min_value = std::min(min_value, value);
      max_value = std::max(max_value, value);
    }
  } else {
    for (std::size_t row = 0U; row < input.rows(); ++row) {
      for (std::size_t col = 0U; col < input.cols(); ++col) {
        const auto value = input(row, col);
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
      }
    }
  }
  if (min_value == max_value) {
    return min_value;
  }

  std::array<std::size_t, bin_count> histogram{};
  const auto min_as_double = static_cast<double>(min_value);
  const auto max_as_double = static_cast<double>(max_value);
  const auto scale = static_cast<double>(bin_count - 1U) / (max_as_double - min_as_double);
  if (input.is_contiguous()) {
    const auto* input_data = input.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      const auto scaled = (static_cast<double>(input_data[index]) - min_as_double) * scale;
      const auto bin = std::clamp(static_cast<std::size_t>(std::floor(scaled)), std::size_t{0}, bin_count - 1U);
      ++histogram[bin];
    }
  } else {
    for (std::size_t row = 0U; row < input.rows(); ++row) {
      for (std::size_t col = 0U; col < input.cols(); ++col) {
        const auto scaled = (static_cast<double>(input(row, col)) - min_as_double) * scale;
        const auto bin = std::clamp(static_cast<std::size_t>(std::floor(scaled)), std::size_t{0}, bin_count - 1U);
        ++histogram[bin];
      }
    }
  }

  double weighted_sum = 0.0;
  for (std::size_t bin = 0U; bin < bin_count; ++bin) {
    weighted_sum += static_cast<double>(bin) * static_cast<double>(histogram[bin]);
  }

  double background_sum = 0.0;
  std::size_t background_weight = 0U;
  double best_variance = -1.0;
  std::size_t best_bin = 0U;
  for (std::size_t bin = 0U; bin < bin_count; ++bin) {
    background_weight += histogram[bin];
    if (background_weight == 0U) {
      continue;
    }

    const auto foreground_weight = input.size() - background_weight;
    if (foreground_weight == 0U) {
      break;
    }

    background_sum += static_cast<double>(bin) * static_cast<double>(histogram[bin]);
    const auto background_mean = background_sum / static_cast<double>(background_weight);
    const auto foreground_mean = (weighted_sum - background_sum) / static_cast<double>(foreground_weight);
    const auto mean_delta = background_mean - foreground_mean;
    const auto between_class_variance =
      static_cast<double>(background_weight) * static_cast<double>(foreground_weight) * mean_delta * mean_delta;
    if (between_class_variance > best_variance) {
      best_variance = between_class_variance;
      best_bin = bin;
    }
  }

  return static_cast<T>(min_as_double + static_cast<double>(best_bin) / scale);
}

template <typename T> [[nodiscard]] T otsu_threshold(ksj::array::ImageView<T> input) {
  return otsu_threshold(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] T otsu_threshold(const ksj::array::PooledImage<T>& input) {
  return otsu_threshold(input.view());
}

template <typename T>
void otsu_mask(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const T low_value = T{},
               const T high_value = T{1}) {
  detail::validate_image_shape(input, output, "otsu_mask output dimension mismatch");
  const auto threshold_value = otsu_threshold(input);
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      output(row, col) = input(row, col) > threshold_value ? high_value : low_value;
    }
  }
}

template <typename T>
void otsu_mask(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const T low_value = T{},
               const T high_value = T{1}) {
  otsu_mask(ksj::array::as_const_view(input), output, low_value, high_value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> otsu_mask(ksj::array::ImageView<const T> input, const T low_value = T{},
                                                   const T high_value = T{1}) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  otsu_mask(input, output.view(), low_value, high_value);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> otsu_mask(ksj::array::ImageView<T> input, const T low_value = T{},
                                                   const T high_value = T{1}) {
  return otsu_mask(ksj::array::as_const_view(input), low_value, high_value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> otsu_mask(const ksj::array::PooledImage<T>& input, const T low_value = T{},
                                                   const T high_value = T{1}) {
  return otsu_mask(input.view(), low_value, high_value);
}

[[nodiscard]] inline std::vector<int> multi_otsu_thresholds_scaled_inplace(ksj::array::ImageView<float> input,
                                                                           const std::size_t class_count = 4U,
                                                                           const std::size_t gray_levels = 256U) {
  return detail::eigen::multi_otsu_thresholds_scaled_inplace(input, class_count, gray_levels);
}

[[nodiscard]] inline std::vector<int> multi_otsu_thresholds_scaled_inplace(ksj::array::PooledImage<float>& input,
                                                                           const std::size_t class_count = 4U,
                                                                           const std::size_t gray_levels = 256U) {
  return multi_otsu_thresholds_scaled_inplace(input.view(), class_count, gray_levels);
}

} // namespace ksj::image
