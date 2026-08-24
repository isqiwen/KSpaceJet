#include "kspacejet/recon/graph/artifact_json.hpp"
#include "kspacejet/recon/graph/execution_plan_compiler.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] ksj::recon::ArtifactDigest digest(const std::string_view value) {
  auto parsed = ksj::recon::ArtifactDigest::parse(value, "test digest");
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] std::string read_fixture(const std::string_view relative_path) {
  const auto path = std::filesystem::path(KSJ_RECON_FIXTURE_DIR) / relative_path;
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << "Unable to open fixture: " << path;
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string_view scan_xml() {
  constexpr std::string_view xml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>8</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>radial</trajectory>
    <encodedSpace><matrixSize><x>64</x><y>64</y><z>1</z></matrixSize><fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm></encodedSpace>
    <reconSpace><matrixSize><x>64</x><y>64</y><z>1</z></matrixSize><fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm></reconSpace>
    <encodingLimits>
      <average><minimum>0</minimum><maximum>0</maximum><center>0</center></average>
      <slice><minimum>0</minimum><maximum>0</maximum><center>0</center></slice>
      <contrast><minimum>0</minimum><maximum>0</maximum><center>0</center></contrast>
      <phase><minimum>0</minimum><maximum>0</maximum><center>0</center></phase>
      <repetition><minimum>0</minimum><maximum>0</maximum><center>0</center></repetition>
      <set><minimum>0</minimum><maximum>0</maximum><center>0</center></set>
      <segment><minimum>0</minimum><maximum>0</maximum><center>0</center></segment>
    </encodingLimits>
  </encoding>
</ismrmrdHeader>
)xml";
  return xml;
}

[[nodiscard]] ksj::recon::ScanDescriptor scan_descriptor() {
  auto parsed = ksj::recon::ScanDescriptor::parse_ismrmrd_xml(scan_xml());
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] ksj::recon::ScanFacts scan_facts() {
  auto facts = ksj::recon::ScanFacts::create({.descriptor = scan_descriptor(),
                                              .source_xml = std::string(scan_xml()),
                                              .acquisition_count = 64U,
                                              .physical_channel_count = 8U,
                                              .maximum_samples_per_acquisition = 64U,
                                              .trajectory_dimensions = 2U});
  EXPECT_TRUE(facts.ok()) << facts.status();
  return std::move(facts).value();
}

[[nodiscard]] ksj::recon::TargetEnvelope target_envelope() {
  auto result = ksj::recon::TargetEnvelope::create({
    .max_xml_bytes = 16U * 1024U,
    .max_frame_charged_bytes = 4096U,
    .max_image_charged_bytes = 4096U,
    .max_decoder_staging_bytes = 512U,
    .max_samples_per_acquisition = 512U,
    .max_trajectory_dimensions = 3U,
    .max_active_channels = 8U,
    .max_channel_groups = 1U,
    .max_dynamic_keys_per_scan = 8U,
    .max_active_scans = 1U,
    .calibration_horizon_items = 8U,
    .calibration_horizon_charged_bytes = 4096U,
    .arrival_envelope = {.max_acquisitions_per_second = 100U, .max_burst_acquisitions = 4U},
    .sink_service_assumption = {.minimum_drain_items_per_second = 100U,
                                .max_pause_us = 0U,
                                .slow_sink_policy = ksj::recon::SlowSinkPolicy::fail,
                                .transport_staging_bytes = 64U},
  });
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

[[nodiscard]] ksj::recon::MachinePolicy machine_policy() {
  auto result = ksj::recon::MachinePolicy::create({
    .resource_capacity = {.domains = {.host_normal_bytes = 1U << 20U,
                                      .host_pinned_bytes = 0U,
                                      .host_hugepage_bytes = 0U,
                                      .shared_host_bytes = 0U,
                                      .spool_bytes = 0U,
                                      .transport_bytes = 0U,
                                      .descriptor_count = 4096U,
                                      .async_token_count = 0U,
                                      .cpu_leaf_permits = 8U,
                                      .backend_gang_permits = 0U,
                                      .provider_private_permits = 0U,
                                      .io_slots = 0U},
                          .host_total_cap_bytes = 1U << 20U},
    .numa_domain_count = 1U,
    .allowed_memory_domains = {ksj::recon::MemoryDomain::host},
    .allowed_profiles = {ksj::recon::ExecutionProfile::bounded_reconstruction_graph},
    .scheduler_policy = ksj::recon::SchedulerPolicy::fifo,
  });
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

[[nodiscard]] ksj::recon::OperatorContract noise_contract();
[[nodiscard]] ksj::recon::OperatorContract reconstruct_contract();

[[nodiscard]] ksj::recon::graph::ResolvedProvider provider() {
  const auto noise = noise_contract();
  const auto reconstruct = reconstruct_contract();
  return {
    .alias = "recon",
    .provider_id = "org.example.recon",
    .bundle_digest = digest("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
    .operators = {{.id = "noise_model_estimate", .contract_digest = noise.artifact_digest()},
                  {.id = "noncartesian_reconstruct", .contract_digest = reconstruct.artifact_digest()}},
  };
}

[[nodiscard]] ksj::recon::OperatorContract noise_contract() {
  auto result = ksj::recon::OperatorContract::create({
    .operator_id = "noise_model_estimate",
    .ports = {{.name = "noise_calibration",
               .type_ref = std::string(ksj::recon::types::kNoiseCalibrationFrameTypeRef),
               .direction = ksj::recon::PortDirection::input},
              {.name = "noise_model",
               .type_ref = std::string(ksj::recon::types::kNoiseModelTypeRef),
               .direction = ksj::recon::PortDirection::output}},
  });
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

[[nodiscard]] ksj::recon::OperatorContract reconstruct_contract() {
  auto result = ksj::recon::OperatorContract::create({
    .operator_id = "noncartesian_reconstruct",
    .ports = {{.name = "kspace",
               .type_ref = std::string(ksj::recon::types::kKspaceFrameTypeRef),
               .direction = ksj::recon::PortDirection::input},
              {.name = "trajectory",
               .type_ref = std::string(ksj::recon::types::kTrajectoryFrameTypeRef),
               .direction = ksj::recon::PortDirection::input},
              {.name = "noise_model",
               .type_ref = std::string(ksj::recon::types::kNoiseModelTypeRef),
               .direction = ksj::recon::PortDirection::input},
              {.name = "image",
               .type_ref = std::string(ksj::recon::types::kImageFrameTypeRef),
               .direction = ksj::recon::PortDirection::output}},
  });
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

[[nodiscard]] ksj::recon::NodePlanningRequirements noise_requirements(const ksj::recon::OperatorContract& contract) {
  using namespace ksj::recon;
  auto result = NodePlanningRequirements::create(
    {.execution = {.input_granularity = InputGranularity::frame,
                   .partition_key = {},
                   .max_active_keys = 1U,
                   .max_in_flight = 1U,
                   .max_items_per_activation = 1U},
     .batch = {.min_items = 1U, .preferred_items = 1U, .max_items = 1U, .max_charged_bytes = 4096U},
     .rates = {.kind = RateKind::keyed_dynamic,
               .ordinary = {.outputs = {{.port_name = "noise_model", .items = 1U, .charged_bytes = 64U}}},
               .normal_flush = {.max_firings = 0U, .outputs = {}}},
     .resources = {.scratch_charged_bytes_per_firing = 32U,
                   .per_key_state_charged_bytes = 0U,
                   .per_scan_workspace_charged_bytes = 16U,
                   .retention_charged_bytes = 0U,
                   .output_items = 1U,
                   .output_charged_bytes = 64U,
                   .cpu_permits = 1U,
                   .memory_domain = MemoryDomain::host},
     .terminal = {.normal_max_output_items = 0U,
                  .normal_max_output_charged_bytes = 0U,
                  .normal_max_async_tokens = 0U,
                  .cancel_max_async_tokens = 0U}},
    contract);
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

[[nodiscard]] ksj::recon::NodePlanningRequirements
reconstruct_requirements(const ksj::recon::OperatorContract& contract) {
  using namespace ksj::recon;
  auto result = NodePlanningRequirements::create(
    {.execution = {.input_granularity = InputGranularity::frame,
                   .partition_key = {},
                   .max_active_keys = 1U,
                   .max_in_flight = 1U,
                   .max_items_per_activation = 3U},
     .batch = {.min_items = 3U, .preferred_items = 3U, .max_items = 3U, .max_charged_bytes = 1344U},
     .rates = {.kind = RateKind::sdf,
               .static_phases = {{.inputs = {{.port_name = "kspace", .items = 1U, .charged_bytes = 1024U},
                                             {.port_name = "trajectory", .items = 1U, .charged_bytes = 256U},
                                             {.port_name = "noise_model", .items = 1U, .charged_bytes = 64U}},
                                  .outputs = {{.port_name = "image", .items = 1U, .charged_bytes = 512U}}}}},
     .resources = {.scratch_charged_bytes_per_firing = 128U,
                   .per_key_state_charged_bytes = 0U,
                   .per_scan_workspace_charged_bytes = 32U,
                   .retention_charged_bytes = 0U,
                   .output_items = 1U,
                   .output_charged_bytes = 512U,
                   .cpu_permits = 1U,
                   .memory_domain = MemoryDomain::host},
     .terminal = {.normal_max_output_items = 0U,
                  .normal_max_output_charged_bytes = 0U,
                  .normal_max_async_tokens = 0U,
                  .cancel_max_async_tokens = 0U}},
    contract);
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

[[nodiscard]] ksj::recon::NodePlanningRequirements
reconstruct_keyed_dynamic_requirements(const ksj::recon::OperatorContract& contract) {
  using namespace ksj::recon;
  auto result = NodePlanningRequirements::create(
    {.execution = {.input_granularity = InputGranularity::frame,
                   .partition_key = {},
                   .max_active_keys = 1U,
                   .max_in_flight = 1U,
                   .max_items_per_activation = 3U},
     .batch = {.min_items = 3U, .preferred_items = 3U, .max_items = 3U, .max_charged_bytes = 1344U},
     .rates = {.kind = RateKind::keyed_dynamic,
               .ordinary = {.outputs = {{.port_name = "image", .items = 1U, .charged_bytes = 512U}}},
               .normal_flush = {.max_firings = 0U, .outputs = {}}},
     .resources = {.scratch_charged_bytes_per_firing = 128U,
                   .per_key_state_charged_bytes = 0U,
                   .per_scan_workspace_charged_bytes = 32U,
                   .retention_charged_bytes = 0U,
                   .output_items = 1U,
                   .output_charged_bytes = 512U,
                   .cpu_permits = 1U,
                   .memory_domain = MemoryDomain::host},
     .terminal = {.normal_max_output_items = 0U,
                  .normal_max_output_charged_bytes = 0U,
                  .normal_max_async_tokens = 0U,
                  .cancel_max_async_tokens = 0U}},
    contract);
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

struct TestInputs final {
  ksj::recon::graph::ResolvedPipeline resolved_pipeline;
  ksj::recon::ScanFacts scan_facts;
  ksj::recon::graph::EffectivePipelineBinding effective_pipeline_binding;
  ksj::recon::TargetEnvelope envelope;
  ksj::recon::MachinePolicy policy;
  std::vector<ksj::recon::graph::OperatorContractBinding> contracts;
  std::vector<ksj::recon::NodePlanningRequirementsBinding> requirements;
};

[[nodiscard]] TestInputs test_inputs(const std::string_view pipeline_document = {}) {
  constexpr std::string_view default_pipeline_json = R"json(
{
  "kind":"PipelineDefinition",
  "pipeline":{"id":"org.example.noncartesian","display_name":"Non-Cartesian join"},
  "input_profile":{"kind":"ismrmrd-hdf5","dataset_group":"dataset"},
  "allowed_profiles":["bounded-reconstruction-graph"],
  "parameters":{},
  "provider_requirements":[{"alias":"recon","provider_id":"org.example.recon"}],
  "nodes":[
    {"id":"noise_estimate","operator":{"provider":"recon","id":"noise_model_estimate"},"config":{}},
    {"id":"reconstruct","operator":{"provider":"recon","id":"noncartesian_reconstruct"},"config":{}}
  ],
  "edges":[],
  "bindings":{
    "ingress":[
      {"id":"noise","type":"ksj.noise-calibration-frame","to":{"node":"noise_estimate","port":"noise_calibration"}},
      {"id":"kspace","type":"ksj.kspace-frame","to":{"node":"reconstruct","port":"kspace"}},
      {"id":"trajectory","type":"ksj.trajectory-frame","to":{"node":"reconstruct","port":"trajectory"}}
    ],
    "egress":[{"id":"images","type":"ksj.image-frame","from":{"node":"reconstruct","port":"image"}}],
    "calibration":[{"id":"noise-model","producer":{"node":"noise_estimate","port":"noise_model"},"consumers":[{"node":"reconstruct","port":"noise_model"}]}],
    "merge":[]
  },
  "annotations":{}
}
)json";
  const auto document = pipeline_document.empty() ? default_pipeline_json : pipeline_document;
  auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  EXPECT_TRUE(definition.ok()) << definition.status();
  auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(std::move(definition).value(), {provider()});
  EXPECT_TRUE(resolved.ok()) << resolved.status();
  auto resolved_pipeline = std::move(resolved).value();
  auto facts = scan_facts();
  std::vector<ksj::recon::graph::HostDerivedNodeConfig> effective_configs;
  effective_configs.reserve(resolved_pipeline.definition().nodes().size());
  for (const auto& node : resolved_pipeline.definition().nodes()) {
    effective_configs.push_back({.node_id = node.id, .canonical_config = node.canonical_config});
  }
  auto effective_pipeline_binding = ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(
    resolved_pipeline, facts, std::move(effective_configs));
  EXPECT_TRUE(effective_pipeline_binding.ok()) << effective_pipeline_binding.status();
  const auto noise = noise_contract();
  const auto reconstruct = reconstruct_contract();
  return {.resolved_pipeline = std::move(resolved_pipeline),
          .scan_facts = std::move(facts),
          .effective_pipeline_binding = std::move(effective_pipeline_binding).value(),
          .envelope = target_envelope(),
          .policy = machine_policy(),
          .contracts = {{.node_id = "noise_estimate", .contract = noise},
                        {.node_id = "reconstruct", .contract = reconstruct}},
          .requirements = {{.node_id = "noise_estimate", .requirements = noise_requirements(noise)},
                           {.node_id = "reconstruct", .requirements = reconstruct_requirements(reconstruct)}}};
}

[[nodiscard]] ksj::recon::graph::PlanBuildRequest request_for(const TestInputs& inputs) {
  return {.resolved_pipeline = inputs.resolved_pipeline,
          .requested_profile = ksj::recon::ExecutionProfile::bounded_reconstruction_graph,
          .scan_facts = inputs.scan_facts,
          .effective_pipeline_binding = inputs.effective_pipeline_binding,
          .target_envelope = inputs.envelope,
          .machine_policy = inputs.policy,
          .operator_contract_bindings = inputs.contracts,
          .node_planning_requirements = inputs.requirements};
}

TEST(SynchronousGraphPlan, CompilesAndVerifiesTwoDynamicInputsWithStaticCalibration) {
  const auto inputs = test_inputs();
  const auto build_request = request_for(inputs);
  auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(build_request);
  ASSERT_TRUE(compiled.ok()) << compiled.status();
  const auto reconstruct = std::find_if(compiled.value().plan.synchronous_node_plans().begin(),
                                        compiled.value().plan.synchronous_node_plans().end(), [](const auto& node) {
                                          return node.node_id() == "reconstruct";
                                        });
  ASSERT_NE(reconstruct, compiled.value().plan.synchronous_node_plans().end());
  EXPECT_EQ(reconstruct->firing().maximum_input_batches(), 3U);
  EXPECT_EQ(reconstruct->firing().maximum_input_items(), 3U);
  EXPECT_EQ(reconstruct->firing().maximum_output_grants(), 1U);
  EXPECT_EQ(std::count_if(reconstruct->inputs().begin(), reconstruct->inputs().end(),
                          [](const auto& input) {
                            return input.source_kind() == ksj::recon::SynchronousInputSourceKind::data_edge;
                          }),
            2);
  auto verified = ksj::recon::graph::ExecutionPlanVerifier::verify(compiled.value().plan, build_request);
  ASSERT_TRUE(verified.ok()) << verified.status();
  EXPECT_EQ(compiled.value().plan.inputs().scan_facts(), inputs.scan_facts.digest());
  EXPECT_EQ(compiled.value().plan.inputs().effective_pipeline_binding(), inputs.effective_pipeline_binding.digest());
  auto target_digest = ksj::recon::derive_target_envelope_artifact_digest(inputs.envelope);
  auto machine_digest = ksj::recon::derive_machine_policy_artifact_digest(inputs.policy);
  ASSERT_TRUE(target_digest.ok()) << target_digest.status();
  ASSERT_TRUE(machine_digest.ok()) << machine_digest.status();
  EXPECT_EQ(compiled.value().plan.inputs().target_envelope(), target_digest.value());
  EXPECT_EQ(compiled.value().plan.inputs().machine_policy(), machine_digest.value());
}

TEST(SynchronousGraphPlan, UsesArtifactProducerCapacityForKeyedDynamicStaticCalibration) {
  auto inputs = test_inputs();
  const auto contract = reconstruct_contract();
  for (auto& binding : inputs.requirements) {
    if (binding.node_id == "reconstruct") {
      binding.requirements = reconstruct_keyed_dynamic_requirements(contract);
    }
  }

  const auto build_request = request_for(inputs);
  auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(build_request);
  ASSERT_TRUE(compiled.ok()) << compiled.status();

  const auto reconstruct = std::find_if(compiled.value().plan.synchronous_node_plans().begin(),
                                        compiled.value().plan.synchronous_node_plans().end(), [](const auto& node) {
                                          return node.node_id() == "reconstruct";
                                        });
  ASSERT_NE(reconstruct, compiled.value().plan.synchronous_node_plans().end());
  const auto noise_model =
    std::find_if(reconstruct->inputs().begin(), reconstruct->inputs().end(), [](const auto& input) {
      return input.port_name() == "noise_model";
    });
  ASSERT_NE(noise_model, reconstruct->inputs().end());
  EXPECT_EQ(noise_model->source_kind(), ksj::recon::SynchronousInputSourceKind::calibration_artifact);
  EXPECT_EQ(noise_model->source_id(), "noise-model");

  const auto producer_pool =
    std::find_if(compiled.value().plan.synchronous_buffer_pool_plans().begin(),
                 compiled.value().plan.synchronous_buffer_pool_plans().end(), [](const auto& pool) {
                   return pool.pool_id() == "pool:node:noise_estimate:noise_model";
                 });
  ASSERT_NE(producer_pool, compiled.value().plan.synchronous_buffer_pool_plans().end());
  EXPECT_EQ(producer_pool->payload_capacity_bytes(), 64U);
  EXPECT_EQ(reconstruct->firing().maximum_input_payload_bytes(), 4096U + 4096U + 64U);

  // Verification independently reaches the same source-kind capacity result;
  // it must not substitute TargetEnvelope.max_frame_charged_bytes for the
  // static noise-model artifact.
  auto verified = ksj::recon::graph::ExecutionPlanVerifier::verify(compiled.value().plan, build_request);
  ASSERT_TRUE(verified.ok()) << verified.status();
}

TEST(SynchronousGraphPlan, CanonicalArtifactUsesOnlyGenericSynchronousPlanSections) {
  const auto inputs = test_inputs();
  const auto build_request = request_for(inputs);
  auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(build_request);
  ASSERT_TRUE(compiled.ok()) << compiled.status();

  auto serialized = ksj::recon::graph::serialize_execution_plan_canonical_json(compiled.value().plan);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  const auto& json = serialized.value();
  EXPECT_NE(json.find("\"synchronous_nodes\""), std::string::npos);
  EXPECT_NE(json.find("\"synchronous_buffer_pools\""), std::string::npos);
  EXPECT_NE(json.find("\"synchronous_data_edges\""), std::string::npos);
  EXPECT_NE(json.find("\"calibration_artifact_bindings\""), std::string::npos);
}

TEST(SynchronousGraphPlan, RejectsAnUnboundDeclaredInputInsteadOfTreatingItAsOptional) {
  auto inputs = test_inputs();
  // The parser and compiler operate on immutable PipelineDefinition values;
  // use the explicit topology test above for the positive case.  This test
  // exercises the compiler's declared-input closure rule by removing the matching
  // contract binding port through a fresh invalid request below.
  auto contract = reconstruct_contract();
  auto modified = ksj::recon::OperatorContract::create({
    .operator_id = "noncartesian_reconstruct",
    .ports = {{.name = "kspace",
               .type_ref = std::string(ksj::recon::types::kKspaceFrameTypeRef),
               .direction = ksj::recon::PortDirection::input},
              {.name = "trajectory",
               .type_ref = std::string(ksj::recon::types::kTrajectoryFrameTypeRef),
               .direction = ksj::recon::PortDirection::input},
              {.name = "noise_model",
               .type_ref = std::string(ksj::recon::types::kNoiseModelTypeRef),
               .direction = ksj::recon::PortDirection::input},
              {.name = "extra",
               .type_ref = std::string(ksj::recon::types::kControlMessageTypeRef),
               .direction = ksj::recon::PortDirection::input},
              {.name = "image",
               .type_ref = std::string(ksj::recon::types::kImageFrameTypeRef),
               .direction = ksj::recon::PortDirection::output}},
  });
  ASSERT_TRUE(modified.ok()) << modified.status();
  for (auto& binding : inputs.contracts) {
    if (binding.node_id == "reconstruct")
      binding.contract = std::move(modified).value();
  }
  const auto build_request = request_for(inputs);
  auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(build_request);
  EXPECT_FALSE(compiled.ok());
}

TEST(SynchronousGraphPlan, RejectsSchemaValidPipelineWithUnboundContractPortFixture) {
  // pipeline.schema.json deliberately cannot know a Provider's typed ports.
  // Parsing and resolution therefore succeed, while the compiler must reject
  // the fixture's ingress endpoint because no resolved contract owns it.
  auto inputs = test_inputs(read_fixture("invalid/pipeline-semantic-unbound-contract-port.json"));
  const auto build_request = request_for(inputs);
  auto compiled = ksj::recon::graph::ExecutionPlanCompiler::compile(build_request);
  ASSERT_FALSE(compiled.ok());
  EXPECT_NE(compiled.status().message().find("Ingress 'unbound'"), std::string::npos);
}

} // namespace
