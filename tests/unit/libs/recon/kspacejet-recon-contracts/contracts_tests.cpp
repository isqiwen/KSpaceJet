#include "kspacejet/recon/contracts.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr auto kPayloadDigest = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr auto kMetadataDigest = "sha256:cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34";

[[nodiscard]] ksj::recon::TypeDescriptor message_type(const std::string_view type_id) {
  auto descriptor = ksj::recon::TypeDescriptor::create({
    .type_id = std::string(type_id),
    .revision = 1U,
    .abi_descriptor_digest = kPayloadDigest,
    .payload_schema_digest = kPayloadDigest,
    .payload_kind = ksj::recon::PayloadKind::message_handle,
    .element_type = ksj::recon::ElementType::none,
    .rank = 0U,
    .dimensions = {},
    .layout = ksj::recon::LayoutKind::opaque,
    .strides = ksj::recon::StrideKind::canonical,
    .explicit_byte_strides = {},
    .allowed_memory_domains = {ksj::recon::TypeMemoryDomain::host_normal},
    .min_alignment_bytes = 8U,
    .mutability = ksj::recon::PayloadMutability::immutable_after_publish,
    .metadata_schema_digest = kMetadataDigest,
  });
  EXPECT_TRUE(descriptor.ok()) << descriptor.status();
  return std::move(descriptor).value();
}

[[nodiscard]] ksj::recon::TypeDescriptor completed_frame_type() {
  auto descriptor = ksj::recon::completed_frame_slot_context_type();
  EXPECT_TRUE(descriptor.ok()) << descriptor.status();
  return std::move(descriptor).value();
}

[[nodiscard]] ksj::recon::TypeDescriptor spoofed_completed_frame_type() {
  auto descriptor = ksj::recon::TypeDescriptor::create({
    .type_id = std::string(ksj::recon::kCompletedFrameSlotContextFrameTypeId),
    .revision = 1U,
    .abi_descriptor_digest = std::string(ksj::recon::kCompletedFrameSlotContextAbiDescriptorDigest),
    .payload_schema_digest = std::string(ksj::recon::kCompletedFrameSlotContextPayloadSchemaDigest),
    .payload_kind = ksj::recon::PayloadKind::buffer_handle,
    .element_type = ksj::recon::ElementType::complex_int16,
    .rank = 3U,
    .dimensions = {"channel", "ky", "kx"},
    .layout = ksj::recon::LayoutKind::channel_major_contiguous,
    .strides = ksj::recon::StrideKind::canonical,
    .explicit_byte_strides = {},
    .allowed_memory_domains = {ksj::recon::TypeMemoryDomain::host_normal},
    .min_alignment_bytes = 64U,
    .mutability = ksj::recon::PayloadMutability::immutable_after_publish,
    .metadata_schema_digest = kMetadataDigest,
  });
  EXPECT_TRUE(descriptor.ok()) << descriptor.status();
  return std::move(descriptor).value();
}

[[nodiscard]] ksj::recon::TypeDescriptor immutable_image_buffer_type() {
  auto descriptor = ksj::recon::TypeDescriptor::create({
    .type_id = "ksj.image-frame",
    .revision = 1U,
    .abi_descriptor_digest = kPayloadDigest,
    .payload_schema_digest = kPayloadDigest,
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
    .metadata_schema_digest = kMetadataDigest,
  });
  EXPECT_TRUE(descriptor.ok()) << descriptor.status();
  return std::move(descriptor).value();
}

[[nodiscard]] ksj::recon::ArtifactDigest artifact_digest(const std::string_view field_name = "test digest") {
  auto digest = ksj::recon::ArtifactDigest::parse(kPayloadDigest, field_name);
  EXPECT_TRUE(digest.ok()) << digest.status();
  return std::move(digest).value();
}

[[nodiscard]] ksj::recon::ExecutionPlanSpec valid_execution_plan_spec() {
  return {
    .inputs = {.resolved_pipeline = kPayloadDigest,
               .scan_descriptor = kPayloadDigest,
               .target_envelope = kPayloadDigest,
               .machine_policy = kPayloadDigest,
               .provider_contracts = {kPayloadDigest}},
    .execution_profile = ksj::recon::ExecutionProfile::offline,
    .key_slot_tables = {{.node_id = "node",
                         .dense_dimensions = {},
                         .key_domain_bound = 1U,
                         .max_distinct_keys = 1U,
                         .max_live_keys = 1U,
                         .slot_count = 1U,
                         .host_metadata_charged_bytes = 32U,
                         .max_items_per_activation = 1U,
                         .max_charged_bytes_per_activation = 1U}},
    .resource_vector = {.host_normal_bytes = 32U},
    .terminal_occurrences = 1U,
    .proof_obligations = {"PO-01.typed_ports"},
  };
}

[[nodiscard]] ksj::recon::OperatorContractSpec bounded_output_contract() {
  using namespace ksj::recon;
  return {
    .operator_id = "bounded_output",
    .operator_revision = "1.0.0",
    .provider_abi_major = 1U,
    .supported_profiles = {ExecutionProfile::offline},
    .ports =
      {
        {.name = "input",
         .type_descriptor = message_type("ismrmrd.acquisition"),
         .direction = PortDirection::input,
         .cardinality = PortCardinality::many,
         .required = true},
        {.name = "output",
         .type_descriptor = message_type("ismrmrd.image"),
         .direction = PortDirection::output,
         .cardinality = PortCardinality::many,
         .required = false},
      },
    .execution = {.input_granularity = InputGranularity::acquisition,
                  .partition_key = {},
                  .order_domain = OrderDomain::strict_global,
                  .max_active_keys = 1U,
                  .max_in_flight = 1U,
                  .call_model = CallModel::serial,
                  .max_items_per_activation = 1U,
                  .cooperative_quantum_us = 100U},
    .batch = {.min_items = 1U, .preferred_items = 1U, .max_items = 1U, .max_charged_bytes = 64U, .max_wait_us = 0U},
    .rates = {.kind = RateKind::sdf,
              .static_phases = {{
                .inputs = {{.port_name = "input", .items = 1U, .charged_bytes = 64U}},
                .outputs = {{.port_name = "output", .items = 2U, .charged_bytes = 128U}},
              }}},
    .resources = {.output_items = 2U, .output_charged_bytes = 128U, .cpu_permits = 1U},
  };
}

TEST(KSpaceJetReconContractsArtifactDigest, AcceptsOnlyCanonicalLowerCaseSha256Digests) {
  constexpr auto valid = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

  const auto parsed = ksj::recon::ArtifactDigest::parse(valid, "test digest");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(valid, parsed.value().value());

  const auto upper_case = ksj::recon::ArtifactDigest::parse(
    "sha256:BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", "test digest");
  EXPECT_FALSE(upper_case.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, upper_case.status().code());

  const auto wrong_length = ksj::recon::ArtifactDigest::parse("sha256:1234", "test digest");
  EXPECT_FALSE(wrong_length.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, wrong_length.status().code());
}

TEST(KSpaceJetReconContractsOperatorContract, RequiresOutputReservationsAndRejectsCancelCleanupDataOutput) {
  using namespace ksj::recon;

  EXPECT_TRUE(OperatorContract::create(bounded_output_contract()).ok());

  auto insufficient_items = bounded_output_contract();
  insufficient_items.resources.output_items = 1U;
  EXPECT_FALSE(OperatorContract::create(insufficient_items).ok());

  auto insufficient_bytes = bounded_output_contract();
  insufficient_bytes.resources.output_charged_bytes = 127U;
  EXPECT_FALSE(OperatorContract::create(insufficient_bytes).ok());

  auto dynamic_cancel_output = bounded_output_contract();
  dynamic_cancel_output.rates = {
    .kind = RateKind::keyed_dynamic,
    .completion = {.kind = CompletionKind::end_of_input, .on_end_of_input = EndOfInputPolicy::fail},
    .ordinary = {.max_firings = 1U, .outputs = {{.port_name = "output", .items = 2U, .charged_bytes = 128U}}},
    .cancel_cleanup = {.max_firings = 1U, .outputs = {{.port_name = "output", .items = 1U, .charged_bytes = 1U}}},
  };
  EXPECT_FALSE(OperatorContract::create(dynamic_cancel_output).ok());
}

TEST(KSpaceJetReconContractsOperatorContract, RequiresNormalTerminalBundleToCoverDynamicFlush) {
  using namespace ksj::recon;

  auto specification = bounded_output_contract();
  specification.rates = {
    .kind = RateKind::keyed_dynamic,
    .completion = {.kind = CompletionKind::end_of_input, .on_end_of_input = EndOfInputPolicy::fail},
    .ordinary = {.max_firings = 1U, .outputs = {{.port_name = "output", .items = 2U, .charged_bytes = 128U}}},
    .normal_flush = {.max_firings = 2U, .outputs = {{.port_name = "output", .items = 1U, .charged_bytes = 64U}}},
  };
  specification.terminal.normal = TerminalBehavior::flush_declared;
  specification.terminal.normal_max_output_items = 1U;
  specification.terminal.normal_max_output_charged_bytes = 64U;
  EXPECT_FALSE(OperatorContract::create(specification).ok());

  specification.terminal.normal_max_output_items = 2U;
  specification.terminal.normal_max_output_charged_bytes = 128U;
  EXPECT_TRUE(OperatorContract::create(specification).ok());
}

TEST(KSpaceJetReconContractsOperatorContract, FreezesTheM3DenseCartesianReorderDeclaration) {
  using namespace ksj::recon;

  auto specification = bounded_output_contract();
  specification.ports.front().type_descriptor = completed_frame_type();
  specification.execution.input_granularity = InputGranularity::frame;
  specification.execution.partition_key = {"slice"};
  specification.execution.max_items_per_activation = 1U;
  specification.batch = {
    .min_items = 1U, .preferred_items = 1U, .max_items = 1U, .max_charged_bytes = 64U, .max_wait_us = 0U};
  specification.rates.static_phases.front().outputs.front().items = 1U;
  specification.reorder = ReorderSpec{
    .completed_frame_input_port = "input",
    .ordered_output_port = "output",
    .outputs_per_ordinal = 1U,
    .order_projection = {"slice"},
    .max_ahead_items = 2U,
    .max_ahead_charged_bytes = 256U,
    .missing_at_end_of_input = EndOfInputPolicy::fail,
  };
  EXPECT_TRUE(OperatorContract::create(specification).ok());

  auto acquisition_granularity = specification;
  acquisition_granularity.execution.input_granularity = InputGranularity::acquisition;
  EXPECT_FALSE(OperatorContract::create(acquisition_granularity).ok());

  auto multi_frame_activation = specification;
  multi_frame_activation.execution.max_items_per_activation = 2U;
  multi_frame_activation.batch.preferred_items = 2U;
  multi_frame_activation.batch.max_items = 2U;
  EXPECT_FALSE(OperatorContract::create(multi_frame_activation).ok());

  auto non_frame_input_type = specification;
  non_frame_input_type.ports.front().type_descriptor = message_type("ismrmrd.acquisition");
  EXPECT_FALSE(OperatorContract::create(non_frame_input_type).ok());

  auto spoofed_frame_input_type = specification;
  spoofed_frame_input_type.ports.front().type_descriptor = spoofed_completed_frame_type();
  EXPECT_FALSE(OperatorContract::create(spoofed_frame_input_type).ok());

  auto missing_frame_input_port = specification;
  missing_frame_input_port.reorder->completed_frame_input_port = "unknown";
  EXPECT_FALSE(OperatorContract::create(missing_frame_input_port).ok());

  auto missing_output_port = specification;
  missing_output_port.reorder->ordered_output_port.clear();
  EXPECT_FALSE(OperatorContract::create(missing_output_port).ok());

  auto unknown_output_port = specification;
  unknown_output_port.reorder->ordered_output_port = "unknown";
  EXPECT_FALSE(OperatorContract::create(unknown_output_port).ok());

  auto multiple_outputs_per_ordinal = specification;
  multiple_outputs_per_ordinal.reorder->outputs_per_ordinal = 2U;
  EXPECT_FALSE(OperatorContract::create(multiple_outputs_per_ordinal).ok());

  auto insufficient_ahead_bytes = specification;
  --insufficient_ahead_bytes.reorder->max_ahead_charged_bytes;
  EXPECT_FALSE(OperatorContract::create(insufficient_ahead_bytes).ok());

  auto empty_projection = specification;
  empty_projection.reorder->order_projection.clear();
  EXPECT_FALSE(OperatorContract::create(empty_projection).ok());

  auto duplicate_projection = specification;
  duplicate_projection.reorder->order_projection = {"slice", "slice"};
  EXPECT_FALSE(OperatorContract::create(duplicate_projection).ok());

  auto mismatched_partition_identity = specification;
  mismatched_partition_identity.execution.partition_key = {"contrast"};
  EXPECT_FALSE(OperatorContract::create(mismatched_partition_identity).ok());

  auto channel_group_partition = specification;
  channel_group_partition.execution.partition_key = {"channel_group"};
  channel_group_partition.execution.channel_group = ChannelGroupSpec{
    .channels_per_group = 1U,
    .max_active_channels = 1U,
    .max_groups = 1U,
    .max_charged_bytes_per_group = 1U,
  };
  EXPECT_FALSE(OperatorContract::create(channel_group_partition).ok());

  auto skipped_at_end_of_input = specification;
  skipped_at_end_of_input.reorder->missing_at_end_of_input = EndOfInputPolicy::skip;
  EXPECT_FALSE(OperatorContract::create(skipped_at_end_of_input).ok());

  auto partial_at_end_of_input = specification;
  partial_at_end_of_input.reorder->missing_at_end_of_input = EndOfInputPolicy::partial_output;
  EXPECT_FALSE(OperatorContract::create(partial_at_end_of_input).ok());
}

TEST(KSpaceJetReconContractsOperatorContract, ValidatesStaticPhaseAggregateInputBatchAndActivationFeasibility) {
  using namespace ksj::recon;

  auto specification = bounded_output_contract();
  specification.ports.push_back({.name = "auxiliary",
                                 .type_descriptor = message_type("ismrmrd.acquisition"),
                                 .direction = PortDirection::input,
                                 .cardinality = PortCardinality::many,
                                 .required = true});
  specification.rates.static_phases.front().inputs = {
    {.port_name = "input", .items = 1U, .charged_bytes = 64U},
    {.port_name = "auxiliary", .items = 1U, .charged_bytes = 64U},
  };
  EXPECT_FALSE(OperatorContract::create(specification).ok());

  specification.batch = {
    .min_items = 1U, .preferred_items = 2U, .max_items = 2U, .max_charged_bytes = 128U, .max_wait_us = 0U};
  specification.execution.max_items_per_activation = 2U;
  EXPECT_TRUE(OperatorContract::create(specification).ok());

  specification.batch.max_charged_bytes = 127U;
  EXPECT_FALSE(OperatorContract::create(specification).ok());
}

TEST(KSpaceJetReconContractsExecutionProfile, InProcessRuntimeSupportsOnlyOfflineAndBoundedOnline) {
  using ksj::recon::ExecutionProfile;

  EXPECT_TRUE(ksj::recon::is_currently_supported_in_process(ExecutionProfile::offline));
  EXPECT_TRUE(ksj::recon::is_currently_supported_in_process(ExecutionProfile::bounded_online));
  EXPECT_FALSE(ksj::recon::is_currently_supported_in_process(ExecutionProfile::isolated_strict_online));
  EXPECT_FALSE(ksj::recon::is_currently_supported_in_process(ExecutionProfile::deadline_qualified_online));
  EXPECT_FALSE(ksj::recon::is_currently_supported_in_process(ExecutionProfile::research_unbounded));
}

TEST(KSpaceJetReconContractsArtifactConstructors, EnforceNonEmptyPlansAndFiniteRecordFields) {
  using namespace ksj::recon;

  auto plan_specification = valid_execution_plan_spec();
  EXPECT_TRUE(ExecutionPlan::create(artifact_digest(), plan_specification).ok());

  plan_specification.key_slot_tables.clear();
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), plan_specification).ok());

  plan_specification = valid_execution_plan_spec();
  plan_specification.terminal_occurrences = 0U;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), plan_specification).ok());

  plan_specification = valid_execution_plan_spec();
  plan_specification.proof_obligations.clear();
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), plan_specification).ok());

  plan_specification = valid_execution_plan_spec();
  plan_specification.proof_obligations.push_back("PO-01.typed_ports");
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), plan_specification).ok());

  VerificationRecordSpec verification{
    .execution_plan_digest = kPayloadDigest,
    .execution_profile = ExecutionProfile::offline,
    .verified_resource_vector = {},
    .verified_terminal_occurrences = 1U,
    .verified_obligations = {"M0.test"},
  };
  EXPECT_TRUE(VerificationRecord::create(artifact_digest(), verification).ok());
  verification.verified_terminal_occurrences = 0U;
  EXPECT_FALSE(VerificationRecord::create(artifact_digest(), verification).ok());

  AdmissionRecordSpec admission{
    .execution_plan_digest = kPayloadDigest,
    .verification_record_digest = kMetadataDigest,
    .outcome = AdmissionOutcome::admitted,
    .reservation = {},
    .reason = std::string(4096U, 'r'),
  };
  EXPECT_TRUE(AdmissionRecord::create(admission).ok());
  admission.reason = "";
  EXPECT_FALSE(AdmissionRecord::create(admission).ok());
  admission.reason = std::string(4097U, 'r');
  EXPECT_FALSE(AdmissionRecord::create(admission).ok());

  // JSON Schema string length is measured in Unicode code points, not UTF-8
  // bytes.  A 4,096-code-point public reason remains representable.
  std::string unicode_reason;
  for (std::size_t index = 0U; index < 4096U; ++index) {
    unicode_reason.append("\xC3\xA9");
  }
  admission.reason = unicode_reason;
  EXPECT_TRUE(AdmissionRecord::create(admission).ok());
  unicode_reason.append("\xC3\xA9");
  admission.reason = unicode_reason;
  EXPECT_FALSE(AdmissionRecord::create(admission).ok());
}

TEST(KSpaceJetReconContractsKeySlotTable, EnforcesDenseMixedRadixIdentityGenerationAndStorageAccounting) {
  using namespace ksj::recon;

  auto specification = valid_execution_plan_spec();
  auto& table = specification.key_slot_tables.front();
  table.dense_dimensions = {{.field = "slice", .minimum = 3U, .cardinality = 2U},
                            {.field = "contrast", .minimum = 0U, .cardinality = 3U}};
  table.key_domain_bound = 6U;
  table.max_distinct_keys = 6U;
  table.max_live_keys = 4U;
  table.slot_count = 4U;
  auto metadata = dense_key_slot_host_metadata_charged_bytes(table.key_domain_bound, table.slot_count, "test table");
  ASSERT_TRUE(metadata.ok()) << metadata.status();
  EXPECT_EQ(160U, metadata.value());
  table.host_metadata_charged_bytes = metadata.value();
  specification.resource_vector.host_normal_bytes = metadata.value();
  EXPECT_TRUE(ExecutionPlan::create(artifact_digest(), specification).ok());

  auto wrong_product = specification;
  wrong_product.key_slot_tables.front().key_domain_bound = 5U;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_product).ok());

  auto wrong_distinct_bound = specification;
  wrong_distinct_bound.key_slot_tables.front().max_distinct_keys = 5U;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_distinct_bound).ok());

  auto wrong_slot_count = specification;
  wrong_slot_count.key_slot_tables.front().slot_count = 3U;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_slot_count).ok());

  auto wrong_metadata = specification;
  --wrong_metadata.key_slot_tables.front().host_metadata_charged_bytes;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_metadata).ok());

  auto undercharged_resource_vector = specification;
  --undercharged_resource_vector.resource_vector.host_normal_bytes;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), undercharged_resource_vector).ok());

  auto wrong_late_event_policy = specification;
  wrong_late_event_policy.key_slot_tables.front().late_event_policy = "drop";
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_late_event_policy).ok());

  auto unsealed_table = specification;
  unsealed_table.key_slot_tables.front().seal_on_completion = false;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), unsealed_table).ok());

  // The dense KeySlot runtime uses a 62-bit semantic slot field and a 63-bit
  // physical owner/free field, so every canonical JSON Quantity is supported.
  // Values beyond that shared artifact/runtime bound are rejected before a
  // compiler- or verifier-produced plan can reach the runtime.
  auto noncanonical_dense_dimension = specification;
  noncanonical_dense_dimension.key_slot_tables.front().dense_dimensions = {
    {.field = "slice", .minimum = 0U, .cardinality = kMaxCanonicalJsonInteger + 1U},
  };
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), noncanonical_dense_dimension).ok());
}

TEST(KSpaceJetReconContractsReorderPlan, EnforcesClosedDenseCartesianIdentityAndResourceCharges) {
  using namespace ksj::recon;

  auto specification = valid_execution_plan_spec();
  auto metadata = dense_cartesian_reorder_host_metadata_charged_bytes(6U, 3U, "test reorder");
  ASSERT_TRUE(metadata.ok()) << metadata.status();
  EXPECT_EQ(336U, metadata.value());
  auto key_metadata = dense_key_slot_host_metadata_charged_bytes(6U, 1U, "test M3 KeySlotTable");
  ASSERT_TRUE(key_metadata.ok()) << key_metadata.status();
  specification.key_slot_tables.front().dense_dimensions = {{.field = "slice", .minimum = 0U, .cardinality = 2U},
                                                            {.field = "contrast", .minimum = 0U, .cardinality = 3U}};
  specification.key_slot_tables.front().key_domain_bound = 6U;
  specification.key_slot_tables.front().max_distinct_keys = 6U;
  specification.key_slot_tables.front().host_metadata_charged_bytes = key_metadata.value();
  specification.reorder_plans = {
    {.node_id = "node",
     .order_domain_id = "node",
     .ordinal_binding_id = std::string(kCompletedFrameSlotContextSemanticKeyOrdinalBindingId),
     .completed_frame_input_port = "frame",
     .ordered_output_port = "image",
     .outputs_per_ordinal = 1U,
     .charged_bytes_per_ordinal = 128U,
     .ordinal_dimensions = {{.field = "slice", .minimum = 0U, .cardinality = 2U},
                            {.field = "contrast", .minimum = 0U, .cardinality = 3U}},
     .ordinal_domain_bound = 6U,
     .first_expected_ordinal = 0U,
     .last_expected_ordinal = 5U,
     .max_ahead_items = 3U,
     .max_ahead_charged_bytes = 512U,
     .max_gap_ordinals = 5U,
     .certified_skipped_ordinals = {},
     .end_of_input_policy = "fail",
     .handle_storage_charged_bytes = 3U * kDenseCartesianReorderHandleSidecarChargedBytes,
     .host_metadata_charged_bytes = metadata.value(),
     .descriptor_charged_count = 3U}};
  specification.resource_vector.host_normal_bytes = key_metadata.value() + metadata.value() + 512U;
  specification.resource_vector.descriptor_count = 3U;
  specification.proof_obligations = {"PO-01.typed_ports", std::string(kM3CompletedFrameSlotBindingProofObligation),
                                     std::string(kM3StrictDenseAllTuplesEoiRuntimeAssumption)};
  EXPECT_TRUE(ExecutionPlan::create(artifact_digest(), specification).ok());

  auto missing_order_domain_identity = specification;
  missing_order_domain_identity.reorder_plans.front().order_domain_id.clear();
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), missing_order_domain_identity).ok());

  auto cross_node_order_domain = specification;
  cross_node_order_domain.reorder_plans.front().order_domain_id = "another_node";
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), cross_node_order_domain).ok());

  auto missing_key_slot_owner = specification;
  missing_key_slot_owner.reorder_plans.front().node_id = "another_node";
  missing_key_slot_owner.reorder_plans.front().order_domain_id = "another_node";
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), missing_key_slot_owner).ok());

  auto mismatched_key_slot_dimensions = specification;
  mismatched_key_slot_dimensions.key_slot_tables.front().dense_dimensions.front().cardinality = 1U;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), mismatched_key_slot_dimensions).ok());

  auto wrong_ordinal_binding = specification;
  wrong_ordinal_binding.reorder_plans.front().ordinal_binding_id = "provider-ordinal/v1";
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_ordinal_binding).ok());

  auto missing_completed_frame_input = specification;
  missing_completed_frame_input.reorder_plans.front().completed_frame_input_port.clear();
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), missing_completed_frame_input).ok());

  auto missing_ordered_output_port = specification;
  missing_ordered_output_port.reorder_plans.front().ordered_output_port.clear();
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), missing_ordered_output_port).ok());

  auto multiple_outputs_per_ordinal = specification;
  multiple_outputs_per_ordinal.reorder_plans.front().outputs_per_ordinal = 2U;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), multiple_outputs_per_ordinal).ok());

  auto insufficient_ahead_bytes = specification;
  insufficient_ahead_bytes.reorder_plans.front().charged_bytes_per_ordinal = 171U;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), insufficient_ahead_bytes).ok());

  auto wrong_last_ordinal = specification;
  --wrong_last_ordinal.reorder_plans.front().last_expected_ordinal;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_last_ordinal).ok());

  auto skipped_ordinal = specification;
  skipped_ordinal.reorder_plans.front().certified_skipped_ordinals = {1U};
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), skipped_ordinal).ok());

  auto skipping_end_of_input = specification;
  skipping_end_of_input.reorder_plans.front().end_of_input_policy = "skip";
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), skipping_end_of_input).ok());

  auto non_strict_occurrence_policy = specification;
  non_strict_occurrence_policy.reorder_plans.front().occurrence_policy = "sparse-observed-tuples";
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), non_strict_occurrence_policy).ok());

  auto wrong_metadata = specification;
  --wrong_metadata.reorder_plans.front().host_metadata_charged_bytes;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_metadata).ok());

  auto wrong_handle_storage = specification;
  --wrong_handle_storage.reorder_plans.front().handle_storage_charged_bytes;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_handle_storage).ok());

  auto wrong_descriptor_charge = specification;
  --wrong_descriptor_charge.reorder_plans.front().descriptor_charged_count;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_descriptor_charge).ok());

  auto undercharged_resource_vector = specification;
  --undercharged_resource_vector.resource_vector.host_normal_bytes;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), undercharged_resource_vector).ok());
}

TEST(KSpaceJetReconContractsPlanBoundDataPlane,
     FreezesPoolPhysicalChargeAnd96ByteEdgeControlWithoutDoubleChargingPayload) {
  using namespace ksj::recon;

  const auto image = immutable_image_buffer_type();
  auto specification = valid_execution_plan_spec();
  auto& table = specification.key_slot_tables.front();
  table.dense_dimensions = {{.field = "slice", .minimum = 0U, .cardinality = 2U}};
  table.key_domain_bound = 2U;
  table.max_distinct_keys = 2U;
  table.max_live_keys = 1U;
  table.slot_count = 1U;
  auto table_metadata = dense_key_slot_host_metadata_charged_bytes(2U, 1U, "M3.7 table");
  ASSERT_TRUE(table_metadata.ok()) << table_metadata.status();
  table.host_metadata_charged_bytes = table_metadata.value();

  auto reorder_metadata = dense_cartesian_reorder_host_metadata_charged_bytes(2U, 2U, "M3.7 reorder");
  ASSERT_TRUE(reorder_metadata.ok()) << reorder_metadata.status();
  specification.reorder_plans = {{
    .node_id = "node",
    .order_domain_id = "node",
    .ordinal_binding_id = std::string(kCompletedFrameSlotContextSemanticKeyOrdinalBindingId),
    .completed_frame_input_port = "frame",
    .ordered_output_port = "image",
    .outputs_per_ordinal = 1U,
    .charged_bytes_per_ordinal = 128U,
    .ordinal_dimensions = {{.field = "slice", .minimum = 0U, .cardinality = 2U}},
    .ordinal_domain_bound = 2U,
    .first_expected_ordinal = 0U,
    .last_expected_ordinal = 1U,
    .max_ahead_items = 2U,
    .max_ahead_charged_bytes = 256U,
    .max_gap_ordinals = 1U,
    .certified_skipped_ordinals = {},
    .end_of_input_policy = "fail",
    .handle_storage_charged_bytes = 128U,
    .host_metadata_charged_bytes = reorder_metadata.value(),
    .descriptor_charged_count = 2U,
  }};

  auto pool_metadata = m37_buffer_pool_host_metadata_charged_bytes(3U, "M3.7 pool");
  auto pool_physical = m37_buffer_pool_physical_charge_bytes(3U, 128U, 0U, "M3.7 pool");
  auto edge_metadata = m37_data_edge_host_metadata_charged_bytes(3U, "M3.7 edge");
  ASSERT_TRUE(pool_metadata.ok()) << pool_metadata.status();
  ASSERT_TRUE(pool_physical.ok()) << pool_physical.status();
  ASSERT_TRUE(edge_metadata.ok()) << edge_metadata.status();
  auto one_edge_item = m37_data_edge_host_metadata_charged_bytes(1U, "M3.7 one edge item");
  ASSERT_TRUE(one_edge_item.ok()) << one_edge_item.status();
  EXPECT_EQ(96U, one_edge_item.value());
  EXPECT_EQ(288U, edge_metadata.value());
  EXPECT_EQ(504U, pool_physical.value());
  specification.buffer_pool_plans = {{
    .pool_id = "image_edge.pool",
    .producer_node_id = "node",
    .producer_port_name = "image",
    .producer_provider_id = "org.kspacejet.test-provider",
    .producer_bundle_digest = kPayloadDigest,
    .producer_operator_id = "image_producer",
    .producer_contract_digest = kPayloadDigest,
    .type_descriptor = image,
    .memory_domain = TypeMemoryDomain::host_normal,
    .slot_count = 3U,
    .payload_capacity_bytes = 128U,
    .metadata_capacity_bytes = 0U,
    .payload_alignment_bytes = 64U,
    .host_metadata_charged_bytes = pool_metadata.value(),
    .descriptor_charged_count = 3U,
    .physical_charge_bytes = pool_physical.value(),
  }};
  specification.data_edge_plans = {{
    .edge_id = "image_edge",
    .source_pool_id = "image_edge.pool",
    .producer_node_id = "node",
    .producer_port_name = "image",
    .producer_abi_port = 0U,
    .consumer_node_id = "sink_adapter",
    .consumer_port_name = "image",
    .type_descriptor = image,
    .max_items = 3U,
    .max_logical_bytes = 384U,
    .host_metadata_charged_bytes = edge_metadata.value(),
    .descriptor_charged_count = 3U,
    .firing_lease_staging_charged_bytes = kM37FiringLeaseHostStagingChargedBytes,
    .firing_lease_staging_descriptor_count = kM37FiringLeaseHostStagingDescriptorCount,
  }};
  specification.resource_vector.host_normal_bytes = table_metadata.value() + reorder_metadata.value() +
                                                    pool_physical.value() + edge_metadata.value() +
                                                    kM37FiringLeaseHostStagingChargedBytes;
  specification.resource_vector.descriptor_count = 2U + 3U + 3U + kM37FiringLeaseHostStagingDescriptorCount;
  specification.proof_obligations = {
    "PO-01.typed_ports",
    std::string(kM3CompletedFrameSlotBindingProofObligation),
    std::string(kM3StrictDenseAllTuplesEoiRuntimeAssumption),
    std::string(kM37PlanBoundDataPlaneProofObligation),
    std::string(kM37SinglePhysicalPayloadChargeRuntimeAssumption),
  };
  const auto plan = ExecutionPlan::create(artifact_digest(), specification);
  ASSERT_TRUE(plan.ok()) << plan.status();
  ASSERT_EQ(1U, plan.value().buffer_pool_plans().size());
  const auto& pool = plan.value().buffer_pool_plans().front();
  EXPECT_EQ("org.kspacejet.test-provider", pool.producer_provider_id());
  EXPECT_EQ(kPayloadDigest, pool.producer_bundle_digest().value());
  EXPECT_EQ("image_producer", pool.producer_operator_id());
  EXPECT_EQ(kPayloadDigest, pool.producer_contract_digest().value());

  auto missing_provider_id = specification;
  missing_provider_id.buffer_pool_plans.front().producer_provider_id.clear();
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), missing_provider_id).ok());

  auto malformed_bundle_digest = specification;
  malformed_bundle_digest.buffer_pool_plans.front().producer_bundle_digest = "sha256:not-a-digest";
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), malformed_bundle_digest).ok());

  auto missing_operator_id = specification;
  missing_operator_id.buffer_pool_plans.front().producer_operator_id.clear();
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), missing_operator_id).ok());

  auto malformed_contract_digest = specification;
  malformed_contract_digest.buffer_pool_plans.front().producer_contract_digest = "sha256:not-a-digest";
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), malformed_contract_digest).ok());

  auto unproven_contract_digest = specification;
  unproven_contract_digest.buffer_pool_plans.front().producer_contract_digest =
    "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), unproven_contract_digest).ok());

  auto wrong_edge_charge = specification;
  --wrong_edge_charge.data_edge_plans.front().host_metadata_charged_bytes;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_edge_charge).ok());

  auto wrong_staging = specification;
  --wrong_staging.data_edge_plans.front().firing_lease_staging_charged_bytes;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_staging).ok());

  auto wrong_slot_bound = specification;
  --wrong_slot_bound.buffer_pool_plans.front().slot_count;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_slot_bound).ok());

  // An artifact may use either the legacy opaque reorder payload reservation
  // or the M3.7 pool-owned model, never both.  This exercises the
  // ExecutionPlan::create cross-check rather than relying only on compiler
  // topology validation.
  auto mixed_legacy_reorder = specification;
  auto legacy_table = mixed_legacy_reorder.key_slot_tables.front();
  legacy_table.node_id = "legacy_node";
  mixed_legacy_reorder.key_slot_tables.push_back(std::move(legacy_table));
  auto legacy_reorder = mixed_legacy_reorder.reorder_plans.front();
  legacy_reorder.node_id = "legacy_node";
  legacy_reorder.order_domain_id = "legacy_node";
  legacy_reorder.ordered_output_port = "legacy_image";
  mixed_legacy_reorder.reorder_plans.push_back(std::move(legacy_reorder));
  const auto mixed = ExecutionPlan::create(artifact_digest(), mixed_legacy_reorder);
  ASSERT_FALSE(mixed.ok());
  EXPECT_NE(std::string::npos, mixed.status().message().find("legacy opaque ReorderPlan"));
}

} // namespace
