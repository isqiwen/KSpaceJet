#pragma once

#include "kspacejet/recon/model.hpp"
#include "kspacejet/recon/scan_facts.hpp"
#include "kspacejet/recon/graph/effective_pipeline_binding.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"

#include <string>
#include <vector>

namespace ksj::recon::graph {

// The loader resolves a Provider bundle before graph planning.  This binding
// joins that trusted resolution to the concrete, provider-owned contract for
// one authored node. It deliberately does not expose a DLL path or a Provider
// instance: those remain loader/runtime concerns.
struct OperatorContractBinding {
  std::string node_id;
  OperatorContract contract;
};

struct PlanBuildRequest {
  // The resolved pipeline identifies the user's authored graph and Provider
  // selection.  ScanFacts and EffectivePipelineBinding are host-owned values:
  // they bind concrete ISMRMRD observations and resulting effective node
  // configurations without letting callers assert unrelated digest strings.
  const ResolvedPipeline& resolved_pipeline;
  ExecutionProfile requested_profile{ExecutionProfile::bounded_reconstruction_graph};
  const ScanFacts& scan_facts;
  const EffectivePipelineBinding& effective_pipeline_binding;
  const TargetEnvelope& target_envelope;
  const MachinePolicy& machine_policy;
  std::vector<OperatorContractBinding> operator_contract_bindings;
  // Planning requirements are scan-node bindings rather than Provider
  // contract content. They must form the same complete node-id set as the
  // resolved PipelineDefinition and OperatorContract bindings.
  std::vector<NodePlanningRequirementsBinding> node_planning_requirements;
};

struct CompiledExecutionPlan {
  ExecutionPlan plan;
};

// A deterministic, serial graph compiler.  It implements the admission-time
// part of PipelineDefinition current: contracts and scan metadata are checked,
// finite synchronous node/pool/edge/resource bounds are derived, and a frozen plan plus a
// separately derived certificate are emitted.  It performs no dynamic
// admission reservation and creates no Provider instance.
class ExecutionPlanCompiler final {
public:
  [[nodiscard]] static Result<CompiledExecutionPlan> compile(const PlanBuildRequest& request);

  // A deterministic recheck is useful for compiler regression tests, but is
  // not an independent safety proof and must never be presented as one.
  [[nodiscard]] static Status deterministic_recheck(const CompiledExecutionPlan& compiled,
                                                    const PlanBuildRequest& request);
};

// A deliberately separate derived-semantics path.  It validates a frozen
// plan against immutable inputs and returns a VerificationRecord; it does not
// call ExecutionPlanCompiler::compile().  M0 covers structural/resource
// invariants only.  Later milestones extend this verifier with typed
// expressions and proof witnesses.
class ExecutionPlanVerifier final {
public:
  [[nodiscard]] static Result<VerificationRecord> verify(const ExecutionPlan& plan, const PlanBuildRequest& request);
};

} // namespace ksj::recon::graph
