#pragma once

#include "kspacejet/recon/result.hpp"

#include <string_view>

namespace ksj::recon {

// A profile is selected by PlanBuildRequest, then frozen into the
// ExecutionPlan, VerificationRecord, AdmissionRecord and RunRecord.  A
// ResolvedPipeline deliberately stays profile-neutral so one exact Provider
// snapshot can be compiled for more than one admissible deployment profile.
//
// Names describe claims, not implementation switches.  In particular,
// isolated_provider_runtime requires a separately qualified worker/fault
// boundary.  The current in-process Provider runtime must reject it rather
// than silently weakening its guarantees.
enum class ExecutionProfile {
  offline_reference,
  bounded_reconstruction_graph,
  provider_development,
  embedded_incremental,
  isolated_provider_runtime,
};

[[nodiscard]] std::string_view to_string(ExecutionProfile profile) noexcept;
[[nodiscard]] Result<ExecutionProfile> parse_execution_profile(std::string_view value);
[[nodiscard]] bool requires_provider_isolation(ExecutionProfile profile) noexcept;
[[nodiscard]] bool is_currently_supported_in_process(ExecutionProfile profile) noexcept;

} // namespace ksj::recon
