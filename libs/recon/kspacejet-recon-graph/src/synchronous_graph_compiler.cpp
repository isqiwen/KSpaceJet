#include "synchronous_graph_plan_internal.hpp"

#include "kspacejet/recon/graph/artifact_json.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon::graph::detail {
namespace {

struct PortAddress final {
  std::string node_id;
  std::string port_name;

  friend bool operator<(const PortAddress& left, const PortAddress& right) noexcept {
    return left.node_id != right.node_id ? left.node_id < right.node_id : left.port_name < right.port_name;
  }
};

enum class InputRouteKind : std::uint8_t { data_edge, calibration_artifact };
enum class OutputRouteKind : std::uint8_t { data_edge, calibration_artifact };

struct InputRoute final {
  InputRouteKind kind;
  std::string source_id;
  std::string source_pool_id;
  const TypeDescriptor* type{nullptr};
};

struct OutputRoute final {
  OutputRouteKind kind;
  std::string destination_id;
  std::string pool_id;
  const TypeDescriptor* type{nullptr};
};

struct NodeContext final {
  const PipelineNode* node{nullptr};
  const ResolvedProvider* provider{nullptr};
  const OperatorContractBinding* contract{nullptr};
  const NodePlanningRequirementsBinding* requirements{nullptr};
};

struct PortEnvelope final {
  Quantity ordinary_items{0U};
  Quantity ordinary_bytes{0U};
  Quantity terminal_items{0U};
  Quantity terminal_bytes{0U};
};

struct DerivedSynchronousPlan final {
  std::vector<SynchronousNodePlanSpec> nodes;
  std::vector<SynchronousBufferPoolPlanSpec> pools;
  std::vector<SynchronousDataEdgePlanSpec> edges;
  std::vector<CalibrationArtifactBindingPlanSpec> artifacts;
  ResourceVectorSpec resources;
  Quantity terminal_occurrences{0U};
  std::vector<std::string> obligations;
};

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

[[nodiscard]] bool contains(const std::vector<ExecutionProfile>& values, const ExecutionProfile wanted) {
  return std::find(values.begin(), values.end(), wanted) != values.end();
}

[[nodiscard]] Result<Quantity> add_to(Quantity& destination, const Quantity value, const std::string_view expression) {
  auto total = checked_add(destination, value, expression);
  if (!total.ok())
    return total.status();
  destination = total.value();
  return destination;
}

[[nodiscard]] const ResolvedPort* find_port(const OperatorContract& contract, const std::string_view port_name) {
  const auto found = std::find_if(contract.ports().begin(), contract.ports().end(), [&](const ResolvedPort& port) {
    return port.name == port_name;
  });
  return found == contract.ports().end() ? nullptr : &*found;
}

[[nodiscard]] Result<Quantity> abi_port(const OperatorContract& contract, const std::string_view port_name,
                                        const PortDirection direction) {
  Quantity position{0U};
  for (const auto& port : contract.ports()) {
    if (port.direction != direction)
      continue;
    if (port.name == port_name)
      return position;
    auto next = checked_add(position, 1U, "generic Provider ABI port position");
    if (!next.ok())
      return next.status();
    position = next.value();
  }
  return validation("Resolved OperatorContract does not contain the requested directional port '" +
                    std::string(port_name) + "'.");
}

[[nodiscard]] const ResolvedProvider* find_provider(const ResolvedPipeline& pipeline, const std::string_view alias) {
  const auto found =
    std::find_if(pipeline.providers().begin(), pipeline.providers().end(), [&](const ResolvedProvider& provider) {
      return provider.alias == alias;
    });
  return found == pipeline.providers().end() ? nullptr : &*found;
}

[[nodiscard]] const ResolvedOperator* find_operator(const ResolvedProvider& provider,
                                                    const std::string_view operator_id) {
  const auto found =
    std::find_if(provider.operators.begin(), provider.operators.end(), [&](const ResolvedOperator& value) {
      return value.id == operator_id;
    });
  return found == provider.operators.end() ? nullptr : &*found;
}

[[nodiscard]] const OperatorContractBinding* find_contract_binding(const PlanBuildRequest& request,
                                                                   const std::string_view node_id) {
  const auto found = std::find_if(request.operator_contract_bindings.begin(), request.operator_contract_bindings.end(),
                                  [&](const OperatorContractBinding& binding) {
                                    return binding.node_id == node_id;
                                  });
  return found == request.operator_contract_bindings.end() ? nullptr : &*found;
}

[[nodiscard]] const NodePlanningRequirementsBinding* find_requirements_binding(const PlanBuildRequest& request,
                                                                               const std::string_view node_id) {
  const auto found = std::find_if(request.node_planning_requirements.begin(), request.node_planning_requirements.end(),
                                  [&](const NodePlanningRequirementsBinding& binding) {
                                    return binding.node_id == node_id;
                                  });
  return found == request.node_planning_requirements.end() ? nullptr : &*found;
}

[[nodiscard]] Status validate_runtime_buffer_type(const TypeDescriptor& type, const std::string_view context) {
  auto registered = types::resolve(type.type_ref().value());
  if (!registered.ok()) {
    return validation(std::string(context) + " names a TypeRef absent from the checked-in type registry.");
  }
  if (!type.exactly_matches(registered.value()) || type.payload_kind() != PayloadKind::buffer_handle ||
      type.mutability() != PayloadMutability::immutable_after_publish ||
      std::find(type.allowed_memory_domains().begin(), type.allowed_memory_domains().end(),
                TypeMemoryDomain::host_normal) == type.allowed_memory_domains().end()) {
    return validation(std::string(context) +
                      " must use the exact immutable host-normal buffer TypeDescriptor registered for its TypeRef.");
  }
  return Status::Ok();
}

[[nodiscard]] Result<TypeDescriptor> resolve_runtime_ingress_or_egress_type(const std::string_view type_ref,
                                                                            const std::string_view context) {
  auto descriptor = types::resolve(type_ref);
  if (!descriptor.ok()) {
    return validation(std::string(context) + " does not resolve to a checked-in TypeDescriptor.");
  }
  const auto status = validate_runtime_buffer_type(descriptor.value(), context);
  if (!status.ok())
    return status;
  return descriptor;
}

[[nodiscard]] Result<std::vector<NodeContext>> validate_node_contexts(const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  if (!contains(definition.allowed_profiles(), request.requested_profile) ||
      !request.machine_policy.allows(request.requested_profile)) {
    return validation("The requested execution profile is not allowed by the pipeline definition and machine policy.");
  }
  if (request.operator_contract_bindings.size() != definition.nodes().size() ||
      request.node_planning_requirements.size() != definition.nodes().size()) {
    return validation("OperatorContract and NodePlanningRequirements bindings must each exactly cover pipeline nodes.");
  }
  std::set<std::string> contracts;
  std::set<std::string> requirements;
  std::vector<NodeContext> result;
  result.reserve(definition.nodes().size());
  for (const auto& node : definition.nodes()) {
    const auto* provider = find_provider(request.resolved_pipeline, node.provider_alias);
    const auto* contract = find_contract_binding(request, node.id);
    const auto* requirement = find_requirements_binding(request, node.id);
    if (provider == nullptr || contract == nullptr || requirement == nullptr || !contracts.insert(node.id).second ||
        !requirements.insert(node.id).second) {
      return validation("Every pipeline node needs one unique resolved Provider, contract, and planning binding.");
    }
    const auto* resolved_operator = find_operator(*provider, node.operator_id);
    if (resolved_operator == nullptr || contract->contract.operator_id() != node.operator_id) {
      return validation("Node '" + node.id + "' does not match its resolved Provider operator.");
    }
    const auto requirements_status = requirement->requirements.validate_against(contract->contract);
    if (!requirements_status.ok())
      return requirements_status;
    if (requirement->requirements.resources().memory_domain != MemoryDomain::host) {
      return validation("The current synchronous executor supports host-owned node resources only ('" + node.id +
                        "').");
    }
    if (requirement->requirements.terminal().normal_max_async_tokens != 0U ||
        requirement->requirements.terminal().cancel_max_async_tokens != 0U) {
      return validation("The current synchronous executor does not admit asynchronous terminal work ('" + node.id +
                        "').");
    }
    result.push_back(
      NodeContext{.node = &node, .provider = provider, .contract = contract, .requirements = requirement});
  }
  for (const auto& binding : request.operator_contract_bindings) {
    if (!contracts.contains(binding.node_id))
      return validation("OperatorContract binding references an unknown node.");
  }
  for (const auto& binding : request.node_planning_requirements) {
    if (!requirements.contains(binding.node_id))
      return validation("NodePlanningRequirements binding references an unknown node.");
  }
  return result;
}

[[nodiscard]] const NodeContext* find_node_context(const std::vector<NodeContext>& nodes,
                                                   const std::string_view node_id) {
  const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const NodeContext& node) {
    return node.node->id == node_id;
  });
  return found == nodes.end() ? nullptr : &*found;
}

[[nodiscard]] Result<PortEnvelope> port_envelope(const NodePlanningRequirements& requirements,
                                                 const std::string_view port_name) {
  PortEnvelope result;
  const auto consider = [&](const std::vector<PortRateSpec>& entries, Quantity& items, Quantity& bytes,
                            const std::string_view context) -> Status {
    const auto entry = std::find_if(entries.begin(), entries.end(), [&](const PortRateSpec& value) {
      return value.port_name == port_name;
    });
    if (entry == entries.end())
      return Status::Ok();
    if (entry->items > 1U) {
      return validation("The current synchronous executor supports one output item per port/firing ('" +
                        std::string(port_name) + "' in " + std::string(context) + ").");
    }
    items = std::max(items, entry->items);
    bytes = std::max(bytes, entry->charged_bytes);
    return Status::Ok();
  };
  const auto& rates = requirements.rates();
  if (rates.kind == RateKind::keyed_dynamic) {
    const auto ordinary =
      consider(rates.ordinary.outputs, result.ordinary_items, result.ordinary_bytes, "rates.ordinary.outputs");
    if (!ordinary.ok())
      return ordinary;
    const auto terminal =
      consider(rates.normal_flush.outputs, result.terminal_items, result.terminal_bytes, "rates.normal_flush.outputs");
    if (!terminal.ok())
      return terminal;
  } else {
    for (const auto& phase : rates.static_phases) {
      const auto ordinary =
        consider(phase.outputs, result.ordinary_items, result.ordinary_bytes, "rates.static_phases.outputs");
      if (!ordinary.ok())
        return ordinary;
    }
  }
  if ((result.ordinary_items != 0U && result.ordinary_bytes == 0U) ||
      (result.terminal_items != 0U && result.terminal_bytes == 0U)) {
    return validation("Output port '" + std::string(port_name) + "' has a zero byte envelope.");
  }
  return result;
}

// Dynamic transport is bounded by the scan ingress envelope.  A calibration
// artifact is not a second frame stream: it is a sealed value retained in the
// artifact store and its physical capacity is frozen by its producing output
// pool.  keyed_dynamic requirements deliberately have no input-rate table, so
// there is no consumer-local byte bound to derive for that static source.
[[nodiscard]] Result<Quantity> input_envelope_bytes(const NodePlanningRequirements& requirements,
                                                    const std::string_view port_name,
                                                    const TargetEnvelope& target_envelope,
                                                    const InputRouteKind source_kind) {
  Quantity bytes{0U};
  const auto& rates = requirements.rates();
  if (rates.kind == RateKind::keyed_dynamic) {
    if (source_kind == InputRouteKind::calibration_artifact)
      return Quantity{0U};
    if (target_envelope.max_frame_charged_bytes() == 0U) {
      return validation("A keyed-dynamic ingress/input requires TargetEnvelope.max_frame_charged_bytes.");
    }
    return target_envelope.max_frame_charged_bytes();
  }
  for (const auto& phase : rates.static_phases) {
    const auto entry = std::find_if(phase.inputs.begin(), phase.inputs.end(), [&](const PortRateSpec& value) {
      return value.port_name == port_name;
    });
    if (entry == phase.inputs.end() || entry->items != 1U || entry->charged_bytes == 0U) {
      return validation("Every static input port must consume exactly one positive-byte item per firing ('" +
                        std::string(port_name) + "').");
    }
    bytes = std::max(bytes, entry->charged_bytes);
  }
  if (bytes == 0U)
    return validation("Input port has no finite envelope ('" + std::string(port_name) + "').");
  return bytes;
}

[[nodiscard]] Status validate_node_activation_bounds(const NodeContext& node, const std::size_t input_count) {
  if (input_count == 0U ||
      input_count > kSynchronousMaximumDynamicInputEdgesPerNode + node.contract->contract.ports().size()) {
    return validation("Node has an invalid frozen input count.");
  }
  if (node.requirements->requirements.execution().max_items_per_activation < input_count ||
      node.requirements->requirements.batch().max_items < input_count) {
    return validation("Node '" + node.node->id +
                      "' does not reserve enough activation/batch items for all named inputs.");
  }
  return Status::Ok();
}

[[nodiscard]] Result<Quantity> pool_host_metadata(const Quantity slots, const std::string_view context) {
  return synchronous_buffer_pool_host_metadata_charged_bytes(slots, context);
}

[[nodiscard]] Result<Quantity> pool_physical_charge(const Quantity slots, const Quantity payload,
                                                    const std::string_view context) {
  return synchronous_buffer_pool_physical_charge_bytes(slots, payload, 0U, context);
}

[[nodiscard]] Result<SynchronousBufferPoolPlanSpec>
make_pool(std::string pool_id, const SynchronousDataEndpointKind owner_kind, std::string owner_id,
          std::string owner_port_name, const TypeDescriptor& type, const Quantity payload_capacity) {
  constexpr Quantity slots = 1U;
  auto metadata = pool_host_metadata(slots, "synchronous pool metadata");
  if (!metadata.ok())
    return metadata.status();
  auto physical = pool_physical_charge(slots, payload_capacity, "synchronous pool physical charge");
  if (!physical.ok())
    return physical.status();
  return SynchronousBufferPoolPlanSpec{
    .pool_id = std::move(pool_id),
    .owner_kind = owner_kind,
    .owner_id = std::move(owner_id),
    .owner_port_name = std::move(owner_port_name),
    .type_descriptor = type,
    .memory_domain = TypeMemoryDomain::host_normal,
    .slot_count = slots,
    .payload_capacity_bytes = payload_capacity,
    .metadata_capacity_bytes = 0U,
    .payload_alignment_bytes = type.min_alignment_bytes(),
    .storage_accounting_id = "kspacejet.buffer-pool-storage/host-normal",
    .host_metadata_charged_bytes = metadata.value(),
    .descriptor_charged_count = slots,
    .physical_charge_bytes = physical.value(),
  };
}

[[nodiscard]] Result<SynchronousDataEdgePlanSpec>
make_edge(std::string edge_id, std::string source_pool_id, const SynchronousDataEndpointKind producer_kind,
          std::string producer_id, std::string producer_port_name, const Quantity producer_abi_port,
          const SynchronousDataEndpointKind consumer_kind, std::string consumer_id, std::string consumer_port_name,
          const Quantity consumer_abi_port, const TypeDescriptor& type, const Quantity payload_capacity) {
  constexpr Quantity max_items = 1U;
  auto metadata = synchronous_data_edge_host_metadata_charged_bytes(max_items, "synchronous edge metadata");
  if (!metadata.ok())
    return metadata.status();
  return SynchronousDataEdgePlanSpec{
    .edge_id = std::move(edge_id),
    .source_pool_id = std::move(source_pool_id),
    .producer_kind = producer_kind,
    .producer_id = std::move(producer_id),
    .producer_port_name = std::move(producer_port_name),
    .producer_abi_port = producer_abi_port,
    .consumer_kind = consumer_kind,
    .consumer_id = std::move(consumer_id),
    .consumer_port_name = std::move(consumer_port_name),
    .consumer_abi_port = consumer_abi_port,
    .type_descriptor = type,
    .max_items = max_items,
    .max_logical_bytes = payload_capacity,
    .storage_accounting_id = "kspacejet.data-edge-storage/fixed-fifo",
    .host_metadata_charged_bytes = metadata.value(),
    .descriptor_charged_count = max_items,
    .terminal_policy = "normal-eoi-drain-cancellation-fail",
  };
}

[[nodiscard]] Status add_dependency(std::map<std::string, std::vector<std::string>>& dependencies,
                                    const std::string& producer, const std::string& consumer) {
  auto& values = dependencies[producer];
  if (std::find(values.begin(), values.end(), consumer) == values.end())
    values.push_back(consumer);
  return Status::Ok();
}

[[nodiscard]] Status verify_acyclic_dependencies(const std::vector<NodeContext>& nodes,
                                                 std::map<std::string, std::vector<std::string>> dependencies) {
  std::map<std::string, Quantity> indegrees;
  for (const auto& node : nodes)
    indegrees.emplace(node.node->id, 0U);
  for (const auto& [source, targets] : dependencies) {
    if (!indegrees.contains(source))
      return validation("Graph dependency has an unknown source node.");
    for (const auto& target : targets) {
      const auto found = indegrees.find(target);
      if (found == indegrees.end())
        return validation("Graph dependency has an unknown destination node.");
      auto next = checked_add(found->second, 1U, "graph dependency indegree");
      if (!next.ok())
        return next.status();
      found->second = next.value();
    }
  }
  std::queue<std::string> ready;
  for (const auto& [id, degree] : indegrees) {
    if (degree == 0U)
      ready.push(id);
  }
  Quantity visited{0U};
  while (!ready.empty()) {
    const auto id = ready.front();
    ready.pop();
    auto next_visited = checked_add(visited, 1U, "acyclic dependency visit count");
    if (!next_visited.ok())
      return next_visited.status();
    visited = next_visited.value();
    for (const auto& target : dependencies[id]) {
      auto& degree = indegrees.at(target);
      --degree;
      if (degree == 0U)
        ready.push(target);
    }
  }
  if (visited != nodes.size())
    return validation("Data and calibration dependencies must form one acyclic graph.");
  return Status::Ok();
}

[[nodiscard]] Result<DerivedSynchronousPlan> derive_synchronous_plan(const PlanBuildRequest& request) {
  if (request.scan_descriptor.source_xml_bytes() > request.target_envelope.max_xml_bytes()) {
    return validation("ScanDescriptor source XML byte length exceeds TargetEnvelope.max_xml_bytes before ingress.");
  }
  auto nodes = validate_node_contexts(request);
  if (!nodes.ok())
    return nodes.status();
  const auto& definition = request.resolved_pipeline.definition();

  std::map<PortAddress, InputRoute> inputs;
  std::map<PortAddress, OutputRoute> outputs;
  std::map<std::string, const CalibrationBinding*> calibrations;
  std::map<std::string, std::vector<std::string>> dependencies;
  const auto insert_input = [&](PortAddress address, InputRoute route) -> Status {
    if (!inputs.emplace(std::move(address), std::move(route)).second) {
      return validation("Every declared input port must have exactly one explicit data or calibration source.");
    }
    return Status::Ok();
  };
  const auto insert_output = [&](PortAddress address, OutputRoute route) -> Status {
    if (!outputs.emplace(std::move(address), std::move(route)).second) {
      return validation("Fan-out is not implemented: every declared output port has exactly one destination.");
    }
    return Status::Ok();
  };

  for (const auto& edge : definition.edges()) {
    const auto* source = find_node_context(nodes.value(), edge.from.node);
    const auto* target = find_node_context(nodes.value(), edge.to.node);
    const auto* source_port = source == nullptr ? nullptr : find_port(source->contract->contract, edge.from.port);
    const auto* target_port = target == nullptr ? nullptr : find_port(target->contract->contract, edge.to.port);
    if (source_port == nullptr || target_port == nullptr || source_port->direction != PortDirection::output ||
        target_port->direction != PortDirection::input ||
        !source_port->type_descriptor.exactly_matches(target_port->type_descriptor)) {
      return validation("Graph edge '" + edge.id + "' must join exact resolved output and input TypeDescriptors.");
    }
    const auto type_status = validate_runtime_buffer_type(source_port->type_descriptor, "Graph edge '" + edge.id + "'");
    if (!type_status.ok())
      return type_status;
    const std::string pool_id = "pool:node:" + edge.from.node + ":" + edge.from.port;
    const std::string edge_id = "edge:graph:" + edge.id;
    auto input_status = insert_input({edge.to.node, edge.to.port}, {.kind = InputRouteKind::data_edge,
                                                                    .source_id = edge_id,
                                                                    .source_pool_id = pool_id,
                                                                    .type = &source_port->type_descriptor});
    if (!input_status.ok())
      return input_status;
    auto output_status = insert_output({edge.from.node, edge.from.port}, {.kind = OutputRouteKind::data_edge,
                                                                          .destination_id = edge_id,
                                                                          .pool_id = pool_id,
                                                                          .type = &source_port->type_descriptor});
    if (!output_status.ok())
      return output_status;
    const auto dependency = add_dependency(dependencies, edge.from.node, edge.to.node);
    if (!dependency.ok())
      return dependency;
  }

  for (const auto& ingress : definition.ingress_ports()) {
    const auto* target = find_node_context(nodes.value(), ingress.to.node);
    const auto* target_port = target == nullptr ? nullptr : find_port(target->contract->contract, ingress.to.port);
    auto ingress_type = resolve_runtime_ingress_or_egress_type(ingress.type, "Ingress '" + ingress.id + "'");
    if (!ingress_type.ok())
      return ingress_type.status();
    if (target_port == nullptr || target_port->direction != PortDirection::input ||
        !target_port->type_descriptor.exactly_matches(ingress_type.value())) {
      return validation("Ingress '" + ingress.id +
                        "' must exactly match its target OperatorContract input TypeDescriptor.");
    }
    auto input_status =
      insert_input({ingress.to.node, ingress.to.port}, {.kind = InputRouteKind::data_edge,
                                                        .source_id = "edge:ingress:" + ingress.id,
                                                        .source_pool_id = "pool:ingress:" + ingress.id,
                                                        .type = &target_port->type_descriptor});
    if (!input_status.ok())
      return input_status;
  }

  for (const auto& egress : definition.egress_ports()) {
    const auto* source = find_node_context(nodes.value(), egress.from.node);
    const auto* source_port = source == nullptr ? nullptr : find_port(source->contract->contract, egress.from.port);
    auto egress_type = resolve_runtime_ingress_or_egress_type(egress.type, "Egress '" + egress.id + "'");
    if (!egress_type.ok())
      return egress_type.status();
    if (source_port == nullptr || source_port->direction != PortDirection::output ||
        !source_port->type_descriptor.exactly_matches(egress_type.value())) {
      return validation("Egress '" + egress.id +
                        "' must exactly match its source OperatorContract output TypeDescriptor.");
    }
    auto output_status = insert_output({egress.from.node, egress.from.port},
                                       {.kind = OutputRouteKind::data_edge,
                                        .destination_id = "edge:egress:" + egress.id,
                                        .pool_id = "pool:node:" + egress.from.node + ":" + egress.from.port,
                                        .type = &source_port->type_descriptor});
    if (!output_status.ok())
      return output_status;
  }

  for (const auto& binding : definition.calibration_bindings()) {
    if (!calibrations.emplace(binding.id, &binding).second) {
      return validation("Calibration binding ids must be unique.");
    }
    const auto* producer = find_node_context(nodes.value(), binding.producer.node);
    const auto* producer_port =
      producer == nullptr ? nullptr : find_port(producer->contract->contract, binding.producer.port);
    if (producer_port == nullptr || producer_port->direction != PortDirection::output) {
      return validation("Calibration binding '" + binding.id + "' must name one resolved producer output port.");
    }
    const auto type_status =
      validate_runtime_buffer_type(producer_port->type_descriptor, "Calibration binding '" + binding.id + "'");
    if (!type_status.ok())
      return type_status;
    const std::string pool_id = "pool:node:" + binding.producer.node + ":" + binding.producer.port;
    auto output_status =
      insert_output({binding.producer.node, binding.producer.port}, {.kind = OutputRouteKind::calibration_artifact,
                                                                     .destination_id = binding.id,
                                                                     .pool_id = pool_id,
                                                                     .type = &producer_port->type_descriptor});
    if (!output_status.ok())
      return output_status;
    if (binding.consumers.empty())
      return validation("Calibration binding '" + binding.id + "' has no consumers.");
    std::set<PortAddress> consumers;
    for (const auto& consumer_endpoint : binding.consumers) {
      const auto* consumer = find_node_context(nodes.value(), consumer_endpoint.node);
      const auto* consumer_port =
        consumer == nullptr ? nullptr : find_port(consumer->contract->contract, consumer_endpoint.port);
      if (consumer_port == nullptr || consumer_port->direction != PortDirection::input ||
          !consumer_port->type_descriptor.exactly_matches(producer_port->type_descriptor) ||
          !consumers.insert({consumer_endpoint.node, consumer_endpoint.port}).second) {
        return validation("Calibration binding '" + binding.id +
                          "' must name unique exact-TypeDescriptor consumer input ports.");
      }
      auto input_status =
        insert_input({consumer_endpoint.node, consumer_endpoint.port}, {.kind = InputRouteKind::calibration_artifact,
                                                                        .source_id = binding.id,
                                                                        .source_pool_id = pool_id,
                                                                        .type = &producer_port->type_descriptor});
      if (!input_status.ok())
        return input_status;
      const auto dependency = add_dependency(dependencies, binding.producer.node, consumer_endpoint.node);
      if (!dependency.ok())
        return dependency;
    }
  }

  for (const auto& node : nodes.value()) {
    for (const auto& port : node.contract->contract.ports()) {
      const PortAddress endpoint{node.node->id, port.name};
      if (port.direction == PortDirection::input && !inputs.contains(endpoint)) {
        return validation("Declared input port '" + node.node->id + "." + port.name + "' is not explicitly bound.");
      }
      if (port.direction == PortDirection::output && !outputs.contains(endpoint)) {
        return validation("Declared output port '" + node.node->id + "." + port.name + "' is not explicitly bound.");
      }
    }
  }
  const auto acyclic = verify_acyclic_dependencies(nodes.value(), std::move(dependencies));
  if (!acyclic.ok())
    return acyclic;

  DerivedSynchronousPlan result;
  std::map<std::string, SynchronousBufferPoolPlanSpec> pools;
  std::map<PortAddress, PortEnvelope> envelopes;
  for (const auto& node : nodes.value()) {
    for (const auto& port : node.contract->contract.ports()) {
      if (port.direction != PortDirection::output)
        continue;
      auto envelope = port_envelope(node.requirements->requirements, port.name);
      if (!envelope.ok())
        return envelope.status();
      if (envelope.value().ordinary_items > 1U || envelope.value().terminal_items > 1U ||
          (envelope.value().ordinary_items == 0U && envelope.value().terminal_items == 0U)) {
        return validation("Output port '" + node.node->id + "." + port.name +
                          " requires an unsupported zero or multi-item output envelope.");
      }
      const auto route = outputs.at({node.node->id, port.name});
      const Quantity capacity = std::max(envelope.value().ordinary_bytes, envelope.value().terminal_bytes);
      auto pool = make_pool(route.pool_id, SynchronousDataEndpointKind::node, node.node->id, port.name,
                            port.type_descriptor, capacity);
      if (!pool.ok())
        return pool.status();
      if (!pools.emplace(route.pool_id, std::move(pool).value()).second) {
        return validation("A synchronous output pool is shared by distinct output ports.");
      }
      envelopes.emplace(PortAddress{node.node->id, port.name}, envelope.value());
    }
  }

  for (const auto& ingress : definition.ingress_ports()) {
    const auto* target = find_node_context(nodes.value(), ingress.to.node);
    const auto bytes = input_envelope_bytes(target->requirements->requirements, ingress.to.port,
                                            request.target_envelope, InputRouteKind::data_edge);
    if (!bytes.ok())
      return bytes.status();
    const auto route = inputs.at({ingress.to.node, ingress.to.port});
    auto pool =
      make_pool(route.source_pool_id, SynchronousDataEndpointKind::ingress, ingress.id, "", *route.type, bytes.value());
    if (!pool.ok())
      return pool.status();
    if (!pools.emplace(route.source_pool_id, std::move(pool).value()).second) {
      return validation("A synchronous ingress pool id is duplicated.");
    }
  }

  for (const auto& edge : definition.edges()) {
    const auto* source = find_node_context(nodes.value(), edge.from.node);
    const auto* target = find_node_context(nodes.value(), edge.to.node);
    const auto& route = outputs.at({edge.from.node, edge.from.port});
    const auto& pool = pools.at(route.pool_id);
    auto producer_port = abi_port(source->contract->contract, edge.from.port, PortDirection::output);
    auto consumer_port = abi_port(target->contract->contract, edge.to.port, PortDirection::input);
    if (!producer_port.ok())
      return producer_port.status();
    if (!consumer_port.ok())
      return consumer_port.status();
    auto data_edge = make_edge(route.destination_id, route.pool_id, SynchronousDataEndpointKind::node, edge.from.node,
                               edge.from.port, producer_port.value(), SynchronousDataEndpointKind::node, edge.to.node,
                               edge.to.port, consumer_port.value(), *route.type, pool.payload_capacity_bytes);
    if (!data_edge.ok())
      return data_edge.status();
    result.edges.push_back(std::move(data_edge).value());
  }
  for (const auto& ingress : definition.ingress_ports()) {
    const auto* target = find_node_context(nodes.value(), ingress.to.node);
    const auto& route = inputs.at({ingress.to.node, ingress.to.port});
    const auto& pool = pools.at(route.source_pool_id);
    auto consumer_port = abi_port(target->contract->contract, ingress.to.port, PortDirection::input);
    if (!consumer_port.ok())
      return consumer_port.status();
    auto data_edge = make_edge(route.source_id, route.source_pool_id, SynchronousDataEndpointKind::ingress, ingress.id,
                               "", 0U, SynchronousDataEndpointKind::node, ingress.to.node, ingress.to.port,
                               consumer_port.value(), *route.type, pool.payload_capacity_bytes);
    if (!data_edge.ok())
      return data_edge.status();
    result.edges.push_back(std::move(data_edge).value());
  }
  for (const auto& egress : definition.egress_ports()) {
    const auto* source = find_node_context(nodes.value(), egress.from.node);
    const auto& route = outputs.at({egress.from.node, egress.from.port});
    const auto& pool = pools.at(route.pool_id);
    auto producer_port = abi_port(source->contract->contract, egress.from.port, PortDirection::output);
    if (!producer_port.ok())
      return producer_port.status();
    auto data_edge = make_edge(route.destination_id, route.pool_id, SynchronousDataEndpointKind::node, egress.from.node,
                               egress.from.port, producer_port.value(), SynchronousDataEndpointKind::egress, egress.id,
                               "", 0U, *route.type, pool.payload_capacity_bytes);
    if (!data_edge.ok())
      return data_edge.status();
    result.edges.push_back(std::move(data_edge).value());
  }

  for (const auto& binding : definition.calibration_bindings()) {
    const auto* producer = find_node_context(nodes.value(), binding.producer.node);
    const auto& route = outputs.at({binding.producer.node, binding.producer.port});
    auto producer_port = abi_port(producer->contract->contract, binding.producer.port, PortDirection::output);
    if (!producer_port.ok())
      return producer_port.status();
    result.artifacts.push_back({.binding_id = binding.id,
                                .producer_node_id = binding.producer.node,
                                .producer_port_name = binding.producer.port,
                                .producer_abi_port = producer_port.value(),
                                .producer_pool_id = route.pool_id,
                                .type_descriptor = *route.type,
                                .host_metadata_charged_bytes = 128U,
                                .descriptor_charged_count = 1U});
  }

  for (auto& [pool_id, pool] : pools)
    result.pools.push_back(std::move(pool));
  std::sort(result.edges.begin(), result.edges.end(), [](const auto& left, const auto& right) {
    return left.edge_id < right.edge_id;
  });
  std::sort(result.artifacts.begin(), result.artifacts.end(), [](const auto& left, const auto& right) {
    return left.binding_id < right.binding_id;
  });

  Quantity host_bytes{0U};
  Quantity descriptors{0U};
  for (const auto& pool : result.pools) {
    auto status = add_to(host_bytes, pool.physical_charge_bytes, "synchronous pool resource accounting");
    if (!status.ok())
      return status.status();
    status = add_to(descriptors, pool.descriptor_charged_count, "synchronous pool descriptor accounting");
    if (!status.ok())
      return status.status();
  }
  for (const auto& edge : result.edges) {
    auto status = add_to(host_bytes, edge.host_metadata_charged_bytes, "synchronous edge resource accounting");
    if (!status.ok())
      return status.status();
    status = add_to(descriptors, edge.descriptor_charged_count, "synchronous edge descriptor accounting");
    if (!status.ok())
      return status.status();
  }
  for (const auto& artifact : result.artifacts) {
    auto status = add_to(host_bytes, artifact.host_metadata_charged_bytes, "calibration artifact resource accounting");
    if (!status.ok())
      return status.status();
    status = add_to(descriptors, artifact.descriptor_charged_count, "calibration artifact descriptor accounting");
    if (!status.ok())
      return status.status();
  }

  for (const auto& node : nodes.value()) {
    std::vector<SynchronousNodeInputBindingPlanSpec> node_inputs;
    std::vector<SynchronousNodeOutputBindingPlanSpec> node_outputs;
    Quantity dynamic_inputs{0U};
    Quantity input_payload_bytes{0U};
    Quantity output_capacity_bytes{0U};
    for (const auto& port : node.contract->contract.ports()) {
      if (port.direction != PortDirection::input)
        continue;
      const auto& route = inputs.at({node.node->id, port.name});
      auto port_position = abi_port(node.contract->contract, port.name, PortDirection::input);
      if (!port_position.ok())
        return port_position.status();
      const auto input_bytes =
        input_envelope_bytes(node.requirements->requirements, port.name, request.target_envelope, route.kind);
      if (!input_bytes.ok())
        return input_bytes.status();
      const auto& source_pool = pools.at(route.source_pool_id);
      if (source_pool.payload_capacity_bytes < input_bytes.value()) {
        return validation("Frozen source pool does not cover the consumer input envelope for '" + node.node->id + "." +
                          port.name + "'.");
      }
      auto aggregate = checked_add(input_payload_bytes, source_pool.payload_capacity_bytes,
                                   "synchronous node aggregate input payload");
      if (!aggregate.ok())
        return aggregate.status();
      input_payload_bytes = aggregate.value();
      if (route.kind == InputRouteKind::data_edge)
        ++dynamic_inputs;
      node_inputs.push_back({.port_name = port.name,
                             .abi_port = port_position.value(),
                             .source_kind = route.kind == InputRouteKind::data_edge
                                              ? SynchronousInputSourceKind::data_edge
                                              : SynchronousInputSourceKind::calibration_artifact,
                             .source_id = route.source_id,
                             .type_descriptor = port.type_descriptor,
                             .maximum_item_count = 1U});
    }
    if (dynamic_inputs == 0U || dynamic_inputs > kSynchronousMaximumDynamicInputEdgesPerNode) {
      return validation("Node '" + node.node->id + "' requires between one and " +
                        std::to_string(kSynchronousMaximumDynamicInputEdgesPerNode) + " dynamic data inputs.");
    }
    const auto activation = validate_node_activation_bounds(node, node_inputs.size());
    if (!activation.ok())
      return activation;
    for (const auto& port : node.contract->contract.ports()) {
      if (port.direction != PortDirection::output)
        continue;
      const auto& route = outputs.at({node.node->id, port.name});
      const auto& envelope = envelopes.at({node.node->id, port.name});
      auto port_position = abi_port(node.contract->contract, port.name, PortDirection::output);
      if (!port_position.ok())
        return port_position.status();
      const auto& pool = pools.at(route.pool_id);
      auto aggregate =
        checked_add(output_capacity_bytes, pool.payload_capacity_bytes, "synchronous node aggregate output capacity");
      if (!aggregate.ok())
        return aggregate.status();
      output_capacity_bytes = aggregate.value();
      node_outputs.push_back({.port_name = port.name,
                              .abi_port = port_position.value(),
                              .destination_kind = route.kind == OutputRouteKind::data_edge
                                                    ? SynchronousOutputDestinationKind::data_edge
                                                    : SynchronousOutputDestinationKind::calibration_artifact,
                              .destination_id = route.destination_id,
                              .pool_id = route.pool_id,
                              .type_descriptor = port.type_descriptor,
                              .maximum_item_count = std::max(envelope.ordinary_items, envelope.terminal_items)});
    }
    if (node.requirements->requirements.terminal().normal_max_output_items > node_outputs.size() ||
        node.requirements->requirements.terminal().normal_max_output_charged_bytes > output_capacity_bytes) {
      return validation("Node terminal output bound exceeds its frozen output grants ('" + node.node->id + "').");
    }
    const Quantity staging_bytes = 4096U + 512U * (node_inputs.size() + node_outputs.size());
    const Quantity staging_descriptors = 1U + 2U * (node_inputs.size() + node_outputs.size());
    const Quantity input_binding_count = static_cast<Quantity>(node_inputs.size());
    const Quantity output_binding_count = static_cast<Quantity>(node_outputs.size());
    result.nodes.push_back({
      .node_id = node.node->id,
      .provider_id = node.provider->provider_id,
      .provider_bundle_digest = node.provider->bundle_digest.value(),
      .operator_id = node.node->operator_id,
      .dynamic_input_join_policy = SynchronousDynamicInputJoinPolicy::exact_item_identity,
      .inputs = std::move(node_inputs),
      .outputs = std::move(node_outputs),
      .firing = {.maximum_input_batches = input_binding_count,
                 .maximum_input_items = input_binding_count,
                 .maximum_output_grants = output_binding_count,
                 .maximum_input_payload_bytes = input_payload_bytes,
                 .maximum_scratch_bytes = node.requirements->requirements.resources().scratch_charged_bytes_per_firing,
                 .maximum_metadata_bytes = 65536U,
                 .staging_charged_bytes = staging_bytes,
                 .staging_descriptor_count = staging_descriptors,
                 .firing_reservation = {.cpu_leaf_permits = node.requirements->requirements.resources().cpu_permits}},
      .terminal = {.normal_max_output_items = node.requirements->requirements.terminal().normal_max_output_items,
                   .normal_max_output_charged_bytes =
                     node.requirements->requirements.terminal().normal_max_output_charged_bytes,
                   .normal_max_async_tokens = 0U,
                   .cancel_max_async_tokens = 0U},
    });
    auto status = add_to(host_bytes, staging_bytes, "synchronous node staging resource accounting");
    if (!status.ok())
      return status.status();
    status = add_to(descriptors, staging_descriptors, "synchronous node staging descriptor accounting");
    if (!status.ok())
      return status.status();
    for (const auto value : {node.requirements->requirements.resources().scratch_charged_bytes_per_firing,
                             node.requirements->requirements.resources().per_scan_workspace_charged_bytes,
                             node.requirements->requirements.resources().retention_charged_bytes,
                             node.requirements->requirements.resources().external_allocation_charged_bytes}) {
      status = add_to(host_bytes, value, "synchronous node host resource accounting");
      if (!status.ok())
        return status.status();
    }
    auto keyed = checked_multiply(node.requirements->requirements.resources().per_key_state_charged_bytes,
                                  node.requirements->requirements.execution().max_active_keys,
                                  "synchronous node per-key state accounting");
    if (!keyed.ok())
      return keyed.status();
    status = add_to(host_bytes, keyed.value(), "synchronous node per-key host resource accounting");
    if (!status.ok())
      return status.status();
    status = add_to(result.resources.cpu_leaf_permits, node.requirements->requirements.resources().cpu_permits,
                    "synchronous CPU permit accounting");
    if (!status.ok())
      return status.status();
    status =
      add_to(result.resources.backend_gang_permits, node.requirements->requirements.resources().backend_gang_threads,
             "synchronous backend permit accounting");
    if (!status.ok())
      return status.status();
    status = add_to(result.resources.provider_private_permits,
                    node.requirements->requirements.resources().provider_private_threads,
                    "synchronous provider permit accounting");
    if (!status.ok())
      return status.status();
  }
  result.resources.host_normal_bytes = host_bytes;
  result.resources.descriptor_count = descriptors;
  auto terminals =
    checked_multiply(static_cast<Quantity>(result.nodes.size()), 2U, "synchronous terminal occurrence bound");
  if (!terminals.ok())
    return terminals.status();
  result.terminal_occurrences = terminals.value();
  result.obligations = {"PO-01.typed-ports",
                        "PO-04.finite-bounds",
                        "PO-05.resource-vector",
                        "PO-15.synchronous-graph",
                        "PO-16.static-calibration-artifacts",
                        "PO-17.transactional-exact-identity-join"};
  return result;
}

[[nodiscard]] Result<std::vector<OperatorPlanBindingSpec>> operator_plan_bindings(const PlanBuildRequest& request) {
  std::vector<OperatorPlanBindingSpec> result;
  result.reserve(request.resolved_pipeline.definition().nodes().size());
  for (const auto& node : request.resolved_pipeline.definition().nodes()) {
    auto digest = derive_canonical_config_digest(node.canonical_config, "node '" + node.id + "' canonical_config");
    if (!digest.ok())
      return digest.status();
    result.push_back({.node_id = node.id, .canonical_config_digest = digest.value().value()});
  }
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    return left.node_id < right.node_id;
  });
  return result;
}

[[nodiscard]] Result<ExecutionPlanSpec> make_specification(const PlanBuildRequest& request,
                                                           const DerivedSynchronousPlan& derived) {
  auto bindings = operator_plan_bindings(request);
  if (!bindings.ok())
    return bindings.status();
  return ExecutionPlanSpec{
    .inputs = {.resolved_pipeline = request.resolved_pipeline.digest().value(),
               .scan_descriptor = request.artifact_digests.scan_descriptor.value(),
               .target_envelope = request.artifact_digests.target_envelope.value(),
               .machine_policy = request.artifact_digests.machine_policy.value()},
    .operator_plan_bindings = std::move(bindings).value(),
    .execution_profile = request.requested_profile,
    .synchronous_node_plans = derived.nodes,
    .synchronous_buffer_pool_plans = derived.pools,
    .synchronous_data_edge_plans = derived.edges,
    .calibration_artifact_binding_plans = derived.artifacts,
    .resource_vector = derived.resources,
    .terminal_occurrences = derived.terminal_occurrences,
    .proof_obligations = derived.obligations,
  };
}

[[nodiscard]] Result<ExecutionPlan> make_plan(const PlanBuildRequest& request, const DerivedSynchronousPlan& derived) {
  auto specification = make_specification(request, derived);
  if (!specification.ok())
    return specification.status();
  auto placeholder = ArtifactDigest::parse("sha256:0000000000000000000000000000000000000000000000000000000000000000",
                                           "ExecutionPlan canonical serialization placeholder");
  if (!placeholder.ok())
    return placeholder.status();
  auto serializable = ExecutionPlan::create(std::move(placeholder).value(), specification.value());
  if (!serializable.ok())
    return serializable.status();
  auto canonical = serialize_execution_plan_canonical_json(serializable.value());
  if (!canonical.ok())
    return canonical.status();
  auto digest = domain_separated_sha256_digest("kspacejet:artifact:execution-plan", canonical.value(),
                                               "synchronous ExecutionPlan compiler output");
  if (!digest.ok())
    return digest.status();
  return ExecutionPlan::create(std::move(digest).value(), specification.value());
}

} // namespace

Result<CompiledExecutionPlan> compile_synchronous_graph_plan(const PlanBuildRequest& request) {
  auto derived = derive_synchronous_plan(request);
  if (!derived.ok())
    return derived.status();
  auto resources = ResourceVector::create(derived.value().resources, "synchronous derived resource_vector");
  if (!resources.ok())
    return resources.status();
  if (!request.machine_policy.resource_capacity().can_admit(resources.value())) {
    return validation("The derived synchronous ResourceVector exceeds MachinePolicy capacity.");
  }
  auto plan = make_plan(request, derived.value());
  if (!plan.ok())
    return plan.status();
  return CompiledExecutionPlan{.plan = std::move(plan).value()};
}

} // namespace ksj::recon::graph::detail

namespace ksj::recon::graph {

Result<CompiledExecutionPlan> ExecutionPlanCompiler::compile(const PlanBuildRequest& request) {
  return detail::compile_synchronous_graph_plan(request);
}

Status ExecutionPlanCompiler::deterministic_recheck(const CompiledExecutionPlan& compiled,
                                                    const PlanBuildRequest& request) {
  auto expected = compile(request);
  if (!expected.ok())
    return expected.status();
  if (expected.value().plan.digest() != compiled.plan.digest()) {
    return Status::ValidationError("ExecutionPlan digest does not match deterministic compiler output.");
  }
  return Status::Ok();
}

} // namespace ksj::recon::graph
