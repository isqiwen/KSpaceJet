// SPDX-License-Identifier: Apache-2.0
//
// Shared canonical-config parsing for the float32 image transform Operators.

#pragma once

#include "kspacejet/provider/provider.h"

#include <string_view>

namespace ksj::image_ops::support {

[[nodiscard]] bool parse_canonical_finite_decimal(const char* first, const char* last, float& out_value) noexcept;
[[nodiscard]] bool parse_single_finite_decimal_config(const ksj_byte_view& config, std::string_view prefix,
                                                      float& out_value) noexcept;

} // namespace ksj::image_ops::support
