#pragma once

/// Special functions for scalars and dense arrays. View overloads write explicit output; return overloads allocate one
/// Pooled result.

#include "kspacejet/array/array.hpp"

#include "kspacejet/special/detail/eigen/eigen_special_functions.hpp"
#include "kspacejet/special/detail/intel/intel_special_functions.hpp"
#include "kspacejet/special/detail/special_policy.hpp"

#include <concepts>
#include <complex>
#include <cstddef>
#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::special {
namespace detail {

template <typename T>
inline constexpr bool supported_real_special_v =
  std::is_same_v<std::remove_cv_t<T>, float> || std::is_same_v<std::remove_cv_t<T>, double>;

template <typename T> using real_special_result_t = std::conditional_t<std::is_same_v<T, float>, float, double>;

struct vector_view_tag {};
struct matrix_view_tag {};
struct image_view_tag {};
struct cube_view_tag {};
struct array4d_view_tag {};

template <typename View> struct special_view_traits {};

template <typename T> struct special_view_traits<ksj::array::VectorView<T>> {
  using tag = vector_view_tag;
  using value_type = typename ksj::array::VectorView<T>::value_type;

  template <typename OutputT> using output_view_type = ksj::array::VectorView<OutputT>;
  template <typename OutputT> using pooled_type = ksj::array::PooledVector<OutputT>;

  template <typename OutputT> [[nodiscard]] static pooled_type<OutputT> make_output(ksj::array::VectorView<T> input) {
    return ksj::array::make_pooled_vector<OutputT>(input.size());
  }

  template <typename Lhs, typename Rhs> static void validate_same_shape(Lhs lhs, Rhs rhs, const char* message) {
    ksj::array::detail::validate_same_size(lhs.size(), rhs.size(), message);
  }
};

template <typename T> struct special_view_traits<ksj::array::MatrixView<T>> {
  using tag = matrix_view_tag;
  using value_type = typename ksj::array::MatrixView<T>::value_type;

  template <typename OutputT> using output_view_type = ksj::array::MatrixView<OutputT>;
  template <typename OutputT> using pooled_type = ksj::array::PooledMatrix<OutputT>;

  template <typename OutputT> [[nodiscard]] static pooled_type<OutputT> make_output(ksj::array::MatrixView<T> input) {
    return ksj::array::make_pooled_matrix<OutputT>(input.rows(), input.cols());
  }

  template <typename Lhs, typename Rhs> static void validate_same_shape(Lhs lhs, Rhs rhs, const char* message) {
    ksj::array::detail::validate_same_shape(lhs, rhs, message);
  }
};

template <typename T> struct special_view_traits<ksj::array::ImageView<T>> {
  using tag = image_view_tag;
  using value_type = typename ksj::array::ImageView<T>::value_type;

  template <typename OutputT> using output_view_type = ksj::array::ImageView<OutputT>;
  template <typename OutputT> using pooled_type = ksj::array::PooledImage<OutputT>;

  template <typename OutputT> [[nodiscard]] static pooled_type<OutputT> make_output(ksj::array::ImageView<T> input) {
    return ksj::array::make_pooled_image<OutputT>(input.rows(), input.cols());
  }

  template <typename Lhs, typename Rhs> static void validate_same_shape(Lhs lhs, Rhs rhs, const char* message) {
    ksj::array::detail::validate_same_shape(lhs, rhs, message);
  }
};

template <typename T> struct special_view_traits<ksj::array::CubeView<T>> {
  using tag = cube_view_tag;
  using value_type = typename ksj::array::CubeView<T>::value_type;

  template <typename OutputT> using output_view_type = ksj::array::CubeView<OutputT>;
  template <typename OutputT> using pooled_type = ksj::array::PooledCube<OutputT>;

  template <typename OutputT> [[nodiscard]] static pooled_type<OutputT> make_output(ksj::array::CubeView<T> input) {
    return ksj::array::make_pooled_cube<OutputT>(input.dim0(), input.dim1(), input.dim2());
  }

  template <typename Lhs, typename Rhs> static void validate_same_shape(Lhs lhs, Rhs rhs, const char* message) {
    ksj::array::detail::validate_same_cube_shape(lhs, rhs, message);
  }
};

template <typename T> struct special_view_traits<ksj::array::Array4DView<T>> {
  using tag = array4d_view_tag;
  using value_type = typename ksj::array::Array4DView<T>::value_type;

  template <typename OutputT> using output_view_type = ksj::array::Array4DView<OutputT>;
  template <typename OutputT> using pooled_type = ksj::array::PooledArray4D<OutputT>;

  template <typename OutputT> [[nodiscard]] static pooled_type<OutputT> make_output(ksj::array::Array4DView<T> input) {
    return ksj::array::make_pooled_array4d<OutputT>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  }

  template <typename Lhs, typename Rhs> static void validate_same_shape(Lhs lhs, Rhs rhs, const char* message) {
    ksj::array::detail::validate_same_array4d_shape(lhs, rhs, message);
  }
};

template <typename View>
concept special_view = requires {
  typename special_view_traits<std::remove_cvref_t<View>>::tag;
  typename special_view_traits<std::remove_cvref_t<View>>::value_type;
};

template <typename View>
concept non_vector_special_view =
  special_view<View> && !std::is_same_v<typename special_view_traits<std::remove_cvref_t<View>>::tag, vector_view_tag>;

template <typename View>
using special_view_value_t = typename special_view_traits<std::remove_cvref_t<View>>::value_type;

template <typename View>
using special_view_data_t = std::remove_pointer_t<decltype(std::declval<std::remove_cvref_t<View>>().data())>;

template <typename View>
inline constexpr bool mutable_special_view_v = special_view<View> && !std::is_const_v<special_view_data_t<View>>;

template <typename Lhs, typename Rhs>
inline constexpr bool same_special_view_kind_v =
  special_view<Lhs> && special_view<Rhs> &&
  std::is_same_v<typename special_view_traits<std::remove_cvref_t<Lhs>>::tag,
                 typename special_view_traits<std::remove_cvref_t<Rhs>>::tag>;

template <typename InputView, typename OutputView>
concept same_value_special_view_pair =
  special_view<InputView> && mutable_special_view_v<OutputView> && same_special_view_kind_v<InputView, OutputView> &&
  std::is_same_v<special_view_value_t<InputView>, special_view_value_t<OutputView>>;

template <typename InputView, typename OutputView>
concept real_output_special_view_pair =
  special_view<InputView> && mutable_special_view_v<OutputView> && same_special_view_kind_v<InputView, OutputView> &&
  std::is_same_v<ksj::array::real_scalar_t<special_view_value_t<InputView>>, special_view_value_t<OutputView>>;

template <typename LhsView, typename RhsView, typename OutputView>
concept same_value_binary_special_view_group =
  special_view<LhsView> && special_view<RhsView> && mutable_special_view_v<OutputView> &&
  same_special_view_kind_v<LhsView, RhsView> && same_special_view_kind_v<LhsView, OutputView> &&
  std::is_same_v<special_view_value_t<LhsView>, special_view_value_t<RhsView>> &&
  std::is_same_v<special_view_value_t<LhsView>, special_view_value_t<OutputView>>;

template <typename OutputT, special_view View>
using pooled_like_t = typename special_view_traits<std::remove_cvref_t<View>>::template pooled_type<OutputT>;

template <typename OutputT, special_view View> [[nodiscard]] pooled_like_t<OutputT, View> make_output_like(View input) {
  return special_view_traits<std::remove_cvref_t<View>>::template make_output<OutputT>(input);
}

template <special_view InputView, special_view OutputView>
void validate_special_same_shape(InputView input, OutputView output, const char* message) {
  special_view_traits<std::remove_cvref_t<InputView>>::validate_same_shape(input, output, message);
}

template <typename InputT>
[[nodiscard]] ksj::array::VectorView<const std::remove_const_t<InputT>>
make_const_vector_from_span(const std::span<InputT> input) noexcept {
  using value_type = std::remove_const_t<InputT>;
  return ksj::array::VectorView<const value_type>(input.data(), input.size());
}

template <typename OutputT>
[[nodiscard]] ksj::array::VectorView<std::remove_const_t<OutputT>>
make_mutable_vector_from_span(const std::span<OutputT> output) noexcept {
  using value_type = std::remove_const_t<OutputT>;
  return ksj::array::VectorView<value_type>(output.data(), output.size());
}

template <special_view InputView, special_view OutputView, typename IntelFunction>
[[nodiscard]] bool try_intel_unary_view_blocks(InputView input, OutputView output, IntelFunction&& intel_function) {
  using input_value_type = special_view_value_t<InputView>;
  using output_value_type = special_view_value_t<OutputView>;
  const auto const_input = ksj::array::as_const_view(input);
  if (const_input.is_contiguous() && output.is_contiguous()) {
    return intel_function(ksj::array::VectorView<const input_value_type>(const_input.data(), const_input.size()),
                          ksj::array::VectorView<output_value_type>(output.data(), output.size()));
  }

  bool ok = true;
  auto block_function = [&ok, &intel_function](auto input_block, auto output_block) {
    if (!ok) {
      return;
    }
    ok = intel_function(make_const_vector_from_span(input_block), make_mutable_vector_from_span(output_block));
  };
  if (!ksj::array::detail::for_each_inner_contiguous_span_group(block_function, const_input, output)) {
    return false;
  }
  return ok;
}

template <special_view LhsView, special_view RhsView, special_view OutputView, typename IntelFunction>
[[nodiscard]] bool try_intel_binary_view_blocks(LhsView lhs, RhsView rhs, OutputView output,
                                                IntelFunction&& intel_function) {
  using value_type = special_view_value_t<LhsView>;
  const auto const_lhs = ksj::array::as_const_view(lhs);
  const auto const_rhs = ksj::array::as_const_view(rhs);
  if (const_lhs.is_contiguous() && const_rhs.is_contiguous() && output.is_contiguous()) {
    return intel_function(ksj::array::VectorView<const value_type>(const_lhs.data(), const_lhs.size()),
                          ksj::array::VectorView<const value_type>(const_rhs.data(), const_rhs.size()),
                          ksj::array::VectorView<value_type>(output.data(), output.size()));
  }

  bool ok = true;
  auto block_function = [&ok, &intel_function](auto lhs_block, auto rhs_block, auto output_block) {
    if (!ok) {
      return;
    }
    ok = intel_function(make_const_vector_from_span(lhs_block), make_const_vector_from_span(rhs_block),
                        make_mutable_vector_from_span(output_block));
  };
  if (!ksj::array::detail::for_each_inner_contiguous_span_group(block_function, const_lhs, const_rhs, output)) {
    return false;
  }
  return ok;
}

template <special_view InputView, special_view OutputView, typename IntelFunction, typename ScalarFunction>
void dispatch_unary_view(InputView input, OutputView output, const bool prefer_intel, IntelFunction&& intel_function,
                         ScalarFunction&& scalar_function, const char* shape_error_message) {
  validate_special_same_shape(input, output, shape_error_message);
  const auto const_input = ksj::array::as_const_view(input);
  if (prefer_intel && try_intel_unary_view_blocks(const_input, output, std::forward<IntelFunction>(intel_function))) {
    return;
  }
  ksj::array::transform(const_input, output, std::forward<ScalarFunction>(scalar_function));
}

template <special_view LhsView, special_view RhsView, special_view OutputView, typename IntelFunction,
          typename ScalarFunction>
void dispatch_binary_view(LhsView lhs, RhsView rhs, OutputView output, const bool prefer_intel,
                          IntelFunction&& intel_function, ScalarFunction&& scalar_function,
                          const char* shape_error_message) {
  validate_special_same_shape(lhs, rhs, shape_error_message);
  validate_special_same_shape(lhs, output, shape_error_message);
  const auto const_lhs = ksj::array::as_const_view(lhs);
  const auto const_rhs = ksj::array::as_const_view(rhs);
  if (prefer_intel &&
      try_intel_binary_view_blocks(const_lhs, const_rhs, output, std::forward<IntelFunction>(intel_function))) {
    return;
  }
  ksj::array::transform(const_lhs, const_rhs, output, std::forward<ScalarFunction>(scalar_function));
}

template <typename OutputT, non_vector_special_view View, typename Function>
[[nodiscard]] pooled_like_t<OutputT, View> make_unary_output(View input, Function&& function) {
  auto output = make_output_like<OutputT>(input);
  function(input, output.view());
  return output;
}

template <typename Pooled> struct special_pooled_traits {};

template <typename T> struct special_pooled_traits<ksj::array::PooledMatrix<T>> {
  using view_type = ksj::array::MatrixView<const T>;
};

template <typename T> struct special_pooled_traits<ksj::array::PooledImage<T>> {
  using view_type = ksj::array::ImageView<const T>;
};

template <typename T> struct special_pooled_traits<ksj::array::PooledCube<T>> {
  using view_type = ksj::array::CubeView<const T>;
};

template <typename T> struct special_pooled_traits<ksj::array::PooledArray4D<T>> {
  using view_type = ksj::array::Array4DView<const T>;
};

template <typename Pooled>
concept non_vector_special_pooled = requires(const std::remove_cvref_t<Pooled>& input) {
  typename special_pooled_traits<std::remove_cvref_t<Pooled>>::view_type;
  { input.view() } -> std::same_as<typename special_pooled_traits<std::remove_cvref_t<Pooled>>::view_type>;
};

template <typename T>
concept scalar_special_argument = !special_view<T> && !non_vector_special_pooled<T>;

template <typename T, typename IntelFunction, typename EigenFunction>
[[nodiscard]] ksj::array::PooledVector<T> dispatch_real_vector(ksj::array::VectorView<const T> input,
                                                               const bool prefer_intel, IntelFunction&& intel_function,
                                                               EigenFunction&& eigen_function) {
  if (prefer_intel) {
    auto output = ksj::array::make_pooled_vector<T>(input.size());
    if (intel_function(input, output.view())) {
      return output;
    }
  }
  return eigen_function(input);
}

template <typename OutputT, typename InputT, typename IntelFunction, typename EigenFunction>
[[nodiscard]] ksj::array::PooledVector<OutputT> dispatch_vector(ksj::array::VectorView<const InputT> input,
                                                                const bool prefer_intel, IntelFunction&& intel_function,
                                                                EigenFunction&& eigen_function) {
  if (prefer_intel) {
    auto output = ksj::array::make_pooled_vector<OutputT>(input.size());
    if (intel_function(input, output.view())) {
      return output;
    }
  }
  return eigen_function(input);
}

template <typename T, typename IntelFunction, typename EigenFunction>
[[nodiscard]] ksj::array::PooledVector<T> dispatch_same_vector(ksj::array::VectorView<const T> input,
                                                               const bool prefer_intel, IntelFunction&& intel_function,
                                                               EigenFunction&& eigen_function) {
  return dispatch_vector<T>(input, prefer_intel, std::forward<IntelFunction>(intel_function),
                            std::forward<EigenFunction>(eigen_function));
}

template <typename T, typename IntelFunction, typename EigenFunction>
[[nodiscard]] ksj::array::PooledVector<T>
dispatch_binary_vector(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs,
                       const bool prefer_intel, IntelFunction&& intel_function, EigenFunction&& eigen_function) {
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("special binary vector inputs must have the same size");
  }
  if (prefer_intel) {
    auto output = ksj::array::make_pooled_vector<T>(lhs.size());
    if (intel_function(lhs, rhs, output.view())) {
      return output;
    }
  }
  return eigen_function(lhs, rhs);
}

} // namespace detail

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto gamma(const T value) {
  using result_type = detail::real_special_result_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<result_type, float>) {
    return detail::eigen::gamma(static_cast<float>(value));
  } else {
    return detail::eigen::gamma(static_cast<double>(value));
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto log_gamma(const T value) {
  using result_type = detail::real_special_result_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<result_type, float>) {
    return detail::eigen::log_gamma(static_cast<float>(value));
  } else {
    return detail::eigen::log_gamma(static_cast<double>(value));
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto bessel_i0(const T value) {
  using result_type = detail::real_special_result_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<result_type, float>) {
    return detail::eigen::bessel_i0(static_cast<float>(value));
  } else {
    return detail::eigen::bessel_i0(static_cast<double>(value));
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto bessel_j0(const T value) {
  using result_type = detail::real_special_result_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<result_type, float>) {
    return detail::eigen::bessel_j0(static_cast<float>(value));
  } else {
    return detail::eigen::bessel_j0(static_cast<double>(value));
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto bessel_j1(const T value) {
  using result_type = detail::real_special_result_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<result_type, float>) {
    return detail::eigen::bessel_j1(static_cast<float>(value));
  } else {
    return detail::eigen::bessel_j1(static_cast<double>(value));
  }
}

template <typename Order, typename T>
  requires(detail::scalar_special_argument<Order> && detail::scalar_special_argument<T>)
[[nodiscard]] auto bessel_j(const Order order, const T value) {
  using result_type = std::common_type_t<Order, T>;
  if constexpr (std::is_same_v<result_type, float>) {
    return detail::eigen::bessel_j(static_cast<float>(order), static_cast<float>(value));
  } else {
    return detail::eigen::bessel_j(static_cast<double>(order), static_cast<double>(value));
  }
}

template <typename Order, typename T>
  requires(detail::scalar_special_argument<Order>)
[[nodiscard]] auto bessel_j(const Order order, const std::complex<T> value) {
  using real_type = std::common_type_t<Order, T>;
  if constexpr (std::is_same_v<real_type, float>) {
    return detail::eigen::bessel_j(static_cast<float>(order), ksj::base::cf32(value));
  } else {
    return detail::eigen::bessel_j(static_cast<double>(order), ksj::base::cf64(value));
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto sin(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::sin(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::sin(static_cast<double>(value));
  } else {
    return std::sin(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto exp(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::exp(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::exp(static_cast<double>(value));
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return detail::eigen::exp(static_cast<ksj::base::cf32>(value));
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::eigen::exp(static_cast<ksj::base::cf64>(value));
  } else {
    return std::exp(value);
  }
}

/// Returns 2 raised to `value` for a real scalar.
template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto exp2(const T value) {
  static_assert(detail::supported_real_special_v<T>, "special exp2 supports float and double");
  return std::exp2(value);
}

/// Returns exp(value) - 1 while preserving precision near zero for a real scalar.
template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto expm1(const T value) {
  static_assert(detail::supported_real_special_v<T>, "special expm1 supports float and double");
  return std::expm1(value);
}

/// Returns log(1 + value) while preserving precision near zero for a real scalar.
template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto log1p(const T value) {
  static_assert(detail::supported_real_special_v<T>, "special log1p supports float and double");
  return std::log1p(value);
}

/// Returns the complementary error function, 1 - erf(value), for a real scalar.
template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto erfc(const T value) {
  static_assert(detail::supported_real_special_v<T>, "special erfc supports float and double");
  return std::erfc(value);
}

/// Returns sin(pi * value) for a real scalar.
template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto sinpi(const T value) {
  static_assert(detail::supported_real_special_v<T>, "special sinpi supports float and double");
  using value_type = std::remove_cv_t<T>;
  return std::sin(std::numbers::pi_v<value_type> * value);
}

/// Returns cos(pi * value) for a real scalar.
template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto cospi(const T value) {
  static_assert(detail::supported_real_special_v<T>, "special cospi supports float and double");
  using value_type = std::remove_cv_t<T>;
  return std::cos(std::numbers::pi_v<value_type> * value);
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto cos(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::cos(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::cos(static_cast<double>(value));
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return detail::eigen::cos(static_cast<ksj::base::cf32>(value));
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::eigen::cos(static_cast<ksj::base::cf64>(value));
  } else {
    return std::cos(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto tan(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::tan(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::tan(static_cast<double>(value));
  } else {
    return std::tan(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto asin(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::asin(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::asin(static_cast<double>(value));
  } else {
    return std::asin(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto acos(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::acos(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::acos(static_cast<double>(value));
  } else {
    return std::acos(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto atan(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::atan(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::atan(static_cast<double>(value));
  } else {
    return std::atan(value);
  }
}

template <typename Y, typename X>
  requires(detail::scalar_special_argument<Y> && detail::scalar_special_argument<X>)
[[nodiscard]] auto atan2(const Y y, const X x) {
  using result_type = std::common_type_t<Y, X>;
  if constexpr (std::is_same_v<result_type, float>) {
    return detail::eigen::atan2(static_cast<float>(y), static_cast<float>(x));
  } else {
    return detail::eigen::atan2(static_cast<double>(y), static_cast<double>(x));
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto ln(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::ln(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::ln(static_cast<double>(value));
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return detail::eigen::ln(static_cast<ksj::base::cf32>(value));
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::eigen::ln(static_cast<ksj::base::cf64>(value));
  } else {
    return std::log(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto log10(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::log10(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::log10(static_cast<double>(value));
  } else {
    return std::log10(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto log2(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::log2(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::log2(static_cast<double>(value));
  } else {
    return std::log2(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto sqrt(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::sqrt(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::sqrt(static_cast<double>(value));
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return detail::eigen::sqrt(static_cast<ksj::base::cf32>(value));
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::eigen::sqrt(static_cast<ksj::base::cf64>(value));
  } else {
    return std::sqrt(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto cbrt(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::cbrt(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::cbrt(static_cast<double>(value));
  } else {
    return std::cbrt(value);
  }
}

template <typename Base, typename Exponent>
  requires(detail::scalar_special_argument<Base> && detail::scalar_special_argument<Exponent>)
[[nodiscard]] auto pow(const Base base, const Exponent exponent) {
  using result_type = std::common_type_t<Base, Exponent>;
  if constexpr (std::is_same_v<result_type, float>) {
    return detail::eigen::pow(static_cast<float>(base), static_cast<float>(exponent));
  } else {
    return detail::eigen::pow(static_cast<double>(base), static_cast<double>(exponent));
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto erf(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::erf(static_cast<float>(value));
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::eigen::erf(static_cast<double>(value));
  } else {
    return std::erf(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto cdf_norm(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::eigen::cdf_norm(static_cast<float>(value));
  } else {
    return detail::eigen::cdf_norm(static_cast<double>(value));
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto cdfnorm(const T value) {
  return cdf_norm(value);
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto conj(const T value) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return detail::eigen::conj(static_cast<ksj::base::cf32>(value));
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::eigen::conj(static_cast<ksj::base::cf64>(value));
  } else if constexpr (std::is_arithmetic_v<value_type>) {
    return value;
  } else {
    return std::conj(value);
  }
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto abs(const T value) {
  return std::abs(value);
}

template <typename T>
  requires(detail::scalar_special_argument<T>)
[[nodiscard]] auto arg(const T value) {
  return std::arg(value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> gamma(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_gamma<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::gamma(source, output);
      },
      [](auto source) {
        return detail::eigen::gamma(source);
      });
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_gamma<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::gamma(source, output);
      },
      [](auto source) {
        return detail::eigen::gamma(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(gamma(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> gamma(const ksj::array::PooledVector<T>& input) {
  return gamma(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> log_gamma(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_log_gamma<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::log_gamma(source, output);
      },
      [](auto source) {
        return detail::eigen::log_gamma(source);
      });
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_log_gamma<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::log_gamma(source, output);
      },
      [](auto source) {
        return detail::eigen::log_gamma(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(log_gamma(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> log_gamma(const ksj::array::PooledVector<T>& input) {
  return log_gamma(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> bessel_i0(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_bessel_i0<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::bessel_i0(source, output);
      },
      [](auto source) {
        return detail::eigen::bessel_i0(source);
      });
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_bessel_i0<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::bessel_i0(source, output);
      },
      [](auto source) {
        return detail::eigen::bessel_i0(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(bessel_i0(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> bessel_i0(const ksj::array::PooledVector<T>& input) {
  return bessel_i0(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> bessel_j0(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_bessel_j0<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::bessel_j0(source, output);
      },
      [](auto source) {
        return detail::eigen::bessel_j0(source);
      });
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_bessel_j0<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::bessel_j0(source, output);
      },
      [](auto source) {
        return detail::eigen::bessel_j0(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(bessel_j0(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> bessel_j0(const ksj::array::PooledVector<T>& input) {
  return bessel_j0(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> bessel_j1(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_bessel_j1<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::bessel_j1(source, output);
      },
      [](auto source) {
        return detail::eigen::bessel_j1(source);
      });
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_bessel_j1<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::bessel_j1(source, output);
      },
      [](auto source) {
        return detail::eigen::bessel_j1(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(bessel_j1(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> bessel_j1(const ksj::array::PooledVector<T>& input) {
  return bessel_j1(input.view());
}

template <typename Order, typename T> [[nodiscard]] auto bessel_j(const Order order, ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  using result_type = decltype(bessel_j(order, std::declval<value_type>()));
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> && std::is_same_v<result_type, float>) {
    return detail::eigen::bessel_j(static_cast<float>(order), const_input);
  } else if constexpr (std::is_same_v<value_type, double> && std::is_same_v<result_type, double>) {
    return detail::eigen::bessel_j(static_cast<double>(order), const_input);
  } else {
    auto output = ksj::array::make_pooled_vector<result_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = bessel_j(order, input(index));
    }
    return output;
  }
}

template <typename Order, typename T>
[[nodiscard]] auto bessel_j(const Order order, const ksj::array::PooledVector<T>& input) {
  return bessel_j(order, input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> sin(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_sin<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::sin(source, output);
      },
      [](auto source) {
        return detail::eigen::sin(source);
      });
  } else if constexpr (std::is_same_v<value_type, double>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_sin<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::sin(source, output);
      },
      [](auto source) {
        return detail::eigen::sin(source);
      });
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32> || std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_complex_sin<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::sin(source, output);
      },
      [](auto source) {
        return detail::eigen::sin(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(sin(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> sin(const ksj::array::PooledVector<T>& input) {
  return sin(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> cos(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::cos(source, output);
      },
      [](auto source) {
        return detail::eigen::cos(source);
      });
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32> || std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_complex_cos<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::cos(source, output);
      },
      [](auto source) {
        return detail::eigen::cos(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(cos(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> cos(const ksj::array::PooledVector<T>& input) {
  return cos(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> tan(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::tan(source, output);
      },
      [](auto source) {
        return detail::eigen::tan(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(tan(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> tan(const ksj::array::PooledVector<T>& input) {
  return tan(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> asin(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::asin(source, output);
      },
      [](auto source) {
        return detail::eigen::asin(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(asin(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> asin(const ksj::array::PooledVector<T>& input) {
  return asin(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> acos(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::acos(source, output);
      },
      [](auto source) {
        return detail::eigen::acos(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(acos(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> acos(const ksj::array::PooledVector<T>& input) {
  return acos(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> atan(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::atan(source, output);
      },
      [](auto source) {
        return detail::eigen::atan(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(atan(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> atan(const ksj::array::PooledVector<T>& input) {
  return atan(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> atan2(ksj::array::VectorView<T> y,
                                                                     ksj::array::VectorView<T> x) {
  using value_type = std::remove_const_t<T>;
  const auto const_y = ksj::array::as_const_view(y);
  const auto const_x = ksj::array::as_const_view(x);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_binary_vector<value_type>(
      const_y, const_x, detail::prefer_intel_vml_real<value_type>(y.size()),
      [](auto lhs, auto rhs, auto output) {
        return detail::intel::atan2(lhs, rhs, output);
      },
      [](auto lhs, auto rhs) {
        return detail::eigen::atan2(lhs, rhs);
      });
  } else {
    if (y.size() != x.size()) {
      throw std::invalid_argument("special atan2 vector inputs must have the same size");
    }
    auto output = ksj::array::make_pooled_vector<value_type>(y.size());
    for (std::size_t index = 0; index < y.size(); ++index) {
      output(index) = static_cast<value_type>(atan2(y(index), x(index)));
    }
    return output;
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> atan2(const ksj::array::PooledVector<T>& y,
                                                const ksj::array::PooledVector<T>& x) {
  return atan2(y.view(), x.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> ln(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::ln(source, output);
      },
      [](auto source) {
        return detail::eigen::ln(source);
      });
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32> || std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_complex<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::ln(source, output);
      },
      [](auto source) {
        return detail::eigen::ln(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(ln(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> ln(const ksj::array::PooledVector<T>& input) {
  return ln(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> log10(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::log10(source, output);
      },
      [](auto source) {
        return detail::eigen::log10(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(log10(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> log10(const ksj::array::PooledVector<T>& input) {
  return log10(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> log2(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::log2(source, output);
      },
      [](auto source) {
        return detail::eigen::log2(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(log2(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> log2(const ksj::array::PooledVector<T>& input) {
  return log2(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> sqrt(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::sqrt(source, output);
      },
      [](auto source) {
        return detail::eigen::sqrt(source);
      });
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32> || std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_complex_sqrt<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::sqrt(source, output);
      },
      [](auto source) {
        return detail::eigen::sqrt(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(sqrt(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> sqrt(const ksj::array::PooledVector<T>& input) {
  return sqrt(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> cbrt(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::cbrt(source, output);
      },
      [](auto source) {
        return detail::eigen::cbrt(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(cbrt(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> cbrt(const ksj::array::PooledVector<T>& input) {
  return cbrt(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> pow(ksj::array::VectorView<T> base,
                                                                   ksj::array::VectorView<T> exponent) {
  using value_type = std::remove_const_t<T>;
  const auto const_base = ksj::array::as_const_view(base);
  const auto const_exponent = ksj::array::as_const_view(exponent);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_binary_vector<value_type>(
      const_base, const_exponent, detail::prefer_intel_vml_real<value_type>(base.size()),
      [](auto lhs, auto rhs, auto output) {
        return detail::intel::pow(lhs, rhs, output);
      },
      [](auto lhs, auto rhs) {
        return detail::eigen::pow(lhs, rhs);
      });
  } else {
    if (base.size() != exponent.size()) {
      throw std::invalid_argument("special pow vector inputs must have the same size");
    }
    auto output = ksj::array::make_pooled_vector<value_type>(base.size());
    for (std::size_t index = 0; index < base.size(); ++index) {
      output(index) = static_cast<value_type>(pow(base(index), exponent(index)));
    }
    return output;
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> pow(const ksj::array::PooledVector<T>& base,
                                              const ksj::array::PooledVector<T>& exponent) {
  return pow(base.view(), exponent.view());
}

template <typename T, typename Exponent>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> pow(ksj::array::VectorView<T> base,
                                                                   const Exponent exponent) {
  using value_type = std::remove_const_t<T>;
  const auto const_base = ksj::array::as_const_view(base);
  const auto converted_exponent = static_cast<value_type>(exponent);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_base, detail::prefer_intel_vml_real<value_type>(base.size()),
      [converted_exponent](auto source, auto output) {
        return detail::intel::pow(source, converted_exponent, output);
      },
      [converted_exponent](auto source) {
        return detail::eigen::pow(source, converted_exponent);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(base.size());
    for (std::size_t index = 0; index < base.size(); ++index) {
      output(index) = static_cast<value_type>(pow(base(index), converted_exponent));
    }
    return output;
  }
}

template <typename T, typename Exponent>
[[nodiscard]] ksj::array::PooledVector<T> pow(const ksj::array::PooledVector<T>& base, const Exponent exponent) {
  return pow(base.view(), exponent);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> erf(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::erf(source, output);
      },
      [](auto source) {
        return detail::eigen::erf(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(erf(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> erf(const ksj::array::PooledVector<T>& input) {
  return erf(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> cdf_norm(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_real<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::cdf_norm(source, output);
      },
      [](auto source) {
        return detail::eigen::cdf_norm(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(cdf_norm(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> cdf_norm(const ksj::array::PooledVector<T>& input) {
  return cdf_norm(input.view());
}

template <typename T> [[nodiscard]] auto cdfnorm(ksj::array::VectorView<T> input) {
  return cdf_norm(input);
}

template <typename T> [[nodiscard]] auto cdfnorm(const ksj::array::PooledVector<T>& input) {
  return cdf_norm(input);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> exp(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, float> || std::is_same_v<value_type, double> ||
                std::is_same_v<value_type, ksj::base::cf32> || std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::dispatch_real_vector<value_type>(
      const_input, detail::prefer_intel_exp<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::exp(source, output);
      },
      [](auto source) {
        return detail::eigen::exp(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(exp(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> exp(const ksj::array::PooledVector<T>& input) {
  return exp(input.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> conj(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, ksj::base::cf32> || std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::dispatch_same_vector<value_type>(
      const_input, detail::prefer_intel_vml_complex<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::conj(source, output);
      },
      [](auto source) {
        return detail::eigen::conj(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = input(index);
    }
    return output;
  }
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> conj(const ksj::array::PooledVector<T>& input) {
  return conj(input.view());
}

template <typename T> [[nodiscard]] auto abs(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return detail::dispatch_vector<float>(
      const_input, detail::prefer_intel_vml_complex<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::abs(source, output);
      },
      [](auto source) {
        return detail::eigen::abs(source);
      });
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::dispatch_vector<double>(
      const_input, detail::prefer_intel_vml_complex<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::abs(source, output);
      },
      [](auto source) {
        return detail::eigen::abs(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(abs(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] auto abs(const ksj::array::PooledVector<T>& input) {
  return abs(input.view());
}

template <typename T> [[nodiscard]] auto arg(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  const auto const_input = ksj::array::as_const_view(input);
  if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return detail::dispatch_vector<float>(
      const_input, detail::prefer_intel_vml_complex<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::arg(source, output);
      },
      [](auto source) {
        return detail::eigen::arg(source);
      });
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return detail::dispatch_vector<double>(
      const_input, detail::prefer_intel_vml_complex<value_type>(input.size()),
      [](auto source, auto output) {
        return detail::intel::arg(source, output);
      },
      [](auto source) {
        return detail::eigen::arg(source);
      });
  } else {
    auto output = ksj::array::make_pooled_vector<value_type>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      output(index) = static_cast<value_type>(arg(input(index)));
    }
    return output;
  }
}

template <typename T> [[nodiscard]] auto arg(const ksj::array::PooledVector<T>& input) {
  return arg(input.view());
}

template <typename View, typename OutputView>
void gamma(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(std::is_same_v<value_type, float> || std::is_same_v<value_type, double>,
                "special gamma view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_gamma<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::gamma(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(gamma(value));
    },
    "special gamma view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto gamma(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::gamma(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto gamma(const Pooled& input) {
  return ::ksj::special::gamma(input.view());
}

template <typename View, typename OutputView>
void log_gamma(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(std::is_same_v<value_type, float> || std::is_same_v<value_type, double>,
                "special log_gamma view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_log_gamma<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::log_gamma(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(log_gamma(value));
    },
    "special log_gamma view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto log_gamma(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::log_gamma(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto log_gamma(const Pooled& input) {
  return ::ksj::special::log_gamma(input.view());
}

template <typename View, typename OutputView>
void bessel_i0(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(std::is_same_v<value_type, float> || std::is_same_v<value_type, double>,
                "special bessel_i0 view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_bessel_i0<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::bessel_i0(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(bessel_i0(value));
    },
    "special bessel_i0 view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto bessel_i0(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::bessel_i0(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto bessel_i0(const Pooled& input) {
  return ::ksj::special::bessel_i0(input.view());
}

template <typename View, typename OutputView>
void bessel_j0(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(std::is_same_v<value_type, float> || std::is_same_v<value_type, double>,
                "special bessel_j0 view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_bessel_j0<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::bessel_j0(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(bessel_j0(value));
    },
    "special bessel_j0 view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto bessel_j0(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::bessel_j0(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto bessel_j0(const Pooled& input) {
  return ::ksj::special::bessel_j0(input.view());
}

template <typename View, typename OutputView>
void bessel_j1(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(std::is_same_v<value_type, float> || std::is_same_v<value_type, double>,
                "special bessel_j1 view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_bessel_j1<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::bessel_j1(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(bessel_j1(value));
    },
    "special bessel_j1 view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto bessel_j1(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::bessel_j1(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto bessel_j1(const Pooled& input) {
  return ::ksj::special::bessel_j1(input.view());
}

template <typename Order, typename View, typename OutputView>
void bessel_j(const Order order, View input, OutputView output)
  requires(detail::scalar_special_argument<Order> && detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, false,
    [](auto, auto) {
      return false;
    },
    [order](const auto& value) -> value_type {
      return static_cast<value_type>(bessel_j(order, value));
    },
    "special bessel_j view output shape mismatch");
}

template <typename Order, detail::non_vector_special_view View>
  requires(detail::scalar_special_argument<Order>)
[[nodiscard]] auto bessel_j(const Order order, View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [order](auto source, auto destination) {
    ::ksj::special::bessel_j(order, source, destination);
  });
}

template <typename Order, detail::non_vector_special_pooled Pooled>
  requires(detail::scalar_special_argument<Order>)
[[nodiscard]] auto bessel_j(const Order order, const Pooled& input) {
  return ::ksj::special::bessel_j(order, input.view());
}

template <typename View, typename OutputView>
void sin(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  const auto prefer_intel = std::is_same_v<value_type, float> || std::is_same_v<value_type, double>
                              ? detail::prefer_intel_sin<value_type>(input.size())
                              : detail::prefer_intel_vml_complex<value_type>(input.size());
  detail::dispatch_unary_view(
    input, output, prefer_intel,
    [](auto source, auto destination) {
      return detail::intel::sin(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(sin(value));
    },
    "special sin view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto sin(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::sin(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto sin(const Pooled& input) {
  return ::ksj::special::sin(input.view());
}

template <typename View, typename OutputView>
void cos(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  const auto prefer_intel = std::is_same_v<value_type, float> || std::is_same_v<value_type, double>
                              ? detail::prefer_intel_vml_real<value_type>(input.size())
                              : detail::prefer_intel_vml_complex<value_type>(input.size());
  detail::dispatch_unary_view(
    input, output, prefer_intel,
    [](auto source, auto destination) {
      return detail::intel::cos(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(cos(value));
    },
    "special cos view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto cos(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::cos(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto cos(const Pooled& input) {
  return ::ksj::special::cos(input.view());
}

template <typename View, typename OutputView>
void tan(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::tan(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(tan(value));
    },
    "special tan view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto tan(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::tan(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto tan(const Pooled& input) {
  return ::ksj::special::tan(input.view());
}

template <typename View, typename OutputView>
void asin(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::asin(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(asin(value));
    },
    "special asin view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto asin(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::asin(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto asin(const Pooled& input) {
  return ::ksj::special::asin(input.view());
}

template <typename View, typename OutputView>
void acos(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::acos(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(acos(value));
    },
    "special acos view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto acos(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::acos(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto acos(const Pooled& input) {
  return ::ksj::special::acos(input.view());
}

template <typename View, typename OutputView>
void atan(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::atan(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(atan(value));
    },
    "special atan view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto atan(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::atan(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto atan(const Pooled& input) {
  return ::ksj::special::atan(input.view());
}

template <typename View, typename OutputView>
void ln(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  const auto prefer_intel = std::is_same_v<value_type, float> || std::is_same_v<value_type, double>
                              ? detail::prefer_intel_vml_real<value_type>(input.size())
                              : detail::prefer_intel_vml_complex<value_type>(input.size());
  detail::dispatch_unary_view(
    input, output, prefer_intel,
    [](auto source, auto destination) {
      return detail::intel::ln(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(ln(value));
    },
    "special ln view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto ln(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::ln(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto ln(const Pooled& input) {
  return ::ksj::special::ln(input.view());
}

template <typename View, typename OutputView>
void log10(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::log10(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(log10(value));
    },
    "special log10 view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto log10(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::log10(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto log10(const Pooled& input) {
  return ::ksj::special::log10(input.view());
}

template <typename View, typename OutputView>
void log2(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::log2(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(log2(value));
    },
    "special log2 view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto log2(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::log2(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto log2(const Pooled& input) {
  return ::ksj::special::log2(input.view());
}

template <typename View, typename OutputView>
void sqrt(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  const auto prefer_intel = std::is_same_v<value_type, float> || std::is_same_v<value_type, double>
                              ? detail::prefer_intel_vml_real<value_type>(input.size())
                              : detail::prefer_intel_vml_complex<value_type>(input.size());
  detail::dispatch_unary_view(
    input, output, prefer_intel,
    [](auto source, auto destination) {
      return detail::intel::sqrt(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(sqrt(value));
    },
    "special sqrt view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto sqrt(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::sqrt(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto sqrt(const Pooled& input) {
  return ::ksj::special::sqrt(input.view());
}

template <typename View, typename OutputView>
void cbrt(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::cbrt(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(cbrt(value));
    },
    "special cbrt view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto cbrt(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::cbrt(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto cbrt(const Pooled& input) {
  return ::ksj::special::cbrt(input.view());
}

template <typename View, typename OutputView>
void erf(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::erf(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(erf(value));
    },
    "special erf view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto erf(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::erf(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto erf(const Pooled& input) {
  return ::ksj::special::erf(input.view());
}

template <typename View, typename OutputView>
void cdf_norm(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::cdf_norm(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(cdf_norm(value));
    },
    "special cdf_norm view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto cdf_norm(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::cdf_norm(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto cdf_norm(const Pooled& input) {
  return ::ksj::special::cdf_norm(input.view());
}

template <typename View, typename OutputView>
void cdfnorm(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  ::ksj::special::cdf_norm(input, output);
}

template <detail::non_vector_special_view View> [[nodiscard]] auto cdfnorm(View input) {
  return ::ksj::special::cdf_norm(input);
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto cdfnorm(const Pooled& input) {
  return ::ksj::special::cdf_norm(input);
}

template <typename View, typename OutputView>
void exp(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_exp<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::exp(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(exp(value));
    },
    "special exp view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto exp(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::exp(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto exp(const Pooled& input) {
  return ::ksj::special::exp(input.view());
}

/// Writes 2 raised to each real input element into `output`.
template <typename View, typename OutputView>
void exp2(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(detail::supported_real_special_v<value_type>, "special exp2 view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::exp2(source, destination);
    },
    [](const auto value) -> value_type {
      return std::exp2(value);
    },
    "special exp2 view output shape mismatch");
}

/// Returns a newly allocated vector containing 2 raised to each input element.
template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> exp2(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_vector<value_type>(input.size());
  ::ksj::special::exp2(input, output.view());
  return output;
}

/// Returns a newly allocated array with 2 raised to each input element.
template <detail::non_vector_special_view View> [[nodiscard]] auto exp2(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::exp2(source, destination);
  });
}

/// Returns a newly allocated array with 2 raised to each input element.
template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto exp2(const Pooled& input) {
  return ::ksj::special::exp2(input.view());
}

/// Writes exp(input) - 1 for each real input element into `output` with near-zero accuracy.
template <typename View, typename OutputView>
void expm1(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(detail::supported_real_special_v<value_type>, "special expm1 view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::expm1(source, destination);
    },
    [](const auto value) -> value_type {
      return std::expm1(value);
    },
    "special expm1 view output shape mismatch");
}

/// Returns a newly allocated vector containing exp(input) - 1.
template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> expm1(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_vector<value_type>(input.size());
  ::ksj::special::expm1(input, output.view());
  return output;
}

/// Returns a newly allocated array containing exp(input) - 1.
template <detail::non_vector_special_view View> [[nodiscard]] auto expm1(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::expm1(source, destination);
  });
}

/// Returns a newly allocated array containing exp(input) - 1.
template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto expm1(const Pooled& input) {
  return ::ksj::special::expm1(input.view());
}

/// Writes log(1 + input) for each real input element into `output` with near-zero accuracy.
template <typename View, typename OutputView>
void log1p(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(detail::supported_real_special_v<value_type>, "special log1p view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::log1p(source, destination);
    },
    [](const auto value) -> value_type {
      return std::log1p(value);
    },
    "special log1p view output shape mismatch");
}

/// Returns a newly allocated vector containing log(1 + input).
template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> log1p(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_vector<value_type>(input.size());
  ::ksj::special::log1p(input, output.view());
  return output;
}

/// Returns a newly allocated array containing log(1 + input).
template <detail::non_vector_special_view View> [[nodiscard]] auto log1p(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::log1p(source, destination);
  });
}

/// Returns a newly allocated array containing log(1 + input).
template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto log1p(const Pooled& input) {
  return ::ksj::special::log1p(input.view());
}

/// Writes the complementary error function, 1 - erf(input), into `output`.
template <typename View, typename OutputView>
void erfc(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(detail::supported_real_special_v<value_type>, "special erfc view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::erfc(source, destination);
    },
    [](const auto value) -> value_type {
      return std::erfc(value);
    },
    "special erfc view output shape mismatch");
}

/// Returns a newly allocated vector containing the complementary error function.
template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> erfc(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_vector<value_type>(input.size());
  ::ksj::special::erfc(input, output.view());
  return output;
}

/// Returns a newly allocated array containing the complementary error function.
template <detail::non_vector_special_view View> [[nodiscard]] auto erfc(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::erfc(source, destination);
  });
}

/// Returns a newly allocated array containing the complementary error function.
template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto erfc(const Pooled& input) {
  return ::ksj::special::erfc(input.view());
}

/// Writes sin(pi * input) for each real input element into `output`.
template <typename View, typename OutputView>
void sinpi(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(detail::supported_real_special_v<value_type>, "special sinpi view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::sinpi(source, destination);
    },
    [](const auto value) -> value_type {
      return std::sin(std::numbers::pi_v<value_type> * value);
    },
    "special sinpi view output shape mismatch");
}

/// Returns a newly allocated vector containing sin(pi * input).
template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> sinpi(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_vector<value_type>(input.size());
  ::ksj::special::sinpi(input, output.view());
  return output;
}

/// Returns a newly allocated array containing sin(pi * input).
template <detail::non_vector_special_view View> [[nodiscard]] auto sinpi(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::sinpi(source, destination);
  });
}

/// Returns a newly allocated array containing sin(pi * input).
template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto sinpi(const Pooled& input) {
  return ::ksj::special::sinpi(input.view());
}

/// Writes cos(pi * input) for each real input element into `output`.
template <typename View, typename OutputView>
void cospi(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  static_assert(detail::supported_real_special_v<value_type>, "special cospi view overload supports float and double");
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_real<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::cospi(source, destination);
    },
    [](const auto value) -> value_type {
      return std::cos(std::numbers::pi_v<value_type> * value);
    },
    "special cospi view output shape mismatch");
}

/// Returns a newly allocated vector containing cos(pi * input).
template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> cospi(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_vector<value_type>(input.size());
  ::ksj::special::cospi(input, output.view());
  return output;
}

/// Returns a newly allocated vector containing 2 raised to each input element.
template <typename T> [[nodiscard]] ksj::array::PooledVector<T> exp2(const ksj::array::PooledVector<T>& input) {
  return ::ksj::special::exp2(input.view());
}

/// Returns a newly allocated vector containing exp(input) - 1.
template <typename T> [[nodiscard]] ksj::array::PooledVector<T> expm1(const ksj::array::PooledVector<T>& input) {
  return ::ksj::special::expm1(input.view());
}

/// Returns a newly allocated vector containing log(1 + input).
template <typename T> [[nodiscard]] ksj::array::PooledVector<T> log1p(const ksj::array::PooledVector<T>& input) {
  return ::ksj::special::log1p(input.view());
}

/// Returns a newly allocated vector containing the complementary error function.
template <typename T> [[nodiscard]] ksj::array::PooledVector<T> erfc(const ksj::array::PooledVector<T>& input) {
  return ::ksj::special::erfc(input.view());
}

/// Returns a newly allocated vector containing sin(pi * input).
template <typename T> [[nodiscard]] ksj::array::PooledVector<T> sinpi(const ksj::array::PooledVector<T>& input) {
  return ::ksj::special::sinpi(input.view());
}

/// Returns a newly allocated vector containing cos(pi * input).
template <typename T> [[nodiscard]] ksj::array::PooledVector<T> cospi(const ksj::array::PooledVector<T>& input) {
  return ::ksj::special::cospi(input.view());
}

/// Returns a newly allocated array containing cos(pi * input).
template <detail::non_vector_special_view View> [[nodiscard]] auto cospi(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::cospi(source, destination);
  });
}

/// Returns a newly allocated array containing cos(pi * input).
template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto cospi(const Pooled& input) {
  return ::ksj::special::cospi(input.view());
}

template <typename View, typename OutputView>
void conj(View input, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView>)
{
  using value_type = detail::special_view_value_t<View>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_complex<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::conj(source, destination);
    },
    [](const auto& value) -> value_type {
      return static_cast<value_type>(conj(value));
    },
    "special conj view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto conj(View input) {
  using value_type = detail::special_view_value_t<View>;
  return detail::make_unary_output<value_type>(input, [](auto source, auto destination) {
    ::ksj::special::conj(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto conj(const Pooled& input) {
  return ::ksj::special::conj(input.view());
}

template <typename InputView, typename OutputView>
void abs(InputView input, OutputView output)
  requires(detail::real_output_special_view_pair<InputView, OutputView>)
{
  using value_type = detail::special_view_value_t<InputView>;
  using output_type = detail::special_view_value_t<OutputView>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_complex<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::abs(source, destination);
    },
    [](const auto& value) -> output_type {
      return static_cast<output_type>(abs(value));
    },
    "special abs view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto abs(View input) {
  using value_type = detail::special_view_value_t<View>;
  using output_type = ksj::array::real_scalar_t<value_type>;
  return detail::make_unary_output<output_type>(input, [](auto source, auto destination) {
    ::ksj::special::abs(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto abs(const Pooled& input) {
  return ::ksj::special::abs(input.view());
}

template <typename InputView, typename OutputView>
void arg(InputView input, OutputView output)
  requires(detail::real_output_special_view_pair<InputView, OutputView>)
{
  using value_type = detail::special_view_value_t<InputView>;
  using output_type = detail::special_view_value_t<OutputView>;
  detail::dispatch_unary_view(
    input, output, detail::prefer_intel_vml_complex<value_type>(input.size()),
    [](auto source, auto destination) {
      return detail::intel::arg(source, destination);
    },
    [](const auto& value) -> output_type {
      return static_cast<output_type>(arg(value));
    },
    "special arg view output shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto arg(View input) {
  using value_type = detail::special_view_value_t<View>;
  using output_type = ksj::array::real_scalar_t<value_type>;
  return detail::make_unary_output<output_type>(input, [](auto source, auto destination) {
    ::ksj::special::arg(source, destination);
  });
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto arg(const Pooled& input) {
  return ::ksj::special::arg(input.view());
}

template <typename LhsView, typename RhsView, typename OutputView>
void atan2(LhsView y, RhsView x, OutputView output)
  requires(detail::same_value_binary_special_view_group<LhsView, RhsView, OutputView>)
{
  using value_type = detail::special_view_value_t<LhsView>;
  detail::dispatch_binary_view(
    y, x, output, detail::prefer_intel_vml_real<value_type>(y.size()),
    [](auto lhs, auto rhs, auto destination) {
      return detail::intel::atan2(lhs, rhs, destination);
    },
    [](const auto& lhs, const auto& rhs) -> value_type {
      return static_cast<value_type>(atan2(lhs, rhs));
    },
    "special atan2 view shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto atan2(View y, View x) {
  using value_type = detail::special_view_value_t<View>;
  auto output = detail::make_output_like<value_type>(y);
  ::ksj::special::atan2(y, x, output.view());
  return output;
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto atan2(const Pooled& y, const Pooled& x) {
  return ::ksj::special::atan2(y.view(), x.view());
}

template <typename LhsView, typename RhsView, typename OutputView>
void pow(LhsView base, RhsView exponent, OutputView output)
  requires(detail::same_value_binary_special_view_group<LhsView, RhsView, OutputView>)
{
  using value_type = detail::special_view_value_t<LhsView>;
  detail::dispatch_binary_view(
    base, exponent, output, detail::prefer_intel_vml_real<value_type>(base.size()),
    [](auto lhs, auto rhs, auto destination) {
      return detail::intel::pow(lhs, rhs, destination);
    },
    [](const auto& lhs, const auto& rhs) -> value_type {
      return static_cast<value_type>(pow(lhs, rhs));
    },
    "special pow view shape mismatch");
}

template <detail::non_vector_special_view View> [[nodiscard]] auto pow(View base, View exponent) {
  using value_type = detail::special_view_value_t<View>;
  auto output = detail::make_output_like<value_type>(base);
  ::ksj::special::pow(base, exponent, output.view());
  return output;
}

template <detail::non_vector_special_pooled Pooled> [[nodiscard]] auto pow(const Pooled& base, const Pooled& exponent) {
  return ::ksj::special::pow(base.view(), exponent.view());
}

template <typename View, typename Exponent, typename OutputView>
void pow(View base, const Exponent exponent, OutputView output)
  requires(detail::same_value_special_view_pair<View, OutputView> && !detail::special_view<Exponent>)
{
  using value_type = detail::special_view_value_t<View>;
  const auto converted_exponent = static_cast<value_type>(exponent);
  detail::dispatch_unary_view(
    base, output, detail::prefer_intel_vml_real<value_type>(base.size()),
    [converted_exponent](auto source, auto destination) {
      return detail::intel::pow(source, converted_exponent, destination);
    },
    [converted_exponent](const auto& value) -> value_type {
      return static_cast<value_type>(pow(value, converted_exponent));
    },
    "special pow view output shape mismatch");
}

template <detail::non_vector_special_view View, typename Exponent>
  requires(!detail::special_view<Exponent>)
[[nodiscard]] auto pow(View base, const Exponent exponent) {
  using value_type = detail::special_view_value_t<View>;
  auto output = detail::make_output_like<value_type>(base);
  ::ksj::special::pow(base, exponent, output.view());
  return output;
}

template <detail::non_vector_special_pooled Pooled, typename Exponent>
  requires(!detail::special_view<Exponent> && !detail::non_vector_special_pooled<Exponent>)
[[nodiscard]] auto pow(const Pooled& base, const Exponent exponent) {
  return ::ksj::special::pow(base.view(), exponent);
}

} // namespace ksj::special
