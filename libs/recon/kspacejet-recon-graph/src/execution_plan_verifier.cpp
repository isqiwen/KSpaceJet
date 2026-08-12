#include "kspacejet/recon/graph/execution_plan_compiler.hpp"

#include "kspacejet/recon/bounded_value.hpp"
#include "kspacejet/recon/graph/artifact_json.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ksj::recon::graph {
namespace {

// This translation unit deliberately owns a second derivation of the M0
// plan semantics.  It must not share the compiler's helper functions: a
// shared arithmetic or canonicalization bug would otherwise let the compiler
// attest its own invalid witness.  Keep changes here structurally independent
// from execution_plan_compiler.cpp and cover both paths with negative tests.
struct VerifierDenseKeyDomain {
  std::vector<DenseKeySlotDimensionSpec> dimensions;
  Quantity cardinality{1};
};

struct VerifierCartesianOrdinalDomain {
  std::vector<DenseCartesianOrdinalDimensionSpec> dimensions;
  Quantity cardinality{1};
};

struct VerifierOutputBound {
  Quantity items{0};
  Quantity bytes{0};
};

struct IndependentlyDerivedPlan {
  std::vector<KeySlotTablePlanSpec> key_slot_tables;
  std::vector<ReorderPlanSpec> reorder_plans;
  std::vector<BufferPoolPlanSpec> buffer_pool_plans;
  std::vector<DataEdgePlanSpec> data_edge_plans;
  std::vector<EdgeCapacitySpec> edge_capacities;
  ResourceVectorSpec resource_vector;
  Quantity terminal_occurrences{0};
};

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

[[nodiscard]] Status validate_pre_ingress_scan_envelope_independently(const PlanBuildRequest& request) {
  if (request.scan_descriptor.source_xml_bytes() > request.target_envelope.max_xml_bytes()) {
    return validation("ScanDescriptor source XML byte length exceeds TargetEnvelope.max_xml_bytes before ingress.");
  }
  return Status::Ok();
}

template <typename T> [[nodiscard]] bool contains(const std::vector<T>& values, const T value) {
  return std::ranges::find(values, value) != values.end();
}

[[nodiscard]] bool contains(const std::vector<std::string>& values, const std::string_view value) {
  return std::ranges::find(values, value) != values.end();
}

[[nodiscard]] Status add_to(Quantity& total, const Quantity value, const std::string_view expression) {
  auto next = checked_add(total, value, expression);
  if (!next.ok()) {
    return next.status();
  }
  total = next.value();
  return Status::Ok();
}

[[nodiscard]] Result<Quantity> multiply(const Quantity lhs, const Quantity rhs, const std::string_view expression) {
  return checked_multiply(lhs, rhs, expression);
}

// Keep M3 record accounting local to this verifier.  Sharing the contracts
// helper would let a defect in the compiler-side plan arithmetic and the
// verifier's check cancel each other out.  These are abstract v1 record
// charges, not C++ object-layout assumptions.
constexpr Quantity kVerifierM3OrdinalRecordChargedBytes = 16U;
constexpr Quantity kVerifierM3BufferedSlotChargedBytes = 16U;
constexpr Quantity kVerifierM3ImmutableHandleSidecarChargedBytes = 64U;
constexpr Quantity kVerifierM37PoolControlChargedBytesPerSlot = 40U;
constexpr Quantity kVerifierM37DataEdgeControlChargedBytesPerItem = 96U;
constexpr Quantity kVerifierM37FiringLeaseHostStagingChargedBytes = 4096U;
constexpr Quantity kVerifierM37FiringLeaseHostStagingDescriptorCount = 5U;

[[nodiscard]] Result<Quantity> derive_m3_host_metadata_independently(const Quantity ordinal_domain_bound,
                                                                     const Quantity max_ahead_items,
                                                                     const std::string_view expression) {
  auto ordinal_records = checked_multiply(ordinal_domain_bound, kVerifierM3OrdinalRecordChargedBytes,
                                          std::string(expression) + ".ordinal_records");
  if (!ordinal_records.ok()) {
    return ordinal_records.status();
  }
  auto buffered_slots =
    checked_multiply(max_ahead_items, kVerifierM3BufferedSlotChargedBytes, std::string(expression) + ".buffered_slots");
  if (!buffered_slots.ok()) {
    return buffered_slots.status();
  }
  auto handles = checked_multiply(max_ahead_items, kVerifierM3ImmutableHandleSidecarChargedBytes,
                                  std::string(expression) + ".immutable_handle_sidecars");
  if (!handles.ok()) {
    return handles.status();
  }
  auto base = checked_add(ordinal_records.value(), buffered_slots.value(),
                          std::string(expression) + ".ordinal_records_and_buffered_slots");
  if (!base.ok()) {
    return base.status();
  }
  return checked_add(base.value(), handles.value(),
                     std::string(expression) + ".total_dense_cartesian_reorder_metadata");
}

[[nodiscard]] const PipelineNode* find_node(const PipelineDefinition& definition, const std::string_view node_id) {
  const auto found = std::ranges::find(definition.nodes(), node_id, &PipelineNode::id);
  return found == definition.nodes().end() ? nullptr : &*found;
}

[[nodiscard]] const OperatorContractBinding* find_binding(const PlanBuildRequest& request,
                                                          const std::string_view node_id) {
  const auto found = std::ranges::find(request.operator_contracts, node_id, &OperatorContractBinding::node_id);
  return found == request.operator_contracts.end() ? nullptr : &*found;
}

[[nodiscard]] const PortSpec* find_port(const OperatorContract& contract, const std::string_view port_name) {
  const auto found = std::ranges::find(contract.ports(), port_name, &PortSpec::name);
  return found == contract.ports().end() ? nullptr : &*found;
}

[[nodiscard]] const ResolvedProvider* find_provider(const ResolvedPipeline& pipeline, const std::string_view alias) {
  const auto found = std::ranges::find(pipeline.providers(), alias, &ResolvedProvider::alias);
  return found == pipeline.providers().end() ? nullptr : &*found;
}

[[nodiscard]] const ResolvedOperator* find_operator(const ResolvedProvider& provider,
                                                    const std::string_view operator_id) {
  const auto found = std::ranges::find(provider.operators, operator_id, &ResolvedOperator::id);
  return found == provider.operators.end() ? nullptr : &*found;
}

[[nodiscard]] std::string type_reference(const TypeDescriptor& descriptor) {
  return descriptor.type_id() + "/v" + std::to_string(descriptor.revision());
}

[[nodiscard]] bool is_memory_domain_allowed(const MachinePolicy& policy, const MemoryDomain domain) {
  return contains(policy.allowed_memory_domains(), domain);
}

[[nodiscard]] Status validate_port_endpoint_independently(const PlanBuildRequest& request,
                                                          const NodePortReference& endpoint,
                                                          const PortDirection expected_direction,
                                                          const std::string_view expected_type,
                                                          const std::string_view context) {
  const auto* binding = find_binding(request, endpoint.node);
  if (binding == nullptr) {
    return validation(std::string(context) + " references an unbound node");
  }
  const auto* port = find_port(binding->contract, endpoint.port);
  if (port == nullptr) {
    return validation(std::string(context) + " references unknown OperatorContract port '" + endpoint.port + "'");
  }
  if (port->direction != expected_direction) {
    return validation(std::string(context) + " references an OperatorContract port with the wrong direction");
  }
  if (!expected_type.empty() && type_reference(port->type_descriptor) != expected_type) {
    return validation(std::string(context) + " type does not exactly match the resolved OperatorContract port");
  }
  return Status::Ok();
}

// Repeat the compiler's provenance rule independently: a completed frame is
// an internal post-assembly value.  A public ingress type string cannot act
// as an ABI or occurrence witness for an M3 ordinal.
[[nodiscard]] Status validate_m3_frame_sources_independently(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  auto frozen_frame = completed_frame_slot_context_type();
  if (!frozen_frame.ok()) {
    return frozen_frame.status();
  }

  for (const auto& consumer : definition.nodes()) {
    const auto* consumer_binding = find_binding(request, consumer.id);
    const auto& reorder = consumer_binding->contract.reorder();
    if (!reorder.has_value()) {
      continue;
    }

    if (std::ranges::any_of(definition.ingress_ports(), [&](const IngressPort& ingress) {
          return ingress.to.node == consumer.id && ingress.to.port == reorder->completed_frame_input_port;
        })) {
      return validation("independent M3 completed_frame_input_port for node '" + consumer.id +
                        "' must not be supplied by public ingress.");
    }

    const auto first_source = std::ranges::find_if(definition.edges(), [&](const PipelineEdge& edge) {
      return edge.to.node == consumer.id && edge.to.port == reorder->completed_frame_input_port;
    });
    if (first_source == definition.edges().end()) {
      return validation("independent M3 completed_frame_input_port for node '" + consumer.id +
                        "' requires a resolved internal typed graph output.");
    }
    if (std::ranges::any_of(std::next(first_source), definition.edges().end(), [&](const PipelineEdge& edge) {
          return edge.to.node == consumer.id && edge.to.port == reorder->completed_frame_input_port;
        })) {
      return validation("independent M3 completed_frame_input_port has multiple graph producers.");
    }

    const auto* producer_binding = find_binding(request, first_source->from.node);
    const auto* producer_port =
      producer_binding == nullptr ? nullptr : find_port(producer_binding->contract, first_source->from.port);
    if (producer_port == nullptr || producer_port->direction != PortDirection::output ||
        !producer_port->type_descriptor.exactly_matches(frozen_frame.value())) {
      return validation("independent M3 completed_frame_input_port must be sourced by an exact completed "
                        "FrameSlotContext output TypeDescriptor.");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_independent_m37_source_contract(const OperatorContract& contract,
                                                              const std::string_view node_id,
                                                              const std::string_view port_name) {
  const auto& phases = contract.rates().static_phases;
  if (phases.size() != 1U) {
    return validation("independent M3.7 buffer producer '" + std::string(node_id) + "." + std::string(port_name) +
                      " must declare one ordinary SDF phase.");
  }
  const auto selected = std::ranges::find(phases.front().outputs, port_name, &PortRateSpec::port_name);
  if (selected == phases.front().outputs.end() || selected->items != 1U || selected->charged_bytes == 0U) {
    return validation("independent M3.7 buffer producer '" + std::string(node_id) + "." + std::string(port_name) +
                      " must declare exactly one positive selected ordinary output envelope.");
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_m37_buffer_topology_independently(const PlanBuildRequest& request) {
  const auto completed = completed_frame_slot_context_type();
  if (!completed.ok()) {
    return completed.status();
  }
  const auto& definition = request.resolved_pipeline.definition();
  for (const auto& egress : definition.egress_ports()) {
    const auto* binding = find_binding(request, egress.from.node);
    const auto* port = binding == nullptr ? nullptr : find_port(binding->contract, egress.from.port);
    if (port != nullptr && port->type_descriptor.payload_kind() == PayloadKind::buffer_handle) {
      return validation("independent M3.7 rejects public buffer_handle egress '" + egress.id +
                        "'; the v1 plan has no public ownership boundary for it.");
    }
  }
  for (const auto& edge : definition.edges()) {
    const auto* binding = find_binding(request, edge.from.node);
    const auto* port = binding == nullptr ? nullptr : find_port(binding->contract, edge.from.port);
    if (port == nullptr || port->type_descriptor.payload_kind() != PayloadKind::buffer_handle) {
      continue;
    }
    if (port->type_descriptor.exactly_matches(completed.value())) {
      continue;
    }
    const auto supports_host_normal =
      std::ranges::find(port->type_descriptor.allowed_memory_domains(), TypeMemoryDomain::host_normal) !=
      port->type_descriptor.allowed_memory_domains().end();
    if (port->type_descriptor.mutability() != PayloadMutability::immutable_after_publish || !supports_host_normal) {
      return validation("independent M3.7 buffer_handle edge '" + edge.id +
                        "' requires immutable_after_publish host_normal payload identity.");
    }
    const auto& reorder = binding->contract.reorder();
    if (!reorder.has_value() || reorder->ordered_output_port != edge.from.port) {
      return validation(
        "independent M3.7 buffer_handle edge '" + edge.id +
        "' must originate at its producer's selected ReorderSpec output; no EdgeCapacity fallback exists.");
    }
    const auto copies = std::ranges::count_if(definition.edges(), [&](const PipelineEdge& candidate) {
      return candidate.from.node == edge.from.node && candidate.from.port == edge.from.port;
    });
    if (copies != 1U) {
      return validation(
        "independent M3.7 rejects buffer-handle fan-out because its move-only pool lease has one consumer.");
    }
    const auto source_contract =
      validate_independent_m37_source_contract(binding->contract, edge.from.node, edge.from.port);
    if (!source_contract.ok()) {
      return source_contract;
    }
  }
  return Status::Ok();
}

[[nodiscard]] bool independent_has_m37_pool_output(const PlanBuildRequest& request, const std::string_view node_id,
                                                   const OperatorContract& contract) {
  if (!contract.reorder().has_value()) {
    return false;
  }
  const auto& port_name = contract.reorder()->ordered_output_port;
  const auto* port = find_port(contract, port_name);
  if (port == nullptr || port->type_descriptor.payload_kind() != PayloadKind::buffer_handle) {
    return false;
  }
  return std::ranges::any_of(request.resolved_pipeline.definition().edges(), [&](const PipelineEdge& edge) {
    return edge.from.node == node_id && edge.from.port == port_name;
  });
}

[[nodiscard]] Status validate_graph_ports_independently(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  std::unordered_set<std::string> produced_inputs;

  for (const auto& edge : definition.edges()) {
    auto source = validate_port_endpoint_independently(request, edge.from, PortDirection::output, "",
                                                       "edge '" + edge.id + "' source");
    if (!source.ok()) {
      return source;
    }
    auto destination = validate_port_endpoint_independently(request, edge.to, PortDirection::input, "",
                                                            "edge '" + edge.id + "' destination");
    if (!destination.ok()) {
      return destination;
    }

    const auto* source_binding = find_binding(request, edge.from.node);
    const auto* destination_binding = find_binding(request, edge.to.node);
    const auto* source_port = find_port(source_binding->contract, edge.from.port);
    const auto* destination_port = find_port(destination_binding->contract, edge.to.port);
    if (!source_port->type_descriptor.exactly_matches(destination_port->type_descriptor)) {
      return validation("edge '" + edge.id + "' connects incompatible resolved OperatorContract port types");
    }
    if (!produced_inputs.insert(edge.to.node + "." + edge.to.port).second) {
      return validation("multiple producers require an explicit MergeBinding");
    }
  }

  for (const auto& ingress : definition.ingress_ports()) {
    auto status = validate_port_endpoint_independently(request, ingress.to, PortDirection::input, ingress.type,
                                                       "ingress '" + ingress.id + "'");
    if (!status.ok()) {
      return status;
    }
    if (!produced_inputs.insert(ingress.to.node + "." + ingress.to.port).second) {
      return validation("ingress '" + ingress.id + "' creates a second producer for one input port");
    }
  }

  for (const auto& egress : definition.egress_ports()) {
    auto status = validate_port_endpoint_independently(request, egress.from, PortDirection::output, egress.type,
                                                       "egress '" + egress.id + "'");
    if (!status.ok()) {
      return status;
    }
  }

  for (const auto& node : definition.nodes()) {
    const auto* binding = find_binding(request, node.id);
    if (binding == nullptr) {
      return validation("pipeline node '" + node.id + "' has no OperatorContract binding");
    }
    for (const auto& port : binding->contract.ports()) {
      if (port.direction == PortDirection::input && port.required &&
          !produced_inputs.contains(node.id + "." + port.name)) {
        return validation("required resolved OperatorContract input '" + node.id + "." + port.name +
                          "' has no producer");
      }
    }
  }
  return validate_m3_frame_sources_independently(request);
}

[[nodiscard]] Status validate_bindings_independently(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  if (request.operator_contracts.size() != definition.nodes().size()) {
    return validation("independent verifier requires exactly one OperatorContract binding for every pipeline node");
  }

  std::unordered_set<std::string> seen_nodes;
  for (const auto& binding : request.operator_contracts) {
    if (binding.node_id.empty() || !seen_nodes.insert(binding.node_id).second) {
      return validation("OperatorContract bindings must use unique non-empty node ids");
    }
    if (find_node(definition, binding.node_id) == nullptr) {
      return validation("OperatorContract binding references unknown node '" + binding.node_id + "'");
    }
  }

  const auto profile = request.requested_profile;
  if (!contains(definition.allowed_profiles(), profile)) {
    return validation("requested execution profile is forbidden by PipelineDefinition");
  }
  if (!request.machine_policy.allows(profile)) {
    return validation("requested execution profile is forbidden by MachinePolicy");
  }
  if (!is_currently_supported_in_process(profile)) {
    return validation("requested execution profile is not supported by the current in-process Provider runtime; "
                      "only offline and bounded-online are currently supported");
  }

  for (const auto& node : definition.nodes()) {
    const auto* binding = find_binding(request, node.id);
    if (binding == nullptr) {
      return validation("pipeline node '" + node.id + "' has no OperatorContract binding");
    }
    const auto& contract = binding->contract;
    if (contract.operator_id() != node.operator_id) {
      return validation("OperatorContract operator id does not match node '" + node.id + "'");
    }
    if (!contract.supports(profile)) {
      return validation("OperatorContract for node '" + node.id + "' does not support the requested execution profile");
    }
    if (!is_memory_domain_allowed(request.machine_policy, contract.resources().memory_domain)) {
      return validation("OperatorContract memory domain is forbidden by MachinePolicy for node '" + node.id + "'");
    }
    if (profile == ExecutionProfile::bounded_online && contract.resources().external_allocation_charged_bytes != 0U) {
      return validation("bounded-online planning rejects unaccounted Provider external allocation for node '" +
                        node.id + "'");
    }

    const auto* provider = find_provider(request.resolved_pipeline, node.provider_alias);
    if (provider == nullptr) {
      return validation("ResolvedPipeline is missing Provider alias '" + node.provider_alias + "'");
    }
    if (provider->abi_major != contract.provider_abi_major()) {
      return validation("Provider ABI major does not match OperatorContract for node '" + node.id + "'");
    }
    const auto* resolved_operator = find_operator(*provider, node.operator_id);
    if (resolved_operator == nullptr || resolved_operator->contract_digest != binding->contract_digest) {
      return validation("ResolvedPipeline contract digest does not match loaded OperatorContract for node '" + node.id +
                        "'");
    }
  }
  const auto graph_ports = validate_graph_ports_independently(request);
  if (!graph_ports.ok()) {
    return graph_ports;
  }
  return validate_m37_buffer_topology_independently(request);
}

[[nodiscard]] const CalibrationBinding* find_calibration_binding(const PipelineDefinition& definition,
                                                                 const std::string_view binding_id) {
  const auto found = std::ranges::find(definition.calibration_bindings(), binding_id, &CalibrationBinding::id);
  return found == definition.calibration_bindings().end() ? nullptr : &*found;
}

[[nodiscard]] Status validate_calibration_bindings_independently(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  for (const auto& node : definition.nodes()) {
    const auto* binding = find_binding(request, node.id);
    if (binding == nullptr) {
      return validation("pipeline node '" + node.id + "' has no OperatorContract binding");
    }
    const auto& calibration = binding->contract.calibration();
    if (calibration.role == CalibrationRole::none) {
      continue;
    }

    const auto* graph_binding = find_calibration_binding(definition, calibration.binding_id);
    if (graph_binding == nullptr) {
      return validation("node '" + node.id + "' declares an unknown calibration binding '" + calibration.binding_id +
                        "'");
    }
    if (calibration.role == CalibrationRole::producer && graph_binding->producer.node != node.id) {
      return validation("calibration producer node does not match OperatorContract binding for '" + node.id + "'");
    }
    if (calibration.role == CalibrationRole::producer) {
      const auto* producer_port = find_port(binding->contract, graph_binding->producer.port);
      if (producer_port == nullptr || producer_port->direction != PortDirection::output ||
          type_reference(producer_port->type_descriptor) != "ksj.calibration-material/v1") {
        return validation("calibration producer binding must reference a ksj.calibration-material/v1 output port");
      }
    }
    if (calibration.role == CalibrationRole::consumer && !contains(graph_binding->consumer_nodes, node.id)) {
      return validation("calibration consumer node does not match OperatorContract binding for '" + node.id + "'");
    }
    if (request.requested_profile != ExecutionProfile::bounded_online ||
        calibration.role != CalibrationRole::consumer) {
      continue;
    }
    if (calibration.precalibration_horizon_items == 0U || calibration.precalibration_horizon_charged_bytes == 0U ||
        request.target_envelope.calibration_horizon_items() == 0U ||
        request.target_envelope.calibration_horizon_charged_bytes() == 0U) {
      return validation("bounded-online calibration consumer '" + node.id +
                        "' requires finite item and byte progress horizons");
    }
    if (calibration.precalibration_horizon_items > request.target_envelope.calibration_horizon_items() ||
        calibration.precalibration_horizon_charged_bytes >
          request.target_envelope.calibration_horizon_charged_bytes()) {
      return validation("calibration horizon exceeds TargetEnvelope for node '" + node.id + "'");
    }
  }
  return Status::Ok();
}

[[nodiscard]] std::optional<EncodingLimitDimension> limit_dimension_for(const std::string_view field) {
  if (field == "average")
    return EncodingLimitDimension::average;
  if (field == "slice")
    return EncodingLimitDimension::slice;
  if (field == "contrast")
    return EncodingLimitDimension::contrast;
  if (field == "phase")
    return EncodingLimitDimension::phase;
  if (field == "repetition")
    return EncodingLimitDimension::repetition;
  if (field == "set")
    return EncodingLimitDimension::set;
  if (field == "segment")
    return EncodingLimitDimension::segment;
  return std::nullopt;
}

// Keep this list separate from the compiler's helper.  The verifier must
// independently reject XML axes that the active FrameSemanticKey cannot turn
// into an M3 ordinal, including `segment`.
[[nodiscard]] bool is_m3_frame_context_projection_field_independently(const std::string_view field) noexcept {
  return field == "encoding" || field == "average" || field == "slice" || field == "contrast" || field == "phase" ||
         field == "repetition" || field == "set";
}

inline constexpr std::array<std::string_view, 7U> kIndependentM3FrameSemanticKeyAxes{
  "encoding", "average", "slice", "contrast", "phase", "repetition", "set",
};

[[nodiscard]] Result<DenseKeySlotDimensionSpec>
derive_channel_group_dimension_independently(const OperatorExecutionShapeSpec& execution, const ScanDescriptor& scan,
                                             const TargetEnvelope& envelope) {
  if (!execution.channel_group.has_value()) {
    return validation("channel_group partition requires ChannelGroupSpec");
  }
  const auto& grouping = *execution.channel_group;
  if (grouping.max_active_channels > envelope.max_active_channels() ||
      grouping.max_groups > envelope.max_channel_groups()) {
    return validation("ChannelGroupSpec exceeds TargetEnvelope channel bounds");
  }
  if (!scan.declared_receiver_channels().has_value()) {
    return validation("dense-mixed-radix/v1 KeySlotTable requires XML-declared receiverChannels for channel_group; "
                      "envelope-backed dynamic/sparse domains are unsupported");
  }
  const auto channels = *scan.declared_receiver_channels();
  if (channels > grouping.max_active_channels || channels > envelope.max_active_channels()) {
    return validation("ScanDescriptor receiver channel count exceeds ChannelGroupSpec or TargetEnvelope");
  }
  auto group_count = checked_ceil_divide(channels, grouping.channels_per_group, "channel_group cardinality");
  if (!group_count.ok()) {
    return group_count.status();
  }
  if (group_count.value() > grouping.max_groups || group_count.value() > envelope.max_channel_groups()) {
    return validation("ScanDescriptor requires more channel groups than the declared bounds");
  }
  return DenseKeySlotDimensionSpec{.field = "channel_group", .minimum = 0U, .cardinality = group_count.value()};
}

[[nodiscard]] Result<DenseKeySlotDimensionSpec>
derive_dense_key_dimension_independently(const std::string_view field, const OperatorExecutionShapeSpec& execution,
                                         const ScanDescriptor& scan, const TargetEnvelope& envelope) {
  if (field == "scan") {
    return DenseKeySlotDimensionSpec{.field = std::string(field), .minimum = 0U, .cardinality = 1U};
  }
  if (field == "encoding") {
    return DenseKeySlotDimensionSpec{
      .field = std::string(field), .minimum = 0U, .cardinality = static_cast<Quantity>(scan.encodings().size())};
  }
  if (field == "acquisition_ordinal") {
    return validation("acquisition_ordinal requires a dynamic/sparse KeySlot domain; only XML-derived fixed dense "
                      "domains are supported by dense-mixed-radix/v1");
  }
  if (field == "channel_group") {
    return derive_channel_group_dimension_independently(execution, scan, envelope);
  }

  const auto dimension = limit_dimension_for(field);
  if (!dimension.has_value()) {
    return validation("unsupported non-dense or unknown v1 IndexProjection field '" + std::string(field) + "'");
  }

  std::optional<IndexLimit> common_limit;
  for (std::size_t index = 0; index < scan.encodings().size(); ++index) {
    const auto& encoding = scan.encodings()[index];
    const auto& limit = encoding.limits().at(*dimension);
    if (!limit.has_value()) {
      return validation("partition key '" + std::string(field) + "' is absent from ISMRMRD XML encoding[" +
                        std::to_string(index) + "]; envelope-backed dynamic/sparse KeySlot domains are unsupported");
    }
    if (!common_limit.has_value()) {
      common_limit = *limit;
    } else if (limit->minimum() != common_limit->minimum() || limit->cardinality() != common_limit->cardinality()) {
      return validation("partition key '" + std::string(field) +
                        "' has non-uniform ISMRMRD XML bounds across encodings; rectangular dense-mixed-radix/v1 "
                        "domains cannot encode sparse per-encoding domains");
    }
  }
  if (!common_limit.has_value()) {
    return validation("dense-mixed-radix/v1 KeySlotTable cannot derive a key dimension from an empty ScanDescriptor");
  }
  return DenseKeySlotDimensionSpec{
    .field = std::string(field), .minimum = common_limit->minimum(), .cardinality = common_limit->cardinality()};
}

[[nodiscard]] Result<VerifierDenseKeyDomain>
derive_dense_key_domain_independently(const OperatorExecutionShapeSpec& execution, const ScanDescriptor& scan,
                                      const TargetEnvelope& envelope) {
  VerifierDenseKeyDomain result;
  result.dimensions.reserve(execution.partition_key.size());
  for (const auto& field : execution.partition_key) {
    auto dimension = derive_dense_key_dimension_independently(field, execution, scan, envelope);
    if (!dimension.ok()) {
      return dimension.status();
    }
    auto cardinality = multiply(result.cardinality, dimension.value().cardinality,
                                "independent dense KeySlotTable mixed-radix key-domain cardinality");
    if (!cardinality.ok()) {
      return cardinality.status();
    }
    result.cardinality = cardinality.value();
    result.dimensions.push_back(std::move(dimension).value());
  }
  return result;
}

[[nodiscard]] Status require_cartesian_reorder_scan_independently(const ScanDescriptor& scan) {
  if (scan.encodings().empty()) {
    return validation("M3 ReorderPlan requires at least one Cartesian ISMRMRD encoding.");
  }
  for (std::size_t index = 0U; index < scan.encodings().size(); ++index) {
    if (scan.encodings()[index].trajectory() != TrajectoryType::cartesian) {
      return validation("M3 ReorderPlan supports only Cartesian ISMRMRD encodings; encoding[" + std::to_string(index) +
                        "] is not Cartesian.");
    }
  }
  return Status::Ok();
}

// Independently reconstruct the only M3 ordinal source.  The verifier must
// reject a plan whose Provider contract could map acquisition firings or
// batched work to ordinals without a completed FrameSlotContext semantic key.
[[nodiscard]] Status require_m3_completed_frame_slot_binding_independently(const OperatorContract& contract,
                                                                           const ReorderSpec& reorder) {
  const auto& execution = contract.execution();
  const auto& batch = contract.batch();
  if (execution.input_granularity != InputGranularity::frame || execution.max_items_per_activation != 1U ||
      batch.min_items != 1U || batch.preferred_items != 1U || batch.max_items != 1U) {
    return validation("independent M3 ReorderPlan requires one completed FrameSlotContext per activation and batch.");
  }
  const auto* frame_port = find_port(contract, reorder.completed_frame_input_port);
  auto completed_frame_type = completed_frame_slot_context_type();
  if (!completed_frame_type.ok()) {
    return completed_frame_type.status();
  }
  if (frame_port == nullptr || frame_port->direction != PortDirection::input ||
      !frame_port->type_descriptor.exactly_matches(completed_frame_type.value())) {
    return validation("independent M3 ReorderPlan completed_frame_input_port must exactly match the frozen "
                      "ksj.kspace-frame FrameSlotContext TypeDescriptor ABI.");
  }
  const auto& rates = contract.rates();
  if (rates.kind != RateKind::sdf || rates.static_phases.size() != 1U) {
    return validation("independent M3 ReorderPlan requires a single SDF frame-to-output phase.");
  }
  const auto& phase_inputs = rates.static_phases.front().inputs;
  const auto frame_rate = std::ranges::find(phase_inputs, reorder.completed_frame_input_port, &PortRateSpec::port_name);
  if (phase_inputs.size() != 1U || frame_rate == phase_inputs.end() || frame_rate->items != 1U) {
    return validation("independent M3 ReorderPlan requires exactly one completed FrameSlot input rate.");
  }
  if (execution.channel_group.has_value() || contains(execution.partition_key, std::string_view{"channel_group"})) {
    return validation("independent M3 ReorderPlan forbids channel_group in its completed FrameSlotContext ordinal "
                      "identity.");
  }
  if (execution.partition_key != reorder.order_projection) {
    return validation("independent M3 ReorderPlan requires execution.partition_key to exactly equal order_projection.");
  }
  return Status::Ok();
}

// This is intentionally separate from the compiler's routine.  It proves
// injectivity over the FrameSlotContext fields that can vary according to
// pre-admission XML; it does not claim that XML ranges prove every tuple will
// occur in the stream (RA-01 remains a runtime EOI obligation).
[[nodiscard]] Status require_independent_complete_frame_projection(const ReorderSpec& reorder,
                                                                   const ScanDescriptor& scan) {
  for (const auto axis : kIndependentM3FrameSemanticKeyAxes) {
    if (contains(reorder.order_projection, axis)) {
      continue;
    }
    if (axis == "encoding") {
      if (scan.encodings().size() != 1U) {
        return validation("independent M3 ReorderPlan order_projection must include varying FrameSlotContext axis "
                          "'encoding'.");
      }
      continue;
    }

    const auto xml_dimension = limit_dimension_for(axis);
    if (!xml_dimension.has_value()) {
      return validation("independent internal error: FrameSlotContext axis is missing its XML dimension.");
    }
    std::optional<Quantity> fixed_value;
    for (std::size_t index = 0U; index < scan.encodings().size(); ++index) {
      const auto& limit = scan.encodings()[index].limits().at(*xml_dimension);
      if (!limit.has_value()) {
        return validation("independent M3 ReorderPlan cannot prove omitted FrameSlotContext axis '" +
                          std::string(axis) + "' is singleton: absent from ISMRMRD XML encoding[" +
                          std::to_string(index) + "].");
      }
      if (limit->cardinality() != 1U) {
        return validation("independent M3 ReorderPlan order_projection must include varying FrameSlotContext axis '" +
                          std::string(axis) + "'.");
      }
      if (fixed_value.has_value() && *fixed_value != limit->minimum()) {
        return validation("independent M3 ReorderPlan cannot omit FrameSlotContext axis '" + std::string(axis) +
                          "': singleton XML values differ across encodings.");
      }
      fixed_value = limit->minimum();
    }
  }
  return Status::Ok();
}

[[nodiscard]] Result<Quantity> derive_m3_ordered_output_charged_bytes_independently(const OperatorContract& contract,
                                                                                    const ReorderSpec& reorder) {
  if (reorder.outputs_per_ordinal != 1U) {
    return validation("independent M3 ReorderPlan requires one OutputEnvelope per ordinal.");
  }
  const auto* port = find_port(contract, reorder.ordered_output_port);
  if (port == nullptr || port->direction != PortDirection::output) {
    return validation("independent M3 ReorderPlan ordered_output_port must name a declared output port.");
  }

  std::optional<Quantity> common_bytes;
  const auto inspect_phase = [&](const std::vector<PortRateSpec>& outputs,
                                 const std::string_view phase_name) -> Status {
    const auto output = std::ranges::find(outputs, reorder.ordered_output_port, &PortRateSpec::port_name);
    if (output == outputs.end()) {
      return validation("independent M3 ReorderPlan ordered_output_port is missing from " + std::string(phase_name) +
                        ".");
    }
    if (output->items != 1U || output->charged_bytes == 0U) {
      return validation("independent M3 ReorderPlan selected output must be one positive-byte OutputEnvelope in " +
                        std::string(phase_name) + ".");
    }
    if (common_bytes.has_value() && *common_bytes != output->charged_bytes) {
      return validation("independent M3 ReorderPlan selected output byte bound varies across ordinary phases.");
    }
    common_bytes = output->charged_bytes;
    return Status::Ok();
  };

  const auto& rates = contract.rates();
  if (rates.kind == RateKind::keyed_dynamic) {
    auto ordinary = inspect_phase(rates.ordinary.outputs, "rates.ordinary.outputs");
    if (!ordinary.ok()) {
      return ordinary;
    }
    const auto terminal =
      std::ranges::find(rates.normal_flush.outputs, reorder.ordered_output_port, &PortRateSpec::port_name);
    if (terminal != rates.normal_flush.outputs.end()) {
      return validation("independent M3 ReorderPlan selected output must not be emitted by normal_flush.");
    }
  } else {
    for (std::size_t index = 0U; index < rates.static_phases.size(); ++index) {
      auto phase =
        inspect_phase(rates.static_phases[index].outputs, "rates.static_phases[" + std::to_string(index) + "].outputs");
      if (!phase.ok()) {
        return phase;
      }
    }
  }
  if (!common_bytes.has_value()) {
    return validation("independent M3 ReorderPlan could not derive a selected output byte bound.");
  }
  return *common_bytes;
}

[[nodiscard]] Result<DenseCartesianOrdinalDimensionSpec>
derive_cartesian_ordinal_dimension_independently(const std::string_view field, const ScanDescriptor& scan) {
  if (!is_m3_frame_context_projection_field_independently(field)) {
    return validation("M3 ReorderPlan order_projection field '" + std::string(field) +
                      "' is not representable by the current FrameSemanticKey/FrameSlotContext.");
  }
  if (field == "encoding") {
    return DenseCartesianOrdinalDimensionSpec{
      .field = std::string(field), .minimum = 0U, .cardinality = static_cast<Quantity>(scan.encodings().size())};
  }

  const auto dimension = limit_dimension_for(field);
  if (!dimension.has_value()) {
    return validation("M3 ReorderPlan order_projection field '" + std::string(field) +
                      "' is not a fixed Cartesian ISMRMRD XML index field.");
  }

  std::optional<IndexLimit> common_limit;
  for (std::size_t index = 0U; index < scan.encodings().size(); ++index) {
    const auto& limit = scan.encodings()[index].limits().at(*dimension);
    if (!limit.has_value()) {
      return validation("M3 ReorderPlan order_projection field '" + std::string(field) +
                        "' is absent from ISMRMRD XML encoding[" + std::to_string(index) + "].");
    }
    if (!common_limit.has_value()) {
      common_limit = *limit;
    } else if (limit->minimum() != common_limit->minimum() || limit->cardinality() != common_limit->cardinality()) {
      return validation("M3 ReorderPlan order_projection field '" + std::string(field) +
                        "' has ragged ISMRMRD XML bounds across encodings; dense Cartesian ordinals require a "
                        "rectangular domain.");
    }
  }
  if (!common_limit.has_value()) {
    return validation("M3 ReorderPlan cannot derive a Cartesian ordinal dimension from an empty ScanDescriptor.");
  }
  return DenseCartesianOrdinalDimensionSpec{
    .field = std::string(field), .minimum = common_limit->minimum(), .cardinality = common_limit->cardinality()};
}

[[nodiscard]] Result<VerifierCartesianOrdinalDomain>
derive_cartesian_ordinal_domain_independently(const ReorderSpec& reorder, const ScanDescriptor& scan) {
  const auto cartesian = require_cartesian_reorder_scan_independently(scan);
  if (!cartesian.ok()) {
    return cartesian;
  }
  const auto complete_projection = require_independent_complete_frame_projection(reorder, scan);
  if (!complete_projection.ok()) {
    return complete_projection;
  }

  VerifierCartesianOrdinalDomain result;
  result.dimensions.reserve(reorder.order_projection.size());
  for (const auto& field : reorder.order_projection) {
    auto dimension = derive_cartesian_ordinal_dimension_independently(field, scan);
    if (!dimension.ok()) {
      return dimension.status();
    }
    auto cardinality = multiply(result.cardinality, dimension.value().cardinality,
                                "independent M3 dense Cartesian ReorderPlan ordinal-domain cardinality");
    if (!cardinality.ok()) {
      return cardinality.status();
    }
    result.cardinality = cardinality.value();
    result.dimensions.push_back(std::move(dimension).value());
  }
  return result;
}

[[nodiscard]] Status require_independent_m3_table_alignment(const KeySlotTablePlanSpec& table,
                                                            const ReorderPlanSpec& reorder) {
  if (table.node_id != reorder.node_id || table.key_domain_bound != reorder.ordinal_domain_bound ||
      table.dense_dimensions.size() != reorder.ordinal_dimensions.size()) {
    return validation("independent M3 ReorderPlan must equal its node-owned KeySlotTable dense domain.");
  }
  for (std::size_t dimension = 0U; dimension < table.dense_dimensions.size(); ++dimension) {
    const auto& key_dimension = table.dense_dimensions[dimension];
    const auto& ordinal_dimension = reorder.ordinal_dimensions[dimension];
    if (key_dimension.field != ordinal_dimension.field || key_dimension.minimum != ordinal_dimension.minimum ||
        key_dimension.cardinality != ordinal_dimension.cardinality) {
      return validation("independent M3 ReorderPlan dimensions must exactly equal KeySlotTable dimensions.");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Result<ReorderPlanSpec> derive_cartesian_reorder_plan_independently(const PipelineNode& node,
                                                                                  const OperatorContract& contract,
                                                                                  const ScanDescriptor& scan) {
  if (!contract.reorder().has_value()) {
    return validation("independent internal error: Cartesian ReorderPlan requested without ReorderSpec");
  }
  if (contract.resources().memory_domain != MemoryDomain::host) {
    return validation("M3 ReorderPlan currently requires host memory-domain ownership for node '" + node.id + "'.");
  }

  const auto& reorder = *contract.reorder();
  const auto frame_binding = require_m3_completed_frame_slot_binding_independently(contract, reorder);
  if (!frame_binding.ok()) {
    return frame_binding;
  }
  auto charged_bytes_per_ordinal = derive_m3_ordered_output_charged_bytes_independently(contract, reorder);
  if (!charged_bytes_per_ordinal.ok()) {
    return charged_bytes_per_ordinal.status();
  }
  auto domain = derive_cartesian_ordinal_domain_independently(reorder, scan);
  if (!domain.ok()) {
    return domain.status();
  }
  if (reorder.max_ahead_items > domain.value().cardinality) {
    return validation("M3 ReorderPlan max_ahead_items exceeds its dense Cartesian ordinal domain for node '" + node.id +
                      "'.");
  }
  auto required_ahead_bytes = checked_multiply(reorder.max_ahead_items, charged_bytes_per_ordinal.value(),
                                               "independent M3 ReorderPlan full OutputEnvelope reservation");
  if (!required_ahead_bytes.ok()) {
    return required_ahead_bytes.status();
  }
  if (reorder.max_ahead_charged_bytes < required_ahead_bytes.value()) {
    return validation("independent M3 ReorderPlan max_ahead_charged_bytes does not cover all full OutputEnvelope "
                      "reservations for node '" +
                      node.id + "'.");
  }
  auto metadata = derive_m3_host_metadata_independently(domain.value().cardinality, reorder.max_ahead_items,
                                                        "independent node '" + node.id + "' ReorderPlan host metadata");
  if (!metadata.ok()) {
    return metadata.status();
  }
  auto handle_storage = checked_multiply(reorder.max_ahead_items, kVerifierM3ImmutableHandleSidecarChargedBytes,
                                         "independent M3 immutable handle sidecars");
  if (!handle_storage.ok()) {
    return handle_storage.status();
  }
  const auto ordinal_domain_bound = domain.value().cardinality;
  auto ordinal_dimensions = std::move(domain).value().dimensions;
  return ReorderPlanSpec{
    .node_id = node.id,
    // Keep the verifier's witness derivation structurally independent from
    // the compiler: M3 names one output-order domain for each graph node.
    .order_domain_id = node.id,
    .ordinal_binding_id = std::string(kCompletedFrameSlotContextSemanticKeyOrdinalBindingId),
    .completed_frame_input_port = reorder.completed_frame_input_port,
    .ordered_output_port = reorder.ordered_output_port,
    .outputs_per_ordinal = reorder.outputs_per_ordinal,
    .charged_bytes_per_ordinal = charged_bytes_per_ordinal.value(),
    .ordinal_dimensions = std::move(ordinal_dimensions),
    .ordinal_domain_bound = ordinal_domain_bound,
    .first_expected_ordinal = kFirstExpectedReorderOrdinal,
    .last_expected_ordinal = ordinal_domain_bound - 1U,
    .max_ahead_items = reorder.max_ahead_items,
    .max_ahead_charged_bytes = reorder.max_ahead_charged_bytes,
    .max_gap_ordinals = ordinal_domain_bound - 1U,
    .occurrence_policy = std::string(kStrictDenseAllTuplesReorderOccurrencePolicy),
    .publish_policy = std::string(kNextExpectedOnlyReorderPublishPolicy),
    .certified_skipped_ordinals = {},
    .end_of_input_policy = std::string(kFailReorderEndOfInputPolicy),
    .handle_storage_charged_bytes = handle_storage.value(),
    .host_metadata_charged_bytes = metadata.value(),
    .descriptor_charged_count = reorder.max_ahead_items,
  };
}

[[nodiscard]] Status add_reorder_plan_resources_independently(const ReorderPlanSpec& reorder,
                                                              ResourceVectorSpec& resource_vector) {
  auto metadata = add_to(resource_vector.host_normal_bytes, reorder.host_metadata_charged_bytes,
                         "independent dense Cartesian ReorderPlan host metadata reservation");
  if (!metadata.ok()) {
    return metadata;
  }
  // Physical ahead payload is added after all edges are independently
  // derived, so compatibility remains all-or-nothing: no M3.7 data edge
  // means legacy payload ownership; otherwise pools own payload exclusively.
  return add_to(resource_vector.descriptor_count, reorder.descriptor_charged_count,
                "independent dense Cartesian ReorderPlan ahead descriptor reservation");
}

[[nodiscard]] Result<VerifierOutputBound> output_bound_for_port_independently(const OperatorContract& contract,
                                                                              const std::string_view port_name) {
  VerifierOutputBound result;
  bool ordinary_declared = false;
  bool normal_terminal_declared = false;
  const auto incorporate_ordinary = [&](const std::vector<PortRateSpec>& outputs) {
    for (const auto& output : outputs) {
      if (output.port_name != port_name) {
        continue;
      }
      ordinary_declared = true;
      result.items = std::max(result.items, output.items);
      result.bytes = std::max(result.bytes, output.charged_bytes);
    }
  };

  const auto& rates = contract.rates();
  if (rates.kind == RateKind::keyed_dynamic) {
    incorporate_ordinary(rates.ordinary.outputs);
    for (const auto& output : rates.normal_flush.outputs) {
      if (output.port_name == port_name) {
        normal_terminal_declared = true;
      }
    }
  } else {
    for (const auto& phase : rates.static_phases) {
      incorporate_ordinary(phase.outputs);
    }
  }
  if (!ordinary_declared && !normal_terminal_declared) {
    return validation("OperatorContract does not declare an ordinary or normal-terminal output bound for port '" +
                      std::string(port_name) + "'");
  }
  return result;
}

[[nodiscard]] Result<EdgeCapacitySpec> derive_edge_capacity_independently(const PipelineEdge& edge,
                                                                          const OperatorContract& source) {
  auto output = output_bound_for_port_independently(source, edge.from.port);
  if (!output.ok()) {
    return output.status();
  }
  auto ordinary_items = multiply(output.value().items, source.execution().max_in_flight,
                                 "independent edge " + edge.id + " ordinary item capacity");
  if (!ordinary_items.ok()) {
    return ordinary_items.status();
  }
  auto ordinary_bytes = multiply(output.value().bytes, source.execution().max_in_flight,
                                 "independent edge " + edge.id + " ordinary byte capacity");
  if (!ordinary_bytes.ok()) {
    return ordinary_bytes.status();
  }
  const auto& terminal = source.terminal();
  auto items = checked_add(ordinary_items.value(), terminal.normal_max_output_items,
                           "independent edge " + edge.id + " item capacity");
  if (!items.ok()) {
    return items.status();
  }
  auto bytes = checked_add(ordinary_bytes.value(), terminal.normal_max_output_charged_bytes,
                           "independent edge " + edge.id + " byte capacity");
  if (!bytes.ok()) {
    return bytes.status();
  }
  if (items.value() == 0U || bytes.value() == 0U) {
    return validation("edge '" + edge.id + "' has an unbounded or zero derived capacity");
  }
  return EdgeCapacitySpec{.edge_id = edge.id, .max_items = items.value(), .max_charged_bytes = bytes.value()};
}

[[nodiscard]] Result<EdgeCapacitySpec> derive_m37_base_edge_capacity_independently(const PipelineEdge& edge,
                                                                                   const OperatorContract& source) {
  auto output = output_bound_for_port_independently(source, edge.from.port);
  if (!output.ok()) {
    return output.status();
  }
  auto items = multiply(output.value().items, source.execution().max_in_flight,
                        "independent M3.7 ordinary downstream item capacity");
  if (!items.ok()) {
    return items.status();
  }
  auto bytes = multiply(output.value().bytes, source.execution().max_in_flight,
                        "independent M3.7 ordinary downstream byte capacity");
  if (!bytes.ok()) {
    return bytes.status();
  }
  if (items.value() == 0U || bytes.value() == 0U) {
    return validation("independent M3.7 buffer handle has no ordinary downstream capacity.");
  }
  return EdgeCapacitySpec{.edge_id = edge.id, .max_items = items.value(), .max_charged_bytes = bytes.value()};
}

struct IndependentM37DataEdge {
  BufferPoolPlanSpec pool;
  DataEdgePlanSpec edge;
};

[[nodiscard]] Result<Quantity> independent_provider_output_index(const OperatorContract& contract,
                                                                 const std::string_view requested_port) {
  Quantity index = 0U;
  for (const auto& port : contract.ports()) {
    if (port.direction != PortDirection::output) {
      continue;
    }
    if (port.name == requested_port) {
      if (index > kM37MaximumProducerAbiPort) {
        return validation("independent M3.7 Provider output index does not fit uint32.");
      }
      return index;
    }
    auto successor = checked_add(index, 1U, "independent M3.7 Provider output index");
    if (!successor.ok()) {
      return successor.status();
    }
    index = successor.value();
  }
  return validation("independent M3.7 could not find selected output in the declared OperatorContract output order.");
}

[[nodiscard]] Result<IndependentM37DataEdge>
derive_m37_data_edge_independently(const PipelineEdge& graph_edge, const ResolvedProvider& source_provider,
                                   const OperatorContractBinding& source_binding, const PortSpec& source_port,
                                   const std::vector<ReorderPlanSpec>& independently_derived_reorders) {
  const auto& contract = source_binding.contract;
  const auto reorder = std::ranges::find_if(independently_derived_reorders, [&](const ReorderPlanSpec& candidate) {
    return candidate.node_id == graph_edge.from.node && candidate.ordered_output_port == graph_edge.from.port;
  });
  if (reorder == independently_derived_reorders.end()) {
    return validation("independent M3.7 cannot attach a DataEdgePlan without its producer ReorderPlan.");
  }
  auto base_capacity = derive_m37_base_edge_capacity_independently(graph_edge, contract);
  if (!base_capacity.ok()) {
    return base_capacity.status();
  }
  auto full_items = checked_add(base_capacity.value().max_items, reorder->max_ahead_items,
                                "independent M3.7 downstream plus reorder-ahead credits");
  if (!full_items.ok()) {
    return full_items.status();
  }
  auto logical_bytes =
    checked_multiply(full_items.value(), reorder->charged_bytes_per_ordinal, "independent M3.7 logical edge bytes");
  if (!logical_bytes.ok()) {
    return logical_bytes.status();
  }
  auto base_logical = checked_multiply(base_capacity.value().max_items, reorder->charged_bytes_per_ordinal,
                                       "independent M3.7 ordinary downstream logical credit");
  if (!base_logical.ok()) {
    return base_logical.status();
  }
  if (base_capacity.value().max_charged_bytes != base_logical.value()) {
    return validation("independent M3.7 selected buffer output must have one ordinary byte envelope with no "
                      "terminal-output credit.");
  }
  const auto alignment = source_port.type_descriptor.min_alignment_bytes();
  if (reorder->charged_bytes_per_ordinal % alignment != 0U) {
    return validation("independent M3.7 pool payload capacity must be aligned to its frozen TypeDescriptor.");
  }
  auto per_slot =
    checked_add(reorder->charged_bytes_per_ordinal, 0U, "independent M3.7 pool payload plus metadata capacity");
  if (!per_slot.ok()) {
    return per_slot.status();
  }
  per_slot = checked_add(per_slot.value(), kVerifierM37PoolControlChargedBytesPerSlot,
                         "independent M3.7 pool payload plus control charge");
  if (!per_slot.ok()) {
    return per_slot.status();
  }
  auto physical = checked_multiply(full_items.value(), per_slot.value(), "independent M3.7 all fixed pool slabs");
  if (!physical.ok()) {
    return physical.status();
  }
  auto pool_metadata =
    checked_multiply(full_items.value(), kVerifierM37PoolControlChargedBytesPerSlot, "independent M3.7 pool metadata");
  if (!pool_metadata.ok()) {
    return pool_metadata.status();
  }
  auto edge_metadata = checked_multiply(full_items.value(), kVerifierM37DataEdgeControlChargedBytesPerItem,
                                        "independent M3.7 fixed edge control metadata");
  if (!edge_metadata.ok()) {
    return edge_metadata.status();
  }
  auto abi_port = independent_provider_output_index(contract, graph_edge.from.port);
  if (!abi_port.ok()) {
    return abi_port.status();
  }
  const auto pool_id = graph_edge.id + ".pool";
  return IndependentM37DataEdge{
    .pool = {.pool_id = pool_id,
             .producer_node_id = graph_edge.from.node,
             .producer_port_name = graph_edge.from.port,
             .producer_provider_id = source_provider.provider_id,
             .producer_bundle_digest = source_provider.bundle_digest.value(),
             .producer_operator_id = contract.operator_id(),
             .producer_contract_digest = source_binding.contract_digest.value(),
             .type_descriptor = source_port.type_descriptor,
             .memory_domain = TypeMemoryDomain::host_normal,
             .slot_count = full_items.value(),
             .payload_capacity_bytes = reorder->charged_bytes_per_ordinal,
             .metadata_capacity_bytes = 0U,
             .payload_alignment_bytes = alignment,
             .storage_accounting_id = std::string(kM37BufferPoolStorageAccountingId),
             .host_metadata_charged_bytes = pool_metadata.value(),
             .descriptor_charged_count = full_items.value(),
             .physical_charge_bytes = physical.value()},
    .edge = {.edge_id = graph_edge.id,
             .source_pool_id = pool_id,
             .producer_node_id = graph_edge.from.node,
             .producer_port_name = graph_edge.from.port,
             .producer_abi_port = abi_port.value(),
             .consumer_node_id = graph_edge.to.node,
             .consumer_port_name = graph_edge.to.port,
             .type_descriptor = source_port.type_descriptor,
             .max_items = full_items.value(),
             .max_logical_bytes = logical_bytes.value(),
             .storage_accounting_id = std::string(kM37DataEdgeStorageAccountingId),
             .host_metadata_charged_bytes = edge_metadata.value(),
             .descriptor_charged_count = full_items.value(),
             .firing_lease_staging_charged_bytes = kVerifierM37FiringLeaseHostStagingChargedBytes,
             .firing_lease_staging_descriptor_count = kVerifierM37FiringLeaseHostStagingDescriptorCount,
             .terminal_policy = std::string(kM37NormalEoiDrainCancellationFailTerminalPolicy)},
  };
}

[[nodiscard]] Result<Quantity*> host_memory_bucket_independently(ResourceVectorSpec& vector,
                                                                 const MemoryDomain domain) {
  switch (domain) {
    case MemoryDomain::host:
      return &vector.host_normal_bytes;
    case MemoryDomain::pinned_host:
      return &vector.host_pinned_bytes;
    case MemoryDomain::shared:
      return &vector.shared_host_bytes;
    case MemoryDomain::device:
      return validation("M0 verifier cannot plan a device-memory OperatorContract without a selected device variant");
  }
  return validation("OperatorContract has an invalid memory domain");
}

[[nodiscard]] Status add_contract_resources_independently(const OperatorContract& contract,
                                                          const KeySlotTablePlanSpec& table,
                                                          const std::optional<VerifierOutputBound>& pool_owns_output,
                                                          ResourceVectorSpec& vector) {
  const auto& resources = contract.resources();
  auto bucket = host_memory_bucket_independently(vector, resources.memory_domain);
  if (!bucket.ok()) {
    return bucket.status();
  }
  const auto add = [&](const Quantity value, const std::string_view name) {
    return add_to(*bucket.value(), value, name);
  };

  auto status = add(resources.per_scan_workspace_charged_bytes, "independent per-scan workspace");
  if (!status.ok()) {
    return status;
  }
  auto keyed_state =
    multiply(resources.per_key_state_charged_bytes, table.max_live_keys, "independent per-key state reservation");
  if (!keyed_state.ok()) {
    return keyed_state.status();
  }
  status = add(keyed_state.value(), "independent per-key state");
  if (!status.ok()) {
    return status;
  }
  auto scratch = multiply(resources.scratch_charged_bytes_per_firing, contract.execution().max_in_flight,
                          "independent scratch reservation");
  if (!scratch.ok()) {
    return scratch.status();
  }
  status = add(scratch.value(), "independent scratch");
  if (!status.ok()) {
    return status;
  }
  auto output = multiply(resources.output_charged_bytes, contract.execution().max_in_flight,
                         "independent in-flight output buffer reservation");
  if (!output.ok()) {
    return output.status();
  }
  auto ordinary_descriptors = multiply(resources.output_items, contract.execution().max_in_flight,
                                       "independent in-flight output descriptor reservation");
  if (!ordinary_descriptors.ok()) {
    return ordinary_descriptors.status();
  }
  Quantity remaining_output_bytes = output.value();
  Quantity remaining_output_descriptors = ordinary_descriptors.value();
  if (pool_owns_output.has_value()) {
    auto selected_bytes = multiply(pool_owns_output->bytes, contract.execution().max_in_flight,
                                   "independent M3.7 selected output charge replaced by pool");
    if (!selected_bytes.ok()) {
      return selected_bytes.status();
    }
    auto selected_descriptors = multiply(pool_owns_output->items, contract.execution().max_in_flight,
                                         "independent M3.7 selected output descriptor replaced by pool");
    if (!selected_descriptors.ok()) {
      return selected_descriptors.status();
    }
    if (selected_bytes.value() > remaining_output_bytes ||
        selected_descriptors.value() > remaining_output_descriptors) {
      return validation("independent M3.7 selected output exceeds aggregate Provider output reservation.");
    }
    remaining_output_bytes -= selected_bytes.value();
    remaining_output_descriptors -= selected_descriptors.value();
  }
  status = add(remaining_output_bytes, "independent in-flight output buffers after pool replacement");
  if (!status.ok()) {
    return status;
  }
  status = add(resources.retention_charged_bytes, "independent retention");
  if (!status.ok()) {
    return status;
  }
  if (contract.join().has_value()) {
    status = add(contract.join()->max_retained_charged_bytes_aggregate, "independent join retention");
    if (!status.ok()) {
      return status;
    }
  }
  // M3 reorder retention is re-derived from the frozen ReorderPlan below,
  // including its dense ordinal metadata and descriptor charge.  Do not trust
  // an implicit Provider-contract-only reservation here.

  const auto& calibration = contract.calibration();
  if (calibration.role == CalibrationRole::consumer) {
    auto calibration_wait = multiply(calibration.max_active_keys, calibration.precalibration_horizon_charged_bytes,
                                     "independent calibration progress reservoir");
    if (!calibration_wait.ok()) {
      return calibration_wait.status();
    }
    status = add(calibration_wait.value(), "independent calibration progress reservoir");
    if (!status.ok()) {
      return status;
    }
    status = add(calibration.max_calibration_frame_charged_bytes, "independent calibration frame");
    if (!status.ok()) {
      return status;
    }
    status =
      add_to(vector.transport_bytes, calibration.max_decoder_staging_bytes, "independent calibration decoder staging");
    if (!status.ok()) {
      return status;
    }
  }

  const auto terminal_bytes = contract.terminal().normal_max_output_charged_bytes;
  status = add(terminal_bytes, "independent terminal output bundle");
  if (!status.ok()) {
    return status;
  }
  status = add_to(vector.descriptor_count, remaining_output_descriptors,
                  "independent in-flight output descriptors after pool replacement");
  if (!status.ok()) {
    return status;
  }
  const auto terminal_descriptors = contract.terminal().normal_max_output_items;
  status = add_to(vector.descriptor_count, terminal_descriptors, "independent terminal output descriptors");
  if (!status.ok()) {
    return status;
  }
  const auto terminal_async =
    std::max(contract.terminal().normal_max_async_tokens, contract.terminal().cancel_max_async_tokens);
  return add_to(vector.async_token_count, terminal_async, "independent terminal async token reservation");
}

[[nodiscard]] Status add_contract_permits_independently(const OperatorContract& contract, ResourceVectorSpec& vector) {
  const auto& resources = contract.resources();
  const auto concurrency = contract.execution().max_in_flight;
  auto executor = multiply(resources.cpu_permits, concurrency, "independent executor permit reservation");
  if (!executor.ok()) {
    return executor.status();
  }
  auto backend = multiply(resources.backend_gang_threads, concurrency, "independent backend permit reservation");
  if (!backend.ok()) {
    return backend.status();
  }
  auto provider = multiply(resources.provider_private_threads, concurrency, "independent Provider permit reservation");
  if (!provider.ok()) {
    return provider.status();
  }
  auto status = add_to(vector.cpu_leaf_permits, executor.value(), "independent executor permit total");
  if (!status.ok()) {
    return status;
  }
  status = add_to(vector.backend_gang_permits, backend.value(), "independent backend permit total");
  if (!status.ok()) {
    return status;
  }
  return add_to(vector.provider_private_permits, provider.value(), "independent Provider permit total");
}

[[nodiscard]] Result<Quantity> terminal_occurrence_bound_independently(const OperatorContract& contract) {
  auto count = checked_add(1U, 1U, "independent normal and cancellation terminal transitions");
  if (!count.ok()) {
    return count.status();
  }
  count = checked_add(count.value(), contract.rates().normal_flush.max_firings,
                      "independent normal terminal flush occurrence count");
  if (!count.ok()) {
    return count.status();
  }
  count = checked_add(count.value(), contract.terminal().normal_max_async_tokens,
                      "independent normal terminal async occurrence count");
  if (!count.ok()) {
    return count.status();
  }
  return checked_add(count.value(), contract.terminal().cancel_max_async_tokens,
                     "independent cancellation terminal async occurrence count");
}

[[nodiscard]] Result<IndependentlyDerivedPlan> derive_plan_independently(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  IndependentlyDerivedPlan result;
  result.key_slot_tables.reserve(definition.nodes().size());
  result.reorder_plans.reserve(definition.nodes().size());
  result.buffer_pool_plans.reserve(definition.edges().size());
  result.data_edge_plans.reserve(definition.edges().size());

  for (const auto& node : definition.nodes()) {
    const auto* binding = find_binding(request, node.id);
    if (binding == nullptr) {
      return validation("pipeline node '" + node.id + "' has no OperatorContract binding");
    }
    const auto& contract = binding->contract;
    auto domain =
      derive_dense_key_domain_independently(contract.execution(), request.scan_descriptor, request.target_envelope);
    if (!domain.ok()) {
      return domain.status();
    }
    const auto max_live_keys = std::min(domain.value().cardinality, contract.execution().max_active_keys);
    if (max_live_keys == 0U) {
      return validation("node '" + node.id + "' has a zero derived KeySlotTable capacity");
    }
    auto host_metadata = dense_key_slot_host_metadata_charged_bytes(
      domain.value().cardinality, max_live_keys, "independent node '" + node.id + "' KeySlotTable host metadata");
    if (!host_metadata.ok()) {
      return host_metadata.status();
    }
    const auto activation_items = std::min(contract.execution().max_items_per_activation, contract.batch().max_items);
    KeySlotTablePlanSpec table{
      .node_id = node.id,
      .dense_dimensions = domain.value().dimensions,
      .key_domain_bound = domain.value().cardinality,
      .max_distinct_keys = domain.value().cardinality,
      .max_live_keys = max_live_keys,
      .slot_count = max_live_keys,
      .host_metadata_charged_bytes = host_metadata.value(),
      .max_items_per_activation = activation_items,
      .max_charged_bytes_per_activation = contract.batch().max_charged_bytes,
    };
    auto metadata = add_to(result.resource_vector.host_normal_bytes, table.host_metadata_charged_bytes,
                           "independent dense KeySlotTable host metadata reservation");
    if (!metadata.ok()) {
      return metadata;
    }
    std::optional<VerifierOutputBound> pool_owns_output;
    if (independent_has_m37_pool_output(request, node.id, contract)) {
      auto selected = output_bound_for_port_independently(contract, contract.reorder()->ordered_output_port);
      if (!selected.ok()) {
        return selected.status();
      }
      pool_owns_output = selected.value();
    }
    auto resources = add_contract_resources_independently(contract, table, pool_owns_output, result.resource_vector);
    if (!resources.ok()) {
      return resources;
    }
    if (contract.reorder().has_value()) {
      auto reorder = derive_cartesian_reorder_plan_independently(node, contract, request.scan_descriptor);
      if (!reorder.ok()) {
        return reorder.status();
      }
      auto alignment = require_independent_m3_table_alignment(table, reorder.value());
      if (!alignment.ok()) {
        return alignment;
      }
      auto reorder_resources = add_reorder_plan_resources_independently(reorder.value(), result.resource_vector);
      if (!reorder_resources.ok()) {
        return reorder_resources;
      }
      result.reorder_plans.push_back(std::move(reorder).value());
    }
    auto permits = add_contract_permits_independently(contract, result.resource_vector);
    if (!permits.ok()) {
      return permits;
    }
    result.key_slot_tables.push_back(std::move(table));

    auto terminal = terminal_occurrence_bound_independently(contract);
    if (!terminal.ok()) {
      return terminal.status();
    }
    auto sum = add_to(result.terminal_occurrences, terminal.value(), "independent terminal occurrence count");
    if (!sum.ok()) {
      return sum;
    }
  }

  result.edge_capacities.reserve(definition.edges().size());
  for (const auto& edge : definition.edges()) {
    const auto* source = find_binding(request, edge.from.node);
    if (source == nullptr) {
      return validation("independent edge '" + edge.id + "' source has no OperatorContract binding");
    }
    const auto* source_port = find_port(source->contract, edge.from.port);
    auto completed = completed_frame_slot_context_type();
    if (!completed.ok()) {
      return completed.status();
    }
    const auto pool_backed_edge = source_port->type_descriptor.payload_kind() == PayloadKind::buffer_handle &&
                                  !source_port->type_descriptor.exactly_matches(completed.value());
    if (pool_backed_edge) {
      const auto* source_node = find_node(definition, edge.from.node);
      const auto* source_provider =
        source_node == nullptr ? nullptr : find_provider(request.resolved_pipeline, source_node->provider_alias);
      if (source_node == nullptr || source_provider == nullptr) {
        return validation("independent M3.7 edge '" + edge.id + "' source has no resolved Producer identity");
      }
      auto derived =
        derive_m37_data_edge_independently(edge, *source_provider, *source, *source_port, result.reorder_plans);
      if (!derived.ok()) {
        return derived.status();
      }
      auto plans = std::move(derived).value();
      auto physical = add_to(result.resource_vector.host_normal_bytes, plans.pool.physical_charge_bytes,
                             "independent M3.7 pool physical reservation");
      if (!physical.ok()) {
        return physical;
      }
      auto pool_descriptors = add_to(result.resource_vector.descriptor_count, plans.pool.descriptor_charged_count,
                                     "independent M3.7 pool descriptor reservation");
      if (!pool_descriptors.ok()) {
        return pool_descriptors;
      }
      auto control = add_to(result.resource_vector.host_normal_bytes, plans.edge.host_metadata_charged_bytes,
                            "independent M3.7 edge control reservation");
      if (!control.ok()) {
        return control;
      }
      auto edge_descriptors = add_to(result.resource_vector.descriptor_count, plans.edge.descriptor_charged_count,
                                     "independent M3.7 edge descriptor reservation");
      if (!edge_descriptors.ok()) {
        return edge_descriptors;
      }
      auto firing_staging =
        add_to(result.resource_vector.host_normal_bytes, plans.edge.firing_lease_staging_charged_bytes,
               "independent M3.7 firing-lease ABI staging reservation");
      if (!firing_staging.ok()) {
        return firing_staging;
      }
      auto firing_staging_descriptors =
        add_to(result.resource_vector.descriptor_count, plans.edge.firing_lease_staging_descriptor_count,
               "independent M3.7 firing-lease staging descriptors");
      if (!firing_staging_descriptors.ok()) {
        return firing_staging_descriptors;
      }
      result.buffer_pool_plans.push_back(std::move(plans.pool));
      result.data_edge_plans.push_back(std::move(plans.edge));
      continue;
    }
    auto capacity = derive_edge_capacity_independently(edge, source->contract);
    if (!capacity.ok()) {
      return capacity.status();
    }
    auto descriptor = add_to(result.resource_vector.descriptor_count, capacity.value().max_items,
                             "independent bounded edge descriptor reservation");
    if (!descriptor.ok()) {
      return descriptor;
    }
    result.edge_capacities.push_back(std::move(capacity).value());
  }

  if (result.data_edge_plans.empty()) {
    for (const auto& reorder : result.reorder_plans) {
      auto legacy_payload = add_to(result.resource_vector.host_normal_bytes, reorder.max_ahead_charged_bytes,
                                   "independent legacy ReorderPlan ahead payload reservation");
      if (!legacy_payload.ok()) {
        return legacy_payload;
      }
    }
  } else {
    for (const auto& reorder : result.reorder_plans) {
      const auto data_edge = std::ranges::find_if(result.data_edge_plans, [&](const DataEdgePlanSpec& edge) {
        return edge.producer_node_id == reorder.node_id && edge.producer_port_name == reorder.ordered_output_port;
      });
      if (data_edge == result.data_edge_plans.end()) {
        return validation("independent M3.7 rejects a mixed legacy opaque ReorderPlan; every selected reorder "
                          "output must have a DataEdgePlan.");
      }
    }
  }

  auto decoder = add_to(result.resource_vector.transport_bytes, request.target_envelope.max_decoder_staging_bytes(),
                        "independent decoder staging reservation");
  if (!decoder.ok()) {
    return decoder;
  }
  auto sink = add_to(result.resource_vector.transport_bytes,
                     request.target_envelope.sink_service_assumption().transport_staging_bytes(),
                     "independent host transport staging reservation");
  if (!sink.ok()) {
    return sink;
  }
  auto io = add_to(result.resource_vector.io_slots, 1U, "independent ingress/egress I/O slot reservation");
  if (!io.ok()) {
    return io;
  }

  std::ranges::sort(result.key_slot_tables, {}, &KeySlotTablePlanSpec::node_id);
  std::ranges::sort(result.reorder_plans, {}, &ReorderPlanSpec::node_id);
  std::ranges::sort(result.buffer_pool_plans, {}, &BufferPoolPlanSpec::pool_id);
  std::ranges::sort(result.data_edge_plans, {}, &DataEdgePlanSpec::edge_id);
  std::ranges::sort(result.edge_capacities, {}, &EdgeCapacitySpec::edge_id);
  return result;
}

[[nodiscard]] Status check_resource_capacity_independently(const ResourceVectorSpec& specification,
                                                           const MachinePolicy& policy) {
  auto demand = ResourceVector::create(specification, "independently derived resource_vector");
  if (!demand.ok()) {
    return demand.status();
  }
  if (!policy.resource_capacity().can_admit(demand.value())) {
    return validation(
      "independently derived ResourceVector exceeds MachinePolicy.resource_capacity in one or more domains");
  }
  return Status::Ok();
}

[[nodiscard]] ResourceVectorSpec resource_vector_spec(const ResourceVector& resources) {
  ResourceVectorSpec specification{
    .host_normal_bytes = resources.host_normal_bytes(),
    .host_pinned_bytes = resources.host_pinned_bytes(),
    .host_hugepage_bytes = resources.host_hugepage_bytes(),
    .shared_host_bytes = resources.shared_host_bytes(),
    .spool_bytes = resources.spool_bytes(),
    .transport_bytes = resources.transport_bytes(),
    .descriptor_count = resources.descriptor_count(),
    .async_token_count = resources.async_token_count(),
    .cpu_leaf_permits = resources.cpu_leaf_permits(),
    .backend_gang_permits = resources.backend_gang_permits(),
    .provider_private_permits = resources.provider_private_permits(),
    .io_slots = resources.io_slots(),
  };
  specification.devices.reserve(resources.devices().size());
  for (const auto& device : resources.devices()) {
    specification.devices.push_back({.device_id = device.device_id(),
                                     .device_bytes = device.device_bytes(),
                                     .gpu_stream_slots = device.gpu_stream_slots(),
                                     .copy_engine_slots = device.copy_engine_slots()});
  }
  return specification;
}

[[nodiscard]] std::vector<std::string> canonical_contract_digests(const PlanBuildRequest& request) {
  std::vector<std::string> digests;
  digests.reserve(request.operator_contracts.size());
  for (const auto& binding : request.operator_contracts) {
    digests.push_back(binding.contract_digest.value());
  }
  std::ranges::sort(digests);
  digests.erase(std::unique(digests.begin(), digests.end()), digests.end());
  return digests;
}

// This is intentionally local to the independent verifier rather than shared
// with the compiler's derivation.  It is a fixed v1 artifact requirement, and
// verification must reject a plan whose serialized proof set differs from the
// one covered by the independently reconstructed digest preimage.
[[nodiscard]] std::vector<std::string> expected_plan_proof_obligations(const IndependentlyDerivedPlan& expected) {
  std::vector<std::string> obligations{
    "PO-01.typed_ports",
    "PO-04.finite_bounds",
    "PO-05.resource_vector",
    "PO-06.dense_key_slots",
    "PO-08.bounded_dependency_progress",
    "PO-12.permit_budget",
  };
  if (!expected.reorder_plans.empty()) {
    obligations.insert(obligations.begin() + 4, std::string{kM3CompletedFrameSlotBindingProofObligation});
    obligations.insert(obligations.begin() + 5, std::string{kM3StrictDenseAllTuplesEoiRuntimeAssumption});
  }
  if (!expected.data_edge_plans.empty()) {
    obligations.push_back(std::string{kM37PlanBoundDataPlaneProofObligation});
    obligations.push_back(std::string{kM37SinglePhysicalPayloadChargeRuntimeAssumption});
  }
  return obligations;
}

[[nodiscard]] ExecutionPlanSpec expected_plan_specification(const PlanBuildRequest& request,
                                                            const IndependentlyDerivedPlan& expected) {
  return ExecutionPlanSpec{
    .inputs = {.resolved_pipeline = request.resolved_pipeline.digest().value(),
               .scan_descriptor = request.artifact_digests.scan_descriptor.value(),
               .target_envelope = request.artifact_digests.target_envelope.value(),
               .machine_policy = request.artifact_digests.machine_policy.value(),
               .provider_contracts = canonical_contract_digests(request)},
    .execution_profile = request.requested_profile,
    .key_slot_tables = expected.key_slot_tables,
    .reorder_plans = expected.reorder_plans,
    .buffer_pool_plans = expected.buffer_pool_plans,
    .data_edge_plans = expected.data_edge_plans,
    .edge_capacities = expected.edge_capacities,
    .resource_vector = expected.resource_vector,
    .terminal_occurrences = expected.terminal_occurrences,
    .proof_obligations = expected_plan_proof_obligations(expected),
  };
}

[[nodiscard]] Result<ArtifactDigest> expected_plan_digest(const PlanBuildRequest& request,
                                                          const IndependentlyDerivedPlan& expected) {
  const auto specification = expected_plan_specification(request, expected);
  auto placeholder = ArtifactDigest::parse("sha256:0000000000000000000000000000000000000000000000000000000000000000",
                                           "independent ExecutionPlan canonical serialization placeholder");
  if (!placeholder.ok()) {
    return placeholder.status();
  }
  auto plan_for_serialization = ExecutionPlan::create(std::move(placeholder).value(), specification);
  if (!plan_for_serialization.ok()) {
    return plan_for_serialization.status();
  }
  auto canonical = serialize_execution_plan_canonical_json(plan_for_serialization.value());
  if (!canonical.ok()) {
    return canonical.status();
  }
  return domain_separated_sha256_digest("kspacejet:artifact:execution-plan:1", canonical.value(),
                                        "independent ExecutionPlan verifier derivation");
}

[[nodiscard]] Status compare_input_digests(const ExecutionPlan& plan, const PlanBuildRequest& request) {
  if (plan.inputs().resolved_pipeline() != request.resolved_pipeline.digest() ||
      plan.inputs().scan_descriptor() != request.artifact_digests.scan_descriptor ||
      plan.inputs().target_envelope() != request.artifact_digests.target_envelope ||
      plan.inputs().machine_policy() != request.artifact_digests.machine_policy) {
    return validation("ExecutionPlan input digests do not match the independently supplied immutable inputs");
  }
  std::vector<std::string> actual;
  actual.reserve(plan.inputs().provider_contracts().size());
  for (const auto& digest : plan.inputs().provider_contracts()) {
    actual.push_back(digest.value());
  }
  const auto expected = canonical_contract_digests(request);
  if (actual != expected) {
    return validation("ExecutionPlan Provider contract digests do not exactly match the canonical frozen binding set");
  }
  return Status::Ok();
}

[[nodiscard]] Status compare_key_slot_tables(const ExecutionPlan& plan, const IndependentlyDerivedPlan& expected) {
  if (plan.key_slot_tables().size() != expected.key_slot_tables.size()) {
    return validation("ExecutionPlan KeySlotTable plan count does not match the independent derivation");
  }
  for (std::size_t index = 0; index < expected.key_slot_tables.size(); ++index) {
    const auto& actual = plan.key_slot_tables()[index];
    const auto& wanted = expected.key_slot_tables[index];
    if (actual.node_id() != wanted.node_id || actual.mapping_algorithm_id() != wanted.mapping_algorithm_id ||
        actual.storage_accounting_id() != wanted.storage_accounting_id ||
        actual.key_domain_bound() != wanted.key_domain_bound ||
        actual.max_distinct_keys() != wanted.max_distinct_keys || actual.max_live_keys() != wanted.max_live_keys ||
        actual.slot_count() != wanted.slot_count || actual.generation_policy() != wanted.generation_policy ||
        actual.initial_generation() != wanted.initial_generation ||
        actual.seal_on_completion() != wanted.seal_on_completion ||
        actual.eviction_policy() != wanted.eviction_policy || actual.late_event_policy() != wanted.late_event_policy ||
        actual.host_metadata_charged_bytes() != wanted.host_metadata_charged_bytes ||
        actual.max_items_per_activation() != wanted.max_items_per_activation ||
        actual.max_charged_bytes_per_activation() != wanted.max_charged_bytes_per_activation) {
      return validation("ExecutionPlan KeySlotTable plan does not exactly match the independent derivation for node '" +
                        wanted.node_id + "'");
    }
    if (actual.dense_dimensions().size() != wanted.dense_dimensions.size()) {
      return validation(
        "ExecutionPlan KeySlotTable dense dimension count does not match the independent derivation for "
        "node '" +
        wanted.node_id + "'");
    }
    for (std::size_t dimension_index = 0; dimension_index < wanted.dense_dimensions.size(); ++dimension_index) {
      const auto& actual_dimension = actual.dense_dimensions()[dimension_index];
      const auto& wanted_dimension = wanted.dense_dimensions[dimension_index];
      if (actual_dimension.field() != wanted_dimension.field ||
          actual_dimension.minimum() != wanted_dimension.minimum ||
          actual_dimension.cardinality() != wanted_dimension.cardinality) {
        return validation("ExecutionPlan KeySlotTable dense dimensions do not exactly match the independent derivation "
                          "for node '" +
                          wanted.node_id + "'");
      }
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status compare_reorder_plans(const ExecutionPlan& plan, const IndependentlyDerivedPlan& expected) {
  if (plan.reorder_plans().size() != expected.reorder_plans.size()) {
    return validation("ExecutionPlan ReorderPlan count does not match the independent derivation");
  }
  for (std::size_t index = 0U; index < expected.reorder_plans.size(); ++index) {
    const auto& actual = plan.reorder_plans()[index];
    const auto& wanted = expected.reorder_plans[index];
    if (actual.node_id() != wanted.node_id || actual.order_domain_id() != wanted.order_domain_id ||
        actual.ordinal_binding_id() != wanted.ordinal_binding_id ||
        actual.completed_frame_input_port() != wanted.completed_frame_input_port ||
        actual.ordered_output_port() != wanted.ordered_output_port ||
        actual.outputs_per_ordinal() != wanted.outputs_per_ordinal ||
        actual.charged_bytes_per_ordinal() != wanted.charged_bytes_per_ordinal ||
        actual.mapping_algorithm_id() != wanted.mapping_algorithm_id ||
        actual.storage_accounting_id() != wanted.storage_accounting_id ||
        actual.ordinal_domain_bound() != wanted.ordinal_domain_bound ||
        actual.first_expected_ordinal() != wanted.first_expected_ordinal ||
        actual.last_expected_ordinal() != wanted.last_expected_ordinal ||
        actual.max_ahead_items() != wanted.max_ahead_items ||
        actual.max_ahead_charged_bytes() != wanted.max_ahead_charged_bytes ||
        actual.handle_storage_charged_bytes() != wanted.handle_storage_charged_bytes ||
        actual.max_gap_ordinals() != wanted.max_gap_ordinals ||
        actual.occurrence_policy() != wanted.occurrence_policy || actual.publish_policy() != wanted.publish_policy ||
        actual.certified_skipped_ordinals() != wanted.certified_skipped_ordinals ||
        actual.end_of_input_policy() != wanted.end_of_input_policy ||
        actual.host_metadata_charged_bytes() != wanted.host_metadata_charged_bytes ||
        actual.descriptor_charged_count() != wanted.descriptor_charged_count) {
      return validation("ExecutionPlan ReorderPlan does not exactly match the independent derivation for node '" +
                        wanted.node_id + "'");
    }
    if (actual.ordinal_dimensions().size() != wanted.ordinal_dimensions.size()) {
      return validation("ExecutionPlan ReorderPlan ordinal dimension count does not match the independent derivation "
                        "for node '" +
                        wanted.node_id + "'");
    }
    for (std::size_t dimension_index = 0U; dimension_index < wanted.ordinal_dimensions.size(); ++dimension_index) {
      const auto& actual_dimension = actual.ordinal_dimensions()[dimension_index];
      const auto& wanted_dimension = wanted.ordinal_dimensions[dimension_index];
      if (actual_dimension.field() != wanted_dimension.field ||
          actual_dimension.minimum() != wanted_dimension.minimum ||
          actual_dimension.cardinality() != wanted_dimension.cardinality) {
        return validation("ExecutionPlan ReorderPlan ordinal dimensions do not exactly match the independent "
                          "derivation for node '" +
                          wanted.node_id + "'");
      }
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status compare_buffer_pool_plans(const ExecutionPlan& plan, const IndependentlyDerivedPlan& expected) {
  if (plan.buffer_pool_plans().size() != expected.buffer_pool_plans.size()) {
    return validation("ExecutionPlan BufferPoolPlan count does not match the independent M3.7 derivation");
  }
  for (std::size_t index = 0U; index < expected.buffer_pool_plans.size(); ++index) {
    const auto& actual = plan.buffer_pool_plans()[index];
    const auto& wanted = expected.buffer_pool_plans[index];
    if (actual.pool_id() != wanted.pool_id || actual.producer_node_id() != wanted.producer_node_id ||
        actual.producer_port_name() != wanted.producer_port_name ||
        actual.producer_provider_id() != wanted.producer_provider_id ||
        actual.producer_bundle_digest().value() != wanted.producer_bundle_digest ||
        actual.producer_operator_id() != wanted.producer_operator_id ||
        actual.producer_contract_digest().value() != wanted.producer_contract_digest ||
        !actual.type_descriptor().exactly_matches(wanted.type_descriptor) ||
        actual.memory_domain() != wanted.memory_domain || actual.slot_count() != wanted.slot_count ||
        actual.payload_capacity_bytes() != wanted.payload_capacity_bytes ||
        actual.metadata_capacity_bytes() != wanted.metadata_capacity_bytes ||
        actual.payload_alignment_bytes() != wanted.payload_alignment_bytes ||
        actual.storage_accounting_id() != wanted.storage_accounting_id ||
        actual.host_metadata_charged_bytes() != wanted.host_metadata_charged_bytes ||
        actual.descriptor_charged_count() != wanted.descriptor_charged_count ||
        actual.physical_charge_bytes() != wanted.physical_charge_bytes) {
      return validation(
        "ExecutionPlan BufferPoolPlan does not exactly match the independent M3.7 pool derivation for '" +
        wanted.pool_id + "'.");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status compare_data_edge_plans(const ExecutionPlan& plan, const IndependentlyDerivedPlan& expected) {
  if (plan.data_edge_plans().size() != expected.data_edge_plans.size()) {
    return validation("ExecutionPlan DataEdgePlan count does not match the independent M3.7 derivation");
  }
  for (std::size_t index = 0U; index < expected.data_edge_plans.size(); ++index) {
    const auto& actual = plan.data_edge_plans()[index];
    const auto& wanted = expected.data_edge_plans[index];
    if (actual.edge_id() != wanted.edge_id || actual.source_pool_id() != wanted.source_pool_id ||
        actual.producer_node_id() != wanted.producer_node_id ||
        actual.producer_port_name() != wanted.producer_port_name ||
        actual.producer_abi_port() != wanted.producer_abi_port ||
        actual.consumer_node_id() != wanted.consumer_node_id ||
        actual.consumer_port_name() != wanted.consumer_port_name ||
        !actual.type_descriptor().exactly_matches(wanted.type_descriptor) || actual.max_items() != wanted.max_items ||
        actual.max_logical_bytes() != wanted.max_logical_bytes ||
        actual.storage_accounting_id() != wanted.storage_accounting_id ||
        actual.host_metadata_charged_bytes() != wanted.host_metadata_charged_bytes ||
        actual.descriptor_charged_count() != wanted.descriptor_charged_count ||
        actual.firing_lease_staging_charged_bytes() != wanted.firing_lease_staging_charged_bytes ||
        actual.firing_lease_staging_descriptor_count() != wanted.firing_lease_staging_descriptor_count ||
        actual.terminal_policy() != wanted.terminal_policy) {
      return validation("ExecutionPlan DataEdgePlan does not exactly match the independent M3.7 edge derivation for '" +
                        wanted.edge_id + "'.");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status compare_edge_capacities(const ExecutionPlan& plan, const IndependentlyDerivedPlan& expected) {
  if (plan.edge_capacities().size() != expected.edge_capacities.size()) {
    return validation("ExecutionPlan edge capacity count does not match the independent derivation");
  }
  for (std::size_t index = 0; index < expected.edge_capacities.size(); ++index) {
    const auto& actual = plan.edge_capacities()[index];
    const auto& wanted = expected.edge_capacities[index];
    if (actual.edge_id() != wanted.edge_id || actual.capacity().max_items() != wanted.max_items ||
        actual.capacity().max_charged_bytes() != wanted.max_charged_bytes) {
      return validation("ExecutionPlan edge capacity does not exactly match the independent derivation for edge '" +
                        wanted.edge_id + "'");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status compare_resource_vector(const ExecutionPlan& plan, const IndependentlyDerivedPlan& expected) {
  auto expected_resource_vector =
    ResourceVector::create(expected.resource_vector, "independently derived resource_vector");
  if (!expected_resource_vector.ok()) {
    return expected_resource_vector.status();
  }
  if (!plan.resources().exactly_matches(expected_resource_vector.value())) {
    return validation("ExecutionPlan ResourceVector does not exactly match the independent derivation");
  }
  return Status::Ok();
}

[[nodiscard]] Status compare_terminal_occurrences(const ExecutionPlan& plan, const IndependentlyDerivedPlan& expected) {
  if (plan.terminal_occurrences() != expected.terminal_occurrences) {
    return validation("ExecutionPlan terminal occurrence bound does not exactly match the independent derivation");
  }
  return Status::Ok();
}

[[nodiscard]] Status compare_proof_obligations(const ExecutionPlan& plan, const IndependentlyDerivedPlan& expected) {
  if (plan.proof_obligations() != expected_plan_proof_obligations(expected)) {
    return validation("ExecutionPlan proof obligations do not exactly match the independent v1 derivation");
  }
  return Status::Ok();
}

} // namespace

Result<VerificationRecord> ExecutionPlanVerifier::verify(const ExecutionPlan& plan, const PlanBuildRequest& request) {
  if (plan.execution_profile() != request.requested_profile) {
    return validation("ExecutionPlan profile does not match PlanBuildRequest");
  }

  const auto pre_ingress = validate_pre_ingress_scan_envelope_independently(request);
  if (!pre_ingress.ok()) {
    return pre_ingress;
  }

  // Do not use ExecutionPlanCompiler::compile() or any compiler derivation
  // helper below this line.  This is an independently compiled checker of the
  // frozen artifact, not a deterministic compiler re-run.
  const auto bindings = validate_bindings_independently(request);
  if (!bindings.ok()) {
    return bindings;
  }
  const auto calibration = validate_calibration_bindings_independently(request);
  if (!calibration.ok()) {
    return calibration;
  }
  auto expected = derive_plan_independently(request);
  if (!expected.ok()) {
    return expected.status();
  }
  const auto capacity = check_resource_capacity_independently(expected.value().resource_vector, request.machine_policy);
  if (!capacity.ok()) {
    return capacity;
  }

  const auto input_digests = compare_input_digests(plan, request);
  if (!input_digests.ok()) {
    return input_digests;
  }
  const auto key_slot_tables = compare_key_slot_tables(plan, expected.value());
  if (!key_slot_tables.ok()) {
    return key_slot_tables;
  }
  const auto reorder_plans = compare_reorder_plans(plan, expected.value());
  if (!reorder_plans.ok()) {
    return reorder_plans;
  }
  const auto buffer_pools = compare_buffer_pool_plans(plan, expected.value());
  if (!buffer_pools.ok()) {
    return buffer_pools;
  }
  const auto data_edges = compare_data_edge_plans(plan, expected.value());
  if (!data_edges.ok()) {
    return data_edges;
  }
  const auto edge_capacities = compare_edge_capacities(plan, expected.value());
  if (!edge_capacities.ok()) {
    return edge_capacities;
  }
  const auto resources = compare_resource_vector(plan, expected.value());
  if (!resources.ok()) {
    return resources;
  }
  const auto terminal = compare_terminal_occurrences(plan, expected.value());
  if (!terminal.ok()) {
    return terminal;
  }
  const auto proof_obligations = compare_proof_obligations(plan, expected.value());
  if (!proof_obligations.ok()) {
    return proof_obligations;
  }

  auto expected_digest = expected_plan_digest(request, expected.value());
  if (!expected_digest.ok()) {
    return expected_digest.status();
  }
  if (plan.digest() != expected_digest.value()) {
    return validation("ExecutionPlan detached digest does not match the independently canonicalized plan artifact");
  }

  std::vector<std::string> obligations{
    "M0.profile_attestation",         "M0.detached_contract_attestation", "M0.plan_input_identity",
    "M3.dense_key_slot_table",        "M0.machine_resource_capacity",     "M0.finite_key_slot_and_edge_bounds",
    "M0.finite_terminal_occurrences",
  };
  if (!plan.reorder_plans().empty()) {
    obligations.insert(obligations.begin() + 4, std::string{kM3CompletedFrameSlotBindingVerificationObligation});
    obligations.insert(obligations.begin() + 5, std::string{kM3StrictDenseAllTuplesEoiVerificationObligation});
  }
  if (!plan.data_edge_plans().empty()) {
    obligations.push_back(std::string{kM37PlanBoundDataPlaneVerificationObligation});
    obligations.push_back(std::string{kM37SinglePhysicalPayloadChargeVerificationObligation});
  }
  const VerificationRecordSpec specification{
    .execution_plan_digest = plan.digest().value(),
    .execution_profile = plan.execution_profile(),
    .verified_resource_vector = resource_vector_spec(plan.resources()),
    .verified_terminal_occurrences = plan.terminal_occurrences(),
    .verified_obligations = obligations,
  };
  auto placeholder = ArtifactDigest::parse("sha256:0000000000000000000000000000000000000000000000000000000000000000",
                                           "VerificationRecord canonical serialization placeholder");
  if (!placeholder.ok()) {
    return placeholder.status();
  }
  auto record_for_serialization = VerificationRecord::create(std::move(placeholder).value(), specification);
  if (!record_for_serialization.ok()) {
    return record_for_serialization.status();
  }
  auto canonical = serialize_verification_record_canonical_json(record_for_serialization.value());
  if (!canonical.ok()) {
    return canonical.status();
  }
  auto record_digest = domain_separated_sha256_digest("kspacejet:artifact:verification-record:1", canonical.value(),
                                                      "independent VerificationRecord verifier output");
  if (!record_digest.ok()) {
    return record_digest.status();
  }
  return VerificationRecord::create(std::move(record_digest).value(), specification);
}

} // namespace ksj::recon::graph
