#pragma once

/// Complex-array operations including magnitude, phase, conjugation, phasors, and fused complex products.

#include "kspacejet/array/detail/array_policy.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_complex.hpp"
#include "kspacejet/array/detail/intel/intel_array_complex.hpp"
#include "kspacejet/array/detail/intel/intel_array_vml.hpp"
#include "kspacejet/array/elementwise.hpp"
#include "kspacejet/array/pooled_cube.hpp"
#include "kspacejet/array/scalar_traits.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace ksj::array {

namespace detail {

template <typename T> [[nodiscard]] real_scalar_t<T> squared_magnitude_scalar(const std::complex<T>& value) {
  return std::norm(value);
}

template <typename T> [[nodiscard]] T squared_magnitude_scalar(const T& value) {
  return value * value;
}

template <typename T>
[[nodiscard]] std::remove_cv_t<T> soft_threshold_scalar(const T& value, const real_scalar_t<T> lambda,
                                                        const real_scalar_t<T> epsilon)
  requires(is_complex_v<T>)
{
  using output_type = std::remove_cv_t<T>;
  const auto magnitude = std::abs(value);
  if (magnitude < epsilon || magnitude == real_scalar_t<T>{}) {
    return output_type{};
  }
  return static_cast<output_type>(value / magnitude * std::max(magnitude - lambda, real_scalar_t<T>{}));
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool dispatch_complex_real(InputView input, OutputView output) {
  const auto const_input = ksj::array::as_const_view(input);
  if (prefer_intel_complex_component(ComplexComponentOperation::real, input, output) &&
      intel::complex_real(const_input, output)) {
    return true;
  }
  if (prefer_eigen_complex_component(input, output) && eigen::complex_real(const_input, output)) {
    return true;
  }
  return false;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool dispatch_complex_imag(InputView input, OutputView output) {
  const auto const_input = ksj::array::as_const_view(input);
  if (prefer_intel_complex_component(ComplexComponentOperation::imag, input, output) &&
      intel::complex_imag(const_input, output)) {
    return true;
  }
  if (prefer_eigen_complex_component(input, output) && eigen::complex_imag(const_input, output)) {
    return true;
  }
  return false;
}

template <typename InputView, typename RealOutputView, typename ImagOutputView>
[[nodiscard]] bool dispatch_split_complex(InputView input, RealOutputView real_output, ImagOutputView imag_output) {
  const auto const_input = ksj::array::as_const_view(input);
  if (prefer_intel_complex_split(input, real_output, imag_output) &&
      intel::split_complex(const_input, real_output, imag_output)) {
    return true;
  }
  if (prefer_eigen_complex_split(input, real_output, imag_output) &&
      eigen::split_complex(const_input, real_output, imag_output)) {
    return true;
  }
  return false;
}

template <typename RealInputView, typename ImagInputView, typename OutputView>
[[nodiscard]] bool dispatch_complex_from_real_imag(RealInputView real_input, ImagInputView imag_input,
                                                   OutputView output) {
  const auto const_real = ksj::array::as_const_view(real_input);
  const auto const_imag = ksj::array::as_const_view(imag_input);
  if (prefer_intel_complex_from_real_imag(real_input, imag_input, output) &&
      intel::complex_from_real_imag(const_real, const_imag, output)) {
    return true;
  }
  if (prefer_eigen_complex_from_real_imag(real_input, imag_input, output) &&
      eigen::complex_from_real_imag(const_real, const_imag, output)) {
    return true;
  }
  return false;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool dispatch_complex_conjugate(InputView input, OutputView output) {
  const auto const_input = ksj::array::as_const_view(input);
  if (prefer_mkl_vml_complex_conjugate(input, output) && intel::vml::complex_conjugate(const_input, output)) {
    return true;
  }
  if (prefer_intel_complex_conjugate(input, output) && intel::complex_conjugate(const_input, output)) {
    return true;
  }
  if (prefer_eigen_complex_conjugate(input, output) && eigen::complex_conjugate(const_input, output)) {
    return true;
  }
  return false;
}

template <typename InputView, typename MagnitudeOutputView, typename PhaseOutputView>
[[nodiscard]] bool dispatch_rectangular_to_polar(InputView input, MagnitudeOutputView magnitude_output,
                                                 PhaseOutputView phase_output) {
  const auto const_input = ksj::array::as_const_view(input);
  if (prefer_intel_rectangular_to_polar(input, magnitude_output, phase_output) &&
      intel::rectangular_to_polar(const_input, magnitude_output, phase_output)) {
    return true;
  }
  if (prefer_eigen_rectangular_to_polar(input, magnitude_output, phase_output) &&
      eigen::rectangular_to_polar(const_input, magnitude_output, phase_output)) {
    return true;
  }
  return false;
}

template <typename MagnitudeInputView, typename PhaseInputView, typename OutputView>
[[nodiscard]] bool dispatch_polar_to_rectangular(MagnitudeInputView magnitude_input, PhaseInputView phase_input,
                                                 OutputView output) {
  const auto const_magnitude = ksj::array::as_const_view(magnitude_input);
  const auto const_phase = ksj::array::as_const_view(phase_input);
  if (prefer_intel_polar_to_rectangular(magnitude_input, phase_input, output) &&
      intel::polar_to_rectangular(const_magnitude, const_phase, output)) {
    return true;
  }
  if (prefer_eigen_polar_to_rectangular(magnitude_input, phase_input, output) &&
      eigen::polar_to_rectangular(const_magnitude, const_phase, output)) {
    return true;
  }
  return false;
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool dispatch_multiply_conjugate(LhsView lhs, RhsView rhs, OutputView output) {
  const auto const_lhs = ksj::array::as_const_view(lhs);
  const auto const_rhs = ksj::array::as_const_view(rhs);
  if (prefer_intel_multiply_conjugate(lhs, rhs, output) && intel::multiply_conjugate(const_lhs, const_rhs, output)) {
    return true;
  }
  if (prefer_eigen_multiply_conjugate(lhs, rhs, output) && eigen::multiply_conjugate(const_lhs, const_rhs, output)) {
    return true;
  }
  return false;
}

template <typename InputView, typename RealOutputView, typename ImagOutputView>
void split_complex_fallback(InputView input, RealOutputView real_output, ImagOutputView imag_output) {
  if (all_views_are_contiguous(input, real_output, imag_output)) {
    const auto* input_data = input.data();
    auto* real_data = real_output.data();
    auto* imag_data = imag_output.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      real_data[index] = input_data[index].real();
      imag_data[index] = input_data[index].imag();
    }
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    const auto& value = input[index];
    real_output[index] = value.real();
    imag_output[index] = value.imag();
  }
}

template <typename InputView, typename OutputView> void complex_real_fallback(InputView input, OutputView output) {
  if (all_views_are_contiguous(input, output)) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output_data[index] = input_data[index].real();
    }
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    output[index] = input[index].real();
  }
}

template <typename InputView, typename OutputView> void complex_imag_fallback(InputView input, OutputView output) {
  if (all_views_are_contiguous(input, output)) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output_data[index] = input_data[index].imag();
    }
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    output[index] = input[index].imag();
  }
}

template <typename RealInputView, typename ImagInputView, typename OutputView>
void complex_from_real_imag_fallback(RealInputView real_input, ImagInputView imag_input, OutputView output) {
  using output_type = typename OutputView::value_type;
  if (all_views_are_contiguous(real_input, imag_input, output)) {
    const auto* real_data = real_input.data();
    const auto* imag_data = imag_input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < output.size(); ++index) {
      output_data[index] = output_type{real_data[index], imag_data[index]};
    }
    return;
  }
  for (std::size_t index = 0U; index < output.size(); ++index) {
    output[index] = output_type{real_input[index], imag_input[index]};
  }
}

template <typename InputView, typename OutputView> void complex_conjugate_fallback(InputView input, OutputView output) {
  if (all_views_are_contiguous(input, output)) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output_data[index] = std::conj(input_data[index]);
    }
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    output[index] = std::conj(input[index]);
  }
}

template <typename MagnitudeInputView, typename PhaseInputView, typename OutputView>
void polar_to_rectangular_fallback(MagnitudeInputView magnitude_input, PhaseInputView phase_input, OutputView output) {
  for (std::size_t index = 0U; index < output.size(); ++index) {
    output[index] = std::polar(magnitude_input[index], phase_input[index]);
  }
}

template <typename LhsView, typename RhsView, typename OutputView>
void multiply_conjugate_fallback(LhsView lhs, RhsView rhs, OutputView output) {
  if (all_views_are_contiguous(lhs, rhs, output)) {
    const auto* lhs_data = lhs.data();
    const auto* rhs_data = rhs.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < output.size(); ++index) {
      output_data[index] = lhs_data[index] * std::conj(rhs_data[index]);
    }
    return;
  }
  for (std::size_t index = 0U; index < output.size(); ++index) {
    output[index] = lhs[index] * std::conj(rhs[index]);
  }
}

} // namespace detail

template <typename Storage> struct SplitComplexResult {
  Storage real;
  Storage imag;
};

template <typename Storage> struct PolarComponents {
  Storage magnitude;
  Storage phase;
};

template <typename T>
void real(VectorView<T> input, VectorView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_real(input, output)) {
    return;
  }
  detail::complex_real_fallback(input, output);
}

template <typename T>
void real(MatrixView<T> input, MatrixView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_real(input, output)) {
    return;
  }
  detail::complex_real_fallback(input, output);
}

template <typename T>
void real(ImageView<T> input, ImageView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_real(input, output)) {
    return;
  }
  detail::complex_real_fallback(input, output);
}

template <typename T>
void real(CubeView<T> input, CubeView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_real(input, output)) {
    return;
  }
  detail::complex_real_fallback(input, output);
}

template <typename T>
void real(Array4DView<T> input, Array4DView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_real(input, output)) {
    return;
  }
  detail::complex_real_fallback(input, output);
}

template <typename T>
void imag(VectorView<T> input, VectorView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_imag(input, output)) {
    return;
  }
  detail::complex_imag_fallback(input, output);
}

template <typename T>
void imag(MatrixView<T> input, MatrixView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_imag(input, output)) {
    return;
  }
  detail::complex_imag_fallback(input, output);
}

template <typename T>
void imag(ImageView<T> input, ImageView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_imag(input, output)) {
    return;
  }
  detail::complex_imag_fallback(input, output);
}

template <typename T>
void imag(CubeView<T> input, CubeView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_imag(input, output)) {
    return;
  }
  detail::complex_imag_fallback(input, output);
}

template <typename T>
void imag(Array4DView<T> input, Array4DView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_imag(input, output)) {
    return;
  }
  detail::complex_imag_fallback(input, output);
}

template <typename T>
void split_complex(VectorView<T> input, VectorView<real_scalar_t<T>> real_output,
                   VectorView<real_scalar_t<T>> imag_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_size(input.size(), real_output.size(), "vector view split_complex real output size mismatch");
  detail::validate_same_size(input.size(), imag_output.size(), "vector view split_complex imag output size mismatch");
  if (detail::dispatch_split_complex(input, real_output, imag_output)) {
    return;
  }
  detail::split_complex_fallback(input, real_output, imag_output);
}

template <typename T>
void split_complex(MatrixView<T> input, MatrixView<real_scalar_t<T>> real_output,
                   MatrixView<real_scalar_t<T>> imag_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_shape(input, real_output, "matrix view split_complex real output shape mismatch");
  detail::validate_same_shape(input, imag_output, "matrix view split_complex imag output shape mismatch");
  if (detail::dispatch_split_complex(input, real_output, imag_output)) {
    return;
  }
  detail::split_complex_fallback(input, real_output, imag_output);
}

template <typename T>
void split_complex(ImageView<T> input, ImageView<real_scalar_t<T>> real_output, ImageView<real_scalar_t<T>> imag_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_shape(input, real_output, "image view split_complex real output shape mismatch");
  detail::validate_same_shape(input, imag_output, "image view split_complex imag output shape mismatch");
  if (detail::dispatch_split_complex(input, real_output, imag_output)) {
    return;
  }
  detail::split_complex_fallback(input, real_output, imag_output);
}

template <typename T>
void split_complex(CubeView<T> input, CubeView<real_scalar_t<T>> real_output, CubeView<real_scalar_t<T>> imag_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_cube_shape(input, real_output, "cube view split_complex real output shape mismatch");
  detail::validate_same_cube_shape(input, imag_output, "cube view split_complex imag output shape mismatch");
  if (detail::dispatch_split_complex(input, real_output, imag_output)) {
    return;
  }
  detail::split_complex_fallback(input, real_output, imag_output);
}

template <typename T>
void split_complex(Array4DView<T> input, Array4DView<real_scalar_t<T>> real_output,
                   Array4DView<real_scalar_t<T>> imag_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_array4d_shape(input, real_output, "array4d view split_complex real output shape mismatch");
  detail::validate_same_array4d_shape(input, imag_output, "array4d view split_complex imag output shape mismatch");
  if (detail::dispatch_split_complex(input, real_output, imag_output)) {
    return;
  }
  detail::split_complex_fallback(input, real_output, imag_output);
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledVector<real_scalar_t<T>>> split_complex(VectorView<T> input)
  requires(is_complex_v<T>)
{
  SplitComplexResult<PooledVector<real_scalar_t<T>>> output{
    make_pooled_vector<real_scalar_t<T>>(input.size()),
    make_pooled_vector<real_scalar_t<T>>(input.size()),
  };
  split_complex(input, output.real.view(), output.imag.view());
  return output;
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledMatrix<real_scalar_t<T>>> split_complex(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  SplitComplexResult<PooledMatrix<real_scalar_t<T>>> output{
    make_pooled_matrix<real_scalar_t<T>>(input.rows(), input.cols()),
    make_pooled_matrix<real_scalar_t<T>>(input.rows(), input.cols()),
  };
  split_complex(input, output.real.view(), output.imag.view());
  return output;
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledImage<real_scalar_t<T>>> split_complex(ImageView<T> input)
  requires(is_complex_v<T>)
{
  SplitComplexResult<PooledImage<real_scalar_t<T>>> output{
    make_pooled_image<real_scalar_t<T>>(input.rows(), input.cols()),
    make_pooled_image<real_scalar_t<T>>(input.rows(), input.cols()),
  };
  split_complex(input, output.real.view(), output.imag.view());
  return output;
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledCube<real_scalar_t<T>>> split_complex(CubeView<T> input)
  requires(is_complex_v<T>)
{
  SplitComplexResult<PooledCube<real_scalar_t<T>>> output{
    make_pooled_cube<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2()),
    make_pooled_cube<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2()),
  };
  split_complex(input, output.real.view(), output.imag.view());
  return output;
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledArray4D<real_scalar_t<T>>> split_complex(Array4DView<T> input)
  requires(is_complex_v<T>)
{
  SplitComplexResult<PooledArray4D<real_scalar_t<T>>> output{
    make_pooled_array4d<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3()),
    make_pooled_array4d<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3()),
  };
  split_complex(input, output.real.view(), output.imag.view());
  return output;
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledVector<T>> split_complex(const PooledVector<std::complex<T>>& input) {
  return split_complex(input.view());
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledMatrix<T>> split_complex(const PooledMatrix<std::complex<T>>& input) {
  return split_complex(input.view());
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledImage<T>> split_complex(const PooledImage<std::complex<T>>& input) {
  return split_complex(input.view());
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledCube<T>> split_complex(const PooledCube<std::complex<T>>& input) {
  return split_complex(input.view());
}

template <typename T>
[[nodiscard]] SplitComplexResult<PooledArray4D<T>> split_complex(const PooledArray4D<std::complex<T>>& input) {
  return split_complex(input.view());
}

template <typename T>
void complex_from_real_imag(VectorView<T> real_input, VectorView<T> imag_input,
                            VectorView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_size(real_input.size(), imag_input.size(),
                             "vector view complex_from_real_imag input size mismatch");
  detail::validate_same_size(real_input.size(), output.size(),
                             "vector view complex_from_real_imag output size mismatch");
  if (detail::dispatch_complex_from_real_imag(real_input, imag_input, output)) {
    return;
  }
  detail::complex_from_real_imag_fallback(real_input, imag_input, output);
}

template <typename T>
void complex_from_real_imag(MatrixView<T> real_input, MatrixView<T> imag_input,
                            MatrixView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_shape(real_input, imag_input, "matrix view complex_from_real_imag input shape mismatch");
  detail::validate_same_shape(real_input, output, "matrix view complex_from_real_imag output shape mismatch");
  if (detail::dispatch_complex_from_real_imag(real_input, imag_input, output)) {
    return;
  }
  detail::complex_from_real_imag_fallback(real_input, imag_input, output);
}

template <typename T>
void complex_from_real_imag(ImageView<T> real_input, ImageView<T> imag_input,
                            ImageView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_shape(real_input, imag_input, "image view complex_from_real_imag input shape mismatch");
  detail::validate_same_shape(real_input, output, "image view complex_from_real_imag output shape mismatch");
  if (detail::dispatch_complex_from_real_imag(real_input, imag_input, output)) {
    return;
  }
  detail::complex_from_real_imag_fallback(real_input, imag_input, output);
}

template <typename T>
void complex_from_real_imag(CubeView<T> real_input, CubeView<T> imag_input,
                            CubeView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_cube_shape(real_input, imag_input, "cube view complex_from_real_imag input shape mismatch");
  detail::validate_same_cube_shape(real_input, output, "cube view complex_from_real_imag output shape mismatch");
  if (detail::dispatch_complex_from_real_imag(real_input, imag_input, output)) {
    return;
  }
  detail::complex_from_real_imag_fallback(real_input, imag_input, output);
}

template <typename T>
void complex_from_real_imag(Array4DView<T> real_input, Array4DView<T> imag_input,
                            Array4DView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_array4d_shape(real_input, imag_input,
                                      "array4d view complex_from_real_imag input shape mismatch");
  detail::validate_same_array4d_shape(real_input, output, "array4d view complex_from_real_imag output shape mismatch");
  if (detail::dispatch_complex_from_real_imag(real_input, imag_input, output)) {
    return;
  }
  detail::complex_from_real_imag_fallback(real_input, imag_input, output);
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<std::remove_cv_t<T>>> complex_from_real_imag(VectorView<T> real_input,
                                                                                     VectorView<T> imag_input)
  requires(!is_complex_v<T>)
{
  auto output = make_pooled_vector<std::complex<std::remove_cv_t<T>>>(real_input.size());
  complex_from_real_imag(real_input, imag_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<std::remove_cv_t<T>>> complex_from_real_imag(MatrixView<T> real_input,
                                                                                     MatrixView<T> imag_input)
  requires(!is_complex_v<T>)
{
  auto output = make_pooled_matrix<std::complex<std::remove_cv_t<T>>>(real_input.rows(), real_input.cols());
  complex_from_real_imag(real_input, imag_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<std::complex<std::remove_cv_t<T>>> complex_from_real_imag(ImageView<T> real_input,
                                                                                    ImageView<T> imag_input)
  requires(!is_complex_v<T>)
{
  auto output = make_pooled_image<std::complex<std::remove_cv_t<T>>>(real_input.rows(), real_input.cols());
  complex_from_real_imag(real_input, imag_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<std::remove_cv_t<T>>> complex_from_real_imag(CubeView<T> real_input,
                                                                                   CubeView<T> imag_input)
  requires(!is_complex_v<T>)
{
  auto output =
    make_pooled_cube<std::complex<std::remove_cv_t<T>>>(real_input.dim0(), real_input.dim1(), real_input.dim2());
  complex_from_real_imag(real_input, imag_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<std::complex<std::remove_cv_t<T>>> complex_from_real_imag(Array4DView<T> real_input,
                                                                                      Array4DView<T> imag_input)
  requires(!is_complex_v<T>)
{
  auto output = make_pooled_array4d<std::complex<std::remove_cv_t<T>>>(real_input.dim0(), real_input.dim1(),
                                                                       real_input.dim2(), real_input.dim3());
  complex_from_real_imag(real_input, imag_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<T>> complex_from_real_imag(const PooledVector<T>& real_input,
                                                                   const PooledVector<T>& imag_input)
  requires(!is_complex_v<T>)
{
  return complex_from_real_imag(real_input.view(), imag_input.view());
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<T>> complex_from_real_imag(const PooledMatrix<T>& real_input,
                                                                   const PooledMatrix<T>& imag_input)
  requires(!is_complex_v<T>)
{
  return complex_from_real_imag(real_input.view(), imag_input.view());
}

template <typename T>
[[nodiscard]] PooledImage<std::complex<T>> complex_from_real_imag(const PooledImage<T>& real_input,
                                                                  const PooledImage<T>& imag_input)
  requires(!is_complex_v<T>)
{
  return complex_from_real_imag(real_input.view(), imag_input.view());
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<T>> complex_from_real_imag(const PooledCube<T>& real_input,
                                                                 const PooledCube<T>& imag_input)
  requires(!is_complex_v<T>)
{
  return complex_from_real_imag(real_input.view(), imag_input.view());
}

template <typename T>
[[nodiscard]] PooledArray4D<std::complex<T>> complex_from_real_imag(const PooledArray4D<T>& real_input,
                                                                    const PooledArray4D<T>& imag_input)
  requires(!is_complex_v<T>)
{
  return complex_from_real_imag(real_input.view(), imag_input.view());
}

template <typename T>
void complex_from_real(VectorView<T> input, VectorView<std::complex<std::remove_cv_t<T>>> output)
  requires(!is_complex_v<T>)
{
  using real_type = std::remove_cv_t<T>;
  transform(input, output, [](const auto& value) {
    return std::complex<real_type>{static_cast<real_type>(value), real_type{}};
  });
}

template <typename T>
void complex_from_real(MatrixView<T> input, MatrixView<std::complex<std::remove_cv_t<T>>> output)
  requires(!is_complex_v<T>)
{
  using real_type = std::remove_cv_t<T>;
  transform(input, output, [](const auto& value) {
    return std::complex<real_type>{static_cast<real_type>(value), real_type{}};
  });
}

template <typename T>
void complex_from_real(ImageView<T> input, ImageView<std::complex<std::remove_cv_t<T>>> output)
  requires(!is_complex_v<T>)
{
  using real_type = std::remove_cv_t<T>;
  transform(input, output, [](const auto& value) {
    return std::complex<real_type>{static_cast<real_type>(value), real_type{}};
  });
}

template <typename T>
void complex_from_real(CubeView<T> input, CubeView<std::complex<std::remove_cv_t<T>>> output)
  requires(!is_complex_v<T>)
{
  using real_type = std::remove_cv_t<T>;
  transform(input, output, [](const auto& value) {
    return std::complex<real_type>{static_cast<real_type>(value), real_type{}};
  });
}

template <typename T>
void complex_from_real(Array4DView<T> input, Array4DView<std::complex<std::remove_cv_t<T>>> output)
  requires(!is_complex_v<T>)
{
  using real_type = std::remove_cv_t<T>;
  transform(input, output, [](const auto& value) {
    return std::complex<real_type>{static_cast<real_type>(value), real_type{}};
  });
}

template <typename T>
void magnitude(VectorView<T> input, VectorView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_magnitude(input, output) &&
      detail::intel::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_magnitude(input, output) &&
      detail::eigen::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::abs(value);
  });
}

template <typename T>
void magnitude(MatrixView<T> input, MatrixView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_magnitude(input, output) &&
      detail::intel::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_magnitude(input, output) &&
      detail::eigen::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::abs(value);
  });
}

template <typename T>
void magnitude(ImageView<T> input, ImageView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_magnitude(input, output) &&
      detail::intel::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_magnitude(input, output) &&
      detail::eigen::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::abs(value);
  });
}

template <typename T>
void magnitude(CubeView<T> input, CubeView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_magnitude(input, output) &&
      detail::intel::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_magnitude(input, output) &&
      detail::eigen::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::abs(value);
  });
}

template <typename T>
void magnitude(Array4DView<T> input, Array4DView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_magnitude(input, output) &&
      detail::intel::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_magnitude(input, output) &&
      detail::eigen::complex_magnitude(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::abs(value);
  });
}

template <typename T>
void squared_magnitude(VectorView<T> input, VectorView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  transform(input, output, [](const auto& value) {
    return std::norm(value);
  });
}

template <typename T>
void squared_magnitude(MatrixView<T> input, MatrixView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  transform(input, output, [](const auto& value) {
    return std::norm(value);
  });
}

template <typename T>
void squared_magnitude(ImageView<T> input, ImageView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  transform(input, output, [](const auto& value) {
    return std::norm(value);
  });
}

template <typename T>
void squared_magnitude(CubeView<T> input, CubeView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  transform(input, output, [](const auto& value) {
    return std::norm(value);
  });
}

template <typename T>
void squared_magnitude(Array4DView<T> input, Array4DView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  transform(input, output, [](const auto& value) {
    return std::norm(value);
  });
}

template <typename T>
void phase(VectorView<T> input, VectorView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_phase(input, output) && detail::intel::complex_phase(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_phase(input, output) && detail::eigen::complex_phase(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::arg(value);
  });
}

template <typename T>
void phase(MatrixView<T> input, MatrixView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_phase(input, output) && detail::intel::complex_phase(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_phase(input, output) && detail::eigen::complex_phase(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::arg(value);
  });
}

template <typename T>
void phase(ImageView<T> input, ImageView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_phase(input, output) && detail::intel::complex_phase(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_phase(input, output) && detail::eigen::complex_phase(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::arg(value);
  });
}

template <typename T>
void phase(CubeView<T> input, CubeView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_phase(input, output) && detail::intel::complex_phase(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_phase(input, output) && detail::eigen::complex_phase(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::arg(value);
  });
}

template <typename T>
void phase(Array4DView<T> input, Array4DView<real_scalar_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_intel_complex_phase(input, output) && detail::intel::complex_phase(as_const_view(input), output)) {
    return;
  }

  if (detail::prefer_eigen_complex_phase(input, output) && detail::eigen::complex_phase(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::arg(value);
  });
}

template <typename T>
void conjugate(VectorView<T> input, VectorView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_conjugate(input, output)) {
    return;
  }
  detail::complex_conjugate_fallback(input, output);
}

template <typename T>
void conjugate(MatrixView<T> input, MatrixView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_conjugate(input, output)) {
    return;
  }
  detail::complex_conjugate_fallback(input, output);
}

template <typename T>
void conjugate(ImageView<T> input, ImageView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_conjugate(input, output)) {
    return;
  }
  detail::complex_conjugate_fallback(input, output);
}

template <typename T>
void conjugate(CubeView<T> input, CubeView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_conjugate(input, output)) {
    return;
  }
  detail::complex_conjugate_fallback(input, output);
}

template <typename T>
void conjugate(Array4DView<T> input, Array4DView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::dispatch_complex_conjugate(input, output)) {
    return;
  }
  detail::complex_conjugate_fallback(input, output);
}

template <typename T>
void soft_threshold(VectorView<T> input, VectorView<std::remove_cv_t<T>> output, const real_scalar_t<T> lambda,
                    const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  transform(input, output, [lambda, epsilon](const auto& value) {
    return detail::soft_threshold_scalar(value, lambda, epsilon);
  });
}

template <typename T>
void soft_threshold(MatrixView<T> input, MatrixView<std::remove_cv_t<T>> output, const real_scalar_t<T> lambda,
                    const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  transform(input, output, [lambda, epsilon](const auto& value) {
    return detail::soft_threshold_scalar(value, lambda, epsilon);
  });
}

template <typename T>
void soft_threshold(ImageView<T> input, ImageView<std::remove_cv_t<T>> output, const real_scalar_t<T> lambda,
                    const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  transform(input, output, [lambda, epsilon](const auto& value) {
    return detail::soft_threshold_scalar(value, lambda, epsilon);
  });
}

template <typename T>
void soft_threshold(CubeView<T> input, CubeView<std::remove_cv_t<T>> output, const real_scalar_t<T> lambda,
                    const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  transform(input, output, [lambda, epsilon](const auto& value) {
    return detail::soft_threshold_scalar(value, lambda, epsilon);
  });
}

template <typename T>
void soft_threshold(Array4DView<T> input, Array4DView<std::remove_cv_t<T>> output, const real_scalar_t<T> lambda,
                    const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  transform(input, output, [lambda, epsilon](const auto& value) {
    return detail::soft_threshold_scalar(value, lambda, epsilon);
  });
}

template <typename T>
[[nodiscard]] PooledVector<real_scalar_t<T>> real(VectorView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<real_scalar_t<T>>(input.size());
  real(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<real_scalar_t<T>> real(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<real_scalar_t<T>>(input.rows(), input.cols());
  real(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<real_scalar_t<T>> real(ImageView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<real_scalar_t<T>>(input.rows(), input.cols());
  real(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<real_scalar_t<T>> real(CubeView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_cube<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2());
  real(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<real_scalar_t<T>> real(Array4DView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_array4d<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  real(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledVector<T> real(const PooledVector<std::complex<T>>& input) {
  return real(input.view());
}

template <typename T> [[nodiscard]] PooledMatrix<T> real(const PooledMatrix<std::complex<T>>& input) {
  return real(input.view());
}

template <typename T> [[nodiscard]] PooledImage<T> real(const PooledImage<std::complex<T>>& input) {
  return real(input.view());
}

template <typename T> [[nodiscard]] PooledCube<T> real(const PooledCube<std::complex<T>>& input) {
  return real(input.view());
}

template <typename T> [[nodiscard]] PooledArray4D<T> real(const PooledArray4D<std::complex<T>>& input) {
  return real(input.view());
}

template <typename T>
[[nodiscard]] PooledVector<real_scalar_t<T>> imag(VectorView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<real_scalar_t<T>>(input.size());
  imag(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<real_scalar_t<T>> imag(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<real_scalar_t<T>>(input.rows(), input.cols());
  imag(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<real_scalar_t<T>> imag(ImageView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<real_scalar_t<T>>(input.rows(), input.cols());
  imag(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<real_scalar_t<T>> imag(CubeView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_cube<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2());
  imag(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<real_scalar_t<T>> imag(Array4DView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_array4d<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  imag(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledVector<T> imag(const PooledVector<std::complex<T>>& input) {
  return imag(input.view());
}

template <typename T> [[nodiscard]] PooledMatrix<T> imag(const PooledMatrix<std::complex<T>>& input) {
  return imag(input.view());
}

template <typename T> [[nodiscard]] PooledImage<T> imag(const PooledImage<std::complex<T>>& input) {
  return imag(input.view());
}

template <typename T> [[nodiscard]] PooledCube<T> imag(const PooledCube<std::complex<T>>& input) {
  return imag(input.view());
}

template <typename T> [[nodiscard]] PooledArray4D<T> imag(const PooledArray4D<std::complex<T>>& input) {
  return imag(input.view());
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<std::remove_cv_t<T>>> complex_from_real(VectorView<T> input)
  requires(!is_complex_v<T>)
{
  using output_type = std::complex<std::remove_cv_t<T>>;
  auto output = make_pooled_vector<output_type>(input.size());
  complex_from_real(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<std::remove_cv_t<T>>> complex_from_real(MatrixView<T> input)
  requires(!is_complex_v<T>)
{
  using output_type = std::complex<std::remove_cv_t<T>>;
  auto output = make_pooled_matrix<output_type>(input.rows(), input.cols());
  complex_from_real(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<std::complex<std::remove_cv_t<T>>> complex_from_real(ImageView<T> input)
  requires(!is_complex_v<T>)
{
  using output_type = std::complex<std::remove_cv_t<T>>;
  auto output = make_pooled_image<output_type>(input.rows(), input.cols());
  complex_from_real(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<std::remove_cv_t<T>>> complex_from_real(CubeView<T> input)
  requires(!is_complex_v<T>)
{
  using output_type = std::complex<std::remove_cv_t<T>>;
  auto output = make_pooled_cube<output_type>(input.dim0(), input.dim1(), input.dim2());
  complex_from_real(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<std::complex<std::remove_cv_t<T>>> complex_from_real(Array4DView<T> input)
  requires(!is_complex_v<T>)
{
  using output_type = std::complex<std::remove_cv_t<T>>;
  auto output = make_pooled_array4d<output_type>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  complex_from_real(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<T>> complex_from_real(const PooledVector<T>& input)
  requires(!is_complex_v<T>)
{
  return complex_from_real(input.view());
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<T>> complex_from_real(const PooledMatrix<T>& input)
  requires(!is_complex_v<T>)
{
  return complex_from_real(input.view());
}

template <typename T>
[[nodiscard]] PooledImage<std::complex<T>> complex_from_real(const PooledImage<T>& input)
  requires(!is_complex_v<T>)
{
  return complex_from_real(input.view());
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<T>> complex_from_real(const PooledCube<T>& input)
  requires(!is_complex_v<T>)
{
  return complex_from_real(input.view());
}

template <typename T>
[[nodiscard]] PooledArray4D<std::complex<T>> complex_from_real(const PooledArray4D<T>& input)
  requires(!is_complex_v<T>)
{
  return complex_from_real(input.view());
}

template <typename T>
[[nodiscard]] PooledVector<real_scalar_t<T>> magnitude(VectorView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<real_scalar_t<T>>(input.size());
  magnitude(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<real_scalar_t<T>> magnitude(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<real_scalar_t<T>>(input.rows(), input.cols());
  magnitude(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<real_scalar_t<T>> magnitude(ImageView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<real_scalar_t<T>>(input.rows(), input.cols());
  magnitude(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<real_scalar_t<T>> magnitude(CubeView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_cube<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2());
  magnitude(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<real_scalar_t<T>> magnitude(Array4DView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_array4d<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  magnitude(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] auto magnitude(const PooledVector<T>& input) {
  if constexpr (is_complex_v<T>) {
    return magnitude(input.view());
  }

  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    using std::abs;
    return abs(value);
  });
}

template <typename T> [[nodiscard]] auto magnitude(const PooledMatrix<T>& input) {
  if constexpr (is_complex_v<T>) {
    return magnitude(input.view());
  }

  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    using std::abs;
    return abs(value);
  });
}

template <typename T> [[nodiscard]] auto magnitude(const PooledImage<T>& input) {
  if constexpr (is_complex_v<T>) {
    return magnitude(input.view());
  }

  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    using std::abs;
    return abs(value);
  });
}

template <typename T> [[nodiscard]] auto magnitude(const PooledCube<T>& input) {
  if constexpr (is_complex_v<T>) {
    return magnitude(input.view());
  }

  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    using std::abs;
    return abs(value);
  });
}

template <typename T> [[nodiscard]] auto magnitude(const PooledArray4D<T>& input) {
  if constexpr (is_complex_v<T>) {
    return magnitude(input.view());
  }

  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    using std::abs;
    return abs(value);
  });
}

template <typename T>
[[nodiscard]] PooledVector<real_scalar_t<T>> squared_magnitude(VectorView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<real_scalar_t<T>>(input.size());
  squared_magnitude(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<real_scalar_t<T>> squared_magnitude(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<real_scalar_t<T>>(input.rows(), input.cols());
  squared_magnitude(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<real_scalar_t<T>> squared_magnitude(ImageView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<real_scalar_t<T>>(input.rows(), input.cols());
  squared_magnitude(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<real_scalar_t<T>> squared_magnitude(CubeView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_cube<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2());
  squared_magnitude(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<real_scalar_t<T>> squared_magnitude(Array4DView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_array4d<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  squared_magnitude(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] auto squared_magnitude(const PooledVector<T>& input) {
  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    return detail::squared_magnitude_scalar(value);
  });
}

template <typename T> [[nodiscard]] auto squared_magnitude(const PooledMatrix<T>& input) {
  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    return detail::squared_magnitude_scalar(value);
  });
}

template <typename T> [[nodiscard]] auto squared_magnitude(const PooledImage<T>& input) {
  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    return detail::squared_magnitude_scalar(value);
  });
}

template <typename T> [[nodiscard]] auto squared_magnitude(const PooledCube<T>& input) {
  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    return detail::squared_magnitude_scalar(value);
  });
}

template <typename T> [[nodiscard]] auto squared_magnitude(const PooledArray4D<T>& input) {
  using output_type = real_scalar_t<T>;
  return transform(input, [](const T& value) -> output_type {
    return detail::squared_magnitude_scalar(value);
  });
}

template <typename T>
[[nodiscard]] PooledVector<real_scalar_t<T>> phase(VectorView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<real_scalar_t<T>>(input.size());
  phase(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<real_scalar_t<T>> phase(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<real_scalar_t<T>>(input.rows(), input.cols());
  phase(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<real_scalar_t<T>> phase(ImageView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<real_scalar_t<T>>(input.rows(), input.cols());
  phase(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<real_scalar_t<T>> phase(CubeView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_cube<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2());
  phase(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<real_scalar_t<T>> phase(Array4DView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_array4d<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  phase(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledVector<T> phase(const PooledVector<std::complex<T>>& input) {
  return phase(input.view());
}

template <typename T> [[nodiscard]] PooledMatrix<T> phase(const PooledMatrix<std::complex<T>>& input) {
  return phase(input.view());
}

template <typename T> [[nodiscard]] PooledImage<T> phase(const PooledImage<std::complex<T>>& input) {
  return phase(input.view());
}

template <typename T> [[nodiscard]] PooledCube<T> phase(const PooledCube<std::complex<T>>& input) {
  return phase(input.view());
}

template <typename T> [[nodiscard]] PooledArray4D<T> phase(const PooledArray4D<std::complex<T>>& input) {
  return phase(input.view());
}

template <typename T>
[[nodiscard]] PooledVector<std::remove_cv_t<T>> conjugate(VectorView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<std::remove_cv_t<T>>(input.size());
  conjugate(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<std::remove_cv_t<T>> conjugate(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<std::remove_cv_t<T>>(input.rows(), input.cols());
  conjugate(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<std::remove_cv_t<T>> conjugate(ImageView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<std::remove_cv_t<T>>(input.rows(), input.cols());
  conjugate(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<std::remove_cv_t<T>> conjugate(CubeView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_cube<std::remove_cv_t<T>>(input.dim0(), input.dim1(), input.dim2());
  conjugate(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<std::remove_cv_t<T>> conjugate(Array4DView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_array4d<std::remove_cv_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  conjugate(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<T>> conjugate(const PooledVector<std::complex<T>>& input) {
  return conjugate(input.view());
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<T>> conjugate(const PooledMatrix<std::complex<T>>& input) {
  return conjugate(input.view());
}

template <typename T> [[nodiscard]] PooledImage<std::complex<T>> conjugate(const PooledImage<std::complex<T>>& input) {
  return conjugate(input.view());
}

template <typename T> [[nodiscard]] PooledCube<std::complex<T>> conjugate(const PooledCube<std::complex<T>>& input) {
  return conjugate(input.view());
}

template <typename T>
[[nodiscard]] PooledArray4D<std::complex<T>> conjugate(const PooledArray4D<std::complex<T>>& input) {
  return conjugate(input.view());
}

template <typename T>
[[nodiscard]] PooledVector<std::remove_cv_t<T>>
soft_threshold(VectorView<T> input, const real_scalar_t<T> lambda,
               const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<std::remove_cv_t<T>>(input.size());
  soft_threshold(input, output.view(), lambda, epsilon);
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<std::remove_cv_t<T>>
soft_threshold(MatrixView<T> input, const real_scalar_t<T> lambda,
               const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<std::remove_cv_t<T>>(input.rows(), input.cols());
  soft_threshold(input, output.view(), lambda, epsilon);
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<std::remove_cv_t<T>>
soft_threshold(ImageView<T> input, const real_scalar_t<T> lambda,
               const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<std::remove_cv_t<T>>(input.rows(), input.cols());
  soft_threshold(input, output.view(), lambda, epsilon);
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<std::remove_cv_t<T>>
soft_threshold(CubeView<T> input, const real_scalar_t<T> lambda,
               const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  auto output = make_pooled_cube<std::remove_cv_t<T>>(input.dim0(), input.dim1(), input.dim2());
  soft_threshold(input, output.view(), lambda, epsilon);
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<std::remove_cv_t<T>>
soft_threshold(Array4DView<T> input, const real_scalar_t<T> lambda,
               const real_scalar_t<T> epsilon = static_cast<real_scalar_t<T>>(1.0e-9))
  requires(is_complex_v<T>)
{
  auto output = make_pooled_array4d<std::remove_cv_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  soft_threshold(input, output.view(), lambda, epsilon);
  return output;
}

template <typename T>
void soft_threshold(const PooledVector<std::complex<T>>& input, PooledVector<std::complex<T>>& output, const T lambda,
                    const T epsilon = static_cast<T>(1.0e-9)) {
  soft_threshold(input.view(), output.view(), lambda, epsilon);
}

template <typename T>
void soft_threshold(const PooledMatrix<std::complex<T>>& input, PooledMatrix<std::complex<T>>& output, const T lambda,
                    const T epsilon = static_cast<T>(1.0e-9)) {
  soft_threshold(input.view(), output.view(), lambda, epsilon);
}

template <typename T>
void soft_threshold(const PooledImage<std::complex<T>>& input, PooledImage<std::complex<T>>& output, const T lambda,
                    const T epsilon = static_cast<T>(1.0e-9)) {
  soft_threshold(input.view(), output.view(), lambda, epsilon);
}

template <typename T>
void soft_threshold(const PooledCube<std::complex<T>>& input, PooledCube<std::complex<T>>& output, const T lambda,
                    const T epsilon = static_cast<T>(1.0e-9)) {
  soft_threshold(input.view(), output.view(), lambda, epsilon);
}

template <typename T>
void soft_threshold(const PooledArray4D<std::complex<T>>& input, PooledArray4D<std::complex<T>>& output, const T lambda,
                    const T epsilon = static_cast<T>(1.0e-9)) {
  soft_threshold(input.view(), output.view(), lambda, epsilon);
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<T>> soft_threshold(const PooledVector<std::complex<T>>& input, const T lambda,
                                                           const T epsilon = static_cast<T>(1.0e-9)) {
  return soft_threshold(input.view(), lambda, epsilon);
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<T>> soft_threshold(const PooledMatrix<std::complex<T>>& input, const T lambda,
                                                           const T epsilon = static_cast<T>(1.0e-9)) {
  return soft_threshold(input.view(), lambda, epsilon);
}

template <typename T>
[[nodiscard]] PooledImage<std::complex<T>> soft_threshold(const PooledImage<std::complex<T>>& input, const T lambda,
                                                          const T epsilon = static_cast<T>(1.0e-9)) {
  return soft_threshold(input.view(), lambda, epsilon);
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<T>> soft_threshold(const PooledCube<std::complex<T>>& input, const T lambda,
                                                         const T epsilon = static_cast<T>(1.0e-9)) {
  return soft_threshold(input.view(), lambda, epsilon);
}

template <typename T>
[[nodiscard]] PooledArray4D<std::complex<T>> soft_threshold(const PooledArray4D<std::complex<T>>& input, const T lambda,
                                                            const T epsilon = static_cast<T>(1.0e-9)) {
  return soft_threshold(input.view(), lambda, epsilon);
}

template <typename T>
void reflect_complex_about(CubeView<const std::complex<T>> input, CubeView<const std::complex<T>> reflector,
                           CubeView<std::complex<T>> output) {
  detail::validate_same_cube_shape(input, reflector, "complex reflection reflector shape mismatch");
  detail::validate_same_cube_shape(input, output, "complex reflection output shape mismatch");

  ksj::array::transform(input, reflector, output, [](const auto& input_value, const auto& reflector_value) {
    const auto normalized_reflector = reflector_value / std::abs(reflector_value);
    const auto projected = std::real(input_value * std::conj(normalized_reflector)) * normalized_reflector;
    return static_cast<T>(2.0) * projected - input_value;
  });
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<T>> reflect_complex_about(CubeView<const std::complex<T>> input,
                                                                CubeView<const std::complex<T>> reflector) {
  auto output = make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  reflect_complex_about(input, reflector, output.view());
  return output;
}

template <typename T>
void reflect_complex_about(const PooledCube<std::complex<T>>& input, const PooledCube<std::complex<T>>& reflector,
                           PooledCube<std::complex<T>>& output) {
  reflect_complex_about(input.view(), reflector.view(), output.view());
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<T>> reflect_complex_about(const PooledCube<std::complex<T>>& input,
                                                                const PooledCube<std::complex<T>>& reflector) {
  return reflect_complex_about(input.view(), reflector.view());
}

template <typename T>
void normalize_complex_phase(MatrixView<const std::complex<T>> input, MatrixView<std::complex<T>> output) {
  detail::validate_same_shape(input, output, "complex phase normalization output shape mismatch");
  ksj::array::transform(input, output, [](const auto& value) {
    return value / std::abs(value);
  });
}

template <typename T> void normalize_complex_phase_in_place(MatrixView<std::complex<T>> input_output) {
  normalize_complex_phase(as_const_view(input_output), input_output);
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<T>> normalize_complex_phase(MatrixView<const std::complex<T>> input) {
  auto output = make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
  normalize_complex_phase(input, output.view());
  return output;
}

template <typename T>
void normalize_complex_phase(const PooledMatrix<std::complex<T>>& input, PooledMatrix<std::complex<T>>& output) {
  normalize_complex_phase(input.view(), output.view());
}

template <typename T> void normalize_complex_phase_in_place(PooledMatrix<std::complex<T>>& input_output) {
  normalize_complex_phase_in_place(input_output.view());
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<T>> normalize_complex_phase(const PooledMatrix<std::complex<T>>& input) {
  return normalize_complex_phase(input.view());
}

template <typename T>
void normalize_complex_phase(CubeView<const std::complex<T>> input, CubeView<std::complex<T>> output) {
  detail::validate_same_cube_shape(input, output, "complex phase normalization output shape mismatch");
  ksj::array::transform(input, output, [](const auto& value) {
    return value / std::abs(value);
  });
}

template <typename T> void normalize_complex_phase_in_place(CubeView<std::complex<T>> input_output) {
  normalize_complex_phase(as_const_view(input_output), input_output);
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<T>> normalize_complex_phase(CubeView<const std::complex<T>> input) {
  auto output = make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  normalize_complex_phase(input, output.view());
  return output;
}

template <typename T>
void normalize_complex_phase(const PooledCube<std::complex<T>>& input, PooledCube<std::complex<T>>& output) {
  normalize_complex_phase(input.view(), output.view());
}

template <typename T> void normalize_complex_phase_in_place(PooledCube<std::complex<T>>& input_output) {
  normalize_complex_phase_in_place(input_output.view());
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<T>> normalize_complex_phase(const PooledCube<std::complex<T>>& input) {
  return normalize_complex_phase(input.view());
}

template <typename T>
void absolute_phase_difference(CubeView<const std::complex<T>> lhs, CubeView<const std::complex<T>> rhs,
                               CubeView<T> output) {
  detail::validate_same_cube_shape(lhs, rhs, "absolute phase difference rhs shape mismatch");
  detail::validate_same_cube_shape(lhs, output, "absolute phase difference output shape mismatch");

  ksj::array::transform(lhs, rhs, output, [](const auto& lhs_value, const auto& rhs_value) {
    return std::abs(std::arg(lhs_value * std::conj(rhs_value)));
  });
}

template <typename T>
[[nodiscard]] PooledCube<T> absolute_phase_difference(CubeView<const std::complex<T>> lhs,
                                                      CubeView<const std::complex<T>> rhs) {
  auto output = make_pooled_cube<T>(lhs.dim0(), lhs.dim1(), lhs.dim2());
  absolute_phase_difference(lhs, rhs, output.view());
  return output;
}

template <typename T>
void absolute_phase_difference(const PooledCube<std::complex<T>>& lhs, const PooledCube<std::complex<T>>& rhs,
                               PooledCube<T>& output) {
  absolute_phase_difference(lhs.view(), rhs.view(), output.view());
}

template <typename T>
[[nodiscard]] PooledCube<T> absolute_phase_difference(const PooledCube<std::complex<T>>& lhs,
                                                      const PooledCube<std::complex<T>>& rhs) {
  return absolute_phase_difference(lhs.view(), rhs.view());
}

template <typename T>
void rectangular_to_polar(VectorView<T> input, VectorView<real_scalar_t<T>> magnitude_output,
                          VectorView<real_scalar_t<T>> phase_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_size(input.size(), magnitude_output.size(),
                             "vector view rectangular_to_polar magnitude output size mismatch");
  detail::validate_same_size(input.size(), phase_output.size(),
                             "vector view rectangular_to_polar phase output size mismatch");
  if (detail::dispatch_rectangular_to_polar(input, magnitude_output, phase_output)) {
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    magnitude_output[index] = std::abs(input[index]);
    phase_output[index] = std::arg(input[index]);
  }
}

template <typename T>
void rectangular_to_polar(MatrixView<T> input, MatrixView<real_scalar_t<T>> magnitude_output,
                          MatrixView<real_scalar_t<T>> phase_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_shape(input, magnitude_output,
                              "matrix view rectangular_to_polar magnitude output shape mismatch");
  detail::validate_same_shape(input, phase_output, "matrix view rectangular_to_polar phase output shape mismatch");
  if (detail::dispatch_rectangular_to_polar(input, magnitude_output, phase_output)) {
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    magnitude_output[index] = std::abs(input[index]);
    phase_output[index] = std::arg(input[index]);
  }
}

template <typename T>
void rectangular_to_polar(ImageView<T> input, ImageView<real_scalar_t<T>> magnitude_output,
                          ImageView<real_scalar_t<T>> phase_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_shape(input, magnitude_output,
                              "image view rectangular_to_polar magnitude output shape mismatch");
  detail::validate_same_shape(input, phase_output, "image view rectangular_to_polar phase output shape mismatch");
  if (detail::dispatch_rectangular_to_polar(input, magnitude_output, phase_output)) {
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    magnitude_output[index] = std::abs(input[index]);
    phase_output[index] = std::arg(input[index]);
  }
}

template <typename T>
void rectangular_to_polar(CubeView<T> input, CubeView<real_scalar_t<T>> magnitude_output,
                          CubeView<real_scalar_t<T>> phase_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_cube_shape(input, magnitude_output,
                                   "cube view rectangular_to_polar magnitude output shape mismatch");
  detail::validate_same_cube_shape(input, phase_output, "cube view rectangular_to_polar phase output shape mismatch");
  if (detail::dispatch_rectangular_to_polar(input, magnitude_output, phase_output)) {
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    magnitude_output[index] = std::abs(input[index]);
    phase_output[index] = std::arg(input[index]);
  }
}

template <typename T>
void rectangular_to_polar(Array4DView<T> input, Array4DView<real_scalar_t<T>> magnitude_output,
                          Array4DView<real_scalar_t<T>> phase_output)
  requires(is_complex_v<T>)
{
  detail::validate_same_array4d_shape(input, magnitude_output,
                                      "array4d view rectangular_to_polar magnitude output shape mismatch");
  detail::validate_same_array4d_shape(input, phase_output,
                                      "array4d view rectangular_to_polar phase output shape mismatch");
  if (detail::dispatch_rectangular_to_polar(input, magnitude_output, phase_output)) {
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    magnitude_output[index] = std::abs(input[index]);
    phase_output[index] = std::arg(input[index]);
  }
}

template <typename T>
[[nodiscard]] PolarComponents<PooledVector<real_scalar_t<T>>> rectangular_to_polar_components(VectorView<T> input)
  requires(is_complex_v<T>)
{
  PolarComponents<PooledVector<real_scalar_t<T>>> output{
    make_pooled_vector<real_scalar_t<T>>(input.size()),
    make_pooled_vector<real_scalar_t<T>>(input.size()),
  };
  rectangular_to_polar(input, output.magnitude.view(), output.phase.view());
  return output;
}

template <typename T>
[[nodiscard]] PolarComponents<PooledMatrix<real_scalar_t<T>>> rectangular_to_polar_components(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  PolarComponents<PooledMatrix<real_scalar_t<T>>> output{
    make_pooled_matrix<real_scalar_t<T>>(input.rows(), input.cols()),
    make_pooled_matrix<real_scalar_t<T>>(input.rows(), input.cols()),
  };
  rectangular_to_polar(input, output.magnitude.view(), output.phase.view());
  return output;
}

template <typename T>
[[nodiscard]] PolarComponents<PooledImage<real_scalar_t<T>>> rectangular_to_polar_components(ImageView<T> input)
  requires(is_complex_v<T>)
{
  PolarComponents<PooledImage<real_scalar_t<T>>> output{
    make_pooled_image<real_scalar_t<T>>(input.rows(), input.cols()),
    make_pooled_image<real_scalar_t<T>>(input.rows(), input.cols()),
  };
  rectangular_to_polar(input, output.magnitude.view(), output.phase.view());
  return output;
}

template <typename T>
[[nodiscard]] PolarComponents<PooledCube<real_scalar_t<T>>> rectangular_to_polar_components(CubeView<T> input)
  requires(is_complex_v<T>)
{
  PolarComponents<PooledCube<real_scalar_t<T>>> output{
    make_pooled_cube<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2()),
    make_pooled_cube<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2()),
  };
  rectangular_to_polar(input, output.magnitude.view(), output.phase.view());
  return output;
}

template <typename T>
[[nodiscard]] PolarComponents<PooledArray4D<real_scalar_t<T>>> rectangular_to_polar_components(Array4DView<T> input)
  requires(is_complex_v<T>)
{
  PolarComponents<PooledArray4D<real_scalar_t<T>>> output{
    make_pooled_array4d<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3()),
    make_pooled_array4d<real_scalar_t<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3()),
  };
  rectangular_to_polar(input, output.magnitude.view(), output.phase.view());
  return output;
}

template <typename T>
[[nodiscard]] PolarComponents<PooledVector<T>>
rectangular_to_polar_components(const PooledVector<std::complex<T>>& input) {
  return rectangular_to_polar_components(input.view());
}

template <typename T>
[[nodiscard]] PolarComponents<PooledMatrix<T>>
rectangular_to_polar_components(const PooledMatrix<std::complex<T>>& input) {
  return rectangular_to_polar_components(input.view());
}

template <typename T>
[[nodiscard]] PolarComponents<PooledImage<T>>
rectangular_to_polar_components(const PooledImage<std::complex<T>>& input) {
  return rectangular_to_polar_components(input.view());
}

template <typename T>
[[nodiscard]] PolarComponents<PooledCube<T>> rectangular_to_polar_components(const PooledCube<std::complex<T>>& input) {
  return rectangular_to_polar_components(input.view());
}

template <typename T>
[[nodiscard]] PolarComponents<PooledArray4D<T>>
rectangular_to_polar_components(const PooledArray4D<std::complex<T>>& input) {
  return rectangular_to_polar_components(input.view());
}

template <typename T>
void rectangular_to_polar(VectorView<T> input, VectorView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_eigen_rectangular_to_polar(input, output) &&
      detail::eigen::rectangular_to_polar(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::remove_cv_t<T>{std::abs(value), std::arg(value)};
  });
}

template <typename T>
void rectangular_to_polar(MatrixView<T> input, MatrixView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_eigen_rectangular_to_polar(input, output) &&
      detail::eigen::rectangular_to_polar(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::remove_cv_t<T>{std::abs(value), std::arg(value)};
  });
}

template <typename T>
void rectangular_to_polar(ImageView<T> input, ImageView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  if (detail::prefer_eigen_rectangular_to_polar(input, output) &&
      detail::eigen::rectangular_to_polar(as_const_view(input), output)) {
    return;
  }

  transform(input, output, [](const auto& value) {
    return std::remove_cv_t<T>{std::abs(value), std::arg(value)};
  });
}

template <typename T>
[[nodiscard]] PooledVector<std::remove_cv_t<T>> rectangular_to_polar(VectorView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<std::remove_cv_t<T>>(input.size());
  rectangular_to_polar(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<std::remove_cv_t<T>> rectangular_to_polar(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<std::remove_cv_t<T>>(input.rows(), input.cols());
  rectangular_to_polar(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<std::remove_cv_t<T>> rectangular_to_polar(ImageView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<std::remove_cv_t<T>>(input.rows(), input.cols());
  rectangular_to_polar(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<T>> rectangular_to_polar(const PooledVector<std::complex<T>>& input) {
  return rectangular_to_polar(input.view());
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<T>> rectangular_to_polar(const PooledMatrix<std::complex<T>>& input) {
  return rectangular_to_polar(input.view());
}

template <typename T>
[[nodiscard]] PooledImage<std::complex<T>> rectangular_to_polar(const PooledImage<std::complex<T>>& input) {
  return rectangular_to_polar(input.view());
}

template <typename T>
void polar_to_rectangular(VectorView<T> magnitude_input, VectorView<T> phase_input,
                          VectorView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_size(magnitude_input.size(), phase_input.size(),
                             "vector view polar_to_rectangular input size mismatch");
  detail::validate_same_size(magnitude_input.size(), output.size(),
                             "vector view polar_to_rectangular output size mismatch");
  if (detail::dispatch_polar_to_rectangular(magnitude_input, phase_input, output)) {
    return;
  }
  detail::polar_to_rectangular_fallback(magnitude_input, phase_input, output);
}

template <typename T>
void polar_to_rectangular(MatrixView<T> magnitude_input, MatrixView<T> phase_input,
                          MatrixView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_shape(magnitude_input, phase_input, "matrix view polar_to_rectangular input shape mismatch");
  detail::validate_same_shape(magnitude_input, output, "matrix view polar_to_rectangular output shape mismatch");
  if (detail::dispatch_polar_to_rectangular(magnitude_input, phase_input, output)) {
    return;
  }
  detail::polar_to_rectangular_fallback(magnitude_input, phase_input, output);
}

template <typename T>
void polar_to_rectangular(ImageView<T> magnitude_input, ImageView<T> phase_input,
                          ImageView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_shape(magnitude_input, phase_input, "image view polar_to_rectangular input shape mismatch");
  detail::validate_same_shape(magnitude_input, output, "image view polar_to_rectangular output shape mismatch");
  if (detail::dispatch_polar_to_rectangular(magnitude_input, phase_input, output)) {
    return;
  }
  detail::polar_to_rectangular_fallback(magnitude_input, phase_input, output);
}

template <typename T>
void polar_to_rectangular(CubeView<T> magnitude_input, CubeView<T> phase_input,
                          CubeView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_cube_shape(magnitude_input, phase_input, "cube view polar_to_rectangular input shape mismatch");
  detail::validate_same_cube_shape(magnitude_input, output, "cube view polar_to_rectangular output shape mismatch");
  if (detail::dispatch_polar_to_rectangular(magnitude_input, phase_input, output)) {
    return;
  }
  detail::polar_to_rectangular_fallback(magnitude_input, phase_input, output);
}

template <typename T>
void polar_to_rectangular(Array4DView<T> magnitude_input, Array4DView<T> phase_input,
                          Array4DView<std::complex<std::remove_cv_t<T>>> output) {
  detail::validate_same_array4d_shape(magnitude_input, phase_input,
                                      "array4d view polar_to_rectangular input shape mismatch");
  detail::validate_same_array4d_shape(magnitude_input, output,
                                      "array4d view polar_to_rectangular output shape mismatch");
  if (detail::dispatch_polar_to_rectangular(magnitude_input, phase_input, output)) {
    return;
  }
  detail::polar_to_rectangular_fallback(magnitude_input, phase_input, output);
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<std::remove_cv_t<T>>> polar_to_rectangular(VectorView<T> magnitude_input,
                                                                                   VectorView<T> phase_input)
  requires(!is_complex_v<T>)
{
  auto output = make_pooled_vector<std::complex<std::remove_cv_t<T>>>(magnitude_input.size());
  polar_to_rectangular(magnitude_input, phase_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<std::remove_cv_t<T>>> polar_to_rectangular(MatrixView<T> magnitude_input,
                                                                                   MatrixView<T> phase_input)
  requires(!is_complex_v<T>)
{
  auto output = make_pooled_matrix<std::complex<std::remove_cv_t<T>>>(magnitude_input.rows(), magnitude_input.cols());
  polar_to_rectangular(magnitude_input, phase_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<std::complex<std::remove_cv_t<T>>> polar_to_rectangular(ImageView<T> magnitude_input,
                                                                                  ImageView<T> phase_input)
  requires(!is_complex_v<T>)
{
  auto output = make_pooled_image<std::complex<std::remove_cv_t<T>>>(magnitude_input.rows(), magnitude_input.cols());
  polar_to_rectangular(magnitude_input, phase_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<std::remove_cv_t<T>>> polar_to_rectangular(CubeView<T> magnitude_input,
                                                                                 CubeView<T> phase_input)
  requires(!is_complex_v<T>)
{
  auto output = make_pooled_cube<std::complex<std::remove_cv_t<T>>>(magnitude_input.dim0(), magnitude_input.dim1(),
                                                                    magnitude_input.dim2());
  polar_to_rectangular(magnitude_input, phase_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<std::complex<std::remove_cv_t<T>>> polar_to_rectangular(Array4DView<T> magnitude_input,
                                                                                    Array4DView<T> phase_input)
  requires(!is_complex_v<T>)
{
  auto output = make_pooled_array4d<std::complex<std::remove_cv_t<T>>>(magnitude_input.dim0(), magnitude_input.dim1(),
                                                                       magnitude_input.dim2(), magnitude_input.dim3());
  polar_to_rectangular(magnitude_input, phase_input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<T>> polar_to_rectangular(const PooledVector<T>& magnitude_input,
                                                                 const PooledVector<T>& phase_input)
  requires(!is_complex_v<T>)
{
  return polar_to_rectangular(magnitude_input.view(), phase_input.view());
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<T>> polar_to_rectangular(const PooledMatrix<T>& magnitude_input,
                                                                 const PooledMatrix<T>& phase_input)
  requires(!is_complex_v<T>)
{
  return polar_to_rectangular(magnitude_input.view(), phase_input.view());
}

template <typename T>
[[nodiscard]] PooledImage<std::complex<T>> polar_to_rectangular(const PooledImage<T>& magnitude_input,
                                                                const PooledImage<T>& phase_input)
  requires(!is_complex_v<T>)
{
  return polar_to_rectangular(magnitude_input.view(), phase_input.view());
}

template <typename T>
[[nodiscard]] PooledCube<std::complex<T>> polar_to_rectangular(const PooledCube<T>& magnitude_input,
                                                               const PooledCube<T>& phase_input)
  requires(!is_complex_v<T>)
{
  return polar_to_rectangular(magnitude_input.view(), phase_input.view());
}

template <typename T>
[[nodiscard]] PooledArray4D<std::complex<T>> polar_to_rectangular(const PooledArray4D<T>& magnitude_input,
                                                                  const PooledArray4D<T>& phase_input)
  requires(!is_complex_v<T>)
{
  return polar_to_rectangular(magnitude_input.view(), phase_input.view());
}

template <typename T>
void polar_to_rectangular(VectorView<T> input, VectorView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  transform(input, output, [](const auto& value) {
    return std::polar(value.real(), value.imag());
  });
}

template <typename T>
void polar_to_rectangular(MatrixView<T> input, MatrixView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  transform(input, output, [](const auto& value) {
    return std::polar(value.real(), value.imag());
  });
}

template <typename T>
void polar_to_rectangular(ImageView<T> input, ImageView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  transform(input, output, [](const auto& value) {
    return std::polar(value.real(), value.imag());
  });
}

template <typename T>
[[nodiscard]] PooledVector<std::remove_cv_t<T>> polar_to_rectangular(VectorView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<std::remove_cv_t<T>>(input.size());
  polar_to_rectangular(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<std::remove_cv_t<T>> polar_to_rectangular(MatrixView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<std::remove_cv_t<T>>(input.rows(), input.cols());
  polar_to_rectangular(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<std::remove_cv_t<T>> polar_to_rectangular(ImageView<T> input)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<std::remove_cv_t<T>>(input.rows(), input.cols());
  polar_to_rectangular(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledVector<std::complex<T>> polar_to_rectangular(const PooledVector<std::complex<T>>& input) {
  return polar_to_rectangular(input.view());
}

template <typename T>
[[nodiscard]] PooledMatrix<std::complex<T>> polar_to_rectangular(const PooledMatrix<std::complex<T>>& input) {
  return polar_to_rectangular(input.view());
}

template <typename T>
[[nodiscard]] PooledImage<std::complex<T>> polar_to_rectangular(const PooledImage<std::complex<T>>& input) {
  return polar_to_rectangular(input.view());
}

template <typename T>
void multiply_conjugate(VectorView<T> lhs, VectorView<T> rhs, VectorView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  detail::validate_same_size(lhs.size(), rhs.size(), "vector view multiply_conjugate input size mismatch");
  detail::validate_same_size(lhs.size(), output.size(), "vector view multiply_conjugate output size mismatch");
  if (detail::dispatch_multiply_conjugate(lhs, rhs, output)) {
    return;
  }
  detail::multiply_conjugate_fallback(lhs, rhs, output);
}

template <typename T>
void multiply_conjugate(MatrixView<T> lhs, MatrixView<T> rhs, MatrixView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  detail::validate_same_shape(lhs, rhs, "matrix view multiply_conjugate input shape mismatch");
  detail::validate_same_shape(lhs, output, "matrix view multiply_conjugate output shape mismatch");
  if (detail::dispatch_multiply_conjugate(lhs, rhs, output)) {
    return;
  }
  detail::multiply_conjugate_fallback(lhs, rhs, output);
}

template <typename T>
void multiply_conjugate(ImageView<T> lhs, ImageView<T> rhs, ImageView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  detail::validate_same_shape(lhs, rhs, "image view multiply_conjugate input shape mismatch");
  detail::validate_same_shape(lhs, output, "image view multiply_conjugate output shape mismatch");
  if (detail::dispatch_multiply_conjugate(lhs, rhs, output)) {
    return;
  }
  detail::multiply_conjugate_fallback(lhs, rhs, output);
}

template <typename T>
void multiply_conjugate(CubeView<T> lhs, CubeView<T> rhs, CubeView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  detail::validate_same_cube_shape(lhs, rhs, "cube view multiply_conjugate input shape mismatch");
  detail::validate_same_cube_shape(lhs, output, "cube view multiply_conjugate output shape mismatch");
  if (detail::dispatch_multiply_conjugate(lhs, rhs, output)) {
    return;
  }
  detail::multiply_conjugate_fallback(lhs, rhs, output);
}

template <typename T>
void multiply_conjugate(Array4DView<T> lhs, Array4DView<T> rhs, Array4DView<std::remove_cv_t<T>> output)
  requires(is_complex_v<T>)
{
  detail::validate_same_array4d_shape(lhs, rhs, "array4d view multiply_conjugate input shape mismatch");
  detail::validate_same_array4d_shape(lhs, output, "array4d view multiply_conjugate output shape mismatch");
  if (detail::dispatch_multiply_conjugate(lhs, rhs, output)) {
    return;
  }
  detail::multiply_conjugate_fallback(lhs, rhs, output);
}

template <typename T>
[[nodiscard]] PooledVector<std::remove_cv_t<T>> multiply_conjugate(VectorView<T> lhs, VectorView<T> rhs)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_vector<std::remove_cv_t<T>>(lhs.size());
  multiply_conjugate(lhs, rhs, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<std::remove_cv_t<T>> multiply_conjugate(MatrixView<T> lhs, MatrixView<T> rhs)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_matrix<std::remove_cv_t<T>>(lhs.rows(), lhs.cols());
  multiply_conjugate(lhs, rhs, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledImage<std::remove_cv_t<T>> multiply_conjugate(ImageView<T> lhs, ImageView<T> rhs)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_image<std::remove_cv_t<T>>(lhs.rows(), lhs.cols());
  multiply_conjugate(lhs, rhs, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledCube<std::remove_cv_t<T>> multiply_conjugate(CubeView<T> lhs, CubeView<T> rhs)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_cube<std::remove_cv_t<T>>(lhs.dim0(), lhs.dim1(), lhs.dim2());
  multiply_conjugate(lhs, rhs, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<std::remove_cv_t<T>> multiply_conjugate(Array4DView<T> lhs, Array4DView<T> rhs)
  requires(is_complex_v<T>)
{
  auto output = make_pooled_array4d<std::remove_cv_t<T>>(lhs.dim0(), lhs.dim1(), lhs.dim2(), lhs.dim3());
  multiply_conjugate(lhs, rhs, output.view());
  return output;
}

template <typename Input, typename Output, typename Epsilon>
void complex_unit_phasor(Input&& input, Output&& output, const Epsilon& epsilon) {
  ksj::array::transform(std::forward<Input>(input), std::forward<Output>(output), [&epsilon](const auto& value) {
    using complex_type = std::remove_cvref_t<decltype(value)>;
    using real_type = real_scalar_t<complex_type>;
    const auto magnitude = std::abs(value);
    if (magnitude > static_cast<real_type>(epsilon)) {
      return static_cast<complex_type>(value / magnitude);
    }
    return complex_type{};
  });
}

template <typename Phase, typename Signal, typename Scale, typename Output>
void conjugate_product_scaled(Phase&& phase, Signal&& signal, const Scale& scale, Output&& output) {
  ksj::array::transform(std::forward<Phase>(phase), std::forward<Signal>(signal), std::forward<Output>(output),
                        [&scale](const auto& phase_value, const auto& signal_value) {
                          return std::conj(phase_value) * signal_value * scale;
                        });
}

template <typename Accumulator, typename Phase, typename Signal, typename Scale>
void accumulate_conjugate_product_scaled(Accumulator&& accumulator, Phase&& phase, Signal&& signal,
                                         const Scale& scale) {
  ksj::array::transform(accumulator, std::forward<Phase>(phase), std::forward<Signal>(signal), accumulator,
                        [&scale](const auto& current, const auto& phase_value, const auto& signal_value) {
                          return current + std::conj(phase_value) * signal_value * scale;
                        });
}

template <typename Accumulator, typename Phase, typename Signal, typename Divisor>
void accumulate_conjugate_product_divided(Accumulator&& accumulator, Phase&& phase, Signal&& signal,
                                          const Divisor& divisor) {
  ksj::array::transform(accumulator, std::forward<Phase>(phase), std::forward<Signal>(signal), accumulator,
                        [&divisor](const auto& current, const auto& phase_value, const auto& signal_value) {
                          return current + (std::conj(phase_value) / divisor) * signal_value;
                        });
}

template <typename InputT, typename OutputT, typename AccumulatorT, typename SignalT, typename Epsilon, typename Scale>
void complex_unit_phasor_and_accumulate_conjugate_product(CubeView<InputT> input, CubeView<OutputT> phasor_output,
                                                          CubeView<AccumulatorT> accumulator, CubeView<SignalT> signal,
                                                          const Epsilon& epsilon, const Scale& scale) {
  detail::validate_same_cube_shape(input, phasor_output, "cube view complex_unit_phasor output shape mismatch");
  detail::validate_same_cube_shape(input, accumulator, "cube view complex_unit_phasor accumulator shape mismatch");
  detail::validate_same_cube_shape(input, signal, "cube view complex_unit_phasor signal shape mismatch");

  if (input.is_contiguous() && phasor_output.is_contiguous() && accumulator.is_contiguous() && signal.is_contiguous()) {
    const auto* input_data = input.data();
    auto* phasor_data = phasor_output.data();
    auto* accumulator_data = accumulator.data();
    const auto* signal_data = signal.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      using complex_type = std::remove_cvref_t<decltype(input_data[index])>;
      using real_type = real_scalar_t<complex_type>;
      const auto value = input_data[index];
      const auto magnitude = std::abs(value);
      complex_type phasor{};
      if (magnitude > static_cast<real_type>(epsilon)) {
        phasor = static_cast<complex_type>(value / magnitude);
      }
      phasor_data[index] = phasor;
      accumulator_data[index] += std::conj(phasor) * signal_data[index] * scale;
    }
    return;
  }

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        using complex_type = std::remove_cvref_t<decltype(input(i0, i1, i2))>;
        using real_type = real_scalar_t<complex_type>;
        const auto value = input(i0, i1, i2);
        const auto magnitude = std::abs(value);
        complex_type phasor{};
        if (magnitude > static_cast<real_type>(epsilon)) {
          phasor = static_cast<complex_type>(value / magnitude);
        }
        phasor_output(i0, i1, i2) = phasor;
        accumulator(i0, i1, i2) += std::conj(phasor) * signal(i0, i1, i2) * scale;
      }
    }
  }
}

template <typename InputT, typename OutputT, typename AccumulatorT, typename SignalT, typename Epsilon,
          typename Divisor>
void complex_unit_phasor_and_accumulate_conjugate_product_divided(CubeView<InputT> input,
                                                                  CubeView<OutputT> phasor_output,
                                                                  CubeView<AccumulatorT> accumulator,
                                                                  CubeView<SignalT> signal, const Epsilon& epsilon,
                                                                  const Divisor& divisor) {
  detail::validate_same_cube_shape(input, phasor_output, "cube view complex_unit_phasor output shape mismatch");
  detail::validate_same_cube_shape(input, accumulator, "cube view complex_unit_phasor accumulator shape mismatch");
  detail::validate_same_cube_shape(input, signal, "cube view complex_unit_phasor signal shape mismatch");

  if (input.is_contiguous() && phasor_output.is_contiguous() && accumulator.is_contiguous() && signal.is_contiguous()) {
    const auto* input_data = input.data();
    auto* phasor_data = phasor_output.data();
    auto* accumulator_data = accumulator.data();
    const auto* signal_data = signal.data();
    for (std::size_t index = 0U; index < input.size(); ++index) {
      using complex_type = std::remove_cvref_t<decltype(input_data[index])>;
      using real_type = real_scalar_t<complex_type>;
      const auto value = input_data[index];
      const auto magnitude = std::abs(value);
      complex_type phasor{};
      if (magnitude > static_cast<real_type>(epsilon)) {
        phasor = static_cast<complex_type>(value / magnitude);
      }
      phasor_data[index] = phasor;
      accumulator_data[index] += (std::conj(phasor) / divisor) * signal_data[index];
    }
    return;
  }

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        using complex_type = std::remove_cvref_t<decltype(input(i0, i1, i2))>;
        using real_type = real_scalar_t<complex_type>;
        const auto value = input(i0, i1, i2);
        const auto magnitude = std::abs(value);
        complex_type phasor{};
        if (magnitude > static_cast<real_type>(epsilon)) {
          phasor = static_cast<complex_type>(value / magnitude);
        }
        phasor_output(i0, i1, i2) = phasor;
        accumulator(i0, i1, i2) += (std::conj(phasor) / divisor) * signal(i0, i1, i2);
      }
    }
  }
}

template <typename InputT, typename OutputT, typename AccumulatorT, typename SignalT, typename Epsilon, typename Scale>
void complex_unit_phasor_and_accumulate_conjugate_product(const PooledCube<InputT>& input,
                                                          PooledCube<OutputT>& phasor_output,
                                                          PooledCube<AccumulatorT>& accumulator,
                                                          const PooledCube<SignalT>& signal, const Epsilon& epsilon,
                                                          const Scale& scale) {
  complex_unit_phasor_and_accumulate_conjugate_product(input.view(), phasor_output.view(), accumulator.view(),
                                                       signal.view(), epsilon, scale);
}

template <typename T> void unit_phasor(VectorView<T> phase, VectorView<std::complex<std::remove_cv_t<T>>> output) {
  transform(phase, output, [](const auto& value) {
    return std::polar(std::remove_cv_t<T>{1}, value);
  });
}

template <typename T> void unit_phasor(MatrixView<T> phase, MatrixView<std::complex<std::remove_cv_t<T>>> output) {
  transform(phase, output, [](const auto& value) {
    return std::polar(std::remove_cv_t<T>{1}, value);
  });
}

template <typename T> void unit_phasor(ImageView<T> phase, ImageView<std::complex<std::remove_cv_t<T>>> output) {
  transform(phase, output, [](const auto& value) {
    return std::polar(std::remove_cv_t<T>{1}, value);
  });
}

template <typename T> void unit_phasor(CubeView<T> phase, CubeView<std::complex<std::remove_cv_t<T>>> output) {
  transform(phase, output, [](const auto& value) {
    return std::polar(std::remove_cv_t<T>{1}, value);
  });
}

template <typename T> void unit_phasor(Array4DView<T> phase, Array4DView<std::complex<std::remove_cv_t<T>>> output) {
  transform(phase, output, [](const auto& value) {
    return std::polar(std::remove_cv_t<T>{1}, value);
  });
}

template <typename T> [[nodiscard]] PooledVector<std::complex<std::remove_cv_t<T>>> unit_phasor(VectorView<T> phase) {
  auto output = make_pooled_vector<std::complex<std::remove_cv_t<T>>>(phase.size());
  unit_phasor(phase, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<std::complex<std::remove_cv_t<T>>> unit_phasor(MatrixView<T> phase) {
  auto output = make_pooled_matrix<std::complex<std::remove_cv_t<T>>>(phase.rows(), phase.cols());
  unit_phasor(phase, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledImage<std::complex<std::remove_cv_t<T>>> unit_phasor(ImageView<T> phase) {
  auto output = make_pooled_image<std::complex<std::remove_cv_t<T>>>(phase.rows(), phase.cols());
  unit_phasor(phase, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledCube<std::complex<std::remove_cv_t<T>>> unit_phasor(CubeView<T> phase) {
  auto output = make_pooled_cube<std::complex<std::remove_cv_t<T>>>(phase.dim0(), phase.dim1(), phase.dim2());
  unit_phasor(phase, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledArray4D<std::complex<std::remove_cv_t<T>>> unit_phasor(Array4DView<T> phase) {
  auto output =
    make_pooled_array4d<std::complex<std::remove_cv_t<T>>>(phase.dim0(), phase.dim1(), phase.dim2(), phase.dim3());
  unit_phasor(phase, output.view());
  return output;
}

template <typename T> [[nodiscard]] PooledVector<std::complex<T>> unit_phasor(const PooledVector<T>& phase) {
  return unit_phasor(phase.view());
}

template <typename T> [[nodiscard]] PooledMatrix<std::complex<T>> unit_phasor(const PooledMatrix<T>& phase) {
  return unit_phasor(phase.view());
}

template <typename T> [[nodiscard]] PooledImage<std::complex<T>> unit_phasor(const PooledImage<T>& phase) {
  return unit_phasor(phase.view());
}

template <typename T> [[nodiscard]] PooledCube<std::complex<T>> unit_phasor(const PooledCube<T>& phase) {
  return unit_phasor(phase.view());
}

template <typename T> [[nodiscard]] PooledArray4D<std::complex<T>> unit_phasor(const PooledArray4D<T>& phase) {
  return unit_phasor(phase.view());
}

} // namespace ksj::array
