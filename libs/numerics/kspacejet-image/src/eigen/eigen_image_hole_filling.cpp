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
void mark_background_component(ksj::array::ImageView<const T> input, ksj::array::PooledVector<unsigned char>& visited,
                               const std::size_t seed_row, const std::size_t seed_col) {
  const auto index_of = [cols = input.cols()](const std::size_t row, const std::size_t col) {
    return row * cols + col;
  };
  if (input(seed_row, seed_col) != T{} || visited(index_of(seed_row, seed_col)) != 0U) {
    return;
  }

  auto stack = ksj::array::make_pooled_vector<std::pair<std::size_t, std::size_t>>(input.size());
  std::size_t stack_size = 0U;
  stack(stack_size++) = {seed_row, seed_col};
  visited(index_of(seed_row, seed_col)) = 1U;
  while (stack_size != 0U) {
    const auto [row, col] = stack(--stack_size);

    const auto push = [&](const std::size_t next_row, const std::size_t next_col) {
      const auto next_index = index_of(next_row, next_col);
      if (visited(next_index) == 0U && input(next_row, next_col) == T{}) {
        visited(next_index) = 1U;
        stack(stack_size++) = {next_row, next_col};
      }
    };

    if (row > 0U) {
      push(row - 1U, col);
    }
    if (row + 1U < input.rows()) {
      push(row + 1U, col);
    }
    if (col > 0U) {
      push(row, col - 1U);
    }
    if (col + 1U < input.cols()) {
      push(row, col + 1U);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<unsigned char> mark_exterior_background(ksj::array::ImageView<const T> input) {
  auto visited = ksj::array::make_pooled_vector<unsigned char>(input.size());
  ksj::array::fill(visited.view(), static_cast<unsigned char>(0U));
  if (input.empty()) {
    return visited;
  }

  for (std::size_t col = 0; col < input.cols(); ++col) {
    mark_background_component(input, visited, 0U, col);
    mark_background_component(input, visited, input.rows() - 1U, col);
  }
  for (std::size_t row = 0; row < input.rows(); ++row) {
    mark_background_component(input, visited, row, 0U);
    mark_background_component(input, visited, row, input.cols() - 1U);
  }
  return visited;
}

template <typename T>
void fill_holes_from_visited_background(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                                        const ksj::array::PooledVector<unsigned char>& visited, const T fill_value) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("fill_holes output dimension mismatch");
  }

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      const auto index = row * input.cols() + col;
      output(row, col) = (input(row, col) != T{} || visited(index) == 0U) ? fill_value : T{};
    }
  }
}

template <typename T>
void fill_holes(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const HoleFillMode mode,
                const T fill_value) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("fill_holes output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  if (mode == HoleFillMode::corner_pair && input(0U, 0U) == T{} && input(input.rows() - 1U, input.cols() - 1U) == T{}) {
    auto top_left = ksj::array::make_pooled_vector<unsigned char>(input.size());
    auto bottom_right = ksj::array::make_pooled_vector<unsigned char>(input.size());
    ksj::array::fill(top_left.view(), static_cast<unsigned char>(0U));
    ksj::array::fill(bottom_right.view(), static_cast<unsigned char>(0U));
    mark_background_component(input, top_left, 0U, 0U);
    mark_background_component(input, bottom_right, input.rows() - 1U, input.cols() - 1U);
    for (std::size_t row = 0; row < input.rows(); ++row) {
      for (std::size_t col = 0; col < input.cols(); ++col) {
        const auto index = row * input.cols() + col;
        output(row, col) =
          (input(row, col) != T{} || (top_left(index) == 0U && bottom_right(index) == 0U)) ? fill_value : T{};
      }
    }
    return;
  }

  fill_holes_from_visited_background(input, output, mark_exterior_background(input), fill_value);
}

template <typename T>
void fill_holes(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const HoleFillMode mode,
                const T fill_value) {
  ::ksj::image::detail::eigen::fill_holes(ksj::array::as_const_view(input.view()), output.view(), mode, fill_value);
}

#define KSJ_IMAGE_INSTANTIATE_HOLE_FILLING(T)                                                                          \
  template void fill_holes<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, HoleFillMode, T);              \
  template void fill_holes<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, HoleFillMode, T)

KSJ_IMAGE_INSTANTIATE_HOLE_FILLING(float);
KSJ_IMAGE_INSTANTIATE_HOLE_FILLING(double);
KSJ_IMAGE_INSTANTIATE_HOLE_FILLING(int);
KSJ_IMAGE_INSTANTIATE_HOLE_FILLING(std::int16_t);
KSJ_IMAGE_INSTANTIATE_HOLE_FILLING(std::uint8_t);
KSJ_IMAGE_INSTANTIATE_HOLE_FILLING(std::uint16_t);
KSJ_IMAGE_INSTANTIATE_HOLE_FILLING(char);
#undef KSJ_IMAGE_INSTANTIATE_HOLE_FILLING

} // namespace ksj::image::detail::eigen
