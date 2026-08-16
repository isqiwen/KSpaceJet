// SPDX-License-Identifier: Apache-2.0

#include "support/calibration_payload.hpp"

#include <cstring>

namespace ksj::calibration::support {

Complex read_complex_float32(const void* const data, const std::size_t element_index) noexcept {
  Complex value{};
  std::memcpy(&value, static_cast<const std::byte*>(data) + element_index * sizeof(value), sizeof(value));
  return value;
}

void write_complex_float32(void* const data, const std::size_t element_index, const Complex value) noexcept {
  std::memcpy(static_cast<std::byte*>(data) + element_index * sizeof(value), &value, sizeof(value));
}

} // namespace ksj::calibration::support
