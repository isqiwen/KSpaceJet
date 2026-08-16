// SPDX-License-Identifier: Apache-2.0
//
// Private declaration for the noncartesian_coil_compress Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::kspace_conditioning::operators {

[[nodiscard]] const state::ConditioningOperatorImplementation& noncartesian_coil_compress_operator() noexcept;

} // namespace ksj::kspace_conditioning::operators
