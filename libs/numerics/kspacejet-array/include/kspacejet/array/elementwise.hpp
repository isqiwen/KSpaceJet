#pragma once

/// Elementwise arithmetic and predicates. View overloads write caller-provided output; return overloads allocate one
/// Pooled result.

#include "kspacejet/array/detail/array_policy.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_elementwise.hpp"
#include "kspacejet/array/detail/intel/intel_array_elementwise.hpp"
#include "kspacejet/array/detail/intel/intel_array_vml.hpp"
#include "kspacejet/array/detail/runtime_checks.hpp"
#include "kspacejet/array/pooled_array4d.hpp"
#include "kspacejet/array/pooled_cube.hpp"
#include "kspacejet/array/pooled_image.hpp"
#include "kspacejet/array/pooled_matrix.hpp"
#include "kspacejet/array/pooled_vector.hpp"
#include "kspacejet/array/copy.hpp"
#include "kspacejet/array/permutation.hpp"
#include "kspacejet/array/scalar_traits.hpp"
#include "kspacejet/array/transforms.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

namespace ksj::array {

namespace detail {

template <typename T> [[nodiscard]] bool scalar_isfinite(const T& value) {
  if constexpr (is_complex_v<T>) {
    using std::isfinite;
    return isfinite(value.real()) && isfinite(value.imag());
  } else {
    using std::isfinite;
    return isfinite(value);
  }
}

template <typename T>
inline constexpr bool unsigned_integral_scalar_v =
  std::is_integral_v<std::remove_cv_t<T>> && std::is_unsigned_v<std::remove_cv_t<T>>;

template <typename First, typename Second, typename Third>
inline constexpr bool same_unsigned_integral_scalars_v =
  unsigned_integral_scalar_v<First> && unsigned_integral_scalar_v<Second> && unsigned_integral_scalar_v<Third> &&
  std::is_same_v<std::remove_cv_t<First>, std::remove_cv_t<Second>> &&
  std::is_same_v<std::remove_cv_t<First>, std::remove_cv_t<Third>>;

template <typename T, typename Replacement>
[[nodiscard]] auto scalar_replace_nan(const T& value, const Replacement& replacement) {
  if constexpr (is_complex_v<T>) {
    using value_type = std::remove_const_t<T>;
    using real_type = real_scalar_t<T>;
    using std::isnan;
    return value_type{isnan(value.real()) ? static_cast<real_type>(replacement.real()) : value.real(),
                      isnan(value.imag()) ? static_cast<real_type>(replacement.imag()) : value.imag()};
  } else {
    using std::isnan;
    return isnan(value) ? replacement : value;
  }
}

template <typename InputView, typename OutputView> void reverse_rows_2d(InputView input, OutputView output) {
  const auto rows = input.rows();
  const auto cols = input.cols();
  for (std::size_t row = 0U; row < rows; ++row) {
    const auto source_row = rows - 1U - row;
    std::copy_n(input.data() + source_row * input.row_stride(), cols, output.data() + row * output.row_stride());
  }
}

template <typename InputView, typename OutputView> void reverse_cols_2d(InputView input, OutputView output) {
  const auto cols = input.cols();
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    const auto* source = input.data() + row * input.row_stride();
    std::reverse_copy(source, source + cols, output.data() + row * output.row_stride());
  }
}

template <typename InputView, typename OutputView> void rotate_180_2d(InputView input, OutputView output) {
  if (input.is_contiguous() && output.is_contiguous()) {
    std::reverse_copy(input.data(), input.data() + input.size(), output.data());
    return;
  }

  const auto rows = input.rows();
  const auto cols = input.cols();
  for (std::size_t row = 0U; row < rows; ++row) {
    const auto source_row = rows - 1U - row;
    const auto* source = input.data() + source_row * input.row_stride();
    std::reverse_copy(source, source + cols, output.data() + row * output.row_stride());
  }
}

template <typename T> struct is_vector_view : std::false_type {};

template <typename T> struct is_vector_view<VectorView<T>> : std::true_type {};

template <typename T> inline constexpr bool is_vector_view_v = is_vector_view<std::remove_cvref_t<T>>::value;

template <typename T> struct is_array_view : std::false_type {};

template <typename T> struct is_array_view<VectorView<T>> : std::true_type {};

template <typename T> struct is_array_view<MatrixView<T>> : std::true_type {};

template <typename T> struct is_array_view<ImageView<T>> : std::true_type {};

template <typename T> struct is_array_view<CubeView<T>> : std::true_type {};

template <typename T> struct is_array_view<Array4DView<T>> : std::true_type {};

template <typename T> struct is_pooled_array : std::false_type {};

template <typename T> struct is_pooled_array<PooledVector<T>> : std::true_type {};

template <typename T> struct is_pooled_array<PooledMatrix<T>> : std::true_type {};

template <typename T> struct is_pooled_array<PooledImage<T>> : std::true_type {};

template <typename T> struct is_pooled_array<PooledCube<T>> : std::true_type {};

template <typename T> struct is_pooled_array<PooledArray4D<T>> : std::true_type {};

template <typename T>
inline constexpr bool array_input_v =
  is_array_view<std::remove_cvref_t<T>>::value || is_pooled_array<std::remove_cvref_t<T>>::value;

template <typename T> [[nodiscard]] constexpr bool small_direct_vector_elementwise_enabled() noexcept {
  using value_type = std::remove_cv_t<T>;
  return std::is_same_v<value_type, float> || std::is_same_v<value_type, double> ||
         std::is_same_v<value_type, ksj::base::cf32> || std::is_same_v<value_type, ksj::base::cf64>;
}

template <typename T> [[nodiscard]] constexpr std::size_t small_direct_vector_elementwise_max_elements() noexcept {
  if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
    return 128U;
  } else if constexpr (std::is_same_v<std::remove_cv_t<T>, double>) {
    return 64U;
  } else if constexpr (std::is_same_v<std::remove_cv_t<T>, ksj::base::cf32> ||
                       std::is_same_v<std::remove_cv_t<T>, ksj::base::cf64>) {
    return 16U;
  } else {
    return 0U;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t direct_vector_divide_max_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::cf32> || std::is_same_v<value_type, ksj::base::cf64>) {
    return 0U;
  } else if constexpr (std::is_same_v<value_type, float>) {
    return 256U;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return 128U;
  } else {
    return small_direct_vector_elementwise_max_elements<T>();
  }
}

template <typename View> using view_pointer_element_t = std::remove_pointer_t<decltype(std::declval<View>().data())>;

template <typename T> [[nodiscard]] decltype(auto) backend_view(T&& value) noexcept {
  if constexpr (requires { std::forward<T>(value).view(); }) {
    return std::forward<T>(value).view();
  } else {
    return std::forward<T>(value);
  }
}

template <typename Input> using backend_view_t = std::remove_cvref_t<decltype(backend_view(std::declval<Input>()))>;

template <typename Input> using array_value_t = typename backend_view_t<Input>::value_type;

template <typename T> [[nodiscard]] auto scalar_sqrt(const T& value) {
  using std::sqrt;
  return sqrt(value);
}

template <typename T> [[nodiscard]] auto scalar_exp(const T& value) {
  using std::exp;
  return exp(value);
}

template <typename T> [[nodiscard]] auto scalar_log(const T& value) {
  using std::log;
  return log(value);
}

template <typename Input> using negate_result_t = std::remove_cvref_t<decltype(-std::declval<array_value_t<Input>>())>;

template <typename Input>
using absolute_result_t = std::conditional_t<is_complex_v<array_value_t<Input>>, real_scalar_t<array_value_t<Input>>,
                                             std::remove_cv_t<array_value_t<Input>>>;

template <typename Input>
using square_result_t = std::remove_cvref_t<decltype(std::declval<const array_value_t<Input>&>() *
                                                     std::declval<const array_value_t<Input>&>())>;

template <typename Input>
using sqrt_result_t = std::remove_cvref_t<decltype(scalar_sqrt(std::declval<const array_value_t<Input>&>()))>;

template <typename Input>
using exp_result_t = std::remove_cvref_t<decltype(scalar_exp(std::declval<const array_value_t<Input>&>()))>;

template <typename Input>
using log_result_t = std::remove_cvref_t<decltype(scalar_log(std::declval<const array_value_t<Input>&>()))>;

template <typename Lhs, typename Rhs>
using add_result_t =
  std::remove_cvref_t<decltype(std::declval<const array_value_t<Lhs>&>() + std::declval<const array_value_t<Rhs>&>())>;

template <typename Lhs, typename Rhs>
using subtract_result_t =
  std::remove_cvref_t<decltype(std::declval<const array_value_t<Lhs>&>() - std::declval<const array_value_t<Rhs>&>())>;

template <typename Lhs, typename Rhs>
using multiply_result_t =
  std::remove_cvref_t<decltype(std::declval<const array_value_t<Lhs>&>() * std::declval<const array_value_t<Rhs>&>())>;

template <typename Lhs, typename Rhs>
using divide_result_t =
  std::remove_cvref_t<decltype(std::declval<const array_value_t<Lhs>&>() / std::declval<const array_value_t<Rhs>&>())>;

template <typename Input, typename Scalar>
using add_scalar_result_t =
  std::remove_cvref_t<decltype(std::declval<const array_value_t<Input>&>() + std::declval<const Scalar&>())>;

template <typename Input, typename Scalar>
using scale_result_t =
  std::remove_cvref_t<decltype(std::declval<const array_value_t<Input>&>() * std::declval<const Scalar&>())>;

template <typename Input, typename Scalar>
using subtract_scalar_result_t =
  std::remove_cvref_t<decltype(std::declval<const array_value_t<Input>&>() - std::declval<const Scalar&>())>;

template <typename Input, typename Scalar>
using scalar_subtract_result_t =
  std::remove_cvref_t<decltype(std::declval<const Scalar&>() - std::declval<const array_value_t<Input>&>())>;

template <typename Input, typename Scalar>
using divide_scalar_result_t =
  std::remove_cvref_t<decltype(std::declval<const array_value_t<Input>&>() / std::declval<const Scalar&>())>;

template <typename Input, typename Scalar>
using scalar_divide_result_t =
  std::remove_cvref_t<decltype(std::declval<const Scalar&>() / std::declval<const array_value_t<Input>&>())>;

template <typename Input, typename Replacement>
using replace_nan_result_t = std::remove_cvref_t<decltype(scalar_replace_nan(
  std::declval<const array_value_t<Input>&>(), std::declval<const Replacement&>()))>;

template <typename OutputT, typename T> [[nodiscard]] auto make_pooled_like(VectorView<T> input) {
  return make_pooled_vector<OutputT>(input.size());
}

template <typename OutputT, typename T> [[nodiscard]] auto make_pooled_like(MatrixView<T> input) {
  return make_pooled_matrix<OutputT>(input.rows(), input.cols());
}

template <typename OutputT, typename T> [[nodiscard]] auto make_pooled_like(ImageView<T> input) {
  return make_pooled_image<OutputT>(input.rows(), input.cols());
}

template <typename OutputT, typename T> [[nodiscard]] auto make_pooled_like(CubeView<T> input) {
  return make_pooled_cube<OutputT>(input.dim0(), input.dim1(), input.dim2());
}

template <typename OutputT, typename T> [[nodiscard]] auto make_pooled_like(Array4DView<T> input) {
  return make_pooled_array4d<OutputT>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
}

template <typename OutputT, typename Input, typename Operation>
[[nodiscard]] auto make_unary_result(Input&& input, Operation&& operation) {
  auto input_view = backend_view(std::forward<Input>(input));
  auto output = make_pooled_like<OutputT>(input_view);
  std::forward<Operation>(operation)(input_view, output.view());
  return output;
}

template <typename OutputT, typename Lhs, typename Rhs, typename Operation>
[[nodiscard]] auto make_binary_result(Lhs&& lhs, Rhs&& rhs, Operation&& operation) {
  auto lhs_view = backend_view(std::forward<Lhs>(lhs));
  auto rhs_view = backend_view(std::forward<Rhs>(rhs));
  auto output = make_pooled_like<OutputT>(lhs_view);
  std::forward<Operation>(operation)(lhs_view, rhs_view, output.view());
  return output;
}

template <typename OutputT, typename First, typename Second, typename Third, typename Operation>
[[nodiscard]] auto make_ternary_result(First&& first, Second&& second, Third&& third, Operation&& operation) {
  auto first_view = backend_view(std::forward<First>(first));
  auto second_view = backend_view(std::forward<Second>(second));
  auto third_view = backend_view(std::forward<Third>(third));
  auto output = make_pooled_like<OutputT>(first_view);
  std::forward<Operation>(operation)(first_view, second_view, third_view, output.view());
  return output;
}

template <typename View> [[nodiscard]] auto contiguous_vector_view(View input) noexcept {
  return VectorView<view_pointer_element_t<View>>(input.data(), input.size());
}

template <typename Span> [[nodiscard]] auto vector_view_from_span(Span input) noexcept {
  return VectorView<typename std::remove_reference_t<Span>::element_type>(input.data(), input.size());
}

template <typename Lhs, typename Rhs> [[nodiscard]] bool same_shape_for_backend(Lhs lhs, Rhs rhs) noexcept {
  if constexpr (requires {
                  lhs.shape().extents;
                  rhs.shape().extents;
                }) {
    return lhs.shape().extents == rhs.shape().extents;
  } else {
    return lhs.size() == rhs.size();
  }
}

template <typename Input, typename Output>
[[nodiscard]] bool exact_same_vector_storage(Input input, Output output) noexcept {
  return input.data() == output.data() && input.size() == output.size();
}

template <typename Input, typename Output>
[[nodiscard]] bool contiguous_vector_ranges_overlap(Input input, Output output) noexcept {
  if (input.empty() || output.empty()) {
    return false;
  }
  const auto* input_begin = input.data();
  const auto* input_end = input_begin + input.size();
  const auto* output_begin = output.data();
  const auto* output_end = output_begin + output.size();
  return input_begin < output_end && output_begin < input_end;
}

template <typename Input, typename Output>
[[nodiscard]] bool safe_direct_vector_output(Input input, Output output) noexcept {
  if constexpr (runtime_checks_enabled) {
    return exact_same_vector_storage(input, output) || !contiguous_vector_ranges_overlap(input, output);
  } else {
    (void)input;
    (void)output;
    return true;
  }
}

template <typename Input, typename Output>
[[nodiscard]] bool backend_alias_check_passes(Input input, Output output) noexcept {
  if constexpr (runtime_checks_enabled) {
    return !views_may_overlap(input, output);
  } else {
    (void)input;
    (void)output;
    return true;
  }
}

template <typename Input, typename Output>
[[nodiscard]] bool prefer_small_intel_divide_scalar_before_direct(Input input, Output output) noexcept {
  using input_type = std::remove_cv_t<typename Input::value_type>;
  using output_type = std::remove_cv_t<typename Output::value_type>;
  const auto direct_max = small_direct_vector_elementwise_max_elements<output_type>();
  return std::is_same_v<input_type, output_type> &&
         (std::is_same_v<output_type, float> || std::is_same_v<output_type, double>) && input.size() <= 1024U &&
         input.size() > direct_max && input.is_contiguous() && output.is_contiguous();
}

template <typename Input, typename Output>
[[nodiscard]] bool can_use_direct_unary_vector_loop(Input input, Output output,
                                                    const std::size_t max_elements) noexcept {
  using output_type = typename Output::value_type;
  using input_type = typename Input::value_type;
  return std::is_same_v<std::remove_cv_t<input_type>, std::remove_cv_t<output_type>> &&
         small_direct_vector_elementwise_enabled<output_type>() && input.size() == output.size() &&
         input.size() <= max_elements && input.is_contiguous() && output.is_contiguous() &&
         safe_direct_vector_output(input, output);
}

template <typename Lhs, typename Rhs, typename Output>
[[nodiscard]] bool can_use_direct_binary_vector_loop(Lhs lhs, Rhs rhs, Output output,
                                                     const std::size_t max_elements) noexcept {
  using output_type = typename Output::value_type;
  using lhs_type = typename Lhs::value_type;
  using rhs_type = typename Rhs::value_type;
  return std::is_same_v<std::remove_cv_t<lhs_type>, std::remove_cv_t<output_type>> &&
         std::is_same_v<std::remove_cv_t<rhs_type>, std::remove_cv_t<output_type>> &&
         small_direct_vector_elementwise_enabled<output_type>() && lhs.size() == rhs.size() &&
         lhs.size() == output.size() && lhs.size() <= max_elements && lhs.is_contiguous() && rhs.is_contiguous() &&
         output.is_contiguous() && safe_direct_vector_output(lhs, output) && safe_direct_vector_output(rhs, output);
}

template <typename Input, typename Output, typename Function>
[[nodiscard]] bool try_direct_unary_vector_loop(Input input, Output output, const std::size_t max_elements,
                                                Function&& function) {
  if (!can_use_direct_unary_vector_loop(input, output, max_elements)) {
    return false;
  }
  const auto* input_data = input.data();
  auto* output_data = output.data();
  for (std::size_t index = 0U; index < input.size(); ++index) {
    output_data[index] = function(input_data[index]);
  }
  return true;
}

template <typename Lhs, typename Rhs, typename Output, typename Function>
[[nodiscard]] bool try_direct_binary_vector_loop(Lhs lhs, Rhs rhs, Output output, const std::size_t max_elements,
                                                 Function&& function) {
  if (!can_use_direct_binary_vector_loop(lhs, rhs, output, max_elements)) {
    return false;
  }
  const auto* lhs_data = lhs.data();
  const auto* rhs_data = rhs.data();
  auto* output_data = output.data();
  for (std::size_t index = 0U; index < lhs.size(); ++index) {
    output_data[index] = function(lhs_data[index], rhs_data[index]);
  }
  return true;
}

template <typename Input, typename Output, typename Function>
[[nodiscard]] bool try_small_direct_unary_vector_loop(Input input, Output output, Function&& function) {
  return try_direct_unary_vector_loop(input, output,
                                      small_direct_vector_elementwise_max_elements<typename Output::value_type>(),
                                      std::forward<Function>(function));
}

template <typename Lhs, typename Rhs, typename Output, typename Function>
[[nodiscard]] bool try_small_direct_binary_vector_loop(Lhs lhs, Rhs rhs, Output output, Function&& function) {
  return try_direct_binary_vector_loop(lhs, rhs, output,
                                       small_direct_vector_elementwise_max_elements<typename Output::value_type>(),
                                       std::forward<Function>(function));
}

template <typename Input, typename Output>
[[nodiscard]] bool can_use_unary_backend_blocks(Input input, Output output) noexcept {
  return same_shape_for_backend(input, output) && backend_alias_check_passes(input, output);
}

template <typename Lhs, typename Rhs, typename Output>
[[nodiscard]] bool can_use_binary_backend_blocks(Lhs lhs, Rhs rhs, Output output) noexcept {
  return same_shape_for_backend(lhs, rhs) && same_shape_for_backend(lhs, output) &&
         backend_alias_check_passes(lhs, output) && backend_alias_check_passes(rhs, output);
}

template <typename First, typename... Rest>
[[nodiscard]] bool can_use_backend_block_dispatch(First first, Rest... rest) noexcept {
  if (!has_matching_inner_contiguous_blocks(first, rest...)) {
    return false;
  }

  const auto length = inner_block_length(first);
  const auto count = inner_block_count(first);
  if (count == 0U || count > ArrayDispatchPolicy::backend_block_max_count) {
    return false;
  }

  return length >= ArrayDispatchPolicy::backend_block_min_elements ||
         count <= ArrayDispatchPolicy::backend_short_block_max_count;
}

template <typename Input, typename Output, typename Backend>
[[nodiscard]] bool try_unary_vector_backend_blocks(Input input, Output output, Backend&& backend) {
  if (!can_use_unary_backend_blocks(input, output)) {
    return false;
  }

  const auto const_input = as_const_view(input);
  if (const_input.is_contiguous() && output.is_contiguous()) {
    return std::forward<Backend>(backend)(contiguous_vector_view(const_input), contiguous_vector_view(output));
  }
  if (!can_use_backend_block_dispatch(const_input, output)) {
    return false;
  }

  bool success = true;
  bool saw_block = false;
  auto block_function = [&](auto input_block, auto output_block) {
    saw_block = true;
    if (success) {
      success = std::forward<Backend>(backend)(vector_view_from_span(input_block), vector_view_from_span(output_block));
    }
  };
  return for_each_inner_contiguous_span_group(block_function, const_input, output) && saw_block && success;
}

template <typename Lhs, typename Rhs, typename Output, typename Backend>
[[nodiscard]] bool try_binary_vector_backend_blocks(Lhs lhs, Rhs rhs, Output output, Backend&& backend) {
  if (!can_use_binary_backend_blocks(lhs, rhs, output)) {
    return false;
  }

  const auto const_lhs = as_const_view(lhs);
  const auto const_rhs = as_const_view(rhs);
  if (const_lhs.is_contiguous() && const_rhs.is_contiguous() && output.is_contiguous()) {
    return std::forward<Backend>(backend)(contiguous_vector_view(const_lhs), contiguous_vector_view(const_rhs),
                                          contiguous_vector_view(output));
  }
  if (!can_use_backend_block_dispatch(const_lhs, const_rhs, output)) {
    return false;
  }

  bool success = true;
  bool saw_block = false;
  auto block_function = [&](auto lhs_block, auto rhs_block, auto output_block) {
    saw_block = true;
    if (success) {
      success = std::forward<Backend>(backend)(vector_view_from_span(lhs_block), vector_view_from_span(rhs_block),
                                               vector_view_from_span(output_block));
    }
  };
  return for_each_inner_contiguous_span_group(block_function, const_lhs, const_rhs, output) && saw_block && success;
}

template <VectorElementwiseOperation Operation, typename Lhs, typename Rhs, typename Output, typename IntelFn,
          typename EigenFn>
[[nodiscard]] bool try_binary_vector_backends(Lhs&& lhs, Rhs&& rhs, Output&& output, IntelFn&& intel_fn,
                                              EigenFn&& eigen_fn) {
  auto lhs_view = backend_view(std::forward<Lhs>(lhs));
  auto rhs_view = backend_view(std::forward<Rhs>(rhs));
  auto output_view = backend_view(std::forward<Output>(output));
  const auto policy_element_count = lhs_view.size();
  using value_type = std::remove_cv_t<typename decltype(output_view)::value_type>;
  const auto intel_size_enabled = [&] {
    if constexpr (Operation == VectorElementwiseOperation::add) {
      return intel_multidimensional_add_size_enabled<value_type>(policy_element_count);
    } else {
      return true;
    }
  }();
  return try_binary_vector_backend_blocks(
    lhs_view, rhs_view, output_view, [&](auto const_lhs, auto const_rhs, auto out) {
      if (intel_size_enabled &&
          prefer_intel_vector_elementwise_for_elements(Operation, policy_element_count, const_lhs, const_rhs, out) &&
          std::forward<IntelFn>(intel_fn)(const_lhs, const_rhs, out)) {
        return true;
      }
      if (prefer_eigen_vector_elementwise_for_elements(Operation, policy_element_count, const_lhs, const_rhs, out) &&
          std::forward<EigenFn>(eigen_fn)(const_lhs, const_rhs, out)) {
        return true;
      }
      return false;
    });
}

template <VectorElementwiseOperation Operation, typename Input, typename Output, typename IntelFn, typename EigenFn>
[[nodiscard]] bool try_unary_vector_backends(Input&& input, Output&& output, IntelFn&& intel_fn, EigenFn&& eigen_fn) {
  auto input_view = backend_view(std::forward<Input>(input));
  auto output_view = backend_view(std::forward<Output>(output));
  const auto policy_element_count = input_view.size();
  return try_unary_vector_backend_blocks(input_view, output_view, [&](auto const_input, auto out) {
    if (prefer_intel_vector_elementwise_for_elements(Operation, policy_element_count, const_input, out) &&
        std::forward<IntelFn>(intel_fn)(const_input, out)) {
      return true;
    }
    if (prefer_eigen_vector_elementwise_for_elements(Operation, policy_element_count, const_input, out) &&
        std::forward<EigenFn>(eigen_fn)(const_input, out)) {
      return true;
    }
    return false;
  });
}

template <VectorElementwiseOperation Operation, typename Input, typename Output, typename VmlFn, typename IntelFn,
          typename EigenFn>
[[nodiscard]] bool try_unary_vector_backends_with_mkl_vml(Input&& input, Output&& output, VmlFn&& vml_fn,
                                                          IntelFn&& intel_fn, EigenFn&& eigen_fn) {
  auto input_view = backend_view(std::forward<Input>(input));
  auto output_view = backend_view(std::forward<Output>(output));
  const auto policy_element_count = input_view.size();
  return try_unary_vector_backend_blocks(input_view, output_view, [&](auto const_input, auto out) {
    if (prefer_mkl_vml_vector_unary_elementwise_for_elements(Operation, policy_element_count, const_input, out) &&
        std::forward<VmlFn>(vml_fn)(const_input, out)) {
      return true;
    }
    if (prefer_intel_vector_elementwise_for_elements(Operation, policy_element_count, const_input, out) &&
        std::forward<IntelFn>(intel_fn)(const_input, out)) {
      return true;
    }
    if (prefer_eigen_vector_elementwise_for_elements(Operation, policy_element_count, const_input, out) &&
        std::forward<EigenFn>(eigen_fn)(const_input, out)) {
      return true;
    }
    return false;
  });
}

template <VectorElementwiseOperation Operation, typename Lhs, typename Rhs, typename Output, typename VmlFn,
          typename IntelFn, typename EigenFn>
[[nodiscard]] bool try_binary_vector_backends_with_mkl_vml(Lhs&& lhs, Rhs&& rhs, Output&& output, VmlFn&& vml_fn,
                                                           IntelFn&& intel_fn, EigenFn&& eigen_fn) {
  auto lhs_view = backend_view(std::forward<Lhs>(lhs));
  auto rhs_view = backend_view(std::forward<Rhs>(rhs));
  auto output_view = backend_view(std::forward<Output>(output));
  const auto policy_element_count = lhs_view.size();
  return try_binary_vector_backend_blocks(
    lhs_view, rhs_view, output_view, [&](auto const_lhs, auto const_rhs, auto out) {
      if (prefer_mkl_vml_vector_binary_elementwise_for_elements(Operation, policy_element_count, const_lhs, const_rhs,
                                                                out) &&
          std::forward<VmlFn>(vml_fn)(const_lhs, const_rhs, out)) {
        return true;
      }
      if (prefer_intel_vector_elementwise_for_elements(Operation, policy_element_count, const_lhs, const_rhs, out) &&
          std::forward<IntelFn>(intel_fn)(const_lhs, const_rhs, out)) {
        return true;
      }
      if (prefer_eigen_vector_elementwise_for_elements(Operation, policy_element_count, const_lhs, const_rhs, out) &&
          std::forward<EigenFn>(eigen_fn)(const_lhs, const_rhs, out)) {
        return true;
      }
      return false;
    });
}

template <VectorElementwiseOperation Operation, typename Input, typename Scalar, typename Output, typename IntelFn,
          typename EigenFn>
[[nodiscard]] bool try_scalar_vector_backends(Input&& input, const Scalar& scalar, Output&& output, IntelFn&& intel_fn,
                                              EigenFn&& eigen_fn) {
  auto input_view = backend_view(std::forward<Input>(input));
  auto output_view = backend_view(std::forward<Output>(output));
  using output_value_type = std::remove_const_t<typename decltype(output_view)::value_type>;
  if constexpr (std::is_convertible_v<Scalar, output_value_type>) {
    const auto output_scalar = static_cast<output_value_type>(scalar);
    const auto policy_element_count = input_view.size();
    return try_unary_vector_backend_blocks(input_view, output_view, [&](auto const_input, auto out) {
      if (prefer_intel_vector_elementwise_for_elements(Operation, policy_element_count, const_input, out) &&
          std::forward<IntelFn>(intel_fn)(const_input, output_scalar, out)) {
        return true;
      }
      if (prefer_eigen_vector_elementwise_for_elements(Operation, policy_element_count, const_input, out) &&
          std::forward<EigenFn>(eigen_fn)(const_input, output_scalar, out)) {
        return true;
      }
      return false;
    });
  } else {
    return false;
  }
}

template <typename Input, typename Lower, typename Upper, typename Output, typename IntelFn, typename EigenFn>
[[nodiscard]] bool try_clamp_vector_backends(Input&& input, const Lower& lower, const Upper& upper, Output&& output,
                                             IntelFn&& intel_fn, EigenFn&& eigen_fn) {
  auto input_view = backend_view(std::forward<Input>(input));
  auto output_view = backend_view(std::forward<Output>(output));
  using output_value_type = std::remove_const_t<typename decltype(output_view)::value_type>;
  if constexpr (std::is_convertible_v<Lower, output_value_type> && std::is_convertible_v<Upper, output_value_type>) {
    const auto output_lower = static_cast<output_value_type>(lower);
    const auto output_upper = static_cast<output_value_type>(upper);
    const auto policy_element_count = input_view.size();
    return try_unary_vector_backend_blocks(input_view, output_view, [&](auto const_input, auto out) {
      if (prefer_intel_vector_elementwise_for_elements(VectorElementwiseOperation::clamp, policy_element_count,
                                                       const_input, out) &&
          std::forward<IntelFn>(intel_fn)(const_input, output_lower, output_upper, out)) {
        return true;
      }
      if (prefer_eigen_vector_elementwise_for_elements(VectorElementwiseOperation::clamp, policy_element_count,
                                                       const_input, out) &&
          std::forward<EigenFn>(eigen_fn)(const_input, output_lower, output_upper, out)) {
        return true;
      }
      return false;
    });
  } else {
    return false;
  }
}

template <typename Input, typename Output>
[[nodiscard]] bool try_absolute_vector_backends(Input&& input, Output&& output) {
  auto input_view = backend_view(std::forward<Input>(input));
  auto output_view = backend_view(std::forward<Output>(output));
  const auto policy_element_count = input_view.size();
  return try_unary_vector_backend_blocks(input_view, output_view, [policy_element_count](auto const_input, auto out) {
    using input_type = std::remove_cv_t<typename decltype(const_input)::value_type>;
    using output_type = std::remove_cv_t<typename decltype(out)::value_type>;
    if constexpr (is_complex_v<input_type> && std::is_same_v<output_type, real_scalar_t<input_type>>) {
      if (prefer_mkl_vml_complex_magnitude_for_elements(policy_element_count, const_input, out) &&
          intel::vml::absolute(const_input, out)) {
        return true;
      }
      if (prefer_intel_complex_magnitude_for_elements(policy_element_count, const_input, out) &&
          intel::absolute(const_input, out)) {
        return true;
      }
      if (prefer_eigen_complex_magnitude_for_elements(policy_element_count, const_input, out) &&
          eigen::absolute(const_input, out)) {
        return true;
      }
    } else {
      if (prefer_intel_vector_elementwise_for_elements(VectorElementwiseOperation::absolute, policy_element_count,
                                                       const_input, out) &&
          intel::absolute(const_input, out)) {
        return true;
      }
      if (prefer_eigen_vector_elementwise_for_elements(VectorElementwiseOperation::absolute, policy_element_count,
                                                       const_input, out) &&
          eigen::absolute(const_input, out)) {
        return true;
      }
    }
    return false;
  });
}

} // namespace detail

template <typename InputT, typename OutputT, typename Lower, typename Upper>
void clamp(VectorView<InputT> input, VectorView<OutputT> output, const Lower& lower, const Upper& upper) {
  if constexpr (std::is_convertible_v<Lower, std::remove_const_t<OutputT>> &&
                std::is_convertible_v<Upper, std::remove_const_t<OutputT>>) {
    if (detail::try_small_direct_unary_vector_loop(input, output, [&](const auto& value) {
          using value_type = std::remove_const_t<InputT>;
          return std::clamp(value, static_cast<value_type>(lower), static_cast<value_type>(upper));
        })) {
      return;
    }
    const auto const_input = as_const_view(input);
    const auto output_lower = static_cast<std::remove_const_t<OutputT>>(lower);
    const auto output_upper = static_cast<std::remove_const_t<OutputT>>(upper);
    if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::clamp, input, output) &&
        detail::intel::clamp(const_input, output_lower, output_upper, output)) {
      return;
    }
    if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::clamp, input, output) &&
        detail::eigen::clamp(const_input, output_lower, output_upper, output)) {
      return;
    }
  }
  ksj::array::transform(input, output, [&](const auto& value) {
    using value_type = std::remove_const_t<InputT>;
    return std::clamp(value, static_cast<value_type>(lower), static_cast<value_type>(upper));
  });
}

template <typename Input, typename Output, typename Lower, typename Upper>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void clamp(Input&& input, Output&& output, const Lower& lower, const Upper& upper) {
  if (detail::try_clamp_vector_backends(
        input, lower, upper, output,
        [](auto const_input, auto low, auto high, auto out) {
          return detail::intel::clamp(const_input, low, high, out);
        },
        [](auto const_input, auto low, auto high, auto out) {
          return detail::eigen::clamp(const_input, low, high, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [&](const auto& value) {
    using value_type = std::remove_const_t<typename std::remove_reference_t<Input>::value_type>;
    return std::clamp(value, static_cast<value_type>(lower), static_cast<value_type>(upper));
  });
}

template <typename InputView, typename OutputView, typename Lower, typename Upper>
void clip(InputView input, OutputView output, const Lower& lower, const Upper& upper) {
  clamp(input, output, lower, upper);
}

template <typename InputView, typename OutputView> void isfinite(InputView input, OutputView output) {
  ksj::array::transform(input, output, [](const auto& value) {
    return detail::scalar_isfinite(value);
  });
}

template <typename InputView, typename OutputView, typename Replacement>
void replace_nan(InputView input, OutputView output, const Replacement& replacement) {
  ksj::array::transform(input, output, [&](const auto& value) {
    return detail::scalar_replace_nan(value, replacement);
  });
}

template <typename T, typename UnaryFunction>
[[nodiscard]] auto transform(const PooledVector<T>& input, UnaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<UnaryFunction, const T&>>;
  auto output = make_pooled_vector<output_type>(input.size());
  transform(input.view(), output.view(), std::forward<UnaryFunction>(function));
  return output;
}

template <typename T, typename UnaryFunction>
[[nodiscard]] auto transform(const PooledMatrix<T>& input, UnaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<UnaryFunction, const T&>>;
  auto output = make_pooled_matrix<output_type>(input.rows(), input.cols());
  transform(input.view(), output.view(), std::forward<UnaryFunction>(function));
  return output;
}

template <typename T, typename UnaryFunction>
[[nodiscard]] auto transform(const PooledImage<T>& input, UnaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<UnaryFunction, const T&>>;
  auto output = make_pooled_image<output_type>(input.rows(), input.cols());
  transform(input.view(), output.view(), std::forward<UnaryFunction>(function));
  return output;
}

template <typename T, typename UnaryFunction>
[[nodiscard]] auto transform(const PooledCube<T>& input, UnaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<UnaryFunction, const T&>>;
  auto output = make_pooled_cube<output_type>(input.dim0(), input.dim1(), input.dim2());
  transform(input.view(), output.view(), std::forward<UnaryFunction>(function));
  return output;
}

template <typename T, typename UnaryFunction>
[[nodiscard]] auto transform(const PooledArray4D<T>& input, UnaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<UnaryFunction, const T&>>;
  auto output = make_pooled_array4d<output_type>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  transform(input.view(), output.view(), std::forward<UnaryFunction>(function));
  return output;
}

template <typename T, typename U, typename BinaryFunction>
[[nodiscard]] auto transform(const PooledVector<T>& lhs, const PooledVector<U>& rhs, BinaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<BinaryFunction, const T&, const U&>>;
  auto output = make_pooled_vector<output_type>(lhs.size());
  transform(lhs.view(), rhs.view(), output.view(), std::forward<BinaryFunction>(function));
  return output;
}

template <typename T, typename U, typename BinaryFunction>
[[nodiscard]] auto transform(const PooledMatrix<T>& lhs, const PooledMatrix<U>& rhs, BinaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<BinaryFunction, const T&, const U&>>;
  auto output = make_pooled_matrix<output_type>(lhs.rows(), lhs.cols());
  transform(lhs.view(), rhs.view(), output.view(), std::forward<BinaryFunction>(function));
  return output;
}

template <typename T, typename U, typename BinaryFunction>
[[nodiscard]] auto transform(const PooledImage<T>& lhs, const PooledImage<U>& rhs, BinaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<BinaryFunction, const T&, const U&>>;
  auto output = make_pooled_image<output_type>(lhs.rows(), lhs.cols());
  transform(lhs.view(), rhs.view(), output.view(), std::forward<BinaryFunction>(function));
  return output;
}

template <typename T, typename U, typename BinaryFunction>
[[nodiscard]] auto transform(const PooledCube<T>& lhs, const PooledCube<U>& rhs, BinaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<BinaryFunction, const T&, const U&>>;
  auto output = make_pooled_cube<output_type>(lhs.dim0(), lhs.dim1(), lhs.dim2());
  transform(lhs.view(), rhs.view(), output.view(), std::forward<BinaryFunction>(function));
  return output;
}

template <typename T, typename U, typename BinaryFunction>
[[nodiscard]] auto transform(const PooledArray4D<T>& lhs, const PooledArray4D<U>& rhs, BinaryFunction&& function) {
  using output_type = std::remove_cvref_t<std::invoke_result_t<BinaryFunction, const T&, const U&>>;
  auto output = make_pooled_array4d<output_type>(lhs.dim0(), lhs.dim1(), lhs.dim2(), lhs.dim3());
  transform(lhs.view(), rhs.view(), output.view(), std::forward<BinaryFunction>(function));
  return output;
}

template <typename T> [[nodiscard]] PooledVector<T> reverse(const PooledVector<T>& input) {
  auto output = make_pooled_vector<T>(input.size());
  std::reverse_copy(input.data(), input.data() + input.size(), output.data());
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<T> reverse_rows(const PooledMatrix<T>& input) {
  auto output = make_pooled_matrix<T>(input.rows(), input.cols());
  detail::reverse_rows_2d(input.view(), output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<T> reverse_cols(const PooledMatrix<T>& input) {
  auto output = make_pooled_matrix<T>(input.rows(), input.cols());
  detail::reverse_cols_2d(input.view(), output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledImage<T> reverse_rows(const PooledImage<T>& input) {
  auto output = make_pooled_image<T>(input.rows(), input.cols());
  detail::reverse_rows_2d(input.view(), output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledImage<T> reverse_cols(const PooledImage<T>& input) {
  auto output = make_pooled_image<T>(input.rows(), input.cols());
  detail::reverse_cols_2d(input.view(), output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<T> rotate_180(const PooledMatrix<T>& input) {
  auto output = make_pooled_matrix<T>(input.rows(), input.cols());
  detail::rotate_180_2d(input.view(), output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledImage<T> rotate_180(const PooledImage<T>& input) {
  auto output = make_pooled_image<T>(input.rows(), input.cols());
  detail::rotate_180_2d(input.view(), output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<std::remove_const_t<T>> transpose(MatrixView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = make_pooled_matrix<value_type>(input.cols(), input.rows());
  transpose(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<T> transpose(const PooledMatrix<T>& input) {
  auto output = make_pooled_matrix<T>(input.cols(), input.rows());
  transpose(input.view(), output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledImage<std::remove_const_t<T>> transpose(ImageView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = make_pooled_image<value_type>(input.cols(), input.rows());
  transpose(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledImage<T> transpose(const PooledImage<T>& input) {
  auto output = make_pooled_image<T>(input.cols(), input.rows());
  transpose(input.view(), output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<std::remove_const_t<T>> transpose_rotated_180(MatrixView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = make_pooled_matrix<value_type>(input.cols(), input.rows());
  transpose_rotated_180(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<T> transpose_rotated_180(const PooledMatrix<T>& input) {
  auto output = make_pooled_matrix<T>(input.cols(), input.rows());
  transpose_rotated_180(input.view(), output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledImage<std::remove_const_t<T>> transpose_rotated_180(ImageView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = make_pooled_image<value_type>(input.cols(), input.rows());
  transpose_rotated_180(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledImage<T> transpose_rotated_180(const PooledImage<T>& input) {
  auto output = make_pooled_image<T>(input.cols(), input.rows());
  transpose_rotated_180(input.view(), output.view());
  return output;
}

template <typename LhsT, typename RhsT, typename OutputT>
void add(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output) {
  if (detail::try_small_direct_binary_vector_loop(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
        return lhs_value + rhs_value;
      })) {
    return;
  }
  const auto const_lhs = as_const_view(lhs);
  const auto const_rhs = as_const_view(rhs);
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::add, lhs, rhs, output) &&
      detail::intel::add(const_lhs, const_rhs, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::add, lhs, rhs, output) &&
      detail::eigen::add(const_lhs, const_rhs, output)) {
    return;
  }
  ksj::array::transform(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
    return lhs_value + rhs_value;
  });
}

template <typename Lhs, typename Rhs, typename Output>
  requires(!(detail::is_vector_view_v<Lhs> && detail::is_vector_view_v<Rhs> && detail::is_vector_view_v<Output>))
void add(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  if (detail::try_binary_vector_backends<detail::VectorElementwiseOperation::add>(
        lhs, rhs, output,
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::intel::add(const_lhs, const_rhs, out);
        },
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::eigen::add(const_lhs, const_rhs, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), std::forward<Output>(output),
                        [](const auto& lhs_value, const auto& rhs_value) {
                          return lhs_value + rhs_value;
                        });
}

template <typename LhsT, typename RhsT, typename OutputT>
void subtract(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output) {
  if (detail::try_small_direct_binary_vector_loop(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
        return lhs_value - rhs_value;
      })) {
    return;
  }
  const auto const_lhs = as_const_view(lhs);
  const auto const_rhs = as_const_view(rhs);
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::subtract, lhs, rhs, output) &&
      detail::intel::subtract(const_lhs, const_rhs, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::subtract, lhs, rhs, output) &&
      detail::eigen::subtract(const_lhs, const_rhs, output)) {
    return;
  }
  ksj::array::transform(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
    return lhs_value - rhs_value;
  });
}

template <typename Lhs, typename Rhs, typename Output>
  requires(!(detail::is_vector_view_v<Lhs> && detail::is_vector_view_v<Rhs> && detail::is_vector_view_v<Output>))
void subtract(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  if (detail::try_binary_vector_backends<detail::VectorElementwiseOperation::subtract>(
        lhs, rhs, output,
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::intel::subtract(const_lhs, const_rhs, out);
        },
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::eigen::subtract(const_lhs, const_rhs, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), std::forward<Output>(output),
                        [](const auto& lhs_value, const auto& rhs_value) {
                          return lhs_value - rhs_value;
                        });
}

template <typename First, typename Second, typename Third, typename Output>
void add_subtract(First&& first, Second&& second, Third&& third, Output&& output) {
  ksj::array::transform(std::forward<First>(first), std::forward<Second>(second), std::forward<Third>(third),
                        std::forward<Output>(output),
                        [](const auto& first_value, const auto& second_value, const auto& third_value) {
                          return first_value + second_value - third_value;
                        });
}

template <typename LhsT, typename RhsT, typename OutputT>
void multiply(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output) {
  if (detail::try_small_direct_binary_vector_loop(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
        return lhs_value * rhs_value;
      })) {
    return;
  }
  const auto const_lhs = as_const_view(lhs);
  const auto const_rhs = as_const_view(rhs);
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::multiply, lhs, rhs, output) &&
      detail::intel::multiply(const_lhs, const_rhs, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::multiply, lhs, rhs, output) &&
      detail::eigen::multiply(const_lhs, const_rhs, output)) {
    return;
  }
  ksj::array::transform(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
    return lhs_value * rhs_value;
  });
}

template <typename Lhs, typename Rhs, typename Output>
  requires(!(detail::is_vector_view_v<Lhs> && detail::is_vector_view_v<Rhs> && detail::is_vector_view_v<Output>))
void multiply(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  if (detail::try_binary_vector_backends<detail::VectorElementwiseOperation::multiply>(
        lhs, rhs, output,
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::intel::multiply(const_lhs, const_rhs, out);
        },
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::eigen::multiply(const_lhs, const_rhs, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), std::forward<Output>(output),
                        [](const auto& lhs_value, const auto& rhs_value) {
                          return lhs_value * rhs_value;
                        });
}

template <typename LhsT, typename RhsT, typename OutputT>
void multiply_accumulate(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output) {
  const auto const_lhs = as_const_view(lhs);
  const auto const_rhs = as_const_view(rhs);
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::multiply, lhs, rhs, output) &&
      detail::intel::multiply_accumulate(const_lhs, const_rhs, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::multiply, lhs, rhs, output) &&
      detail::eigen::multiply_accumulate(const_lhs, const_rhs, output)) {
    return;
  }
  ksj::array::transform(lhs, rhs, output, output,
                        [](const auto& lhs_value, const auto& rhs_value, const auto& output_value) {
                          return output_value + lhs_value * rhs_value;
                        });
}

template <typename Lhs, typename Rhs, typename Output>
  requires(!(detail::is_vector_view_v<Lhs> && detail::is_vector_view_v<Rhs> && detail::is_vector_view_v<Output>))
void multiply_accumulate(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  if (detail::try_binary_vector_backends<detail::VectorElementwiseOperation::multiply>(
        lhs, rhs, output,
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::intel::multiply_accumulate(const_lhs, const_rhs, out);
        },
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::eigen::multiply_accumulate(const_lhs, const_rhs, out);
        })) {
    return;
  }
  auto lhs_view = detail::backend_view(std::forward<Lhs>(lhs));
  auto rhs_view = detail::backend_view(std::forward<Rhs>(rhs));
  auto output_view = detail::backend_view(std::forward<Output>(output));
  ksj::array::transform(lhs_view, rhs_view, output_view, output_view,
                        [](const auto& lhs_value, const auto& rhs_value, const auto& output_value) {
                          return output_value + lhs_value * rhs_value;
                        });
}

template <typename LhsT, typename RhsT, typename OutputT>
void divide(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output) {
  if (detail::try_direct_binary_vector_loop(lhs, rhs, output, detail::direct_vector_divide_max_elements<OutputT>(),
                                            [](const auto& lhs_value, const auto& rhs_value) {
                                              return lhs_value / rhs_value;
                                            })) {
    return;
  }
  const auto const_lhs = as_const_view(lhs);
  const auto const_rhs = as_const_view(rhs);
  if (detail::prefer_mkl_vml_vector_divide(const_lhs, const_rhs, output) &&
      detail::intel::vml::divide(const_lhs, const_rhs, output)) {
    return;
  }
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::divide, lhs, rhs, output) &&
      detail::intel::divide(const_lhs, const_rhs, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::divide, lhs, rhs, output) &&
      detail::eigen::divide(const_lhs, const_rhs, output)) {
    return;
  }
  ksj::array::transform(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
    return lhs_value / rhs_value;
  });
}

template <typename Lhs, typename Rhs, typename Output>
  requires(!(detail::is_vector_view_v<Lhs> && detail::is_vector_view_v<Rhs> && detail::is_vector_view_v<Output>))
void divide(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  if (detail::try_binary_vector_backends<detail::VectorElementwiseOperation::divide>(
        lhs, rhs, output,
        [](auto const_lhs, auto const_rhs, auto out) {
          if (detail::prefer_mkl_vml_vector_divide(const_lhs, const_rhs, out) &&
              detail::intel::vml::divide(const_lhs, const_rhs, out)) {
            return true;
          }
          return detail::intel::divide(const_lhs, const_rhs, out);
        },
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::eigen::divide(const_lhs, const_rhs, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), std::forward<Output>(output),
                        [](const auto& lhs_value, const auto& rhs_value) {
                          return lhs_value / rhs_value;
                        });
}

template <typename InputT, typename Scalar, typename OutputT>
void add_scalar(VectorView<InputT> input, const Scalar& scalar, VectorView<OutputT> output) {
  if constexpr (std::is_convertible_v<Scalar, std::remove_const_t<OutputT>>) {
    if (detail::try_small_direct_unary_vector_loop(input, output, [&scalar](const auto& value) {
          return value + scalar;
        })) {
      return;
    }
    const auto const_input = as_const_view(input);
    const auto output_scalar = static_cast<std::remove_const_t<OutputT>>(scalar);
    if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::add_scalar, input, output) &&
        detail::intel::add_scalar(const_input, output_scalar, output)) {
      return;
    }
    if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::add_scalar, input, output) &&
        detail::eigen::add_scalar(const_input, output_scalar, output)) {
      return;
    }
  }
  ksj::array::transform(input, output, [&scalar](const auto& value) {
    return value + scalar;
  });
}

template <typename Input, typename Scalar, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void add_scalar(Input&& input, const Scalar& scalar, Output&& output) {
  if (detail::try_scalar_vector_backends<detail::VectorElementwiseOperation::add_scalar>(
        input, scalar, output,
        [](auto const_input, auto value, auto out) {
          return detail::intel::add_scalar(const_input, value, out);
        },
        [](auto const_input, auto value, auto out) {
          return detail::eigen::add_scalar(const_input, value, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [&scalar](const auto& value) {
    return value + scalar;
  });
}

template <typename InputT, typename Scalar, typename OutputT>
void scale(VectorView<InputT> input, const Scalar& scalar, VectorView<OutputT> output) {
  if constexpr (std::is_convertible_v<Scalar, std::remove_const_t<OutputT>>) {
    if (detail::try_small_direct_unary_vector_loop(input, output, [&scalar](const auto& value) {
          return value * scalar;
        })) {
      return;
    }
    const auto const_input = as_const_view(input);
    const auto output_scalar = static_cast<std::remove_const_t<OutputT>>(scalar);
    if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::scale, input, output) &&
        detail::intel::scale(const_input, output_scalar, output)) {
      return;
    }
    if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::scale, input, output) &&
        detail::eigen::scale(const_input, output_scalar, output)) {
      return;
    }
  }
  ksj::array::transform(input, output, [&scalar](const auto& value) {
    return value * scalar;
  });
}

template <typename Input, typename Scalar, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void scale(Input&& input, const Scalar& scalar, Output&& output) {
  if (detail::try_scalar_vector_backends<detail::VectorElementwiseOperation::scale>(
        input, scalar, output,
        [](auto const_input, auto value, auto out) {
          return detail::intel::scale(const_input, value, out);
        },
        [](auto const_input, auto value, auto out) {
          return detail::eigen::scale(const_input, value, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [&scalar](const auto& value) {
    return value * scalar;
  });
}

template <typename InputT, typename Scalar, typename OutputT>
void subtract_scalar(VectorView<InputT> input, const Scalar& scalar, VectorView<OutputT> output) {
  if constexpr (std::is_convertible_v<Scalar, std::remove_const_t<OutputT>>) {
    if (detail::try_small_direct_unary_vector_loop(input, output, [&scalar](const auto& value) {
          return value - scalar;
        })) {
      return;
    }
    const auto const_input = as_const_view(input);
    const auto output_scalar = static_cast<std::remove_const_t<OutputT>>(scalar);
    if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::subtract_scalar, input, output) &&
        detail::intel::subtract_scalar(const_input, output_scalar, output)) {
      return;
    }
    if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::subtract_scalar, input, output) &&
        detail::eigen::subtract_scalar(const_input, output_scalar, output)) {
      return;
    }
  }
  ksj::array::transform(input, output, [&scalar](const auto& value) {
    return value - scalar;
  });
}

template <typename Input, typename Scalar, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void subtract_scalar(Input&& input, const Scalar& scalar, Output&& output) {
  if (detail::try_scalar_vector_backends<detail::VectorElementwiseOperation::subtract_scalar>(
        input, scalar, output,
        [](auto const_input, auto value, auto out) {
          return detail::intel::subtract_scalar(const_input, value, out);
        },
        [](auto const_input, auto value, auto out) {
          return detail::eigen::subtract_scalar(const_input, value, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [&scalar](const auto& value) {
    return value - scalar;
  });
}

template <typename InputT, typename Scalar, typename OutputT>
void scalar_subtract(VectorView<InputT> input, const Scalar& scalar, VectorView<OutputT> output) {
  if constexpr (std::is_convertible_v<Scalar, std::remove_const_t<OutputT>>) {
    if (detail::try_small_direct_unary_vector_loop(input, output, [&scalar](const auto& value) {
          return scalar - value;
        })) {
      return;
    }
    const auto const_input = as_const_view(input);
    const auto output_scalar = static_cast<std::remove_const_t<OutputT>>(scalar);
    if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::scalar_subtract, input, output) &&
        detail::intel::scalar_subtract(const_input, output_scalar, output)) {
      return;
    }
    if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::scalar_subtract, input, output) &&
        detail::eigen::scalar_subtract(const_input, output_scalar, output)) {
      return;
    }
  }
  ksj::array::transform(input, output, [&scalar](const auto& value) {
    return scalar - value;
  });
}

template <typename Input, typename Scalar, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void scalar_subtract(Input&& input, const Scalar& scalar, Output&& output) {
  if (detail::try_scalar_vector_backends<detail::VectorElementwiseOperation::scalar_subtract>(
        input, scalar, output,
        [](auto const_input, auto value, auto out) {
          return detail::intel::scalar_subtract(const_input, value, out);
        },
        [](auto const_input, auto value, auto out) {
          return detail::eigen::scalar_subtract(const_input, value, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [&scalar](const auto& value) {
    return scalar - value;
  });
}

template <typename InputT, typename Scalar, typename OutputT>
void divide_scalar(VectorView<InputT> input, const Scalar& scalar, VectorView<OutputT> output) {
  if constexpr (std::is_convertible_v<Scalar, std::remove_const_t<OutputT>>) {
    const auto const_input = as_const_view(input);
    const auto output_scalar = static_cast<std::remove_const_t<OutputT>>(scalar);
    if (detail::prefer_small_intel_divide_scalar_before_direct(input, output) &&
        detail::intel::divide_scalar(const_input, output_scalar, output)) {
      return;
    }
    if (detail::try_small_direct_unary_vector_loop(input, output, [&scalar](const auto& value) {
          return value / scalar;
        })) {
      return;
    }
    if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::divide_scalar, input, output) &&
        detail::intel::divide_scalar(const_input, output_scalar, output)) {
      return;
    }
    if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::divide_scalar, input, output) &&
        detail::eigen::divide_scalar(const_input, output_scalar, output)) {
      return;
    }
  }
  ksj::array::transform(input, output, [&scalar](const auto& value) {
    return value / scalar;
  });
}

template <typename Input, typename Scalar, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void divide_scalar(Input&& input, const Scalar& scalar, Output&& output) {
  if (detail::try_scalar_vector_backends<detail::VectorElementwiseOperation::divide_scalar>(
        input, scalar, output,
        [](auto const_input, auto value, auto out) {
          return detail::intel::divide_scalar(const_input, value, out);
        },
        [](auto const_input, auto value, auto out) {
          return detail::eigen::divide_scalar(const_input, value, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [&scalar](const auto& value) {
    return value / scalar;
  });
}

template <typename InputT, typename Scalar, typename OutputT>
void scalar_divide(VectorView<InputT> input, const Scalar& scalar, VectorView<OutputT> output) {
  if constexpr (std::is_convertible_v<Scalar, std::remove_const_t<OutputT>>) {
    if (detail::try_small_direct_unary_vector_loop(input, output, [&scalar](const auto& value) {
          return scalar / value;
        })) {
      return;
    }
    const auto const_input = as_const_view(input);
    const auto output_scalar = static_cast<std::remove_const_t<OutputT>>(scalar);
    if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::scalar_divide, input, output) &&
        detail::intel::scalar_divide(const_input, output_scalar, output)) {
      return;
    }
    if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::scalar_divide, input, output) &&
        detail::eigen::scalar_divide(const_input, output_scalar, output)) {
      return;
    }
  }
  ksj::array::transform(input, output, [&scalar](const auto& value) {
    return scalar / value;
  });
}

template <typename Input, typename Scalar, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void scalar_divide(Input&& input, const Scalar& scalar, Output&& output) {
  if (detail::try_scalar_vector_backends<detail::VectorElementwiseOperation::scalar_divide>(
        input, scalar, output,
        [](auto const_input, auto value, auto out) {
          return detail::intel::scalar_divide(const_input, value, out);
        },
        [](auto const_input, auto value, auto out) {
          return detail::eigen::scalar_divide(const_input, value, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [&scalar](const auto& value) {
    return scalar / value;
  });
}

template <typename InputT, typename OutputT> void negate(VectorView<InputT> input, VectorView<OutputT> output) {
  if (detail::try_small_direct_unary_vector_loop(input, output, [](const auto& value) {
        return -value;
      })) {
    return;
  }
  const auto const_input = as_const_view(input);
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::negate, input, output) &&
      detail::intel::negate(const_input, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::negate, input, output) &&
      detail::eigen::negate(const_input, output)) {
    return;
  }
  ksj::array::transform(input, output, [](const auto& value) {
    return -value;
  });
}

template <typename Input, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void negate(Input&& input, Output&& output) {
  if (detail::try_unary_vector_backends<detail::VectorElementwiseOperation::negate>(
        input, output,
        [](auto const_input, auto out) {
          return detail::intel::negate(const_input, out);
        },
        [](auto const_input, auto out) {
          return detail::eigen::negate(const_input, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [](const auto& value) {
    return -value;
  });
}

template <typename InputT, typename OutputT> void absolute(VectorView<InputT> input, VectorView<OutputT> output) {
  using input_type = std::remove_cv_t<InputT>;
  using output_type = std::remove_cv_t<OutputT>;
  if constexpr (!is_complex_v<input_type>) {
    if (detail::try_small_direct_unary_vector_loop(input, output, [](const auto& value) {
          using std::abs;
          return abs(value);
        })) {
      return;
    }
  }
  const auto const_input = as_const_view(input);
  if constexpr (is_complex_v<input_type> && std::is_same_v<output_type, real_scalar_t<input_type>>) {
    if (detail::prefer_mkl_vml_complex_magnitude(const_input, output) &&
        detail::intel::vml::absolute(const_input, output)) {
      return;
    }
    if (detail::prefer_intel_complex_magnitude(const_input, output) && detail::intel::absolute(const_input, output)) {
      return;
    }
    if (detail::prefer_eigen_complex_magnitude(const_input, output) && detail::eigen::absolute(const_input, output)) {
      return;
    }
  } else {
    if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::absolute, input, output) &&
        detail::intel::absolute(const_input, output)) {
      return;
    }
    if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::absolute, input, output) &&
        detail::eigen::absolute(const_input, output)) {
      return;
    }
  }
  ksj::array::transform(input, output, [](const auto& value) {
    using std::abs;
    return abs(value);
  });
}

template <typename Input, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void absolute(Input&& input, Output&& output) {
  if (detail::try_absolute_vector_backends(input, output)) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [](const auto& value) {
    using std::abs;
    return abs(value);
  });
}

template <typename Input, typename Output> void abs(Input&& input, Output&& output) {
  ksj::array::absolute(std::forward<Input>(input), std::forward<Output>(output));
}

template <typename InputT, typename OutputT> void square(VectorView<InputT> input, VectorView<OutputT> output) {
  if (detail::try_small_direct_unary_vector_loop(input, output, [](const auto& value) {
        return value * value;
      })) {
    return;
  }
  const auto const_input = as_const_view(input);
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::square, input, output) &&
      detail::intel::square(const_input, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::square, input, output) &&
      detail::eigen::square(const_input, output)) {
    return;
  }
  ksj::array::transform(input, output, [](const auto& value) {
    return value * value;
  });
}

template <typename Input, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void square(Input&& input, Output&& output) {
  if (detail::try_unary_vector_backends<detail::VectorElementwiseOperation::square>(
        input, output,
        [](auto const_input, auto out) {
          return detail::intel::square(const_input, out);
        },
        [](auto const_input, auto out) {
          return detail::eigen::square(const_input, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [](const auto& value) {
    return value * value;
  });
}

template <typename InputT, typename OutputT> void sqrt(VectorView<InputT> input, VectorView<OutputT> output) {
  if (detail::try_direct_unary_vector_loop(input, output, 16U, [](const auto& value) {
        using std::sqrt;
        return sqrt(value);
      })) {
    return;
  }
  const auto const_input = as_const_view(input);
  if (detail::prefer_mkl_vml_vector_unary_elementwise(detail::VectorElementwiseOperation::sqrt, input, output) &&
      detail::intel::vml::sqrt(const_input, output)) {
    return;
  }
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::sqrt, input, output) &&
      detail::intel::sqrt(const_input, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::sqrt, input, output) &&
      detail::eigen::sqrt(const_input, output)) {
    return;
  }
  ksj::array::transform(input, output, [](const auto& value) {
    using std::sqrt;
    return sqrt(value);
  });
}

template <typename Input, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void sqrt(Input&& input, Output&& output) {
  if (detail::try_unary_vector_backends_with_mkl_vml<detail::VectorElementwiseOperation::sqrt>(
        input, output,
        [](auto const_input, auto out) {
          return detail::intel::vml::sqrt(const_input, out);
        },
        [](auto const_input, auto out) {
          return detail::intel::sqrt(const_input, out);
        },
        [](auto const_input, auto out) {
          return detail::eigen::sqrt(const_input, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [](const auto& value) {
    using std::sqrt;
    return sqrt(value);
  });
}

/// Writes the reciprocal, 1 / input, for each real floating-point element into `output`.
template <typename InputT, typename OutputT> void inverse(VectorView<InputT> input, VectorView<OutputT> output) {
  static_assert(std::is_same_v<std::remove_const_t<InputT>, std::remove_const_t<OutputT>> &&
                  std::is_floating_point_v<std::remove_const_t<InputT>>,
                "array inverse requires matching float or double input and output views");
  if (detail::try_unary_vector_backends_with_mkl_vml<detail::VectorElementwiseOperation::inverse>(
        input, output,
        [](auto const_input, auto out) {
          return detail::intel::vml::inverse(const_input, out);
        },
        [](auto, auto) {
          return false;
        },
        [](auto const_input, auto out) {
          return detail::eigen::inverse(const_input, out);
        })) {
    return;
  }
  ksj::array::transform(input, output, [](const auto value) {
    using value_type = std::remove_cv_t<decltype(value)>;
    return value_type{1} / value;
  });
}

/// Writes the reciprocal, 1 / input, for each real floating-point element into `output`.
template <typename Input, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void inverse(Input&& input, Output&& output) {
  using input_type = std::remove_cv_t<detail::array_value_t<Input>>;
  using output_type = std::remove_cv_t<detail::array_value_t<Output>>;
  static_assert(std::is_same_v<input_type, output_type> && std::is_floating_point_v<input_type>,
                "array inverse requires matching float or double input and output arrays");
  if (detail::try_unary_vector_backends_with_mkl_vml<detail::VectorElementwiseOperation::inverse>(
        input, output,
        [](auto const_input, auto out) {
          return detail::intel::vml::inverse(const_input, out);
        },
        [](auto, auto) {
          return false;
        },
        [](auto const_input, auto out) {
          return detail::eigen::inverse(const_input, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [](const auto value) {
    using value_type = std::remove_cv_t<decltype(value)>;
    return value_type{1} / value;
  });
}

/// Writes 1 / sqrt(input) for each positive real floating-point element into `output`.
template <typename InputT, typename OutputT> void rsqrt(VectorView<InputT> input, VectorView<OutputT> output) {
  static_assert(std::is_same_v<std::remove_const_t<InputT>, std::remove_const_t<OutputT>> &&
                  std::is_floating_point_v<std::remove_const_t<InputT>>,
                "array rsqrt requires matching float or double input and output views");
  if (detail::try_unary_vector_backends_with_mkl_vml<detail::VectorElementwiseOperation::inverse_sqrt>(
        input, output,
        [](auto const_input, auto out) {
          return detail::intel::vml::inverse_sqrt(const_input, out);
        },
        [](auto, auto) {
          return false;
        },
        [](auto const_input, auto out) {
          return detail::eigen::inverse_sqrt(const_input, out);
        })) {
    return;
  }
  ksj::array::transform(input, output, [](const auto value) {
    using value_type = std::remove_cv_t<decltype(value)>;
    return value_type{1} / std::sqrt(value);
  });
}

/// Writes 1 / sqrt(input) for each positive real floating-point element into `output`.
template <typename Input, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void rsqrt(Input&& input, Output&& output) {
  using input_type = std::remove_cv_t<detail::array_value_t<Input>>;
  using output_type = std::remove_cv_t<detail::array_value_t<Output>>;
  static_assert(std::is_same_v<input_type, output_type> && std::is_floating_point_v<input_type>,
                "array rsqrt requires matching float or double input and output arrays");
  if (detail::try_unary_vector_backends_with_mkl_vml<detail::VectorElementwiseOperation::inverse_sqrt>(
        input, output,
        [](auto const_input, auto out) {
          return detail::intel::vml::inverse_sqrt(const_input, out);
        },
        [](auto, auto) {
          return false;
        },
        [](auto const_input, auto out) {
          return detail::eigen::inverse_sqrt(const_input, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [](const auto value) {
    using value_type = std::remove_cv_t<decltype(value)>;
    return value_type{1} / std::sqrt(value);
  });
}

/// Compatibility synonym for `rsqrt`; writes 1 / sqrt(input) into `output`.
template <typename Input, typename Output> void inverse_sqrt(Input&& input, Output&& output) {
  ksj::array::rsqrt(std::forward<Input>(input), std::forward<Output>(output));
}

template <typename InputT, typename OutputT> void exp(VectorView<InputT> input, VectorView<OutputT> output) {
  const auto const_input = as_const_view(input);
  if (detail::prefer_mkl_vml_vector_unary_elementwise(detail::VectorElementwiseOperation::exp, input, output) &&
      detail::intel::vml::exp(const_input, output)) {
    return;
  }
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::exp, input, output) &&
      detail::intel::exp(const_input, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::exp, input, output) &&
      detail::eigen::exp(const_input, output)) {
    return;
  }
  ksj::array::transform(input, output, [](const auto& value) {
    using std::exp;
    return exp(value);
  });
}

template <typename Input, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void exp(Input&& input, Output&& output) {
  if (detail::try_unary_vector_backends_with_mkl_vml<detail::VectorElementwiseOperation::exp>(
        input, output,
        [](auto const_input, auto out) {
          return detail::intel::vml::exp(const_input, out);
        },
        [](auto const_input, auto out) {
          return detail::intel::exp(const_input, out);
        },
        [](auto const_input, auto out) {
          return detail::eigen::exp(const_input, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [](const auto& value) {
    using std::exp;
    return exp(value);
  });
}

template <typename InputT, typename OutputT> void log(VectorView<InputT> input, VectorView<OutputT> output) {
  if constexpr (std::is_same_v<std::remove_cv_t<InputT>, float> && std::is_same_v<std::remove_cv_t<OutputT>, float>) {
    if (detail::try_direct_unary_vector_loop(input, output, 16U, [](const auto& value) {
          using std::log;
          return log(value);
        })) {
      return;
    }
  }
  const auto const_input = as_const_view(input);
  if (detail::prefer_mkl_vml_vector_unary_elementwise(detail::VectorElementwiseOperation::log, input, output) &&
      detail::intel::vml::log(const_input, output)) {
    return;
  }
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::log, input, output) &&
      detail::intel::log(const_input, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::log, input, output) &&
      detail::eigen::log(const_input, output)) {
    return;
  }
  ksj::array::transform(input, output, [](const auto& value) {
    using std::log;
    return log(value);
  });
}

template <typename Input, typename Output>
  requires(!(detail::is_vector_view_v<Input> && detail::is_vector_view_v<Output>))
void log(Input&& input, Output&& output) {
  if (detail::try_unary_vector_backends_with_mkl_vml<detail::VectorElementwiseOperation::log>(
        input, output,
        [](auto const_input, auto out) {
          return detail::intel::vml::log(const_input, out);
        },
        [](auto const_input, auto out) {
          return detail::intel::log(const_input, out);
        },
        [](auto const_input, auto out) {
          return detail::eigen::log(const_input, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [](const auto& value) {
    using std::log;
    return log(value);
  });
}

template <typename LhsT, typename RhsT, typename OutputT>
void bitwise_and(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output)
  requires(detail::same_unsigned_integral_scalars_v<LhsT, RhsT, OutputT>)
{
  detail::validate_same_size(lhs.size(), rhs.size(), "vector view bitwise_and input size mismatch");
  detail::validate_same_size(lhs.size(), output.size(), "vector view bitwise_and output size mismatch");

  const auto const_lhs = as_const_view(lhs);
  const auto const_rhs = as_const_view(rhs);
  if (detail::intel::bitwise_and(const_lhs, const_rhs, output)) {
    return;
  }
  ksj::array::transform(lhs, rhs, output, [](const auto lhs_value, const auto rhs_value) {
    return static_cast<std::remove_const_t<OutputT>>(lhs_value & rhs_value);
  });
}

template <typename InputT, typename OutputT>
void bitwise_not(VectorView<InputT> input, VectorView<OutputT> output)
  requires(detail::same_unsigned_integral_scalars_v<InputT, OutputT, OutputT>)
{
  detail::validate_same_size(input.size(), output.size(), "vector view bitwise_not output size mismatch");

  const auto const_input = as_const_view(input);
  if (detail::intel::bitwise_not(const_input, output)) {
    return;
  }
  ksj::array::transform(input, output, [](const auto value) {
    return static_cast<std::remove_const_t<OutputT>>(~value);
  });
}

template <typename InputT, typename OutputT>
void bitwise_and_inplace(VectorView<InputT> input, VectorView<OutputT> output)
  requires(detail::same_unsigned_integral_scalars_v<InputT, OutputT, OutputT>)
{
  bitwise_and(input, output, output);
}

template <typename T>
void bitwise_not_inplace(VectorView<T> data)
  requires(detail::unsigned_integral_scalar_v<T>)
{
  bitwise_not(data, data);
}

template <typename Input, typename Scale, typename Addend, typename Output>
void scale_add(Input&& input, const Scale& scale, Addend&& addend, Output&& output) {
  ksj::array::transform(std::forward<Input>(input), std::forward<Addend>(addend), std::forward<Output>(output),
                        [&scale](const auto& input_value, const auto& addend_value) {
                          return input_value * scale + addend_value;
                        });
}

template <typename First, typename FirstScalar, typename Second, typename SecondScalar, typename Output>
void linear_combination(First&& first, const FirstScalar& first_scalar, Second&& second,
                        const SecondScalar& second_scalar, Output&& output) {
  ksj::array::transform(std::forward<First>(first), std::forward<Second>(second), std::forward<Output>(output),
                        [&first_scalar, &second_scalar](const auto& first_value, const auto& second_value) {
                          return first_scalar * first_value + second_scalar * second_value;
                        });
}

template <typename First, typename FirstScalar, typename Second, typename SecondScalar, typename Third,
          typename ThirdScalar, typename Output>
void linear_combination(First&& first, const FirstScalar& first_scalar, Second&& second,
                        const SecondScalar& second_scalar, Third&& third, const ThirdScalar& third_scalar,
                        Output&& output) {
  ksj::array::transform(std::forward<First>(first), std::forward<Second>(second), std::forward<Third>(third),
                        std::forward<Output>(output),
                        [&first_scalar, &second_scalar,
                         &third_scalar](const auto& first_value, const auto& second_value, const auto& third_value) {
                          return first_scalar * first_value + second_scalar * second_value + third_scalar * third_value;
                        });
}

template <typename First, typename FirstScalar, typename Second, typename SecondScalar, typename Third,
          typename ThirdScalar, typename Fourth, typename FourthScalar, typename Output>
void linear_combination(First&& first, const FirstScalar& first_scalar, Second&& second,
                        const SecondScalar& second_scalar, Third&& third, const ThirdScalar& third_scalar,
                        Fourth&& fourth, const FourthScalar& fourth_scalar, Output&& output) {
  ksj::array::transform(
    std::forward<First>(first), std::forward<Second>(second), std::forward<Third>(third), std::forward<Fourth>(fourth),
    std::forward<Output>(output),
    [&first_scalar, &second_scalar, &third_scalar, &fourth_scalar](const auto& first_value, const auto& second_value,
                                                                   const auto& third_value, const auto& fourth_value) {
      return first_scalar * first_value + second_scalar * second_value + third_scalar * third_value +
             fourth_scalar * fourth_value;
    });
}

template <typename First, typename FirstScalar, typename Second, typename SecondScalar, typename Third,
          typename ThirdScalar, typename Fourth, typename FourthScalar, typename Fifth, typename FifthScalar,
          typename Output>
void linear_combination(First&& first, const FirstScalar& first_scalar, Second&& second,
                        const SecondScalar& second_scalar, Third&& third, const ThirdScalar& third_scalar,
                        Fourth&& fourth, const FourthScalar& fourth_scalar, Fifth&& fifth,
                        const FifthScalar& fifth_scalar, Output&& output) {
  ksj::array::transform(std::forward<First>(first), std::forward<Second>(second), std::forward<Third>(third),
                        std::forward<Fourth>(fourth), std::forward<Fifth>(fifth), std::forward<Output>(output),
                        [&first_scalar, &second_scalar, &third_scalar, &fourth_scalar,
                         &fifth_scalar](const auto& first_value, const auto& second_value, const auto& third_value,
                                        const auto& fourth_value, const auto& fifth_value) {
                          return first_scalar * first_value + second_scalar * second_value +
                                 third_scalar * third_value + fourth_scalar * fourth_value + fifth_scalar * fifth_value;
                        });
}

template <typename LhsT, typename RhsT, typename OutputT>
void minimum(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output) {
  if (detail::try_small_direct_binary_vector_loop(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
        return std::min(lhs_value, rhs_value);
      })) {
    return;
  }
  const auto const_lhs = as_const_view(lhs);
  const auto const_rhs = as_const_view(rhs);
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::minimum, lhs, rhs, output) &&
      detail::intel::minimum(const_lhs, const_rhs, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::minimum, lhs, rhs, output) &&
      detail::eigen::minimum(const_lhs, const_rhs, output)) {
    return;
  }
  ksj::array::transform(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
    return std::min(lhs_value, rhs_value);
  });
}

template <typename Lhs, typename Rhs, typename Output>
  requires(!(detail::is_vector_view_v<Lhs> && detail::is_vector_view_v<Rhs> && detail::is_vector_view_v<Output>))
void minimum(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  if (detail::try_binary_vector_backends<detail::VectorElementwiseOperation::minimum>(
        lhs, rhs, output,
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::intel::minimum(const_lhs, const_rhs, out);
        },
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::eigen::minimum(const_lhs, const_rhs, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), std::forward<Output>(output),
                        [](const auto& lhs_value, const auto& rhs_value) {
                          return std::min(lhs_value, rhs_value);
                        });
}

template <typename LhsT, typename RhsT, typename OutputT>
void maximum(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output) {
  if (detail::try_small_direct_binary_vector_loop(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
        return std::max(lhs_value, rhs_value);
      })) {
    return;
  }
  const auto const_lhs = as_const_view(lhs);
  const auto const_rhs = as_const_view(rhs);
  if (detail::prefer_intel_vector_elementwise(detail::VectorElementwiseOperation::maximum, lhs, rhs, output) &&
      detail::intel::maximum(const_lhs, const_rhs, output)) {
    return;
  }
  if (detail::prefer_eigen_vector_elementwise(detail::VectorElementwiseOperation::maximum, lhs, rhs, output) &&
      detail::eigen::maximum(const_lhs, const_rhs, output)) {
    return;
  }
  ksj::array::transform(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
    return std::max(lhs_value, rhs_value);
  });
}

template <typename Lhs, typename Rhs, typename Output>
  requires(!(detail::is_vector_view_v<Lhs> && detail::is_vector_view_v<Rhs> && detail::is_vector_view_v<Output>))
void maximum(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  if (detail::try_binary_vector_backends<detail::VectorElementwiseOperation::maximum>(
        lhs, rhs, output,
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::intel::maximum(const_lhs, const_rhs, out);
        },
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::eigen::maximum(const_lhs, const_rhs, out);
        })) {
    return;
  }
  ksj::array::transform(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), std::forward<Output>(output),
                        [](const auto& lhs_value, const auto& rhs_value) {
                          return std::max(lhs_value, rhs_value);
                        });
}

/// Writes sqrt(lhs * lhs + rhs * rhs) for each pair of real floating-point elements into `output`.
template <typename LhsT, typename RhsT, typename OutputT>
void hypot(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output) {
  static_assert(std::is_same_v<std::remove_const_t<LhsT>, std::remove_const_t<RhsT>> &&
                  std::is_same_v<std::remove_const_t<LhsT>, std::remove_const_t<OutputT>> &&
                  std::is_floating_point_v<std::remove_const_t<LhsT>>,
                "array hypot requires matching float or double input and output views");
  if (detail::try_binary_vector_backends_with_mkl_vml<detail::VectorElementwiseOperation::hypot>(
        lhs, rhs, output,
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::intel::vml::hypot(const_lhs, const_rhs, out);
        },
        [](auto, auto, auto) {
          return false;
        },
        [](auto, auto, auto) {
          return false;
        })) {
    return;
  }
  ksj::array::transform(lhs, rhs, output, [](const auto lhs_value, const auto rhs_value) {
    return std::hypot(lhs_value, rhs_value);
  });
}

/// Writes sqrt(lhs * lhs + rhs * rhs) for each pair of real floating-point elements into `output`.
template <typename Lhs, typename Rhs, typename Output>
  requires(!(detail::is_vector_view_v<Lhs> && detail::is_vector_view_v<Rhs> && detail::is_vector_view_v<Output>))
void hypot(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  using lhs_type = std::remove_cv_t<detail::array_value_t<Lhs>>;
  using rhs_type = std::remove_cv_t<detail::array_value_t<Rhs>>;
  using output_type = std::remove_cv_t<detail::array_value_t<Output>>;
  static_assert(std::is_same_v<lhs_type, rhs_type> && std::is_same_v<lhs_type, output_type> &&
                  std::is_floating_point_v<lhs_type>,
                "array hypot requires matching float or double input and output arrays");
  if (detail::try_binary_vector_backends_with_mkl_vml<detail::VectorElementwiseOperation::hypot>(
        lhs, rhs, output,
        [](auto const_lhs, auto const_rhs, auto out) {
          return detail::intel::vml::hypot(const_lhs, const_rhs, out);
        },
        [](auto, auto, auto) {
          return false;
        },
        [](auto, auto, auto) {
          return false;
        })) {
    return;
  }
  ksj::array::transform(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), std::forward<Output>(output),
                        [](const auto lhs_value, const auto rhs_value) {
                          return std::hypot(lhs_value, rhs_value);
                        });
}

template <typename Lhs, typename Rhs, typename Output> void cwise_min(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  ksj::array::minimum(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), std::forward<Output>(output));
}

template <typename Lhs, typename Rhs, typename Output> void cwise_max(Lhs&& lhs, Rhs&& rhs, Output&& output) {
  ksj::array::maximum(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), std::forward<Output>(output));
}

template <typename Input, typename Lower, typename Upper>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto clamp(Input&& input, const Lower& lower, const Upper& upper) {
  using output_type = std::remove_cv_t<detail::array_value_t<Input>>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [&](auto in, auto out) {
    ksj::array::clamp(in, out, lower, upper);
  });
}

template <typename Input, typename Lower, typename Upper>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto clip(Input&& input, const Lower& lower, const Upper& upper) {
  return ksj::array::clamp(std::forward<Input>(input), lower, upper);
}

template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto isfinite(Input&& input) {
  return detail::make_unary_result<bool>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::isfinite(in, out);
  });
}

template <typename Input, typename Replacement>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto replace_nan(Input&& input, const Replacement& replacement) {
  using output_type = detail::replace_nan_result_t<Input, Replacement>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [&](auto in, auto out) {
    ksj::array::replace_nan(in, out, replacement);
  });
}

template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs>)
[[nodiscard]] auto add(Lhs&& lhs, Rhs&& rhs) {
  using output_type = detail::add_result_t<Lhs, Rhs>;
  return detail::make_binary_result<output_type>(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs),
                                                 [](auto lhs_view, auto rhs_view, auto out) {
                                                   ksj::array::add(lhs_view, rhs_view, out);
                                                 });
}

template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs>)
[[nodiscard]] auto subtract(Lhs&& lhs, Rhs&& rhs) {
  using output_type = detail::subtract_result_t<Lhs, Rhs>;
  return detail::make_binary_result<output_type>(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs),
                                                 [](auto lhs_view, auto rhs_view, auto out) {
                                                   ksj::array::subtract(lhs_view, rhs_view, out);
                                                 });
}

template <typename First, typename Second, typename Third>
  requires(detail::array_input_v<First> && detail::array_input_v<Second> && detail::array_input_v<Third>)
[[nodiscard]] auto add_subtract(First&& first, Second&& second, Third&& third) {
  using output_type = std::remove_cvref_t<decltype(std::declval<const detail::array_value_t<First>&>() +
                                                   std::declval<const detail::array_value_t<Second>&>() -
                                                   std::declval<const detail::array_value_t<Third>&>())>;
  return detail::make_ternary_result<output_type>(std::forward<First>(first), std::forward<Second>(second),
                                                  std::forward<Third>(third),
                                                  [](auto first_view, auto second_view, auto third_view, auto out) {
                                                    ksj::array::add_subtract(first_view, second_view, third_view, out);
                                                  });
}

template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs>)
[[nodiscard]] auto multiply(Lhs&& lhs, Rhs&& rhs) {
  using output_type = detail::multiply_result_t<Lhs, Rhs>;
  return detail::make_binary_result<output_type>(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs),
                                                 [](auto lhs_view, auto rhs_view, auto out) {
                                                   ksj::array::multiply(lhs_view, rhs_view, out);
                                                 });
}

template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs>)
[[nodiscard]] auto divide(Lhs&& lhs, Rhs&& rhs) {
  using output_type = detail::divide_result_t<Lhs, Rhs>;
  return detail::make_binary_result<output_type>(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs),
                                                 [](auto lhs_view, auto rhs_view, auto out) {
                                                   ksj::array::divide(lhs_view, rhs_view, out);
                                                 });
}

template <typename Input, typename Scalar>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto add_scalar(Input&& input, const Scalar& scalar) {
  using output_type = detail::add_scalar_result_t<Input, Scalar>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [&](auto in, auto out) {
    ksj::array::add_scalar(in, scalar, out);
  });
}

template <typename Input, typename Scalar>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto scale(Input&& input, const Scalar& scalar) {
  using output_type = detail::scale_result_t<Input, Scalar>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [&](auto in, auto out) {
    ksj::array::scale(in, scalar, out);
  });
}

template <typename Input, typename Scalar>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto subtract_scalar(Input&& input, const Scalar& scalar) {
  using output_type = detail::subtract_scalar_result_t<Input, Scalar>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [&](auto in, auto out) {
    ksj::array::subtract_scalar(in, scalar, out);
  });
}

template <typename Input, typename Scalar>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto scalar_subtract(Input&& input, const Scalar& scalar) {
  using output_type = detail::scalar_subtract_result_t<Input, Scalar>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [&](auto in, auto out) {
    ksj::array::scalar_subtract(in, scalar, out);
  });
}

template <typename Input, typename Scalar>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto divide_scalar(Input&& input, const Scalar& scalar) {
  using output_type = detail::divide_scalar_result_t<Input, Scalar>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [&](auto in, auto out) {
    ksj::array::divide_scalar(in, scalar, out);
  });
}

template <typename Input, typename Scalar>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto scalar_divide(Input&& input, const Scalar& scalar) {
  using output_type = detail::scalar_divide_result_t<Input, Scalar>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [&](auto in, auto out) {
    ksj::array::scalar_divide(in, scalar, out);
  });
}

template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto negate(Input&& input) {
  using output_type = detail::negate_result_t<Input>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::negate(in, out);
  });
}

template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto absolute(Input&& input) {
  using output_type = detail::absolute_result_t<Input>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::absolute(in, out);
  });
}

template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto abs(Input&& input) {
  return ksj::array::absolute(std::forward<Input>(input));
}

template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto square(Input&& input) {
  using output_type = detail::square_result_t<Input>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::square(in, out);
  });
}

template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto sqrt(Input&& input) {
  using output_type = detail::sqrt_result_t<Input>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::sqrt(in, out);
  });
}

/// Returns a newly allocated array containing the reciprocal of each real floating-point input element.
template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto inverse(Input&& input) {
  using output_type = std::remove_cv_t<detail::array_value_t<Input>>;
  static_assert(std::is_floating_point_v<output_type>, "array inverse requires float or double input");
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::inverse(in, out);
  });
}

/// Returns a newly allocated array containing 1 / sqrt(input) for each real floating-point input element.
template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto rsqrt(Input&& input) {
  using output_type = std::remove_cv_t<detail::array_value_t<Input>>;
  static_assert(std::is_floating_point_v<output_type>, "array rsqrt requires float or double input");
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::rsqrt(in, out);
  });
}

/// Compatibility synonym for `rsqrt`; returns a newly allocated result.
template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto inverse_sqrt(Input&& input) {
  return ksj::array::rsqrt(std::forward<Input>(input));
}

template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto exp(Input&& input) {
  using output_type = detail::exp_result_t<Input>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::exp(in, out);
  });
}

template <typename Input>
  requires(detail::array_input_v<Input>)
[[nodiscard]] auto log(Input&& input) {
  using output_type = detail::log_result_t<Input>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::log(in, out);
  });
}

template <typename Input, typename Scale, typename Addend>
  requires(detail::array_input_v<Input> && detail::array_input_v<Addend>)
[[nodiscard]] auto scale_add(Input&& input, const Scale& scale, Addend&& addend) {
  using output_type =
    std::remove_cvref_t<decltype(std::declval<const detail::array_value_t<Input>&>() * std::declval<const Scale&>() +
                                 std::declval<const detail::array_value_t<Addend>&>())>;
  return detail::make_binary_result<output_type>(std::forward<Input>(input), std::forward<Addend>(addend),
                                                 [&scale](auto input_view, auto addend_view, auto out) {
                                                   ksj::array::scale_add(input_view, scale, addend_view, out);
                                                 });
}

template <typename First, typename FirstScalar, typename Second, typename SecondScalar>
  requires(detail::array_input_v<First> && detail::array_input_v<Second>)
[[nodiscard]] auto linear_combination(First&& first, const FirstScalar& first_scalar, Second&& second,
                                      const SecondScalar& second_scalar) {
  using output_type = std::remove_cvref_t<
    decltype(std::declval<const FirstScalar&>() * std::declval<const detail::array_value_t<First>&>() +
             std::declval<const SecondScalar&>() * std::declval<const detail::array_value_t<Second>&>())>;
  return detail::make_binary_result<output_type>(
    std::forward<First>(first), std::forward<Second>(second),
    [&first_scalar, &second_scalar](auto first_view, auto second_view, auto out) {
      ksj::array::linear_combination(first_view, first_scalar, second_view, second_scalar, out);
    });
}

template <typename First, typename FirstScalar, typename Second, typename SecondScalar, typename Third,
          typename ThirdScalar>
  requires(detail::array_input_v<First> && detail::array_input_v<Second> && detail::array_input_v<Third>)
[[nodiscard]] auto linear_combination(First&& first, const FirstScalar& first_scalar, Second&& second,
                                      const SecondScalar& second_scalar, Third&& third,
                                      const ThirdScalar& third_scalar) {
  using output_type = std::remove_cvref_t<
    decltype(std::declval<const FirstScalar&>() * std::declval<const detail::array_value_t<First>&>() +
             std::declval<const SecondScalar&>() * std::declval<const detail::array_value_t<Second>&>() +
             std::declval<const ThirdScalar&>() * std::declval<const detail::array_value_t<Third>&>())>;
  return detail::make_ternary_result<output_type>(
    std::forward<First>(first), std::forward<Second>(second), std::forward<Third>(third),
    [&first_scalar, &second_scalar, &third_scalar](auto first_view, auto second_view, auto third_view, auto out) {
      ksj::array::linear_combination(first_view, first_scalar, second_view, second_scalar, third_view, third_scalar,
                                     out);
    });
}

template <typename First, typename FirstScalar, typename Second, typename SecondScalar, typename Third,
          typename ThirdScalar, typename Fourth, typename FourthScalar>
  requires(detail::array_input_v<First> && detail::array_input_v<Second> && detail::array_input_v<Third> &&
           detail::array_input_v<Fourth>)
[[nodiscard]] auto linear_combination(First&& first, const FirstScalar& first_scalar, Second&& second,
                                      const SecondScalar& second_scalar, Third&& third, const ThirdScalar& third_scalar,
                                      Fourth&& fourth, const FourthScalar& fourth_scalar) {
  using output_type = std::remove_cvref_t<
    decltype(std::declval<const FirstScalar&>() * std::declval<const detail::array_value_t<First>&>() +
             std::declval<const SecondScalar&>() * std::declval<const detail::array_value_t<Second>&>() +
             std::declval<const ThirdScalar&>() * std::declval<const detail::array_value_t<Third>&>() +
             std::declval<const FourthScalar&>() * std::declval<const detail::array_value_t<Fourth>&>())>;
  auto first_view = detail::backend_view(std::forward<First>(first));
  auto second_view = detail::backend_view(std::forward<Second>(second));
  auto third_view = detail::backend_view(std::forward<Third>(third));
  auto fourth_view = detail::backend_view(std::forward<Fourth>(fourth));
  auto output = detail::make_pooled_like<output_type>(first_view);
  ksj::array::linear_combination(first_view, first_scalar, second_view, second_scalar, third_view, third_scalar,
                                 fourth_view, fourth_scalar, output.view());
  return output;
}

template <typename First, typename FirstScalar, typename Second, typename SecondScalar, typename Third,
          typename ThirdScalar, typename Fourth, typename FourthScalar, typename Fifth, typename FifthScalar>
  requires(detail::array_input_v<First> && detail::array_input_v<Second> && detail::array_input_v<Third> &&
           detail::array_input_v<Fourth> && detail::array_input_v<Fifth>)
[[nodiscard]] auto linear_combination(First&& first, const FirstScalar& first_scalar, Second&& second,
                                      const SecondScalar& second_scalar, Third&& third, const ThirdScalar& third_scalar,
                                      Fourth&& fourth, const FourthScalar& fourth_scalar, Fifth&& fifth,
                                      const FifthScalar& fifth_scalar) {
  using output_type = std::remove_cvref_t<
    decltype(std::declval<const FirstScalar&>() * std::declval<const detail::array_value_t<First>&>() +
             std::declval<const SecondScalar&>() * std::declval<const detail::array_value_t<Second>&>() +
             std::declval<const ThirdScalar&>() * std::declval<const detail::array_value_t<Third>&>() +
             std::declval<const FourthScalar&>() * std::declval<const detail::array_value_t<Fourth>&>() +
             std::declval<const FifthScalar&>() * std::declval<const detail::array_value_t<Fifth>&>())>;
  auto first_view = detail::backend_view(std::forward<First>(first));
  auto second_view = detail::backend_view(std::forward<Second>(second));
  auto third_view = detail::backend_view(std::forward<Third>(third));
  auto fourth_view = detail::backend_view(std::forward<Fourth>(fourth));
  auto fifth_view = detail::backend_view(std::forward<Fifth>(fifth));
  auto output = detail::make_pooled_like<output_type>(first_view);
  ksj::array::linear_combination(first_view, first_scalar, second_view, second_scalar, third_view, third_scalar,
                                 fourth_view, fourth_scalar, fifth_view, fifth_scalar, output.view());
  return output;
}

template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs>)
[[nodiscard]] auto minimum(Lhs&& lhs, Rhs&& rhs) {
  using output_type = std::remove_cvref_t<decltype(std::min(std::declval<const detail::array_value_t<Lhs>&>(),
                                                            std::declval<const detail::array_value_t<Rhs>&>()))>;
  return detail::make_binary_result<output_type>(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs),
                                                 [](auto lhs_view, auto rhs_view, auto out) {
                                                   ksj::array::minimum(lhs_view, rhs_view, out);
                                                 });
}

template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs>)
[[nodiscard]] auto maximum(Lhs&& lhs, Rhs&& rhs) {
  using output_type = std::remove_cvref_t<decltype(std::max(std::declval<const detail::array_value_t<Lhs>&>(),
                                                            std::declval<const detail::array_value_t<Rhs>&>()))>;
  return detail::make_binary_result<output_type>(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs),
                                                 [](auto lhs_view, auto rhs_view, auto out) {
                                                   ksj::array::maximum(lhs_view, rhs_view, out);
                                                 });
}

/// Returns a newly allocated array containing sqrt(lhs * lhs + rhs * rhs) for each element pair.
template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs>)
[[nodiscard]] auto hypot(Lhs&& lhs, Rhs&& rhs) {
  using output_type = std::remove_cv_t<detail::array_value_t<Lhs>>;
  static_assert(std::is_same_v<output_type, std::remove_cv_t<detail::array_value_t<Rhs>>> &&
                  std::is_floating_point_v<output_type>,
                "array hypot requires matching float or double inputs");
  return detail::make_binary_result<output_type>(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs),
                                                 [](auto lhs_view, auto rhs_view, auto out) {
                                                   ksj::array::hypot(lhs_view, rhs_view, out);
                                                 });
}

template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs>)
[[nodiscard]] auto cwise_min(Lhs&& lhs, Rhs&& rhs) {
  return ksj::array::minimum(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs));
}

template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs>)
[[nodiscard]] auto cwise_max(Lhs&& lhs, Rhs&& rhs) {
  return ksj::array::maximum(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs));
}

template <typename Lhs, typename Rhs>
  requires(detail::array_input_v<Lhs> && detail::array_input_v<Rhs> &&
           detail::is_vector_view_v<detail::backend_view_t<Lhs>> &&
           detail::is_vector_view_v<detail::backend_view_t<Rhs>> &&
           detail::same_unsigned_integral_scalars_v<detail::array_value_t<Lhs>, detail::array_value_t<Rhs>,
                                                    detail::array_value_t<Lhs>>)
[[nodiscard]] auto bitwise_and(Lhs&& lhs, Rhs&& rhs) {
  using output_type = std::remove_cv_t<detail::array_value_t<Lhs>>;
  return detail::make_binary_result<output_type>(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs),
                                                 [](auto lhs_view, auto rhs_view, auto out) {
                                                   ksj::array::bitwise_and(lhs_view, rhs_view, out);
                                                 });
}

template <typename Input>
  requires(detail::array_input_v<Input> && detail::is_vector_view_v<detail::backend_view_t<Input>> &&
           detail::unsigned_integral_scalar_v<detail::array_value_t<Input>>)
[[nodiscard]] auto bitwise_not(Input&& input) {
  using output_type = std::remove_cv_t<detail::array_value_t<Input>>;
  return detail::make_unary_result<output_type>(std::forward<Input>(input), [](auto in, auto out) {
    ksj::array::bitwise_not(in, out);
  });
}

template <typename Target, typename Source, typename Scalar>
void lerp_in_place(Target&& target, Source&& source, const Scalar& amount) {
  ksj::array::transform(target, std::forward<Source>(source), target,
                        [&amount](const auto& current, const auto& destination) {
                          return current + amount * (destination - current);
                        });
}

} // namespace ksj::array
