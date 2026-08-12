#include "kspacejet/recon/runtime/serial_cartesian_pipeline.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {

using ksj::base::byte;
using ksj::recon::runtime::AcquisitionLane;
using ksj::recon::runtime::CartesianFrameSlotConfig;
using ksj::recon::runtime::DuplicateAcquisitionPolicy;
using ksj::recon::runtime::FrameCompletion;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::IncompleteFramePolicy;
using ksj::recon::runtime::NormalizedAcquisitionIngressFacts;
using ksj::recon::runtime::NormalizedCartesianAcquisitionFrame;
using ksj::recon::runtime::ScanState;
using ksj::recon::runtime::SealedCartesianFrame;
using ksj::recon::runtime::SerialCartesianPipeline;
using ksj::recon::runtime::SerialCartesianPipelineConfig;
using ksj::recon::runtime::SerialIngressDisposition;
using ksj::recon::runtime::TerminalCause;

struct ObservedFrame {
  std::uint64_t order_key{0};
  FrameCompletion completion{FrameCompletion::not_sealed};
  std::uint64_t imaging_arrivals{0};
  std::uint64_t calibration_and_imaging_arrivals{0};
  std::vector<byte> bytes;
};

CartesianFrameSlotConfig
frame_slot_config(const std::uint32_t slot_id,
                  const IncompleteFramePolicy incomplete_policy = IncompleteFramePolicy::fail,
                  const DuplicateAcquisitionPolicy duplicate_policy = DuplicateAcquisitionPolicy::reject) {
  return {
    .slot_id = slot_id,
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

std::array<byte, 4U> payload(const std::uint8_t seed) {
  return {
    static_cast<byte>(seed),
    static_cast<byte>(seed + 1U),
    static_cast<byte>(seed + 2U),
    static_cast<byte>(seed + 3U),
  };
}

[[nodiscard]] ksj::base::Result<ksj::recon::TargetEnvelope>
target_envelope(const std::uint64_t max_frame_charged_bytes = 24U, const std::uint64_t max_samples_per_acquisition = 2U,
                const std::uint64_t max_active_channels = 1U, const std::uint64_t max_trajectory_dimensions = 0U) {
  return ksj::recon::TargetEnvelope::create({
    .max_xml_bytes = 1U,
    .max_frame_charged_bytes = max_frame_charged_bytes,
    .max_image_charged_bytes = 1U,
    .max_decoder_staging_bytes = 0U,
    .max_samples_per_acquisition = max_samples_per_acquisition,
    .max_trajectory_dimensions = max_trajectory_dimensions,
    .max_active_channels = max_active_channels,
    .max_channel_groups = 1U,
    .max_dynamic_keys_per_scan = 1U,
    .max_active_scans = 1U,
    .calibration_horizon_items = 0U,
    .calibration_horizon_charged_bytes = 0U,
    .arrival_envelope = {.max_acquisitions_per_second = 1U, .max_burst_acquisitions = 1U},
    .sink_service_assumption = {},
  });
}

FrameSlotContext context(const std::uint64_t order_key, const std::uint16_t slice = 0U) {
  return {
    .semantic_key = {.encoding_space = 1U, .slice = slice, .contrast = 2U, .repetition = 3U},
    .order_key = order_key,
    .placement_key = order_key + 100U,
  };
}

NormalizedCartesianAcquisitionFrame frame(const FrameSlotContext frame_context, const std::uint32_t phase_encode_1,
                                          const std::uint32_t phase_encode_2, const std::array<byte, 4U>& bytes,
                                          const ksj::recon::runtime::NormalizedAcquisitionFlags flags = {}) {
  return {
    .classification_input = {.flags = flags},
    .frame_context = frame_context,
    .coordinate = {.phase_encode_1 = phase_encode_1, .phase_encode_2 = phase_encode_2},
    .payload = ksj::base::ConstByteSpan{bytes.data(), bytes.size()},
  };
}

SerialCartesianPipelineConfig pipeline_config(std::vector<ObservedFrame>& observed,
                                              std::vector<CartesianFrameSlotConfig> slots,
                                              const std::size_t max_terminal_records = 16U,
                                              std::optional<ksj::recon::TargetEnvelope> envelope = std::nullopt) {
  return {
    .target_envelope = std::move(envelope),
    .frame_slots = std::move(slots),
    .max_terminal_frame_records = max_terminal_records,
    .max_explicitly_ignored_records = 8U,
    .on_sealed_frame =
      [&observed](const SealedCartesianFrame& sealed) {
        observed.push_back({
          .order_key = sealed.context.order_key,
          .completion = sealed.completion,
          .imaging_arrivals = sealed.imaging_arrivals,
          .calibration_and_imaging_arrivals = sealed.calibration_and_imaging_arrivals,
          .bytes = {sealed.bytes.begin(), sealed.bytes.end()},
        });
        return ksj::base::Status::Ok();
      },
  };
}

TEST(KSpaceJetSerialCartesianPipeline, RejectsFrameSlotStorageOutsideTargetEnvelopeBeforeConstruction) {
  std::vector<ObservedFrame> observed;
  auto envelope = target_envelope(23U);
  ASSERT_TRUE(envelope.ok()) << envelope.status();

  auto rejected = SerialCartesianPipeline::create(
    pipeline_config(observed, {frame_slot_config(7U)}, 16U, std::move(envelope).value()));
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, rejected.status().code());
}

TEST(KSpaceJetSerialCartesianPipeline, EnforcesObservedTargetEnvelopeFactsBeforeClassificationOrSlotUse) {
  std::vector<ObservedFrame> observed;
  auto envelope = target_envelope();
  ASSERT_TRUE(envelope.ok()) << envelope.status();
  auto pipeline = SerialCartesianPipeline::create(
    pipeline_config(observed, {frame_slot_config(7U)}, 16U, std::move(envelope).value()));
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();
  ASSERT_TRUE(pipeline.value().start().ok());

  EXPECT_TRUE(
    pipeline.value()
      .validate_ingress(
        {.samples_per_acquisition = 2U, .active_channels = 1U, .trajectory_dimensions = 0U, .complete = true}, 4U)
      .ok());
  EXPECT_EQ(
    ksj::base::StatusCode::validation_error,
    pipeline.value()
      .validate_ingress(
        {.samples_per_acquisition = 3U, .active_channels = 1U, .trajectory_dimensions = 0U, .complete = true}, 4U)
      .code());
  EXPECT_EQ(
    ksj::base::StatusCode::validation_error,
    pipeline.value()
      .validate_ingress(
        {.samples_per_acquisition = 2U, .active_channels = 2U, .trajectory_dimensions = 0U, .complete = true}, 4U)
      .code());
  EXPECT_EQ(
    ksj::base::StatusCode::validation_error,
    pipeline.value()
      .validate_ingress(
        {.samples_per_acquisition = 2U, .active_channels = 1U, .trajectory_dimensions = 1U, .complete = true}, 4U)
      .code());
  EXPECT_EQ(
    ksj::base::StatusCode::validation_error,
    pipeline.value()
      .validate_ingress(
        {.samples_per_acquisition = 2U, .active_channels = 1U, .trajectory_dimensions = 0U, .complete = false}, 4U)
      .code());

  std::array<byte, 25U> oversized_payload{};
  const NormalizedCartesianAcquisitionFrame oversized{
    .classification_input = {},
    .frame_context = context(1U),
    .coordinate = {.phase_encode_1 = 0U, .phase_encode_2 = 0U},
    .ingress_facts = {.samples_per_acquisition = 2U,
                      .active_channels = 1U,
                      .trajectory_dimensions = 0U,
                      .complete = true},
    .payload = ksj::base::ConstByteSpan{oversized_payload.data(), oversized_payload.size()},
  };
  const auto submission = pipeline.value().submit(oversized);
  EXPECT_FALSE(submission.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, submission.status().code());
  EXPECT_EQ(ScanState::failed, pipeline.value().snapshot().state);
  EXPECT_EQ(0U, pipeline.value().snapshot().active_frames);
  EXPECT_EQ(0U, pipeline.value().snapshot().arrivals_by_lane.at(static_cast<std::size_t>(AcquisitionLane::imaging)));
  EXPECT_TRUE(observed.empty());
}

TEST(KSpaceJetSerialCartesianPipeline, RoutesOnlyImagingRecordsExplicitIgnoreAndCompletesNormalDrain) {
  std::vector<ObservedFrame> observed;
  auto pipeline = SerialCartesianPipeline::create(pipeline_config(observed, {frame_slot_config(7U)}));
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();
  ASSERT_TRUE(pipeline.value().start().ok());

  const auto ignored_payload = payload(1U);
  const auto ignored = pipeline.value().submit(frame(context(99U), 0U, 0U, ignored_payload, {.dummy_scan = true}));
  ASSERT_TRUE(ignored.ok()) << ignored.status();
  EXPECT_EQ(SerialIngressDisposition::recorded_explicitly_ignored, ignored.value().disposition);
  EXPECT_EQ(AcquisitionLane::ignored_explicitly, ignored.value().classification.lane);
  ASSERT_EQ(1U, pipeline.value().explicitly_ignored_records().size());
  EXPECT_EQ(4U, pipeline.value().explicitly_ignored_records().front().payload_bytes);

  const auto noise = pipeline.value().submit(frame(context(98U), 0U, 0U, ignored_payload, {.noise_measurement = true}));
  ASSERT_TRUE(noise.ok()) << noise.status();
  EXPECT_EQ(SerialIngressDisposition::classified_non_imaging, noise.value().disposition);
  EXPECT_EQ(AcquisitionLane::noise, noise.value().classification.lane);

  const auto first = payload(10U);
  const auto second = payload(20U);
  const auto third = payload(30U);
  const auto combined =
    pipeline.value().submit(frame(context(1U), 2U, 0U, first, {.parallel_calibration_and_imaging = true}));
  ASSERT_TRUE(combined.ok()) << combined.status();
  EXPECT_EQ(AcquisitionLane::calibration_and_imaging, combined.value().classification.lane);
  ASSERT_TRUE(pipeline.value().submit(frame(context(1U), 1U, 1U, second)).ok());
  EXPECT_TRUE(observed.empty());
  ASSERT_TRUE(pipeline.value().submit(frame(context(1U), 0U, 0U, third)).ok());

  ASSERT_EQ(1U, observed.size());
  EXPECT_EQ(1U, observed.front().order_key);
  EXPECT_EQ(FrameCompletion::complete, observed.front().completion);
  EXPECT_EQ(2U, observed.front().imaging_arrivals);
  EXPECT_EQ(1U, observed.front().calibration_and_imaging_arrivals);
  ASSERT_EQ(24U, observed.front().bytes.size());
  EXPECT_EQ(third[0], observed.front().bytes[0]);
  EXPECT_EQ(first[0], observed.front().bytes[8]);
  EXPECT_EQ(second[0], observed.front().bytes[16]);

  const auto running_snapshot = pipeline.value().snapshot();
  EXPECT_EQ(ScanState::running, running_snapshot.state);
  EXPECT_EQ(0U, running_snapshot.active_frames);
  EXPECT_EQ(1U, running_snapshot.terminal_frames);
  EXPECT_EQ(1U, running_snapshot.explicitly_ignored_records);
  EXPECT_EQ(1U, running_snapshot.arrivals_by_lane.at(static_cast<std::size_t>(AcquisitionLane::noise)));
  EXPECT_EQ(1U, running_snapshot.arrivals_by_lane.at(static_cast<std::size_t>(AcquisitionLane::ignored_explicitly)));

  ASSERT_TRUE(pipeline.value().end_of_input().ok());
  const auto completed_snapshot = pipeline.value().snapshot();
  EXPECT_EQ(ScanState::completed, completed_snapshot.state);
  EXPECT_EQ(TerminalCause::none, completed_snapshot.terminal_cause);
  EXPECT_EQ(1U, completed_snapshot.callbacks_completed);
}

TEST(KSpaceJetSerialCartesianPipeline, ResolvesPartialFailAndCertifiedSkipAtEndOfInput) {
  const auto line = payload(10U);

  std::vector<ObservedFrame> partial_observed;
  auto partial = SerialCartesianPipeline::create(
    pipeline_config(partial_observed, {frame_slot_config(7U, IncompleteFramePolicy::emit_partial)}));
  ASSERT_TRUE(partial.ok()) << partial.status();
  ASSERT_TRUE(partial.value().start().ok());
  ASSERT_TRUE(partial.value().submit(frame(context(1U), 0U, 0U, line)).ok());
  ASSERT_TRUE(partial.value().end_of_input().ok());
  ASSERT_EQ(1U, partial_observed.size());
  EXPECT_EQ(FrameCompletion::partial, partial_observed.front().completion);
  EXPECT_EQ(ScanState::completed, partial.value().snapshot().state);

  std::vector<ObservedFrame> skipped_observed;
  auto skipped = SerialCartesianPipeline::create(
    pipeline_config(skipped_observed, {frame_slot_config(7U, IncompleteFramePolicy::certified_skip)}));
  ASSERT_TRUE(skipped.ok()) << skipped.status();
  ASSERT_TRUE(skipped.value().start().ok());
  ASSERT_TRUE(skipped.value().submit(frame(context(1U), 0U, 0U, line)).ok());
  ASSERT_TRUE(skipped.value().end_of_input().ok());
  EXPECT_TRUE(skipped_observed.empty());
  ASSERT_EQ(1U, skipped.value().terminal_frame_records().size());
  EXPECT_EQ(FrameCompletion::certified_skip, skipped.value().terminal_frame_records().front().completion);
  EXPECT_FALSE(skipped.value().terminal_frame_records().front().delivered_to_callback);
  EXPECT_EQ(1U, skipped.value().snapshot().certified_skips);

  std::vector<ObservedFrame> failed_observed;
  auto failed = SerialCartesianPipeline::create(
    pipeline_config(failed_observed, {frame_slot_config(7U, IncompleteFramePolicy::fail)}));
  ASSERT_TRUE(failed.ok()) << failed.status();
  ASSERT_TRUE(failed.value().start().ok());
  ASSERT_TRUE(failed.value().submit(frame(context(1U), 0U, 0U, line)).ok());
  const auto end_status = failed.value().end_of_input();
  EXPECT_FALSE(end_status.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, end_status.code());
  EXPECT_TRUE(failed_observed.empty());
  EXPECT_EQ(ScanState::failed, failed.value().snapshot().state);
  EXPECT_EQ(TerminalCause::failure, failed.value().snapshot().terminal_cause);
}

TEST(KSpaceJetSerialCartesianPipeline, CancellationAndCallbackOrDuplicateFailureDoNotCompleteNormally) {
  const auto line = payload(10U);

  std::vector<ObservedFrame> cancelled_observed;
  auto cancelled = SerialCartesianPipeline::create(pipeline_config(cancelled_observed, {frame_slot_config(7U)}));
  ASSERT_TRUE(cancelled.ok()) << cancelled.status();
  ASSERT_TRUE(cancelled.value().start().ok());
  ASSERT_TRUE(cancelled.value().submit(frame(context(1U), 0U, 0U, line)).ok());
  ASSERT_TRUE(cancelled.value().cancel().ok());
  EXPECT_EQ(ScanState::cancelled, cancelled.value().snapshot().state);
  EXPECT_EQ(TerminalCause::cancel, cancelled.value().snapshot().terminal_cause);
  EXPECT_TRUE(cancelled_observed.empty());
  EXPECT_FALSE(cancelled.value().end_of_input().ok());

  auto duplicate_failure =
    SerialCartesianPipeline::create(pipeline_config(cancelled_observed, {frame_slot_config(8U)}));
  ASSERT_TRUE(duplicate_failure.ok()) << duplicate_failure.status();
  ASSERT_TRUE(duplicate_failure.value().start().ok());
  ASSERT_TRUE(duplicate_failure.value().submit(frame(context(2U), 0U, 0U, line)).ok());
  const auto duplicate = duplicate_failure.value().submit(frame(context(2U), 0U, 0U, line));
  EXPECT_FALSE(duplicate.ok());
  EXPECT_EQ(ksj::base::StatusCode::already_exists, duplicate.status().code());
  EXPECT_EQ(ScanState::failed, duplicate_failure.value().snapshot().state);

  auto callback_failure = SerialCartesianPipeline::create({
    .frame_slots = {frame_slot_config(9U)},
    .max_terminal_frame_records = 8U,
    .max_explicitly_ignored_records = 8U,
    .on_sealed_frame =
      [](const SealedCartesianFrame&) {
        return ksj::base::Status::InternalError("reference callback failure");
      },
  });
  ASSERT_TRUE(callback_failure.ok()) << callback_failure.status();
  ASSERT_TRUE(callback_failure.value().start().ok());
  ASSERT_TRUE(callback_failure.value().submit(frame(context(3U), 0U, 0U, line)).ok());
  ASSERT_TRUE(callback_failure.value().submit(frame(context(3U), 2U, 0U, line)).ok());
  const auto callback_status = callback_failure.value().submit(frame(context(3U), 1U, 1U, line));
  EXPECT_FALSE(callback_status.ok());
  EXPECT_EQ(ksj::base::StatusCode::internal_error, callback_status.status().code());
  EXPECT_EQ(ScanState::failed, callback_failure.value().snapshot().state);
  EXPECT_EQ(TerminalCause::failure, callback_failure.value().snapshot().terminal_cause);
}

TEST(KSpaceJetSerialCartesianPipeline, PreservesOrderAcrossSlotsReusesGenerationAndRejectsLateFrame) {
  std::vector<ObservedFrame> observed;
  auto pipeline =
    SerialCartesianPipeline::create(pipeline_config(observed, {frame_slot_config(7U), frame_slot_config(8U)}, 8U));
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();
  ASSERT_TRUE(pipeline.value().start().ok());

  const auto a = context(1U, 1U);
  const auto b = context(2U, 2U);
  const auto c = context(3U, 3U);
  const auto first = payload(10U);
  const auto second = payload(20U);
  const auto third = payload(30U);

  ASSERT_TRUE(pipeline.value().submit(frame(a, 0U, 0U, first)).ok());
  ASSERT_TRUE(pipeline.value().submit(frame(b, 0U, 0U, first)).ok());
  ASSERT_TRUE(pipeline.value().submit(frame(b, 2U, 0U, second)).ok());
  ASSERT_TRUE(pipeline.value().submit(frame(b, 1U, 1U, third)).ok());
  EXPECT_TRUE(observed.empty());
  EXPECT_EQ(2U, pipeline.value().snapshot().active_frames);

  ASSERT_TRUE(pipeline.value().submit(frame(a, 2U, 0U, second)).ok());
  ASSERT_TRUE(pipeline.value().submit(frame(a, 1U, 1U, third)).ok());
  ASSERT_EQ(2U, observed.size());
  EXPECT_EQ(1U, observed[0].order_key);
  EXPECT_EQ(2U, observed[1].order_key);

  ASSERT_TRUE(pipeline.value().submit(frame(c, 0U, 0U, first)).ok());
  ASSERT_TRUE(pipeline.value().submit(frame(c, 2U, 0U, second)).ok());
  ASSERT_TRUE(pipeline.value().submit(frame(c, 1U, 1U, third)).ok());
  ASSERT_EQ(3U, pipeline.value().terminal_frame_records().size());
  const auto& terminals = pipeline.value().terminal_frame_records();
  EXPECT_EQ(7U, terminals[0].token.slot_id);
  EXPECT_EQ(7U, terminals[2].token.slot_id);
  EXPECT_NE(terminals[0].token.generation, terminals[2].token.generation);

  const auto late = pipeline.value().submit(frame(a, 0U, 0U, first));
  EXPECT_FALSE(late.ok());
  EXPECT_EQ(ksj::base::StatusCode::state_error, late.status().code());
  EXPECT_EQ(ScanState::failed, pipeline.value().snapshot().state);
  EXPECT_EQ(TerminalCause::failure, pipeline.value().snapshot().terminal_cause);
}

TEST(KSpaceJetSerialCartesianPipeline, RequiresNondecreasingFirstSeenOrderKeysInM1) {
  std::vector<ObservedFrame> observed;
  auto pipeline =
    SerialCartesianPipeline::create(pipeline_config(observed, {frame_slot_config(7U), frame_slot_config(8U)}, 8U));
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();
  ASSERT_TRUE(pipeline.value().start().ok());

  const auto line = payload(10U);
  ASSERT_TRUE(pipeline.value().submit(frame(context(2U, 2U), 0U, 0U, line)).ok());

  const auto lower_order_key = pipeline.value().submit(frame(context(1U, 1U), 0U, 0U, line));
  EXPECT_FALSE(lower_order_key.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, lower_order_key.status().code());
  EXPECT_EQ(ScanState::failed, pipeline.value().snapshot().state);
  EXPECT_EQ(TerminalCause::failure, pipeline.value().snapshot().terminal_cause);
}

} // namespace
