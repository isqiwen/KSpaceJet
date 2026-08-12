#include "kspacejet/array/detail/intel/intel_array_elementwise.hpp"

#include <ipp.h>

#include <cstdint>
#include <limits>
#include <type_traits>

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

template <typename T> [[nodiscard]] bool valid_contiguous_view(VectorView<T> view) noexcept {
  return view.is_contiguous() && fits_ipp_length(view.size());
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
[[nodiscard]] bool exact_same_vector_storage(VectorView<const InputT> input, VectorView<OutputT> output) noexcept {
  return input.data() == output.data() && input.size() == output.size();
}

template <typename T>
[[nodiscard]] bool valid_non_overlapping_binary(VectorView<const T> lhs, VectorView<const T> rhs,
                                                VectorView<T> output) {
  if (lhs.size() != rhs.size() || lhs.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(lhs) || !valid_contiguous_view(rhs) || !valid_contiguous_view(output)) {
    return false;
  }
  return !output_overlaps_input(lhs, output) && !output_overlaps_input(rhs, output);
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool valid_non_overlapping_unary(VectorView<const InputT> input, VectorView<OutputT> output) {
  if (input.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(input) || !valid_contiguous_view(output)) {
    return false;
  }
  return !output_overlaps_input(input, output);
}

[[nodiscard]] Ipp32fc to_ipp(const ksj::base::cf32 value) noexcept {
  return {value.real(), value.imag()};
}

[[nodiscard]] Ipp64fc to_ipp(const ksj::base::cf64 value) noexcept {
  return {value.real(), value.imag()};
}

template <typename T, typename IppT, typename Fn>
[[nodiscard]] bool binary_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output, Fn fn,
                               const bool reversed_args = false) {
  if (!valid_non_overlapping_binary(lhs, rhs, output)) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }
  const auto* left = reinterpret_cast<const IppT*>(lhs.data());
  const auto* right = reinterpret_cast<const IppT*>(rhs.data());
  auto* out = reinterpret_cast<IppT*>(output.data());
  if constexpr (std::is_same_v<T, ksj::base::f32> || std::is_same_v<T, ksj::base::f64>) {
    if (reversed_args) {
      return check_status(fn(rhs.data(), lhs.data(), output.data(), static_cast<int>(lhs.size())));
    }
    return check_status(fn(lhs.data(), rhs.data(), output.data(), static_cast<int>(lhs.size())));
  } else {
    if (reversed_args) {
      return check_status(fn(right, left, out, static_cast<int>(lhs.size())));
    }
    return check_status(fn(left, right, out, static_cast<int>(lhs.size())));
  }
}

template <typename T, typename IppT, typename Fn>
[[nodiscard]] bool scalar_impl(VectorView<const T> input, const T scalar, VectorView<T> output, Fn fn) {
  if (!valid_non_overlapping_unary(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  if constexpr (std::is_same_v<T, ksj::base::f32> || std::is_same_v<T, ksj::base::f64>) {
    return check_status(fn(input.data(), scalar, output.data(), static_cast<int>(input.size())));
  } else {
    return check_status(fn(reinterpret_cast<const IppT*>(input.data()), to_ipp(scalar),
                           reinterpret_cast<IppT*>(output.data()), static_cast<int>(input.size())));
  }
}

template <typename T, typename IppT, typename Fn, typename InPlaceFn>
[[nodiscard]] bool multiply_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output, Fn fn,
                                 InPlaceFn in_place_fn) {
  if (lhs.size() != rhs.size() || lhs.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(lhs) || !valid_contiguous_view(rhs) || !valid_contiguous_view(output)) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }

  const auto output_is_lhs = exact_same_vector_storage(lhs, output);
  const auto output_is_rhs = exact_same_vector_storage(rhs, output);
  if (output_is_lhs && output_is_rhs) {
    return false;
  }
  if ((output_overlaps_input(lhs, output) && !output_is_lhs) ||
      (output_overlaps_input(rhs, output) && !output_is_rhs)) {
    return false;
  }

  if constexpr (std::is_same_v<T, ksj::base::f32> || std::is_same_v<T, ksj::base::f64>) {
    if (output_is_lhs) {
      return check_status(in_place_fn(rhs.data(), output.data(), static_cast<int>(lhs.size())));
    }
    if (output_is_rhs) {
      return check_status(in_place_fn(lhs.data(), output.data(), static_cast<int>(lhs.size())));
    }
    return check_status(fn(lhs.data(), rhs.data(), output.data(), static_cast<int>(lhs.size())));
  } else {
    const auto* left = reinterpret_cast<const IppT*>(lhs.data());
    const auto* right = reinterpret_cast<const IppT*>(rhs.data());
    auto* out = reinterpret_cast<IppT*>(output.data());
    if (output_is_lhs) {
      return check_status(in_place_fn(right, out, static_cast<int>(lhs.size())));
    }
    if (output_is_rhs) {
      return check_status(in_place_fn(left, out, static_cast<int>(lhs.size())));
    }
    return check_status(fn(left, right, out, static_cast<int>(lhs.size())));
  }
}

template <typename T, typename IppT, typename Fn>
[[nodiscard]] bool multiply_accumulate_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output,
                                            Fn fn) {
  if (!valid_non_overlapping_binary(lhs, rhs, output)) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }

  if constexpr (std::is_same_v<T, ksj::base::f32> || std::is_same_v<T, ksj::base::f64>) {
    return check_status(fn(lhs.data(), rhs.data(), output.data(), static_cast<int>(lhs.size())));
  } else {
    return check_status(fn(reinterpret_cast<const IppT*>(lhs.data()), reinterpret_cast<const IppT*>(rhs.data()),
                           reinterpret_cast<IppT*>(output.data()), static_cast<int>(lhs.size())));
  }
}

template <typename InputT, typename OutputT, typename IppInputT, typename IppOutputT, typename Fn>
[[nodiscard]] bool unary_impl(VectorView<const InputT> input, VectorView<OutputT> output, Fn fn) {
  if (!valid_non_overlapping_unary(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  return check_status(fn(reinterpret_cast<const IppInputT*>(input.data()), reinterpret_cast<IppOutputT*>(output.data()),
                         static_cast<int>(input.size())));
}

} // namespace

#define KSJ_ARRAY_INTEL_REAL_DEFS(T, IPP_T, SUFFIX)                                                                    \
  bool add(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                                   \
    return binary_impl<T, IPP_T>(lhs, rhs, output, ippsAdd_##SUFFIX);                                                  \
  }                                                                                                                    \
  bool subtract(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                              \
    return binary_impl<T, IPP_T>(lhs, rhs, output, ippsSub_##SUFFIX, true);                                            \
  }                                                                                                                    \
  bool multiply(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                              \
    return multiply_impl<T, IPP_T>(lhs, rhs, output, ippsMul_##SUFFIX, ippsMul_##SUFFIX##_I);                          \
  }                                                                                                                    \
  bool multiply_accumulate(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                   \
    return multiply_accumulate_impl<T, IPP_T>(lhs, rhs, output, ippsAddProduct_##SUFFIX);                              \
  }                                                                                                                    \
  bool divide(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                                \
    return binary_impl<T, IPP_T>(lhs, rhs, output, ippsDiv_##SUFFIX, true);                                            \
  }                                                                                                                    \
  bool add_scalar(VectorView<const T> input, T scalar, VectorView<T> output) {                                         \
    return scalar_impl<T, IPP_T>(input, scalar, output, ippsAddC_##SUFFIX);                                            \
  }                                                                                                                    \
  bool subtract_scalar(VectorView<const T> input, T scalar, VectorView<T> output) {                                    \
    return scalar_impl<T, IPP_T>(input, scalar, output, ippsSubC_##SUFFIX);                                            \
  }                                                                                                                    \
  bool scalar_subtract(VectorView<const T> input, T scalar, VectorView<T> output) {                                    \
    return scalar_impl<T, IPP_T>(input, scalar, output, ippsSubCRev_##SUFFIX);                                         \
  }                                                                                                                    \
  bool scale(VectorView<const T> input, T scalar, VectorView<T> output) {                                              \
    return scalar_impl<T, IPP_T>(input, scalar, output, ippsMulC_##SUFFIX);                                            \
  }                                                                                                                    \
  bool divide_scalar(VectorView<const T> input, T scalar, VectorView<T> output) {                                      \
    return scalar_impl<T, IPP_T>(input, scalar, output, ippsDivC_##SUFFIX);                                            \
  }                                                                                                                    \
  bool negate(VectorView<const T> input, VectorView<T> output) {                                                       \
    return scalar_impl<T, IPP_T>(input, T{-1}, output, ippsMulC_##SUFFIX);                                             \
  }                                                                                                                    \
  bool square(VectorView<const T> input, VectorView<T> output) {                                                       \
    return unary_impl<T, T, IPP_T, IPP_T>(input, output, ippsSqr_##SUFFIX);                                            \
  }                                                                                                                    \
  bool sqrt(VectorView<const T> input, VectorView<T> output) {                                                         \
    return unary_impl<T, T, IPP_T, IPP_T>(input, output, ippsSqrt_##SUFFIX);                                           \
  }

KSJ_ARRAY_INTEL_REAL_DEFS(ksj::base::f32, Ipp32f, 32f)
KSJ_ARRAY_INTEL_REAL_DEFS(ksj::base::f64, Ipp64f, 64f)
KSJ_ARRAY_INTEL_REAL_DEFS(ksj::base::cf32, Ipp32fc, 32fc)
KSJ_ARRAY_INTEL_REAL_DEFS(ksj::base::cf64, Ipp64fc, 64fc)

#undef KSJ_ARRAY_INTEL_REAL_DEFS

bool scalar_divide(VectorView<const ksj::base::f32> input, ksj::base::f32 scalar, VectorView<ksj::base::f32> output) {
  return scalar_impl<ksj::base::f32, Ipp32f>(input, scalar, output, ippsDivCRev_32f);
}

bool scalar_divide(VectorView<const ksj::base::f64>, ksj::base::f64, VectorView<ksj::base::f64>) {
  return false;
}

bool scalar_divide(VectorView<const ksj::base::cf32>, ksj::base::cf32, VectorView<ksj::base::cf32>) {
  return false;
}

bool scalar_divide(VectorView<const ksj::base::cf64>, ksj::base::cf64, VectorView<ksj::base::cf64>) {
  return false;
}

bool absolute(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return unary_impl<ksj::base::f32, ksj::base::f32, Ipp32f, Ipp32f>(input, output, ippsAbs_32f);
}

bool absolute(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return unary_impl<ksj::base::f64, ksj::base::f64, Ipp64f, Ipp64f>(input, output, ippsAbs_64f);
}

bool absolute(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output) {
  return unary_impl<ksj::base::cf32, ksj::base::f32, Ipp32fc, Ipp32f>(input, output, ippsMagnitude_32fc);
}

bool absolute(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output) {
  return unary_impl<ksj::base::cf64, ksj::base::f64, Ipp64fc, Ipp64f>(input, output, ippsMagnitude_64fc);
}

bool exp(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return unary_impl<ksj::base::f32, ksj::base::f32, Ipp32f, Ipp32f>(input, output, ippsExp_32f);
}

bool exp(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return unary_impl<ksj::base::f64, ksj::base::f64, Ipp64f, Ipp64f>(input, output, ippsExp_64f);
}

bool log(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return unary_impl<ksj::base::f32, ksj::base::f32, Ipp32f, Ipp32f>(input, output, ippsLn_32f);
}

bool log(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return unary_impl<ksj::base::f64, ksj::base::f64, Ipp64f, Ipp64f>(input, output, ippsLn_64f);
}

bool minimum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
             VectorView<ksj::base::f32> output) {
  return binary_impl<ksj::base::f32, Ipp32f>(
    lhs, rhs, output, [](const Ipp32f* left, const Ipp32f* right, Ipp32f* out, const int length) {
      return ippsMinEvery_32f(left, right, out, static_cast<Ipp32u>(length));
    });
}

bool minimum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
             VectorView<ksj::base::f64> output) {
  return binary_impl<ksj::base::f64, Ipp64f>(
    lhs, rhs, output, [](const Ipp64f* left, const Ipp64f* right, Ipp64f* out, const int length) {
      return ippsMinEvery_64f(left, right, out, static_cast<Ipp32u>(length));
    });
}

bool maximum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
             VectorView<ksj::base::f32> output) {
  return binary_impl<ksj::base::f32, Ipp32f>(
    lhs, rhs, output, [](const Ipp32f* left, const Ipp32f* right, Ipp32f* out, const int length) {
      return ippsMaxEvery_32f(left, right, out, static_cast<Ipp32u>(length));
    });
}

bool maximum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
             VectorView<ksj::base::f64> output) {
  return binary_impl<ksj::base::f64, Ipp64f>(
    lhs, rhs, output, [](const Ipp64f* left, const Ipp64f* right, Ipp64f* out, const int length) {
      return ippsMaxEvery_64f(left, right, out, static_cast<Ipp32u>(length));
    });
}

bool clamp(VectorView<const ksj::base::f32> input, const ksj::base::f32 lower, const ksj::base::f32 upper,
           VectorView<ksj::base::f32> output) {
  if (!valid_non_overlapping_unary(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  return check_status(ippsThreshold_LTValGTVal_32f(input.data(), output.data(), static_cast<int>(input.size()), lower,
                                                   lower, upper, upper));
}

bool clamp(VectorView<const ksj::base::f64> input, const ksj::base::f64 lower, const ksj::base::f64 upper,
           VectorView<ksj::base::f64> output) {
  if (!valid_non_overlapping_unary(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  return check_status(ippsThreshold_LTValGTVal_64f(input.data(), output.data(), static_cast<int>(input.size()), lower,
                                                   lower, upper, upper));
}

bool bitwise_and(VectorView<const ksj::base::u32> lhs, VectorView<const ksj::base::u32> rhs,
                 VectorView<ksj::base::u32> output) {
  if (lhs.size() != rhs.size() || lhs.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(lhs) || !valid_contiguous_view(rhs) || !valid_contiguous_view(output)) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }

  const auto output_is_lhs = exact_same_vector_storage(lhs, output);
  const auto output_is_rhs = exact_same_vector_storage(rhs, output);
  if (output_is_lhs && output_is_rhs) {
    return true;
  }
  if ((output_overlaps_input(lhs, output) && !output_is_lhs) ||
      (output_overlaps_input(rhs, output) && !output_is_rhs)) {
    return false;
  }

  if (output_is_lhs) {
    return check_status(ippsAnd_32u_I(rhs.data(), output.data(), static_cast<int>(lhs.size())));
  }
  if (output_is_rhs) {
    return check_status(ippsAnd_32u_I(lhs.data(), output.data(), static_cast<int>(lhs.size())));
  }
  return false;
}

bool bitwise_not(VectorView<const ksj::base::u32> input, VectorView<ksj::base::u32> output) {
  if (input.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(input) || !valid_contiguous_view(output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }

  const auto output_is_input = exact_same_vector_storage(input, output);
  if (output_overlaps_input(input, output) && !output_is_input) {
    return false;
  }
  if (output_is_input) {
    return check_status(ippsNot_32u_I(output.data(), static_cast<int>(input.size())));
  }
  return false;
}

} // namespace ksj::array::detail::intel
