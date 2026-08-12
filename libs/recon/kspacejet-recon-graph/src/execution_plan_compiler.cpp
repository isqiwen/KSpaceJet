#include "kspacejet/recon/graph/execution_plan_compiler.hpp"

#include "kspacejet/recon/bounded_value.hpp"
#include "kspacejet/recon/graph/artifact_json.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ksj::recon::graph {
namespace {

struct DerivedDenseKeyDomain {
  std::vector<DenseKeySlotDimensionSpec> dimensions;
  Quantity cardinality{1};
};

struct DerivedCartesianOrdinalDomain {
  std::vector<DenseCartesianOrdinalDimensionSpec> dimensions;
  Quantity cardinality{1};
};

struct OutputBound {
  Quantity items{0};
  Quantity bytes{0};
};

struct DerivedPlanParts {
  std::vector<KeySlotTablePlanSpec> key_slot_tables;
  std::vector<ReorderPlanSpec> reorder_plans;
  std::vector<EdgeCapacitySpec> edges;
  ResourceVectorSpec resources;
  Quantity terminal_occurrences{0};
  std::vector<std::string> obligations;
};

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

[[nodiscard]] Status validate_pre_ingress_scan_envelope(const PlanBuildRequest& request) {
  if (request.scan_descriptor.source_xml_bytes() > request.target_envelope.max_xml_bytes()) {
    return validation("ScanDescriptor source XML byte length exceeds TargetEnvelope.max_xml_bytes before ingress.");
  }
  return Status::Ok();
}

[[nodiscard]] bool contains(const std::vector<std::string>& values, const std::string_view needle) {
  return std::ranges::find(values, needle) != values.end();
}

template <typename T> [[nodiscard]] bool contains(const std::vector<T>& values, const T needle) {
  return std::ranges::find(values, needle) != values.end();
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

[[nodiscard]] const PipelineNode* find_node(const PipelineDefinition& definition, const std::string_view node_id) {
  const auto found = std::ranges::find(definition.nodes(), node_id, &PipelineNode::id);
  return found == definition.nodes().end() ? nullptr : &*found;
}

[[nodiscard]] const PortSpec* find_contract_port(const OperatorContract& contract, const std::string_view name) {
  const auto found = std::ranges::find(contract.ports(), name, &PortSpec::name);
  return found == contract.ports().end() ? nullptr : &*found;
}

[[nodiscard]] std::string type_reference(const TypeDescriptor& descriptor) {
  return descriptor.type_id() + "/v" + std::to_string(descriptor.revision());
}

[[nodiscard]] const OperatorContractBinding* find_contract_binding(const PlanBuildRequest& request,
                                                                   const std::string_view node_id) {
  const auto found = std::ranges::find(request.operator_contracts, node_id, &OperatorContractBinding::node_id);
  return found == request.operator_contracts.end() ? nullptr : &*found;
}

[[nodiscard]] const ResolvedProvider* find_resolved_provider(const ResolvedPipeline& pipeline,
                                                             const std::string_view alias) {
  const auto found = std::ranges::find(pipeline.providers(), alias, &ResolvedProvider::alias);
  return found == pipeline.providers().end() ? nullptr : &*found;
}

[[nodiscard]] const ResolvedOperator* find_resolved_operator(const ResolvedProvider& provider,
                                                             const std::string_view operator_id) {
  const auto found = std::ranges::find(provider.operators, operator_id, &ResolvedOperator::id);
  return found == provider.operators.end() ? nullptr : &*found;
}

[[nodiscard]] bool memory_domain_allowed(const MachinePolicy& policy, const MemoryDomain domain) {
  return contains(policy.allowed_memory_domains(), domain);
}

[[nodiscard]] Status validate_port_endpoint(const PlanBuildRequest& request, const NodePortReference& endpoint,
                                            const PortDirection direction, const std::string_view expected_type,
                                            const std::string_view context) {
  const auto* binding = find_contract_binding(request, endpoint.node);
  if (binding == nullptr)
    return validation(std::string(context) + " references an unbound node");
  const auto* port = find_contract_port(binding->contract, endpoint.port);
  if (port == nullptr) {
    return validation(std::string(context) + " references unknown OperatorContract port '" + endpoint.port + "'");
  }
  if (port->direction != direction) {
    return validation(std::string(context) + " references an OperatorContract port with the wrong direction");
  }
  if (!expected_type.empty() && type_reference(port->type_descriptor) != expected_type) {
    return validation(std::string(context) + " type does not exactly match the resolved OperatorContract port");
  }
  return Status::Ok();
}

// A completed FrameSlotContext is an in-process post-assembly value, never a
// public MRD/ISMRMRD ingress payload.  M3 can therefore bind its ordinal only
// when the named input has one resolved graph-edge producer whose frozen
// output descriptor is the exact completed-frame ABI.  This is deliberately
// checked separately from ordinary typed-edge validation so no type-id-only
// ingress assertion can stand in for FrameSlot provenance.
[[nodiscard]] Status validate_m3_completed_frame_graph_sources(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  auto completed_frame_type = completed_frame_slot_context_type();
  if (!completed_frame_type.ok()) {
    return completed_frame_type.status();
  }

  for (const auto& node : definition.nodes()) {
    const auto* binding = find_contract_binding(request, node.id);
    const auto& reorder = binding->contract.reorder();
    if (!reorder.has_value()) {
      continue;
    }

    const auto ingress = std::ranges::find_if(definition.ingress_ports(), [&](const IngressPort& candidate) {
      return candidate.to.node == node.id && candidate.to.port == reorder->completed_frame_input_port;
    });
    if (ingress != definition.ingress_ports().end()) {
      return validation("M3 completed_frame_input_port for node '" + node.id +
                        "' must not be a public ingress; it requires a resolved internal typed node output.");
    }

    const auto source = std::ranges::find_if(definition.edges(), [&](const PipelineEdge& edge) {
      return edge.to.node == node.id && edge.to.port == reorder->completed_frame_input_port;
    });
    if (source == definition.edges().end()) {
      return validation("M3 completed_frame_input_port for node '" + node.id +
                        "' requires one resolved graph edge from an internal typed node output.");
    }
    const auto duplicate = std::find_if(std::next(source), definition.edges().end(), [&](const PipelineEdge& edge) {
      return edge.to.node == node.id && edge.to.port == reorder->completed_frame_input_port;
    });
    if (duplicate != definition.edges().end()) {
      return validation("M3 completed_frame_input_port for node '" + node.id + "' has multiple graph producers.");
    }

    const auto* source_binding = find_contract_binding(request, source->from.node);
    const auto* source_port =
      source_binding == nullptr ? nullptr : find_contract_port(source_binding->contract, source->from.port);
    if (source_port == nullptr || source_port->direction != PortDirection::output ||
        !source_port->type_descriptor.exactly_matches(completed_frame_type.value())) {
      return validation("M3 completed_frame_input_port for node '" + node.id +
                        "' must be produced by a resolved output with the exact completed FrameSlotContext "
                        "TypeDescriptor ABI.");
    }
  }
  return Status::Ok();
}

// Authored nodes contain only Operator references.  Port existence, direction,
// type and required-input closure are checked once against the frozen contracts
// here, so there is never a second node-owned port authority.
[[nodiscard]] Status validate_graph_ports(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  std::unordered_set<std::string> produced_inputs;
  for (const auto& edge : definition.edges()) {
    auto source =
      validate_port_endpoint(request, edge.from, PortDirection::output, "", "edge '" + edge.id + "' source");
    if (!source.ok())
      return source;
    auto destination =
      validate_port_endpoint(request, edge.to, PortDirection::input, "", "edge '" + edge.id + "' destination");
    if (!destination.ok())
      return destination;
    const auto* source_binding = find_contract_binding(request, edge.from.node);
    const auto* destination_binding = find_contract_binding(request, edge.to.node);
    const auto* source_port = find_contract_port(source_binding->contract, edge.from.port);
    const auto* destination_port = find_contract_port(destination_binding->contract, edge.to.port);
    if (!source_port->type_descriptor.exactly_matches(destination_port->type_descriptor)) {
      return validation("edge '" + edge.id + "' connects incompatible resolved OperatorContract port types");
    }
    if (!produced_inputs.insert(edge.to.node + "." + edge.to.port).second) {
      return validation("multiple producers require an explicit MergeBinding");
    }
  }
  for (const auto& ingress : definition.ingress_ports()) {
    auto status =
      validate_port_endpoint(request, ingress.to, PortDirection::input, ingress.type, "ingress '" + ingress.id + "'");
    if (!status.ok())
      return status;
    if (!produced_inputs.insert(ingress.to.node + "." + ingress.to.port).second) {
      return validation("ingress '" + ingress.id + "' creates a second producer for one input port");
    }
  }
  for (const auto& egress : definition.egress_ports()) {
    auto status =
      validate_port_endpoint(request, egress.from, PortDirection::output, egress.type, "egress '" + egress.id + "'");
    if (!status.ok())
      return status;
  }
  for (const auto& node : definition.nodes()) {
    const auto* binding = find_contract_binding(request, node.id);
    for (const auto& port : binding->contract.ports()) {
      if (port.direction == PortDirection::input && port.required &&
          !produced_inputs.contains(node.id + "." + port.name)) {
        return validation("required resolved OperatorContract input '" + node.id + "." + port.name +
                          "' has no producer");
      }
    }
  }
  return validate_m3_completed_frame_graph_sources(request);
}

[[nodiscard]] Status validate_binding_set(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  if (request.operator_contracts.size() != definition.nodes().size()) {
    return validation("plan compiler requires exactly one OperatorContract binding for every pipeline node");
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
  if (std::ranges::find(definition.allowed_profiles(), profile) == definition.allowed_profiles().end()) {
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
    const auto* binding = find_contract_binding(request, node.id);
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
    if (!memory_domain_allowed(request.machine_policy, contract.resources().memory_domain)) {
      return validation("OperatorContract memory domain is forbidden by MachinePolicy for node '" + node.id + "'");
    }
    if (profile == ExecutionProfile::bounded_online && contract.resources().external_allocation_charged_bytes != 0U) {
      return validation("bounded-online planning rejects unaccounted Provider external allocation for node '" +
                        node.id + "'");
    }

    const auto* provider = find_resolved_provider(request.resolved_pipeline, node.provider_alias);
    if (provider == nullptr) {
      return validation("ResolvedPipeline is missing Provider alias '" + node.provider_alias + "'");
    }
    if (provider->abi_major != contract.provider_abi_major()) {
      return validation("Provider ABI major does not match OperatorContract for node '" + node.id + "'");
    }
    const auto* resolved_operator = find_resolved_operator(*provider, node.operator_id);
    if (resolved_operator == nullptr || resolved_operator->contract_digest != binding->contract_digest) {
      return validation("ResolvedPipeline contract digest does not match loaded OperatorContract for node '" + node.id +
                        "'");
    }
  }
  return validate_graph_ports(request);
}

[[nodiscard]] const CalibrationBinding* find_calibration_binding(const PipelineDefinition& definition,
                                                                 const std::string_view binding_id) {
  const auto found = std::ranges::find(definition.calibration_bindings(), binding_id, &CalibrationBinding::id);
  return found == definition.calibration_bindings().end() ? nullptr : &*found;
}

[[nodiscard]] Status validate_calibration_bindings(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  const auto profile = request.requested_profile;
  for (const auto& node : definition.nodes()) {
    const auto* binding = find_contract_binding(request, node.id);
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
      const auto* producer_port = find_contract_port(binding->contract, graph_binding->producer.port);
      if (producer_port == nullptr || producer_port->direction != PortDirection::output ||
          type_reference(producer_port->type_descriptor) != "ksj.calibration-material/v1") {
        return validation("calibration producer binding must reference a ksj.calibration-material/v1 output port");
      }
    }
    if (calibration.role == CalibrationRole::consumer && !contains(graph_binding->consumer_nodes, node.id)) {
      return validation("calibration consumer node does not match OperatorContract binding for '" + node.id + "'");
    }
    if (profile != ExecutionProfile::bounded_online || calibration.role != CalibrationRole::consumer) {
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

// M3 must derive ordinals from fields carried by the current host-owned
// FrameSemanticKey/FrameSlotContext.  In particular, XML `segment` exists in
// ISMRMRD but is not yet part of that runtime semantic key, so accepting it
// here would create a plan that the runtime could not realize.
[[nodiscard]] bool is_m3_frame_context_projection_field(const std::string_view field) noexcept {
  return field == "encoding" || field == "average" || field == "slice" || field == "contrast" || field == "phase" ||
         field == "repetition" || field == "set";
}

inline constexpr std::array<std::string_view, 7U> kM3FrameSemanticKeyAxes{
  "encoding", "average", "slice", "contrast", "phase", "repetition", "set",
};

[[nodiscard]] Result<DenseKeySlotDimensionSpec>
derive_channel_group_dimension(const OperatorExecutionShapeSpec& execution, const ScanDescriptor& scan,
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

[[nodiscard]] Result<DenseKeySlotDimensionSpec> derive_dense_key_dimension(const std::string_view field,
                                                                           const OperatorExecutionShapeSpec& execution,
                                                                           const ScanDescriptor& scan,
                                                                           const TargetEnvelope& envelope) {
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
    return derive_channel_group_dimension(execution, scan, envelope);
  }
  const auto dimension = limit_dimension_for(field);
  if (!dimension.has_value()) {
    return validation("unsupported non-dense or unknown v1 IndexProjection field '" + std::string(field) + "'");
  }

  std::optional<IndexLimit> fixed_limit;
  for (std::size_t encoding_index = 0; encoding_index < scan.encodings().size(); ++encoding_index) {
    const auto& encoding = scan.encodings()[encoding_index];
    const auto& limit = encoding.limits().at(*dimension);
    if (!limit.has_value()) {
      return validation("partition key '" + std::string(field) + "' is absent from ISMRMRD XML encoding[" +
                        std::to_string(encoding_index) +
                        "]; envelope-backed dynamic/sparse KeySlot domains are unsupported");
    }
    if (!fixed_limit.has_value()) {
      fixed_limit = *limit;
      continue;
    }
    if (limit->minimum() != fixed_limit->minimum() || limit->cardinality() != fixed_limit->cardinality()) {
      return validation("partition key '" + std::string(field) +
                        "' has non-uniform ISMRMRD XML bounds across encodings; rectangular dense-mixed-radix/v1 "
                        "domains cannot encode sparse per-encoding domains");
    }
  }
  if (!fixed_limit.has_value()) {
    return validation("dense-mixed-radix/v1 KeySlotTable cannot derive a key dimension from an empty ScanDescriptor");
  }
  return DenseKeySlotDimensionSpec{
    .field = std::string(field), .minimum = fixed_limit->minimum(), .cardinality = fixed_limit->cardinality()};
}

[[nodiscard]] Result<DerivedDenseKeyDomain> derive_dense_key_domain(const OperatorExecutionShapeSpec& execution,
                                                                    const ScanDescriptor& scan,
                                                                    const TargetEnvelope& envelope) {
  DerivedDenseKeyDomain result;
  result.dimensions.reserve(execution.partition_key.size());
  for (const auto& field : execution.partition_key) {
    auto dimension = derive_dense_key_dimension(field, execution, scan, envelope);
    if (!dimension.ok()) {
      return dimension.status();
    }
    auto cardinality = multiply(result.cardinality, dimension.value().cardinality,
                                "dense KeySlotTable mixed-radix key-domain cardinality");
    if (!cardinality.ok()) {
      return cardinality.status();
    }
    result.cardinality = cardinality.value();
    result.dimensions.push_back(std::move(dimension).value());
  }
  return result;
}

[[nodiscard]] Status require_cartesian_reorder_scan(const ScanDescriptor& scan) {
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

// This compiler-local witness check deliberately repeats the completed-frame
// binding invariant held by OperatorContract.  A ReorderPlan is an admission
// artifact, so it must never infer a one-to-one ordinal from an arbitrary
// acquisition firing merely because a Provider contract happened to declare
// one output item.
[[nodiscard]] Status require_m3_completed_frame_slot_binding(const OperatorContract& contract,
                                                             const ReorderSpec& reorder) {
  const auto& execution = contract.execution();
  const auto& batch = contract.batch();
  const auto& rates = contract.rates();
  if (execution.input_granularity != InputGranularity::frame || execution.max_items_per_activation != 1U ||
      batch.min_items != 1U || batch.preferred_items != 1U || batch.max_items != 1U) {
    return validation("M3 ReorderPlan requires exactly one completed FrameSlotContext input per activation and batch.");
  }
  const auto* frame_port = find_contract_port(contract, reorder.completed_frame_input_port);
  auto completed_frame_type = completed_frame_slot_context_type();
  if (!completed_frame_type.ok()) {
    return completed_frame_type.status();
  }
  if (frame_port == nullptr || frame_port->direction != PortDirection::input ||
      !frame_port->type_descriptor.exactly_matches(completed_frame_type.value())) {
    return validation("M3 ReorderPlan completed_frame_input_port must exactly match the frozen ksj.kspace-frame "
                      "FrameSlotContext TypeDescriptor ABI.");
  }
  if (rates.kind != RateKind::sdf || rates.static_phases.size() != 1U) {
    return validation("M3 ReorderPlan requires one SDF phase for its exact completed-FrameSlot-to-output binding.");
  }
  const auto& inputs = rates.static_phases.front().inputs;
  const auto frame_input = std::ranges::find(inputs, reorder.completed_frame_input_port, &PortRateSpec::port_name);
  if (frame_input == inputs.end() || frame_input->items != 1U || inputs.size() != 1U) {
    return validation("M3 ReorderPlan requires exactly one completed FrameSlot input rate in its SDF phase.");
  }
  if (execution.channel_group.has_value() || contains(execution.partition_key, std::string_view{"channel_group"})) {
    return validation("M3 ReorderPlan forbids channel_group because its ordinal is the completed FrameSlotContext "
                      "semantic-key identity.");
  }
  if (execution.partition_key != reorder.order_projection) {
    return validation("M3 ReorderPlan requires execution.partition_key to exactly equal order_projection so the "
                      "KeySlotTable and ordinal domain cannot diverge.");
  }
  return Status::Ok();
}

// A projection is an ordinal identity only if it distinguishes every possible
// completed FrameSlotContext semantic key.  An omitted field is safe solely
// when XML proves it is one fixed value in every encoding.  XML limits still
// do not prove tuple occurrence; that separate strict-dense EOI obligation is
// serialized in the ReorderPlan.
[[nodiscard]] Status require_complete_m3_frame_semantic_key_projection(const ReorderSpec& reorder,
                                                                       const ScanDescriptor& scan) {
  for (const auto field : kM3FrameSemanticKeyAxes) {
    if (contains(reorder.order_projection, field)) {
      continue;
    }
    if (field == "encoding") {
      if (scan.encodings().size() != 1U) {
        return validation("M3 ReorderPlan order_projection must include varying FrameSlotContext axis 'encoding'.");
      }
      continue;
    }

    const auto dimension = limit_dimension_for(field);
    if (!dimension.has_value()) {
      return validation("internal error: M3 FrameSlotContext axis has no ISMRMRD XML mapping.");
    }
    std::optional<Quantity> singleton_value;
    for (std::size_t encoding_index = 0U; encoding_index < scan.encodings().size(); ++encoding_index) {
      const auto& limit = scan.encodings()[encoding_index].limits().at(*dimension);
      if (!limit.has_value()) {
        return validation("M3 ReorderPlan cannot prove omitted FrameSlotContext axis '" + std::string(field) +
                          "' is singleton: it is absent from ISMRMRD XML encoding[" + std::to_string(encoding_index) +
                          "].");
      }
      if (limit->cardinality() != 1U) {
        return validation("M3 ReorderPlan order_projection must include varying FrameSlotContext axis '" +
                          std::string(field) + "'.");
      }
      if (singleton_value.has_value() && *singleton_value != limit->minimum()) {
        return validation("M3 ReorderPlan cannot omit FrameSlotContext axis '" + std::string(field) +
                          "': singleton XML values differ across encodings.");
      }
      singleton_value = limit->minimum();
    }
  }
  return Status::Ok();
}

[[nodiscard]] Result<Quantity> derive_m3_ordered_output_charged_bytes(const OperatorContract& contract,
                                                                      const ReorderSpec& reorder) {
  if (reorder.outputs_per_ordinal != 1U) {
    return validation("M3 ReorderPlan requires exactly one OutputEnvelope per ordinal.");
  }
  const auto* port = find_contract_port(contract, reorder.ordered_output_port);
  if (port == nullptr || port->direction != PortDirection::output) {
    return validation("M3 ReorderPlan ordered_output_port must name a declared output port.");
  }

  std::optional<Quantity> charged_bytes;
  const auto require_rate = [&](const std::vector<PortRateSpec>& outputs, const std::string_view rate_name) -> Status {
    const auto found = std::ranges::find(outputs, reorder.ordered_output_port, &PortRateSpec::port_name);
    if (found == outputs.end()) {
      return validation("M3 ReorderPlan ordered_output_port must have an ordinary rate in " + std::string(rate_name) +
                        ".");
    }
    if (found->items != 1U || found->charged_bytes == 0U) {
      return validation("M3 ReorderPlan selected output rate must be exactly one positive-byte OutputEnvelope in " +
                        std::string(rate_name) + ".");
    }
    if (charged_bytes.has_value() && *charged_bytes != found->charged_bytes) {
      return validation("M3 ReorderPlan selected output port must have one fixed charged-byte bound across every "
                        "ordinary rate phase.");
    }
    charged_bytes = found->charged_bytes;
    return Status::Ok();
  };

  const auto& rates = contract.rates();
  if (rates.kind == RateKind::keyed_dynamic) {
    auto ordinary = require_rate(rates.ordinary.outputs, "rates.ordinary.outputs");
    if (!ordinary.ok()) {
      return ordinary;
    }
    const auto terminal =
      std::ranges::find(rates.normal_flush.outputs, reorder.ordered_output_port, &PortRateSpec::port_name);
    if (terminal != rates.normal_flush.outputs.end()) {
      return validation("M3 ReorderPlan selected output port must not be emitted by normal_flush.");
    }
  } else {
    for (std::size_t index = 0U; index < rates.static_phases.size(); ++index) {
      auto phase =
        require_rate(rates.static_phases[index].outputs, "rates.static_phases[" + std::to_string(index) + "].outputs");
      if (!phase.ok()) {
        return phase;
      }
    }
  }
  if (!charged_bytes.has_value()) {
    return validation("M3 ReorderPlan could not derive a selected output byte bound.");
  }
  return *charged_bytes;
}

[[nodiscard]] Result<DenseCartesianOrdinalDimensionSpec>
derive_cartesian_ordinal_dimension(const std::string_view field, const ScanDescriptor& scan) {
  if (!is_m3_frame_context_projection_field(field)) {
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
  for (std::size_t encoding_index = 0U; encoding_index < scan.encodings().size(); ++encoding_index) {
    const auto& limit = scan.encodings()[encoding_index].limits().at(*dimension);
    if (!limit.has_value()) {
      return validation("M3 ReorderPlan order_projection field '" + std::string(field) +
                        "' is absent from ISMRMRD XML encoding[" + std::to_string(encoding_index) + "].");
    }
    if (!common_limit.has_value()) {
      common_limit = *limit;
      continue;
    }
    if (limit->minimum() != common_limit->minimum() || limit->cardinality() != common_limit->cardinality()) {
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

[[nodiscard]] Result<DerivedCartesianOrdinalDomain> derive_cartesian_ordinal_domain(const ReorderSpec& reorder,
                                                                                    const ScanDescriptor& scan) {
  const auto cartesian = require_cartesian_reorder_scan(scan);
  if (!cartesian.ok()) {
    return cartesian;
  }
  const auto complete_projection = require_complete_m3_frame_semantic_key_projection(reorder, scan);
  if (!complete_projection.ok()) {
    return complete_projection;
  }

  DerivedCartesianOrdinalDomain result;
  result.dimensions.reserve(reorder.order_projection.size());
  for (const auto& field : reorder.order_projection) {
    auto dimension = derive_cartesian_ordinal_dimension(field, scan);
    if (!dimension.ok()) {
      return dimension.status();
    }
    auto cardinality = multiply(result.cardinality, dimension.value().cardinality,
                                "M3 dense Cartesian ReorderPlan ordinal-domain cardinality");
    if (!cardinality.ok()) {
      return cardinality.status();
    }
    result.cardinality = cardinality.value();
    result.dimensions.push_back(std::move(dimension).value());
  }
  return result;
}

[[nodiscard]] Status require_m3_key_slot_reorder_alignment(const KeySlotTablePlanSpec& table,
                                                           const ReorderPlanSpec& reorder) {
  if (table.node_id != reorder.node_id || table.key_domain_bound != reorder.ordinal_domain_bound ||
      table.dense_dimensions.size() != reorder.ordinal_dimensions.size()) {
    return validation("M3 ReorderPlan must have the same node-owned dense domain as its KeySlotTable.");
  }
  for (std::size_t index = 0U; index < table.dense_dimensions.size(); ++index) {
    const auto& key = table.dense_dimensions[index];
    const auto& ordinal = reorder.ordinal_dimensions[index];
    if (key.field != ordinal.field || key.minimum != ordinal.minimum || key.cardinality != ordinal.cardinality) {
      return validation("M3 ReorderPlan ordinal dimensions must exactly equal its node KeySlotTable dimensions.");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Result<ReorderPlanSpec>
derive_cartesian_reorder_plan(const PipelineNode& node, const OperatorContract& contract, const ScanDescriptor& scan) {
  if (!contract.reorder().has_value()) {
    return validation("internal error: Cartesian ReorderPlan requested for an OperatorContract without ReorderSpec");
  }
  if (contract.resources().memory_domain != MemoryDomain::host) {
    return validation("M3 ReorderPlan currently requires host memory-domain ownership for node '" + node.id + "'.");
  }

  const auto& reorder = *contract.reorder();
  const auto frame_binding = require_m3_completed_frame_slot_binding(contract, reorder);
  if (!frame_binding.ok()) {
    return frame_binding;
  }
  auto charged_bytes_per_ordinal = derive_m3_ordered_output_charged_bytes(contract, reorder);
  if (!charged_bytes_per_ordinal.ok()) {
    return charged_bytes_per_ordinal.status();
  }
  auto domain = derive_cartesian_ordinal_domain(reorder, scan);
  if (!domain.ok()) {
    return domain.status();
  }
  if (reorder.max_ahead_items > domain.value().cardinality) {
    return validation("M3 ReorderPlan max_ahead_items exceeds its dense Cartesian ordinal domain for node '" + node.id +
                      "'.");
  }
  auto required_ahead_bytes = checked_multiply(reorder.max_ahead_items, charged_bytes_per_ordinal.value(),
                                               "M3 ReorderPlan max ahead full OutputEnvelope reservation");
  if (!required_ahead_bytes.ok()) {
    return required_ahead_bytes.status();
  }
  if (reorder.max_ahead_charged_bytes < required_ahead_bytes.value()) {
    return validation("M3 ReorderPlan max_ahead_charged_bytes does not cover all max_ahead_items full "
                      "OutputEnvelope reservations for node '" +
                      node.id + "'.");
  }
  auto metadata = dense_cartesian_reorder_host_metadata_charged_bytes(
    domain.value().cardinality, reorder.max_ahead_items, "node '" + node.id + "' ReorderPlan host metadata");
  if (!metadata.ok()) {
    return metadata.status();
  }
  const auto ordinal_domain_bound = domain.value().cardinality;
  auto ordinal_dimensions = std::move(domain).value().dimensions;
  return ReorderPlanSpec{
    .node_id = node.id,
    // M3 has one explicit output-order domain per node.  The frozen
    // mixed-radix dimensions describe its ordinal mapping; a future
    // multi-domain extension must introduce a distinct plan identity rather
    // than overload a Provider-defined ordinal source.
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
    .host_metadata_charged_bytes = metadata.value(),
    .descriptor_charged_count = reorder.max_ahead_items,
  };
}

[[nodiscard]] Status add_reorder_plan_resources(const ReorderPlanSpec& reorder, ResourceVectorSpec& plan) {
  auto metadata = add_to(plan.host_normal_bytes, reorder.host_metadata_charged_bytes,
                         "dense Cartesian ReorderPlan host metadata reservation");
  if (!metadata.ok()) {
    return metadata;
  }
  auto payload = add_to(plan.host_normal_bytes, reorder.max_ahead_charged_bytes,
                        "dense Cartesian ReorderPlan ahead payload reservation");
  if (!payload.ok()) {
    return payload;
  }
  return add_to(plan.descriptor_count, reorder.descriptor_charged_count,
                "dense Cartesian ReorderPlan ahead descriptor reservation");
}

[[nodiscard]] Result<OutputBound> output_bound_for_port(const OperatorContract& contract,
                                                        const std::string_view port_name) {
  OutputBound result;
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

[[nodiscard]] Result<EdgeCapacitySpec> derive_edge_capacity(const PipelineEdge& edge, const OperatorContract& source) {
  auto output = output_bound_for_port(source, edge.from.port);
  if (!output.ok()) {
    return output.status();
  }
  auto ordinary_items =
    multiply(output.value().items, source.execution().max_in_flight, "edge " + edge.id + " ordinary item capacity");
  if (!ordinary_items.ok())
    return ordinary_items.status();
  auto ordinary_bytes =
    multiply(output.value().bytes, source.execution().max_in_flight, "edge " + edge.id + " ordinary byte capacity");
  if (!ordinary_bytes.ok())
    return ordinary_bytes.status();
  const auto& terminal = source.terminal();
  auto items =
    checked_add(ordinary_items.value(), terminal.normal_max_output_items, "edge " + edge.id + " item capacity");
  if (!items.ok())
    return items.status();
  auto bytes =
    checked_add(ordinary_bytes.value(), terminal.normal_max_output_charged_bytes, "edge " + edge.id + " byte capacity");
  if (!bytes.ok())
    return bytes.status();
  if (items.value() == 0U || bytes.value() == 0U) {
    return validation("edge '" + edge.id + "' has an unbounded or zero derived capacity");
  }
  return EdgeCapacitySpec{.edge_id = edge.id, .max_items = items.value(), .max_charged_bytes = bytes.value()};
}

[[nodiscard]] Result<Quantity*> host_memory_bucket(ResourceVectorSpec& vector, const MemoryDomain domain) {
  switch (domain) {
    case MemoryDomain::host:
      return &vector.host_normal_bytes;
    case MemoryDomain::pinned_host:
      return &vector.host_pinned_bytes;
    case MemoryDomain::shared:
      return &vector.shared_host_bytes;
    case MemoryDomain::device:
      // A device allocation requires a chosen, stable DeviceResourceSlot id.
      // M0 deliberately has no device-variant planner, so accepting it here
      // would place an unowned number into the resource ledger.
      return validation("M0 compiler cannot plan a device-memory OperatorContract without a selected device variant");
  }
  return validation("OperatorContract has an invalid memory domain");
}

[[nodiscard]] Status add_contract_resources(const OperatorContract& contract, const KeySlotTablePlanSpec& table,
                                            ResourceVectorSpec& plan) {
  const auto& resources = contract.resources();
  auto bucket = host_memory_bucket(plan, resources.memory_domain);
  if (!bucket.ok())
    return bucket.status();
  const auto add = [&](const Quantity value, const std::string_view name) {
    return add_to(*bucket.value(), value, name);
  };
  auto status = add(resources.per_scan_workspace_charged_bytes, "per-scan workspace");
  if (!status.ok())
    return status;
  auto keyed_state = multiply(resources.per_key_state_charged_bytes, table.max_live_keys, "per-key state reservation");
  if (!keyed_state.ok())
    return keyed_state.status();
  status = add(keyed_state.value(), "per-key state");
  if (!status.ok())
    return status;
  auto scratch =
    multiply(resources.scratch_charged_bytes_per_firing, contract.execution().max_in_flight, "scratch reservation");
  if (!scratch.ok())
    return scratch.status();
  status = add(scratch.value(), "scratch");
  if (!status.ok())
    return status;
  auto output =
    multiply(resources.output_charged_bytes, contract.execution().max_in_flight, "in-flight output buffer reservation");
  if (!output.ok())
    return output.status();
  status = add(output.value(), "in-flight output buffers");
  if (!status.ok())
    return status;
  status = add(resources.retention_charged_bytes, "retention");
  if (!status.ok())
    return status;
  if (contract.join().has_value()) {
    status = add(contract.join()->max_retained_charged_bytes_aggregate, "join retention");
    if (!status.ok())
      return status;
  }
  // M3 reorder retention is a plan-owned host-normal reservation.  It is
  // added from the frozen ReorderPlan rather than implicitly from the
  // Provider contract so the artifact exposes the exact ordinal domain,
  // gap, payload and descriptor charge together.
  const auto& calibration = contract.calibration();
  if (calibration.role == CalibrationRole::consumer) {
    auto calibration_wait = multiply(calibration.max_active_keys, calibration.precalibration_horizon_charged_bytes,
                                     "calibration progress reservoir");
    if (!calibration_wait.ok())
      return calibration_wait.status();
    status = add(calibration_wait.value(), "calibration progress reservoir");
    if (!status.ok())
      return status;
    status = add(calibration.max_calibration_frame_charged_bytes, "calibration frame");
    if (!status.ok())
      return status;
    status = add_to(plan.transport_bytes, calibration.max_decoder_staging_bytes, "calibration decoder staging");
    if (!status.ok())
      return status;
  }
  const auto terminal_bytes = contract.terminal().normal_max_output_charged_bytes;
  status = add(terminal_bytes, "terminal output bundle");
  if (!status.ok())
    return status;

  auto ordinary_descriptors =
    multiply(resources.output_items, contract.execution().max_in_flight, "in-flight output descriptor reservation");
  if (!ordinary_descriptors.ok())
    return ordinary_descriptors.status();
  status = add_to(plan.descriptor_count, ordinary_descriptors.value(), "in-flight output descriptors");
  if (!status.ok())
    return status;
  const auto terminal_descriptors = contract.terminal().normal_max_output_items;
  status = add_to(plan.descriptor_count, terminal_descriptors, "terminal output descriptors");
  if (!status.ok())
    return status;
  const auto terminal_async =
    std::max(contract.terminal().normal_max_async_tokens, contract.terminal().cancel_max_async_tokens);
  return add_to(plan.async_token_count, terminal_async, "terminal async token reservation");
}

[[nodiscard]] Status add_contract_permits(const OperatorContract& contract, ResourceVectorSpec& plan) {
  const auto& resources = contract.resources();
  const auto concurrency = contract.execution().max_in_flight;
  auto executor = multiply(resources.cpu_permits, concurrency, "executor permit reservation");
  if (!executor.ok())
    return executor.status();
  auto backend = multiply(resources.backend_gang_threads, concurrency, "backend permit reservation");
  if (!backend.ok())
    return backend.status();
  auto provider = multiply(resources.provider_private_threads, concurrency, "Provider permit reservation");
  if (!provider.ok())
    return provider.status();
  auto status = add_to(plan.cpu_leaf_permits, executor.value(), "executor permit total");
  if (!status.ok())
    return status;
  status = add_to(plan.backend_gang_permits, backend.value(), "backend permit total");
  if (!status.ok())
    return status;
  return add_to(plan.provider_private_permits, provider.value(), "Provider permit total");
}

// The normal and cancellation paths are mutually exclusive in one scan, but
// the certificate records a conservative finite upper bound over both paths.
// Count the host terminal transition for each path, every declared normal
// flush firing, and the Provider's declared asynchronous work.  This avoids
// treating terminal callbacks or bounded flush work as untracked afterthoughts.
[[nodiscard]] Result<Quantity> terminal_occurrence_bound(const OperatorContract& contract) {
  auto count = checked_add(1U, 1U, "normal and cancellation terminal transitions");
  if (!count.ok())
    return count.status();
  count =
    checked_add(count.value(), contract.rates().normal_flush.max_firings, "normal terminal flush occurrence count");
  if (!count.ok())
    return count.status();
  count =
    checked_add(count.value(), contract.terminal().normal_max_async_tokens, "normal terminal async occurrence count");
  if (!count.ok())
    return count.status();
  return checked_add(count.value(), contract.terminal().cancel_max_async_tokens,
                     "cancellation terminal async occurrence count");
}

[[nodiscard]] Status check_budget(const ResourceVectorSpec& resources, const MachinePolicy& policy) {
  auto demand = ResourceVector::create(resources, "derived resource_vector");
  if (!demand.ok())
    return demand.status();
  if (!policy.resource_capacity().can_admit(demand.value())) {
    return validation("derived ResourceVector exceeds MachinePolicy.resource_capacity in one or more domains");
  }
  return Status::Ok();
}

[[nodiscard]] Result<DerivedPlanParts> derive_plan_parts(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  DerivedPlanParts result;
  result.key_slot_tables.reserve(definition.nodes().size());
  result.reorder_plans.reserve(definition.nodes().size());
  for (const auto& node : definition.nodes()) {
    const auto* binding = find_contract_binding(request, node.id);
    const auto& contract = binding->contract;
    auto domain = derive_dense_key_domain(contract.execution(), request.scan_descriptor, request.target_envelope);
    if (!domain.ok())
      return domain.status();
    const auto max_live_keys = std::min(domain.value().cardinality, contract.execution().max_active_keys);
    if (max_live_keys == 0U) {
      return validation("node '" + node.id + "' has a zero derived KeySlotTable capacity");
    }
    auto host_metadata = dense_key_slot_host_metadata_charged_bytes(
      domain.value().cardinality, max_live_keys, "node '" + node.id + "' KeySlotTable host metadata");
    if (!host_metadata.ok())
      return host_metadata.status();
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
    auto metadata_status = add_to(result.resources.host_normal_bytes, table.host_metadata_charged_bytes,
                                  "dense KeySlotTable host metadata reservation");
    if (!metadata_status.ok())
      return metadata_status;
    auto resource_status = add_contract_resources(contract, table, result.resources);
    if (!resource_status.ok())
      return resource_status;
    if (contract.reorder().has_value()) {
      auto reorder = derive_cartesian_reorder_plan(node, contract, request.scan_descriptor);
      if (!reorder.ok()) {
        return reorder.status();
      }
      auto alignment = require_m3_key_slot_reorder_alignment(table, reorder.value());
      if (!alignment.ok()) {
        return alignment;
      }
      auto reorder_resources = add_reorder_plan_resources(reorder.value(), result.resources);
      if (!reorder_resources.ok()) {
        return reorder_resources;
      }
      result.reorder_plans.push_back(std::move(reorder).value());
    }
    auto permit_status = add_contract_permits(contract, result.resources);
    if (!permit_status.ok())
      return permit_status;
    result.key_slot_tables.push_back(std::move(table));
    auto node_terminal = terminal_occurrence_bound(contract);
    if (!node_terminal.ok())
      return node_terminal.status();
    auto terminal = add_to(result.terminal_occurrences, node_terminal.value(), "terminal occurrence count");
    if (!terminal.ok())
      return terminal;
  }

  result.edges.reserve(definition.edges().size());
  for (const auto& edge : definition.edges()) {
    const auto* source = find_contract_binding(request, edge.from.node);
    auto capacity = derive_edge_capacity(edge, source->contract);
    if (!capacity.ok())
      return capacity.status();
    // Queue capacity is a logical item/byte bound.  The producer's output
    // lease owns physical payload memory, while each queued edge charges only
    // its descriptor credit; this avoids double-accounting fan-out payloads.
    auto descriptor_status =
      add_to(result.resources.descriptor_count, capacity.value().max_items, "bounded edge descriptor reservation");
    if (!descriptor_status.ok())
      return descriptor_status;
    result.edges.push_back(std::move(capacity).value());
  }
  auto decoder = add_to(result.resources.transport_bytes, request.target_envelope.max_decoder_staging_bytes(),
                        "decoder staging reservation");
  if (!decoder.ok())
    return decoder;
  auto sink = add_to(result.resources.transport_bytes,
                     request.target_envelope.sink_service_assumption().transport_staging_bytes(),
                     "host transport staging reservation");
  if (!sink.ok())
    return sink;
  auto io = add_to(result.resources.io_slots, 1U, "ingress/egress I/O slot reservation");
  if (!io.ok())
    return io;

  std::ranges::sort(result.key_slot_tables, {}, &KeySlotTablePlanSpec::node_id);
  std::ranges::sort(result.reorder_plans, {}, &ReorderPlanSpec::node_id);
  std::ranges::sort(result.edges, {}, &EdgeCapacitySpec::edge_id);
  result.obligations = {
    "PO-01.typed_ports",
    "PO-04.finite_bounds",
    "PO-05.resource_vector",
    "PO-06.dense_key_slots",
    "PO-08.bounded_dependency_progress",
    "PO-12.permit_budget",
  };
  if (!result.reorder_plans.empty()) {
    result.obligations.insert(result.obligations.begin() + 4, std::string{kM3CompletedFrameSlotBindingProofObligation});
    result.obligations.insert(result.obligations.begin() + 5, std::string{kM3StrictDenseAllTuplesEoiRuntimeAssumption});
  }
  return result;
}

[[nodiscard]] ExecutionPlanSpec execution_plan_specification(const PlanBuildRequest& request,
                                                             const DerivedPlanParts& parts) {
  std::vector<std::string> provider_contracts;
  provider_contracts.reserve(request.operator_contracts.size());
  for (const auto& binding : request.operator_contracts) {
    provider_contracts.push_back(binding.contract_digest.value());
  }
  std::ranges::sort(provider_contracts);
  provider_contracts.erase(std::unique(provider_contracts.begin(), provider_contracts.end()), provider_contracts.end());
  return ExecutionPlanSpec{
    .inputs = {.resolved_pipeline = request.resolved_pipeline.digest().value(),
               .scan_descriptor = request.artifact_digests.scan_descriptor.value(),
               .target_envelope = request.artifact_digests.target_envelope.value(),
               .machine_policy = request.artifact_digests.machine_policy.value(),
               .provider_contracts = std::move(provider_contracts)},
    .execution_profile = request.requested_profile,
    .key_slot_tables = parts.key_slot_tables,
    .reorder_plans = parts.reorder_plans,
    .edge_capacities = parts.edges,
    .resource_vector = parts.resources,
    .terminal_occurrences = parts.terminal_occurrences,
    .proof_obligations = parts.obligations,
  };
}

[[nodiscard]] Result<ExecutionPlan> make_plan(const PlanBuildRequest& request, const DerivedPlanParts& parts) {
  const auto specification = execution_plan_specification(request, parts);
  auto placeholder = ArtifactDigest::parse("sha256:0000000000000000000000000000000000000000000000000000000000000000",
                                           "ExecutionPlan canonical serialization placeholder");
  if (!placeholder.ok()) {
    return placeholder.status();
  }
  auto plan_for_serialization = ExecutionPlan::create(std::move(placeholder).value(), specification);
  if (!plan_for_serialization.ok()) {
    return plan_for_serialization.status();
  }
  auto canonical = serialize_execution_plan_canonical_json(plan_for_serialization.value());
  if (!canonical.ok())
    return canonical.status();
  auto digest = domain_separated_sha256_digest("kspacejet:artifact:execution-plan:1", canonical.value(),
                                               "ExecutionPlan compiler output");
  if (!digest.ok())
    return digest.status();
  return ExecutionPlan::create(std::move(digest).value(), specification);
}

} // namespace

Result<CompiledExecutionPlan> ExecutionPlanCompiler::compile(const PlanBuildRequest& request) {
  const auto pre_ingress = validate_pre_ingress_scan_envelope(request);
  if (!pre_ingress.ok())
    return pre_ingress;
  const auto bindings = validate_binding_set(request);
  if (!bindings.ok())
    return bindings;
  const auto calibration = validate_calibration_bindings(request);
  if (!calibration.ok())
    return calibration;
  auto parts = derive_plan_parts(request);
  if (!parts.ok())
    return parts.status();
  const auto budget = check_budget(parts.value().resources, request.machine_policy);
  if (!budget.ok())
    return budget;
  auto plan = make_plan(request, parts.value());
  if (!plan.ok())
    return plan.status();
  return CompiledExecutionPlan{.plan = std::move(plan).value()};
}

Status ExecutionPlanCompiler::deterministic_recheck(const CompiledExecutionPlan& compiled,
                                                    const PlanBuildRequest& request) {
  auto expected = compile(request);
  if (!expected.ok())
    return expected.status();
  if (expected.value().plan.digest() != compiled.plan.digest()) {
    return validation("ExecutionPlan digest does not match deterministic compiler output");
  }
  return Status::Ok();
}

} // namespace ksj::recon::graph
