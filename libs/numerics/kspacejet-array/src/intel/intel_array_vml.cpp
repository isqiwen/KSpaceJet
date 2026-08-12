#include "kspacejet/array/detail/intel/intel_array_vml.hpp"

#include <mkl_vml.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace ksj::array::detail::intel::vml {
namespace {

static_assert(sizeof(ksj::base::cf32) == sizeof(MKL_Complex8));
static_assert(sizeof(ksj::base::cf64) == sizeof(MKL_Complex16));
static_assert(alignof(ksj::base::cf32) == alignof(MKL_Complex8));
static_assert(alignof(ksj::base::cf64) == alignof(MKL_Complex16));
static_assert(std::is_trivially_copyable_v<ksj::base::cf32>);
static_assert(std::is_trivially_copyable_v<ksj::base::cf64>);
static_assert(std::is_trivially_copyable_v<MKL_Complex8>);
static_assert(std::is_trivially_copyable_v<MKL_Complex16>);

constexpr MKL_INT64 kVmlMode = VML_EP | VML_FTZDAZ_ON | VML_ERRMODE_STDERR;
constexpr MKL_INT64 kVmlAccurateMode = VML_HA | VML_FTZDAZ_ON | VML_ERRMODE_STDERR;

[[nodiscard]] bool fits_mkl_length(const std::size_t size) noexcept {
  return size <= static_cast<std::size_t>(std::numeric_limits<MKL_INT>::max());
}

template <typename T> [[nodiscard]] bool valid_contiguous_view(VectorView<T> view) noexcept {
  return view.is_contiguous() && fits_mkl_length(view.size());
}

template <typename T> [[nodiscard]] std::uintptr_t address(T* pointer) noexcept {
  return reinterpret_cast<std::uintptr_t>(pointer);
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool output_overlaps_input(VectorView<const InputT> input, VectorView<OutputT> output) noexcept {
  if (input.empty()) {
    return false;
  }
  const auto input_begin = address(input.data());
  const auto input_end = input_begin + input.size() * sizeof(InputT);
  const auto output_begin = address(output.data());
  const auto output_end = output_begin + output.size() * sizeof(OutputT);
  return input_begin < output_end && output_begin < input_end;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool valid_non_overlapping_unary(VectorView<const InputT> input, VectorView<OutputT> output) noexcept {
  if (input.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(input) || !valid_contiguous_view(output)) {
    return false;
  }
  return !output_overlaps_input(input, output);
}

template <typename T>
[[nodiscard]] bool valid_non_overlapping_binary(VectorView<const T> lhs, VectorView<const T> rhs,
                                                VectorView<T> output) noexcept {
  if (lhs.size() != rhs.size() || lhs.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(lhs) || !valid_contiguous_view(rhs) || !valid_contiguous_view(output)) {
    return false;
  }
  return !output_overlaps_input(lhs, output) && !output_overlaps_input(rhs, output);
}

template <typename T, typename Function>
[[nodiscard]] bool real_unary_impl(VectorView<const T> input, VectorView<T> output, Function function,
                                   const MKL_INT64 mode = kVmlMode) {
  if (!valid_non_overlapping_unary(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  function(static_cast<MKL_INT>(input.size()), input.data(), output.data(), mode);
  return true;
}

template <typename T, typename Function>
[[nodiscard]] bool real_binary_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output,
                                    Function function, const MKL_INT64 mode = kVmlMode) {
  if (!valid_non_overlapping_binary(lhs, rhs, output)) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }
  function(static_cast<MKL_INT>(lhs.size()), lhs.data(), rhs.data(), output.data(), mode);
  return true;
}

} // namespace

bool add(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
         VectorView<ksj::base::f32> output) {
  return real_binary_impl(lhs, rhs, output, vmsAdd, kVmlAccurateMode);
}

bool add(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
         VectorView<ksj::base::f64> output) {
  return real_binary_impl(lhs, rhs, output, vmdAdd, kVmlAccurateMode);
}

bool subtract(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
              VectorView<ksj::base::f32> output) {
  return real_binary_impl(lhs, rhs, output, vmsSub, kVmlAccurateMode);
}

bool subtract(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
              VectorView<ksj::base::f64> output) {
  return real_binary_impl(lhs, rhs, output, vmdSub, kVmlAccurateMode);
}

bool multiply(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
              VectorView<ksj::base::f32> output) {
  return real_binary_impl(lhs, rhs, output, vmsMul, kVmlAccurateMode);
}

bool multiply(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
              VectorView<ksj::base::f64> output) {
  return real_binary_impl(lhs, rhs, output, vmdMul, kVmlAccurateMode);
}

bool divide(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
            VectorView<ksj::base::f32> output) {
  return real_binary_impl(lhs, rhs, output, vmsDiv, kVmlAccurateMode);
}

bool divide(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
            VectorView<ksj::base::f64> output) {
  return real_binary_impl(lhs, rhs, output, vmdDiv, kVmlAccurateMode);
}

bool absolute(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return real_unary_impl(input, output, vmsAbs, kVmlAccurateMode);
}

bool absolute(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return real_unary_impl(input, output, vmdAbs, kVmlAccurateMode);
}

bool sqrt(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return real_unary_impl(input, output, vmsSqrt);
}

bool sqrt(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return real_unary_impl(input, output, vmdSqrt);
}

bool inverse(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return real_unary_impl(input, output, vmsInv, kVmlAccurateMode);
}

bool inverse(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return real_unary_impl(input, output, vmdInv, kVmlAccurateMode);
}

bool inverse_sqrt(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return real_unary_impl(input, output, vmsInvSqrt, kVmlAccurateMode);
}

bool inverse_sqrt(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return real_unary_impl(input, output, vmdInvSqrt, kVmlAccurateMode);
}

bool exp(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return real_unary_impl(input, output, vmsExp);
}

bool exp(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return real_unary_impl(input, output, vmdExp);
}

bool log(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return real_unary_impl(input, output, vmsLn);
}

bool log(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return real_unary_impl(input, output, vmdLn);
}

bool minimum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
             VectorView<ksj::base::f32> output) {
  return real_binary_impl(lhs, rhs, output, vmsFmin, kVmlAccurateMode);
}

bool minimum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
             VectorView<ksj::base::f64> output) {
  return real_binary_impl(lhs, rhs, output, vmdFmin, kVmlAccurateMode);
}

bool maximum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
             VectorView<ksj::base::f32> output) {
  return real_binary_impl(lhs, rhs, output, vmsFmax, kVmlAccurateMode);
}

bool maximum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
             VectorView<ksj::base::f64> output) {
  return real_binary_impl(lhs, rhs, output, vmdFmax, kVmlAccurateMode);
}

bool hypot(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
           VectorView<ksj::base::f32> output) {
  return real_binary_impl(lhs, rhs, output, vmsHypot, kVmlAccurateMode);
}

bool hypot(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
           VectorView<ksj::base::f64> output) {
  return real_binary_impl(lhs, rhs, output, vmdHypot, kVmlAccurateMode);
}

bool absolute(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output) {
  if (!valid_non_overlapping_unary(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  vmcAbs(static_cast<MKL_INT>(input.size()), reinterpret_cast<const MKL_Complex8*>(input.data()), output.data(),
         kVmlMode);
  return true;
}

bool absolute(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output) {
  if (!valid_non_overlapping_unary(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  vmzAbs(static_cast<MKL_INT>(input.size()), reinterpret_cast<const MKL_Complex16*>(input.data()), output.data(),
         kVmlMode);
  return true;
}

bool complex_conjugate(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::cf32> output) {
  if (!valid_non_overlapping_unary(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  vmcConj(static_cast<MKL_INT>(input.size()), reinterpret_cast<const MKL_Complex8*>(input.data()),
          reinterpret_cast<MKL_Complex8*>(output.data()), kVmlMode);
  return true;
}

bool complex_conjugate(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::cf64> output) {
  if (!valid_non_overlapping_unary(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  vmzConj(static_cast<MKL_INT>(input.size()), reinterpret_cast<const MKL_Complex16*>(input.data()),
          reinterpret_cast<MKL_Complex16*>(output.data()), kVmlMode);
  return true;
}

bool multiply(VectorView<const ksj::base::cf32> lhs, VectorView<const ksj::base::cf32> rhs,
              VectorView<ksj::base::cf32> output) {
  if (!valid_non_overlapping_binary(lhs, rhs, output)) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }
  vmcMul(static_cast<MKL_INT>(lhs.size()), reinterpret_cast<const MKL_Complex8*>(lhs.data()),
         reinterpret_cast<const MKL_Complex8*>(rhs.data()), reinterpret_cast<MKL_Complex8*>(output.data()), kVmlMode);
  return true;
}

bool multiply(VectorView<const ksj::base::cf64> lhs, VectorView<const ksj::base::cf64> rhs,
              VectorView<ksj::base::cf64> output) {
  if (!valid_non_overlapping_binary(lhs, rhs, output)) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }
  vmzMul(static_cast<MKL_INT>(lhs.size()), reinterpret_cast<const MKL_Complex16*>(lhs.data()),
         reinterpret_cast<const MKL_Complex16*>(rhs.data()), reinterpret_cast<MKL_Complex16*>(output.data()), kVmlMode);
  return true;
}

bool divide(VectorView<const ksj::base::cf32> lhs, VectorView<const ksj::base::cf32> rhs,
            VectorView<ksj::base::cf32> output) {
  if (!valid_non_overlapping_binary(lhs, rhs, output)) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }
  vmcDiv(static_cast<MKL_INT>(lhs.size()), reinterpret_cast<const MKL_Complex8*>(lhs.data()),
         reinterpret_cast<const MKL_Complex8*>(rhs.data()), reinterpret_cast<MKL_Complex8*>(output.data()), kVmlMode);
  return true;
}

bool divide(VectorView<const ksj::base::cf64> lhs, VectorView<const ksj::base::cf64> rhs,
            VectorView<ksj::base::cf64> output) {
  if (!valid_non_overlapping_binary(lhs, rhs, output)) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }
  vmzDiv(static_cast<MKL_INT>(lhs.size()), reinterpret_cast<const MKL_Complex16*>(lhs.data()),
         reinterpret_cast<const MKL_Complex16*>(rhs.data()), reinterpret_cast<MKL_Complex16*>(output.data()), kVmlMode);
  return true;
}

} // namespace ksj::array::detail::intel::vml
