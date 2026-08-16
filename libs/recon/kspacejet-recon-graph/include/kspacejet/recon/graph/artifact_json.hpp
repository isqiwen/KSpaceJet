#pragma once

#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/result.hpp"

#include <string>

namespace ksj::recon::graph {

// Serializes exactly the JSON payload covered by the detached
// ExecutionPlan digest.  The returned document deliberately omits the digest
// itself and optional `$schema` decoration.
[[nodiscard]] Result<std::string> serialize_execution_plan_canonical_json(const ExecutionPlan& plan);

// Serializes exactly the JSON payload covered by the detached
// VerificationRecord digest.  The returned document deliberately omits the
// record digest itself and optional `$schema` decoration.
[[nodiscard]] Result<std::string> serialize_verification_record_canonical_json(const VerificationRecord& record);

} // namespace ksj::recon::graph
