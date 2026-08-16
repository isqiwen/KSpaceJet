// SPDX-License-Identifier: Apache-2.0
//
// Private declaration for the phase_correct Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::kspace_conditioning::operators {

[[nodiscard]] const state::ConditioningOperatorImplementation& phase_correct_operator() noexcept;

} // namespace ksj::kspace_conditioning::operators
