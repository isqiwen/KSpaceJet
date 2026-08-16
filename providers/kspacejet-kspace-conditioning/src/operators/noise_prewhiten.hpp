// SPDX-License-Identifier: Apache-2.0
//
// Private declaration for the noise_prewhiten Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::kspace_conditioning::operators {

[[nodiscard]] const state::ConditioningOperatorImplementation& noise_prewhiten_operator() noexcept;

} // namespace ksj::kspace_conditioning::operators
