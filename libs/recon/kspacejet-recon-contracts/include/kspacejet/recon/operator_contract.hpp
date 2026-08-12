#pragma once

#include "kspacejet/recon/resource_contracts.hpp"
#include "kspacejet/recon/type_descriptor.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {

inline constexpr std::string_view kOperatorContractSchemaVersion = "kspacejet.operator-contract/v1";
// The M3 completed-frame binding has a single frozen in-process ABI.  The
// TypeDescriptor, including these payload/semantic-metadata schema digests,
// is compared exactly; a Provider cannot relabel an arbitrary acquisition
// message as a FrameSlotContext merely by copying the type id.
inline constexpr std::string_view kCompletedFrameSlotContextFrameTypeId = "ksj.kspace-frame";
inline constexpr std::string_view kCompletedFrameSlotContextPayloadSchemaDigest =
  "sha256:7318daba9d4e9992d33ded54fcf8bd2db1ad9c501ca1bb4f30f351fcace94e9b";
inline constexpr std::string_view kCompletedFrameSlotContextAbiDescriptorDigest =
  "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
inline constexpr std::string_view kCompletedFrameSlotContextMetadataSchemaDigest =
  "sha256:2eb80e75da97288c839ca2c1d2c81e480f93c71739dd182a071e7b3145c72994";

// Returns the one exact TypeDescriptor which promises immutable frame payload
// storage plus the completed FrameSlotContext semantic key used by M3 ordinal
// mapping.  It is an in-process ABI boundary, not a KSpaceJet wire format.
[[nodiscard]] Result<TypeDescriptor> completed_frame_slot_context_type();

enum class PortDirection {
  input,
  output,
};

enum class PortCardinality {
  single,
  many,
};

enum class InputGranularity {
  acquisition,
  microbatch,
  window,
  frame,
  volume,
  scan_finalizer,
};

enum class OrderDomain {
  strict_global,
  per_key,
  unordered,
};

enum class CallModel {
  serial,
  keyed_parallel,
};

enum class RateKind {
  sdf,
  csdf,
  keyed_dynamic,
};

enum class CompletionKind {
  expected_count,
  header_predicate,
  watermark,
  end_of_key,
  end_of_input,
};

enum class EndOfInputPolicy {
  fail,
  partial_output,
  skip,
};

enum class CalibrationRole {
  none,
  producer,
  consumer,
};

enum class TerminalBehavior {
  none,
  flush_declared,
  cleanup_declared,
};

enum class JoinProgressProof {
  none,
  verified_schedule_automaton,
  cohort_reservation,
};

enum class ChannelGroupSource {
  acquisition_active_channel_range,
};

struct PortSpec {
  std::string name;
  // Exact ABI payload semantics.  A type id alone is deliberately not a
  // compatibility rule: descriptor layout, schema, memory domain, alignment
  // and mutability must all match or a separate explicit adapter is needed.
  TypeDescriptor type_descriptor;
  PortDirection direction = PortDirection::input;
  PortCardinality cardinality = PortCardinality::single;
  bool required = true;
  std::vector<std::string> layout_capabilities;
  std::vector<std::string> metadata_capabilities;
};

// An item/byte bound for one named port.  Input entries model exact SDF/CSDF
// consumption; output entries model exact rate or dynamic upper bound,
// depending on the enclosing RateSpec phase.
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

struct CompletionSpec {
  CompletionKind kind = CompletionKind::end_of_input;
  // Used only by expected_count.  A future expression evaluator replaces this
  // value with a checked, plan-specific quantity before admission.
  Quantity expected_count = 0;
  EndOfInputPolicy on_end_of_input = EndOfInputPolicy::fail;
};

struct RateSpec {
  RateKind kind = RateKind::keyed_dynamic;
  // One phase is SDF; two or more phases are CSDF.  They are intentionally
  // unused for keyed_dynamic regions.
  std::vector<RatePhaseSpec> static_phases;
  CompletionSpec completion;
  DynamicPhaseBoundSpec ordinary;
  DynamicPhaseBoundSpec normal_flush;
  DynamicPhaseBoundSpec cancel_cleanup;
};

struct ChannelGroupSpec {
  ChannelGroupSource source = ChannelGroupSource::acquisition_active_channel_range;
  Quantity channels_per_group = 0;
  Quantity max_active_channels = 0;
  Quantity max_groups = 0;
  Quantity max_charged_bytes_per_group = 0;
};

struct OperatorExecutionShapeSpec {
  InputGranularity input_granularity = InputGranularity::acquisition;
  std::vector<std::string> partition_key;
  // channel_group may appear in partition_key only with this explicit source,
  // canonical grouping rule, and finite bounds.  Coil channels otherwise stay
  // a payload dimension and must not create implicit KeyShards.
  std::optional<ChannelGroupSpec> channel_group;
  OrderDomain order_domain = OrderDomain::per_key;
  Quantity max_active_keys = 0;
  // This is instance-wide across every KeyShard, never per KeyShard.
  Quantity max_in_flight = 0;
  CallModel call_model = CallModel::keyed_parallel;
  Quantity max_items_per_activation = 0;
  Quantity cooperative_quantum_us = 0;
};

struct OperatorBatchSpec {
  Quantity min_items = 0;
  Quantity preferred_items = 0;
  Quantity max_items = 0;
  Quantity max_charged_bytes = 0;
  Quantity max_wait_us = 0;
};

struct OperatorResourceSpec {
  Quantity scratch_charged_bytes_per_firing = 0;
  Quantity per_key_state_charged_bytes = 0;
  Quantity per_scan_workspace_charged_bytes = 0;
  Quantity retention_items = 0;
  Quantity retention_charged_bytes = 0;
  Quantity output_items = 0;
  Quantity output_charged_bytes = 0;
  Quantity cpu_permits = 0;
  Quantity backend_gang_threads = 0;
  Quantity provider_private_threads = 0;
  Quantity external_allocation_charged_bytes = 0;
  MemoryDomain memory_domain = MemoryDomain::host;
};

struct CalibrationSpec {
  CalibrationRole role = CalibrationRole::none;
  std::string binding_id;
  std::vector<std::string> key_projection;
  // v1 supports only single_epoch_v1.  The explicit field makes a later epoch
  // extension an ABI/schema change rather than a silently accepted string.
  bool single_epoch_v1 = true;
  Quantity max_active_keys = 0;
  Quantity precalibration_horizon_items = 0;
  Quantity precalibration_horizon_charged_bytes = 0;
  Quantity max_calibration_frame_charged_bytes = 0;
  Quantity max_decoder_staging_bytes = 0;
  EndOfInputPolicy on_end_of_input = EndOfInputPolicy::fail;
};

struct JoinInputSpec {
  std::string port_name;
  std::vector<std::string> key_projection;
  Quantity required_items_per_key = 0;
};

// This is an Operator declaration, not a graph merge declaration: it names
// input ports and contract keys, never graph edge ids.  The PipelineDefinition
// remains responsible for wiring those ports and validating its MergeSpec.
struct JoinSpec {
  std::vector<JoinInputSpec> inputs;
  Quantity max_retained_items_per_key = 0;
  Quantity max_retained_charged_bytes_per_key = 0;
  Quantity max_retained_items_aggregate = 0;
  Quantity max_retained_charged_bytes_aggregate = 0;
  bool require_matching_calibration_version = true;
  bool exactly_once = true;
  EndOfInputPolicy on_end_of_input = EndOfInputPolicy::fail;
  JoinProgressProof progress_proof = JoinProgressProof::none;
};

// Reordering is opt-in.  M3 admits only a non-empty projection of fixed
// Cartesian ISMRMRD XML index fields; the graph compiler owns the resulting
// dense mixed-radix ordinal, capacity and EndOfInput behavior.  Providers do
// not supply an arbitrary ordinal source in v1.
struct ReorderSpec {
  // The one input port that receives an immutable completed FrameSlot.  M3
  // assigns ordinals from its host-owned FrameSlotContext semantic key, never
  // from a Provider ordinal or an acquisition callback.  The port must expose
  // the frozen ksj.kspace-frame TypeDescriptor ABI.
  std::string completed_frame_input_port;
  // The one Provider output stream whose envelopes are ordered by the M3
  // ReorderPlan.  Other output ports are outside this minimal plan model.
  std::string ordered_output_port;
  // M3 supports exactly one OutputEnvelope for every dense ordinal.  Keeping
  // this explicit makes a future multi-output-per-ordinal extension a schema
  // and plan change rather than an ambiguous reinterpretation of capacity.
  Quantity outputs_per_ordinal = 0;
  std::vector<std::string> order_projection;
  Quantity max_ahead_items = 0;
  Quantity max_ahead_charged_bytes = 0;
  EndOfInputPolicy missing_at_end_of_input = EndOfInputPolicy::fail;
};

struct TerminalContractSpec {
  TerminalBehavior normal = TerminalBehavior::none;
  TerminalBehavior cancel = TerminalBehavior::none;
  Quantity normal_max_output_items = 0;
  Quantity normal_max_output_charged_bytes = 0;
  Quantity normal_max_async_tokens = 0;
  Quantity cancel_max_async_tokens = 0;
};

struct OperatorContractSpec {
  std::string schema_version = std::string(kOperatorContractSchemaVersion);
  std::string operator_id;
  std::string operator_revision;
  Quantity provider_abi_major = 0;
  std::vector<ExecutionProfile> supported_profiles;
  std::vector<PortSpec> ports;
  OperatorExecutionShapeSpec execution;
  OperatorBatchSpec batch;
  RateSpec rates;
  OperatorResourceSpec resources;
  CalibrationSpec calibration;
  std::optional<JoinSpec> join;
  std::optional<ReorderSpec> reorder;
  TerminalContractSpec terminal;
};

// Provider-owned, immutable declaration of execution semantics.  The host
// validates this at Provider resolution; PipelineDefinition port assertions
// can only narrow it, never override it.
class OperatorContract final {
public:
  [[nodiscard]] static Result<OperatorContract> create(const OperatorContractSpec& specification);

  [[nodiscard]] const std::string& operator_id() const noexcept { return operator_id_; }
  [[nodiscard]] const std::string& operator_revision() const noexcept { return operator_revision_; }
  [[nodiscard]] Quantity provider_abi_major() const noexcept { return provider_abi_major_.value(); }
  [[nodiscard]] const std::vector<ExecutionProfile>& supported_profiles() const noexcept { return supported_profiles_; }
  [[nodiscard]] bool supports(ExecutionProfile profile) const noexcept;
  [[nodiscard]] const std::vector<PortSpec>& ports() const noexcept { return ports_; }
  [[nodiscard]] const OperatorExecutionShapeSpec& execution() const noexcept { return execution_; }
  [[nodiscard]] const OperatorBatchSpec& batch() const noexcept { return batch_; }
  [[nodiscard]] const RateSpec& rates() const noexcept { return rates_; }
  [[nodiscard]] const OperatorResourceSpec& resources() const noexcept { return resources_; }
  [[nodiscard]] const CalibrationSpec& calibration() const noexcept { return calibration_; }
  [[nodiscard]] const std::optional<JoinSpec>& join() const noexcept { return join_; }
  [[nodiscard]] const std::optional<ReorderSpec>& reorder() const noexcept { return reorder_; }
  [[nodiscard]] const TerminalContractSpec& terminal() const noexcept { return terminal_; }

private:
  OperatorContract(std::string operator_id, std::string operator_revision, CanonicalQuantity provider_abi_major,
                   std::vector<ExecutionProfile> supported_profiles, std::vector<PortSpec> ports,
                   OperatorExecutionShapeSpec execution, OperatorBatchSpec batch, RateSpec rates,
                   OperatorResourceSpec resources, CalibrationSpec calibration, std::optional<JoinSpec> join,
                   std::optional<ReorderSpec> reorder, TerminalContractSpec terminal) noexcept
      : operator_id_(std::move(operator_id)), operator_revision_(std::move(operator_revision)),
        provider_abi_major_(provider_abi_major), supported_profiles_(std::move(supported_profiles)),
        ports_(std::move(ports)), execution_(std::move(execution)), batch_(std::move(batch)), rates_(std::move(rates)),
        resources_(std::move(resources)), calibration_(std::move(calibration)), join_(std::move(join)),
        reorder_(std::move(reorder)), terminal_(std::move(terminal)) {}

  std::string operator_id_;
  std::string operator_revision_;
  CanonicalQuantity provider_abi_major_;
  std::vector<ExecutionProfile> supported_profiles_;
  std::vector<PortSpec> ports_;
  OperatorExecutionShapeSpec execution_;
  OperatorBatchSpec batch_;
  RateSpec rates_;
  OperatorResourceSpec resources_;
  CalibrationSpec calibration_;
  std::optional<JoinSpec> join_;
  std::optional<ReorderSpec> reorder_;
  TerminalContractSpec terminal_;
};

} // namespace ksj::recon
