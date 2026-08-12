#pragma once

#include <ipp.h>

#include <cstddef>
#include <limits>

namespace ksj::signal::detail::intel::impl {

[[nodiscard]] inline bool check_status(const IppStatus status) noexcept {
  return status == ippStsNoErr;
}

[[nodiscard]] inline bool fits_ipp_length(const std::size_t size) noexcept {
  return size <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

[[nodiscard]] inline bool fits_ipp_step(const std::size_t bytes) noexcept {
  return bytes <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

[[nodiscard]] inline bool fits_ipp_size(const std::size_t rows, const std::size_t cols) noexcept {
  return fits_ipp_length(rows) && fits_ipp_length(cols);
}

[[nodiscard]] inline IppiSize ipp_size(const std::size_t rows, const std::size_t cols) noexcept {
  return IppiSize{static_cast<int>(cols), static_cast<int>(rows)};
}

} // namespace ksj::signal::detail::intel::impl
