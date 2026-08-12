#pragma once

#include "kspacejet/recon/result.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ksj::recon::detail {

// JSON Schema measures string `minLength` and `maxLength` in Unicode code
// points, not UTF-8 code units.  Public artifact constructors therefore use
// this strict validator before applying schema-aligned string bounds.
[[nodiscard]] inline Result<std::size_t> utf8_code_point_count(const std::string_view value,
                                                               const std::string_view field_name) {
  std::size_t count = 0U;
  for (std::size_t index = 0U; index < value.size();) {
    const auto byte = static_cast<std::uint8_t>(value[index]);
    std::size_t width = 0U;
    if (byte <= 0x7FU) {
      width = 1U;
    } else if (byte >= 0xC2U && byte <= 0xDFU) {
      width = 2U;
    } else if (byte >= 0xE0U && byte <= 0xEFU) {
      width = 3U;
    } else if (byte >= 0xF0U && byte <= 0xF4U) {
      width = 4U;
    } else {
      return Status::ValidationError(std::string(field_name) + " must contain valid UTF-8.");
    }
    if (index + width > value.size()) {
      return Status::ValidationError(std::string(field_name) + " must contain valid UTF-8.");
    }
    for (std::size_t offset = 1U; offset < width; ++offset) {
      const auto continuation = static_cast<std::uint8_t>(value[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        return Status::ValidationError(std::string(field_name) + " must contain valid UTF-8.");
      }
    }
    if ((byte == 0xE0U && static_cast<std::uint8_t>(value[index + 1U]) < 0xA0U) ||
        (byte == 0xEDU && static_cast<std::uint8_t>(value[index + 1U]) > 0x9FU) ||
        (byte == 0xF0U && static_cast<std::uint8_t>(value[index + 1U]) < 0x90U) ||
        (byte == 0xF4U && static_cast<std::uint8_t>(value[index + 1U]) > 0x8FU)) {
      return Status::ValidationError(std::string(field_name) + " must contain valid UTF-8.");
    }
    index += width;
    ++count;
  }
  return count;
}

} // namespace ksj::recon::detail
