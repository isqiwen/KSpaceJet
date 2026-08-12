#include "kspacejet/array/detail/intel/intel_array_storage.hpp"

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

template <typename T> [[nodiscard]] bool contiguous_ranges_overlap(VectorView<const T> input, VectorView<T> output) {
  if (input.empty()) {
    return false;
  }
  const auto input_begin = address(input.data());
  const auto input_end = input_begin + input.size() * sizeof(T);
  const auto output_begin = address(output.data());
  const auto output_end = output_begin + output.size() * sizeof(T);
  return input_begin < output_end && output_begin < input_end;
}

[[nodiscard]] Ipp32fc to_ipp(const ksj::base::cf32 value) noexcept {
  return {value.real(), value.imag()};
}

[[nodiscard]] Ipp64fc to_ipp(const ksj::base::cf64 value) noexcept {
  return {value.real(), value.imag()};
}

template <typename T, typename IppT, typename SetFn>
[[nodiscard]] bool fill_impl(VectorView<T> output, const T value, SetFn set_fn) {
  if (!valid_contiguous_view(output)) {
    return false;
  }
  if (output.empty()) {
    return true;
  }
  if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return check_status(set_fn(to_ipp(value), reinterpret_cast<IppT*>(output.data()), static_cast<int>(output.size())));
  } else {
    return check_status(set_fn(value, output.data(), static_cast<int>(output.size())));
  }
}

template <typename T, typename IppT, typename CopyFn>
[[nodiscard]] bool copy_impl(VectorView<const T> input, VectorView<T> output, CopyFn copy_fn) {
  if (input.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(input) || !valid_contiguous_view(output)) {
    return false;
  }
  if (input.data() == output.data() || input.empty()) {
    return true;
  }
  if (contiguous_ranges_overlap(input, output)) {
    return false;
  }
  if constexpr (std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>) {
    return check_status(copy_fn(reinterpret_cast<const IppT*>(input.data()), reinterpret_cast<IppT*>(output.data()),
                                static_cast<int>(input.size())));
  } else {
    return check_status(copy_fn(input.data(), output.data(), static_cast<int>(input.size())));
  }
}

} // namespace

bool fill(VectorView<ksj::base::f32> output, const ksj::base::f32 value) {
  return fill_impl<ksj::base::f32, Ipp32f>(output, value, ippsSet_32f);
}

bool fill(VectorView<ksj::base::f64> output, const ksj::base::f64 value) {
  return fill_impl<ksj::base::f64, Ipp64f>(output, value, ippsSet_64f);
}

bool fill(VectorView<ksj::base::cf32> output, const ksj::base::cf32 value) {
  return fill_impl<ksj::base::cf32, Ipp32fc>(output, value, ippsSet_32fc);
}

bool fill(VectorView<ksj::base::cf64> output, const ksj::base::cf64 value) {
  return fill_impl<ksj::base::cf64, Ipp64fc>(output, value, ippsSet_64fc);
}

bool copy(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return copy_impl<ksj::base::f32, Ipp32f>(input, output, ippsCopy_32f);
}

bool copy(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return copy_impl<ksj::base::f64, Ipp64f>(input, output, ippsCopy_64f);
}

bool copy(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::cf32> output) {
  return copy_impl<ksj::base::cf32, Ipp32fc>(input, output, ippsCopy_32fc);
}

bool copy(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::cf64> output) {
  return copy_impl<ksj::base::cf64, Ipp64fc>(input, output, ippsCopy_64fc);
}

} // namespace ksj::array::detail::intel
