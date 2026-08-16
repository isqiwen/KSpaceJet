// SPDX-License-Identifier: Apache-2.0
//
// Private declaration for the readout_oversampling_remove Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::kspace_conditioning::operators {

[[nodiscard]] const state::ConditioningOperatorImplementation& readout_oversampling_remove_operator() noexcept;

} // namespace ksj::kspace_conditioning::operators
