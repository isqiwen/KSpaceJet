#include "kspacejet/array/detail/intel/intel_array_reductions.hpp"

#include <ipp.h>

#include <limits>

namespace ksj::array::detail::intel {
namespace {

[[nodiscard]] bool check_status(const IppStatus status) noexcept {
  return status == ippStsNoErr;
}

[[nodiscard]] bool fits_ipp_length(const std::size_t size) noexcept {
  return size <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

template <typename T> [[nodiscard]] bool valid_contiguous_view(VectorView<T> view) noexcept {
  return view.is_contiguous() && fits_ipp_length(view.size());
}

} // namespace

bool sum(VectorView<const ksj::base::f32> input, ksj::base::f32& output) {
  if (!valid_contiguous_view(input)) {
    return false;
  }
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  return check_status(ippsSum_32f(input.data(), static_cast<int>(input.size()), &output, ippAlgHintAccurate));
}

bool sum(VectorView<const ksj::base::f64> input, ksj::base::f64& output) {
  if (!valid_contiguous_view(input)) {
    return false;
  }
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  return check_status(ippsSum_64f(input.data(), static_cast<int>(input.size()), &output));
}

bool min(VectorView<const ksj::base::f32> input, ksj::base::f32& output) {
  if (input.empty() || !valid_contiguous_view(input)) {
    return false;
  }
  return check_status(ippsMin_32f(input.data(), static_cast<int>(input.size()), &output));
}

bool min(VectorView<const ksj::base::f64> input, ksj::base::f64& output) {
  if (input.empty() || !valid_contiguous_view(input)) {
    return false;
  }
  return check_status(ippsMin_64f(input.data(), static_cast<int>(input.size()), &output));
}

bool max(VectorView<const ksj::base::f32> input, ksj::base::f32& output) {
  if (input.empty() || !valid_contiguous_view(input)) {
    return false;
  }
  return check_status(ippsMax_32f(input.data(), static_cast<int>(input.size()), &output));
}

bool max(VectorView<const ksj::base::f64> input, ksj::base::f64& output) {
  if (input.empty() || !valid_contiguous_view(input)) {
    return false;
  }
  return check_status(ippsMax_64f(input.data(), static_cast<int>(input.size()), &output));
}

} // namespace ksj::array::detail::intel
