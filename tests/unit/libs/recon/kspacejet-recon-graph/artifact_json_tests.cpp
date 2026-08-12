#include "kspacejet/recon/graph/artifact_json.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] ksj::recon::ArtifactDigest parsed_digest(const std::string_view value) {
  auto digest = ksj::recon::ArtifactDigest::parse(value, "test digest");
  EXPECT_TRUE(digest.ok()) << digest.status();
  return std::move(digest).value();
}

[[nodiscard]] ksj::recon::ResourceVectorSpec representative_resources() {
  return {
    .host_normal_bytes = 4096U,
    .host_pinned_bytes = 17U,
    .host_hugepage_bytes = 0U,
    .shared_host_bytes = 23U,
    .spool_bytes = 0U,
    .transport_bytes = 37U,
    .descriptor_count = 11U,
    .async_token_count = 7U,
    .cpu_leaf_permits = 3U,
    .backend_gang_permits = 0U,
    .provider_private_permits = 2U,
    .io_slots = 1U,
    // ResourceVector validates and sorts this deployment identity set.  The
    // serializer must therefore emit the canonical model order, not this
    // caller-supplied order.
    .devices = {{.device_id = "cuda:1", .device_bytes = 256U, .gpu_stream_slots = 2U, .copy_engine_slots = 1U},
                {.device_id = "cuda:0", .device_bytes = 128U, .gpu_stream_slots = 1U, .copy_engine_slots = 0U}},
  };
}

[[nodiscard]] ksj::recon::ExecutionPlanSpec representative_execution_plan_spec() {
  return {
    .inputs = {.resolved_pipeline = "sha256:1111111111111111111111111111111111111111111111111111111111111111",
               .scan_descriptor = "sha256:2222222222222222222222222222222222222222222222222222222222222222",
               .target_envelope = "sha256:3333333333333333333333333333333333333333333333333333333333333333",
               .machine_policy = "sha256:4444444444444444444444444444444444444444444444444444444444444444",
               .provider_contracts = {"sha256:5555555555555555555555555555555555555555555555555555555555555555"}},
    .execution_profile = ksj::recon::ExecutionProfile::bounded_online,
    .key_slot_tables = {{.node_id = "reconstruct",
                         .dense_dimensions = {{.field = "slice", .minimum = 1U, .cardinality = 2U},
                                              {.field = "contrast", .minimum = 0U, .cardinality = 2U}},
                         .key_domain_bound = 4U,
                         .max_distinct_keys = 4U,
                         .max_live_keys = 4U,
                         .slot_count = 4U,
                         .host_metadata_charged_bytes = 128U,
                         .max_items_per_activation = 2U,
                         .max_charged_bytes_per_activation = 1024U}},
    .reorder_plans = {{.node_id = "reconstruct",
                       .order_domain_id = "reconstruct",
                       .ordinal_binding_id =
                         std::string(ksj::recon::kCompletedFrameSlotContextSemanticKeyOrdinalBindingId),
                       .completed_frame_input_port = "frame",
                       .ordered_output_port = "image",
                       .outputs_per_ordinal = 1U,
                       .charged_bytes_per_ordinal = 128U,
                       .ordinal_dimensions = {{.field = "slice", .minimum = 1U, .cardinality = 2U},
                                              {.field = "contrast", .minimum = 0U, .cardinality = 2U}},
                       .ordinal_domain_bound = 4U,
                       .first_expected_ordinal = 0U,
                       .last_expected_ordinal = 3U,
                       .max_ahead_items = 2U,
                       .max_ahead_charged_bytes = 512U,
                       .max_gap_ordinals = 3U,
                       .occurrence_policy = std::string(ksj::recon::kStrictDenseAllTuplesReorderOccurrencePolicy),
                       .certified_skipped_ordinals = {},
                       .end_of_input_policy = "fail",
                       .handle_storage_charged_bytes = 128U,
                       .host_metadata_charged_bytes = 224U,
                       .descriptor_charged_count = 2U}},
    .edge_capacities = {{.edge_id = "ingress_to_reconstruct", .max_items = 2U, .max_charged_bytes = 2048U}},
    .resource_vector = representative_resources(),
    .terminal_occurrences = 2U,
    .proof_obligations = {"PO-01.typed_ports", "PO-05.resource_vector",
                          std::string(ksj::recon::kM3CompletedFrameSlotBindingProofObligation),
                          std::string(ksj::recon::kM3StrictDenseAllTuplesEoiRuntimeAssumption)},
  };
}

[[nodiscard]] ksj::recon::VerificationRecordSpec representative_verification_record_spec() {
  return {
    .execution_plan_digest = "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
    .execution_profile = ksj::recon::ExecutionProfile::bounded_online,
    .verified_resource_vector = representative_resources(),
    .verified_terminal_occurrences = 2U,
    .verified_obligations = {"M0.profile_attestation", "M0.machine_resource_capacity"},
  };
}

TEST(KSpaceJetReconGraphArtifactJson, SerializesExecutionPlanAsStableDetachedDigestPreimage) {
  const auto specification = representative_execution_plan_spec();
  const auto plan = ksj::recon::ExecutionPlan::create(
    parsed_digest("sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), specification);
  ASSERT_TRUE(plan.ok()) << plan.status();

  const auto first = ksj::recon::graph::serialize_execution_plan_canonical_json(plan.value());
  const auto second = ksj::recon::graph::serialize_execution_plan_canonical_json(plan.value());
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();

  constexpr std::string_view kExpected =
    R"json({"buffer_pool_plans":[],"data_edge_plans":[],"edge_capacities":[{"edge_id":"ingress_to_reconstruct","max_charged_bytes":2048,"max_items":2}],"execution_profile":"bounded-online","input_digests":{"machine_policy":"sha256:4444444444444444444444444444444444444444444444444444444444444444","provider_contracts":["sha256:5555555555555555555555555555555555555555555555555555555555555555"],"resolved_pipeline":"sha256:1111111111111111111111111111111111111111111111111111111111111111","scan_descriptor":"sha256:2222222222222222222222222222222222222222222222222222222222222222","target_envelope":"sha256:3333333333333333333333333333333333333333333333333333333333333333"},"key_slot_tables":[{"dense_dimensions":[{"cardinality":2,"field":"slice","minimum":1},{"cardinality":2,"field":"contrast","minimum":0}],"eviction_policy":"completed-only","generation_policy":"monotonic-u64/v1","host_metadata_charged_bytes":128,"initial_generation":1,"key_domain_bound":4,"late_event_policy":"fail","mapping_algorithm_id":"dense-mixed-radix/v1","max_charged_bytes_per_activation":1024,"max_distinct_keys":4,"max_items_per_activation":2,"max_live_keys":4,"node_id":"reconstruct","seal_on_completion":true,"slot_count":4,"storage_accounting_id":"kspacejet.key-slot-storage/dense-v1"}],"kind":"ExecutionPlan","proof_obligations":["PO-01.typed_ports","PO-05.resource_vector","PO-07.m3_completed_frame_slot_binding","RA-01.m3_strict_dense_all_tuples_eoi"],"reorder_plans":[{"certified_skipped_ordinals":[],"charged_bytes_per_ordinal":128,"completed_frame_input_port":"frame","descriptor_charged_count":2,"end_of_input_policy":"fail","first_expected_ordinal":0,"handle_storage_charged_bytes":128,"host_metadata_charged_bytes":224,"last_expected_ordinal":3,"mapping_algorithm_id":"dense-cartesian-ordinal/v1","max_ahead_charged_bytes":512,"max_ahead_items":2,"max_gap_ordinals":3,"node_id":"reconstruct","occurrence_policy":"strict-dense-all-tuples-eoi-fail","order_domain_id":"reconstruct","ordered_output_port":"image","ordinal_binding_id":"completed-frame-slot-context-semantic-key/v1","ordinal_dimensions":[{"cardinality":2,"field":"slice","minimum":1},{"cardinality":2,"field":"contrast","minimum":0}],"ordinal_domain_bound":4,"outputs_per_ordinal":1,"publish_policy":"next-expected-only","storage_accounting_id":"kspacejet.reorder-storage/dense-cartesian-v1"}],"resource_vector":{"async_token_count":7,"backend_gang_permits":0,"cpu_leaf_permits":3,"descriptor_count":11,"devices":[{"copy_engine_slots":0,"device_bytes":128,"device_id":"cuda:0","gpu_stream_slots":1},{"copy_engine_slots":1,"device_bytes":256,"device_id":"cuda:1","gpu_stream_slots":2}],"host_hugepage_bytes":0,"host_normal_bytes":4096,"host_pinned_bytes":17,"io_slots":1,"provider_private_permits":2,"shared_host_bytes":23,"spool_bytes":0,"transport_bytes":37},"schema_version":"kspacejet.execution-plan/v1","terminal_occurrences":2})json";
  EXPECT_EQ(kExpected, first.value());
  EXPECT_EQ(first.value(), second.value());
  EXPECT_EQ(std::string::npos, first.value().find(plan.value().digest().value()));

  const auto preimage_digest = ksj::recon::graph::domain_separated_sha256_digest(
    "kspacejet:artifact:execution-plan:1", first.value(), "test ExecutionPlan preimage");
  ASSERT_TRUE(preimage_digest.ok()) << preimage_digest.status();
  const auto same_payload_new_identity =
    ksj::recon::ExecutionPlan::create(std::move(preimage_digest).value(), specification);
  ASSERT_TRUE(same_payload_new_identity.ok()) << same_payload_new_identity.status();
  const auto reserialized =
    ksj::recon::graph::serialize_execution_plan_canonical_json(same_payload_new_identity.value());
  ASSERT_TRUE(reserialized.ok()) << reserialized.status();
  EXPECT_EQ(first.value(), reserialized.value());
}

TEST(KSpaceJetReconGraphArtifactJson, SerializesPlanBoundDataEdgeAbiIdentityAndFrozenStaging) {
  const auto descriptor = ksj::recon::TypeDescriptor::create({
    .type_id = "ksj.image-frame",
    .revision = 1U,
    .abi_descriptor_digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    .payload_schema_digest = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    .payload_kind = ksj::recon::PayloadKind::buffer_handle,
    .element_type = ksj::recon::ElementType::float32,
    .rank = 2U,
    .dimensions = {"y", "x"},
    .layout = ksj::recon::LayoutKind::row_major_contiguous,
    .strides = ksj::recon::StrideKind::canonical,
    .explicit_byte_strides = {},
    .allowed_memory_domains = {ksj::recon::TypeMemoryDomain::host_normal},
    .min_alignment_bytes = 64U,
    .mutability = ksj::recon::PayloadMutability::immutable_after_publish,
    .metadata_schema_digest = "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
  });
  ASSERT_TRUE(descriptor.ok()) << descriptor.status();
  auto specification = representative_execution_plan_spec();
  specification.buffer_pool_plans = {{
    .pool_id = "image_edge.pool",
    .producer_node_id = "reconstruct",
    .producer_port_name = "image",
    .producer_provider_id = "org.kspacejet.test-provider",
    .producer_bundle_digest = "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
    .producer_operator_id = "test_reconstruct",
    .producer_contract_digest = "sha256:5555555555555555555555555555555555555555555555555555555555555555",
    .type_descriptor = descriptor.value(),
    .slot_count = 4U,
    .payload_capacity_bytes = 128U,
    .metadata_capacity_bytes = 0U,
    .payload_alignment_bytes = 64U,
    .host_metadata_charged_bytes = 160U,
    .descriptor_charged_count = 4U,
    .physical_charge_bytes = 672U,
  }};
  specification.data_edge_plans = {{
    .edge_id = "image_edge",
    .source_pool_id = "image_edge.pool",
    .producer_node_id = "reconstruct",
    .producer_port_name = "image",
    .producer_abi_port = 0U,
    .consumer_node_id = "image_sink",
    .consumer_port_name = "image",
    .type_descriptor = descriptor.value(),
    .max_items = 4U,
    .max_logical_bytes = 512U,
    .host_metadata_charged_bytes = 384U,
    .descriptor_charged_count = 4U,
    .firing_lease_staging_charged_bytes = ksj::recon::kM37FiringLeaseHostStagingChargedBytes,
    .firing_lease_staging_descriptor_count = ksj::recon::kM37FiringLeaseHostStagingDescriptorCount,
  }};
  specification.resource_vector.host_normal_bytes =
    4096U + 672U + 384U + ksj::recon::kM37FiringLeaseHostStagingChargedBytes;
  specification.resource_vector.descriptor_count =
    11U + 4U + 4U + ksj::recon::kM37FiringLeaseHostStagingDescriptorCount;
  specification.proof_obligations.push_back(std::string(ksj::recon::kM37PlanBoundDataPlaneProofObligation));
  specification.proof_obligations.push_back(std::string(ksj::recon::kM37SinglePhysicalPayloadChargeRuntimeAssumption));
  const auto plan = ksj::recon::ExecutionPlan::create(
    parsed_digest("sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), specification);
  ASSERT_TRUE(plan.ok()) << plan.status();
  const auto canonical = ksj::recon::graph::serialize_execution_plan_canonical_json(plan.value());
  ASSERT_TRUE(canonical.ok()) << canonical.status();
  EXPECT_NE(std::string::npos, canonical.value().find("\"producer_abi_port\":0"));
  EXPECT_NE(std::string::npos, canonical.value().find("\"producer_provider_id\":\"org.kspacejet.test-provider\""));
  EXPECT_NE(std::string::npos,
            canonical.value().find("\"producer_bundle_digest\":\"sha256:dddddddddddddddddddddddddddddddd"));
  EXPECT_NE(std::string::npos, canonical.value().find("\"producer_operator_id\":\"test_reconstruct\""));
  EXPECT_NE(std::string::npos,
            canonical.value().find("\"producer_contract_digest\":\"sha256:555555555555555555555555555555"));
  EXPECT_NE(std::string::npos, canonical.value().find("\"abi_descriptor_digest\":\"sha256:aaaaaaaa"));
  EXPECT_NE(std::string::npos, canonical.value().find("\"firing_lease_staging_charged_bytes\":4096"));
  EXPECT_NE(std::string::npos, canonical.value().find("\"host_metadata_charged_bytes\":384"));
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

  constexpr std::string_view kExpected =
    R"json({"execution_plan_digest":"sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff","execution_profile":"bounded-online","kind":"VerificationRecord","schema_version":"kspacejet.verification-record/v1","verified_obligations":["M0.profile_attestation","M0.machine_resource_capacity"],"verified_resource_vector":{"async_token_count":7,"backend_gang_permits":0,"cpu_leaf_permits":3,"descriptor_count":11,"devices":[{"copy_engine_slots":0,"device_bytes":128,"device_id":"cuda:0","gpu_stream_slots":1},{"copy_engine_slots":1,"device_bytes":256,"device_id":"cuda:1","gpu_stream_slots":2}],"host_hugepage_bytes":0,"host_normal_bytes":4096,"host_pinned_bytes":17,"io_slots":1,"provider_private_permits":2,"shared_host_bytes":23,"spool_bytes":0,"transport_bytes":37},"verified_terminal_occurrences":2})json";
  EXPECT_EQ(kExpected, first.value());
  EXPECT_EQ(first.value(), second.value());
  EXPECT_EQ(std::string::npos, first.value().find(record.value().digest().value()));

  const auto preimage_digest = ksj::recon::graph::domain_separated_sha256_digest(
    "kspacejet:artifact:verification-record:1", first.value(), "test VerificationRecord preimage");
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
