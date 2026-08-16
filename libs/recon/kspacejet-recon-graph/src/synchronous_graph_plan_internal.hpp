#pragma once

#include "kspacejet/recon/graph/execution_plan_compiler.hpp"

namespace ksj::recon::graph::detail {

// The generic graph compiler and independent verifier intentionally replace
// the retired single-link derivation.  They are source-private so callers have
// one public planning API while the two implementations remain separately
// testable and cannot become an accidental second artifact surface.
[[nodiscard]] Result<CompiledExecutionPlan> compile_synchronous_graph_plan(const PlanBuildRequest& request);
[[nodiscard]] Result<VerificationRecord> verify_synchronous_graph_plan(const ExecutionPlan& plan,
                                                                       const PlanBuildRequest& request);

} // namespace ksj::recon::graph::detail
