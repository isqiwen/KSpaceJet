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

void validate_connectivity(const Connectivity connectivity) {
  if (connectivity != Connectivity::four && connectivity != Connectivity::eight) {
    throw std::invalid_argument("connected_components connectivity must be four or eight");
  }
}

template <typename T>
std::size_t connected_components(const ksj::array::ImageView<const T> input,
                                 ksj::array::ImageView<ConnectedComponentLabel> labels,
                                 std::vector<ConnectedComponentStats>* stats, const Connectivity connectivity) {
  validate_connectivity(connectivity);
  if (labels.rows() != input.rows() || labels.cols() != input.cols()) {
    throw std::invalid_argument("connected_components label dimension mismatch");
  }
  if (stats != nullptr) {
    stats->clear();
  }
  for (std::size_t row = 0; row < labels.rows(); ++row) {
    for (std::size_t col = 0; col < labels.cols(); ++col) {
      labels(row, col) = ConnectedComponentLabel{};
    }
  }
  if (input.empty()) {
    return 0U;
  }

  const auto max_label = std::numeric_limits<ConnectedComponentLabel>::max();
  ConnectedComponentLabel current_label = 0;
  auto stack = ksj::array::make_pooled_vector<std::pair<std::size_t, std::size_t>>(input.size());
  std::size_t stack_size = 0U;

  const auto push_if_foreground = [&](const long row, const long col) {
    if (row < 0 || col < 0 || row >= static_cast<long>(input.rows()) || col >= static_cast<long>(input.cols())) {
      return;
    }
    const auto row_index = static_cast<std::size_t>(row);
    const auto col_index = static_cast<std::size_t>(col);
    if (labels(row_index, col_index) != ConnectedComponentLabel{} || input(row_index, col_index) == T{}) {
      return;
    }
    labels(row_index, col_index) = current_label;
    stack(stack_size++) = {row_index, col_index};
  };

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      if (input(row, col) == T{} || labels(row, col) != ConnectedComponentLabel{}) {
        continue;
      }
      if (current_label == max_label) {
        throw std::overflow_error("connected_components exceeded label capacity");
      }
      ++current_label;

      ConnectedComponentStats component{};
      component.label = current_label;
      component.min_row = row;
      component.max_row = row;
      component.min_col = col;
      component.max_col = col;
      double row_sum = 0.0;
      double col_sum = 0.0;

      labels(row, col) = current_label;
      stack_size = 0U;
      stack(stack_size++) = {row, col};
      while (stack_size != 0U) {
        const auto [current_row, current_col] = stack(--stack_size);

        ++component.area;
        component.min_row = std::min(component.min_row, current_row);
        component.max_row = std::max(component.max_row, current_row);
        component.min_col = std::min(component.min_col, current_col);
        component.max_col = std::max(component.max_col, current_col);
        row_sum += static_cast<double>(current_row);
        col_sum += static_cast<double>(current_col);

        const auto base_row = static_cast<long>(current_row);
        const auto base_col = static_cast<long>(current_col);
        push_if_foreground(base_row - 1L, base_col);
        push_if_foreground(base_row + 1L, base_col);
        push_if_foreground(base_row, base_col - 1L);
        push_if_foreground(base_row, base_col + 1L);
        if (connectivity == Connectivity::eight) {
          push_if_foreground(base_row - 1L, base_col - 1L);
          push_if_foreground(base_row - 1L, base_col + 1L);
          push_if_foreground(base_row + 1L, base_col - 1L);
          push_if_foreground(base_row + 1L, base_col + 1L);
        }
      }

      component.centroid_row = row_sum / static_cast<double>(component.area);
      component.centroid_col = col_sum / static_cast<double>(component.area);
      if (stats != nullptr) {
        stats->push_back(component);
      }
    }
  }

  return static_cast<std::size_t>(current_label);
}

template <typename T>
std::size_t connected_components(const ksj::array::PooledImage<T>& input,
                                 ksj::array::PooledImage<ConnectedComponentLabel>& labels,
                                 std::vector<ConnectedComponentStats>* stats, const Connectivity connectivity) {
  return ::ksj::image::detail::eigen::connected_components(ksj::array::image_view(input),
                                                           ksj::array::image_view(labels), stats, connectivity);
}

template <typename T>
std::size_t connected_components_alias_safe(const ksj::array::ImageView<const T> input,
                                            ksj::array::ImageView<ConnectedComponentLabel> labels,
                                            std::vector<ConnectedComponentStats>* stats,
                                            const Connectivity connectivity) {
  if constexpr (std::is_same_v<T, ConnectedComponentLabel>) {
    if (input.data() == labels.data()) {
      auto temp = ksj::array::make_pooled_image<ConnectedComponentLabel>(labels.rows(), labels.cols());
      const auto component_count =
        ::ksj::image::detail::eigen::connected_components(input, ksj::array::image_view(temp), stats, connectivity);
      copy_image_view(ksj::array::image_view(temp), labels);
      return component_count;
    }
  }

  return ::ksj::image::detail::eigen::connected_components(input, labels, stats, connectivity);
}

template <typename T>
[[nodiscard]] ConnectedComponentsResult connected_components(const ksj::array::PooledImage<T>& input,
                                                             const Connectivity connectivity) {
  ConnectedComponentsResult result;
  result.labels = ksj::array::make_pooled_image<ConnectedComponentLabel>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::connected_components(input, result.labels, &result.stats, connectivity);
  return result;
}

template <typename T>
std::size_t region_grow(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<RegionGrowMaskValue>& mask,
                        const std::size_t seed_row, const std::size_t seed_col, const T lower_threshold,
                        const T upper_threshold, const Connectivity connectivity) {
  validate_connectivity(connectivity);
  if (mask.rows() != input.rows() || mask.cols() != input.cols()) {
    throw std::invalid_argument("region_grow mask dimension mismatch");
  }
  if (lower_threshold > upper_threshold) {
    throw std::invalid_argument("region_grow lower threshold must not exceed upper threshold");
  }
  std::fill(mask.data(), mask.data() + mask.size(), RegionGrowMaskValue{});
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
[[nodiscard]] ksj::array::PooledImage<RegionGrowMaskValue>
region_grow(const ksj::array::PooledImage<T>& input, const std::size_t seed_row, const std::size_t seed_col,
            const T lower_threshold, const T upper_threshold, const Connectivity connectivity) {
  auto mask = ksj::array::make_pooled_image<RegionGrowMaskValue>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::region_grow(input, mask, seed_row, seed_col, lower_threshold, upper_threshold,
                                           connectivity);
  return mask;
}

#define KSJ_IMAGE_INSTANTIATE_COMPONENTS(T)                                                                            \
  template std::size_t connected_components<T>(ksj::array::ImageView<const T>,                                         \
                                               ksj::array::ImageView<ConnectedComponentLabel>,                         \
                                               std::vector<ConnectedComponentStats>*, Connectivity);                   \
  template std::size_t connected_components<T>(const ksj::array::PooledImage<T>&,                                      \
                                               ksj::array::PooledImage<ConnectedComponentLabel>&,                      \
                                               std::vector<ConnectedComponentStats>*, Connectivity);                   \
  template std::size_t connected_components_alias_safe<T>(ksj::array::ImageView<const T>,                              \
                                                          ksj::array::ImageView<ConnectedComponentLabel>,              \
                                                          std::vector<ConnectedComponentStats>*, Connectivity);        \
  template ConnectedComponentsResult connected_components<T>(const ksj::array::PooledImage<T>&, Connectivity);         \
  template std::size_t region_grow<T>(const ksj::array::PooledImage<T>&,                                               \
                                      ksj::array::PooledImage<RegionGrowMaskValue>&, std::size_t, std::size_t, T, T,   \
                                      Connectivity);                                                                   \
  template ksj::array::PooledImage<RegionGrowMaskValue> region_grow<T>(const ksj::array::PooledImage<T>&, std::size_t, \
                                                                       std::size_t, T, T, Connectivity)

KSJ_IMAGE_INSTANTIATE_COMPONENTS(float);
KSJ_IMAGE_INSTANTIATE_COMPONENTS(double);
KSJ_IMAGE_INSTANTIATE_COMPONENTS(int);
KSJ_IMAGE_INSTANTIATE_COMPONENTS(std::int16_t);
KSJ_IMAGE_INSTANTIATE_COMPONENTS(std::uint8_t);
KSJ_IMAGE_INSTANTIATE_COMPONENTS(std::uint16_t);
KSJ_IMAGE_INSTANTIATE_COMPONENTS(char);
#undef KSJ_IMAGE_INSTANTIATE_COMPONENTS

} // namespace ksj::image::detail::eigen
