// SPDX-License-Identifier: Apache-2.0
//
// Private declaration for the coil_compression_basis_estimate Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::calibration::operators {

[[nodiscard]] const state::CalibrationOperatorImplementation& coil_compression_basis_estimate_operator() noexcept;

} // namespace ksj::calibration::operators
