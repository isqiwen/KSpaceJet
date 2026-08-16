// SPDX-License-Identifier: Apache-2.0
//
// Private declaration for the noise_model_estimate Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::calibration::operators {

[[nodiscard]] const state::CalibrationOperatorImplementation& noise_model_estimate_operator() noexcept;

} // namespace ksj::calibration::operators
