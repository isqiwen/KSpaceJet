// SPDX-License-Identifier: Apache-2.0
//
// Private declaration for the coil_compress Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::kspace_conditioning::operators {

[[nodiscard]] const state::ConditioningOperatorImplementation& coil_compress_operator() noexcept;

} // namespace ksj::kspace_conditioning::operators
