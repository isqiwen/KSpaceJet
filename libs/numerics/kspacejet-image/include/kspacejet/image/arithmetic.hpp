#pragma once

/// Pixelwise image arithmetic with shape validation; elementwise operations delegate backend selection to
/// kspacejet-array.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/common.hpp"

#include <type_traits>

namespace ksj::image {

namespace detail {

template <typename T> [[nodiscard]] auto make_arithmetic_output_like(ksj::array::ImageView<T> input) {
  return ksj::array::make_pooled_image<std::remove_const_t<T>>(input.rows(), input.cols());
}

} // namespace detail

template <typename T>
void cwise_add(ksj::array::ImageView<const T> lhs, ksj::array::ImageView<const T> rhs,
               ksj::array::ImageView<T> output) {
  detail::validate_image_shape(lhs, rhs, "cwise_add input dimension mismatch");
  detail::validate_image_shape(lhs, output, "cwise_add output dimension mismatch");
  ksj::array::add(lhs, rhs, output);
}

template <typename T>
void cwise_subtract(ksj::array::ImageView<const T> lhs, ksj::array::ImageView<const T> rhs,
                    ksj::array::ImageView<T> output) {
  detail::validate_image_shape(lhs, rhs, "cwise_subtract input dimension mismatch");
  detail::validate_image_shape(lhs, output, "cwise_subtract output dimension mismatch");
  ksj::array::subtract(lhs, rhs, output);
}

template <typename T>
void cwise_multiply(ksj::array::ImageView<const T> lhs, ksj::array::ImageView<const T> rhs,
                    ksj::array::ImageView<T> output) {
  detail::validate_image_shape(lhs, rhs, "cwise_multiply input dimension mismatch");
  detail::validate_image_shape(lhs, output, "cwise_multiply output dimension mismatch");
  ksj::array::multiply(lhs, rhs, output);
}

template <typename T>
void cwise_divide(ksj::array::ImageView<const T> lhs, ksj::array::ImageView<const T> rhs,
                  ksj::array::ImageView<T> output) {
  detail::validate_image_shape(lhs, rhs, "cwise_divide input dimension mismatch");
  detail::validate_image_shape(lhs, output, "cwise_divide output dimension mismatch");
  ksj::array::divide(lhs, rhs, output);
}

template <typename T, typename Scalar>
void cwise_add_scalar(ksj::array::ImageView<const T> input, const Scalar& scalar, ksj::array::ImageView<T> output) {
  detail::validate_image_shape(input, output, "cwise_add_scalar output dimension mismatch");
  const auto converted = static_cast<T>(scalar);
  ksj::array::add_scalar(input, converted, output);
}

template <typename T, typename Scalar>
void cwise_subtract_scalar(ksj::array::ImageView<const T> input, const Scalar& scalar,
                           ksj::array::ImageView<T> output) {
  detail::validate_image_shape(input, output, "cwise_subtract_scalar output dimension mismatch");
  const auto converted = static_cast<T>(scalar);
  ksj::array::subtract_scalar(input, converted, output);
}

template <typename T, typename Scalar>
void cwise_multiply_scalar(ksj::array::ImageView<const T> input, const Scalar& scalar,
                           ksj::array::ImageView<T> output) {
  detail::validate_image_shape(input, output, "cwise_multiply_scalar output dimension mismatch");
  const auto converted = static_cast<T>(scalar);
  ksj::array::scale(input, converted, output);
}

template <typename T, typename Scalar>
void cwise_divide_scalar(ksj::array::ImageView<const T> input, const Scalar& scalar, ksj::array::ImageView<T> output) {
  detail::validate_image_shape(input, output, "cwise_divide_scalar output dimension mismatch");
  const auto converted = static_cast<T>(scalar);
  ksj::array::divide_scalar(input, converted, output);
}

template <typename T> void cwise_abs(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) {
  detail::validate_image_shape(input, output, "cwise_abs output dimension mismatch");
  ksj::array::absolute(input, output);
}

template <typename T> void cwise_square(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) {
  detail::validate_image_shape(input, output, "cwise_square output dimension mismatch");
  ksj::array::square(input, output);
}

template <typename T> void cwise_sqrt(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) {
  detail::validate_image_shape(input, output, "cwise_sqrt output dimension mismatch");
  ksj::array::sqrt(input, output);
}

template <typename T> void copy(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) {
  detail::validate_image_shape(input, output, "copy output dimension mismatch");
  ksj::array::copy(input, output);
}

template <typename T, typename Value> void fill(ksj::array::ImageView<T> output, const Value& value) {
  const auto converted = static_cast<T>(value);
  ksj::array::fill(output, converted);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cwise_add(ksj::array::ImageView<const T> lhs,
                                                   ksj::array::ImageView<const T> rhs) {
  auto output = detail::make_arithmetic_output_like(lhs);
  cwise_add(lhs, rhs, output.view());
  return output;
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_add(ksj::array::ImageView<T> lhs, ksj::array::ImageView<T> rhs) {
  return cwise_add(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cwise_add(const ksj::array::PooledImage<T>& lhs,
                                                   const ksj::array::PooledImage<T>& rhs) {
  return cwise_add(lhs.view(), rhs.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cwise_subtract(ksj::array::ImageView<const T> lhs,
                                                        ksj::array::ImageView<const T> rhs) {
  auto output = detail::make_arithmetic_output_like(lhs);
  cwise_subtract(lhs, rhs, output.view());
  return output;
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_subtract(ksj::array::ImageView<T> lhs, ksj::array::ImageView<T> rhs) {
  return cwise_subtract(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cwise_subtract(const ksj::array::PooledImage<T>& lhs,
                                                        const ksj::array::PooledImage<T>& rhs) {
  return cwise_subtract(lhs.view(), rhs.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cwise_multiply(ksj::array::ImageView<const T> lhs,
                                                        ksj::array::ImageView<const T> rhs) {
  auto output = detail::make_arithmetic_output_like(lhs);
  cwise_multiply(lhs, rhs, output.view());
  return output;
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_multiply(ksj::array::ImageView<T> lhs, ksj::array::ImageView<T> rhs) {
  return cwise_multiply(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cwise_multiply(const ksj::array::PooledImage<T>& lhs,
                                                        const ksj::array::PooledImage<T>& rhs) {
  return cwise_multiply(lhs.view(), rhs.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cwise_divide(ksj::array::ImageView<const T> lhs,
                                                      ksj::array::ImageView<const T> rhs) {
  auto output = detail::make_arithmetic_output_like(lhs);
  cwise_divide(lhs, rhs, output.view());
  return output;
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_divide(ksj::array::ImageView<T> lhs, ksj::array::ImageView<T> rhs) {
  return cwise_divide(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> cwise_divide(const ksj::array::PooledImage<T>& lhs,
                                                      const ksj::array::PooledImage<T>& rhs) {
  return cwise_divide(lhs.view(), rhs.view());
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledImage<T> cwise_add_scalar(ksj::array::ImageView<const T> input, const Scalar& scalar) {
  auto output = detail::make_arithmetic_output_like(input);
  cwise_add_scalar(input, scalar, output.view());
  return output;
}

template <typename T, typename Scalar>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_add_scalar(ksj::array::ImageView<T> input, const Scalar& scalar) {
  return cwise_add_scalar(ksj::array::as_const_view(input), scalar);
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledImage<T> cwise_add_scalar(const ksj::array::PooledImage<T>& input,
                                                          const Scalar& scalar) {
  return cwise_add_scalar(input.view(), scalar);
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledImage<T> cwise_subtract_scalar(ksj::array::ImageView<const T> input,
                                                               const Scalar& scalar) {
  auto output = detail::make_arithmetic_output_like(input);
  cwise_subtract_scalar(input, scalar, output.view());
  return output;
}

template <typename T, typename Scalar>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_subtract_scalar(ksj::array::ImageView<T> input, const Scalar& scalar) {
  return cwise_subtract_scalar(ksj::array::as_const_view(input), scalar);
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledImage<T> cwise_subtract_scalar(const ksj::array::PooledImage<T>& input,
                                                               const Scalar& scalar) {
  return cwise_subtract_scalar(input.view(), scalar);
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledImage<T> cwise_multiply_scalar(ksj::array::ImageView<const T> input,
                                                               const Scalar& scalar) {
  auto output = detail::make_arithmetic_output_like(input);
  cwise_multiply_scalar(input, scalar, output.view());
  return output;
}

template <typename T, typename Scalar>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_multiply_scalar(ksj::array::ImageView<T> input, const Scalar& scalar) {
  return cwise_multiply_scalar(ksj::array::as_const_view(input), scalar);
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledImage<T> cwise_multiply_scalar(const ksj::array::PooledImage<T>& input,
                                                               const Scalar& scalar) {
  return cwise_multiply_scalar(input.view(), scalar);
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledImage<T> cwise_divide_scalar(ksj::array::ImageView<const T> input,
                                                             const Scalar& scalar) {
  auto output = detail::make_arithmetic_output_like(input);
  cwise_divide_scalar(input, scalar, output.view());
  return output;
}

template <typename T, typename Scalar>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_divide_scalar(ksj::array::ImageView<T> input, const Scalar& scalar) {
  return cwise_divide_scalar(ksj::array::as_const_view(input), scalar);
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledImage<T> cwise_divide_scalar(const ksj::array::PooledImage<T>& input,
                                                             const Scalar& scalar) {
  return cwise_divide_scalar(input.view(), scalar);
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> cwise_abs(ksj::array::ImageView<const T> input) {
  auto output = detail::make_arithmetic_output_like(input);
  cwise_abs(input, output.view());
  return output;
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_abs(ksj::array::ImageView<T> input) {
  return cwise_abs(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> cwise_abs(const ksj::array::PooledImage<T>& input) {
  return cwise_abs(input.view());
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> cwise_square(ksj::array::ImageView<const T> input) {
  auto output = detail::make_arithmetic_output_like(input);
  cwise_square(input, output.view());
  return output;
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_square(ksj::array::ImageView<T> input) {
  return cwise_square(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> cwise_square(const ksj::array::PooledImage<T>& input) {
  return cwise_square(input.view());
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> cwise_sqrt(ksj::array::ImageView<const T> input) {
  auto output = detail::make_arithmetic_output_like(input);
  cwise_sqrt(input, output.view());
  return output;
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> cwise_sqrt(ksj::array::ImageView<T> input) {
  return cwise_sqrt(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> cwise_sqrt(const ksj::array::PooledImage<T>& input) {
  return cwise_sqrt(input.view());
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> copy(ksj::array::ImageView<const T> input) {
  auto output = detail::make_arithmetic_output_like(input);
  copy(input, output.view());
  return output;
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] ksj::array::PooledImage<T> copy(ksj::array::ImageView<T> input) {
  return copy(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> copy(const ksj::array::PooledImage<T>& input) {
  return copy(input.view());
}

} // namespace ksj::image
