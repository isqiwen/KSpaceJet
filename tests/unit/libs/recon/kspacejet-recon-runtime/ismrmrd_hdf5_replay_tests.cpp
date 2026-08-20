#include "kspacejet/recon/runtime/ismrmrd_hdf5_replay.hpp"

#include <ismrmrd/dataset.h>

#include <gtest/gtest.h>

#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ksj::recon::runtime::AcquisitionLane;
using ksj::recon::runtime::CartesianFrameSlotConfig;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::IncompleteFramePolicy;
using ksj::recon::runtime::IsmrmrdHdf5ReplayAcquisitionDescriptor;
using ksj::recon::runtime::ScanState;
using ksj::recon::runtime::SealedCartesianFrame;
using ksj::recon::runtime::SerialCartesianPipeline;
using ksj::recon::runtime::SerialCartesianPipelineConfig;
using ksj::recon::runtime::TerminalCause;

struct ObservedFrame {
  std::uint64_t order_key{0};
  std::uint64_t imaging_arrivals{0};
  std::uint64_t calibration_and_imaging_arrivals{0};
  std::size_t payload_bytes{0};
};

[[nodiscard]] std::filesystem::path make_test_dataset_path(const std::string_view filename) {
  const auto directory = std::filesystem::temp_directory_path() / "ksj_recon_runtime_tests";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  const auto path = directory / filename;
  std::filesystem::remove(path, error);
  return path;
}

void write_standard_ismrmrd_dataset(const std::filesystem::path& path, const std::uint16_t active_channels = 1U) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader("<ismrmrdHeader xmlns=\"http://www.ismrm.org/ISMRMRD\"><experimentalConditions>"
                      "<H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz>"
                      "</experimentalConditions></ismrmrdHeader>");

  const auto append = [&dataset](const ISMRMRD::ISMRMRD_AcquisitionFlags flag, const std::uint16_t slice,
                                 const float value, const std::uint16_t channels) {
    ISMRMRD::Acquisition acquisition(2U, channels, 0U);
    acquisition.encoding_space_ref() = 3U;
    acquisition.idx().kspace_encode_step_1 = 0U;
    acquisition.idx().kspace_encode_step_2 = 0U;
    acquisition.idx().slice = slice;
    acquisition.idx().contrast = 5U;
    acquisition.idx().repetition = 7U;
    acquisition.setFlag(flag);
    for (std::uint16_t channel = 0U; channel < channels; ++channel) {
      const auto channel_value = value + static_cast<float>(channel * 10U);
      acquisition.data(0U, channel) = {channel_value, channel_value + 0.5F};
      acquisition.data(1U, channel) = {channel_value + 1.0F, channel_value + 1.5F};
    }
    dataset.appendAcquisition(acquisition);
  };

  append(ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION_AND_IMAGING, 11U, 10.0F, active_channels);
  append(ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT, 12U, 20.0F, active_channels);
  append(ISMRMRD::ISMRMRD_ACQ_IS_DUMMYSCAN_DATA, 13U, 30.0F, active_channels);
}

[[nodiscard]] ksj::base::Result<ksj::recon::TargetEnvelope> target_envelope() {
  return ksj::recon::TargetEnvelope::create({
    .max_xml_bytes = 1U,
    .max_frame_charged_bytes = 16U,
    .max_image_charged_bytes = 16U,
    .max_decoder_staging_bytes = 0U,
    .max_samples_per_acquisition = 2U,
    .max_trajectory_dimensions = 0U,
    .max_active_channels = 1U,
    .max_channel_groups = 1U,
    .max_dynamic_keys_per_scan = 1U,
    .max_active_scans = 1U,
    .calibration_horizon_items = 0U,
    .calibration_horizon_charged_bytes = 0U,
    .arrival_envelope = {.max_acquisitions_per_second = 1U, .max_burst_acquisitions = 1U},
    .sink_service_assumption = {},
  });
}

[[nodiscard]] CartesianFrameSlotConfig frame_slot_config() {
  return {
    .slot_id = 31U,
    .dimensions =
      {
        .readout_samples = 2U,
        .phase_encode_1 = 1U,
        .phase_encode_2 = 1U,
        .channels = 1U,
        .bytes_per_sample = static_cast<std::uint32_t>(sizeof(std::complex<float>)),
      },
    .completion = {.required_indices = {{.phase_encode_1 = 0U, .phase_encode_2 = 0U}}},
    .resource_upper_bound =
      {
        .max_total_arrivals = 1U,
        .max_duplicate_arrivals = 0U,
        .max_payload_bytes = 2U * sizeof(std::complex<float>),
      },
    .incomplete_policy = IncompleteFramePolicy::fail,
  };
}

[[nodiscard]] SerialCartesianPipelineConfig
pipeline_config(std::vector<ObservedFrame>& observed,
                std::optional<ksj::recon::TargetEnvelope> envelope = std::nullopt) {
  return {
    .target_envelope = std::move(envelope),
    .frame_slots = {frame_slot_config()},
    .max_terminal_frame_records = 4U,
    .max_explicitly_ignored_records = 4U,
    .on_sealed_frame =
      [&observed](const SealedCartesianFrame& frame) {
        observed.push_back({
          .order_key = frame.context.order_key,
          .imaging_arrivals = frame.imaging_arrivals,
          .calibration_and_imaging_arrivals = frame.calibration_and_imaging_arrivals,
          .payload_bytes = frame.bytes.size(),
        });
        return ksj::base::Status::Ok();
      },
  };
}

[[nodiscard]] FrameSlotContext context_for(const IsmrmrdHdf5ReplayAcquisitionDescriptor& descriptor) {
  return {
    .semantic_key =
      {
        .encoding_space = descriptor.classification_input.index.encoding_space,
        .slice = descriptor.classification_input.index.slice,
        .contrast = descriptor.classification_input.index.contrast,
        .repetition = descriptor.classification_input.index.repetition,
        .set = descriptor.classification_input.index.set,
        .phase = descriptor.classification_input.index.phase,
        .average = descriptor.classification_input.index.average,
      },
    // The caller/compiled plan supplies the identity and its monotonic order.
    // This test admits one imaging frame, so one fixed plan-approved order key
    // is sufficient without encoding scanner task ordering in the adapter.
    .order_key = 41U,
    .placement_key = 17U,
  };
}

TEST(KSpaceJetIsmrmrdHdf5Replay, ReplaysStandardHdf5AndNormalizesFlagsBeforeSerialAssembly) {
  const auto path = make_test_dataset_path("serial_replay.h5");
  write_standard_ismrmrd_dataset(path);

  std::vector<ObservedFrame> observed;
  auto pipeline = SerialCartesianPipeline::create(pipeline_config(observed));
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();

  std::vector<IsmrmrdHdf5ReplayAcquisitionDescriptor> resolved;
  const auto replay = ksj::recon::runtime::replay_ismrmrd_hdf5(
    {
      .input_file = path,
      .dataset_group = "dataset",
      .resolve_frame_context =
        [&resolved](const IsmrmrdHdf5ReplayAcquisitionDescriptor& descriptor) {
          resolved.push_back(descriptor);
          return ksj::base::Result<FrameSlotContext>{context_for(descriptor)};
        },
    },
    pipeline.value());
  ASSERT_TRUE(replay.ok()) << replay.status();

  EXPECT_EQ(3U, replay.value().declared_acquisitions);
  EXPECT_EQ(3U, replay.value().acquisitions_read);
  EXPECT_EQ(1U, replay.value().routed_to_frame_slots);
  EXPECT_EQ(1U, replay.value().classified_non_imaging);
  EXPECT_EQ(1U, replay.value().explicitly_ignored);

  ASSERT_EQ(3U, resolved.size());
  EXPECT_TRUE(resolved[0].classification_input.flags.parallel_calibration_and_imaging);
  EXPECT_FALSE(resolved[0].classification_input.flags.parallel_calibration);
  EXPECT_EQ(3U, resolved[0].classification_input.index.encoding_space);
  EXPECT_EQ(11U, resolved[0].classification_input.index.slice);
  EXPECT_EQ(0U, resolved[0].coordinate.phase_encode_1);
  EXPECT_EQ(0U, resolved[0].coordinate.phase_encode_2);
  EXPECT_EQ(2U, resolved[0].number_of_samples);
  EXPECT_EQ(1U, resolved[0].active_channels);
  EXPECT_EQ(0U, resolved[0].trajectory_dimensions);
  EXPECT_TRUE(resolved[1].classification_input.flags.noise_measurement);
  EXPECT_TRUE(resolved[2].classification_input.flags.dummy_scan);

  ASSERT_EQ(1U, observed.size());
  EXPECT_EQ(41U, observed.front().order_key);
  EXPECT_EQ(0U, observed.front().imaging_arrivals);
  EXPECT_EQ(1U, observed.front().calibration_and_imaging_arrivals);
  EXPECT_EQ(2U * sizeof(std::complex<float>), observed.front().payload_bytes);

  const auto snapshot = pipeline.value().snapshot();
  EXPECT_EQ(ScanState::completed, snapshot.state);
  EXPECT_EQ(TerminalCause::none, snapshot.terminal_cause);
  EXPECT_EQ(1U, snapshot.arrivals_by_lane.at(static_cast<std::size_t>(AcquisitionLane::calibration_and_imaging)));
  EXPECT_EQ(1U, snapshot.arrivals_by_lane.at(static_cast<std::size_t>(AcquisitionLane::noise)));
  EXPECT_EQ(1U, snapshot.arrivals_by_lane.at(static_cast<std::size_t>(AcquisitionLane::ignored_explicitly)));
  ASSERT_EQ(1U, pipeline.value().explicitly_ignored_records().size());
  EXPECT_EQ(AcquisitionLane::ignored_explicitly,
            pipeline.value().explicitly_ignored_records().front().classification.lane);
  EXPECT_EQ(13U, pipeline.value().explicitly_ignored_records().front().classification.index.slice);
}

TEST(KSpaceJetIsmrmrdHdf5Replay, RejectsActualEnvelopeViolationBeforeFrameContextResolution) {
  const auto path = make_test_dataset_path("envelope_violation_serial_replay.h5");
  write_standard_ismrmrd_dataset(path, 2U);

  std::vector<ObservedFrame> observed;
  auto envelope = target_envelope();
  ASSERT_TRUE(envelope.ok()) << envelope.status();
  auto pipeline = SerialCartesianPipeline::create(pipeline_config(observed, std::move(envelope).value()));
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();

  bool resolver_called = false;
  const auto replay = ksj::recon::runtime::replay_ismrmrd_hdf5(
    {
      .input_file = path,
      .dataset_group = "dataset",
      .resolve_frame_context =
        [&resolver_called](const IsmrmrdHdf5ReplayAcquisitionDescriptor& descriptor) {
          resolver_called = true;
          return ksj::base::Result<FrameSlotContext>{context_for(descriptor)};
        },
    },
    pipeline.value());

  EXPECT_FALSE(replay.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, replay.status().code());
  EXPECT_FALSE(resolver_called);
  EXPECT_EQ(ScanState::failed, pipeline.value().snapshot().state);
  EXPECT_EQ(TerminalCause::failure, pipeline.value().snapshot().terminal_cause);
  EXPECT_TRUE(observed.empty());
}

TEST(KSpaceJetIsmrmrdHdf5Replay, ReaderOpenFailureOccursBeforePipelineAdmission) {
  std::vector<ObservedFrame> observed;
  auto pipeline = SerialCartesianPipeline::create(pipeline_config(observed));
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();

  const auto missing_path = make_test_dataset_path("does_not_exist.h5");
  const auto replay = ksj::recon::runtime::replay_ismrmrd_hdf5(
    {
      .input_file = missing_path,
      .dataset_group = "dataset",
      .resolve_frame_context =
        [](const IsmrmrdHdf5ReplayAcquisitionDescriptor& descriptor) {
          return ksj::base::Result<FrameSlotContext>{context_for(descriptor)};
        },
    },
    pipeline.value());

  EXPECT_FALSE(replay.ok());
  EXPECT_EQ(ksj::base::StatusCode::io_error, replay.status().code());
  EXPECT_EQ(ScanState::input_candidate, pipeline.value().snapshot().state);
  EXPECT_EQ(TerminalCause::none, pipeline.value().snapshot().terminal_cause);
  EXPECT_TRUE(observed.empty());
}

TEST(KSpaceJetIsmrmrdHdf5Replay, ResolverFailureStopsTraversalAndFailsStartedPipeline) {
  const auto path = make_test_dataset_path("resolver_failure_serial_replay.h5");
  write_standard_ismrmrd_dataset(path);

  std::vector<ObservedFrame> observed;
  auto pipeline = SerialCartesianPipeline::create(pipeline_config(observed));
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();

  const auto replay = ksj::recon::runtime::replay_ismrmrd_hdf5(
    {
      .input_file = path,
      .dataset_group = "dataset",
      .resolve_frame_context =
        [](const IsmrmrdHdf5ReplayAcquisitionDescriptor&) {
          return ksj::base::Result<FrameSlotContext>{
            ksj::base::Status::ValidationError("compiled plan rejected replay acquisition")};
        },
    },
    pipeline.value());

  EXPECT_FALSE(replay.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, replay.status().code());
  EXPECT_EQ(ScanState::failed, pipeline.value().snapshot().state);
  EXPECT_EQ(TerminalCause::failure, pipeline.value().snapshot().terminal_cause);
  EXPECT_TRUE(observed.empty());
}

TEST(KSpaceJetIsmrmrdHdf5Replay, StopRequestTurnsReaderInterruptionIntoCancellationWithoutEndOfInput) {
  const auto path = make_test_dataset_path("cancelled_serial_replay.h5");
  write_standard_ismrmrd_dataset(path);

  std::vector<ObservedFrame> observed;
  auto pipeline = SerialCartesianPipeline::create(pipeline_config(observed));
  ASSERT_TRUE(pipeline.ok()) << pipeline.status();

  std::stop_source stop_source;
  const auto replay = ksj::recon::runtime::replay_ismrmrd_hdf5(
    {
      .input_file = path,
      .dataset_group = "dataset",
      .resolve_frame_context =
        [&stop_source](const IsmrmrdHdf5ReplayAcquisitionDescriptor& descriptor) {
          const auto context = context_for(descriptor);
          if (descriptor.acquisition_ordinal == 0U) {
            stop_source.request_stop();
          }
          return ksj::base::Result<FrameSlotContext>{context};
        },
      .stop_token = stop_source.get_token(),
    },
    pipeline.value());

  EXPECT_FALSE(replay.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, replay.status().code());
  EXPECT_EQ(ScanState::cancelled, pipeline.value().snapshot().state);
  EXPECT_EQ(TerminalCause::cancel, pipeline.value().snapshot().terminal_cause);
  EXPECT_EQ(1U, observed.size());
}

} // namespace
