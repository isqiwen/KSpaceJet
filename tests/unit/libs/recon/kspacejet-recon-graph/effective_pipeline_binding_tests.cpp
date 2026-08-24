#include "kspacejet/recon/graph/effective_pipeline_binding.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"
#include "kspacejet/recon/scan_facts.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
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
  EXPECT_NE(position, std::string::npos) << "Fixture must contain " << needle;
  if (position == std::string::npos) {
    return document;
  }
  document.replace(position, needle.size(), replacement);
  return document;
}

[[nodiscard]] ksj::recon::ArtifactDigest digest(const std::string_view value) {
  auto parsed = ksj::recon::ArtifactDigest::parse(value, "EffectivePipelineBinding test digest");
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] std::vector<ksj::recon::graph::ResolvedProvider> resolved_providers() {
  using ksj::recon::graph::ResolvedOperator;
  using ksj::recon::graph::ResolvedProvider;
  return {
    ResolvedProvider{
      .alias = "cartesian",
      .provider_id = "org.kspacejet.cartesian-recon",
      .bundle_digest = digest("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
      .operators = {ResolvedOperator{
        .id = "cartesian_ifft2_coil_images",
        .contract_digest = digest("sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")}},
    },
    ResolvedProvider{
      .alias = "coilcombine",
      .provider_id = "org.kspacejet.coil-combine",
      .bundle_digest = digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
      .operators = {ResolvedOperator{
        .id = "coil_combine_rss",
        .contract_digest = digest("sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd")}},
    },
  };
}

[[nodiscard]] ksj::recon::graph::ResolvedPipeline resolved_pipeline() {
  auto definition = ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-minimal.json"));
  EXPECT_TRUE(definition.ok()) << definition.status();
  auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(std::move(definition).value(), resolved_providers());
  EXPECT_TRUE(resolved.ok()) << resolved.status();
  return std::move(resolved).value();
}

[[nodiscard]] ksj::recon::graph::ResolvedPipeline
resolved_pipeline_with_reconstruction_config(const std::string_view canonical_config) {
  std::string document = read_fixture("valid/pipeline-minimal.json");
  constexpr std::string_view kFirstNodeConfig = "\"config\": {}";
  const auto position = document.find(kFirstNodeConfig);
  EXPECT_NE(position, std::string::npos);
  document.replace(position, kFirstNodeConfig.size(), "\"config\": " + std::string(canonical_config));

  auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  EXPECT_TRUE(definition.ok()) << definition.status();
  auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(std::move(definition).value(), resolved_providers());
  EXPECT_TRUE(resolved.ok()) << resolved.status();
  return std::move(resolved).value();
}

[[nodiscard]] std::string_view scan_xml() {
  constexpr std::string_view xml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>8</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>cartesian</trajectory>
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

[[nodiscard]] ksj::recon::ScanFacts scan_facts(const ksj::recon::Quantity acquisition_count = 12U) {
  auto facts = ksj::recon::ScanFacts::create({
    .descriptor = scan_descriptor(),
    .source_xml = std::string(scan_xml()),
    .acquisition_count = acquisition_count,
    .physical_channel_count = 8U,
    .maximum_samples_per_acquisition = 64U,
    .trajectory_dimensions = 0U,
  });
  EXPECT_TRUE(facts.ok()) << facts.status();
  return std::move(facts).value();
}

[[nodiscard]] std::vector<ksj::recon::graph::HostDerivedNodeConfig> node_configs() {
  return {
    {.node_id = "reconstruct", .canonical_config = "{\"columns\":64,\"rows\":64}"},
    {.node_id = "combine", .canonical_config = "{\"method\":\"rss\"}"},
  };
}

TEST(KSpaceJetReconGraphEffectivePipelineBinding, BindsEveryResolvedNodeToCanonicalEffectiveConfiguration) {
  const auto pipeline = resolved_pipeline();
  const auto facts = scan_facts();

  auto binding =
    ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(pipeline, facts, node_configs());
  ASSERT_TRUE(binding.ok()) << binding.status();
  EXPECT_EQ(binding.value().resolved_pipeline_digest(), pipeline.digest());
  EXPECT_EQ(binding.value().scan_facts_digest(), facts.digest());
  ASSERT_EQ(binding.value().node_configs().size(), 2U);
  EXPECT_EQ(binding.value().node_configs()[0].node_id, "combine");
  EXPECT_EQ(binding.value().node_configs()[1].node_id, "reconstruct");
  EXPECT_EQ(binding.value().config_for("reconstruct").value(), "{\"columns\":64,\"rows\":64}");
  EXPECT_FALSE(binding.value().config_for("unknown").ok());
  EXPECT_NE(binding.value().canonical_json().find("\"kind\":\"EffectivePipelineBinding\""), std::string::npos);
  EXPECT_EQ(binding.value().canonical_json().find("canonical_config_digest"), std::string::npos);

  auto reordered_configs = node_configs();
  std::swap(reordered_configs[0], reordered_configs[1]);
  auto reordered = ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(
    pipeline, facts, std::move(reordered_configs));
  ASSERT_TRUE(reordered.ok()) << reordered.status();
  EXPECT_EQ(reordered.value().canonical_json(), binding.value().canonical_json());
  EXPECT_EQ(reordered.value().digest(), binding.value().digest());

  auto different_facts = scan_facts(13U);
  auto different_scan = ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(
    pipeline, different_facts, node_configs());
  ASSERT_TRUE(different_scan.ok()) << different_scan.status();
  EXPECT_NE(different_scan.value().digest(), binding.value().digest());
}

TEST(KSpaceJetReconGraphEffectivePipelineBinding, PreservesStaticAuthoredAlgorithmConfiguration) {
  const auto pipeline = resolved_pipeline_with_reconstruction_config("{\"algorithm\":\"fft\",\"iterations\":4}");
  const auto facts = scan_facts();

  auto matching = node_configs();
  matching[0].canonical_config = "{\"algorithm\":\"fft\",\"columns\":64,\"iterations\":4,\"rows\":64}";
  auto accepted =
    ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(pipeline, facts, matching);
  ASSERT_TRUE(accepted.ok()) << accepted.status();

  auto missing_static = matching;
  missing_static[0].canonical_config = "{\"algorithm\":\"fft\",\"columns\":64,\"rows\":64}";
  auto missing_result = ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(
    pipeline, facts, std::move(missing_static));
  ASSERT_FALSE(missing_result.ok());
  EXPECT_NE(missing_result.status().message().find("preserve"), std::string::npos);

  auto overridden_static = matching;
  overridden_static[0].canonical_config = "{\"algorithm\":\"nufft\",\"columns\":64,\"iterations\":4,\"rows\":64}";
  auto overridden_result = ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(
    pipeline, facts, std::move(overridden_static));
  ASSERT_FALSE(overridden_result.ok());
  EXPECT_NE(overridden_result.status().message().find("preserve"), std::string::npos);
}

TEST(KSpaceJetReconGraphEffectivePipelineBinding, MaterializesOnlyDeclaredScanFactsAfterParameterResolution) {
  auto document = read_fixture("valid/pipeline-minimal.json");
  document = replace_once(
    document, "\"parameters\": {}",
    R"json("parameters": {"algorithm": {"type": "enum", "values": ["fft", "nufft"], "default": "fft"}})json");
  document = replace_once(
    document, "\"config\": {}",
    R"json("config": {"algorithm": {"$param": "algorithm"}}, "scan_fact_bindings": {"channels": {"$scan_fact": "physical_channel_count"}, "columns": {"$scan_fact": "recon_matrix_x", "encoding": 0}, "rows": {"$scan_fact": "recon_matrix_y", "encoding": 0}})json");
  auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_TRUE(definition.ok()) << definition.status();
  auto pipeline = ksj::recon::graph::ResolvedPipeline::resolve(std::move(definition).value(), resolved_providers());
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();
  EXPECT_EQ(pipeline.value().config_for("reconstruct").value(), "{\"algorithm\":\"fft\"}");

  const auto facts = scan_facts();
  auto binding =
    ksj::recon::graph::EffectivePipelineBinding::create_from_declared_scan_fact_bindings(pipeline.value(), facts);
  ASSERT_TRUE(binding.ok()) << binding.status();
  EXPECT_EQ(binding.value().config_for("reconstruct").value(),
            "{\"algorithm\":\"fft\",\"channels\":8,\"columns\":64,\"rows\":64}");
  EXPECT_EQ(binding.value().config_for("combine").value(), "{}");

  auto unavailable_encoding = replace_once(document, "\"encoding\": 0", "\"encoding\": 1");
  auto unavailable_definition = ksj::recon::graph::PipelineDefinition::parse_json(unavailable_encoding);
  ASSERT_TRUE(unavailable_definition.ok()) << unavailable_definition.status();
  auto unavailable_pipeline =
    ksj::recon::graph::ResolvedPipeline::resolve(std::move(unavailable_definition).value(), resolved_providers());
  ASSERT_TRUE(unavailable_pipeline.ok()) << unavailable_pipeline.status();
  auto unavailable_binding = ksj::recon::graph::EffectivePipelineBinding::create_from_declared_scan_fact_bindings(
    unavailable_pipeline.value(), facts);
  ASSERT_FALSE(unavailable_binding.ok());
  EXPECT_NE(unavailable_binding.status().message().find("unavailable ISMRMRD encoding 1"), std::string::npos);
}

TEST(KSpaceJetReconGraphEffectivePipelineBinding, ParsesOnlyTheSuppliedParentArtifactBinding) {
  const auto pipeline = resolved_pipeline();
  const auto facts = scan_facts();
  auto binding =
    ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(pipeline, facts, node_configs());
  ASSERT_TRUE(binding.ok()) << binding.status();

  auto parsed =
    ksj::recon::graph::EffectivePipelineBinding::parse_json(binding.value().canonical_json(), pipeline, facts);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed.value().digest(), binding.value().digest());

  auto with_schema = binding.value().canonical_json();
  with_schema.insert(1U, "\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",");
  auto parsed_with_schema = ksj::recon::graph::EffectivePipelineBinding::parse_json(with_schema, pipeline, facts);
  ASSERT_TRUE(parsed_with_schema.ok()) << parsed_with_schema.status();
  EXPECT_EQ(parsed_with_schema.value().canonical_json(), binding.value().canonical_json());

  auto fixture = read_fixture("valid/effective-pipeline-binding-minimal.json");
  fixture = replace_once(fixture, "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                         pipeline.digest().value());
  fixture = replace_once(fixture, "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                         facts.digest().value());
  auto parsed_fixture = ksj::recon::graph::EffectivePipelineBinding::parse_json(fixture, pipeline, facts);
  ASSERT_TRUE(parsed_fixture.ok()) << parsed_fixture.status();
  EXPECT_EQ(parsed_fixture.value().canonical_json(), binding.value().canonical_json());

  auto loader_fixture = read_fixture("invalid/effective-pipeline-binding-loader-field.json");
  loader_fixture =
    replace_once(loader_fixture, "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                 pipeline.digest().value());
  loader_fixture = replace_once(
    loader_fixture, "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", facts.digest().value());
  auto parsed_loader_fixture = ksj::recon::graph::EffectivePipelineBinding::parse_json(loader_fixture, pipeline, facts);
  ASSERT_FALSE(parsed_loader_fixture.ok());
  EXPECT_NE(parsed_loader_fixture.status().message().find("module"), std::string::npos);

  auto substituted = binding.value().canonical_json();
  const auto scan_digest_position = substituted.find(facts.digest().value());
  ASSERT_NE(scan_digest_position, std::string::npos);
  substituted.replace(scan_digest_position, facts.digest().value().size(),
                      "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
  auto substituted_result = ksj::recon::graph::EffectivePipelineBinding::parse_json(substituted, pipeline, facts);
  ASSERT_FALSE(substituted_result.ok());
  EXPECT_NE(substituted_result.status().message().find("scan_facts_digest"), std::string::npos);
}

TEST(KSpaceJetReconGraphEffectivePipelineBinding, RejectsNonCanonicalOrNonExactNodeConfigurationSets) {
  const auto pipeline = resolved_pipeline();
  const auto facts = scan_facts();

  auto noncanonical = node_configs();
  noncanonical[0].canonical_config = "{ \"columns\":64,\"rows\":64 }";
  auto noncanonical_result = ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(
    pipeline, facts, std::move(noncanonical));
  ASSERT_FALSE(noncanonical_result.ok());
  EXPECT_NE(noncanonical_result.status().message().find("canonical JSON"), std::string::npos);

  auto missing = node_configs();
  missing.pop_back();
  auto missing_result =
    ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(pipeline, facts, std::move(missing));
  ASSERT_FALSE(missing_result.ok());
  EXPECT_NE(missing_result.status().message().find("missing"), std::string::npos);

  auto duplicate = node_configs();
  duplicate.push_back({.node_id = "combine", .canonical_config = "{}"});
  auto duplicate_result = ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(
    pipeline, facts, std::move(duplicate));
  ASSERT_FALSE(duplicate_result.ok());
  EXPECT_NE(duplicate_result.status().message().find("duplicate"), std::string::npos);

  auto unknown = node_configs();
  unknown.push_back({.node_id = "unexpected", .canonical_config = "{}"});
  auto unknown_result =
    ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(pipeline, facts, std::move(unknown));
  ASSERT_FALSE(unknown_result.ok());
  EXPECT_NE(unknown_result.status().message().find("unknown"), std::string::npos);

  auto forbidden_loader = node_configs();
  forbidden_loader[0].canonical_config = "{\"command\":\"untrusted-provider.exe\"}";
  auto forbidden_loader_result = ksj::recon::graph::EffectivePipelineBinding::create_from_host_derived_configs(
    pipeline, facts, std::move(forbidden_loader));
  ASSERT_FALSE(forbidden_loader_result.ok());
  EXPECT_NE(forbidden_loader_result.status().message().find("command"), std::string::npos);
}

} // namespace
