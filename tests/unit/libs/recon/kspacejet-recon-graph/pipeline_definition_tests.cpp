#include "kspacejet/recon/graph/pipeline_definition.hpp"
#include "kspacejet/recon/type_registry.hpp"

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
  EXPECT_NE(std::string::npos, position) << "Fixture did not contain expected text: " << needle;
  if (position == std::string::npos) {
    return {};
  }
  document.replace(position, needle.size(), replacement);
  return document;
}

[[nodiscard]] ksj::recon::ArtifactDigest digest(const std::string_view value) {
  auto parsed = ksj::recon::ArtifactDigest::parse(value, "test digest");
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] std::vector<ksj::recon::graph::ResolvedProvider> resolved_providers() {
  using ksj::recon::graph::ResolvedOperator;
  using ksj::recon::graph::ResolvedProvider;
  return {
    ResolvedProvider{
      .alias = "coilcombine",
      .provider_id = "org.kspacejet.coil-combine",
      .bundle_digest = digest("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
      .operators = {ResolvedOperator{.id = "coil_combine_rss"}},
    },
    ResolvedProvider{
      .alias = "cartesian",
      .provider_id = "org.kspacejet.cartesian-recon",
      .bundle_digest = digest("sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
      .operators = {ResolvedOperator{.id = "cartesian_ifft2_coil_images"}},
    },
  };
}

[[nodiscard]] std::string calibration_pipeline() {
  return R"json(
{
  "kind": "PipelineDefinition",
  "pipeline": {"id": "org.example.calibrated-reconstruction", "display_name": "Calibrated reconstruction"},
  "allowed_profiles": ["offline-reference", "bounded-reconstruction-graph"],
  "parameters": {},
  "provider_requirements": [
    {"alias": "calibration", "provider_id": "org.example.calibration"},
    {"alias": "recon", "provider_id": "org.example.recon"}
  ],
  "nodes": [
    {"id": "noise_estimate", "operator": {"provider": "calibration", "id": "noise_model_estimate"}, "config": {}},
    {"id": "reconstruct", "operator": {"provider": "recon", "id": "reconstruct"}, "config": {}}
  ],
  "edges": [],
  "bindings": {
    "ingress": [
      {"id": "noise", "type": "ksj.noise-calibration-frame", "to": {"node": "noise_estimate", "port": "noise_calibration"}},
      {"id": "kspace", "type": "ksj.kspace-frame", "to": {"node": "reconstruct", "port": "kspace"}}
    ],
    "egress": [
      {"id": "images", "type": "ksj.image-frame", "from": {"node": "reconstruct", "port": "image"}}
    ],
    "calibration": [
      {"id": "noise-model", "producer": {"node": "noise_estimate", "port": "noise_model"}, "consumers": [{"node": "reconstruct", "port": "noise_model"}]}
    ],
    "merge": []
  },
  "annotations": {}
}
)json";
}

TEST(KSpaceJetReconGraphPipelineDefinition, ParsesTypedCartesianImageChainFixture) {
  auto parsed = ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-minimal.json"));
  ASSERT_TRUE(parsed.ok()) << parsed.status();

  EXPECT_EQ(parsed.value().id(), "org.example.cartesian-image");
  ASSERT_EQ(parsed.value().nodes().size(), 2U);
  ASSERT_EQ(parsed.value().edges().size(), 1U);
  ASSERT_EQ(parsed.value().ingress_ports().size(), 1U);
  ASSERT_EQ(parsed.value().egress_ports().size(), 1U);
  EXPECT_EQ(parsed.value().ingress_ports().front().type, ksj::recon::types::kKspaceFrameTypeRef);
  EXPECT_EQ(parsed.value().egress_ports().front().type, ksj::recon::types::kImageFrameTypeRef);
  EXPECT_EQ(parsed.value().edges().front().from.node, "reconstruct");
  EXPECT_EQ(parsed.value().edges().front().to.node, "combine");

  auto reparsed = ksj::recon::graph::PipelineDefinition::parse_json(parsed.value().canonical_json());
  ASSERT_TRUE(reparsed.ok()) << reparsed.status();
  EXPECT_EQ(reparsed.value().artifact_digest(), parsed.value().artifact_digest());
  EXPECT_EQ(reparsed.value().semantic_digest(), parsed.value().semantic_digest());
}

TEST(KSpaceJetReconGraphPipelineDefinition, AcceptsRegisteredFrameIngressAndRejectsUnknownTypeRefs) {
  const auto document = read_fixture("valid/pipeline-minimal.json");
  auto registered = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_TRUE(registered.ok()) << registered.status();

  auto unknown = ksj::recon::graph::PipelineDefinition::parse_json(
    replace_once(document, "ksj.kspace-frame", "ksj.not-a-registered-type"));
  ASSERT_FALSE(unknown.ok());
  EXPECT_NE(unknown.status().message().find("checked-in TypeRef"), std::string::npos);
}

TEST(KSpaceJetReconGraphPipelineDefinition, UsesExplicitCalibrationProducerAndConsumerPorts) {
  auto parsed = ksj::recon::graph::PipelineDefinition::parse_json(calibration_pipeline());
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_EQ(parsed.value().calibration_bindings().size(), 1U);
  const auto& binding = parsed.value().calibration_bindings().front();
  EXPECT_EQ(binding.id, "noise-model");
  EXPECT_EQ(binding.producer.node, "noise_estimate");
  EXPECT_EQ(binding.producer.port, "noise_model");
  ASSERT_EQ(binding.consumers.size(), 1U);
  EXPECT_EQ(binding.consumers.front().node, "reconstruct");
  EXPECT_EQ(binding.consumers.front().port, "noise_model");
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsCyclesCreatedByCalibrationDependencies) {
  const auto cycle =
    replace_once(calibration_pipeline(), "\"producer\": {\"node\": \"noise_estimate\", \"port\": \"noise_model\"}",
                 "\"producer\": {\"node\": \"reconstruct\", \"port\": \"noise_model\"}");
  auto parsed = ksj::recon::graph::PipelineDefinition::parse_json(cycle);
  ASSERT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("cycle"), std::string::npos);
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsDuplicateCalibrationConsumerEndpoint) {
  const auto duplicate =
    replace_once(calibration_pipeline(), "\"consumers\": [{\"node\": \"reconstruct\", \"port\": \"noise_model\"}]",
                 "\"consumers\": [{\"node\": \"reconstruct\", \"port\": \"noise_model\"}, {\"node\": \"reconstruct\", "
                 "\"port\": \"noise_model\"}]");
  auto parsed = ksj::recon::graph::PipelineDefinition::parse_json(duplicate);
  ASSERT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("duplicated"), std::string::npos);
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsDuplicateJsonKeysBeforeDomMaterialization) {
  constexpr std::string_view duplicate_key = R"json(
{"kind":"PipelineDefinition","kind":"PipelineDefinition"}
)json";
  auto parsed = ksj::recon::graph::PipelineDefinition::parse_json(duplicate_key);
  ASSERT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("duplicate"), std::string::npos);
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsAuthoredRuntimeSizingFields) {
  auto parsed = ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("invalid/pipeline-runtime-field.json"));
  ASSERT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("runtime field"), std::string::npos);
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsDeletedInterfaceDigestRequirement) {
  const auto document =
    replace_once(read_fixture("valid/pipeline-minimal.json"), "\"id\": \"cartesian_ifft2_coil_images\"",
                 "\"id\": \"cartesian_ifft2_coil_images\",\n        "
                 "\"requires_interface_digest\": \"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"");
  auto parsed = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("unknown field"), std::string::npos);
}

TEST(KSpaceJetReconGraphPipelineDefinition, RejectsNonemptyDeferredMergeBinding) {
  const auto document =
    replace_once(read_fixture("valid/pipeline-minimal.json"), "\"merge\": []", "\"merge\": [{\"id\": \"merge\"}]");
  auto parsed = ksj::recon::graph::PipelineDefinition::parse_json(document);
  ASSERT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("deferred"), std::string::npos);
}

TEST(KSpaceJetReconGraphPipelineDefinition, CanonicalizesUnorderedProfileLists) {
  const auto original_document = read_fixture("valid/pipeline-minimal.json");
  const auto reordered_document =
    replace_once(original_document, "\"offline-reference\",\n    \"bounded-reconstruction-graph\"",
                 "\"bounded-reconstruction-graph\",\n    \"offline-reference\"");
  auto original = ksj::recon::graph::PipelineDefinition::parse_json(original_document);
  auto reordered = ksj::recon::graph::PipelineDefinition::parse_json(reordered_document);
  ASSERT_TRUE(original.ok()) << original.status();
  ASSERT_TRUE(reordered.ok()) << reordered.status();
  EXPECT_EQ(original.value().canonical_json(), reordered.value().canonical_json());
  EXPECT_EQ(original.value().artifact_digest(), reordered.value().artifact_digest());
}

TEST(KSpaceJetReconGraphResolvedPipeline, FreezesExactCurrentProviderAndOperatorIdentities) {
  auto definition = ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-minimal.json"));
  ASSERT_TRUE(definition.ok()) << definition.status();

  auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), resolved_providers());
  ASSERT_TRUE(resolved.ok()) << resolved.status();
  ASSERT_EQ(resolved.value().providers().size(), 2U);
  EXPECT_EQ(resolved.value().providers().front().alias, "cartesian");
  EXPECT_EQ(resolved.value().providers().back().alias, "coilcombine");
  EXPECT_NE(resolved.value().canonical_json().find("cartesian_ifft2_coil_images"), std::string::npos);
  EXPECT_NE(resolved.value().canonical_json().find("coil_combine_rss"), std::string::npos);
  EXPECT_EQ(resolved.value().canonical_json().find("contract_digest"), std::string::npos);
}

TEST(KSpaceJetReconGraphResolvedPipeline, RejectsProviderIdentityMismatch) {
  auto definition = ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("valid/pipeline-minimal.json"));
  ASSERT_TRUE(definition.ok()) << definition.status();
  auto providers = resolved_providers();
  providers.front().provider_id = "org.example.wrong-provider";

  auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), std::move(providers));
  ASSERT_FALSE(resolved.ok());
  EXPECT_NE(resolved.status().message().find("does not match"), std::string::npos);
}

TEST(KSpaceJetReconGraphResolvedPipeline, RejectsSchemaShapedSemanticProviderMismatchFixture) {
  // The fixture follows pipeline.schema.json: provider_id is only a qualified
  // identifier structurally. Its absence from the resolved Provider bundle is
  // instead a semantic resolution error, so schema-shaped input cannot bypass
  // the resolver's exact identity check.
  auto definition =
    ksj::recon::graph::PipelineDefinition::parse_json(read_fixture("invalid/pipeline-semantic-provider-mismatch.json"));
  ASSERT_TRUE(definition.ok()) << definition.status();

  auto resolved = ksj::recon::graph::ResolvedPipeline::resolve(definition.value(), resolved_providers());
  ASSERT_FALSE(resolved.ok());
  EXPECT_NE(resolved.status().message().find("does not match"), std::string::npos);
}

} // namespace
