#include "kspacejet/recon/runtime/ismrmrd_hdf5_replay.hpp"

#include "kspacejet/ismrmrd/dataset_reader.hpp"
#include "kspacejet/recon/runtime/ismrmrd_semantic_ingress.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace ksj::recon::runtime {
struct IsmrmrdHdf5ReplaySession::State {
  State(ksj::ismrmrd::DatasetReader source_reader, const std::stop_token source_stop_token)
      : reader(std::move(source_reader)), stop_token(source_stop_token) {
    const auto& source_metadata = reader.metadata();
    metadata = {
      .dataset_group = source_metadata.group,
      .xml_header = source_metadata.xml_header,
      .declared_acquisitions = source_metadata.acquisition_count,
    };
  }

  ksj::ismrmrd::DatasetReader reader;
  IsmrmrdHdf5ReplaySourceMetadata metadata;
  std::stop_token stop_token{};
  bool iterated{false};
};

namespace {

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

IsmrmrdHdf5ReplaySource::IsmrmrdHdf5ReplaySource(IsmrmrdHdf5ReplaySourceConfig config) : config_(std::move(config)) {}

IsmrmrdHdf5ReplaySession::IsmrmrdHdf5ReplaySession(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

IsmrmrdHdf5ReplaySession::IsmrmrdHdf5ReplaySession(IsmrmrdHdf5ReplaySession&&) noexcept = default;
IsmrmrdHdf5ReplaySession& IsmrmrdHdf5ReplaySession::operator=(IsmrmrdHdf5ReplaySession&&) noexcept = default;
IsmrmrdHdf5ReplaySession::~IsmrmrdHdf5ReplaySession() = default;

const IsmrmrdHdf5ReplaySourceMetadata& IsmrmrdHdf5ReplaySession::metadata() const noexcept {
  static const IsmrmrdHdf5ReplaySourceMetadata empty_metadata{};
  return state_ == nullptr ? empty_metadata : state_->metadata;
}

ksj::base::Result<IsmrmrdHdf5ReplayIterationResult>
IsmrmrdHdf5ReplaySession::for_each_acquisition(const IsmrmrdHdf5ReplayAcquisitionConsumer& consumer) {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("ISMRMRD HDF5 replay session is not open");
  }
  if (!consumer) {
    return ksj::base::Status::InvalidArgument("ISMRMRD HDF5 replay acquisition consumer must not be empty");
  }
  if (state_->iterated) {
    return ksj::base::Status::StateError("ISMRMRD HDF5 replay session supports one acquisition pass");
  }
  state_->iterated = true;
  if (state_->stop_token.stop_requested()) {
    return IsmrmrdHdf5ReplayIterationResult::stopped;
  }

  ksj::base::Status callback_error = ksj::base::Status::Ok();
  std::string reader_error;
  const auto iteration = state_->reader.for_each_acquisition(
    [&](const ksj::ismrmrd::AcquisitionView& acquisition) {
      if (state_->stop_token.stop_requested()) {
        return false;
      }
      try {
        return consumer(acquisition);
      } catch (const std::exception& exception) {
        callback_error = ksj::base::Status::InternalError(
          std::string("ISMRMRD HDF5 replay acquisition consumer threw: ") + exception.what());
      } catch (...) {
        callback_error = ksj::base::Status::InternalError("ISMRMRD HDF5 replay acquisition consumer threw");
      }
      return false;
    },
    reader_error);
  if (!callback_error.ok()) {
    return callback_error;
  }

  switch (iteration) {
    case ksj::ismrmrd::AcquisitionIterationResult::completed:
      return IsmrmrdHdf5ReplayIterationResult::completed;
    case ksj::ismrmrd::AcquisitionIterationResult::stopped:
      return IsmrmrdHdf5ReplayIterationResult::stopped;
    case ksj::ismrmrd::AcquisitionIterationResult::failed:
      return ksj::base::Status::IoError(reader_error);
  }
  return ksj::base::Status::InternalError("ISMRMRD reader returned an unknown iteration result");
}

ksj::base::Result<IsmrmrdHdf5ReplaySession> IsmrmrdHdf5ReplaySource::open() const {
  if (config_.input_file.empty()) {
    return ksj::base::Status::InvalidArgument("ISMRMRD HDF5 replay input_file must not be empty");
  }
  if (config_.dataset_group.empty()) {
    return ksj::base::Status::InvalidArgument("ISMRMRD HDF5 replay dataset_group must not be empty");
  }
  ksj::ismrmrd::DatasetReader reader;
  std::string reader_error;
  if (!reader.open(config_.input_file, config_.dataset_group, reader_error)) {
    return ksj::base::Status::IoError(reader_error);
  }
  try {
    return IsmrmrdHdf5ReplaySession{
      std::make_unique<IsmrmrdHdf5ReplaySession::State>(std::move(reader), config_.stop_token)};
  } catch (const std::exception& exception) {
    return ksj::base::Status::InternalError(std::string("Unable to create ISMRMRD HDF5 replay session: ") +
                                            exception.what());
  } catch (...) {
    return ksj::base::Status::InternalError("Unable to create ISMRMRD HDF5 replay session");
  }
}

ksj::base::Result<IsmrmrdHdf5ReplaySourceReport>
IsmrmrdHdf5ReplaySource::replay_into(SerialCartesianPipeline& pipeline) const {
  if (config_.input_file.empty()) {
    return ksj::base::Status::InvalidArgument("ISMRMRD HDF5 replay input_file must not be empty");
  }
  if (config_.dataset_group.empty()) {
    return ksj::base::Status::InvalidArgument("ISMRMRD HDF5 replay dataset_group must not be empty");
  }
  if (!config_.resolve_frame_slot_binding) {
    return ksj::base::Status::InvalidArgument("ISMRMRD HDF5 replay requires a deterministic FrameSlotContext binding "
                                              "resolver from the compiled plan or caller");
  }

  auto opened = open();
  if (!opened.ok()) {
    return replay_io_error(opened.status().message());
  }
  auto session = std::move(opened).value();
  if (config_.stop_token.stop_requested()) {
    return cancel_pipeline_after_reader_stop(pipeline);
  }

  const auto start_status = pipeline.start();
  if (!start_status.ok()) {
    return start_status;
  }

  IsmrmrdHdf5ReplaySourceReport report{.declared_acquisitions = session.metadata().declared_acquisitions};
  ksj::base::Status callback_error = ksj::base::Status::Ok();

  const auto iteration = session.for_each_acquisition([&](const ksj::ismrmrd::AcquisitionView& acquisition) {
    try {
      if (config_.stop_token.stop_requested()) {
        return false;
      }
      const auto normalized = normalize_ismrmrd_acquisition(acquisition, pipeline.acquisition_classifier());
      if (!normalized.ok()) {
        callback_error = normalized.status();
        fail_pipeline(pipeline, callback_error);
        return false;
      }
      const auto& normalized_acquisition = normalized.value();

      // The shared normalizer creates a non-owning byte view over
      // DatasetReader's borrowed samples. Validation occurs before the
      // resolver can create per-frame state, and submit() copies the view
      // synchronously before this callback returns.
      const auto envelope_status =
        pipeline.validate_ingress(normalized_acquisition.ingress_facts, normalized_acquisition.sample_bytes.size());
      if (!envelope_status.ok()) {
        callback_error = envelope_status;
        fail_pipeline(pipeline, callback_error);
        return false;
      }

      const IsmrmrdHdf5ReplayAcquisitionDescriptor descriptor{
        .acquisition_ordinal = report.acquisitions_read,
        .classification_input = normalized_acquisition.classification_input,
        .classification = normalized_acquisition.classification,
        .frame_key = normalized_acquisition.frame_key,
        .coordinate = normalized_acquisition.cartesian_coordinate,
        .number_of_samples = acquisition.header.number_of_samples,
        .active_channels = acquisition.header.active_channels,
        .trajectory_dimensions = acquisition.header.trajectory_dimensions,
        .control_flags = normalized_acquisition.control_flags,
        .ingress_facts = normalized_acquisition.ingress_facts,
      };

      auto binding = config_.resolve_frame_slot_binding(descriptor);
      if (!binding.ok()) {
        callback_error = binding.status();
        fail_pipeline(pipeline, callback_error);
        return false;
      }
      const auto context = make_ismrmrd_frame_slot_context(normalized_acquisition, binding.value());

      // submit() copies the byte view before this callback returns; neither
      // the byte view nor AcquisitionView escapes.
      const NormalizedCartesianAcquisitionFrame frame{
        .classification_input = normalized_acquisition.classification_input,
        .frame_context = context,
        .coordinate = descriptor.coordinate,
        .ingress_facts = normalized_acquisition.ingress_facts,
        .payload = normalized_acquisition.sample_bytes,
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
  });

  if (!iteration.ok()) {
    const auto status = iteration.status().code() == ksj::base::StatusCode::io_error
                          ? replay_io_error(iteration.status().message())
                          : iteration.status();
    fail_pipeline(pipeline, status);
    return status;
  }

  switch (iteration.value()) {
    case IsmrmrdHdf5ReplayIterationResult::completed:
      {
        const auto end_status = pipeline.end_of_input();
        if (!end_status.ok()) {
          return end_status;
        }
        return report;
      }
    case IsmrmrdHdf5ReplayIterationResult::stopped:
      if (!callback_error.ok()) {
        return callback_error;
      }
      return cancel_pipeline_after_reader_stop(pipeline);
  }

  const auto status = ksj::base::Status::InternalError("ISMRMRD reader returned an unknown iteration result");
  fail_pipeline(pipeline, status);
  return status;
}

} // namespace ksj::recon::runtime
