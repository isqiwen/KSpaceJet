// SPDX-License-Identifier: Apache-2.0
//
// Private declaration for the image_offset_float32 Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::image_ops::operators {

[[nodiscard]] const state::ImageOperatorImplementation& image_offset_float32_operator() noexcept;

} // namespace ksj::image_ops::operators
