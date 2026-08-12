#include "kspacejet/recon/runtime/ismrmrd_hdf5_replay.hpp"

#include "kspacejet/ismrmrd/dataset_reader.hpp"

#include <ismrmrd/ismrmrd.h>

#include <exception>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace ksj::recon::runtime {
namespace {

[[nodiscard]] bool is_set(const std::uint64_t flags, const ISMRMRD::ISMRMRD_AcquisitionFlags flag) noexcept {
  return ISMRMRD::FlagBit{static_cast<unsigned short>(flag)}.isSet(flags);
}

[[nodiscard]] AcquisitionClassificationInput
normalize_classification_input(const ksj::ismrmrd::AcquisitionHeader& header) noexcept {
  return {
    .message_kind = PublicMrdMessageKind::acquisition,
    .flags =
      {
        .noise_measurement = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT),
        .parallel_calibration = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION),
        .parallel_calibration_and_imaging =
          is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION_AND_IMAGING),
        .phase_correction = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_PHASECORR_DATA),
        .navigation = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_NAVIGATION_DATA),
        .dummy_scan = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_DUMMYSCAN_DATA),
        .explicitly_ignored = false,
      },
    .index =
      {
        .encoding_space = header.encoding_space_ref,
        .kspace_encode_step_1 = header.index.kspace_encode_step_1,
        .kspace_encode_step_2 = header.index.kspace_encode_step_2,
        .average = header.index.average,
        .slice = header.index.slice,
        .contrast = header.index.contrast,
        .phase = header.index.phase,
        .repetition = header.index.repetition,
        .set = header.index.set,
        .segment = header.index.segment,
      },
  };
}

[[nodiscard]] CartesianLineCoordinate
normalize_cartesian_coordinate(const ksj::ismrmrd::AcquisitionHeader& header) noexcept {
  return {
    .phase_encode_1 = header.index.kspace_encode_step_1,
    .phase_encode_2 = header.index.kspace_encode_step_2,
  };
}

[[nodiscard]] bool product_matches_size(const std::uint64_t lhs, const std::uint64_t rhs,
                                        const std::size_t observed_size) noexcept {
  if (lhs != 0U && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
    return false;
  }
  const auto expected = lhs * rhs;
  if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
    if (expected > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      return false;
    }
  }
  return static_cast<std::size_t>(expected) == observed_size;
}

[[nodiscard]] ksj::base::Status validate_public_acquisition_layout(const ksj::ismrmrd::AcquisitionView& acquisition) {
  const auto& header = acquisition.header;
  if (!product_matches_size(header.number_of_samples, header.active_channels, acquisition.samples.size())) {
    return ksj::base::Status::ValidationError(
      "ISMRMRD HDF5 acquisition samples do not match header number_of_samples * active_channels");
  }
  if (!product_matches_size(header.number_of_samples, header.trajectory_dimensions, acquisition.trajectory.size())) {
    return ksj::base::Status::ValidationError(
      "ISMRMRD HDF5 acquisition trajectory does not match header number_of_samples * trajectory_dimensions");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status replay_io_error(const std::string& message) {
  return ksj::base::Status::IoError("ISMRMRD HDF5 replay failed: " + message);
}

void fail_pipeline(SerialCartesianPipeline& pipeline, const ksj::base::Status& cause) {
  const auto state = pipeline.snapshot().state;
  if (state == ScanState::completed || state == ScanState::rejected || state == ScanState::cancelled ||
      state == ScanState::failed) {
    return;
  }
  static_cast<void>(pipeline.fail(cause));
}

[[nodiscard]] ksj::base::Status cancel_pipeline_after_reader_stop(SerialCartesianPipeline& pipeline) {
  const auto state = pipeline.snapshot().state;
  if (state == ScanState::cancelled) {
    return ksj::base::Status::Unavailable("ISMRMRD HDF5 replay stopped before EndOfInput");
  }
  if (state == ScanState::failed) {
    return pipeline.snapshot().last_error;
  }
  const auto cancel_status = pipeline.cancel();
  if (!cancel_status.ok()) {
    return cancel_status;
  }
  return ksj::base::Status::Unavailable("ISMRMRD HDF5 replay stopped before EndOfInput");
}

} // namespace

ksj::base::Result<IsmrmrdHdf5ReplayReport> replay_ismrmrd_hdf5(const IsmrmrdHdf5ReplayConfig& config,
                                                               SerialCartesianPipeline& pipeline) {
  if (config.input_file.empty()) {
    return ksj::base::Status::InvalidArgument("ISMRMRD HDF5 replay input_file must not be empty");
  }
  if (config.dataset_group.empty()) {
    return ksj::base::Status::InvalidArgument("ISMRMRD HDF5 replay dataset_group must not be empty");
  }
  if (!config.resolve_frame_context) {
    return ksj::base::Status::InvalidArgument(
      "ISMRMRD HDF5 replay requires a deterministic FrameSlotContext resolver from the compiled plan or caller");
  }

  ksj::ismrmrd::DatasetReader reader;
  std::string reader_error;
  if (!reader.open(config.input_file, config.dataset_group, reader_error)) {
    return replay_io_error(reader_error);
  }
  if (config.stop_token.stop_requested()) {
    return cancel_pipeline_after_reader_stop(pipeline);
  }

  const auto start_status = pipeline.start();
  if (!start_status.ok()) {
    return start_status;
  }

  IsmrmrdHdf5ReplayReport report{.declared_acquisitions = reader.metadata().acquisition_count};
  ksj::base::Status callback_error = ksj::base::Status::Ok();

  const auto iteration = reader.for_each_acquisition(
    [&](const ksj::ismrmrd::AcquisitionView& acquisition) {
      try {
        if (config.stop_token.stop_requested()) {
          return false;
        }
        const auto classification_input = normalize_classification_input(acquisition.header);
        const auto layout_status = validate_public_acquisition_layout(acquisition);
        if (!layout_status.ok()) {
          callback_error = layout_status;
          fail_pipeline(pipeline, callback_error);
          return false;
        }

        // `std::as_bytes` creates a non-owning view over DatasetReader's
        // borrowed complex<float> samples.  The envelope validation below
        // occurs before the resolver can create per-frame runtime state, and
        // submit() validates it again before classification or a payload copy.
        const auto sample_bytes = std::as_bytes(acquisition.samples);
        const NormalizedAcquisitionIngressFacts ingress_facts{
          .samples_per_acquisition = acquisition.header.number_of_samples,
          .active_channels = acquisition.header.active_channels,
          .trajectory_dimensions = acquisition.header.trajectory_dimensions,
          .complete = true,
        };
        const auto envelope_status = pipeline.validate_ingress(ingress_facts, sample_bytes.size());
        if (!envelope_status.ok()) {
          callback_error = envelope_status;
          fail_pipeline(pipeline, callback_error);
          return false;
        }

        const IsmrmrdHdf5ReplayAcquisitionDescriptor descriptor{
          .acquisition_ordinal = report.acquisitions_read,
          .classification_input = classification_input,
          .coordinate = normalize_cartesian_coordinate(acquisition.header),
          .number_of_samples = acquisition.header.number_of_samples,
          .active_channels = acquisition.header.active_channels,
          .trajectory_dimensions = acquisition.header.trajectory_dimensions,
        };

        auto context = config.resolve_frame_context(descriptor);
        if (!context.ok()) {
          callback_error = context.status();
          fail_pipeline(pipeline, callback_error);
          return false;
        }

        // submit() copies the byte view before this callback returns; neither
        // the byte view nor AcquisitionView escapes.
        const NormalizedCartesianAcquisitionFrame frame{
          .classification_input = classification_input,
          .frame_context = context.value(),
          .coordinate = descriptor.coordinate,
          .ingress_facts = ingress_facts,
          .payload = ksj::base::ConstByteSpan{sample_bytes.data(), sample_bytes.size()},
        };
        auto receipt = pipeline.submit(frame);
        if (!receipt.ok()) {
          callback_error = receipt.status();
          fail_pipeline(pipeline, callback_error);
          return false;
        }

        ++report.acquisitions_read;
        switch (receipt.value().disposition) {
          case SerialIngressDisposition::routed_to_frame_slot:
            ++report.routed_to_frame_slots;
            break;
          case SerialIngressDisposition::classified_non_imaging:
            ++report.classified_non_imaging;
            break;
          case SerialIngressDisposition::recorded_explicitly_ignored:
            ++report.explicitly_ignored;
            break;
        }
        return true;
      } catch (const std::exception& exception) {
        callback_error =
          ksj::base::Status::InternalError(std::string("ISMRMRD HDF5 replay callback threw: ") + exception.what());
      } catch (...) {
        callback_error = ksj::base::Status::InternalError("ISMRMRD HDF5 replay callback threw");
      }
      fail_pipeline(pipeline, callback_error);
      return false;
    },
    reader_error);

  switch (iteration) {
    case ksj::ismrmrd::AcquisitionIterationResult::completed:
      {
        const auto end_status = pipeline.end_of_input();
        if (!end_status.ok()) {
          return end_status;
        }
        return report;
      }
    case ksj::ismrmrd::AcquisitionIterationResult::stopped:
      if (!callback_error.ok()) {
        return callback_error;
      }
      return cancel_pipeline_after_reader_stop(pipeline);
    case ksj::ismrmrd::AcquisitionIterationResult::failed:
      {
        const auto status = replay_io_error(reader_error);
        fail_pipeline(pipeline, status);
        return status;
      }
  }

  const auto status = ksj::base::Status::InternalError("ISMRMRD reader returned an unknown iteration result");
  fail_pipeline(pipeline, status);
  return status;
}

} // namespace ksj::recon::runtime
