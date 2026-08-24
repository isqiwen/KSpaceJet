// SPDX-License-Identifier: Apache-2.0
//
// Private implementation declaration for the radial_gridding_reconstruct
// Operator.

#pragma once

#include "provider_state.hpp"

namespace ksj::noncartesian_recon::operators {

[[nodiscard]] const state::NonCartesianReconOperatorImplementation& radial_gridding_reconstruct_operator() noexcept;

} // namespace ksj::noncartesian_recon::operators
