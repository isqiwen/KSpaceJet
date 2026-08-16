#pragma once

// NodePlanningRequirements are scan/pipeline-plan inputs.  They describe how
// one resolved node is scheduled, bounded, and connected for one plan; they
// are not a Provider capability declaration.  The Provider ABI remains the
// capability upper bound, while OperatorContract declares only the stable
// operator identity and typed ports that a pipeline may connect.

#include "kspacejet/recon/planning_inputs.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ksj::recon {

class OperatorContract;

enum class InputGranularity {
  acquisition,
  microbatch,
  window,
  frame,
  volume,
  scan_finalizer,
};

enum class RateKind {
  sdf,
  csdf,
  keyed_dynamic,
};

enum class CalibrationRole {
  none,
  producer,
  consumer,
};

enum class JoinProgressProof {
  none,
  verified_schedule_automaton,
  cohort_reservation,
};

// An item/byte bound for one named port.  Input entries model exact SDF/CSDF
// consumption; output entries model exact rate or dynamic upper bound,
// depending on the enclosing NodeRateSpec phase.
struct PortRateSpec {
  std::string port_name;
  Quantity items = 0;
  Quantity charged_bytes = 0;
};

struct RatePhaseSpec {
  std::vector<PortRateSpec> inputs;
  std::vector<PortRateSpec> outputs;
};

struct DynamicPhaseBoundSpec {
  Quantity max_firings = 0;
  std::vector<PortRateSpec> outputs;
};

struct OutputPhaseBoundSpec {
  std::vector<PortRateSpec> outputs;
};

struct NodeRateSpec {
  RateKind kind = RateKind::keyed_dynamic;
  // One phase is SDF; two or more phases are CSDF.  They are intentionally
  // unused for keyed_dynamic regions.
  std::vector<RatePhaseSpec> static_phases;
  OutputPhaseBoundSpec ordinary;
  DynamicPhaseBoundSpec normal_flush;
};

struct NodeChannelGroupSpec {
  Quantity channels_per_group = 0;
  Quantity max_active_channels = 0;
  Quantity max_groups = 0;
};

struct NodeExecutionSpec {
  InputGranularity input_granularity = InputGranularity::acquisition;
  std::vector<std::string> partition_key;
  // channel_group may appear in partition_key only with the canonical grouping
  // rule and finite bounds.  Coil channels otherwise stay
  // a payload dimension and must not create implicit KeyShards.
  std::optional<NodeChannelGroupSpec> channel_group;
  Quantity max_active_keys = 0;
  // This is instance-wide across every KeyShard, never per KeyShard.
  Quantity max_in_flight = 0;
  Quantity max_items_per_activation = 0;
};

struct NodeBatchSpec {
  Quantity min_items = 0;
  Quantity preferred_items = 0;
  Quantity max_items = 0;
  Quantity max_charged_bytes = 0;
};

struct NodeResourceRequirements {
  Quantity scratch_charged_bytes_per_firing = 0;
  Quantity per_key_state_charged_bytes = 0;
  Quantity per_scan_workspace_charged_bytes = 0;
  Quantity retention_charged_bytes = 0;
  Quantity output_items = 0;
  Quantity output_charged_bytes = 0;
  Quantity cpu_permits = 0;
  Quantity backend_gang_threads = 0;
  Quantity provider_private_threads = 0;
  Quantity external_allocation_charged_bytes = 0;
  MemoryDomain memory_domain = MemoryDomain::host;
};

struct NodeCalibrationRequirements {
  CalibrationRole role = CalibrationRole::none;
  std::string binding_id;
  Quantity max_active_keys = 0;
  Quantity precalibration_horizon_items = 0;
  Quantity precalibration_horizon_charged_bytes = 0;
  Quantity max_calibration_frame_charged_bytes = 0;
  Quantity max_decoder_staging_bytes = 0;
};

// A NodeJoinSpec supplies only the bounded retention reservation and the proof
// required for online admission.  Graph wiring remains PipelineDefinition's
// responsibility; it is not duplicated in node planning requirements.
struct NodeJoinSpec {
  Quantity max_retained_charged_bytes_aggregate = 0;
  JoinProgressProof progress_proof = JoinProgressProof::none;
};

struct TerminalPlanningSpec {
  Quantity normal_max_output_items = 0;
  Quantity normal_max_output_charged_bytes = 0;
  Quantity normal_max_async_tokens = 0;
  Quantity cancel_max_async_tokens = 0;
};

struct NodePlanningRequirementsSpec {
  NodeExecutionSpec execution;
  NodeBatchSpec batch;
  NodeRateSpec rates;
  NodeResourceRequirements resources;
  NodeCalibrationRequirements calibration;
  std::optional<NodeJoinSpec> join;
  TerminalPlanningSpec terminal;
};

// Immutable requirements for one node in one scan/pipeline plan.  Validation
// is explicitly tied to that node's resolved OperatorContract so named rate
// and topology ports cannot be interpreted without a typed contract.
class NodePlanningRequirements final {
public:
  [[nodiscard]] static Result<NodePlanningRequirements> create(const NodePlanningRequirementsSpec& specification,
                                                               const OperatorContract& contract);

  // Recheck a stored requirements payload against the exact typed interface
  // paired with it by PlanBuildRequest.  Graph compilation and independent
  // verification call this after matching bindings by node_id; requirements
  // never carry a duplicate mutable contract identity.
  [[nodiscard]] Status validate_against(const OperatorContract& contract) const;

  [[nodiscard]] const NodeExecutionSpec& execution() const noexcept { return execution_; }
  [[nodiscard]] const NodeBatchSpec& batch() const noexcept { return batch_; }
  [[nodiscard]] const NodeRateSpec& rates() const noexcept { return rates_; }
  [[nodiscard]] const NodeResourceRequirements& resources() const noexcept { return resources_; }
  [[nodiscard]] const NodeCalibrationRequirements& calibration() const noexcept { return calibration_; }
  [[nodiscard]] const std::optional<NodeJoinSpec>& join() const noexcept { return join_; }
  [[nodiscard]] const TerminalPlanningSpec& terminal() const noexcept { return terminal_; }

private:
  NodePlanningRequirements(NodeExecutionSpec execution, NodeBatchSpec batch, NodeRateSpec rates,
                           NodeResourceRequirements resources, NodeCalibrationRequirements calibration,
                           std::optional<NodeJoinSpec> join, TerminalPlanningSpec terminal) noexcept
      : execution_(std::move(execution)), batch_(std::move(batch)), rates_(std::move(rates)),
        resources_(std::move(resources)), calibration_(std::move(calibration)), join_(std::move(join)),
        terminal_(std::move(terminal)) {}

  NodeExecutionSpec execution_;
  NodeBatchSpec batch_;
  NodeRateSpec rates_;
  NodeResourceRequirements resources_;
  NodeCalibrationRequirements calibration_;
  std::optional<NodeJoinSpec> join_;
  TerminalPlanningSpec terminal_;
};

// PlanBuildRequest binds one validated planning payload to one pipeline node.
// It intentionally carries no Provider capability claim and no node config:
// Provider capability comes from the ABI, while canonical node config remains
// owned by the ResolvedPipeline and is frozen separately in ExecutionPlan.
struct NodePlanningRequirementsBinding {
  std::string node_id;
  NodePlanningRequirements requirements;
};

} // namespace ksj::recon
