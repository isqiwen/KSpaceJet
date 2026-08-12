#pragma once

/// Connected-component labeling and component measurement operations for binary and labeled images.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/common.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_basic.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_components.hpp"
#include "kspacejet/image/detail/image_policy.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_components.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::image {

template <typename T>
std::size_t connected_components(const ksj::array::ImageView<const T> input,
                                 ksj::array::ImageView<ConnectedComponentLabel> labels,
                                 std::vector<ConnectedComponentStats>* stats = nullptr,
                                 const Connectivity connectivity = Connectivity::eight) {
  return detail::eigen::connected_components_alias_safe(input, labels, stats, connectivity);
}

template <typename T>
std::size_t connected_components(const ksj::array::PooledImage<T>& input,
                                 ksj::array::PooledImage<ConnectedComponentLabel>& labels,
                                 std::vector<ConnectedComponentStats>* stats = nullptr,
                                 const Connectivity connectivity = Connectivity::eight) {
  if constexpr (std::is_same_v<T, ConnectedComponentLabel>) {
    if (detail::eigen::aliases_image_storage(input, labels)) {
      auto temp = ksj::array::make_pooled_image<ConnectedComponentLabel>(labels.rows(), labels.cols());
      const auto component_count = connected_components(input, temp, stats, connectivity);
      detail::eigen::copy_image_storage(temp, labels);
      return component_count;
    }
  }

  std::size_t component_count = 0U;
  if (detail::prefer_opencv_connected_components<T>(input.size()) &&
      detail::opencv::connected_components(input, labels, stats, connectivity, component_count)) {
    return component_count;
  }

  return connected_components(ksj::array::image_view(input), ksj::array::image_view(labels), stats, connectivity);
}

template <typename T>
[[nodiscard]] ConnectedComponentsResult connected_components(const ksj::array::PooledImage<T>& input,
                                                             const Connectivity connectivity = Connectivity::eight) {
  ConnectedComponentsResult result;
  result.labels = ksj::array::make_pooled_image<ConnectedComponentLabel>(input.rows(), input.cols());
  connected_components(input, result.labels, &result.stats, connectivity);
  return result;
}

template <typename T>
[[nodiscard]] ConnectedComponentsResult connected_components(const ksj::array::ImageView<const T> input,
                                                             const Connectivity connectivity = Connectivity::eight) {
  ConnectedComponentsResult result;
  result.labels = ksj::array::make_pooled_image<ConnectedComponentLabel>(input.rows(), input.cols());
  connected_components(input, ksj::array::image_view(result.labels), &result.stats, connectivity);
  return result;
}

template <typename T>
std::size_t region_grow(ksj::array::ImageView<const T> input, ksj::array::ImageView<RegionGrowMaskValue> mask,
                        const std::size_t seed_row, const std::size_t seed_col, const T lower_threshold,
                        const T upper_threshold, const Connectivity connectivity = Connectivity::four) {
  detail::validate_region_grow_connectivity(connectivity);
  detail::validate_image_shape(input, mask, "region_grow mask dimension mismatch");
  if (lower_threshold > upper_threshold) {
    throw std::invalid_argument("region_grow lower threshold must not exceed upper threshold");
  }

  if constexpr (std::is_same_v<T, RegionGrowMaskValue>) {
    if (detail::aliases_image_view_storage(input, mask)) {
      auto temp = ksj::array::make_pooled_image<RegionGrowMaskValue>(mask.rows(), mask.cols());
      const auto area =
        region_grow(input, temp.view(), seed_row, seed_col, lower_threshold, upper_threshold, connectivity);
      ksj::array::copy(temp.view(), mask);
      return area;
    }
  }

  ksj::array::fill(mask, RegionGrowMaskValue{});
  if (input.empty()) {
    throw std::invalid_argument("region_grow input must not be empty");
  }
  if (seed_row >= input.rows() || seed_col >= input.cols()) {
    throw std::invalid_argument("region_grow seed is outside image bounds");
  }

  const auto in_range = [&](const std::size_t row, const std::size_t col) {
    const auto value = input(row, col);
    return value >= lower_threshold && value <= upper_threshold;
  };
  if (!in_range(seed_row, seed_col)) {
    return 0U;
  }

  auto stack = ksj::array::make_pooled_vector<std::pair<std::size_t, std::size_t>>(input.size());
  std::size_t stack_size = 0U;
  stack(stack_size++) = {seed_row, seed_col};
  mask(seed_row, seed_col) = RegionGrowMaskValue{1};
  std::size_t area = 0U;

  const auto push_if_valid = [&](const long row, const long col) {
    if (row < 0 || col < 0 || row >= static_cast<long>(input.rows()) || col >= static_cast<long>(input.cols())) {
      return;
    }
    const auto row_index = static_cast<std::size_t>(row);
    const auto col_index = static_cast<std::size_t>(col);
    if (mask(row_index, col_index) != RegionGrowMaskValue{} || !in_range(row_index, col_index)) {
      return;
    }
    mask(row_index, col_index) = RegionGrowMaskValue{1};
    stack(stack_size++) = {row_index, col_index};
  };

  while (stack_size != 0U) {
    const auto [row, col] = stack(--stack_size);
    ++area;

    const auto base_row = static_cast<long>(row);
    const auto base_col = static_cast<long>(col);
    push_if_valid(base_row - 1L, base_col);
    push_if_valid(base_row + 1L, base_col);
    push_if_valid(base_row, base_col - 1L);
    push_if_valid(base_row, base_col + 1L);
    if (connectivity == Connectivity::eight) {
      push_if_valid(base_row - 1L, base_col - 1L);
      push_if_valid(base_row - 1L, base_col + 1L);
      push_if_valid(base_row + 1L, base_col - 1L);
      push_if_valid(base_row + 1L, base_col + 1L);
    }
  }

  return area;
}

template <typename T>
std::size_t region_grow(ksj::array::ImageView<T> input, ksj::array::ImageView<RegionGrowMaskValue> mask,
                        const std::size_t seed_row, const std::size_t seed_col, const T lower_threshold,
                        const T upper_threshold, const Connectivity connectivity = Connectivity::four) {
  return region_grow(ksj::array::as_const_view(input), mask, seed_row, seed_col, lower_threshold, upper_threshold,
                     connectivity);
}

template <typename T>
std::size_t region_grow(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<RegionGrowMaskValue>& mask,
                        const std::size_t seed_row, const std::size_t seed_col, const T lower_threshold,
                        const T upper_threshold, const Connectivity connectivity = Connectivity::four) {
  return region_grow(input.view(), mask.view(), seed_row, seed_col, lower_threshold, upper_threshold, connectivity);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<RegionGrowMaskValue>
region_grow(ksj::array::ImageView<const T> input, const std::size_t seed_row, const std::size_t seed_col,
            const T lower_threshold, const T upper_threshold, const Connectivity connectivity = Connectivity::four) {
  auto mask = ksj::array::make_pooled_image<RegionGrowMaskValue>(input.rows(), input.cols());
  region_grow(input, mask.view(), seed_row, seed_col, lower_threshold, upper_threshold, connectivity);
  return mask;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<RegionGrowMaskValue>
region_grow(ksj::array::ImageView<T> input, const std::size_t seed_row, const std::size_t seed_col,
            const T lower_threshold, const T upper_threshold, const Connectivity connectivity = Connectivity::four) {
  return region_grow(ksj::array::as_const_view(input), seed_row, seed_col, lower_threshold, upper_threshold,
                     connectivity);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<RegionGrowMaskValue>
region_grow(const ksj::array::PooledImage<T>& input, const std::size_t seed_row, const std::size_t seed_col,
            const T lower_threshold, const T upper_threshold, const Connectivity connectivity = Connectivity::four) {
  return region_grow(input.view(), seed_row, seed_col, lower_threshold, upper_threshold, connectivity);
}

} // namespace ksj::image
