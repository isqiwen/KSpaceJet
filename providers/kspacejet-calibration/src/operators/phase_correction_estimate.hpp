// SPDX-License-Identifier: Apache-2.0
//
// Private declaration for the phase_correction_estimate Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::calibration::operators {

[[nodiscard]] const state::CalibrationOperatorImplementation& phase_correction_estimate_operator() noexcept;

} // namespace ksj::calibration::operators
