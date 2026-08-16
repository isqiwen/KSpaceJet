#include "kspacejet/recon/graph/artifact_json.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr ksj::recon::Quantity kPayloadCapacityBytes = 4096U;
constexpr ksj::recon::Quantity kPoolControlBytes = 40U;
constexpr ksj::recon::Quantity kDataEdgeControlBytes = 128U;

[[nodiscard]] ksj::recon::ArtifactDigest parsed_digest(const std::string_view value) {
  auto digest = ksj::recon::ArtifactDigest::parse(value, "test digest");
  EXPECT_TRUE(digest.ok()) << digest.status();
  return std::move(digest).value();
}

[[nodiscard]] ksj::recon::ResourceVectorSpec resources(const ksj::recon::Quantity host_normal_bytes,
                                                       const ksj::recon::Quantity descriptor_count,
                                                       const ksj::recon::Quantity cpu_permits = 1U) {
  return {
    .host_normal_bytes = host_normal_bytes,
    .host_pinned_bytes = 0U,
    .host_hugepage_bytes = 0U,
    .shared_host_bytes = 0U,
    .spool_bytes = 0U,
    .transport_bytes = 0U,
    .descriptor_count = descriptor_count,
    .async_token_count = 0U,
    .cpu_leaf_permits = cpu_permits,
    .backend_gang_permits = 0U,
    .provider_private_permits = 0U,
    .io_slots = 0U,
    .devices = {},
  };
}

[[nodiscard]] ksj::recon::ExecutionPlanSpec representative_execution_plan_spec() {
  using namespace ksj::recon;

  auto image = types::image_frame();
  EXPECT_TRUE(image.ok()) << image.status();
  const auto pool_physical_bytes = kPayloadCapacityBytes + kPoolControlBytes;

  ExecutionPlanSpec specification{
    .inputs = {.resolved_pipeline = "sha256:1111111111111111111111111111111111111111111111111111111111111111",
               .scan_descriptor = "sha256:2222222222222222222222222222222222222222222222222222222222222222",
               .target_envelope = "sha256:3333333333333333333333333333333333333333333333333333333333333333",
               .machine_policy = "sha256:4444444444444444444444444444444444444444444444444444444444444444"},
    .operator_plan_bindings = {{
      .node_id = "process",
      .canonical_config_digest = "sha256:6666666666666666666666666666666666666666666666666666666666666666",
    }},
    .execution_profile = ExecutionProfile::bounded_online,
    .synchronous_node_plans = {{
      .node_id = "process",
      .provider_id = "org.example.image",
      .provider_bundle_digest = "sha256:7777777777777777777777777777777777777777777777777777777777777777",
      .operator_id = "image_process",
      .dynamic_input_join_policy = SynchronousDynamicInputJoinPolicy::exact_item_identity,
      .inputs = {{
        .port_name = "image",
        .abi_port = 0U,
        .source_kind = SynchronousInputSourceKind::data_edge,
        .source_id = "ingress-to-process",
        .type_descriptor = image.value(),
        .maximum_item_count = 1U,
      }},
      .outputs = {{
        .port_name = "image",
        .abi_port = 0U,
        .destination_kind = SynchronousOutputDestinationKind::data_edge,
        .destination_id = "process-to-images",
        .pool_id = "pool:process:image",
        .type_descriptor = image.value(),
        .maximum_item_count = 1U,
      }},
      .firing =
        {
          .maximum_input_batches = 1U,
          .maximum_input_items = 1U,
          .maximum_output_grants = 1U,
          .maximum_input_payload_bytes = kPayloadCapacityBytes,
          .maximum_scratch_bytes = 0U,
          .maximum_metadata_bytes = 64U,
          .staging_charged_bytes = 256U,
          .staging_descriptor_count = 1U,
          .firing_reservation = resources(0U, 0U),
        },
      .terminal =
        {
          .normal_max_output_items = 0U,
          .normal_max_output_charged_bytes = 0U,
          .normal_max_async_tokens = 0U,
          .cancel_max_async_tokens = 0U,
        },
    }},
    .synchronous_buffer_pool_plans =
      {
        {
          .pool_id = "pool:ingress:input",
          .owner_kind = SynchronousDataEndpointKind::ingress,
          .owner_id = "input",
          .owner_port_name = "",
          .type_descriptor = image.value(),
          .memory_domain = TypeMemoryDomain::host_normal,
          .slot_count = 1U,
          .payload_capacity_bytes = kPayloadCapacityBytes,
          .metadata_capacity_bytes = 0U,
          .payload_alignment_bytes = image.value().min_alignment_bytes(),
          .storage_accounting_id = "kspacejet.buffer-pool-storage/host-normal",
          .host_metadata_charged_bytes = kPoolControlBytes,
          .descriptor_charged_count = 1U,
          .physical_charge_bytes = pool_physical_bytes,
        },
        {
          .pool_id = "pool:process:image",
          .owner_kind = SynchronousDataEndpointKind::node,
          .owner_id = "process",
          .owner_port_name = "image",
          .type_descriptor = image.value(),
          .memory_domain = TypeMemoryDomain::host_normal,
          .slot_count = 1U,
          .payload_capacity_bytes = kPayloadCapacityBytes,
          .metadata_capacity_bytes = 0U,
          .payload_alignment_bytes = image.value().min_alignment_bytes(),
          .storage_accounting_id = "kspacejet.buffer-pool-storage/host-normal",
          .host_metadata_charged_bytes = kPoolControlBytes,
          .descriptor_charged_count = 1U,
          .physical_charge_bytes = pool_physical_bytes,
        },
      },
    .synchronous_data_edge_plans =
      {
        {
          .edge_id = "ingress-to-process",
          .source_pool_id = "pool:ingress:input",
          .producer_kind = SynchronousDataEndpointKind::ingress,
          .producer_id = "input",
          .producer_port_name = "",
          .producer_abi_port = 0U,
          .consumer_kind = SynchronousDataEndpointKind::node,
          .consumer_id = "process",
          .consumer_port_name = "image",
          .consumer_abi_port = 0U,
          .type_descriptor = image.value(),
          .max_items = 1U,
          .max_logical_bytes = kPayloadCapacityBytes,
          .storage_accounting_id = "kspacejet.data-edge-storage/fixed-fifo",
          .host_metadata_charged_bytes = kDataEdgeControlBytes,
          .descriptor_charged_count = 1U,
          .terminal_policy = "normal-eoi-drain-cancellation-fail",
        },
        {
          .edge_id = "process-to-images",
          .source_pool_id = "pool:process:image",
          .producer_kind = SynchronousDataEndpointKind::node,
          .producer_id = "process",
          .producer_port_name = "image",
          .producer_abi_port = 0U,
          .consumer_kind = SynchronousDataEndpointKind::egress,
          .consumer_id = "images",
          .consumer_port_name = "",
          .consumer_abi_port = 0U,
          .type_descriptor = image.value(),
          .max_items = 1U,
          .max_logical_bytes = kPayloadCapacityBytes,
          .storage_accounting_id = "kspacejet.data-edge-storage/fixed-fifo",
          .host_metadata_charged_bytes = kDataEdgeControlBytes,
          .descriptor_charged_count = 1U,
          .terminal_policy = "normal-eoi-drain-cancellation-fail",
        },
      },
    .calibration_artifact_binding_plans = {},
    .resource_vector = resources(2U * pool_physical_bytes + 2U * kDataEdgeControlBytes + 256U, 5U),
    .terminal_occurrences = 1U,
    .proof_obligations = {"PO-01.generic-synchronous-graph"},
  };
  return specification;
}

[[nodiscard]] ksj::recon::VerificationRecordSpec representative_verification_record_spec() {
  return {
    .execution_plan_digest = "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
    .execution_profile = ksj::recon::ExecutionProfile::bounded_online,
    .verified_resource_vector = resources(8192U, 8U),
    .verified_terminal_occurrences = 1U,
    .verified_obligations = {"M0.profile_attestation", "M0.machine_resource_capacity"},
  };
}

TEST(KSpaceJetReconGraphArtifactJson, SerializesGenericSynchronousPlanAsStableDetachedDigestPreimage) {
  const auto specification = representative_execution_plan_spec();
  const auto plan = ksj::recon::ExecutionPlan::create(
    parsed_digest("sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"), specification);
  ASSERT_TRUE(plan.ok()) << plan.status();

  const auto first = ksj::recon::graph::serialize_execution_plan_canonical_json(plan.value());
  const auto second = ksj::recon::graph::serialize_execution_plan_canonical_json(plan.value());
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(first.value(), second.value());
  EXPECT_EQ(std::string::npos, first.value().find(plan.value().digest().value()));
  EXPECT_NE(std::string::npos, first.value().find("\"synchronous_nodes\""));
  EXPECT_NE(std::string::npos, first.value().find("\"synchronous_buffer_pools\""));
  EXPECT_NE(std::string::npos, first.value().find("\"synchronous_data_edges\""));
  EXPECT_NE(std::string::npos, first.value().find("\"calibration_artifact_bindings\":[]"));
  EXPECT_EQ(std::string::npos, first.value().find("operator_contract_digest"));
  EXPECT_EQ(std::string::npos, first.value().find("operator_contract_digests"));

  const auto preimage_digest = ksj::recon::graph::domain_separated_sha256_digest(
    "kspacejet:artifact:execution-plan", first.value(), "test ExecutionPlan preimage");
  ASSERT_TRUE(preimage_digest.ok()) << preimage_digest.status();
  const auto same_payload_new_identity =
    ksj::recon::ExecutionPlan::create(std::move(preimage_digest).value(), specification);
  ASSERT_TRUE(same_payload_new_identity.ok()) << same_payload_new_identity.status();
  const auto reserialized =
    ksj::recon::graph::serialize_execution_plan_canonical_json(same_payload_new_identity.value());
  ASSERT_TRUE(reserialized.ok()) << reserialized.status();
  EXPECT_EQ(first.value(), reserialized.value());
}

TEST(KSpaceJetReconGraphArtifactJson, SerializesVerificationRecordAsStableDetachedDigestPreimage) {
  const auto specification = representative_verification_record_spec();
  const auto record = ksj::recon::VerificationRecord::create(
    parsed_digest("sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"), specification);
  ASSERT_TRUE(record.ok()) << record.status();

  const auto first = ksj::recon::graph::serialize_verification_record_canonical_json(record.value());
  const auto second = ksj::recon::graph::serialize_verification_record_canonical_json(record.value());
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(first.value(), second.value());
  EXPECT_EQ(std::string::npos, first.value().find(record.value().digest().value()));

  const auto preimage_digest = ksj::recon::graph::domain_separated_sha256_digest(
    "kspacejet:artifact:verification-record", first.value(), "test VerificationRecord preimage");
  ASSERT_TRUE(preimage_digest.ok()) << preimage_digest.status();
  const auto same_payload_new_identity =
    ksj::recon::VerificationRecord::create(std::move(preimage_digest).value(), specification);
  ASSERT_TRUE(same_payload_new_identity.ok()) << same_payload_new_identity.status();
  const auto reserialized =
    ksj::recon::graph::serialize_verification_record_canonical_json(same_payload_new_identity.value());
  ASSERT_TRUE(reserialized.ok()) << reserialized.status();
  EXPECT_EQ(first.value(), reserialized.value());
}

} // namespace
