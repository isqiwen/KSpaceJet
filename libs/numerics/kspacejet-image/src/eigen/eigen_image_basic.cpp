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
[[nodiscard]] bool aliases_image_storage(const ksj::array::PooledImage<T>& input,
                                         const ksj::array::PooledImage<T>& output) noexcept {
  return !input.empty() && !output.empty() && input.data() == output.data();
}

template <typename T>
void copy_image_storage(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("copy image storage dimension mismatch");
  }
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      output(row, col) = input(row, col);
    }
  }
}

template <typename T>
void copy_image_view(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("copy image view dimension mismatch");
  }
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      output(row, col) = input(row, col);
    }
  }
}

template <typename T> void copy_image_view(const ksj::array::ImageView<T> input, ksj::array::ImageView<T> output) {
  copy_image_view(ksj::array::as_const_view(input), output);
}

#define KSJ_IMAGE_INSTANTIATE_STORAGE(T)                                                                               \
  template bool aliases_image_storage<T>(const ksj::array::PooledImage<T>&,                                            \
                                         const ksj::array::PooledImage<T>&) noexcept;                                  \
  template void copy_image_storage<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&);                 \
  template void copy_image_view<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>);                          \
  template void copy_image_view<T>(ksj::array::ImageView<T>, ksj::array::ImageView<T>)

KSJ_IMAGE_INSTANTIATE_STORAGE(float);
KSJ_IMAGE_INSTANTIATE_STORAGE(double);
KSJ_IMAGE_INSTANTIATE_STORAGE(int);
KSJ_IMAGE_INSTANTIATE_STORAGE(std::int16_t);
KSJ_IMAGE_INSTANTIATE_STORAGE(std::uint8_t);
KSJ_IMAGE_INSTANTIATE_STORAGE(std::uint16_t);
KSJ_IMAGE_INSTANTIATE_STORAGE(char);
#undef KSJ_IMAGE_INSTANTIATE_STORAGE

} // namespace ksj::image::detail::eigen
