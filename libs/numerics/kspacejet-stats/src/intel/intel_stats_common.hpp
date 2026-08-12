#pragma once

#include "kspacejet/array/array.hpp"

#include <ipp.h>

#include <cstddef>
#include <limits>

namespace ksj::stats::detail::intel::impl {

[[nodiscard]] inline bool check_status(const IppStatus status) noexcept {
  return status == ippStsNoErr;
}

[[nodiscard]] inline bool fits_ipp_length(const std::size_t size) noexcept {
  return size <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

template <typename T> [[nodiscard]] bool is_ipp_compatible(ksj::array::VectorView<const T> input) noexcept {
  return input.is_contiguous() && fits_ipp_length(input.size());
}

} // namespace ksj::stats::detail::intel::impl
