#pragma once

/// Shape-preserving reshape and flatten View helpers that do not alter row-major element order.

#include "kspacejet/array/dimensions.hpp"
#include "kspacejet/array/views.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::array {

namespace detail {

template <typename View> using view_data_t = std::remove_pointer_t<decltype(std::declval<View&>().data())>;

template <typename View> void validate_contiguous_reshape(View&& input) {
  if (!input.is_contiguous()) {
    throw std::invalid_argument("reshape_view requires contiguous storage");
  }
}

template <typename View> void validate_reshape_shape(View&& input, const std::size_t element_count) {
  validate_contiguous_reshape(input);
  if (input.size() != element_count) {
    throw std::invalid_argument("reshape_view cannot change the element count");
  }
}

} // namespace detail

template <typename View> [[nodiscard]] auto flatten_view(View&& input) {
  detail::validate_contiguous_reshape(input);
  return VectorView<detail::view_data_t<View>>(input.data(), input.size());
}

template <typename View> [[nodiscard]] auto ravel(View&& input) {
  return flatten_view(std::forward<View>(input));
}

template <typename View> [[nodiscard]] auto reshape_view(View&& input, const Shape<1U> shape) {
  detail::validate_reshape_shape(input, shape.element_count());
  return VectorView<detail::view_data_t<View>>(input.data(), shape[0U]);
}

template <typename View> [[nodiscard]] auto reshape_view(View&& input, const Shape<2U> shape) {
  detail::validate_reshape_shape(input, shape.element_count());
  return MatrixView<detail::view_data_t<View>>(input.data(), shape[0U], shape[1U]);
}

template <typename View> [[nodiscard]] auto reshape_view(View&& input, const Shape<3U> shape) {
  detail::validate_reshape_shape(input, shape.element_count());
  return CubeView<detail::view_data_t<View>>(input.data(), shape[0U], shape[1U], shape[2U]);
}

template <typename View> [[nodiscard]] auto reshape_view(View&& input, const Shape<4U> shape) {
  detail::validate_reshape_shape(input, shape.element_count());
  return Array4DView<detail::view_data_t<View>>(input.data(), shape[0U], shape[1U], shape[2U], shape[3U]);
}

} // namespace ksj::array
