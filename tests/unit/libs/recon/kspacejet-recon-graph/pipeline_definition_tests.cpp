#include "kspacejet/recon/graph/artifact_json.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/graph/execution_plan_compiler.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::string read_fixture(const std::string_view relative_path) {
  const auto path = std::filesystem::path(KSJ_RECON_FIXTURE_DIR) / relative_path;
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << "Unable to open fixture: " << path;
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string replace_once(std::string document, const std::string_view needle,
                                       const std::string_view replacement) {
  const auto position = document.find(needle);
  EXPECT_NE(std::string::npos, position) << "Test fixture did not contain expected text: " << needle;
  if (position == std::string::npos) {
    return {};
  }
  document.replace(position, needle.size(), replacement);
  return document;
}

[[nodiscard]] std::string json_null_array(const std::size_t count) {
  std::string result;
  result.reserve(count * 5U);
  for (std::size_t index = 0; index < count; ++index) {
    if (index != 0U)
      result.push_back(',');
    result.append("null");
  }
  return result;
}

[[nodiscard]] ksj::recon::ArtifactDigest parsed_digest(const std::string_view value) {
  auto digest = ksj::recon::ArtifactDigest::parse(value, "test digest");
  EXPECT_TRUE(digest.ok()) << digest.status();
  return std::move(digest).value();
}

[[nodiscard]] ksj::recon::TypeDescriptor reference_port_type(const std::string_view type_id) {
  auto descriptor = ksj::recon::TypeDescriptor::create({
    .type_id = std::string(type_id),
    .revision = 1U,
    .payload_schema_digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    .payload_kind = ksj::recon::PayloadKind::message_handle,
    .element_type = ksj::recon::ElementType::none,
    .rank = 0U,
    .dimensions = {},
    .layout = ksj::recon::LayoutKind::opaque,
    .strides = ksj::recon::StrideKind::canonical,
    .allowed_memory_domains = {ksj::recon::TypeMemoryDomain::host_normal},
    .min_alignment_bytes = 8U,
    .mutability = ksj::recon::PayloadMutability::immutable_after_publish,
    .metadata_schema_digest = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  });
  EXPECT_TRUE(descriptor.ok()) << descriptor.status();
  return std::move(descriptor).value();
}

[[nodiscard]] ksj::recon::TypeDescriptor reference_completed_frame_type() {
  auto descriptor = ksj::recon::completed_frame_slot_context_type();
  EXPECT_TRUE(descriptor.ok()) << descriptor.status();
  return std::move(descriptor).value();
}

[[nodiscard]] ksj::recon::graph::ResolvedProvider matching_provider() {
  return {
    .alias = "reference",
    .provider_id = "org.kspacejet.reference",
    .version = "1.0.0",
    .abi_major = 1U,
    .bundle_digest = parsed_digest("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
    .operators = {{
      .id = "reference_reconstruct",
      .interface_revision = "1",
      .contract_digest = parsed_digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
    }},
  };
}

[[nodiscard]] ksj::recon::graph::ResolvedProvider matching_m3_provider() {
  auto provider = matching_provider();
  provider.operators.push_back({
    .id = "reference_frame_source",
    .interface_revision = "1",
    .contract_digest = parsed_digest("sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
  });
  return provider;
}

// M3 is never wired directly to public ingress.  The test graph includes a
// generic resolved internal typed source; this deliberately does not establish
// host frame-completion provenance.  RA-01 keeps the future
// completion/occurrence binding fail-closed at EndOfInput.
[[nodiscard]] ksj::base::Result<ksj::recon::graph::PipelineDefinition> parse_frame_pipeline_definition() {
  constexpr std::string_view kM3Pipeline = R"json(
{
  "schema_version": "kspacejet.pipeline/v1",
  "kind": "PipelineDefinition",
  "pipeline": {"id": "org.example.internal-frame-m3", "revision": "1.0.0", "display_name": "Internal typed-frame M3 test"},
  "allowed_profiles": ["offline", "bounded-online"],
  "parameters": {},
  "provider_requirements": [{"alias": "reference", "provider_id": "org.kspacejet.reference", "version_requirement": ">=1.0.0 <2.0.0", "required_abi_major": 1}],
  "nodes": [
    {"id": "reconstruct", "operator": {"provider": "reference", "id": "reference_reconstruct", "requires_interface_revision": "1"}, "config": {}},
    {"id": "frame_source", "operator": {"provider": "reference", "id": "reference_frame_source", "requires_interface_revision": "1"}, "config": {}}
  ],
  "edges": [{"id": "completed_frame", "from": {"node": "frame_source", "port": "frame"}, "to": {"node": "reconstruct", "port": "acquisition"}}],
  "bindings": {
    "ingress": [{"id": "acquisitions", "type": "ismrmrd.acquisition/v1", "to": {"node": "frame_source", "port": "acquisition"}}],
    "egress": [{"id": "images", "type": "ismrmrd.image/v1", "from": {"node": "reconstruct", "port": "image"}}],
    "calibration": [],
    "merge": []
  },
  "annotations": {}
}
)json";
  return ksj::recon::graph::PipelineDefinition::parse_json(kM3Pipeline);
}

[[nodiscard]] ksj::recon::OperatorContract reference_contract(
  const ksj::recon::Quantity normal_flush_max_firings = 0U,
  std::vector<ksj::recon::ExecutionProfile> supported_profiles = {ksj::recon::ExecutionProfile::offline,
                                                                  ksj::recon::ExecutionProfile::bounded_online},
  std::vector<std::string> partition_key = {"slice", "contrast"},
  std::optional<ksj::recon::ReorderSpec> reorder = std::nullopt) {
  using namespace ksj::recon;

  const bool has_m3_reorder = reorder.has_value();
  const auto rates =
    normal_flush_max_firings == 0U
      ? RateSpec{.kind = RateKind::sdf,
                 .static_phases = {{
                   .inputs = {{.port_name = "acquisition", .items = 1U, .charged_bytes = 1024U}},
                   .outputs = {{.port_name = "image", .items = 1U, .charged_bytes = 128U}},
                 }}}
      : RateSpec{
          .kind = RateKind::keyed_dynamic,
          .completion = {.kind = CompletionKind::end_of_input, .on_end_of_input = EndOfInputPolicy::fail},
          .ordinary = {.max_firings = 1U, .outputs = {{.port_name = "image", .items = 1U, .charged_bytes = 128U}}},
          .normal_flush = {
            .max_firings = normal_flush_max_firings,
            .outputs = {{.port_name = "image", .items = 1U, .charged_bytes = 128U}},
          }};
  const auto normal_terminal_items = std::max<Quantity>(1U, normal_flush_max_firings);
  const auto activation_items = has_m3_reorder ? 1U : 2U;

  auto contract = OperatorContract::create({
    .operator_id = "reference_reconstruct",
    .operator_revision = "1.0.0",
    .provider_abi_major = 1U,
    .supported_profiles = std::move(supported_profiles),
    .ports =
      {
        {.name = "acquisition",
         .type_descriptor =
           has_m3_reorder ? reference_completed_frame_type() : reference_port_type("ismrmrd.acquisition"),
         .direction = PortDirection::input,
         .cardinality = PortCardinality::many,
         .required = true},
        {.name = "image",
         .type_descriptor = reference_port_type("ismrmrd.image"),
         .direction = PortDirection::output,
         .cardinality = PortCardinality::many,
         .required = false},
      },
    .execution = {.input_granularity = has_m3_reorder ? InputGranularity::frame : InputGranularity::acquisition,
                  .partition_key = std::move(partition_key),
                  .order_domain = OrderDomain::per_key,
                  .max_active_keys = 4U,
                  .max_in_flight = 2U,
                  .call_model = CallModel::keyed_parallel,
                  .max_items_per_activation = activation_items,
                  .cooperative_quantum_us = 100U},
    .batch = {.min_items = 1U,
              .preferred_items = activation_items,
              .max_items = activation_items,
              .max_charged_bytes = 1024U,
              .max_wait_us = 100U},
    .rates = rates,
    .resources = {.scratch_charged_bytes_per_firing = 64U,
                  .per_key_state_charged_bytes = 32U,
                  .per_scan_workspace_charged_bytes = 16U,
                  .output_items = 1U,
                  .output_charged_bytes = 128U,
                  .cpu_permits = 1U,
                  .memory_domain = MemoryDomain::host},
    .reorder = std::move(reorder),
    .terminal = {.normal = TerminalBehavior::flush_declared,
                 .cancel = TerminalBehavior::cleanup_declared,
                 .normal_max_output_items = normal_terminal_items,
                 .normal_max_output_charged_bytes = 128U * normal_terminal_items,
                 .normal_max_async_tokens = 1U,
                 .cancel_max_async_tokens = 1U},
  });
  EXPECT_TRUE(contract.ok()) << contract.status();
  return std::move(contract).value();
}

[[nodiscard]] ksj::recon::OperatorContract reference_internal_frame_source_contract() {
  using namespace ksj::recon;
  auto contract = OperatorContract::create({
    .operator_id = "reference_frame_source",
    .operator_revision = "1.0.0",
    .provider_abi_major = 1U,
    .supported_profiles = {ExecutionProfile::offline, ExecutionProfile::bounded_online},
    .ports =
      {
        {.name = "acquisition",
         .type_descriptor = reference_port_type("ismrmrd.acquisition"),
         .direction = PortDirection::input,
         .cardinality = PortCardinality::many,
         .required = true},
        {.name = "frame",
         .type_descriptor = reference_completed_frame_type(),
         .direction = PortDirection::output,
         .cardinality = PortCardinality::many,
         .required = false},
      },
    .execution = {.input_granularity = InputGranularity::acquisition,
                  .partition_key = {"slice", "contrast"},
                  .order_domain = OrderDomain::per_key,
                  .max_active_keys = 4U,
                  .max_in_flight = 1U,
                  .call_model = CallModel::keyed_parallel,
                  .max_items_per_activation = 1U,
                  .cooperative_quantum_us = 100U},
    .batch = {.min_items = 1U, .preferred_items = 1U, .max_items = 1U, .max_charged_bytes = 1024U, .max_wait_us = 100U},
    .rates = {.kind = RateKind::sdf,
              .static_phases = {{.inputs = {{.port_name = "acquisition", .items = 1U, .charged_bytes = 1024U}},
                                 .outputs = {{.port_name = "frame", .items = 1U, .charged_bytes = 128U}}}}},
    .resources = {.scratch_charged_bytes_per_firing = 16U,
                  .per_key_state_charged_bytes = 16U,
                  .per_scan_workspace_charged_bytes = 16U,
                  .output_items = 1U,
                  .output_charged_bytes = 128U,
                  .cpu_permits = 1U,
                  .memory_domain = MemoryDomain::host},
    .terminal = {.normal = TerminalBehavior::flush_declared,
                 .cancel = TerminalBehavior::cleanup_declared,
                 .normal_max_output_items = 1U,
                 .normal_max_output_charged_bytes = 128U,
                 .normal_max_async_tokens = 1U,
                 .cancel_max_async_tokens = 1U},
  });
  EXPECT_TRUE(contract.ok()) << contract.status();
  return std::move(contract).value();
}

[[nodiscard]] std::vector<ksj::recon::graph::OperatorContractBinding>
m3_contract_bindings(const ksj::recon::OperatorContract& reconstruct) {
  return {
    {.node_id = "reconstruct",
     .contract_digest = parsed_digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
     .contract = reconstruct},
    {.node_id = "frame_source",
     .contract_digest = parsed_digest("sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
     .contract = reference_internal_frame_source_contract()},
  };
}

[[nodiscard]] ksj::recon::ScanDescriptor scan_descriptor_with_slice_and_contrast_limits() {
  constexpr std::string_view kXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>8</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>cartesian</trajectory>
    <encodedSpace>
      <matrixSize><x>64</x><y>64</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm>
    </encodedSpace>
    <reconSpace>
      <matrixSize><x>64</x><y>64</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm>
    </reconSpace>
    <encodingLimits>
      <average><minimum>0</minimum><maximum>0</maximum><center>0</center></average>
      <slice><minimum>0</minimum><maximum>1</maximum><center>0</center></slice>
      <contrast><minimum>0</minimum><maximum>2</maximum><center>1</center></contrast>
      <phase><minimum>0</minimum><maximum>0</maximum><center>0</center></phase>
      <repetition><minimum>0</minimum><maximum>0</maximum><center>0</center></repetition>
      <set><minimum>0</minimum><maximum>0</maximum><center>0</center></set>
      <segment><minimum>0</minimum><maximum>0</maximum><center>0</center></segment>
    </encodingLimits>
  </encoding>
</ismrmrdHeader>
)xml";
  auto parsed = ksj::recon::ScanDescriptor::parse_ismrmrd_xml(kXml);
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] ksj::recon::ScanDescriptor scan_descriptor_with_ragged_slice_limits() {
  constexpr std::string_view kXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>8</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>cartesian</trajectory>
    <encodedSpace>
      <matrixSize><x>64</x><y>64</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm>
    </encodedSpace>
    <reconSpace>
      <matrixSize><x>64</x><y>64</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm>
    </reconSpace>
    <encodingLimits>
      <average><minimum>0</minimum><maximum>0</maximum><center>0</center></average>
      <slice><minimum>0</minimum><maximum>1</maximum><center>0</center></slice>
      <contrast><minimum>0</minimum><maximum>0</maximum><center>0</center></contrast>
      <phase><minimum>0</minimum><maximum>0</maximum><center>0</center></phase>
      <repetition><minimum>0</minimum><maximum>0</maximum><center>0</center></repetition>
      <set><minimum>0</minimum><maximum>0</maximum><center>0</center></set>
    </encodingLimits>
  </encoding>
  <encoding>
    <trajectory>cartesian</trajectory>
    <encodedSpace>
      <matrixSize><x>64</x><y>64</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm>
    </encodedSpace>
    <reconSpace>
      <matrixSize><x>64</x><y>64</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm>
    </reconSpace>
    <encodingLimits>
      <average><minimum>0</minimum><maximum>0</maximum><center>0</center></average>
      <slice><minimum>0</minimum><maximum>2</maximum><center>1</center></slice>
      <contrast><minimum>0</minimum><maximum>0</maximum><center>0</center></contrast>
      <phase><minimum>0</minimum><maximum>0</maximum><center>0</center></phase>
      <repetition><minimum>0</minimum><maximum>0</maximum><center>0</center></repetition>
      <set><minimum>0</minimum><maximum>0</maximum><center>0</center></set>
    </encodingLimits>
  </encoding>
</ismrmrdHeader>
)xml";
  auto parsed = ksj::recon::ScanDescriptor::parse_ismrmrd_xml(kXml);
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] ksj::recon::ScanDescriptor scan_descriptor_with_radial_slice_limits() {
  constexpr std::string_view kXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>8</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>radial</trajectory>
    <encodedSpace>
      <matrixSize><x>64</x><y>64</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm>
    </encodedSpace>
    <reconSpace>
      <matrixSize><x>64</x><y>64</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm>
    </reconSpace>
    <encodingLimits>
      <average><minimum>0</minimum><maximum>0</maximum><center>0</center></average>
      <slice><minimum>0</minimum><maximum>1</maximum><center>0</center></slice>
      <contrast><minimum>0</minimum><maximum>0</maximum><center>0</center></contrast>
      <phase><minimum>0</minimum><maximum>0</maximum><center>0</center></phase>
      <repetition><minimum>0</minimum><maximum>0</maximum><center>0</center></repetition>
      <set><minimum>0</minimum><maximum>0</maximum><center>0</center></set>
    </encodingLimits>
  </encoding>
</ismrmrdHeader>
)xml";
  auto parsed = ksj::recon::ScanDescriptor::parse_ismrmrd_xml(kXml);
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] ksj::recon::TargetEnvelope reference_target_envelope(const ksj::recon::Quantity max_xml_bytes = 8U *
                                                                                                              1024U) {
  auto envelope = ksj::recon::TargetEnvelope::create({
    .max_xml_bytes = max_xml_bytes,
    .max_frame_charged_bytes = 1024U,
    .max_image_charged_bytes = 1024U,
    .max_decoder_staging_bytes = 64U,
    .max_samples_per_acquisition = 256U,
    .max_trajectory_dimensions = 3U,
    .max_active_channels = 8U,
    .max_channel_groups = 1U,
    .max_dynamic_keys_per_scan = 8U,
    .max_active_scans = 1U,
    .arrival_envelope = {.max_acquisitions_per_second = 100U, .max_burst_acquisitions = 4U},
    .sink_service_assumption = {.minimum_drain_items_per_second = 100U,
                                .max_pause_us = 0U,
                                .slow_sink_policy = ksj::recon::SlowSinkPolicy::fail,
                                .transport_staging_bytes = 64U},
  });
  EXPECT_TRUE(envelope.ok()) << envelope.status();
  return std::move(envelope).value();
}

[[nodiscard]] ksj::recon::MachinePolicy
reference_machine_policy(const std::uint64_t scan_memory_bytes = 4096U,
                         std::vector<ksj::recon::ExecutionProfile> allowed_profiles = {
                           ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online}) {
  auto policy = ksj::recon::MachinePolicy::create({
    .resource_capacity =
      {
        .domains = {.host_normal_bytes = scan_memory_bytes,
                    .host_pinned_bytes = 0U,
                    .host_hugepage_bytes = 0U,
                    .shared_host_bytes = 0U,
                    .spool_bytes = 0U,
                    .transport_bytes = scan_memory_bytes,
                    .descriptor_count = scan_memory_bytes,
                    .async_token_count = scan_memory_bytes,
                    .cpu_leaf_permits = 4U,
                    .backend_gang_permits = 0U,
                    .provider_private_permits = 0U,
                    .io_slots = 1U},
        .host_total_cap_bytes = scan_memory_bytes,
      },
    .numa_domain_count = 1U,
    .allowed_memory_domains = {ksj::recon::MemoryDomain::host},
    .allowed_profiles = std::move(allowed_profiles),
    .scheduler_policy = ksj::recon::SchedulerPolicy::fair,
  });
  EXPECT_TRUE(policy.ok()) << policy.status();
  return std::move(policy).value();
}

[[nodiscard]] ksj::recon::graph::PlanArtifactDigests test_plan_digests() {
  return {
    .scan_descriptor = parsed_digest("sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
    .target_envelope = parsed_digest("sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
    .machine_policy = parsed_digest("sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
  };
}

[[nodiscard]] ksj::recon::ResourceVectorSpec resource_vector_spec_from(const ksj::recon::ResourceVector& resources) {
  ksj::recon::ResourceVectorSpec specification{
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
  for (const auto& device : resources.devices()) {
    specification.devices.push_back({.device_id = device.device_id(),
                                     .device_bytes = device.device_bytes(),
                                     .gpu_stream_slots = device.gpu_stream_slots(),
                                     .copy_engine_slots = device.copy_engine_slots()});
  }
  return specification;
}

[[nodiscard]] ksj::recon::ExecutionPlanSpec execution_plan_spec_from(const ksj::recon::ExecutionPlan& plan) {
  ksj::recon::ExecutionPlanSpec specification{
    .inputs = {.resolved_pipeline = plan.inputs().resolved_pipeline().value(),
               .scan_descriptor = plan.inputs().scan_descriptor().value(),
               .target_envelope = plan.inputs().target_envelope().value(),
               .machine_policy = plan.inputs().machine_policy().value()},
    .execution_profile = plan.execution_profile(),
    .resource_vector = resource_vector_spec_from(plan.resources()),
    .terminal_occurrences = plan.terminal_occurrences(),
    .proof_obligations = plan.proof_obligations(),
  };
  for (const auto& digest : plan.inputs().provider_contracts()) {
    specification.inputs.provider_contracts.push_back(digest.value());
  }
  for (const auto& table : plan.key_slot_tables()) {
    ksj::recon::KeySlotTablePlanSpec table_specification{
      .node_id = table.node_id(),
      .mapping_algorithm_id = table.mapping_algorithm_id(),
      .storage_accounting_id = table.storage_accounting_id(),
      .key_domain_bound = table.key_domain_bound(),
      .max_distinct_keys = table.max_distinct_keys(),
      .max_live_keys = table.max_live_keys(),
      .slot_count = table.slot_count(),
      .generation_policy = table.generation_policy(),
      .initial_generation = table.initial_generation(),
      .seal_on_completion = table.seal_on_completion(),
      .eviction_policy = table.eviction_policy(),
      .late_event_policy = table.late_event_policy(),
      .host_metadata_charged_bytes = table.host_metadata_charged_bytes(),
      .max_items_per_activation = table.max_items_per_activation(),
      .max_charged_bytes_per_activation = table.max_charged_bytes_per_activation(),
    };
    for (const auto& dimension : table.dense_dimensions()) {
      table_specification.dense_dimensions.push_back(
        {.field = dimension.field(), .minimum = dimension.minimum(), .cardinality = dimension.cardinality()});
    }
    specification.key_slot_tables.push_back(std::move(table_specification));
  }
  for (const auto& reorder : plan.reorder_plans()) {
    ksj::recon::ReorderPlanSpec reorder_specification{
      .node_id = reorder.node_id(),
      .order_domain_id = reorder.order_domain_id(),
      .ordinal_binding_id = reorder.ordinal_binding_id(),
      .completed_frame_input_port = reorder.completed_frame_input_port(),
      .ordered_output_port = reorder.ordered_output_port(),
      .outputs_per_ordinal = reorder.outputs_per_ordinal(),
      .charged_bytes_per_ordinal = reorder.charged_bytes_per_ordinal(),
      .mapping_algorithm_id = reorder.mapping_algorithm_id(),
      .storage_accounting_id = reorder.storage_accounting_id(),
      .ordinal_domain_bound = reorder.ordinal_domain_bound(),
      .first_expected_ordinal = reorder.first_expected_ordinal(),
      .last_expected_ordinal = reorder.last_expected_ordinal(),
      .max_ahead_items = reorder.max_ahead_items(),
      .max_ahead_charged_bytes = reorder.max_ahead_charged_bytes(),
      .max_gap_ordinals = reorder.max_gap_ordinals(),
      .occurrence_policy = reorder.occurrence_policy(),
      .publish_policy = reorder.publish_policy(),
      .certified_skipped_ordinals = reorder.certified_skipped_ordinals(),
      .end_of_input_policy = reorder.end_of_input_policy(),
      .host_metadata_charged_bytes = reorder.host_metadata_charged_bytes(),
      .descriptor_charged_count = reorder.descriptor_charged_count(),
    };
    for (const auto& dimension : reorder.ordinal_dimensions()) {
      reorder_specification.ordinal_dimensions.push_back(
        {.field = dimension.field(), .minimum = dimension.minimum(), .cardinality = dimension.cardinality()});
    }
    specification.reorder_plans.push_back(std::move(reorder_specification));
  }
  for (const auto& edge : plan.edge_capacities()) {
    specification.edge_capacities.push_back({.edge_id = edge.edge_id(),
                                             .max_items = edge.capacity().max_items(),
                                             .max_charged_bytes = edge.capacity().max_charged_bytes()});
  }
  return specification;
}

TEST(KSpaceJetReconGraphCanonicalJson, CanonicalizesObjectOrderAndComputesKnownSha256Vector) {
  const auto first = ksj::recon::graph::canonicalize_json(R"({"b":2,"a":{"z":true,"x":null}})");
  const auto second = ksj::recon::graph::canonicalize_json(R"({"a":{"x":null,"z":true},"b":2})");
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(R"({"a":{"x":null,"z":true},"b":2})", first.value());
  EXPECT_EQ(first.value(), second.value());

  const auto first_digest = ksj::recon::graph::canonical_json_digest(R"({"b":2,"a":{"z":true,"x":null}})");
  const auto second_digest = ksj::recon::graph::canonical_json_digest(R"({"a":{"x":null,"z":true},"b":2})");
  ASSERT_TRUE(first_digest.ok()) << first_digest.status();
  ASSERT_TRUE(second_digest.ok()) << second_digest.status();
  EXPECT_EQ(first_digest.value(), second_digest.value());

  const auto sha256 = ksj::recon::graph::sha256_digest("abc", "SHA-256 known vector");
  ASSERT_TRUE(sha256.ok()) << sha256.status();
  EXPECT_EQ("sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", sha256.value().value());
}

TEST(KSpaceJetReconGraphPipelineDefinition, ParsesValidFixtureAndRetainsItsCanonicalIdentity) {
  const auto document = read_fixture("valid/pipeline-v1-minimal.json");
  ASSERT_FALSE(document.empty());

  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_TRUE(definition.ok()) << definition.status();
  EXPECT_EQ("org.example.reference-cartesian", definition.value().id());
  EXPECT_EQ("1.0.0", definition.value().revision());
  ASSERT_EQ(2U, definition.value().allowed_profiles().size());
  EXPECT_EQ(ksj::recon::ExecutionProfile::offline, definition.value().allowed_profiles().front());
  ASSERT_EQ(1U, definition.value().providers().size());
  ASSERT_EQ(1U, definition.value().nodes().size());
  EXPECT_EQ("{\"acceleration\":{\"$param\":\"acceleration\"}}", definition.value().nodes().front().canonical_config);

  const auto reparsed = ksj::recon::graph::PipelineDefinition::parse_json(definition.value().canonical_json());
  ASSERT_TRUE(reparsed.ok()) << reparsed.status();
  EXPECT_EQ(definition.value().canonical_json(), reparsed.value().canonical_json());
  EXPECT_EQ(definition.value().digest(), reparsed.value().digest());

  EXPECT_NE(definition.value().artifact_digest(), definition.value().semantic_digest());
}

TEST(KSpaceJetReconGraphPipelineDefinition, EnforcesSchemaIdentityIdentifiersAndDisplayNameBounds) {
  const auto document = read_fixture("valid/pipeline-v1-minimal.json");

  const auto wrong_schema = replace_once(document, "{\n  \"schema_version\"",
                                         "{\n  \"$schema\": \"https://example.invalid/schema\",\n  \"schema_version\"");
  const auto wrong_schema_definition = ksj::recon::graph::PipelineDefinition::parse_json(wrong_schema);
  ASSERT_FALSE(wrong_schema_definition.ok());
  EXPECT_NE(std::string::npos, wrong_schema_definition.status().message().find("$schema"));

  const auto invalid_pipeline_id =
    replace_once(document, "\"id\": \"org.example.reference-cartesian\"", "\"id\": \"-invalid\"");
  EXPECT_FALSE(ksj::recon::graph::PipelineDefinition::parse_json(invalid_pipeline_id).ok());

  const auto invalid_provider_alias = replace_once(document, "\"alias\": \"reference\"", "\"alias\": \"9reference\"");
  EXPECT_FALSE(ksj::recon::graph::PipelineDefinition::parse_json(invalid_provider_alias).ok());

  const std::string overlong_display_name(257U, 'x');
  const auto invalid_display_name = replace_once(document, "\"display_name\": \"Reference Cartesian reconstruction\"",
                                                 "\"display_name\": \"" + overlong_display_name + "\"");
  EXPECT_FALSE(ksj::recon::graph::PipelineDefinition::parse_json(invalid_display_name).ok());

  const std::string overlong_node_id(129U, 'n');
  const auto invalid_node_id =
    replace_once(document, "\"id\": \"reconstruct\"", "\"id\": \"" + overlong_node_id + "\"");
  EXPECT_FALSE(ksj::recon::graph::PipelineDefinition::parse_json(invalid_node_id).ok());
}

TEST(KSpaceJetReconGraphPipelineDefinition, EnforcesStrictPipelineRevisionAndNarrowProviderVersionSyntax) {
  const auto document = read_fixture("valid/pipeline-v1-minimal.json");

  const auto strict_prerelease_and_build =
    replace_once(document, "\"revision\": \"1.0.0\"", "\"revision\": \"1.2.3-alpha.1+build.5\"");
  EXPECT_TRUE(ksj::recon::graph::PipelineDefinition::parse_json(strict_prerelease_and_build).ok());

  const auto leading_zero_revision = replace_once(document, "\"revision\": \"1.0.0\"", "\"revision\": \"01.0.0\"");
  EXPECT_FALSE(ksj::recon::graph::PipelineDefinition::parse_json(leading_zero_revision).ok());

  const auto invalid_prerelease_revision =
    replace_once(document, "\"revision\": \"1.0.0\"", "\"revision\": \"1.0.0-01\"");
  EXPECT_FALSE(ksj::recon::graph::PipelineDefinition::parse_json(invalid_prerelease_revision).ok());

  for (const std::string_view requirement : {"^1.0.0", ">= 1.0.0", ">=1.0.0-beta", "1.0"}) {
    const auto invalid_requirement = replace_once(document, "\"version_requirement\": \">=1.0.0 <2.0.0\"",
                                                  "\"version_requirement\": \"" + std::string(requirement) + "\"");
    EXPECT_FALSE(ksj::recon::graph::PipelineDefinition::parse_json(invalid_requirement).ok()) << requirement;
  }
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsCompletedFrameContextAsPublicIngress) {
  const auto document = replace_once(read_fixture("valid/pipeline-v1-minimal.json"),
                                     "\"type\": \"ismrmrd.acquisition/v1\"", "\"type\": \"ksj.kspace-frame/v1\"");
  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(definition.ok());
  EXPECT_NE(std::string::npos, definition.status().message().find("must not be a public ingress"));
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsDuplicateJsonKeysBeforeDomMaterialization) {
  auto document = replace_once(read_fixture("valid/pipeline-v1-minimal.json"), R"("kind": "PipelineDefinition",)",
                               R"("kind": "PipelineDefinition",
  "\u006b\u0069\u006e\u0064": "PipelineDefinition",)");

  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, definition.status().code());
  EXPECT_NE(std::string::npos, definition.status().message().find("duplicate key"));
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsUnknownFrameworkSemanticField) {
  auto document = replace_once(read_fixture("valid/pipeline-v1-minimal.json"), R"("id": "reference_reconstruct",)",
                               R"("id": "reference_reconstruct",
        "unexpected_semantic": true,)");

  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, definition.status().code());
  EXPECT_NE(std::string::npos, definition.status().message().find("unknown field 'unexpected_semantic'"));
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsInputBeyondTheControlPlaneByteLimit) {
  auto document = read_fixture("valid/pipeline-v1-minimal.json");
  document.append(ksj::recon::graph::kPipelineDefinitionJsonParseLimits.max_document_bytes + 1U, ' ');

  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, definition.status().code());
  EXPECT_NE(std::string::npos, definition.status().message().find("maximum byte size"));
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsInputBeyondTheNestingLimit) {
  std::string nested = "{}";
  for (std::size_t index = 0; index <= ksj::recon::graph::kPipelineDefinitionJsonParseLimits.max_depth; ++index) {
    nested = "{\"nested\":" + nested + "}";
  }
  auto document =
    replace_once(read_fixture("valid/pipeline-v1-minimal.json"), R"("annotations": {})", "\"annotations\": " + nested);

  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, definition.status().code());
  EXPECT_NE(std::string::npos, definition.status().message().find("maximum depth"));
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsInputBeyondTheArrayElementLimit) {
  const auto array = "\"edges\": [" +
                     json_null_array(ksj::recon::graph::kPipelineDefinitionJsonParseLimits.max_array_elements + 1U) +
                     "]";
  auto document = replace_once(read_fixture("valid/pipeline-v1-minimal.json"), R"("edges": [])", array);

  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, definition.status().code());
  EXPECT_NE(std::string::npos, definition.status().message().find("array exceeds"));
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsInputBeyondTheStringLimit) {
  const std::string display_name(ksj::recon::graph::kPipelineDefinitionJsonParseLimits.max_string_bytes + 1U, 'x');
  auto document = replace_once(read_fixture("valid/pipeline-v1-minimal.json"),
                               R"("display_name": "Reference Cartesian reconstruction")",
                               "\"display_name\": \"" + display_name + "\"");

  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, definition.status().code());
  EXPECT_NE(std::string::npos, definition.status().message().find("string exceeds"));
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsIntegersOutsideTheExactV1Range) {
  auto document = replace_once(read_fixture("valid/pipeline-v1-minimal.json"), R"("required_abi_major": 1)",
                               R"("required_abi_major": 9007199254740992)");

  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, definition.status().code());
  EXPECT_NE(std::string::npos, definition.status().message().find("exact v1 range"));
}

TEST(KSpaceJetReconGraphPipelineDefinition, CanonicalizesSchemaDeclaredUnorderedProfilesStably) {
  const auto original_document = read_fixture("valid/pipeline-v1-minimal.json");
  auto reordered_document = replace_once(original_document, R"("offline",
    "bounded-online")",
                                         R"("bounded-online",
    "offline")");

  const auto original = ksj::recon::graph::PipelineDefinition::parse_json(original_document);
  const auto reordered = ksj::recon::graph::PipelineDefinition::parse_json(reordered_document);
  ASSERT_TRUE(original.ok()) << original.status();
  ASSERT_TRUE(reordered.ok()) << reordered.status();
  EXPECT_EQ(original.value().canonical_json(), reordered.value().canonical_json());
  EXPECT_EQ(original.value().artifact_digest(), reordered.value().artifact_digest());
  EXPECT_EQ(original.value().semantic_digest(), reordered.value().semantic_digest());
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsRuntimeSizingFieldAnywhereInAuthoredDocument) {
  const auto document = read_fixture("invalid/pipeline-v1-runtime-field.json");
  ASSERT_FALSE(document.empty());

  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, definition.status().code());
  EXPECT_NE(std::string::npos, definition.status().message().find("worker_count"));

  const auto authored_key_slot_table = replace_once(read_fixture("valid/pipeline-v1-minimal.json"), "\"config\": {",
                                                    "\"config\": {\n        \"key_slot_tables\": [],");
  const auto key_slot_definition = ksj::recon::graph::PipelineDefinition::parse_json(authored_key_slot_table);
  ASSERT_FALSE(key_slot_definition.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, key_slot_definition.status().code());
  EXPECT_NE(std::string::npos, key_slot_definition.status().message().find("key_slot_tables"));
}

TEST(KSpaceJetReconGraphResolvedPipeline, RejectsProviderIdentityMismatch) {
  const auto definition =
    ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-v1-minimal.json"));
  ASSERT_TRUE(definition.ok()) << definition.status();

  auto provider = matching_provider();
  provider.provider_id = "org.example.wrong-provider";
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {std::move(provider)});

  ASSERT_FALSE(resolved.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, resolved.status().code());
  EXPECT_NE(std::string::npos, resolved.status().message().find("provider_id/ABI"));
}

TEST(KSpaceJetReconGraphResolvedPipeline, EvaluatesOnlyStableExactAndComparatorProviderVersions) {
  const auto range_definition =
    ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-v1-minimal.json"));
  ASSERT_TRUE(range_definition.ok()) << range_definition.status();

  auto range_match = matching_provider();
  range_match.version = "1.9.0";
  EXPECT_TRUE(ksj::recon::graph::ResolvedPipeline::resolve(range_definition.value(), {std::move(range_match)}).ok());

  auto range_miss = matching_provider();
  range_miss.version = "2.0.0";
  const auto out_of_range =
    ksj::recon::graph::ResolvedPipeline::resolve(range_definition.value(), {std::move(range_miss)});
  ASSERT_FALSE(out_of_range.ok());
  EXPECT_NE(std::string::npos, out_of_range.status().message().find("does not satisfy"));

  auto prerelease = matching_provider();
  prerelease.version = "1.0.0-beta";
  const auto unstable = ksj::recon::graph::ResolvedPipeline::resolve(range_definition.value(), {std::move(prerelease)});
  ASSERT_FALSE(unstable.ok());
  EXPECT_NE(std::string::npos, unstable.status().message().find("stable"));

  const auto exact_document =
    replace_once(read_fixture("valid/pipeline-v1-minimal.json"), "\"version_requirement\": \">=1.0.0 <2.0.0\"",
                 "\"version_requirement\": \"1.2.3\"");
  const auto exact_definition = ksj::recon::graph::PipelineDefinition::parse_json(exact_document);
  ASSERT_TRUE(exact_definition.ok()) << exact_definition.status();
  auto exact_match = matching_provider();
  exact_match.version = "1.2.3";
  EXPECT_TRUE(ksj::recon::graph::ResolvedPipeline::resolve(exact_definition.value(), {std::move(exact_match)}).ok());

  auto exact_miss = matching_provider();
  exact_miss.version = "1.2.4";
  EXPECT_FALSE(ksj::recon::graph::ResolvedPipeline::resolve(exact_definition.value(), {std::move(exact_miss)}).ok());
}

TEST(KSpaceJetReconGraphResolvedPipeline, SupportsEveryNarrowProviderVersionComparator) {
  struct ComparatorCase {
    std::string_view requirement;
    std::string_view accepted_version;
    std::string_view rejected_version;
  };
  constexpr ComparatorCase cases[]{
    {.requirement = ">1.0.0", .accepted_version = "1.0.1", .rejected_version = "1.0.0"},
    {.requirement = ">=1.0.0", .accepted_version = "1.0.0", .rejected_version = "0.9.9"},
    {.requirement = "<2.0.0", .accepted_version = "1.9.9", .rejected_version = "2.0.0"},
    {.requirement = "<=1.0.0", .accepted_version = "1.0.0", .rejected_version = "1.0.1"},
    {.requirement = "=1.0.0", .accepted_version = "1.0.0", .rejected_version = "1.0.1"},
  };

  const auto document = read_fixture("valid/pipeline-v1-minimal.json");
  for (const auto& test_case : cases) {
    const auto definition_document =
      replace_once(document, "\"version_requirement\": \">=1.0.0 <2.0.0\"",
                   "\"version_requirement\": \"" + std::string(test_case.requirement) + "\"");
    const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(definition_document);
    ASSERT_TRUE(definition.ok()) << test_case.requirement << ": " << definition.status();

    auto accepted_provider = matching_provider();
    accepted_provider.version = test_case.accepted_version;
    EXPECT_TRUE(ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {std::move(accepted_provider)}).ok())
      << test_case.requirement;

    auto rejected_provider = matching_provider();
    rejected_provider.version = test_case.rejected_version;
    EXPECT_FALSE(ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {std::move(rejected_provider)}).ok())
      << test_case.requirement;
  }
}

TEST(KSpaceJetReconGraphExecutionPlanCompiler, DerivesDenseKeySlotTablesAndVerifiesTheFrozenPlan) {
  const auto definition =
    ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-v1-minimal.json"));
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const auto scan = scan_descriptor_with_slice_and_contrast_limits();
  const auto envelope = reference_target_envelope();
  const auto policy = reference_machine_policy();
  const auto contract = reference_contract();
  const ksj::recon::graph::PlanBuildRequest request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan,
    .target_envelope = envelope,
    .machine_policy = policy,
    .artifact_digests = test_plan_digests(),
    .operator_contracts = {{.node_id = "reconstruct",
                            .contract_digest =
                              parsed_digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
                            .contract = contract}},
  };

  const auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(request);
  ASSERT_TRUE(compiled.ok()) << compiled.status();
  ASSERT_EQ(1U, compiled.value().plan.key_slot_tables().size());
  const auto& key_slot_table = compiled.value().plan.key_slot_tables().front();
  EXPECT_EQ("reconstruct", key_slot_table.node_id());
  ASSERT_EQ(2U, key_slot_table.dense_dimensions().size());
  EXPECT_EQ("slice", key_slot_table.dense_dimensions()[0].field());
  EXPECT_EQ(0U, key_slot_table.dense_dimensions()[0].minimum());
  EXPECT_EQ(2U, key_slot_table.dense_dimensions()[0].cardinality());
  EXPECT_EQ("contrast", key_slot_table.dense_dimensions()[1].field());
  EXPECT_EQ(0U, key_slot_table.dense_dimensions()[1].minimum());
  EXPECT_EQ(3U, key_slot_table.dense_dimensions()[1].cardinality());
  // XML says 2 slices x 3 contrasts.  Four fixed physical slots serve the
  // six-key dense semantic table; only Completed keys may be evicted/reused.
  EXPECT_EQ(6U, key_slot_table.key_domain_bound());
  EXPECT_EQ(6U, key_slot_table.max_distinct_keys());
  EXPECT_EQ(4U, key_slot_table.max_live_keys());
  EXPECT_EQ(4U, key_slot_table.slot_count());
  EXPECT_EQ("dense-mixed-radix/v1", key_slot_table.mapping_algorithm_id());
  EXPECT_EQ("monotonic-u64/v1", key_slot_table.generation_policy());
  EXPECT_EQ(1U, key_slot_table.initial_generation());
  EXPECT_TRUE(key_slot_table.seal_on_completion());
  EXPECT_EQ("completed-only", key_slot_table.eviction_policy());
  EXPECT_EQ("fail", key_slot_table.late_event_policy());
  EXPECT_EQ(160U, key_slot_table.host_metadata_charged_bytes());
  EXPECT_EQ(4U, compiled.value().plan.terminal_occurrences());
  // 160 B KeySlot metadata + 16 B scan workspace + 4*32 B Provider
  // per-slot state + 2*64 B scratch + 2*128 B outputs + 128 B terminal.
  EXPECT_EQ(816U, compiled.value().plan.resources().host_normal_bytes());
  EXPECT_EQ(128U, compiled.value().plan.resources().transport_bytes());
  EXPECT_GT(compiled.value().plan.resources().descriptor_count(), 0U);
  EXPECT_EQ(
    (std::vector<std::string>{"PO-01.typed_ports", "PO-04.finite_bounds", "PO-05.resource_vector",
                              "PO-06.dense_key_slots", "PO-08.bounded_dependency_progress", "PO-12.permit_budget"}),
    compiled.value().plan.proof_obligations());
  const auto plan_preimage = ksj::recon::graph::serialize_execution_plan_canonical_json(compiled.value().plan);
  ASSERT_TRUE(plan_preimage.ok()) << plan_preimage.status();
  const auto plan_preimage_digest = ksj::recon::graph::domain_separated_sha256_digest(
    "kspacejet:artifact:execution-plan:1", plan_preimage.value(), "public ExecutionPlan preimage");
  ASSERT_TRUE(plan_preimage_digest.ok()) << plan_preimage_digest.status();
  EXPECT_EQ(compiled.value().plan.digest(), plan_preimage_digest.value());
  EXPECT_TRUE(ksj::recon::graph::ExecutionPlanCompiler::deterministic_recheck(compiled.value(), request).ok());
  const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(compiled.value().plan, request);
  ASSERT_TRUE(verification.ok()) << verification.status();
  EXPECT_EQ(compiled.value().plan.digest(), verification.value().execution_plan_digest());
  const auto record_preimage = ksj::recon::graph::serialize_verification_record_canonical_json(verification.value());
  ASSERT_TRUE(record_preimage.ok()) << record_preimage.status();
  const auto record_preimage_digest = ksj::recon::graph::domain_separated_sha256_digest(
    "kspacejet:artifact:verification-record:1", record_preimage.value(), "public VerificationRecord preimage");
  ASSERT_TRUE(record_preimage_digest.ok()) << record_preimage_digest.status();
  EXPECT_EQ(verification.value().digest(), record_preimage_digest.value());
}

TEST(KSpaceJetReconGraphExecutionPlanCompiler, DerivesAndIndependentlyVerifiesThePlanOwnedDenseCartesianReorderDomain) {
  const auto definition = parse_frame_pipeline_definition();
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_m3_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const auto contract = reference_contract(
    0U, {ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online}, {"slice", "contrast"},
    ksj::recon::ReorderSpec{
      .completed_frame_input_port = "acquisition",
      .ordered_output_port = "image",
      .outputs_per_ordinal = 1U,
      .order_projection = {"slice", "contrast"},
      .max_ahead_items = 3U,
      .max_ahead_charged_bytes = 512U,
      .missing_at_end_of_input = ksj::recon::EndOfInputPolicy::fail,
    });
  const ksj::recon::graph::PlanBuildRequest request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan_descriptor_with_slice_and_contrast_limits(),
    .target_envelope = reference_target_envelope(),
    .machine_policy = reference_machine_policy(),
    .artifact_digests = test_plan_digests(),
    .operator_contracts = m3_contract_bindings(contract),
  };

  const auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(request);
  ASSERT_TRUE(compiled.ok()) << compiled.status();
  ASSERT_EQ(1U, compiled.value().plan.reorder_plans().size());
  const auto& reorder = compiled.value().plan.reorder_plans().front();
  EXPECT_EQ("reconstruct", reorder.node_id());
  EXPECT_EQ("reconstruct", reorder.order_domain_id());
  EXPECT_EQ(ksj::recon::kCompletedFrameSlotContextSemanticKeyOrdinalBindingId, reorder.ordinal_binding_id());
  EXPECT_EQ("acquisition", reorder.completed_frame_input_port());
  EXPECT_EQ("image", reorder.ordered_output_port());
  EXPECT_EQ(1U, reorder.outputs_per_ordinal());
  EXPECT_EQ(128U, reorder.charged_bytes_per_ordinal());
  EXPECT_EQ("dense-cartesian-ordinal/v1", reorder.mapping_algorithm_id());
  EXPECT_EQ("kspacejet.reorder-storage/dense-cartesian-v1", reorder.storage_accounting_id());
  ASSERT_EQ(2U, reorder.ordinal_dimensions().size());
  EXPECT_EQ("slice", reorder.ordinal_dimensions()[0].field());
  EXPECT_EQ(0U, reorder.ordinal_dimensions()[0].minimum());
  EXPECT_EQ(2U, reorder.ordinal_dimensions()[0].cardinality());
  EXPECT_EQ("contrast", reorder.ordinal_dimensions()[1].field());
  EXPECT_EQ(0U, reorder.ordinal_dimensions()[1].minimum());
  EXPECT_EQ(3U, reorder.ordinal_dimensions()[1].cardinality());
  EXPECT_EQ(6U, reorder.ordinal_domain_bound());
  EXPECT_EQ(0U, reorder.first_expected_ordinal());
  EXPECT_EQ(5U, reorder.last_expected_ordinal());
  EXPECT_EQ(3U, reorder.max_ahead_items());
  EXPECT_EQ(512U, reorder.max_ahead_charged_bytes());
  // Closed-domain arithmetic fact only, never a dispatch or skip grant.
  EXPECT_EQ(5U, reorder.max_gap_ordinals());
  EXPECT_EQ(ksj::recon::kStrictDenseAllTuplesReorderOccurrencePolicy, reorder.occurrence_policy());
  EXPECT_EQ("next-expected-only", reorder.publish_policy());
  EXPECT_TRUE(reorder.certified_skipped_ordinals().empty());
  EXPECT_EQ("fail", reorder.end_of_input_policy());
  EXPECT_EQ(144U, reorder.host_metadata_charged_bytes());
  EXPECT_EQ(3U, reorder.descriptor_charged_count());
  // The graph also carries a resolved internal typed source.  Its resources
  // are distinct from the M3 reservation, which must still be covered.
  EXPECT_GE(compiled.value().plan.resources().host_normal_bytes(),
            reorder.host_metadata_charged_bytes() + reorder.max_ahead_charged_bytes());
  EXPECT_GE(compiled.value().plan.resources().descriptor_count(), reorder.descriptor_charged_count());
  EXPECT_EQ((std::vector<std::string>{"PO-01.typed_ports", "PO-04.finite_bounds", "PO-05.resource_vector",
                                      "PO-06.dense_key_slots", "PO-07.m3_completed_frame_slot_binding",
                                      "RA-01.m3_strict_dense_all_tuples_eoi", "PO-08.bounded_dependency_progress",
                                      "PO-12.permit_budget"}),
            compiled.value().plan.proof_obligations());

  const auto canonical = ksj::recon::graph::serialize_execution_plan_canonical_json(compiled.value().plan);
  ASSERT_TRUE(canonical.ok()) << canonical.status();
  EXPECT_NE(std::string::npos, canonical.value().find("\"reorder_plans\":[{"));
  const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(compiled.value().plan, request);
  ASSERT_TRUE(verification.ok()) << verification.status();
}

TEST(KSpaceJetReconGraphExecutionPlanCompiler, RejectsNonXmlAndIncompleteDenseCartesianReorderAxes) {
  const auto definition = parse_frame_pipeline_definition();
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_m3_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const auto valid_contract = reference_contract(
    0U, {ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online}, {"slice", "contrast"},
    ksj::recon::ReorderSpec{
      .completed_frame_input_port = "acquisition",
      .ordered_output_port = "image",
      .outputs_per_ordinal = 1U,
      .order_projection = {"slice", "contrast"},
      .max_ahead_items = 3U,
      .max_ahead_charged_bytes = 512U,
      .missing_at_end_of_input = ksj::recon::EndOfInputPolicy::fail,
    });
  const ksj::recon::graph::PlanBuildRequest baseline_request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan_descriptor_with_slice_and_contrast_limits(),
    .target_envelope = reference_target_envelope(),
    .machine_policy = reference_machine_policy(),
    .artifact_digests = test_plan_digests(),
    .operator_contracts = m3_contract_bindings(valid_contract),
  };
  const auto baseline = ksj::recon::graph::ExecutionPlanCompiler::compile(baseline_request);
  ASSERT_TRUE(baseline.ok()) << baseline.status();

  auto non_xml_axis_request = baseline_request;
  non_xml_axis_request.operator_contracts.front().contract = reference_contract(
    0U, {ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online}, {"acquisition_ordinal"},
    ksj::recon::ReorderSpec{
      .completed_frame_input_port = "acquisition",
      .ordered_output_port = "image",
      .outputs_per_ordinal = 1U,
      .order_projection = {"acquisition_ordinal"},
      .max_ahead_items = 3U,
      .max_ahead_charged_bytes = 512U,
      .missing_at_end_of_input = ksj::recon::EndOfInputPolicy::fail,
    });
  const auto non_xml_compilation = ksj::recon::graph::ExecutionPlanCompiler::compile(non_xml_axis_request);
  ASSERT_FALSE(non_xml_compilation.ok());
  EXPECT_NE(std::string::npos, non_xml_compilation.status().message().find("dynamic/sparse KeySlot domain"));
  const auto non_xml_verification =
    ksj::recon::graph::ExecutionPlanVerifier::verify(baseline.value().plan, non_xml_axis_request);
  ASSERT_FALSE(non_xml_verification.ok());
  EXPECT_NE(std::string::npos, non_xml_verification.status().message().find("dynamic/sparse KeySlot domain"));

  auto undeclared_axis_request = baseline_request;
  undeclared_axis_request.operator_contracts.front().contract = reference_contract(
    0U, {ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online}, {"slice", "phase"},
    ksj::recon::ReorderSpec{
      .completed_frame_input_port = "acquisition",
      .ordered_output_port = "image",
      .outputs_per_ordinal = 1U,
      .order_projection = {"slice", "phase"},
      .max_ahead_items = 3U,
      .max_ahead_charged_bytes = 512U,
      .missing_at_end_of_input = ksj::recon::EndOfInputPolicy::fail,
    });
  const auto undeclared_compilation = ksj::recon::graph::ExecutionPlanCompiler::compile(undeclared_axis_request);
  ASSERT_FALSE(undeclared_compilation.ok());
  EXPECT_NE(std::string::npos,
            undeclared_compilation.status().message().find("must include varying FrameSlotContext axis 'contrast'"));
  const auto undeclared_verification =
    ksj::recon::graph::ExecutionPlanVerifier::verify(baseline.value().plan, undeclared_axis_request);
  ASSERT_FALSE(undeclared_verification.ok());
  EXPECT_NE(std::string::npos,
            undeclared_verification.status().message().find("must include varying FrameSlotContext axis 'contrast'"));

  auto unsupported_frame_key_axis_request = baseline_request;
  unsupported_frame_key_axis_request.operator_contracts.front().contract =
    reference_contract(0U, {ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online},
                       {"slice", "contrast", "segment"},
                       ksj::recon::ReorderSpec{
                         .completed_frame_input_port = "acquisition",
                         .ordered_output_port = "image",
                         .outputs_per_ordinal = 1U,
                         .order_projection = {"slice", "contrast", "segment"},
                         .max_ahead_items = 3U,
                         .max_ahead_charged_bytes = 512U,
                         .missing_at_end_of_input = ksj::recon::EndOfInputPolicy::fail,
                       });
  const auto unsupported_frame_key_compilation =
    ksj::recon::graph::ExecutionPlanCompiler::compile(unsupported_frame_key_axis_request);
  ASSERT_FALSE(unsupported_frame_key_compilation.ok());
  EXPECT_NE(std::string::npos,
            unsupported_frame_key_compilation.status().message().find("FrameSemanticKey/FrameSlotContext"));
  const auto unsupported_frame_key_verification =
    ksj::recon::graph::ExecutionPlanVerifier::verify(baseline.value().plan, unsupported_frame_key_axis_request);
  ASSERT_FALSE(unsupported_frame_key_verification.ok());
  EXPECT_NE(std::string::npos,
            unsupported_frame_key_verification.status().message().find("FrameSemanticKey/FrameSlotContext"));
}

TEST(KSpaceJetReconGraphExecutionPlanCompiler, RejectsRaggedAndNonCartesianM3ReorderScanDomains) {
  const auto definition = parse_frame_pipeline_definition();
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_m3_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const auto contract =
    reference_contract(0U, {ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online},
                       {"encoding", "slice", "contrast"},
                       ksj::recon::ReorderSpec{
                         .completed_frame_input_port = "acquisition",
                         .ordered_output_port = "image",
                         .outputs_per_ordinal = 1U,
                         .order_projection = {"encoding", "slice", "contrast"},
                         .max_ahead_items = 2U,
                         .max_ahead_charged_bytes = 256U,
                         .missing_at_end_of_input = ksj::recon::EndOfInputPolicy::fail,
                       });
  const ksj::recon::graph::PlanBuildRequest baseline_request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan_descriptor_with_slice_and_contrast_limits(),
    .target_envelope = reference_target_envelope(),
    .machine_policy = reference_machine_policy(),
    .artifact_digests = test_plan_digests(),
    .operator_contracts = m3_contract_bindings(contract),
  };
  const auto baseline = ksj::recon::graph::ExecutionPlanCompiler::compile(baseline_request);
  ASSERT_TRUE(baseline.ok()) << baseline.status();

  const auto ragged_scan = scan_descriptor_with_ragged_slice_limits();
  const ksj::recon::graph::PlanBuildRequest ragged_request{
    .resolved_pipeline = baseline_request.resolved_pipeline,
    .requested_profile = baseline_request.requested_profile,
    .scan_descriptor = ragged_scan,
    .target_envelope = baseline_request.target_envelope,
    .machine_policy = baseline_request.machine_policy,
    .artifact_digests = baseline_request.artifact_digests,
    .operator_contracts = baseline_request.operator_contracts,
  };
  const auto ragged_compilation = ksj::recon::graph::ExecutionPlanCompiler::compile(ragged_request);
  ASSERT_FALSE(ragged_compilation.ok());
  EXPECT_NE(std::string::npos, ragged_compilation.status().message().find("non-uniform ISMRMRD XML bounds"));
  const auto ragged_verification =
    ksj::recon::graph::ExecutionPlanVerifier::verify(baseline.value().plan, ragged_request);
  ASSERT_FALSE(ragged_verification.ok());
  EXPECT_NE(std::string::npos, ragged_verification.status().message().find("non-uniform ISMRMRD XML bounds"));

  const auto radial_scan = scan_descriptor_with_radial_slice_limits();
  const ksj::recon::graph::PlanBuildRequest radial_request{
    .resolved_pipeline = baseline_request.resolved_pipeline,
    .requested_profile = baseline_request.requested_profile,
    .scan_descriptor = radial_scan,
    .target_envelope = baseline_request.target_envelope,
    .machine_policy = baseline_request.machine_policy,
    .artifact_digests = baseline_request.artifact_digests,
    .operator_contracts = baseline_request.operator_contracts,
  };
  const auto radial_compilation = ksj::recon::graph::ExecutionPlanCompiler::compile(radial_request);
  ASSERT_FALSE(radial_compilation.ok());
  EXPECT_NE(std::string::npos, radial_compilation.status().message().find("only Cartesian ISMRMRD encodings"));
  const auto radial_verification =
    ksj::recon::graph::ExecutionPlanVerifier::verify(baseline.value().plan, radial_request);
  ASSERT_FALSE(radial_verification.ok());
  EXPECT_NE(std::string::npos, radial_verification.status().message().find("only Cartesian ISMRMRD encodings"));
}

TEST(KSpaceJetReconGraphExecutionPlanCompiler, RejectsDynamicAndSparseKeyDomainsBeforeTheyReachTheKeySlotPlan) {
  const auto definition =
    ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-v1-minimal.json"));
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const auto scan = scan_descriptor_with_slice_and_contrast_limits();
  const auto envelope = reference_target_envelope();
  const auto policy = reference_machine_policy();
  const ksj::recon::graph::PlanBuildRequest baseline_request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan,
    .target_envelope = envelope,
    .machine_policy = policy,
    .artifact_digests = test_plan_digests(),
    .operator_contracts = {{.node_id = "reconstruct",
                            .contract_digest =
                              parsed_digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
                            .contract = reference_contract()}},
  };
  const auto baseline = ksj::recon::graph::ExecutionPlanCompiler::compile(baseline_request);
  ASSERT_TRUE(baseline.ok()) << baseline.status();

  auto dynamic_request = baseline_request;
  dynamic_request.operator_contracts.front().contract = reference_contract(
    0U, {ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online}, {"acquisition_ordinal"});
  const auto dynamic_compilation = ksj::recon::graph::ExecutionPlanCompiler::compile(dynamic_request);
  ASSERT_FALSE(dynamic_compilation.ok());
  EXPECT_NE(std::string::npos, dynamic_compilation.status().message().find("dynamic/sparse"));
  const auto dynamic_verification =
    ksj::recon::graph::ExecutionPlanVerifier::verify(baseline.value().plan, dynamic_request);
  ASSERT_FALSE(dynamic_verification.ok());
  EXPECT_NE(std::string::npos, dynamic_verification.status().message().find("dynamic/sparse"));

  // An XML-absent semantic index cannot be turned into an envelope-sized
  // sparse table.  Use a separate descriptor because PlanBuildRequest keeps
  // ScanDescriptor by reference.
  const auto sparse_scan = scan_descriptor_with_radial_slice_limits();
  auto sparse_bindings = baseline_request.operator_contracts;
  sparse_bindings.front().contract = reference_contract(
    0U, {ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online}, {"segment"});
  const ksj::recon::graph::PlanBuildRequest sparse_request{
    .resolved_pipeline = baseline_request.resolved_pipeline,
    .requested_profile = baseline_request.requested_profile,
    .scan_descriptor = sparse_scan,
    .target_envelope = baseline_request.target_envelope,
    .machine_policy = baseline_request.machine_policy,
    .artifact_digests = baseline_request.artifact_digests,
    .operator_contracts = sparse_bindings,
  };
  const auto sparse_compilation = ksj::recon::graph::ExecutionPlanCompiler::compile(sparse_request);
  ASSERT_FALSE(sparse_compilation.ok());
  EXPECT_NE(std::string::npos,
            sparse_compilation.status().message().find("envelope-backed dynamic/sparse KeySlot domains"));
  const auto sparse_verification =
    ksj::recon::graph::ExecutionPlanVerifier::verify(baseline.value().plan, sparse_request);
  ASSERT_FALSE(sparse_verification.ok());
  EXPECT_NE(std::string::npos,
            sparse_verification.status().message().find("envelope-backed dynamic/sparse KeySlot domains"));
}

TEST(KSpaceJetReconGraphExecutionPlanCompiler, CountsEveryDeclaredNormalFlushOccurrenceInTheTerminalCertificate) {
  const auto definition =
    ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-v1-minimal.json"));
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const auto contract = reference_contract(3U);
  const ksj::recon::graph::PlanBuildRequest request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan_descriptor_with_slice_and_contrast_limits(),
    .target_envelope = reference_target_envelope(),
    .machine_policy = reference_machine_policy(),
    .artifact_digests = test_plan_digests(),
    .operator_contracts = {{.node_id = "reconstruct",
                            .contract_digest =
                              parsed_digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
                            .contract = contract}},
  };

  const auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(request);
  ASSERT_TRUE(compiled.ok()) << compiled.status();
  // Two host terminal transitions + three normal_flush firings + one async
  // token for each terminal path.
  EXPECT_EQ(7U, compiled.value().plan.terminal_occurrences());

  const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(compiled.value().plan, request);
  ASSERT_TRUE(verification.ok()) << verification.status();
  EXPECT_EQ(7U, verification.value().verified_terminal_occurrences());
}

TEST(KSpaceJetReconGraphExecutionPlanAdmission, RejectsScanDescriptorXmlThatExceedsTheTargetEnvelopeBeforeIngress) {
  const auto definition =
    ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-v1-minimal.json"));
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const auto scan = scan_descriptor_with_slice_and_contrast_limits();
  ASSERT_GT(scan.source_xml_bytes(), 1U);
  const auto valid_envelope = reference_target_envelope();
  const auto too_small_envelope = reference_target_envelope(scan.source_xml_bytes() - 1U);
  const auto policy = reference_machine_policy();
  const auto contract = reference_contract();
  const auto digests = test_plan_digests();
  const auto binding = ksj::recon::graph::OperatorContractBinding{
    .node_id = "reconstruct",
    .contract_digest = parsed_digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
    .contract = contract,
  };
  const ksj::recon::graph::PlanBuildRequest admissible_request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan,
    .target_envelope = valid_envelope,
    .machine_policy = policy,
    .artifact_digests = digests,
    .operator_contracts = {binding},
  };
  const auto admissible = ksj::recon::graph::ExecutionPlanCompiler::compile(admissible_request);
  ASSERT_TRUE(admissible.ok()) << admissible.status();

  const ksj::recon::graph::PlanBuildRequest rejected_request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan,
    .target_envelope = too_small_envelope,
    .machine_policy = policy,
    .artifact_digests = digests,
    .operator_contracts = {binding},
  };
  const auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(rejected_request);
  ASSERT_FALSE(compiled.ok());
  EXPECT_NE(std::string::npos, compiled.status().message().find("source XML byte length"));

  const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(admissible.value().plan, rejected_request);
  ASSERT_FALSE(verification.ok());
  EXPECT_NE(std::string::npos, verification.status().message().find("source XML byte length"));
}

TEST(KSpaceJetReconGraphExecutionPlanProfiles, RejectsResearchUnboundedInTheCurrentInProcessRuntime) {
  const auto research_document = replace_once(read_fixture("valid/pipeline-v1-minimal.json"), "\"bounded-online\"\n  ]",
                                              "\"bounded-online\",\n    \"research-unbounded\"\n  ]");
  const auto definition = ksj::recon::graph::PipelineDefinition::parse_json(research_document);
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const std::vector profiles{ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online,
                             ksj::recon::ExecutionProfile::research_unbounded};
  const auto contract = reference_contract(0U, profiles);
  const auto policy = reference_machine_policy(4096U, profiles);
  const auto scan = scan_descriptor_with_slice_and_contrast_limits();
  const auto envelope = reference_target_envelope();
  const auto digests = test_plan_digests();
  const auto binding = ksj::recon::graph::OperatorContractBinding{
    .node_id = "reconstruct",
    .contract_digest = parsed_digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
    .contract = contract,
  };
  const ksj::recon::graph::PlanBuildRequest bounded_request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan,
    .target_envelope = envelope,
    .machine_policy = policy,
    .artifact_digests = digests,
    .operator_contracts = {binding},
  };
  const auto bounded = ksj::recon::graph::ExecutionPlanCompiler::compile(bounded_request);
  ASSERT_TRUE(bounded.ok()) << bounded.status();

  auto research_plan_specification = execution_plan_spec_from(bounded.value().plan);
  research_plan_specification.execution_profile = ksj::recon::ExecutionProfile::research_unbounded;
  const auto research_plan =
    ksj::recon::ExecutionPlan::create(bounded.value().plan.digest(), research_plan_specification);
  ASSERT_TRUE(research_plan.ok()) << research_plan.status();

  const ksj::recon::graph::PlanBuildRequest research_request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::research_unbounded,
    .scan_descriptor = scan,
    .target_envelope = envelope,
    .machine_policy = policy,
    .artifact_digests = digests,
    .operator_contracts = {binding},
  };
  const auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(research_request);
  ASSERT_FALSE(compiled.ok());
  EXPECT_NE(std::string::npos, compiled.status().message().find("only offline and bounded-online"));

  const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(research_plan.value(), research_request);
  ASSERT_FALSE(verification.ok());
  EXPECT_NE(std::string::npos, verification.status().message().find("only offline and bounded-online"));
}

TEST(KSpaceJetReconGraphExecutionPlanVerifier,
     RejectsValidlyConstructedPlansWhoseDerivedFieldsOrDetachedDigestAreTampered) {
  const auto definition = parse_frame_pipeline_definition();
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_m3_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const auto scan = scan_descriptor_with_slice_and_contrast_limits();
  const auto envelope = reference_target_envelope();
  const auto policy = reference_machine_policy();
  const ksj::recon::graph::PlanBuildRequest request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan,
    .target_envelope = envelope,
    .machine_policy = policy,
    .artifact_digests = test_plan_digests(),
    .operator_contracts = m3_contract_bindings(reference_contract(
      0U, {ksj::recon::ExecutionProfile::offline, ksj::recon::ExecutionProfile::bounded_online}, {"slice", "contrast"},
      ksj::recon::ReorderSpec{
        .completed_frame_input_port = "acquisition",
        .ordered_output_port = "image",
        .outputs_per_ordinal = 1U,
        .order_projection = {"slice", "contrast"},
        .max_ahead_items = 3U,
        .max_ahead_charged_bytes = 512U,
        .missing_at_end_of_input = ksj::recon::EndOfInputPolicy::fail,
      })),
  };
  const auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(request);
  ASSERT_TRUE(compiled.ok()) << compiled.status();
  const auto original = execution_plan_spec_from(compiled.value().plan);

  {
    auto specification = original;
    auto& table = specification.key_slot_tables.front();
    ++table.max_live_keys;
    ++table.slot_count;
    const auto metadata = ksj::recon::dense_key_slot_host_metadata_charged_bytes(table.key_domain_bound,
                                                                                 table.slot_count, "tampered table");
    ASSERT_TRUE(metadata.ok()) << metadata.status();
    table.host_metadata_charged_bytes = metadata.value();
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("KeySlotTable"));
  }

  {
    auto specification = original;
    specification.reorder_plans.front().order_domain_id = "forged_order_domain";
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_FALSE(tampered.ok());
    EXPECT_NE(std::string::npos, tampered.status().message().find("order_domain_id"));
  }

  {
    auto specification = original;
    specification.reorder_plans.front().completed_frame_input_port = "forged_frame_input";
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("ReorderPlan"));
  }

  {
    auto specification = original;
    specification.reorder_plans.front().ordered_output_port = "forged_output";
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("ReorderPlan"));
  }

  {
    auto specification = original;
    --specification.reorder_plans.front().charged_bytes_per_ordinal;
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("ReorderPlan"));
  }

  {
    auto specification = original;
    auto& reorder = specification.reorder_plans.front();
    const auto table = std::find_if(specification.key_slot_tables.begin(), specification.key_slot_tables.end(),
                                    [&](const ksj::recon::KeySlotTablePlanSpec& candidate) {
                                      return candidate.node_id == reorder.node_id;
                                    });
    ASSERT_NE(table, specification.key_slot_tables.end());
    std::swap(reorder.ordinal_dimensions[0], reorder.ordinal_dimensions[1]);
    std::swap(table->dense_dimensions[0], table->dense_dimensions[1]);
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("KeySlotTable"));
  }

  {
    auto specification = original;
    auto& reorder = specification.reorder_plans.front();
    --reorder.max_ahead_items;
    const auto metadata = ksj::recon::dense_cartesian_reorder_host_metadata_charged_bytes(
      reorder.ordinal_domain_bound, reorder.max_ahead_items, "tampered reorder");
    ASSERT_TRUE(metadata.ok()) << metadata.status();
    reorder.host_metadata_charged_bytes = metadata.value();
    reorder.descriptor_charged_count = reorder.max_ahead_items;
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("ReorderPlan"));
  }

  {
    auto specification = original;
    specification.edge_capacities.push_back({.edge_id = "unplanned_edge", .max_items = 1U, .max_charged_bytes = 1U});
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("edge capacity"));
  }

  {
    auto specification = original;
    ++specification.resource_vector.host_normal_bytes;
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("ResourceVector"));
  }

  {
    auto specification = original;
    ++specification.terminal_occurrences;
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("terminal occurrence"));
  }

  {
    auto specification = original;
    specification.proof_obligations = {
      std::string(ksj::recon::kM3CompletedFrameSlotBindingProofObligation),
      std::string(ksj::recon::kM3StrictDenseAllTuplesEoiRuntimeAssumption),
    };
    const auto tampered = ksj::recon::ExecutionPlan::create(compiled.value().plan.digest(), specification);
    ASSERT_TRUE(tampered.ok()) << tampered.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(tampered.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("proof obligations"));
  }

  {
    const auto forged = ksj::recon::ExecutionPlan::create(
      parsed_digest("sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), original);
    ASSERT_TRUE(forged.ok()) << forged.status();
    const auto verification = ksj::recon::graph::ExecutionPlanVerifier::verify(forged.value(), request);
    ASSERT_FALSE(verification.ok());
    EXPECT_NE(std::string::npos, verification.status().message().find("detached digest"));
  }
}

TEST(KSpaceJetReconGraphExecutionPlanCompiler, RejectsAPlanWhoseDerivedMemoryExceedsMachinePolicy) {
  const auto definition =
    ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-v1-minimal.json"));
  ASSERT_TRUE(definition.ok()) << definition.status();
  const auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), {matching_provider()});
  ASSERT_TRUE(resolved.ok()) << resolved.status();

  const auto scan = scan_descriptor_with_slice_and_contrast_limits();
  const auto envelope = reference_target_envelope();
  const auto policy = reference_machine_policy(128U);
  const ksj::recon::graph::PlanBuildRequest request{
    .resolved_pipeline = resolved.value(),
    .requested_profile = ksj::recon::ExecutionProfile::bounded_online,
    .scan_descriptor = scan,
    .target_envelope = envelope,
    .machine_policy = policy,
    .artifact_digests = test_plan_digests(),
    .operator_contracts = {{.node_id = "reconstruct",
                            .contract_digest =
                              parsed_digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
                            .contract = reference_contract()}},
  };

  const auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(request);
  ASSERT_FALSE(compiled.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, compiled.status().code());
  EXPECT_NE(std::string::npos, compiled.status().message().find("ResourceVector"));
}

} // namespace
