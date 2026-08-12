#include "kspacejet/array/detail/intel/intel_array_complex.hpp"

#include <cstdint>
#include <limits>

#include <ipp.h>

namespace ksj::array::detail::intel {

namespace {

static_assert(sizeof(ksj::base::cf32) == sizeof(Ipp32fc));
static_assert(sizeof(ksj::base::cf64) == sizeof(Ipp64fc));

[[nodiscard]] bool check_status(const IppStatus status) noexcept {
  return status == ippStsNoErr;
}

[[nodiscard]] bool fits_ipp_length(const std::size_t size) noexcept {
  return size <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool valid_contiguous_views(const InputView& input, const OutputView& output) noexcept {
  return input.shape().extents == output.shape().extents && input.is_contiguous() && output.is_contiguous() &&
         fits_ipp_length(input.size());
}

template <typename FirstView, typename SecondView, typename ThirdView>
[[nodiscard]] bool valid_contiguous_views(const FirstView& first, const SecondView& second,
                                          const ThirdView& third) noexcept {
  return first.shape().extents == second.shape().extents && first.shape().extents == third.shape().extents &&
         first.is_contiguous() && second.is_contiguous() && third.is_contiguous() && fits_ipp_length(first.size());
}

template <typename T> [[nodiscard]] std::uintptr_t address(T* pointer) noexcept {
  return reinterpret_cast<std::uintptr_t>(pointer);
}

template <typename LeftT, typename RightT>
[[nodiscard]] bool byte_ranges_overlap(LeftT* left, const std::size_t left_count, RightT* right,
                                       const std::size_t right_count) noexcept {
  if (left_count == 0U || right_count == 0U) {
    return false;
  }
  const auto left_begin = address(left);
  const auto left_end = left_begin + left_count * sizeof(LeftT);
  const auto right_begin = address(right);
  const auto right_end = right_begin + right_count * sizeof(RightT);
  return left_begin < right_end && right_begin < left_end;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool exact_same_data(const InputView& input, const OutputView& output) noexcept {
  return static_cast<const void*>(input.data()) == static_cast<const void*>(output.data()) &&
         input.size() == output.size();
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool output_partially_overlaps_input(const InputView& input, const OutputView& output) noexcept {
  return byte_ranges_overlap(input.data(), input.size(), output.data(), output.size()) &&
         !exact_same_data(input, output);
}

template <typename FirstOutputView, typename SecondOutputView>
[[nodiscard]] bool outputs_overlap(const FirstOutputView& first, const SecondOutputView& second) noexcept {
  return byte_ranges_overlap(first.data(), first.size(), second.data(), second.size());
}

[[nodiscard]] bool complex_magnitude_contiguous(const ksj::base::cf32* input, ksj::base::f32* output,
                                                const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsMagnitude_32fc(reinterpret_cast<const Ipp32fc*>(input), output, static_cast<int>(size)));
}

[[nodiscard]] bool complex_magnitude_contiguous(const ksj::base::cf64* input, ksj::base::f64* output,
                                                const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsMagnitude_64fc(reinterpret_cast<const Ipp64fc*>(input), output, static_cast<int>(size)));
}

[[nodiscard]] bool complex_phase_contiguous(const ksj::base::cf32* input, ksj::base::f32* output,
                                            const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsPhase_32fc(reinterpret_cast<const Ipp32fc*>(input), output, static_cast<int>(size)));
}

[[nodiscard]] bool complex_phase_contiguous(const ksj::base::cf64* input, ksj::base::f64* output,
                                            const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsPhase_64fc(reinterpret_cast<const Ipp64fc*>(input), output, static_cast<int>(size)));
}

[[nodiscard]] bool complex_real_contiguous(const ksj::base::cf32* input, ksj::base::f32* output,
                                           const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsReal_32fc(reinterpret_cast<const Ipp32fc*>(input), output, static_cast<int>(size)));
}

[[nodiscard]] bool complex_real_contiguous(const ksj::base::cf64* input, ksj::base::f64* output,
                                           const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsReal_64fc(reinterpret_cast<const Ipp64fc*>(input), output, static_cast<int>(size)));
}

[[nodiscard]] bool complex_imag_contiguous(const ksj::base::cf32* input, ksj::base::f32* output,
                                           const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsImag_32fc(reinterpret_cast<const Ipp32fc*>(input), output, static_cast<int>(size)));
}

[[nodiscard]] bool complex_imag_contiguous(const ksj::base::cf64* input, ksj::base::f64* output,
                                           const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsImag_64fc(reinterpret_cast<const Ipp64fc*>(input), output, static_cast<int>(size)));
}

[[nodiscard]] bool split_complex_contiguous(const ksj::base::cf32* input, ksj::base::f32* real_output,
                                            ksj::base::f32* imag_output, const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(
    ippsCplxToReal_32fc(reinterpret_cast<const Ipp32fc*>(input), real_output, imag_output, static_cast<int>(size)));
}

[[nodiscard]] bool split_complex_contiguous(const ksj::base::cf64* input, ksj::base::f64* real_output,
                                            ksj::base::f64* imag_output, const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(
    ippsCplxToReal_64fc(reinterpret_cast<const Ipp64fc*>(input), real_output, imag_output, static_cast<int>(size)));
}

[[nodiscard]] bool complex_from_real_imag_contiguous(const ksj::base::f32* real_input, const ksj::base::f32* imag_input,
                                                     ksj::base::cf32* output, const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(
    ippsRealToCplx_32f(real_input, imag_input, reinterpret_cast<Ipp32fc*>(output), static_cast<int>(size)));
}

[[nodiscard]] bool complex_from_real_imag_contiguous(const ksj::base::f64* real_input, const ksj::base::f64* imag_input,
                                                     ksj::base::cf64* output, const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(
    ippsRealToCplx_64f(real_input, imag_input, reinterpret_cast<Ipp64fc*>(output), static_cast<int>(size)));
}

[[nodiscard]] bool complex_conjugate_contiguous(const ksj::base::cf32* input, ksj::base::cf32* output,
                                                const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  if (input == output) {
    return check_status(ippsConj_32fc_I(reinterpret_cast<Ipp32fc*>(output), static_cast<int>(size)));
  }
  return check_status(
    ippsConj_32fc(reinterpret_cast<const Ipp32fc*>(input), reinterpret_cast<Ipp32fc*>(output), static_cast<int>(size)));
}

[[nodiscard]] bool complex_conjugate_contiguous(const ksj::base::cf64* input, ksj::base::cf64* output,
                                                const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  if (input == output) {
    return check_status(ippsConj_64fc_I(reinterpret_cast<Ipp64fc*>(output), static_cast<int>(size)));
  }
  return check_status(
    ippsConj_64fc(reinterpret_cast<const Ipp64fc*>(input), reinterpret_cast<Ipp64fc*>(output), static_cast<int>(size)));
}

[[nodiscard]] bool rectangular_to_polar_contiguous(const ksj::base::cf32* input, ksj::base::f32* magnitude_output,
                                                   ksj::base::f32* phase_output, const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsCartToPolar_32fc(reinterpret_cast<const Ipp32fc*>(input), magnitude_output, phase_output,
                                           static_cast<int>(size)));
}

[[nodiscard]] bool rectangular_to_polar_contiguous(const ksj::base::cf64* input, ksj::base::f64* magnitude_output,
                                                   ksj::base::f64* phase_output, const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsCartToPolar_64fc(reinterpret_cast<const Ipp64fc*>(input), magnitude_output, phase_output,
                                           static_cast<int>(size)));
}

[[nodiscard]] bool polar_to_rectangular_contiguous(const ksj::base::f32* magnitude_input,
                                                   const ksj::base::f32* phase_input, ksj::base::cf32* output,
                                                   const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(
    ippsPolarToCart_32fc(magnitude_input, phase_input, reinterpret_cast<Ipp32fc*>(output), static_cast<int>(size)));
}

[[nodiscard]] bool polar_to_rectangular_contiguous(const ksj::base::f64* magnitude_input,
                                                   const ksj::base::f64* phase_input, ksj::base::cf64* output,
                                                   const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(
    ippsPolarToCart_64fc(magnitude_input, phase_input, reinterpret_cast<Ipp64fc*>(output), static_cast<int>(size)));
}

[[nodiscard]] bool multiply_conjugate_contiguous(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs,
                                                 ksj::base::cf32* output, const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsMulByConj_32fc_A24(reinterpret_cast<const Ipp32fc*>(lhs),
                                             reinterpret_cast<const Ipp32fc*>(rhs), reinterpret_cast<Ipp32fc*>(output),
                                             static_cast<int>(size)));
}

[[nodiscard]] bool multiply_conjugate_contiguous(const ksj::base::cf64* lhs, const ksj::base::cf64* rhs,
                                                 ksj::base::cf64* output, const std::size_t size) {
  if (size == 0U) {
    return true;
  }
  return check_status(ippsMulByConj_64fc_A53(reinterpret_cast<const Ipp64fc*>(lhs),
                                             reinterpret_cast<const Ipp64fc*>(rhs), reinterpret_cast<Ipp64fc*>(output),
                                             static_cast<int>(size)));
}

template <typename T, typename Output>
[[nodiscard]] bool complex_magnitude_impl(VectorView<const T> input, VectorView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_magnitude_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_real_impl(VectorView<const T> input, VectorView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_real_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_real_impl(MatrixView<const T> input, MatrixView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_real_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_real_impl(ImageView<const T> input, ImageView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_real_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_real_impl(CubeView<const T> input, CubeView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_real_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_real_impl(Array4DView<const T> input, Array4DView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_real_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_imag_impl(VectorView<const T> input, VectorView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_imag_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_imag_impl(MatrixView<const T> input, MatrixView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_imag_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_imag_impl(ImageView<const T> input, ImageView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_imag_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_imag_impl(CubeView<const T> input, CubeView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_imag_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_imag_impl(Array4DView<const T> input, Array4DView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_imag_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool split_complex_impl(VectorView<const T> input, VectorView<Output> real_output,
                                      VectorView<Output> imag_output) {
  if (!valid_contiguous_views(input, real_output, imag_output) || outputs_overlap(real_output, imag_output)) {
    return false;
  }
  return split_complex_contiguous(input.data(), real_output.data(), imag_output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool split_complex_impl(MatrixView<const T> input, MatrixView<Output> real_output,
                                      MatrixView<Output> imag_output) {
  if (!valid_contiguous_views(input, real_output, imag_output) || outputs_overlap(real_output, imag_output)) {
    return false;
  }
  return split_complex_contiguous(input.data(), real_output.data(), imag_output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool split_complex_impl(ImageView<const T> input, ImageView<Output> real_output,
                                      ImageView<Output> imag_output) {
  if (!valid_contiguous_views(input, real_output, imag_output) || outputs_overlap(real_output, imag_output)) {
    return false;
  }
  return split_complex_contiguous(input.data(), real_output.data(), imag_output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool split_complex_impl(CubeView<const T> input, CubeView<Output> real_output,
                                      CubeView<Output> imag_output) {
  if (!valid_contiguous_views(input, real_output, imag_output) || outputs_overlap(real_output, imag_output)) {
    return false;
  }
  return split_complex_contiguous(input.data(), real_output.data(), imag_output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool split_complex_impl(Array4DView<const T> input, Array4DView<Output> real_output,
                                      Array4DView<Output> imag_output) {
  if (!valid_contiguous_views(input, real_output, imag_output) || outputs_overlap(real_output, imag_output)) {
    return false;
  }
  return split_complex_contiguous(input.data(), real_output.data(), imag_output.data(), input.size());
}

template <typename Input, typename T>
[[nodiscard]] bool complex_from_real_imag_impl(VectorView<const Input> real_input, VectorView<const Input> imag_input,
                                               VectorView<T> output) {
  if (!valid_contiguous_views(real_input, imag_input, output)) {
    return false;
  }
  return complex_from_real_imag_contiguous(real_input.data(), imag_input.data(), output.data(), output.size());
}

template <typename Input, typename T>
[[nodiscard]] bool complex_from_real_imag_impl(MatrixView<const Input> real_input, MatrixView<const Input> imag_input,
                                               MatrixView<T> output) {
  if (!valid_contiguous_views(real_input, imag_input, output)) {
    return false;
  }
  return complex_from_real_imag_contiguous(real_input.data(), imag_input.data(), output.data(), output.size());
}

template <typename Input, typename T>
[[nodiscard]] bool complex_from_real_imag_impl(ImageView<const Input> real_input, ImageView<const Input> imag_input,
                                               ImageView<T> output) {
  if (!valid_contiguous_views(real_input, imag_input, output)) {
    return false;
  }
  return complex_from_real_imag_contiguous(real_input.data(), imag_input.data(), output.data(), output.size());
}

template <typename Input, typename T>
[[nodiscard]] bool complex_from_real_imag_impl(CubeView<const Input> real_input, CubeView<const Input> imag_input,
                                               CubeView<T> output) {
  if (!valid_contiguous_views(real_input, imag_input, output)) {
    return false;
  }
  return complex_from_real_imag_contiguous(real_input.data(), imag_input.data(), output.data(), output.size());
}

template <typename Input, typename T>
[[nodiscard]] bool complex_from_real_imag_impl(Array4DView<const Input> real_input, Array4DView<const Input> imag_input,
                                               Array4DView<T> output) {
  if (!valid_contiguous_views(real_input, imag_input, output)) {
    return false;
  }
  return complex_from_real_imag_contiguous(real_input.data(), imag_input.data(), output.data(), output.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_magnitude_impl(MatrixView<const T> input, MatrixView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_magnitude_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_magnitude_impl(ImageView<const T> input, ImageView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_magnitude_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_magnitude_impl(CubeView<const T> input, CubeView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_magnitude_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_magnitude_impl(Array4DView<const T> input, Array4DView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_magnitude_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_phase_impl(VectorView<const T> input, VectorView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_phase_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_phase_impl(MatrixView<const T> input, MatrixView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_phase_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_phase_impl(ImageView<const T> input, ImageView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_phase_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_phase_impl(CubeView<const T> input, CubeView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_phase_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool complex_phase_impl(Array4DView<const T> input, Array4DView<Output> output) {
  if (!valid_contiguous_views(input, output)) {
    return false;
  }
  return complex_phase_contiguous(input.data(), output.data(), input.size());
}

template <typename T> [[nodiscard]] bool complex_conjugate_impl(VectorView<const T> input, VectorView<T> output) {
  if (!valid_contiguous_views(input, output) || output_partially_overlaps_input(input, output)) {
    return false;
  }
  return complex_conjugate_contiguous(input.data(), output.data(), input.size());
}

template <typename T> [[nodiscard]] bool complex_conjugate_impl(MatrixView<const T> input, MatrixView<T> output) {
  if (!valid_contiguous_views(input, output) || output_partially_overlaps_input(input, output)) {
    return false;
  }
  return complex_conjugate_contiguous(input.data(), output.data(), input.size());
}

template <typename T> [[nodiscard]] bool complex_conjugate_impl(ImageView<const T> input, ImageView<T> output) {
  if (!valid_contiguous_views(input, output) || output_partially_overlaps_input(input, output)) {
    return false;
  }
  return complex_conjugate_contiguous(input.data(), output.data(), input.size());
}

template <typename T> [[nodiscard]] bool complex_conjugate_impl(CubeView<const T> input, CubeView<T> output) {
  if (!valid_contiguous_views(input, output) || output_partially_overlaps_input(input, output)) {
    return false;
  }
  return complex_conjugate_contiguous(input.data(), output.data(), input.size());
}

template <typename T> [[nodiscard]] bool complex_conjugate_impl(Array4DView<const T> input, Array4DView<T> output) {
  if (!valid_contiguous_views(input, output) || output_partially_overlaps_input(input, output)) {
    return false;
  }
  return complex_conjugate_contiguous(input.data(), output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool rectangular_to_polar_impl(VectorView<const T> input, VectorView<Output> magnitude_output,
                                             VectorView<Output> phase_output) {
  if (!valid_contiguous_views(input, magnitude_output, phase_output) ||
      outputs_overlap(magnitude_output, phase_output)) {
    return false;
  }
  return rectangular_to_polar_contiguous(input.data(), magnitude_output.data(), phase_output.data(), input.size());
}

template <typename T, typename Output>
[[nodiscard]] bool polar_to_rectangular_impl(VectorView<const Output> magnitude_input,
                                             VectorView<const Output> phase_input, VectorView<T> output) {
  if (!valid_contiguous_views(magnitude_input, phase_input, output)) {
    return false;
  }
  return polar_to_rectangular_contiguous(magnitude_input.data(), phase_input.data(), output.data(), output.size());
}

template <typename T>
[[nodiscard]] bool multiply_conjugate_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {
  if (!valid_contiguous_views(lhs, rhs, output) || output_partially_overlaps_input(lhs, output) ||
      output_partially_overlaps_input(rhs, output) || exact_same_data(lhs, output) || exact_same_data(rhs, output)) {
    return false;
  }
  return multiply_conjugate_contiguous(lhs.data(), rhs.data(), output.data(), output.size());
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

bool split_complex(MatrixView<const ksj::base::cf32> input, MatrixView<ksj::base::f32> real_output,
                   MatrixView<ksj::base::f32> imag_output) {
  return split_complex_impl(input, real_output, imag_output);
}

bool split_complex(MatrixView<const ksj::base::cf64> input, MatrixView<ksj::base::f64> real_output,
                   MatrixView<ksj::base::f64> imag_output) {
  return split_complex_impl(input, real_output, imag_output);
}

bool split_complex(ImageView<const ksj::base::cf32> input, ImageView<ksj::base::f32> real_output,
                   ImageView<ksj::base::f32> imag_output) {
  return split_complex_impl(input, real_output, imag_output);
}

bool split_complex(ImageView<const ksj::base::cf64> input, ImageView<ksj::base::f64> real_output,
                   ImageView<ksj::base::f64> imag_output) {
  return split_complex_impl(input, real_output, imag_output);
}

bool split_complex(CubeView<const ksj::base::cf32> input, CubeView<ksj::base::f32> real_output,
                   CubeView<ksj::base::f32> imag_output) {
  return split_complex_impl(input, real_output, imag_output);
}

bool split_complex(CubeView<const ksj::base::cf64> input, CubeView<ksj::base::f64> real_output,
                   CubeView<ksj::base::f64> imag_output) {
  return split_complex_impl(input, real_output, imag_output);
}

bool split_complex(Array4DView<const ksj::base::cf32> input, Array4DView<ksj::base::f32> real_output,
                   Array4DView<ksj::base::f32> imag_output) {
  return split_complex_impl(input, real_output, imag_output);
}

bool split_complex(Array4DView<const ksj::base::cf64> input, Array4DView<ksj::base::f64> real_output,
                   Array4DView<ksj::base::f64> imag_output) {
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

bool complex_from_real_imag(MatrixView<const ksj::base::f32> real_input, MatrixView<const ksj::base::f32> imag_input,
                            MatrixView<ksj::base::cf32> output) {
  return complex_from_real_imag_impl(real_input, imag_input, output);
}

bool complex_from_real_imag(MatrixView<const ksj::base::f64> real_input, MatrixView<const ksj::base::f64> imag_input,
                            MatrixView<ksj::base::cf64> output) {
  return complex_from_real_imag_impl(real_input, imag_input, output);
}

bool complex_from_real_imag(ImageView<const ksj::base::f32> real_input, ImageView<const ksj::base::f32> imag_input,
                            ImageView<ksj::base::cf32> output) {
  return complex_from_real_imag_impl(real_input, imag_input, output);
}

bool complex_from_real_imag(ImageView<const ksj::base::f64> real_input, ImageView<const ksj::base::f64> imag_input,
                            ImageView<ksj::base::cf64> output) {
  return complex_from_real_imag_impl(real_input, imag_input, output);
}

bool complex_from_real_imag(CubeView<const ksj::base::f32> real_input, CubeView<const ksj::base::f32> imag_input,
                            CubeView<ksj::base::cf32> output) {
  return complex_from_real_imag_impl(real_input, imag_input, output);
}

bool complex_from_real_imag(CubeView<const ksj::base::f64> real_input, CubeView<const ksj::base::f64> imag_input,
                            CubeView<ksj::base::cf64> output) {
  return complex_from_real_imag_impl(real_input, imag_input, output);
}

bool complex_from_real_imag(Array4DView<const ksj::base::f32> real_input, Array4DView<const ksj::base::f32> imag_input,
                            Array4DView<ksj::base::cf32> output) {
  return complex_from_real_imag_impl(real_input, imag_input, output);
}

bool complex_from_real_imag(Array4DView<const ksj::base::f64> real_input, Array4DView<const ksj::base::f64> imag_input,
                            Array4DView<ksj::base::cf64> output) {
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

bool rectangular_to_polar(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> magnitude_output,
                          VectorView<ksj::base::f32> phase_output) {
  return rectangular_to_polar_impl(input, magnitude_output, phase_output);
}

bool rectangular_to_polar(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> magnitude_output,
                          VectorView<ksj::base::f64> phase_output) {
  return rectangular_to_polar_impl(input, magnitude_output, phase_output);
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

} // namespace ksj::array::detail::intel
