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
// isolated_strict_online and deadline_qualified_online require a separately
// qualified worker/fault boundary.  The current in-process Provider runtime
// must reject them rather than silently weakening their guarantees.
enum class ExecutionProfile {
  offline,
  bounded_online,
  isolated_strict_online,
  deadline_qualified_online,
  research_unbounded,
};

[[nodiscard]] std::string_view to_string(ExecutionProfile profile) noexcept;
[[nodiscard]] Result<ExecutionProfile> parse_execution_profile(std::string_view value);
[[nodiscard]] bool requires_provider_isolation(ExecutionProfile profile) noexcept;
[[nodiscard]] bool is_currently_supported_in_process(ExecutionProfile profile) noexcept;

} // namespace ksj::recon
