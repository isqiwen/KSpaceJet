#include "kspacejet/array/detail/eigen/eigen_array_complex.hpp"

#include <Eigen/Core>

#include <cmath>
#include <complex>
#include <type_traits>

namespace ksj::array::detail::eigen {
namespace {

template <typename T>
using ConstComplexMap = Eigen::Map<const Eigen::Array<std::complex<T>, Eigen::Dynamic, 1>, Eigen::Unaligned>;

template <typename T> using ComplexMap = Eigen::Map<Eigen::Array<std::complex<T>, Eigen::Dynamic, 1>, Eigen::Unaligned>;

template <typename T> using ConstRealMap = Eigen::Map<const Eigen::Array<T, Eigen::Dynamic, 1>, Eigen::Unaligned>;

template <typename T> using RealMap = Eigen::Map<Eigen::Array<T, Eigen::Dynamic, 1>, Eigen::Unaligned>;

template <typename InputView, typename OutputView>
[[nodiscard]] bool valid_contiguous_views(const InputView& input, const OutputView& output) noexcept {
  return input.shape().extents == output.shape().extents && input.is_contiguous() && output.is_contiguous();
}

template <typename FirstView, typename SecondView, typename ThirdView>
[[nodiscard]] bool valid_contiguous_views(const FirstView& first, const SecondView& second,
                                          const ThirdView& third) noexcept {
  return first.shape().extents == second.shape().extents && first.shape().extents == third.shape().extents &&
         first.is_contiguous() && second.is_contiguous() && third.is_contiguous();
}

template <typename T> [[nodiscard]] auto as_const_complex_map(const T* data, const std::size_t size) {
  return ConstComplexMap<typename T::value_type>(data, static_cast<Eigen::Index>(size));
}

template <typename T> [[nodiscard]] auto as_real_map(T* data, const std::size_t size) {
  return RealMap<T>(data, static_cast<Eigen::Index>(size));
}

template <typename T> [[nodiscard]] auto as_const_real_map(const T* data, const std::size_t size) {
  return ConstRealMap<T>(data, static_cast<Eigen::Index>(size));
}

template <typename T> [[nodiscard]] auto as_complex_map(std::complex<T>* data, const std::size_t size) {
  return ComplexMap<T>(data, static_cast<Eigen::Index>(size));
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool complex_magnitude_impl(InputView input, OutputView output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }

  auto input_map = as_const_complex_map(input.data(), input.size());
  auto output_map = as_real_map(output.data(), output.size());
  output_map = input_map.abs();
  return true;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool complex_real_impl(InputView input, OutputView output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }

  auto input_map = as_const_complex_map(input.data(), input.size());
  auto output_map = as_real_map(output.data(), output.size());
  output_map = input_map.real();
  return true;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool complex_imag_impl(InputView input, OutputView output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }

  auto input_map = as_const_complex_map(input.data(), input.size());
  auto output_map = as_real_map(output.data(), output.size());
  output_map = input_map.imag();
  return true;
}

template <typename InputView, typename RealOutputView, typename ImagOutputView>
[[nodiscard]] bool split_complex_impl(InputView input, RealOutputView real_output, ImagOutputView imag_output) {
  if (!valid_contiguous_views(input, real_output, imag_output)) {
    return false;
  }

  auto input_map = as_const_complex_map(input.data(), input.size());
  auto real_map = as_real_map(real_output.data(), real_output.size());
  auto imag_map = as_real_map(imag_output.data(), imag_output.size());
  real_map = input_map.real();
  imag_map = input_map.imag();
  return true;
}

template <typename RealInputView, typename ImagInputView, typename OutputView>
[[nodiscard]] bool complex_from_real_imag_impl(RealInputView real_input, ImagInputView imag_input, OutputView output) {
  if (!valid_contiguous_views(real_input, imag_input, output)) {
    return false;
  }

  auto real_map = as_const_real_map(real_input.data(), real_input.size());
  auto imag_map = as_const_real_map(imag_input.data(), imag_input.size());
  auto output_map = as_complex_map(output.data(), output.size());
  output_map.real() = real_map;
  output_map.imag() = imag_map;
  return true;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool complex_phase_impl(InputView input, OutputView output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }

  auto input_map = as_const_complex_map(input.data(), input.size());
  auto output_map = as_real_map(output.data(), output.size());
  output_map = input_map.unaryExpr([](const auto& value) {
    return std::arg(value);
  });
  return true;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool complex_conjugate_impl(InputView input, OutputView output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }

  auto input_map = as_const_complex_map(input.data(), input.size());
  auto output_map = as_complex_map(output.data(), output.size());
  output_map = input_map.conjugate();
  return true;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool rectangular_to_polar_impl(InputView input, OutputView output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }

  auto input_map = as_const_complex_map(input.data(), input.size());
  auto output_map = as_complex_map(output.data(), output.size());
  output_map = input_map.unaryExpr([](const auto& value) {
    using complex_type = std::remove_cvref_t<decltype(value)>;
    using real_type = typename complex_type::value_type;
    return complex_type{std::abs(value), static_cast<real_type>(std::arg(value))};
  });
  return true;
}

template <typename InputView, typename MagnitudeOutputView, typename PhaseOutputView>
[[nodiscard]] bool rectangular_to_polar_split_impl(InputView input, MagnitudeOutputView magnitude_output,
                                                   PhaseOutputView phase_output) {
  if (!valid_contiguous_views(input, magnitude_output, phase_output)) {
    return false;
  }

  auto input_map = as_const_complex_map(input.data(), input.size());
  auto magnitude_map = as_real_map(magnitude_output.data(), magnitude_output.size());
  auto phase_map = as_real_map(phase_output.data(), phase_output.size());
  magnitude_map = input_map.abs();
  phase_map = input_map.unaryExpr([](const auto& value) {
    return std::arg(value);
  });
  return true;
}

template <typename MagnitudeInputView, typename PhaseInputView, typename OutputView>
[[nodiscard]] bool polar_to_rectangular_impl(MagnitudeInputView magnitude_input, PhaseInputView phase_input,
                                             OutputView output) {
  if (!valid_contiguous_views(magnitude_input, phase_input, output)) {
    return false;
  }

  auto magnitude_map = as_const_real_map(magnitude_input.data(), magnitude_input.size());
  auto phase_map = as_const_real_map(phase_input.data(), phase_input.size());
  auto output_map = as_complex_map(output.data(), output.size());
  output_map = magnitude_map.binaryExpr(phase_map, [](const auto magnitude, const auto phase) {
    return std::polar(magnitude, phase);
  });
  return true;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool multiply_conjugate_impl(InputView lhs, InputView rhs, OutputView output) {
  if (!valid_contiguous_views(lhs, rhs, output)) {
    return false;
  }

  auto lhs_map = as_const_complex_map(lhs.data(), lhs.size());
  auto rhs_map = as_const_complex_map(rhs.data(), rhs.size());
  auto output_map = as_complex_map(output.data(), output.size());
  output_map = lhs_map * rhs_map.conjugate();
  return true;
}

} // namespace

bool complex_real(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output) {
  return complex_real_impl(input, output);
}

bool complex_real(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output) {
  return complex_real_impl(input, output);
}

bool complex_real(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> output) {
  return complex_real_impl(input, output);
}

bool complex_real(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> output) {
  return complex_real_impl(input, output);
}

bool complex_real(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> output) {
  return complex_real_impl(input, output);
}

bool complex_real(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> output) {
  return complex_real_impl(input, output);
}

bool complex_real(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> output) {
  return complex_real_impl(input, output);
}

bool complex_real(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> output) {
  return complex_real_impl(input, output);
}

bool complex_real(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> output) {
  return complex_real_impl(input, output);
}

bool complex_real(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> output) {
  return complex_real_impl(input, output);
}

bool complex_imag(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output) {
  return complex_imag_impl(input, output);
}

bool complex_imag(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output) {
  return complex_imag_impl(input, output);
}

bool complex_imag(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> output) {
  return complex_imag_impl(input, output);
}

bool complex_imag(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> output) {
  return complex_imag_impl(input, output);
}

bool complex_imag(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> output) {
  return complex_imag_impl(input, output);
}

bool complex_imag(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> output) {
  return complex_imag_impl(input, output);
}

bool complex_imag(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> output) {
  return complex_imag_impl(input, output);
}

bool complex_imag(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> output) {
  return complex_imag_impl(input, output);
}

bool complex_imag(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> output) {
  return complex_imag_impl(input, output);
}

bool complex_imag(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> output) {
  return complex_imag_impl(input, output);
}

bool split_complex(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> real_output,
                   VectorView<ksj::base::f32> imag_output) {
  return split_complex_impl(input, real_output, imag_output);
}

bool split_complex(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> real_output,
                   VectorView<ksj::base::f64> imag_output) {
  return split_complex_impl(input, real_output, imag_output);
}

bool complex_from_real_imag(VectorView<const ksj::base::f32> real_input, VectorView<const ksj::base::f32> imag_input,
                            VectorView<ksj::base::cf32> output) {
  return complex_from_real_imag_impl(real_input, imag_input, output);
}

bool complex_from_real_imag(VectorView<const ksj::base::f64> real_input, VectorView<const ksj::base::f64> imag_input,
                            VectorView<ksj::base::cf64> output) {
  return complex_from_real_imag_impl(real_input, imag_input, output);
}

bool complex_magnitude(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_magnitude(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_magnitude(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_magnitude(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_magnitude(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_magnitude(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_magnitude(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_magnitude(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_magnitude(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_magnitude(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> output) {
  return complex_magnitude_impl(input, output);
}

bool complex_phase(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output) {
  return complex_phase_impl(input, output);
}

bool complex_phase(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output) {
  return complex_phase_impl(input, output);
}

bool complex_phase(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> output) {
  return complex_phase_impl(input, output);
}

bool complex_phase(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> output) {
  return complex_phase_impl(input, output);
}

bool complex_phase(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> output) {
  return complex_phase_impl(input, output);
}

bool complex_phase(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> output) {
  return complex_phase_impl(input, output);
}

bool complex_phase(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> output) {
  return complex_phase_impl(input, output);
}

bool complex_phase(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> output) {
  return complex_phase_impl(input, output);
}

bool complex_phase(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> output) {
  return complex_phase_impl(input, output);
}

bool complex_phase(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> output) {
  return complex_phase_impl(input, output);
}

bool complex_conjugate(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::cf32> output) {
  return complex_conjugate_impl(input, output);
}

bool complex_conjugate(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::cf64> output) {
  return complex_conjugate_impl(input, output);
}

bool complex_conjugate(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::cf32> output) {
  return complex_conjugate_impl(input, output);
}

bool complex_conjugate(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::cf64> output) {
  return complex_conjugate_impl(input, output);
}

bool complex_conjugate(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::cf32> output) {
  return complex_conjugate_impl(input, output);
}

bool complex_conjugate(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::cf64> output) {
  return complex_conjugate_impl(input, output);
}

bool complex_conjugate(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::cf32> output) {
  return complex_conjugate_impl(input, output);
}

bool complex_conjugate(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::cf64> output) {
  return complex_conjugate_impl(input, output);
}

bool complex_conjugate(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::cf32> output) {
  return complex_conjugate_impl(input, output);
}

bool complex_conjugate(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::cf64> output) {
  return complex_conjugate_impl(input, output);
}

bool rectangular_to_polar(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::cf32> output) {
  return rectangular_to_polar_impl(input, output);
}

bool rectangular_to_polar(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::cf64> output) {
  return rectangular_to_polar_impl(input, output);
}

bool rectangular_to_polar(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::cf32> output) {
  return rectangular_to_polar_impl(input, output);
}

bool rectangular_to_polar(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::cf64> output) {
  return rectangular_to_polar_impl(input, output);
}

bool rectangular_to_polar(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::cf32> output) {
  return rectangular_to_polar_impl(input, output);
}

bool rectangular_to_polar(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::cf64> output) {
  return rectangular_to_polar_impl(input, output);
}

bool rectangular_to_polar(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> magnitude_output,
                          VectorView<ksj::base::f32> phase_output) {
  return rectangular_to_polar_split_impl(input, magnitude_output, phase_output);
}

bool rectangular_to_polar(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> magnitude_output,
                          VectorView<ksj::base::f64> phase_output) {
  return rectangular_to_polar_split_impl(input, magnitude_output, phase_output);
}

bool polar_to_rectangular(VectorView<const ksj::base::f32> magnitude_input,
                          VectorView<const ksj::base::f32> phase_input, VectorView<ksj::base::cf32> output) {
  return polar_to_rectangular_impl(magnitude_input, phase_input, output);
}

bool polar_to_rectangular(VectorView<const ksj::base::f64> magnitude_input,
                          VectorView<const ksj::base::f64> phase_input, VectorView<ksj::base::cf64> output) {
  return polar_to_rectangular_impl(magnitude_input, phase_input, output);
}

bool multiply_conjugate(VectorView<const ksj::base::cf32> lhs, VectorView<const ksj::base::cf32> rhs,
                        VectorView<ksj::base::cf32> output) {
  return multiply_conjugate_impl(lhs, rhs, output);
}

bool multiply_conjugate(VectorView<const ksj::base::cf64> lhs, VectorView<const ksj::base::cf64> rhs,
                        VectorView<ksj::base::cf64> output) {
  return multiply_conjugate_impl(lhs, rhs, output);
}

} // namespace ksj::array::detail::eigen
