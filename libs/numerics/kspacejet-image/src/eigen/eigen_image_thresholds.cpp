#include "kspacejet/base/types.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"

#include "kspacejet/image/detail/eigen/eigen_image_basic.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_thresholds.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_hole_filling.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_regions.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_components.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_interpolation.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_filters.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_morphology.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <Eigen/Core>

namespace ksj::image::detail::eigen {
using ksj::array::detail::eigen_adapter::as_eigen;

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> threshold(const ksj::array::PooledImage<T>& input, const T threshold_value,
                                                   const T low_value, const T high_value) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  as_eigen(output).array() =
    low_value + (as_eigen(input).array() >= threshold_value).template cast<T>() * (high_value - low_value);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> normalize_minmax(const ksj::array::PooledImage<T>& input) {
  if (input.empty()) {
    throw std::invalid_argument("normalize_minmax input must not be empty");
  }

  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  const auto min_value = as_eigen(input).minCoeff();
  const auto max_value = as_eigen(input).maxCoeff();
  const auto range = max_value - min_value;
  if (range == T{}) {
    as_eigen(output).setZero();
    return output;
  }

  as_eigen(output).array() = (as_eigen(input).array() - min_value) / range;
  return output;
}

template <typename T> [[nodiscard]] T otsu_threshold(const ksj::array::PooledImage<T>& input) {
  if (input.empty()) {
    throw std::invalid_argument("otsu_threshold input must not be empty");
  }

  constexpr std::size_t bin_count = 256U;
  const auto min_value = as_eigen(input).minCoeff();
  const auto max_value = as_eigen(input).maxCoeff();
  if (min_value == max_value) {
    return min_value;
  }

  std::array<std::size_t, bin_count> histogram{};
  const auto min_as_double = static_cast<double>(min_value);
  const auto max_as_double = static_cast<double>(max_value);
  const auto scale = static_cast<double>(bin_count - 1U) / (max_as_double - min_as_double);
  for (std::size_t index = 0; index < input.size(); ++index) {
    const auto scaled = (static_cast<double>(input.data()[index]) - min_as_double) * scale;
    const auto bin = std::clamp(static_cast<std::size_t>(std::floor(scaled)), std::size_t{0}, bin_count - 1U);
    ++histogram[bin];
  }

  double weighted_sum = 0.0;
  for (std::size_t bin = 0; bin < bin_count; ++bin) {
    weighted_sum += static_cast<double>(bin) * static_cast<double>(histogram[bin]);
  }

  double background_sum = 0.0;
  std::size_t background_weight = 0U;
  double best_variance = -1.0;
  std::size_t best_bin = 0U;
  for (std::size_t bin = 0; bin < bin_count; ++bin) {
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

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> otsu_mask(const ksj::array::PooledImage<T>& input, const T low_value,
                                                   const T high_value) {
  const auto threshold_value = otsu_threshold(input);
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      output(row, col) = input(row, col) > threshold_value ? high_value : low_value;
    }
  }
  return output;
}

[[nodiscard]] std::vector<int> multi_otsu_thresholds_scaled_inplace(const ksj::array::ImageView<float> input,
                                                                    const std::size_t class_count,
                                                                    const std::size_t gray_levels) {
  if (input.empty()) {
    throw std::invalid_argument("multi_otsu_thresholds_scaled_inplace input must not be empty");
  }
  if (class_count < 2U || class_count > 5U) {
    throw std::invalid_argument("multi_otsu_thresholds_scaled_inplace supports 2 to 5 classes");
  }
  if (gray_levels < 2U) {
    throw std::invalid_argument("multi_otsu_thresholds_scaled_inplace requires at least two gray levels");
  }

  float max_value = input(0U, 0U);
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      max_value = std::max(max_value, input(row, col));
    }
  }

  std::vector<int> thresholds(class_count, 0);
  if (max_value <= 0.0F) {
    ksj::array::fill(input, 0.0F);
    return thresholds;
  }

  const auto last_gray = gray_levels - 1U;
  auto histogram = ksj::array::make_pooled_vector<float>(gray_levels);
  ksj::array::fill(histogram.view(), 0.0F);
  const auto scale = static_cast<float>(last_gray) / max_value;
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      const auto scaled = std::clamp(input(row, col) * scale, 0.0F, static_cast<float>(last_gray));
      input(row, col) = scaled;
      const auto bin =
        std::min(static_cast<std::size_t>(static_cast<int>(scaled + 0.5F)), static_cast<std::size_t>(last_gray));
      histogram(bin) += 1.0F;
    }
  }

  const auto pixel_count = static_cast<float>(input.rows() * input.cols());
  for (std::size_t bin = 0U; bin < histogram.size(); ++bin) {
    histogram(bin) /= pixel_count;
  }

  auto probability = ksj::array::make_pooled_matrix<float>(gray_levels, gray_levels);
  auto weighted_sum = ksj::array::make_pooled_matrix<float>(gray_levels, gray_levels);
  auto score = ksj::array::make_pooled_matrix<float>(gray_levels, gray_levels);
  ksj::array::fill(probability.view(), 0.0F);
  ksj::array::fill(weighted_sum.view(), 0.0F);
  ksj::array::fill(score.view(), 0.0F);

  for (std::size_t gray = 1U; gray < gray_levels; ++gray) {
    probability(gray, gray) = histogram(gray);
    weighted_sum(gray, gray) = static_cast<float>(gray) * histogram(gray);
  }

  for (std::size_t gray = 1U; gray + 1U < gray_levels; ++gray) {
    probability(1U, gray + 1U) = probability(1U, gray) + histogram(gray + 1U);
    weighted_sum(1U, gray + 1U) = weighted_sum(1U, gray) + static_cast<float>(gray + 1U) * histogram(gray + 1U);
  }

  for (std::size_t begin = 2U; begin < gray_levels; ++begin) {
    for (std::size_t end = begin + 1U; end < gray_levels; ++end) {
      probability(begin, end) = probability(1U, end) - probability(1U, begin - 1U);
      weighted_sum(begin, end) = weighted_sum(1U, end) - weighted_sum(1U, begin - 1U);
    }
  }

  for (std::size_t begin = 1U; begin < gray_levels; ++begin) {
    for (std::size_t end = begin + 1U; end < gray_levels; ++end) {
      score(begin, end) = probability(begin, end) != 0.0F
                            ? (weighted_sum(begin, end) * weighted_sum(begin, end)) / probability(begin, end)
                            : 0.0F;
    }
  }

  float best_score = 0.0F;
  switch (class_count) {
    case 2U:
      for (std::size_t i = 1U; i < gray_levels - class_count; ++i) {
        const auto candidate = score(1U, i) + score(i + 1U, last_gray);
        if (candidate > best_score) {
          thresholds[1U] = static_cast<int>(i);
          best_score = candidate;
        }
      }
      break;

    case 3U:
      for (std::size_t i = 1U; i < gray_levels - class_count; ++i) {
        for (std::size_t j = i + 1U; j < gray_levels - class_count + 1U; ++j) {
          const auto candidate = score(1U, i) + score(i + 1U, j) + score(j + 1U, last_gray);
          if (candidate > best_score) {
            thresholds[1U] = static_cast<int>(i);
            thresholds[2U] = static_cast<int>(j);
            best_score = candidate;
          }
        }
      }
      break;

    case 4U:
      for (std::size_t i = 1U; i < gray_levels - class_count; ++i) {
        for (std::size_t j = i + 1U; j < gray_levels - class_count + 1U; ++j) {
          for (std::size_t k = j + 1U; k < gray_levels - class_count + 2U; ++k) {
            const auto candidate = score(1U, i) + score(i + 1U, j) + score(j + 1U, k) + score(k + 1U, last_gray);
            if (candidate > best_score) {
              thresholds[1U] = static_cast<int>(i);
              thresholds[2U] = static_cast<int>(j);
              thresholds[3U] = static_cast<int>(k);
              best_score = candidate;
            }
          }
        }
      }
      break;

    case 5U:
      for (std::size_t i = 1U; i < gray_levels - class_count; ++i) {
        for (std::size_t j = i + 1U; j < gray_levels - class_count + 1U; ++j) {
          for (std::size_t k = j + 1U; k < gray_levels - class_count + 2U; ++k) {
            for (std::size_t m = k + 1U; m < gray_levels - class_count + 3U; ++m) {
              const auto candidate =
                score(1U, i) + score(i + 1U, j) + score(j + 1U, k) + score(k + 1U, m) + score(m + 1U, last_gray);
              if (candidate > best_score) {
                thresholds[1U] = static_cast<int>(i);
                thresholds[2U] = static_cast<int>(j);
                thresholds[3U] = static_cast<int>(k);
                thresholds[4U] = static_cast<int>(m);
                best_score = candidate;
              }
            }
          }
        }
      }
      break;
  }

  return thresholds;
}

#define KSJ_IMAGE_INSTANTIATE_THRESHOLDS(T)                                                                            \
  template ksj::array::PooledImage<T> threshold<T>(const ksj::array::PooledImage<T>&, T, T, T);                        \
  template ksj::array::PooledImage<T> normalize_minmax<T>(const ksj::array::PooledImage<T>&);                          \
  template T otsu_threshold<T>(const ksj::array::PooledImage<T>&);                                                     \
  template ksj::array::PooledImage<T> otsu_mask<T>(const ksj::array::PooledImage<T>&, T, T)

KSJ_IMAGE_INSTANTIATE_THRESHOLDS(float);
KSJ_IMAGE_INSTANTIATE_THRESHOLDS(double);
KSJ_IMAGE_INSTANTIATE_THRESHOLDS(int);
KSJ_IMAGE_INSTANTIATE_THRESHOLDS(std::int16_t);
KSJ_IMAGE_INSTANTIATE_THRESHOLDS(std::uint8_t);
KSJ_IMAGE_INSTANTIATE_THRESHOLDS(std::uint16_t);
KSJ_IMAGE_INSTANTIATE_THRESHOLDS(char);
#undef KSJ_IMAGE_INSTANTIATE_THRESHOLDS

} // namespace ksj::image::detail::eigen
