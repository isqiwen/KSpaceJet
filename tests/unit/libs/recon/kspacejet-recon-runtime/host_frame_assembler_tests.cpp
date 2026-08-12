#include "kspacejet/recon/runtime/host_frame_assembler.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ksj::base::Result;
using ksj::recon::ArtifactDigest;
using ksj::recon::ExecutionPlan;
using ksj::recon::ExecutionPlanSpec;
using ksj::recon::VerificationRecord;
using ksj::recon::VerificationRecordSpec;
using ksj::recon::runtime::CartesianFrameSlotConfig;
using ksj::recon::runtime::CompletedFrameLeaseBindingStatus;
using ksj::recon::runtime::DuplicateAcquisitionPolicy;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::HostFrameAssembler;
using ksj::recon::runtime::HostFrameAssemblerConfig;
using ksj::recon::runtime::IncompleteFramePolicy;

constexpr std::string_view kPlanDigest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view kVerificationDigest =
  "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr std::string_view kOtherPlanDigest = "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

struct RuntimeArtifacts {
  ExecutionPlan plan;
  VerificationRecord verification;
};

[[nodiscard]] Result<ArtifactDigest> digest(const std::string_view value) {
  return ArtifactDigest::parse(value, "host frame assembler test digest");
}

[[nodiscard]] Result<RuntimeArtifacts> make_artifacts(const std::string_view plan_digest = kPlanDigest) {
  const auto key_metadata = ksj::recon::dense_key_slot_host_metadata_charged_bytes(4U, 4U, "test key metadata");
  if (!key_metadata.ok()) {
    return key_metadata.status();
  }
  const auto reorder_metadata =
    ksj::recon::dense_cartesian_reorder_host_metadata_charged_bytes(4U, 2U, "test reorder metadata");
  if (!reorder_metadata.ok()) {
    return reorder_metadata.status();
  }
  const auto parsed_plan_digest = digest(plan_digest);
  if (!parsed_plan_digest.ok()) {
    return parsed_plan_digest.status();
  }

  ExecutionPlanSpec plan_specification;
  plan_specification.inputs = {
    .resolved_pipeline = std::string(kPlanDigest),
    .scan_descriptor = std::string(kPlanDigest),
    .target_envelope = std::string(kPlanDigest),
    .machine_policy = std::string(kPlanDigest),
    .provider_contracts = {std::string(kPlanDigest)},
  };
  plan_specification.execution_profile = ksj::recon::ExecutionProfile::offline;
  plan_specification.key_slot_tables = {
    {
      .node_id = "reconstruct",
      .dense_dimensions = {{.field = "slice", .minimum = 0U, .cardinality = 4U}},
      .key_domain_bound = 4U,
      .max_distinct_keys = 4U,
      .max_live_keys = 4U,
      .slot_count = 4U,
      .host_metadata_charged_bytes = key_metadata.value(),
      .max_items_per_activation = 1U,
      .max_charged_bytes_per_activation = 16U,
    },
  };
  plan_specification.reorder_plans = {
    {
      .node_id = "reconstruct",
      .order_domain_id = "reconstruct",
      .ordinal_binding_id = std::string(ksj::recon::kCompletedFrameSlotContextSemanticKeyOrdinalBindingId),
      .completed_frame_input_port = "completed-frame",
      .ordered_output_port = "image",
      .outputs_per_ordinal = 1U,
      .charged_bytes_per_ordinal = 16U,
      .ordinal_dimensions = {{.field = "slice", .minimum = 0U, .cardinality = 4U}},
      .ordinal_domain_bound = 4U,
      .first_expected_ordinal = 0U,
      .last_expected_ordinal = 3U,
      .max_ahead_items = 2U,
      .max_ahead_charged_bytes = 32U,
      .max_gap_ordinals = 3U,
      .occurrence_policy = std::string(ksj::recon::kStrictDenseAllTuplesReorderOccurrencePolicy),
      .publish_policy = std::string(ksj::recon::kNextExpectedOnlyReorderPublishPolicy),
      .certified_skipped_ordinals = {},
      .end_of_input_policy = std::string(ksj::recon::kFailReorderEndOfInputPolicy),
      .handle_storage_charged_bytes = 2U * ksj::recon::kDenseCartesianReorderHandleSidecarChargedBytes,
      .host_metadata_charged_bytes = reorder_metadata.value(),
      .descriptor_charged_count = 2U,
    },
  };
  plan_specification.resource_vector = {
    .host_normal_bytes = key_metadata.value() + reorder_metadata.value() + 32U,
    .descriptor_count = 2U,
  };
  plan_specification.terminal_occurrences = 4U;
  plan_specification.proof_obligations = {
    std::string(ksj::recon::kM3CompletedFrameSlotBindingProofObligation),
    std::string(ksj::recon::kM3StrictDenseAllTuplesEoiRuntimeAssumption),
  };
  auto plan = ExecutionPlan::create(parsed_plan_digest.value(), plan_specification);
  if (!plan.ok()) {
    return plan.status();
  }

  const auto parsed_verification_digest = digest(kVerificationDigest);
  if (!parsed_verification_digest.ok()) {
    return parsed_verification_digest.status();
  }
  const VerificationRecordSpec verification_specification{
    .execution_plan_digest = plan.value().digest().value(),
    .execution_profile = plan.value().execution_profile(),
    .verified_resource_vector =
      {
        .host_normal_bytes = plan.value().resources().host_normal_bytes(),
        .descriptor_count = plan.value().resources().descriptor_count(),
      },
    .verified_terminal_occurrences = plan.value().terminal_occurrences(),
    .verified_obligations =
      {
        std::string(ksj::recon::kM3CompletedFrameSlotBindingVerificationObligation),
        std::string(ksj::recon::kM3StrictDenseAllTuplesEoiVerificationObligation),
      },
  };
  auto verification = VerificationRecord::create(parsed_verification_digest.value(), verification_specification);
  if (!verification.ok()) {
    return verification.status();
  }
  return RuntimeArtifacts{std::move(plan).value(), std::move(verification).value()};
}

[[nodiscard]] CartesianFrameSlotConfig frame_slot_config(const std::uint32_t slot_id) {
  return {
    .slot_id = slot_id,
    .dimensions =
      {
        .readout_samples = 2U,
        .phase_encode_1 = 2U,
        .phase_encode_2 = 1U,
        .channels = 1U,
        .bytes_per_sample = 2U,
      },
    .completion = {.required_indices = {{.phase_encode_1 = 0U, .phase_encode_2 = 0U},
                                        {.phase_encode_1 = 1U, .phase_encode_2 = 0U}}},
    .resource_upper_bound = {.max_total_arrivals = 2U, .max_duplicate_arrivals = 0U, .max_payload_bytes = 4U},
    .duplicate_policy = DuplicateAcquisitionPolicy::reject,
    .incomplete_policy = IncompleteFramePolicy::fail,
  };
}

[[nodiscard]] HostFrameAssemblerConfig assembler_config() {
  return {
    .scan_instance_id = "test-scan-1",
    .frame_slots = {frame_slot_config(10U), frame_slot_config(11U), frame_slot_config(12U)},
  };
}

[[nodiscard]] FrameSlotContext frame_context(const std::uint16_t slice = 0U) {
  FrameSlotContext result;
  result.semantic_key.slice = slice;
  result.order_key = slice;
  result.placement_key = slice;
  return result;
}

[[nodiscard]] std::array<ksj::base::byte, 4U> line(const std::uint8_t first) {
  return {static_cast<ksj::base::byte>(first), static_cast<ksj::base::byte>(first + 1U),
          static_cast<ksj::base::byte>(first + 2U), static_cast<ksj::base::byte>(first + 3U)};
}

TEST(KSpaceJetHostFrameAssembler, RejectsUnboundArtifactsPoliciesAndInsufficientHeadReserve) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();

  auto too_small = assembler_config();
  too_small.frame_slots.pop_back();
  EXPECT_FALSE(HostFrameAssembler::create(artifacts.value().plan, artifacts.value().verification, "reconstruct",
                                          std::move(too_small))
                 .ok());

  auto wrong_policy = assembler_config();
  wrong_policy.frame_slots.front().incomplete_policy = IncompleteFramePolicy::emit_partial;
  EXPECT_FALSE(HostFrameAssembler::create(artifacts.value().plan, artifacts.value().verification, "reconstruct",
                                          std::move(wrong_policy))
                 .ok());

  auto missing_verdict_digest = digest(kVerificationDigest);
  ASSERT_TRUE(missing_verdict_digest.ok()) << missing_verdict_digest.status();
  auto missing_verdict = VerificationRecord::create(
    missing_verdict_digest.value(),
    {
      .execution_plan_digest = artifacts.value().plan.digest().value(),
      .execution_profile = artifacts.value().plan.execution_profile(),
      .verified_resource_vector =
        {
          .host_normal_bytes = artifacts.value().plan.resources().host_normal_bytes(),
          .descriptor_count = artifacts.value().plan.resources().descriptor_count(),
        },
      .verified_terminal_occurrences = artifacts.value().plan.terminal_occurrences(),
      .verified_obligations = {std::string(ksj::recon::kM3CompletedFrameSlotBindingVerificationObligation)},
    });
  ASSERT_TRUE(missing_verdict.ok()) << missing_verdict.status();
  EXPECT_FALSE(
    HostFrameAssembler::create(artifacts.value().plan, missing_verdict.value(), "reconstruct", assembler_config())
      .ok());
}

TEST(KSpaceJetHostFrameAssembler, SealsExactCoverageAndRecyclesOnlyAfterConsumerAcknowledgement) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto created = HostFrameAssembler::create(artifacts.value().plan, artifacts.value().verification, "reconstruct",
                                            assembler_config());
  ASSERT_TRUE(created.ok()) << created.status();
  auto assembler = std::move(created).value();

  auto assembly = assembler->try_begin_frame(frame_context());
  ASSERT_TRUE(assembly.ok()) << assembly.status();
  auto writable = std::move(assembly).value();
  const auto first_line = line(10U);
  ASSERT_TRUE(writable.scatter({.phase_encode_1 = 0U, .phase_encode_2 = 0U}, first_line).ok());
  EXPECT_FALSE(writable.seal_complete().ok());
  const auto second_line = line(20U);
  ASSERT_TRUE(writable.scatter({.phase_encode_1 = 1U, .phase_encode_2 = 0U}, second_line).ok());

  auto sealed = writable.seal_complete();
  ASSERT_TRUE(sealed.ok()) << sealed.status();
  auto completed = std::move(sealed).value();
  EXPECT_FALSE(writable.valid());
  const auto bytes = completed.bytes();
  ASSERT_TRUE(bytes.ok()) << bytes.status();
  ASSERT_EQ(8U, bytes.value().size());
  EXPECT_EQ(first_line[0], bytes.value()[0]);
  EXPECT_EQ(second_line[0], bytes.value()[4]);
  EXPECT_EQ(
    CompletedFrameLeaseBindingStatus::match,
    completed.binding_status(artifacts.value().plan, artifacts.value().verification, "reconstruct", "completed-frame"));

  ASSERT_TRUE(completed.begin_dispatch().ok());
  EXPECT_EQ(1U, assembler->snapshot().dispatched_slots);
  ASSERT_TRUE(completed.acknowledge_consumed().ok());
  EXPECT_FALSE(completed.valid());
  EXPECT_EQ(3U, assembler->snapshot().free_slots);
  EXPECT_TRUE(assembler->end_of_input().ok());
}

TEST(KSpaceJetHostFrameAssembler, RejectsLiveDuplicateSemanticKeysAndFailsClosedOnDroppedOrIncompleteLeases) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto created = HostFrameAssembler::create(artifacts.value().plan, artifacts.value().verification, "reconstruct",
                                            assembler_config());
  ASSERT_TRUE(created.ok()) << created.status();
  auto assembler = std::move(created).value();

  auto first = assembler->try_begin_frame(frame_context());
  ASSERT_TRUE(first.ok()) << first.status();
  EXPECT_FALSE(assembler->try_begin_frame(frame_context()).ok());
  EXPECT_FALSE(assembler->end_of_input().ok());
  EXPECT_TRUE(assembler->snapshot().failed);
  EXPECT_GE(assembler->snapshot().quarantined_slots, 1U);

  auto second_artifacts = make_artifacts();
  ASSERT_TRUE(second_artifacts.ok()) << second_artifacts.status();
  auto second_created = HostFrameAssembler::create(second_artifacts.value().plan, second_artifacts.value().verification,
                                                   "reconstruct", assembler_config());
  ASSERT_TRUE(second_created.ok()) << second_created.status();
  auto second_assembler = std::move(second_created).value();
  {
    auto dropped = second_assembler->try_begin_frame(frame_context(1U));
    ASSERT_TRUE(dropped.ok()) << dropped.status();
  }
  EXPECT_TRUE(second_assembler->snapshot().failed);
}

TEST(KSpaceJetHostFrameAssembler, KeepsLeaseStateAliveAcrossOwnerDestructionAndRejectsForeignArtifacts) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto created = HostFrameAssembler::create(artifacts.value().plan, artifacts.value().verification, "reconstruct",
                                            assembler_config());
  ASSERT_TRUE(created.ok()) << created.status();
  auto assembler = std::move(created).value();
  auto assembly = assembler->try_begin_frame(frame_context());
  ASSERT_TRUE(assembly.ok()) << assembly.status();
  auto writable = std::move(assembly).value();
  const auto payload = line(1U);
  ASSERT_TRUE(writable.scatter({.phase_encode_1 = 0U, .phase_encode_2 = 0U}, payload).ok());
  ASSERT_TRUE(writable.scatter({.phase_encode_1 = 1U, .phase_encode_2 = 0U}, payload).ok());
  auto sealed = writable.seal_complete();
  ASSERT_TRUE(sealed.ok()) << sealed.status();
  auto completed = std::move(sealed).value();

  auto foreign_artifacts = make_artifacts(kOtherPlanDigest);
  ASSERT_TRUE(foreign_artifacts.ok()) << foreign_artifacts.status();
  EXPECT_EQ(CompletedFrameLeaseBindingStatus::foreign,
            completed.binding_status(foreign_artifacts.value().plan, foreign_artifacts.value().verification,
                                     "reconstruct", "completed-frame"));
  assembler.reset();
  EXPECT_FALSE(completed.bytes().ok());
  EXPECT_EQ(
    CompletedFrameLeaseBindingStatus::stale_or_consumed,
    completed.binding_status(artifacts.value().plan, artifacts.value().verification, "reconstruct", "completed-frame"));
}

} // namespace
