// SPDX-License-Identifier: Apache-2.0

#include "support/image_transform_config.hpp"

#include "kspacejet/provider/detail/provider_support.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>

namespace ksj::image_ops::support {

using ::ksj::provider::detail::has_full_compatible_header;

bool parse_canonical_finite_decimal(const char* const first, const char* const last, float& out_value) noexcept {
  if (first == nullptr || last == nullptr || first >= last || static_cast<std::size_t>(last - first) > 64U) {
    return false;
  }

  const char* cursor = first;
  bool negative = false;
  if (*cursor == '-') {
    negative = true;
    ++cursor;
    if (cursor == last) {
      return false;
    }
  }

  const char* const integer_first = cursor;
  if (*cursor == '0') {
    ++cursor;
    if (cursor != last && *cursor >= '0' && *cursor <= '9') {
      return false;
    }
  } else if (*cursor >= '1' && *cursor <= '9') {
    do {
      ++cursor;
    } while (cursor != last && *cursor >= '0' && *cursor <= '9');
  } else {
    return false;
  }

  bool has_fraction = false;
  if (cursor != last && *cursor == '.') {
    has_fraction = true;
    ++cursor;
    const char* const fraction_first = cursor;
    while (cursor != last && *cursor >= '0' && *cursor <= '9') {
      ++cursor;
    }
    if (cursor == fraction_first || *(cursor - 1) == '0') {
      return false;
    }
  }
  if (cursor != last) {
    return false;
  }
  if (negative && integer_first[0U] == '0' && !has_fraction) {
    return false;
  }

  float parsed = 0.0F;
  const auto parsed_result = std::from_chars(first, last, parsed, std::chars_format::fixed);
  if (parsed_result.ec != std::errc{} || parsed_result.ptr != last || !std::isfinite(parsed)) {
    return false;
  }
  out_value = parsed;
  return true;
}

bool parse_single_finite_decimal_config(const ksj_byte_view& config, const std::string_view prefix,
                                        float& out_value) noexcept {
  constexpr std::string_view kSuffix{"}"};
  if (!has_full_compatible_header(&config) || config.data == nullptr || config.size == 0U ||
      config.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  const auto encoded = std::string_view{static_cast<const char*>(config.data), static_cast<std::size_t>(config.size)};
  if (!encoded.starts_with(prefix) || !encoded.ends_with(kSuffix)) {
    return false;
  }
  const char* const first = encoded.data() + prefix.size();
  const char* const last = encoded.data() + encoded.size() - kSuffix.size();
  return parse_canonical_finite_decimal(first, last, out_value);
}

} // namespace ksj::image_ops::support
