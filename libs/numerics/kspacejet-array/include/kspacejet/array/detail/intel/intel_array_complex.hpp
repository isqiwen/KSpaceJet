#pragma once

#include "kspacejet/array/views.hpp"
#include "kspacejet/base/types.hpp"

namespace ksj::array::detail::intel {

[[nodiscard]] bool complex_real(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool complex_real(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output);
[[nodiscard]] bool complex_real(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> output);
[[nodiscard]] bool complex_real(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> output);
[[nodiscard]] bool complex_real(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> output);
[[nodiscard]] bool complex_real(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> output);
[[nodiscard]] bool complex_real(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> output);
[[nodiscard]] bool complex_real(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> output);
[[nodiscard]] bool complex_real(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> output);
[[nodiscard]] bool complex_real(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> output);

[[nodiscard]] bool complex_imag(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool complex_imag(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output);
[[nodiscard]] bool complex_imag(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> output);
[[nodiscard]] bool complex_imag(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> output);
[[nodiscard]] bool complex_imag(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> output);
[[nodiscard]] bool complex_imag(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> output);
[[nodiscard]] bool complex_imag(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> output);
[[nodiscard]] bool complex_imag(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> output);
[[nodiscard]] bool complex_imag(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> output);
[[nodiscard]] bool complex_imag(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> output);

[[nodiscard]] bool split_complex(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> real_output,
                                 VectorView<ksj::base::f32> imag_output);
[[nodiscard]] bool split_complex(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> real_output,
                                 VectorView<ksj::base::f64> imag_output);
[[nodiscard]] bool split_complex(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> real_output,
                                 MatrixView<ksj::base::f32> imag_output);
[[nodiscard]] bool split_complex(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> real_output,
                                 MatrixView<ksj::base::f64> imag_output);
[[nodiscard]] bool split_complex(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> real_output,
                                 ImageView<ksj::base::f32> imag_output);
[[nodiscard]] bool split_complex(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> real_output,
                                 ImageView<ksj::base::f64> imag_output);
[[nodiscard]] bool split_complex(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> real_output,
                                 CubeView<ksj::base::f32> imag_output);
[[nodiscard]] bool split_complex(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> real_output,
                                 CubeView<ksj::base::f64> imag_output);
[[nodiscard]] bool split_complex(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> real_output,
                                 Array4DView<ksj::base::f32> imag_output);
[[nodiscard]] bool split_complex(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> real_output,
                                 Array4DView<ksj::base::f64> imag_output);

[[nodiscard]] bool complex_from_real_imag(VectorView<const ksj::base::f32> real_input,
                                          VectorView<const ksj::base::f32> imag_input,
                                          VectorView<ksj::base::cf32> output);
[[nodiscard]] bool complex_from_real_imag(VectorView<const ksj::base::f64> real_input,
                                          VectorView<const ksj::base::f64> imag_input,
                                          VectorView<ksj::base::cf64> output);
[[nodiscard]] bool complex_from_real_imag(MatrixView<const ksj::base::f32> real_input,
                                          MatrixView<const ksj::base::f32> imag_input,
                                          MatrixView<ksj::base::cf32> output);
[[nodiscard]] bool complex_from_real_imag(MatrixView<const ksj::base::f64> real_input,
                                          MatrixView<const ksj::base::f64> imag_input,
                                          MatrixView<ksj::base::cf64> output);
[[nodiscard]] bool complex_from_real_imag(ImageView<const ksj::base::f32> real_input,
                                          ImageView<const ksj::base::f32> imag_input,
                                          ImageView<ksj::base::cf32> output);
[[nodiscard]] bool complex_from_real_imag(ImageView<const ksj::base::f64> real_input,
                                          ImageView<const ksj::base::f64> imag_input,
                                          ImageView<ksj::base::cf64> output);
[[nodiscard]] bool complex_from_real_imag(CubeView<const ksj::base::f32> real_input,
                                          CubeView<const ksj::base::f32> imag_input, CubeView<ksj::base::cf32> output);
[[nodiscard]] bool complex_from_real_imag(CubeView<const ksj::base::f64> real_input,
                                          CubeView<const ksj::base::f64> imag_input, CubeView<ksj::base::cf64> output);
[[nodiscard]] bool complex_from_real_imag(Array4DView<const ksj::base::f32> real_input,
                                          Array4DView<const ksj::base::f32> imag_input,
                                          Array4DView<ksj::base::cf32> output);
[[nodiscard]] bool complex_from_real_imag(Array4DView<const ksj::base::f64> real_input,
                                          Array4DView<const ksj::base::f64> imag_input,
                                          Array4DView<ksj::base::cf64> output);

[[nodiscard]] bool complex_magnitude(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool complex_magnitude(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output);
[[nodiscard]] bool complex_magnitude(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> output);
[[nodiscard]] bool complex_magnitude(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> output);
[[nodiscard]] bool complex_magnitude(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> output);
[[nodiscard]] bool complex_magnitude(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> output);
[[nodiscard]] bool complex_magnitude(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> output);
[[nodiscard]] bool complex_magnitude(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> output);
[[nodiscard]] bool complex_magnitude(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> output);
[[nodiscard]] bool complex_magnitude(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> output);

[[nodiscard]] bool complex_phase(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool complex_phase(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output);
[[nodiscard]] bool complex_phase(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> output);
[[nodiscard]] bool complex_phase(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> output);
[[nodiscard]] bool complex_phase(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> output);
[[nodiscard]] bool complex_phase(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> output);
[[nodiscard]] bool complex_phase(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> output);
[[nodiscard]] bool complex_phase(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> output);
[[nodiscard]] bool complex_phase(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> output);
[[nodiscard]] bool complex_phase(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> output);

[[nodiscard]] bool complex_conjugate(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::cf32> output);
[[nodiscard]] bool complex_conjugate(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::cf64> output);
[[nodiscard]] bool complex_conjugate(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::cf32> output);
[[nodiscard]] bool complex_conjugate(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::cf64> output);
[[nodiscard]] bool complex_conjugate(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::cf32> output);
[[nodiscard]] bool complex_conjugate(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::cf64> output);
[[nodiscard]] bool complex_conjugate(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::cf32> output);
[[nodiscard]] bool complex_conjugate(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::cf64> output);
[[nodiscard]] bool complex_conjugate(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::cf32> output);
[[nodiscard]] bool complex_conjugate(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::cf64> output);

[[nodiscard]] bool rectangular_to_polar(VectorView<const ksj::base::cf32> input,
                                        VectorView<ksj::base::f32> magnitude_output,
                                        VectorView<ksj::base::f32> phase_output);
[[nodiscard]] bool rectangular_to_polar(VectorView<const ksj::base::cf64> input,
                                        VectorView<ksj::base::f64> magnitude_output,
                                        VectorView<ksj::base::f64> phase_output);
[[nodiscard]] bool polar_to_rectangular(VectorView<const ksj::base::f32> magnitude_input,
                                        VectorView<const ksj::base::f32> phase_input,
                                        VectorView<ksj::base::cf32> output);
[[nodiscard]] bool polar_to_rectangular(VectorView<const ksj::base::f64> magnitude_input,
                                        VectorView<const ksj::base::f64> phase_input,
                                        VectorView<ksj::base::cf64> output);
[[nodiscard]] bool multiply_conjugate(VectorView<const ksj::base::cf32> lhs, VectorView<const ksj::base::cf32> rhs,
                                      VectorView<ksj::base::cf32> output);
[[nodiscard]] bool multiply_conjugate(VectorView<const ksj::base::cf64> lhs, VectorView<const ksj::base::cf64> rhs,
                                      VectorView<ksj::base::cf64> output);

template <typename InputView, typename OutputView> [[nodiscard]] bool complex_real(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView> [[nodiscard]] bool complex_imag(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename RealOutputView, typename ImagOutputView>
[[nodiscard]] bool split_complex(InputView, RealOutputView, ImagOutputView) noexcept {
  return false;
}

template <typename RealInputView, typename ImagInputView, typename OutputView>
[[nodiscard]] bool complex_from_real_imag(RealInputView, ImagInputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool complex_magnitude(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView> [[nodiscard]] bool complex_phase(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool complex_conjugate(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename MagnitudeOutputView, typename PhaseOutputView>
[[nodiscard]] bool rectangular_to_polar(InputView, MagnitudeOutputView, PhaseOutputView) noexcept {
  return false;
}

template <typename MagnitudeInputView, typename PhaseInputView, typename OutputView>
[[nodiscard]] bool polar_to_rectangular(MagnitudeInputView, PhaseInputView, OutputView) noexcept {
  return false;
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool multiply_conjugate(LhsView, RhsView, OutputView) noexcept {
  return false;
}

} // namespace ksj::array::detail::intel
