#include "kspacejet/recon/graph/controlled_pipeline_resolver.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ksj::recon::ArtifactDigest;
using ksj::recon::OperatorContract;
using ksj::recon::OperatorContractSpec;
using ksj::recon::PortDirection;
using ksj::recon::PortSpec;
using ksj::recon::graph::ControlledPipelineResolver;
using ksj::recon::graph::ControlledProviderSnapshot;
using ksj::recon::graph::PipelineDefinition;

[[nodiscard]] ArtifactDigest digest(const std::string_view value) {
  auto parsed = ArtifactDigest::parse(value, "controlled Provider bundle digest");
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] OperatorContract contract(std::string operator_id, std::vector<PortSpec> ports) {
  auto created = OperatorContract::create({.operator_id = std::move(operator_id), .ports = std::move(ports)});
  EXPECT_TRUE(created.ok()) << created.status();
  return std::move(created).value();
}

[[nodiscard]] std::string pipeline_document() {
  return R"json(
{
  "kind": "PipelineDefinition",
  "pipeline": {"id": "org.example.cartesian-image", "display_name": "Cartesian image reconstruction"},
  "input_profile": {"kind": "ismrmrd-hdf5", "dataset_group": "dataset"},
  "allowed_profiles": ["offline-reference"],
  "parameters": {},
  "provider_requirements": [
    {"alias": "cartesian", "provider_id": "org.kspacejet.cartesian-recon"},
    {"alias": "coilcombine", "provider_id": "org.kspacejet.coil-combine"}
  ],
  "nodes": [
    {"id": "reconstruct", "operator": {"provider": "cartesian", "id": "cartesian_ifft2_coil_images"}, "config": {}},
    {"id": "combine", "operator": {"provider": "coilcombine", "id": "coil_combine_rss"}, "config": {}}
  ],
  "edges": [
    {"id": "coil_images", "from": {"node": "reconstruct", "port": "coil_images"}, "to": {"node": "combine", "port": "coil_images"}}
  ],
  "bindings": {
    "ingress": [{"id": "kspace", "type": "ksj.kspace-frame", "to": {"node": "reconstruct", "port": "kspace"}}],
    "egress": [{"id": "images", "type": "ksj.image-frame", "from": {"node": "combine", "port": "image"}}],
    "calibration": [],
    "merge": []
  },
  "annotations": {}
}
)json";
}

[[nodiscard]] std::vector<ControlledProviderSnapshot> controlled_snapshot() {
  return {
    {
      .provider_id = "org.kspacejet.cartesian-recon",
      .bundle_digest = digest("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
      .operator_contracts = {contract(
        "cartesian_ifft2_coil_images",
        {{.name = "kspace", .type_ref = "ksj.kspace-frame", .direction = PortDirection::input},
         {.name = "coil_images", .type_ref = "ksj.coil-image-frame", .direction = PortDirection::output}})},
    },
    {
      .provider_id = "org.kspacejet.coil-combine",
      .bundle_digest = digest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
      .operator_contracts = {contract(
        "coil_combine_rss",
        {{.name = "coil_images", .type_ref = "ksj.coil-image-frame", .direction = PortDirection::input},
         {.name = "image", .type_ref = "ksj.image-frame", .direction = PortDirection::output}})},
    },
  };
}

TEST(KSpaceJetReconGraphControlledPipelineResolver, FreezesOnlySelectedExecutableContracts) {
  auto definition = PipelineDefinition::parse_json(pipeline_document());
  ASSERT_TRUE(definition.ok()) << definition.status();
  auto resolver = ControlledPipelineResolver::create(controlled_snapshot());
  ASSERT_TRUE(resolver.ok()) << resolver.status();

  auto first = resolver.value().resolve(definition.value());
  auto second = resolver.value().resolve(definition.value());
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(first.value().pipeline.digest(), second.value().pipeline.digest());
  ASSERT_EQ(first.value().node_contracts.size(), 2U);
  EXPECT_EQ(first.value().node_contracts[0].node_id, "reconstruct");
  EXPECT_EQ(first.value().node_contracts[0].contract.operator_id(), "cartesian_ifft2_coil_images");
  ASSERT_EQ(first.value().pipeline.providers().size(), 2U);
  ASSERT_EQ(first.value().pipeline.providers()[0].operators.size(), 1U);
  EXPECT_EQ(first.value().pipeline.providers()[0].operators[0].contract_digest,
            first.value().node_contracts[0].contract.artifact_digest());
  EXPECT_NE(first.value().pipeline.canonical_json().find("contract_digest"), std::string::npos);
}

TEST(KSpaceJetReconGraphControlledPipelineResolver, RejectsAbsentProviderAndAbsentExecutableContract) {
  auto missing_provider = PipelineDefinition::parse_json(
    std::string(pipeline_document())
      .replace(std::string(pipeline_document()).find("org.kspacejet.coil-combine"),
               std::string("org.kspacejet.coil-combine").size(), "org.example.not-installed"));
  ASSERT_TRUE(missing_provider.ok()) << missing_provider.status();
  auto resolver = ControlledPipelineResolver::create(controlled_snapshot());
  ASSERT_TRUE(resolver.ok()) << resolver.status();
  auto missing_provider_result = resolver.value().resolve(missing_provider.value());
  ASSERT_FALSE(missing_provider_result.ok());
  EXPECT_NE(missing_provider_result.status().message().find("controlled executable snapshot"), std::string::npos);

  auto snapshot = controlled_snapshot();
  snapshot[1].operator_contracts = {
    contract("image_scale", {{.name = "input", .type_ref = "ksj.image-frame", .direction = PortDirection::input},
                             {.name = "output", .type_ref = "ksj.image-frame", .direction = PortDirection::output}})};
  auto missing_contract_resolver = ControlledPipelineResolver::create(std::move(snapshot));
  ASSERT_TRUE(missing_contract_resolver.ok()) << missing_contract_resolver.status();
  auto missing_contract_result =
    missing_contract_resolver.value().resolve(PipelineDefinition::parse_json(pipeline_document()).value());
  ASSERT_FALSE(missing_contract_result.ok());
  EXPECT_NE(missing_contract_result.status().message().find("OperatorContract"), std::string::npos);
}

TEST(KSpaceJetReconGraphControlledPipelineResolver, RejectsPortDirectionAndExactTypeMismatches) {
  auto definition = PipelineDefinition::parse_json(pipeline_document());
  ASSERT_TRUE(definition.ok()) << definition.status();

  auto type_mismatch_snapshot = controlled_snapshot();
  type_mismatch_snapshot[0].operator_contracts = {
    contract("cartesian_ifft2_coil_images",
             {{.name = "kspace", .type_ref = "ksj.kspace-frame", .direction = PortDirection::input},
              {.name = "coil_images", .type_ref = "ksj.image-frame", .direction = PortDirection::output}})};
  auto type_mismatch_resolver = ControlledPipelineResolver::create(std::move(type_mismatch_snapshot));
  ASSERT_TRUE(type_mismatch_resolver.ok()) << type_mismatch_resolver.status();
  auto type_mismatch = type_mismatch_resolver.value().resolve(definition.value());
  ASSERT_FALSE(type_mismatch.ok());
  EXPECT_NE(type_mismatch.status().message().find("incompatible TypeRef"), std::string::npos);

  auto direction_mismatch_snapshot = controlled_snapshot();
  direction_mismatch_snapshot[0].operator_contracts = {
    contract("cartesian_ifft2_coil_images",
             {{.name = "kspace", .type_ref = "ksj.kspace-frame", .direction = PortDirection::output},
              {.name = "coil_images", .type_ref = "ksj.coil-image-frame", .direction = PortDirection::output}})};
  auto direction_mismatch_resolver = ControlledPipelineResolver::create(std::move(direction_mismatch_snapshot));
  ASSERT_TRUE(direction_mismatch_resolver.ok()) << direction_mismatch_resolver.status();
  auto direction_mismatch = direction_mismatch_resolver.value().resolve(definition.value());
  ASSERT_FALSE(direction_mismatch.ok());
  EXPECT_NE(direction_mismatch.status().message().find("opposite direction"), std::string::npos);
}

} // namespace
