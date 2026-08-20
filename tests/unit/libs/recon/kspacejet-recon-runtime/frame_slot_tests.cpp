#include "kspacejet/recon/runtime/acquisition_classification.hpp"
#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
namespace {

using ksj::base::byte;
using ksj::recon::runtime::CartesianFrameSlot;
using ksj::recon::runtime::CartesianFrameSlotConfig;
using ksj::recon::runtime::DuplicateAcquisitionPolicy;
using ksj::recon::runtime::FrameCompletion;
using ksj::recon::runtime::FrameSealDisposition;
using ksj::recon::runtime::FrameSlotState;
using ksj::recon::runtime::IncompleteFramePolicy;

CartesianFrameSlotConfig
frame_config(const IncompleteFramePolicy incomplete_policy = IncompleteFramePolicy::fail,
             const DuplicateAcquisitionPolicy duplicate_policy = DuplicateAcquisitionPolicy::reject) {
  return {
    .slot_id = 7U,
    .dimensions =
      {
        .readout_samples = 2U,
        .phase_encode_1 = 3U,
        .phase_encode_2 = 2U,
        .channels = 1U,
        .bytes_per_sample = 2U,
      },
    .completion =
      {
        .required_indices =
          {
            {.phase_encode_1 = 0U, .phase_encode_2 = 0U},
            {.phase_encode_1 = 2U, .phase_encode_2 = 0U},
            {.phase_encode_1 = 1U, .phase_encode_2 = 1U},
          },
      },
    .resource_upper_bound =
      {
        .max_total_arrivals = duplicate_policy == DuplicateAcquisitionPolicy::reject ? 3U : 5U,
        .max_duplicate_arrivals = duplicate_policy == DuplicateAcquisitionPolicy::reject ? 0U : 2U,
        .max_payload_bytes = 4U,
      },
    .duplicate_policy = duplicate_policy,
    .incomplete_policy = incomplete_policy,
  };
}

std::array<byte, 4> payload(const std::uint8_t seed) {
  return {
    static_cast<byte>(seed),
    static_cast<byte>(seed + 1U),
    static_cast<byte>(seed + 2U),
    static_cast<byte>(seed + 3U),
  };
}

TEST(KSpaceJetAcquisitionClassifier, RoutesOnlyAcquisitionsAndRecordsExplicitIgnoredReason) {
  auto classifier = ksj::recon::runtime::AcquisitionClassifier::create({});
  ASSERT_TRUE(classifier.ok()) << classifier.status();

  const auto waveform = classifier.value().classify({
    .message_kind = ksj::recon::runtime::IsmrmrdMessageKind::waveform,
  });
  EXPECT_FALSE(waveform.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, waveform.status().code());

  const auto ignored = classifier.value().classify({
    .flags = {.dummy_scan = true},
    .index = {.encoding_space = 1U, .slice = 2U},
  });
  ASSERT_TRUE(ignored.ok()) << ignored.status();
  EXPECT_EQ(ksj::recon::runtime::AcquisitionLane::ignored_explicitly, ignored.value().lane);
  EXPECT_EQ(ksj::recon::runtime::AcquisitionClassificationReason::dummy_scan_rule, ignored.value().reason);
  EXPECT_EQ(2U, ignored.value().index.slice);
}

TEST(KSpaceJetAcquisitionClassifier, RejectsConflictingSemanticFlagsAndKeepsCombinedCalibrationImaging) {
  auto classifier = ksj::recon::runtime::AcquisitionClassifier::create({});
  ASSERT_TRUE(classifier.ok()) << classifier.status();

  const auto conflict = classifier.value().classify({
    .flags = {.noise_measurement = true, .navigation = true},
  });
  EXPECT_FALSE(conflict.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, conflict.status().code());

  const auto combined = classifier.value().classify({
    .flags = {.parallel_calibration = true, .parallel_calibration_and_imaging = true},
  });
  ASSERT_TRUE(combined.ok()) << combined.status();
  EXPECT_EQ(ksj::recon::runtime::AcquisitionLane::calibration_and_imaging, combined.value().lane);
  EXPECT_TRUE(ksj::recon::runtime::is_imaging_lane(combined.value().lane));
}

TEST(KSpaceJetCartesianFrameSlot, DirectScatterCompletesExactCoverageAndProtectsGeneration) {
  auto slot = CartesianFrameSlot::create(frame_config());
  ASSERT_TRUE(slot.ok()) << slot.status();
  auto token = slot.value().begin_frame({
    .semantic_key = {.encoding_space = 1U, .slice = 3U, .contrast = 2U},
    .order_key = 42U,
    .placement_key = 99U,
  });
  ASSERT_TRUE(token.ok()) << token.status();

  const auto first = payload(10U);
  const auto second = payload(20U);
  const auto third = payload(30U);
  ASSERT_TRUE(slot.value().scatter(token.value(), {.phase_encode_1 = 2U, .phase_encode_2 = 0U}, first).ok());
  ASSERT_TRUE(slot.value().scatter(token.value(), {.phase_encode_1 = 1U, .phase_encode_2 = 1U}, second).ok());
  EXPECT_EQ(FrameSlotState::filling, slot.value().snapshot().state);
  ASSERT_TRUE(slot.value().scatter(token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, third).ok());

  const auto snapshot = slot.value().snapshot();
  EXPECT_EQ(FrameSlotState::ready, snapshot.state);
  EXPECT_EQ(FrameCompletion::complete, snapshot.completion);
  EXPECT_EQ(3U, snapshot.covered_indices);
  EXPECT_EQ(3U, snapshot.total_arrivals);
  EXPECT_EQ(24U, snapshot.storage_bytes);

  const auto bytes = slot.value().frame_bytes(token.value());
  ASSERT_TRUE(bytes.ok()) << bytes.status();
  ASSERT_EQ(24U, bytes.value().size());
  // Physical line 0 is the final third acquisition; line 2 holds first;
  // line 4 holds second.  The uncovered physical lines remain zero.
  EXPECT_EQ(third[0], bytes.value()[0]);
  EXPECT_EQ(first[0], bytes.value()[8]);
  EXPECT_EQ(second[0], bytes.value()[16]);

  ASSERT_TRUE(slot.value().begin_compute(token.value()).ok());
  ASSERT_TRUE(slot.value().begin_emit(token.value()).ok());
  ASSERT_TRUE(slot.value().recycle(token.value()).ok());
  auto next = slot.value().begin_frame({});
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_NE(token.value().generation, next.value().generation);
  EXPECT_FALSE(slot.value().scatter(token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, first).ok());
}

TEST(KSpaceJetCartesianFrameSlot, AppliesDuplicatePoliciesBeforeSeal) {
  const auto line = payload(7U);
  const auto changed_line = payload(8U);

  auto rejecting = CartesianFrameSlot::create(frame_config());
  ASSERT_TRUE(rejecting.ok()) << rejecting.status();
  auto rejecting_token = rejecting.value().begin_frame({});
  ASSERT_TRUE(rejecting_token.ok()) << rejecting_token.status();
  ASSERT_TRUE(
    rejecting.value().scatter(rejecting_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line).ok());
  const auto duplicate =
    rejecting.value().scatter(rejecting_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line);
  EXPECT_FALSE(duplicate.ok());
  EXPECT_EQ(ksj::base::StatusCode::already_exists, duplicate.code());

  auto ignore_identical =
    CartesianFrameSlot::create(frame_config(IncompleteFramePolicy::fail, DuplicateAcquisitionPolicy::ignore_identical));
  ASSERT_TRUE(ignore_identical.ok()) << ignore_identical.status();
  auto ignore_token = ignore_identical.value().begin_frame({});
  ASSERT_TRUE(ignore_token.ok()) << ignore_token.status();
  ASSERT_TRUE(
    ignore_identical.value().scatter(ignore_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line).ok());
  ASSERT_TRUE(
    ignore_identical.value().scatter(ignore_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line).ok());
  EXPECT_FALSE(ignore_identical.value()
                 .scatter(ignore_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, changed_line)
                 .ok());
  EXPECT_EQ(2U, ignore_identical.value().snapshot().duplicate_arrivals);

  auto replacing = CartesianFrameSlot::create(
    frame_config(IncompleteFramePolicy::fail, DuplicateAcquisitionPolicy::replace_before_seal));
  ASSERT_TRUE(replacing.ok()) << replacing.status();
  auto replace_token = replacing.value().begin_frame({});
  ASSERT_TRUE(replace_token.ok()) << replace_token.status();
  ASSERT_TRUE(
    replacing.value().scatter(replace_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line).ok());
  ASSERT_TRUE(
    replacing.value().scatter(replace_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, changed_line).ok());
  EXPECT_EQ(1U, replacing.value().snapshot().duplicate_arrivals);
  EXPECT_EQ(2U, replacing.value().snapshot().total_arrivals);
}

TEST(KSpaceJetCartesianFrameSlot, AppliesAllEndOfInputMissingPoliciesWithoutCountGuessing) {
  const auto line = payload(11U);

  auto failing = CartesianFrameSlot::create(frame_config(IncompleteFramePolicy::fail));
  ASSERT_TRUE(failing.ok()) << failing.status();
  auto failing_token = failing.value().begin_frame({});
  ASSERT_TRUE(failing_token.ok()) << failing_token.status();
  ASSERT_TRUE(failing.value().scatter(failing_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line).ok());
  const auto failed_eoi = failing.value().end_of_input(failing_token.value());
  EXPECT_FALSE(failed_eoi.ok());
  EXPECT_EQ(FrameSlotState::quarantined, failing.value().snapshot().state);
  auto missing = failing.value().missing_indices(failing_token.value());
  ASSERT_TRUE(missing.ok()) << missing.status();
  ASSERT_EQ(2U, missing.value().size());

  auto partial = CartesianFrameSlot::create(frame_config(IncompleteFramePolicy::emit_partial));
  ASSERT_TRUE(partial.ok()) << partial.status();
  auto partial_token = partial.value().begin_frame({});
  ASSERT_TRUE(partial_token.ok()) << partial_token.status();
  ASSERT_TRUE(partial.value().scatter(partial_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line).ok());
  const auto partial_eoi = partial.value().end_of_input(partial_token.value());
  ASSERT_TRUE(partial_eoi.ok()) << partial_eoi.status();
  EXPECT_EQ(FrameSealDisposition::partial, partial_eoi.value());
  EXPECT_EQ(FrameCompletion::partial, partial.value().snapshot().completion);
  EXPECT_EQ(FrameSlotState::ready, partial.value().snapshot().state);

  auto skipped = CartesianFrameSlot::create(frame_config(IncompleteFramePolicy::certified_skip));
  ASSERT_TRUE(skipped.ok()) << skipped.status();
  auto skip_token = skipped.value().begin_frame({});
  ASSERT_TRUE(skip_token.ok()) << skip_token.status();
  ASSERT_TRUE(skipped.value().scatter(skip_token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line).ok());
  const auto skip_eoi = skipped.value().end_of_input(skip_token.value());
  ASSERT_TRUE(skip_eoi.ok()) << skip_eoi.status();
  EXPECT_EQ(FrameSealDisposition::certified_skip, skip_eoi.value());
  EXPECT_EQ(FrameSlotState::skipped, skipped.value().snapshot().state);
  ASSERT_TRUE(skipped.value().recycle(skip_token.value()).ok());
}

TEST(KSpaceJetCartesianFrameSlot, RejectsMalformedBoundedConfigurationAndLateInput) {
  auto invalid = frame_config();
  invalid.completion.required_indices.push_back({.phase_encode_1 = 0U, .phase_encode_2 = 0U});
  EXPECT_FALSE(CartesianFrameSlot::create(std::move(invalid)).ok());

  auto slot = CartesianFrameSlot::create(frame_config());
  ASSERT_TRUE(slot.ok()) << slot.status();
  auto token = slot.value().begin_frame({});
  ASSERT_TRUE(token.ok()) << token.status();
  const auto line = payload(1U);
  ASSERT_TRUE(slot.value().scatter(token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line).ok());
  ASSERT_TRUE(slot.value().scatter(token.value(), {.phase_encode_1 = 2U, .phase_encode_2 = 0U}, line).ok());
  ASSERT_TRUE(slot.value().scatter(token.value(), {.phase_encode_1 = 1U, .phase_encode_2 = 1U}, line).ok());
  EXPECT_FALSE(slot.value().scatter(token.value(), {.phase_encode_1 = 0U, .phase_encode_2 = 0U}, line).ok());
}

} // namespace
