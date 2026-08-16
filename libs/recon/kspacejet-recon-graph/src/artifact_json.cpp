#include "kspacejet/recon/graph/artifact_json.hpp"

#include "kspacejet/recon/graph/canonical_json.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace ksj::recon::graph {
namespace {

using Json = nlohmann::json;

[[nodiscard]] Json resource_vector_json(const ResourceVector& resources) {
  Json devices = Json::array();
  for (const auto& device : resources.devices()) {
    devices.push_back({{"device_id", device.device_id()},
                       {"device_bytes", device.device_bytes()},
                       {"gpu_stream_slots", device.gpu_stream_slots()},
                       {"copy_engine_slots", device.copy_engine_slots()}});
  }
  return {
    {"host_normal_bytes", resources.host_normal_bytes()},
    {"host_pinned_bytes", resources.host_pinned_bytes()},
    {"host_hugepage_bytes", resources.host_hugepage_bytes()},
    {"shared_host_bytes", resources.shared_host_bytes()},
    {"spool_bytes", resources.spool_bytes()},
    {"transport_bytes", resources.transport_bytes()},
    {"descriptor_count", resources.descriptor_count()},
    {"async_token_count", resources.async_token_count()},
    {"cpu_leaf_permits", resources.cpu_leaf_permits()},
    {"backend_gang_permits", resources.backend_gang_permits()},
    {"provider_private_permits", resources.provider_private_permits()},
    {"io_slots", resources.io_slots()},
    {"devices", std::move(devices)},
  };
}

[[nodiscard]] Json type_descriptor_json(const TypeDescriptor& descriptor) {
  Json allowed_memory_domains = Json::array();
  for (const auto domain : descriptor.allowed_memory_domains()) {
    allowed_memory_domains.push_back(to_string(domain));
  }
  Json explicit_byte_strides = Json::array();
  for (const auto stride : descriptor.explicit_byte_strides()) {
    explicit_byte_strides.push_back(stride);
  }
  return {
    {"type_ref", descriptor.type_ref().value()},
    {"type_identity_digest", descriptor.type_identity_digest().value()},
    {"payload_kind", to_string(descriptor.payload_kind())},
    {"element_type", to_string(descriptor.element_type())},
    {"rank", descriptor.rank()},
    {"dimensions", descriptor.dimensions()},
    {"layout", to_string(descriptor.layout())},
    {"strides", to_string(descriptor.strides())},
    {"explicit_byte_strides", std::move(explicit_byte_strides)},
    {"allowed_memory_domains", std::move(allowed_memory_domains)},
    {"min_alignment_bytes", descriptor.min_alignment_bytes()},
    {"mutability", to_string(descriptor.mutability())},
  };
}

[[nodiscard]] const char* synchronous_endpoint_kind_json(const SynchronousDataEndpointKind kind) {
  switch (kind) {
    case SynchronousDataEndpointKind::ingress:
      return "ingress";
    case SynchronousDataEndpointKind::node:
      return "node";
    case SynchronousDataEndpointKind::egress:
      return "egress";
  }
  return "invalid";
}

[[nodiscard]] const char* synchronous_input_source_kind_json(const SynchronousInputSourceKind kind) {
  switch (kind) {
    case SynchronousInputSourceKind::data_edge:
      return "data-edge";
    case SynchronousInputSourceKind::calibration_artifact:
      return "calibration-artifact";
  }
  return "invalid";
}

[[nodiscard]] const char* synchronous_output_destination_kind_json(const SynchronousOutputDestinationKind kind) {
  switch (kind) {
    case SynchronousOutputDestinationKind::data_edge:
      return "data-edge";
    case SynchronousOutputDestinationKind::calibration_artifact:
      return "calibration-artifact";
  }
  return "invalid";
}

[[nodiscard]] const char* synchronous_join_policy_json(const SynchronousDynamicInputJoinPolicy policy) {
  switch (policy) {
    case SynchronousDynamicInputJoinPolicy::exact_item_identity:
      return "exact-item-identity";
  }
  return "invalid";
}

[[nodiscard]] Json execution_plan_json(const ExecutionPlan& plan) {
  Json operator_plan_bindings = Json::array();
  for (const auto& binding : plan.operator_plan_bindings()) {
    operator_plan_bindings.push_back(
      {{"node_id", binding.node_id()}, {"canonical_config_digest", binding.canonical_config_digest().value()}});
  }

  Json result{
    {"kind", "ExecutionPlan"},
    {"input_digests",
     {{"resolved_pipeline", plan.inputs().resolved_pipeline().value()},
      {"scan_descriptor", plan.inputs().scan_descriptor().value()},
      {"target_envelope", plan.inputs().target_envelope().value()},
      {"machine_policy", plan.inputs().machine_policy().value()}}},
    {"operator_plan_bindings", std::move(operator_plan_bindings)},
    {"execution_profile", to_string(plan.execution_profile())},
    {"resource_vector", resource_vector_json(plan.resources())},
    {"terminal_occurrences", plan.terminal_occurrences()},
    {"proof_obligations", plan.proof_obligations()},
  };

  Json synchronous_nodes = Json::array();
  for (const auto& node : plan.synchronous_node_plans()) {
    Json inputs = Json::array();
    for (const auto& input : node.inputs()) {
      inputs.push_back({{"port_name", input.port_name()},
                        {"abi_port", input.abi_port()},
                        {"source_kind", synchronous_input_source_kind_json(input.source_kind())},
                        {"source_id", input.source_id()},
                        {"type_descriptor", type_descriptor_json(input.type_descriptor())},
                        {"maximum_item_count", input.maximum_item_count()}});
    }
    Json outputs = Json::array();
    for (const auto& output : node.outputs()) {
      outputs.push_back({{"port_name", output.port_name()},
                         {"abi_port", output.abi_port()},
                         {"destination_kind", synchronous_output_destination_kind_json(output.destination_kind())},
                         {"destination_id", output.destination_id()},
                         {"pool_id", output.pool_id()},
                         {"type_descriptor", type_descriptor_json(output.type_descriptor())},
                         {"maximum_item_count", output.maximum_item_count()}});
    }
    synchronous_nodes.push_back({
      {"node_id", node.node_id()},
      {"provider_id", node.provider_id()},
      {"provider_bundle_digest", node.provider_bundle_digest().value()},
      {"operator_id", node.operator_id()},
      {"dynamic_input_join_policy", synchronous_join_policy_json(node.dynamic_input_join_policy())},
      {"inputs", std::move(inputs)},
      {"outputs", std::move(outputs)},
      {"firing",
       {{"maximum_input_batches", node.firing().maximum_input_batches()},
        {"maximum_input_items", node.firing().maximum_input_items()},
        {"maximum_output_grants", node.firing().maximum_output_grants()},
        {"maximum_input_payload_bytes", node.firing().maximum_input_payload_bytes()},
        {"maximum_scratch_bytes", node.firing().maximum_scratch_bytes()},
        {"maximum_metadata_bytes", node.firing().maximum_metadata_bytes()},
        {"staging_charged_bytes", node.firing().staging_charged_bytes()},
        {"staging_descriptor_count", node.firing().staging_descriptor_count()},
        {"firing_reservation", resource_vector_json(node.firing().firing_reservation())}}},
      {"terminal",
       {{"normal_max_output_items", node.terminal().normal_max_output_items()},
        {"normal_max_output_charged_bytes", node.terminal().normal_max_output_charged_bytes()},
        {"normal_max_async_tokens", node.terminal().normal_max_async_tokens()},
        {"cancel_max_async_tokens", node.terminal().cancel_max_async_tokens()}}},
    });
  }
  Json synchronous_pools = Json::array();
  for (const auto& pool : plan.synchronous_buffer_pool_plans()) {
    synchronous_pools.push_back({
      {"pool_id", pool.pool_id()},
      {"owner_kind", synchronous_endpoint_kind_json(pool.owner_kind())},
      {"owner_id", pool.owner_id()},
      {"owner_port_name", pool.owner_port_name()},
      {"type_descriptor", type_descriptor_json(pool.type_descriptor())},
      {"memory_domain", to_string(pool.memory_domain())},
      {"slot_count", pool.slot_count()},
      {"payload_capacity_bytes", pool.payload_capacity_bytes()},
      {"metadata_capacity_bytes", pool.metadata_capacity_bytes()},
      {"payload_alignment_bytes", pool.payload_alignment_bytes()},
      {"storage_accounting_id", pool.storage_accounting_id()},
      {"host_metadata_charged_bytes", pool.host_metadata_charged_bytes()},
      {"descriptor_charged_count", pool.descriptor_charged_count()},
      {"physical_charge_bytes", pool.physical_charge_bytes()},
    });
  }
  Json synchronous_edges = Json::array();
  for (const auto& edge : plan.synchronous_data_edge_plans()) {
    synchronous_edges.push_back({
      {"edge_id", edge.edge_id()},
      {"source_pool_id", edge.source_pool_id()},
      {"producer_kind", synchronous_endpoint_kind_json(edge.producer_kind())},
      {"producer_id", edge.producer_id()},
      {"producer_port_name", edge.producer_port_name()},
      {"producer_abi_port", edge.producer_abi_port()},
      {"consumer_kind", synchronous_endpoint_kind_json(edge.consumer_kind())},
      {"consumer_id", edge.consumer_id()},
      {"consumer_port_name", edge.consumer_port_name()},
      {"consumer_abi_port", edge.consumer_abi_port()},
      {"type_descriptor", type_descriptor_json(edge.type_descriptor())},
      {"max_items", edge.max_items()},
      {"max_logical_bytes", edge.max_logical_bytes()},
      {"storage_accounting_id", edge.storage_accounting_id()},
      {"host_metadata_charged_bytes", edge.host_metadata_charged_bytes()},
      {"descriptor_charged_count", edge.descriptor_charged_count()},
      {"terminal_policy", edge.terminal_policy()},
    });
  }
  Json calibration_artifacts = Json::array();
  for (const auto& artifact : plan.calibration_artifact_binding_plans()) {
    calibration_artifacts.push_back({
      {"binding_id", artifact.binding_id()},
      {"producer_node_id", artifact.producer_node_id()},
      {"producer_port_name", artifact.producer_port_name()},
      {"producer_abi_port", artifact.producer_abi_port()},
      {"producer_pool_id", artifact.producer_pool_id()},
      {"type_descriptor", type_descriptor_json(artifact.type_descriptor())},
      {"host_metadata_charged_bytes", artifact.host_metadata_charged_bytes()},
      {"descriptor_charged_count", artifact.descriptor_charged_count()},
    });
  }
  result["synchronous_nodes"] = std::move(synchronous_nodes);
  result["synchronous_buffer_pools"] = std::move(synchronous_pools);
  result["synchronous_data_edges"] = std::move(synchronous_edges);
  result["calibration_artifact_bindings"] = std::move(calibration_artifacts);
  return result;
}

[[nodiscard]] Json verification_record_json(const VerificationRecord& record) {
  return {
    {"kind", "VerificationRecord"},
    {"execution_plan_digest", record.execution_plan_digest().value()},
    {"execution_profile", to_string(record.execution_profile())},
    {"verified_resource_vector", resource_vector_json(record.verified_resource_vector())},
    {"verified_terminal_occurrences", record.verified_terminal_occurrences()},
    {"verified_obligations", record.verified_obligations()},
  };
}

} // namespace

Result<std::string> serialize_execution_plan_canonical_json(const ExecutionPlan& plan) {
  return canonicalize_json(execution_plan_json(plan).dump());
}

Result<std::string> serialize_verification_record_canonical_json(const VerificationRecord& record) {
  return canonicalize_json(verification_record_json(record).dump());
}

} // namespace ksj::recon::graph
