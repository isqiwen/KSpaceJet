#include "synchronous_graph_plan_internal.hpp"

#include "kspacejet/recon/graph/artifact_json.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/planning_input_artifacts.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <algorithm>
#include <map>
#include <numeric>
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

struct PortEnvelope final {
  Quantity ordinary_items{0U};
  Quantity ordinary_bytes{0U};
  Quantity terminal_items{0U};
  Quantity terminal_bytes{0U};
};

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

[[nodiscard]] bool contains(const std::vector<ExecutionProfile>& values, const ExecutionProfile profile) {
  return std::find(values.begin(), values.end(), profile) != values.end();
}

[[nodiscard]] const OperatorContractBinding* contract_binding(const PlanBuildRequest& request,
                                                              const std::string_view node_id) {
  const auto found = std::find_if(request.operator_contract_bindings.begin(), request.operator_contract_bindings.end(),
                                  [&](const OperatorContractBinding& binding) {
                                    return binding.node_id == node_id;
                                  });
  return found == request.operator_contract_bindings.end() ? nullptr : &*found;
}

[[nodiscard]] const NodePlanningRequirementsBinding* requirements_binding(const PlanBuildRequest& request,
                                                                          const std::string_view node_id) {
  const auto found = std::find_if(request.node_planning_requirements.begin(), request.node_planning_requirements.end(),
                                  [&](const NodePlanningRequirementsBinding& binding) {
                                    return binding.node_id == node_id;
                                  });
  return found == request.node_planning_requirements.end() ? nullptr : &*found;
}

[[nodiscard]] const ResolvedProvider* provider(const ResolvedPipeline& pipeline, const std::string_view alias) {
  const auto found =
    std::find_if(pipeline.providers().begin(), pipeline.providers().end(), [&](const ResolvedProvider& value) {
      return value.alias == alias;
    });
  return found == pipeline.providers().end() ? nullptr : &*found;
}

[[nodiscard]] const ResolvedOperator* resolved_operator(const ResolvedProvider& value,
                                                        const std::string_view operator_id) {
  const auto found =
    std::find_if(value.operators.begin(), value.operators.end(), [&](const ResolvedOperator& candidate) {
      return candidate.id == operator_id;
    });
  return found == value.operators.end() ? nullptr : &*found;
}

[[nodiscard]] const ResolvedPort* port(const OperatorContract& contract, const std::string_view port_name) {
  const auto found = std::find_if(contract.ports().begin(), contract.ports().end(), [&](const ResolvedPort& value) {
    return value.name == port_name;
  });
  return found == contract.ports().end() ? nullptr : &*found;
}

[[nodiscard]] Result<Quantity> abi_port(const OperatorContract& contract, const std::string_view port_name,
                                        const PortDirection direction) {
  Quantity index{0U};
  for (const auto& value : contract.ports()) {
    if (value.direction != direction)
      continue;
    if (value.name == port_name)
      return index;
    auto next = checked_add(index, 1U, "independent Provider ABI port position");
    if (!next.ok())
      return next.status();
    index = next.value();
  }
  return validation("The resolved OperatorContract does not contain an expected directional port.");
}

[[nodiscard]] Status validate_runtime_buffer(const TypeDescriptor& descriptor, const std::string_view context) {
  auto registered = types::resolve(descriptor.type_ref().value());
  if (!registered.ok() || !descriptor.exactly_matches(registered.value()) ||
      descriptor.payload_kind() != PayloadKind::buffer_handle ||
      descriptor.mutability() != PayloadMutability::immutable_after_publish ||
      std::find(descriptor.allowed_memory_domains().begin(), descriptor.allowed_memory_domains().end(),
                TypeMemoryDomain::host_normal) == descriptor.allowed_memory_domains().end()) {
    return validation(std::string(context) + " is not an exact immutable host-normal registered buffer type.");
  }
  return Status::Ok();
}

[[nodiscard]] Result<TypeDescriptor> resolve_external_type(const std::string_view type_ref,
                                                           const std::string_view context) {
  auto result = types::resolve(type_ref);
  if (!result.ok())
    return validation(std::string(context) + " does not resolve in the type registry.");
  const auto status = validate_runtime_buffer(result.value(), context);
  if (!status.ok())
    return status;
  return result;
}

[[nodiscard]] Result<PortEnvelope> output_envelope(const NodePlanningRequirements& requirements,
                                                   const std::string_view port_name) {
  PortEnvelope result;
  const auto inspect = [&](const std::vector<PortRateSpec>& entries, Quantity& items, Quantity& bytes) -> Status {
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const PortRateSpec& entry) {
      return entry.port_name == port_name;
    });
    if (found == entries.end())
      return Status::Ok();
    if (found->items > 1U || found->charged_bytes == 0U) {
      return validation("A current synchronous output port must have one positive-byte item at most.");
    }
    items = std::max(items, found->items);
    bytes = std::max(bytes, found->charged_bytes);
    return Status::Ok();
  };
  const auto& rates = requirements.rates();
  if (rates.kind == RateKind::keyed_dynamic) {
    const auto ordinary = inspect(rates.ordinary.outputs, result.ordinary_items, result.ordinary_bytes);
    if (!ordinary.ok())
      return ordinary;
    const auto terminal = inspect(rates.normal_flush.outputs, result.terminal_items, result.terminal_bytes);
    if (!terminal.ok())
      return terminal;
  } else {
    for (const auto& phase : rates.static_phases) {
      const auto ordinary = inspect(phase.outputs, result.ordinary_items, result.ordinary_bytes);
      if (!ordinary.ok())
        return ordinary;
    }
  }
  if ((result.ordinary_items == 0U && result.terminal_items == 0U) || result.ordinary_items > 1U ||
      result.terminal_items > 1U) {
    return validation("Output port has no supported finite current firing/terminal envelope.");
  }
  return result;
}

// A dynamic edge carries scan frames and is therefore bounded by the target
// frame envelope for keyed-dynamic operators.  A calibration artifact is a
// sealed static value whose capacity comes from its producing pool; keyed
// dynamic requirements intentionally do not provide a per-input rate table
// for that source, so this function returns zero as "no extra consumer-side
// minimum" rather than borrowing the frame bound.
[[nodiscard]] Result<Quantity> input_bytes(const NodePlanningRequirements& requirements,
                                           const std::string_view port_name, const TargetEnvelope& envelope,
                                           const SynchronousInputSourceKind source_kind) {
  if (requirements.rates().kind == RateKind::keyed_dynamic) {
    if (source_kind == SynchronousInputSourceKind::calibration_artifact)
      return Quantity{0U};
    if (envelope.max_frame_charged_bytes() == 0U) {
      return validation("keyed-dynamic input needs TargetEnvelope.max_frame_charged_bytes.");
    }
    return envelope.max_frame_charged_bytes();
  }
  Quantity maximum{0U};
  for (const auto& phase : requirements.rates().static_phases) {
    const auto found = std::find_if(phase.inputs.begin(), phase.inputs.end(), [&](const PortRateSpec& entry) {
      return entry.port_name == port_name;
    });
    if (found == phase.inputs.end() || found->items != 1U || found->charged_bytes == 0U) {
      return validation("Every static input port must have one positive-byte current envelope.");
    }
    maximum = std::max(maximum, found->charged_bytes);
  }
  return maximum == 0U ? Result<Quantity>{validation("Input port has no finite envelope.")} : Result<Quantity>{maximum};
}

[[nodiscard]] Result<Quantity> pool_metadata(const Quantity slot_count) {
  return synchronous_buffer_pool_host_metadata_charged_bytes(slot_count, "independent synchronous pool metadata");
}

[[nodiscard]] Result<Quantity> pool_charge(const Quantity payload) {
  return synchronous_buffer_pool_physical_charge_bytes(1U, payload, 0U, "independent synchronous pool charge");
}

[[nodiscard]] Result<Quantity> edge_metadata() {
  return synchronous_data_edge_host_metadata_charged_bytes(1U, "independent synchronous edge metadata");
}

[[nodiscard]] Status add(Quantity& total, const Quantity value, const std::string_view expression) {
  auto next = checked_add(total, value, expression);
  if (!next.ok())
    return next.status();
  total = next.value();
  return Status::Ok();
}

[[nodiscard]] Status compare_input_digests(const ExecutionPlan& plan, const PlanBuildRequest& request) {
  if (request.effective_pipeline_binding.resolved_pipeline_digest() != request.resolved_pipeline.digest() ||
      request.effective_pipeline_binding.scan_facts_digest() != request.scan_facts.digest()) {
    return validation("EffectivePipelineBinding does not attest the verifier's ResolvedPipeline and ScanFacts.");
  }
  auto target_envelope = derive_target_envelope_artifact_digest(request.target_envelope);
  if (!target_envelope.ok()) {
    return target_envelope.status();
  }
  auto machine_policy = derive_machine_policy_artifact_digest(request.machine_policy);
  if (!machine_policy.ok()) {
    return machine_policy.status();
  }
  if (plan.inputs().resolved_pipeline() != request.resolved_pipeline.digest() ||
      plan.inputs().scan_facts() != request.scan_facts.digest() ||
      plan.inputs().effective_pipeline_binding() != request.effective_pipeline_binding.digest() ||
      plan.inputs().target_envelope() != target_envelope.value() ||
      plan.inputs().machine_policy() != machine_policy.value()) {
    return validation("ExecutionPlan input digests do not exactly attest PlanBuildRequest artifacts.");
  }
  return Status::Ok();
}

[[nodiscard]] Status compare_operator_bindings(const ExecutionPlan& plan, const PlanBuildRequest& request) {
  if (plan.operator_plan_bindings().size() != request.resolved_pipeline.definition().nodes().size()) {
    return validation("ExecutionPlan operator binding count does not cover pipeline nodes.");
  }
  for (const auto& node : request.resolved_pipeline.definition().nodes()) {
    auto config = request.effective_pipeline_binding.config_for(node.id);
    if (!config.ok()) {
      return config.status();
    }
    auto digest = derive_canonical_config_digest(config.value(), "independent node effective canonical config digest");
    if (!digest.ok())
      return digest.status();
    const auto found = std::find_if(plan.operator_plan_bindings().begin(), plan.operator_plan_bindings().end(),
                                    [&](const OperatorPlanBinding& value) {
                                      return value.node_id() == node.id;
                                    });
    if (found == plan.operator_plan_bindings().end() || found->canonical_config_digest() != digest.value()) {
      return validation(
        "ExecutionPlan operator config binding does not exactly match an effective node configuration.");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status verify_generic_shape(const ExecutionPlan& plan) {
  if (plan.synchronous_node_plans().empty() || plan.synchronous_buffer_pool_plans().empty() ||
      plan.synchronous_data_edge_plans().empty()) {
    return validation("ExecutionPlan requires the generic synchronous graph representation.");
  }
  return Status::Ok();
}

[[nodiscard]] Status verify_topology_and_resources(const ExecutionPlan& plan, const PlanBuildRequest& request) {
  const auto& definition = request.resolved_pipeline.definition();
  if (!contains(definition.allowed_profiles(), request.requested_profile) ||
      !request.machine_policy.allows(request.requested_profile) ||
      plan.execution_profile() != request.requested_profile) {
    return validation("ExecutionPlan profile is not admitted by pipeline definition and MachinePolicy.");
  }
  if (request.operator_contract_bindings.size() != definition.nodes().size() ||
      request.node_planning_requirements.size() != definition.nodes().size() ||
      plan.synchronous_node_plans().size() != definition.nodes().size()) {
    return validation("Generic graph node/contract/requirements sets must have exact equal cardinality.");
  }

  std::map<std::string, const SynchronousNodePlan*> planned_nodes;
  std::map<std::string, const SynchronousBufferPoolPlan*> planned_pools;
  std::map<std::string, const SynchronousDataEdgePlan*> planned_edges;
  std::map<std::string, const CalibrationArtifactBindingPlan*> planned_artifacts;
  for (const auto& node : plan.synchronous_node_plans()) {
    if (!planned_nodes.emplace(node.node_id(), &node).second)
      return validation("ExecutionPlan node ids are duplicate.");
  }
  for (const auto& pool : plan.synchronous_buffer_pool_plans()) {
    if (!planned_pools.emplace(pool.pool_id(), &pool).second)
      return validation("ExecutionPlan pool ids are duplicate.");
  }
  for (const auto& edge : plan.synchronous_data_edge_plans()) {
    if (!planned_edges.emplace(edge.edge_id(), &edge).second)
      return validation("ExecutionPlan edge ids are duplicate.");
  }
  for (const auto& artifact : plan.calibration_artifact_binding_plans()) {
    if (!planned_artifacts.emplace(artifact.binding_id(), &artifact).second) {
      return validation("ExecutionPlan calibration binding ids are duplicate.");
    }
  }
  if (planned_edges.size() !=
        definition.edges().size() + definition.ingress_ports().size() + definition.egress_ports().size() ||
      planned_pools.size() !=
        definition.ingress_ports().size() +
          std::accumulate(definition.nodes().begin(), definition.nodes().end(), std::size_t{0U},
                          [&](const std::size_t count, const PipelineNode& node) {
                            const auto* contract = contract_binding(request, node.id);
                            return count + (contract == nullptr
                                              ? 0U
                                              : static_cast<std::size_t>(std::count_if(
                                                  contract->contract.ports().begin(), contract->contract.ports().end(),
                                                  [](const ResolvedPort& port) {
                                                    return port.direction == PortDirection::output;
                                                  })));
                          }) ||
      planned_artifacts.size() != definition.calibration_bindings().size()) {
    return validation("ExecutionPlan generic edge/pool/calibration cardinalities do not match authored topology.");
  }

  std::map<PortAddress, std::string> expected_inputs;
  std::map<PortAddress, std::string> expected_outputs;
  std::map<std::string, Quantity> pool_capacities;
  for (const auto& graph_edge : definition.edges()) {
    const auto* source_contract = contract_binding(request, graph_edge.from.node);
    const auto* target_contract = contract_binding(request, graph_edge.to.node);
    const auto* source_port =
      source_contract == nullptr ? nullptr : port(source_contract->contract, graph_edge.from.port);
    const auto* target_port =
      target_contract == nullptr ? nullptr : port(target_contract->contract, graph_edge.to.port);
    if (source_port == nullptr || target_port == nullptr || source_port->direction != PortDirection::output ||
        target_port->direction != PortDirection::input ||
        !source_port->type_descriptor.exactly_matches(target_port->type_descriptor)) {
      return validation("Authored graph edge does not connect exact directional resolved ports.");
    }
    const auto type = validate_runtime_buffer(source_port->type_descriptor, "Authored graph edge");
    if (!type.ok())
      return type;
    const auto edge_it = planned_edges.find("edge:graph:" + graph_edge.id);
    if (edge_it == planned_edges.end())
      return validation("ExecutionPlan is missing a graph data edge.");
    const auto& actual = *edge_it->second;
    auto producer_abi = abi_port(source_contract->contract, graph_edge.from.port, PortDirection::output);
    auto consumer_abi = abi_port(target_contract->contract, graph_edge.to.port, PortDirection::input);
    if (!producer_abi.ok())
      return producer_abi.status();
    if (!consumer_abi.ok())
      return consumer_abi.status();
    const std::string pool_id = "pool:node:" + graph_edge.from.node + ":" + graph_edge.from.port;
    if (actual.source_pool_id() != pool_id || actual.producer_kind() != SynchronousDataEndpointKind::node ||
        actual.producer_id() != graph_edge.from.node || actual.producer_port_name() != graph_edge.from.port ||
        actual.producer_abi_port() != producer_abi.value() ||
        actual.consumer_kind() != SynchronousDataEndpointKind::node || actual.consumer_id() != graph_edge.to.node ||
        actual.consumer_port_name() != graph_edge.to.port || actual.consumer_abi_port() != consumer_abi.value() ||
        !actual.type_descriptor().exactly_matches(source_port->type_descriptor) || actual.max_items() != 1U ||
        actual.terminal_policy() != "normal-eoi-drain-cancellation-fail") {
      return validation("ExecutionPlan graph data edge does not exactly match the independent topology derivation.");
    }
    if (!expected_inputs.emplace(PortAddress{graph_edge.to.node, graph_edge.to.port}, actual.edge_id()).second ||
        !expected_outputs.emplace(PortAddress{graph_edge.from.node, graph_edge.from.port}, actual.edge_id()).second) {
      return validation("Authored data fan-in/fan-out is unsupported by the current generic executor.");
    }
  }

  for (const auto& ingress : definition.ingress_ports()) {
    auto external_type = resolve_external_type(ingress.type, "Authored ingress");
    if (!external_type.ok())
      return external_type.status();
    const auto* target_contract = contract_binding(request, ingress.to.node);
    const auto* target_port = target_contract == nullptr ? nullptr : port(target_contract->contract, ingress.to.port);
    if (target_port == nullptr || target_port->direction != PortDirection::input ||
        !target_port->type_descriptor.exactly_matches(external_type.value())) {
      return validation("Authored ingress does not exactly match its target input port.");
    }
    const auto edge_it = planned_edges.find("edge:ingress:" + ingress.id);
    const auto pool_it = planned_pools.find("pool:ingress:" + ingress.id);
    if (edge_it == planned_edges.end() || pool_it == planned_pools.end())
      return validation("ExecutionPlan is missing ingress data plane.");
    auto consumer_abi = abi_port(target_contract->contract, ingress.to.port, PortDirection::input);
    if (!consumer_abi.ok())
      return consumer_abi.status();
    auto bytes = input_bytes(requirements_binding(request, ingress.to.node)->requirements, ingress.to.port,
                             request.target_envelope, SynchronousInputSourceKind::data_edge);
    if (!bytes.ok())
      return bytes.status();
    const auto& edge = *edge_it->second;
    const auto& pool = *pool_it->second;
    if (pool.owner_kind() != SynchronousDataEndpointKind::ingress || pool.owner_id() != ingress.id ||
        !pool.owner_port_name().empty() || !pool.type_descriptor().exactly_matches(target_port->type_descriptor) ||
        pool.slot_count() != 1U || pool.payload_capacity_bytes() != bytes.value() ||
        pool.metadata_capacity_bytes() != 0U || edge.source_pool_id() != pool.pool_id() ||
        edge.producer_kind() != SynchronousDataEndpointKind::ingress || edge.producer_id() != ingress.id ||
        !edge.producer_port_name().empty() || edge.producer_abi_port() != 0U ||
        edge.consumer_kind() != SynchronousDataEndpointKind::node || edge.consumer_id() != ingress.to.node ||
        edge.consumer_port_name() != ingress.to.port || edge.consumer_abi_port() != consumer_abi.value()) {
      return validation("ExecutionPlan ingress pool/edge does not exactly match its frozen endpoint.");
    }
    if (!expected_inputs.emplace(PortAddress{ingress.to.node, ingress.to.port}, edge.edge_id()).second) {
      return validation("Authored input has more than one data/calibration source.");
    }
  }

  for (const auto& egress : definition.egress_ports()) {
    auto external_type = resolve_external_type(egress.type, "Authored egress");
    if (!external_type.ok())
      return external_type.status();
    const auto* source_contract = contract_binding(request, egress.from.node);
    const auto* source_port = source_contract == nullptr ? nullptr : port(source_contract->contract, egress.from.port);
    if (source_port == nullptr || source_port->direction != PortDirection::output ||
        !source_port->type_descriptor.exactly_matches(external_type.value())) {
      return validation("Authored egress does not exactly match its source output port.");
    }
    const auto edge_it = planned_edges.find("edge:egress:" + egress.id);
    if (edge_it == planned_edges.end())
      return validation("ExecutionPlan is missing egress data edge.");
    auto producer_abi = abi_port(source_contract->contract, egress.from.port, PortDirection::output);
    if (!producer_abi.ok())
      return producer_abi.status();
    const auto& edge = *edge_it->second;
    if (edge.source_pool_id() != "pool:node:" + egress.from.node + ":" + egress.from.port ||
        edge.producer_kind() != SynchronousDataEndpointKind::node || edge.producer_id() != egress.from.node ||
        edge.producer_port_name() != egress.from.port || edge.producer_abi_port() != producer_abi.value() ||
        edge.consumer_kind() != SynchronousDataEndpointKind::egress || edge.consumer_id() != egress.id ||
        !edge.consumer_port_name().empty() || edge.consumer_abi_port() != 0U ||
        !edge.type_descriptor().exactly_matches(source_port->type_descriptor)) {
      return validation("ExecutionPlan egress data edge does not exactly match its frozen endpoint.");
    }
    if (!expected_outputs.emplace(PortAddress{egress.from.node, egress.from.port}, edge.edge_id()).second) {
      return validation("Authored output fan-out is unsupported by the current generic executor.");
    }
  }

  for (const auto& binding : definition.calibration_bindings()) {
    const auto* producer_contract = contract_binding(request, binding.producer.node);
    const auto* producer_port =
      producer_contract == nullptr ? nullptr : port(producer_contract->contract, binding.producer.port);
    const auto artifact_it = planned_artifacts.find(binding.id);
    if (producer_port == nullptr || producer_port->direction != PortDirection::output ||
        artifact_it == planned_artifacts.end()) {
      return validation("ExecutionPlan calibration binding has no exact producer artifact record.");
    }
    const auto type = validate_runtime_buffer(producer_port->type_descriptor, "Authored calibration binding");
    if (!type.ok())
      return type;
    auto producer_abi = abi_port(producer_contract->contract, binding.producer.port, PortDirection::output);
    if (!producer_abi.ok())
      return producer_abi.status();
    const auto& artifact = *artifact_it->second;
    if (artifact.producer_node_id() != binding.producer.node ||
        artifact.producer_port_name() != binding.producer.port ||
        artifact.producer_abi_port() != producer_abi.value() ||
        artifact.producer_pool_id() != "pool:node:" + binding.producer.node + ":" + binding.producer.port ||
        !artifact.type_descriptor().exactly_matches(producer_port->type_descriptor) ||
        artifact.descriptor_charged_count() != 1U || artifact.host_metadata_charged_bytes() != 128U) {
      return validation("ExecutionPlan calibration artifact record does not exactly match the explicit binding.");
    }
    if (!expected_outputs.emplace(PortAddress{binding.producer.node, binding.producer.port}, binding.id).second) {
      return validation("Calibration producer output cannot also be a data destination.");
    }
    if (binding.consumers.empty())
      return validation("Calibration binding has no consumers.");
    std::set<PortAddress> consumers;
    for (const auto& consumer : binding.consumers) {
      const auto* consumer_contract = contract_binding(request, consumer.node);
      const auto* consumer_port =
        consumer_contract == nullptr ? nullptr : port(consumer_contract->contract, consumer.port);
      if (consumer_port == nullptr || consumer_port->direction != PortDirection::input ||
          !consumer_port->type_descriptor.exactly_matches(producer_port->type_descriptor) ||
          !consumers.insert({consumer.node, consumer.port}).second ||
          !expected_inputs.emplace(PortAddress{consumer.node, consumer.port}, binding.id).second) {
        return validation("Calibration consumer inputs must be unique, exact typed, and otherwise unbound.");
      }
    }
  }

  Quantity host_bytes{0U};
  Quantity descriptors{0U};
  ResourceVectorSpec expected_resources;
  for (const auto& [id, pool] : planned_pools) {
    const auto type = validate_runtime_buffer(pool->type_descriptor(), "ExecutionPlan synchronous pool");
    if (!type.ok())
      return type;
    auto metadata = pool_metadata(pool->slot_count());
    auto charge = pool_charge(pool->payload_capacity_bytes());
    if (!metadata.ok())
      return metadata.status();
    if (!charge.ok())
      return charge.status();
    if (pool->memory_domain() != TypeMemoryDomain::host_normal || pool->slot_count() != 1U ||
        pool->metadata_capacity_bytes() != 0U ||
        pool->payload_alignment_bytes() != pool->type_descriptor().min_alignment_bytes() ||
        pool->storage_accounting_id() != "kspacejet.buffer-pool-storage/host-normal" ||
        pool->host_metadata_charged_bytes() != metadata.value() || pool->descriptor_charged_count() != 1U ||
        pool->physical_charge_bytes() != charge.value()) {
      return validation("ExecutionPlan synchronous pool has invalid fixed storage/accounting fields.");
    }
    auto status = add(host_bytes, pool->physical_charge_bytes(), "independent pool resource total");
    if (!status.ok())
      return status;
    status = add(descriptors, pool->descriptor_charged_count(), "independent pool descriptor total");
    if (!status.ok())
      return status;
    pool_capacities.emplace(id, pool->payload_capacity_bytes());
  }
  for (const auto& [id, edge] : planned_edges) {
    auto metadata = edge_metadata();
    if (!metadata.ok())
      return metadata.status();
    const auto pool = pool_capacities.find(edge->source_pool_id());
    if (pool == pool_capacities.end() || edge->max_items() != 1U || edge->max_logical_bytes() != pool->second ||
        edge->storage_accounting_id() != "kspacejet.data-edge-storage/fixed-fifo" ||
        edge->host_metadata_charged_bytes() != metadata.value() || edge->descriptor_charged_count() != 1U ||
        edge->terminal_policy() != "normal-eoi-drain-cancellation-fail") {
      return validation("ExecutionPlan synchronous data edge has invalid source pool/fixed accounting fields.");
    }
    auto status = add(host_bytes, edge->host_metadata_charged_bytes(), "independent edge resource total");
    if (!status.ok())
      return status;
    status = add(descriptors, edge->descriptor_charged_count(), "independent edge descriptor total");
    if (!status.ok())
      return status;
  }
  for (const auto& [id, artifact] : planned_artifacts) {
    auto status = add(host_bytes, artifact->host_metadata_charged_bytes(), "independent artifact resource total");
    if (!status.ok())
      return status;
    status = add(descriptors, artifact->descriptor_charged_count(), "independent artifact descriptor total");
    if (!status.ok())
      return status;
  }

  for (const auto& node : definition.nodes()) {
    const auto* contract = contract_binding(request, node.id);
    const auto* requirements = requirements_binding(request, node.id);
    const auto* resolved_provider = provider(request.resolved_pipeline, node.provider_alias);
    const auto* resolved =
      resolved_provider == nullptr ? nullptr : resolved_operator(*resolved_provider, node.operator_id);
    const auto node_it = planned_nodes.find(node.id);
    if (contract == nullptr || requirements == nullptr || resolved_provider == nullptr || resolved == nullptr ||
        node_it == planned_nodes.end() || contract->contract.operator_id() != node.operator_id ||
        contract->contract.artifact_digest() != resolved->contract_digest ||
        !requirements->requirements.validate_against(contract->contract).ok()) {
      return validation("ExecutionPlan node has incomplete or mismatched resolved inputs.");
    }
    const auto& actual = *node_it->second;
    if (actual.provider_id() != resolved_provider->provider_id ||
        actual.provider_bundle_digest() != resolved_provider->bundle_digest ||
        actual.operator_id() != node.operator_id ||
        actual.dynamic_input_join_policy() != SynchronousDynamicInputJoinPolicy::exact_item_identity) {
      return validation("ExecutionPlan node provenance or join policy does not match the resolved pipeline.");
    }
    Quantity dynamic_inputs{0U};
    Quantity input_payload{0U};
    Quantity outputs_capacity{0U};
    Quantity input_count{0U};
    Quantity output_count{0U};
    for (const auto& contract_port : contract->contract.ports()) {
      if (contract_port.direction == PortDirection::input) {
        auto abi = abi_port(contract->contract, contract_port.name, PortDirection::input);
        if (!abi.ok())
          return abi.status();
        const auto expected_route = expected_inputs.find({node.id, contract_port.name});
        const auto found = std::find_if(actual.inputs().begin(), actual.inputs().end(), [&](const auto& input) {
          return input.port_name() == contract_port.name;
        });
        if (expected_route == expected_inputs.end() || found == actual.inputs().end() ||
            found->abi_port() != abi.value() ||
            !found->type_descriptor().exactly_matches(contract_port.type_descriptor) ||
            found->maximum_item_count() != 1U) {
          return validation("ExecutionPlan node input binding does not exactly match a declared contract port/source.");
        }
        const bool calibration = planned_artifacts.contains(expected_route->second);
        if ((calibration && (found->source_kind() != SynchronousInputSourceKind::calibration_artifact ||
                             found->source_id() != expected_route->second)) ||
            (!calibration && (found->source_kind() != SynchronousInputSourceKind::data_edge ||
                              found->source_id() != expected_route->second))) {
          return validation("ExecutionPlan node input source kind/id does not match independent topology.");
        }
        const auto data_source = calibration ? planned_artifacts.at(expected_route->second)->producer_pool_id()
                                             : planned_edges.at(expected_route->second)->source_pool_id();
        const auto source_capacity = pool_capacities.find(data_source);
        if (source_capacity == pool_capacities.end()) {
          return validation("ExecutionPlan node input source has no planned buffer pool.");
        }
        auto source_capacity_needed = input_bytes(
          requirements->requirements, contract_port.name, request.target_envelope,
          calibration ? SynchronousInputSourceKind::calibration_artifact : SynchronousInputSourceKind::data_edge);
        if (!source_capacity_needed.ok())
          return source_capacity_needed.status();
        if (source_capacity->second < source_capacity_needed.value()) {
          return validation("ExecutionPlan node input source pool does not cover the independently derived source-kind "
                            "capacity.");
        }
        auto aggregate = checked_add(input_payload, source_capacity->second, "independent node input payload");
        if (!aggregate.ok())
          return aggregate.status();
        input_payload = aggregate.value();
        if (!calibration)
          ++dynamic_inputs;
        ++input_count;
      } else {
        auto abi = abi_port(contract->contract, contract_port.name, PortDirection::output);
        if (!abi.ok())
          return abi.status();
        const auto expected_route = expected_outputs.find({node.id, contract_port.name});
        const auto found = std::find_if(actual.outputs().begin(), actual.outputs().end(), [&](const auto& output) {
          return output.port_name() == contract_port.name;
        });
        if (expected_route == expected_outputs.end() || found == actual.outputs().end() ||
            found->abi_port() != abi.value() ||
            !found->type_descriptor().exactly_matches(contract_port.type_descriptor) ||
            found->pool_id() != "pool:node:" + node.id + ":" + contract_port.name ||
            found->maximum_item_count() != 1U) {
          return validation(
            "ExecutionPlan node output binding does not exactly match a declared contract port/destination.");
        }
        const bool calibration = planned_artifacts.contains(expected_route->second);
        if ((calibration && (found->destination_kind() != SynchronousOutputDestinationKind::calibration_artifact ||
                             found->destination_id() != expected_route->second)) ||
            (!calibration && (found->destination_kind() != SynchronousOutputDestinationKind::data_edge ||
                              found->destination_id() != expected_route->second))) {
          return validation("ExecutionPlan node output destination kind/id does not match independent topology.");
        }
        auto envelope = output_envelope(requirements->requirements, contract_port.name);
        if (!envelope.ok())
          return envelope.status();
        const auto capacity = std::max(envelope.value().ordinary_bytes, envelope.value().terminal_bytes);
        if (pool_capacities.at(found->pool_id()) != capacity) {
          return validation("ExecutionPlan node output pool capacity does not match independent rate derivation.");
        }
        auto aggregate = checked_add(outputs_capacity, capacity, "independent node output capacity");
        if (!aggregate.ok())
          return aggregate.status();
        outputs_capacity = aggregate.value();
        ++output_count;
      }
    }
    auto expected_firing_reservation =
      ResourceVector::create({.cpu_leaf_permits = requirements->requirements.resources().cpu_permits},
                             "independent synchronous node firing reservation");
    if (!expected_firing_reservation.ok())
      return expected_firing_reservation.status();
    if (actual.inputs().size() != input_count || actual.outputs().size() != output_count || dynamic_inputs == 0U ||
        dynamic_inputs > kSynchronousMaximumDynamicInputEdgesPerNode ||
        requirements->requirements.execution().max_items_per_activation < input_count ||
        requirements->requirements.batch().max_items < input_count ||
        actual.firing().maximum_input_batches() != input_count ||
        actual.firing().maximum_input_items() != input_count ||
        actual.firing().maximum_output_grants() != output_count ||
        actual.firing().maximum_input_payload_bytes() != input_payload ||
        actual.firing().maximum_scratch_bytes() !=
          requirements->requirements.resources().scratch_charged_bytes_per_firing ||
        !actual.firing().firing_reservation().exactly_matches(expected_firing_reservation.value()) ||
        actual.firing().maximum_metadata_bytes() != 65536U ||
        actual.firing().staging_charged_bytes() != 4096U + 512U * (input_count + output_count) ||
        actual.firing().staging_descriptor_count() != 1U + 2U * (input_count + output_count) ||
        actual.terminal().normal_max_output_items() != requirements->requirements.terminal().normal_max_output_items ||
        actual.terminal().normal_max_output_charged_bytes() !=
          requirements->requirements.terminal().normal_max_output_charged_bytes ||
        actual.terminal().normal_max_async_tokens() != 0U || actual.terminal().cancel_max_async_tokens() != 0U ||
        actual.terminal().normal_max_output_items() > output_count ||
        actual.terminal().normal_max_output_charged_bytes() > outputs_capacity) {
      return validation("ExecutionPlan node firing/terminal bounds do not exactly match independent derivation.");
    }
    auto status = add(host_bytes, actual.firing().staging_charged_bytes(), "independent node staging total");
    if (!status.ok())
      return status;
    status = add(descriptors, actual.firing().staging_descriptor_count(), "independent node staging descriptor total");
    if (!status.ok())
      return status;
    for (const auto value : {requirements->requirements.resources().scratch_charged_bytes_per_firing,
                             requirements->requirements.resources().per_scan_workspace_charged_bytes,
                             requirements->requirements.resources().retention_charged_bytes,
                             requirements->requirements.resources().external_allocation_charged_bytes}) {
      status = add(host_bytes, value, "independent node host resources");
      if (!status.ok())
        return status;
    }
    auto per_key =
      checked_multiply(requirements->requirements.resources().per_key_state_charged_bytes,
                       requirements->requirements.execution().max_active_keys, "independent node per-key resource");
    if (!per_key.ok())
      return per_key.status();
    status = add(host_bytes, per_key.value(), "independent node per-key total");
    if (!status.ok())
      return status;
    status = add(expected_resources.cpu_leaf_permits, requirements->requirements.resources().cpu_permits,
                 "independent CPU permit total");
    if (!status.ok())
      return status;
    status = add(expected_resources.backend_gang_permits, requirements->requirements.resources().backend_gang_threads,
                 "independent backend permit total");
    if (!status.ok())
      return status;
    status = add(expected_resources.provider_private_permits,
                 requirements->requirements.resources().provider_private_threads, "independent provider permit total");
    if (!status.ok())
      return status;
  }
  expected_resources.host_normal_bytes = host_bytes;
  expected_resources.descriptor_count = descriptors;
  auto expected_vector = ResourceVector::create(expected_resources, "independent generic resource vector");
  if (!expected_vector.ok())
    return expected_vector.status();
  if (!plan.resources().exactly_matches(expected_vector.value()) ||
      !request.machine_policy.resource_capacity().can_admit(plan.resources())) {
    return validation("ExecutionPlan ResourceVector is not exactly independently derived/admissible.");
  }
  auto terminals =
    checked_multiply(static_cast<Quantity>(definition.nodes().size()), 2U, "independent terminal occurrence bound");
  if (!terminals.ok())
    return terminals.status();
  if (plan.terminal_occurrences() != terminals.value() ||
      plan.proof_obligations() != std::vector<std::string>{"PO-01.typed-ports", "PO-04.finite-bounds",
                                                           "PO-05.resource-vector", "PO-15.synchronous-graph",
                                                           "PO-16.static-calibration-artifacts",
                                                           "PO-17.transactional-exact-identity-join"}) {
    return validation("ExecutionPlan terminal/proof obligations do not match generic verifier derivation.");
  }
  return Status::Ok();
}

[[nodiscard]] Result<VerificationRecord> make_record(const ExecutionPlan& plan) {
  const VerificationRecordSpec specification{
    .execution_plan_digest = plan.digest().value(),
    .execution_profile = plan.execution_profile(),
    .verified_resource_vector = {.host_normal_bytes = plan.resources().host_normal_bytes(),
                                 .host_pinned_bytes = plan.resources().host_pinned_bytes(),
                                 .host_hugepage_bytes = plan.resources().host_hugepage_bytes(),
                                 .shared_host_bytes = plan.resources().shared_host_bytes(),
                                 .spool_bytes = plan.resources().spool_bytes(),
                                 .transport_bytes = plan.resources().transport_bytes(),
                                 .descriptor_count = plan.resources().descriptor_count(),
                                 .async_token_count = plan.resources().async_token_count(),
                                 .cpu_leaf_permits = plan.resources().cpu_leaf_permits(),
                                 .backend_gang_permits = plan.resources().backend_gang_permits(),
                                 .provider_private_permits = plan.resources().provider_private_permits(),
                                 .io_slots = plan.resources().io_slots()},
    .verified_terminal_occurrences = plan.terminal_occurrences(),
    .verified_obligations = {"V1.profile-attestation", "V1.plan-input-identity", "V1.typed-topology",
                             "V1.transactional-exact-identity-join", "V1.static-calibration-artifacts",
                             "V1.resource-capacity"},
  };
  auto placeholder = ArtifactDigest::parse("sha256:0000000000000000000000000000000000000000000000000000000000000000",
                                           "VerificationRecord canonical serialization placeholder");
  if (!placeholder.ok())
    return placeholder.status();
  auto serializable = VerificationRecord::create(std::move(placeholder).value(), specification);
  if (!serializable.ok())
    return serializable.status();
  auto canonical = serialize_verification_record_canonical_json(serializable.value());
  if (!canonical.ok())
    return canonical.status();
  auto digest = derive_domain_separated_sha256_digest("kspacejet:artifact:verification-record", canonical.value(),
                                                      "synchronous graph verifier output");
  if (!digest.ok())
    return digest.status();
  return VerificationRecord::create(std::move(digest).value(), specification);
}

} // namespace

Result<VerificationRecord> verify_synchronous_graph_plan(const ExecutionPlan& plan, const PlanBuildRequest& request) {
  const auto shape = verify_generic_shape(plan);
  if (!shape.ok())
    return shape;
  const auto inputs = compare_input_digests(plan, request);
  if (!inputs.ok())
    return inputs;
  const auto bindings = compare_operator_bindings(plan, request);
  if (!bindings.ok())
    return bindings;
  const auto topology = verify_topology_and_resources(plan, request);
  if (!topology.ok())
    return topology;
  auto canonical = serialize_execution_plan_canonical_json(plan);
  if (!canonical.ok())
    return canonical.status();
  auto expected_digest = derive_domain_separated_sha256_digest("kspacejet:artifact:execution-plan", canonical.value(),
                                                               "independent synchronous plan digest");
  if (!expected_digest.ok())
    return expected_digest.status();
  if (plan.digest() != expected_digest.value()) {
    return validation("ExecutionPlan detached digest does not match its independently validated canonical payload.");
  }
  return make_record(plan);
}

} // namespace ksj::recon::graph::detail

namespace ksj::recon::graph {

Result<VerificationRecord> ExecutionPlanVerifier::verify(const ExecutionPlan& plan, const PlanBuildRequest& request) {
  return detail::verify_synchronous_graph_plan(plan, request);
}

} // namespace ksj::recon::graph
