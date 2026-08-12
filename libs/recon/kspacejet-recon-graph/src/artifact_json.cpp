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

[[nodiscard]] Json execution_plan_json(const ExecutionPlan& plan) {
  Json provider_contracts = Json::array();
  for (const auto& digest : plan.inputs().provider_contracts()) {
    provider_contracts.push_back(digest.value());
  }

  Json key_slot_tables = Json::array();
  for (const auto& table : plan.key_slot_tables()) {
    Json dense_dimensions = Json::array();
    for (const auto& dimension : table.dense_dimensions()) {
      dense_dimensions.push_back(
        {{"field", dimension.field()}, {"minimum", dimension.minimum()}, {"cardinality", dimension.cardinality()}});
    }
    key_slot_tables.push_back({
      {"node_id", table.node_id()},
      {"dense_dimensions", std::move(dense_dimensions)},
      {"mapping_algorithm_id", table.mapping_algorithm_id()},
      {"storage_accounting_id", table.storage_accounting_id()},
      {"key_domain_bound", table.key_domain_bound()},
      {"max_distinct_keys", table.max_distinct_keys()},
      {"max_live_keys", table.max_live_keys()},
      {"slot_count", table.slot_count()},
      {"generation_policy", table.generation_policy()},
      {"initial_generation", table.initial_generation()},
      {"seal_on_completion", table.seal_on_completion()},
      {"eviction_policy", table.eviction_policy()},
      {"late_event_policy", table.late_event_policy()},
      {"host_metadata_charged_bytes", table.host_metadata_charged_bytes()},
      {"max_items_per_activation", table.max_items_per_activation()},
      {"max_charged_bytes_per_activation", table.max_charged_bytes_per_activation()},
    });
  }

  Json reorder_plans = Json::array();
  for (const auto& reorder : plan.reorder_plans()) {
    Json ordinal_dimensions = Json::array();
    for (const auto& dimension : reorder.ordinal_dimensions()) {
      ordinal_dimensions.push_back(
        {{"field", dimension.field()}, {"minimum", dimension.minimum()}, {"cardinality", dimension.cardinality()}});
    }
    reorder_plans.push_back({
      {"node_id", reorder.node_id()},
      {"order_domain_id", reorder.order_domain_id()},
      {"ordinal_binding_id", reorder.ordinal_binding_id()},
      {"completed_frame_input_port", reorder.completed_frame_input_port()},
      {"ordered_output_port", reorder.ordered_output_port()},
      {"outputs_per_ordinal", reorder.outputs_per_ordinal()},
      {"charged_bytes_per_ordinal", reorder.charged_bytes_per_ordinal()},
      {"ordinal_dimensions", std::move(ordinal_dimensions)},
      {"mapping_algorithm_id", reorder.mapping_algorithm_id()},
      {"storage_accounting_id", reorder.storage_accounting_id()},
      {"ordinal_domain_bound", reorder.ordinal_domain_bound()},
      {"first_expected_ordinal", reorder.first_expected_ordinal()},
      {"last_expected_ordinal", reorder.last_expected_ordinal()},
      {"max_ahead_items", reorder.max_ahead_items()},
      {"max_ahead_charged_bytes", reorder.max_ahead_charged_bytes()},
      {"max_gap_ordinals", reorder.max_gap_ordinals()},
      {"occurrence_policy", reorder.occurrence_policy()},
      {"publish_policy", reorder.publish_policy()},
      {"certified_skipped_ordinals", reorder.certified_skipped_ordinals()},
      {"end_of_input_policy", reorder.end_of_input_policy()},
      {"host_metadata_charged_bytes", reorder.host_metadata_charged_bytes()},
      {"descriptor_charged_count", reorder.descriptor_charged_count()},
    });
  }

  Json edge_capacities = Json::array();
  for (const auto& edge : plan.edge_capacities()) {
    edge_capacities.push_back({{"edge_id", edge.edge_id()},
                               {"max_items", edge.capacity().max_items()},
                               {"max_charged_bytes", edge.capacity().max_charged_bytes()}});
  }

  return {
    {"schema_version", kExecutionPlanSchemaVersion},
    {"kind", "ExecutionPlan"},
    {"input_digests",
     {{"resolved_pipeline", plan.inputs().resolved_pipeline().value()},
      {"scan_descriptor", plan.inputs().scan_descriptor().value()},
      {"target_envelope", plan.inputs().target_envelope().value()},
      {"machine_policy", plan.inputs().machine_policy().value()},
      {"provider_contracts", std::move(provider_contracts)}}},
    {"execution_profile", to_string(plan.execution_profile())},
    {"key_slot_tables", std::move(key_slot_tables)},
    {"reorder_plans", std::move(reorder_plans)},
    {"edge_capacities", std::move(edge_capacities)},
    {"resource_vector", resource_vector_json(plan.resources())},
    {"terminal_occurrences", plan.terminal_occurrences()},
    {"proof_obligations", plan.proof_obligations()},
  };
}

[[nodiscard]] Json verification_record_json(const VerificationRecord& record) {
  return {
    {"schema_version", kVerificationRecordSchemaVersion},
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
