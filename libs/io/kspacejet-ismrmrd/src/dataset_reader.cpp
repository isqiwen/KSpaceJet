#include "kspacejet/ismrmrd/dataset_reader.hpp"

#include <ismrmrd/dataset.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace ksj::ismrmrd {
namespace {

[[nodiscard]] EncodingCounters copy_encoding_counters(const ISMRMRD::EncodingCounters& source) {
  EncodingCounters result;
  result.kspace_encode_step_1 = source.kspace_encode_step_1;
  result.kspace_encode_step_2 = source.kspace_encode_step_2;
  result.average = source.average;
  result.slice = source.slice;
  result.contrast = source.contrast;
  result.phase = source.phase;
  result.repetition = source.repetition;
  result.set = source.set;
  result.segment = source.segment;
  std::copy(std::begin(source.user), std::end(source.user), result.user.begin());
  return result;
}

[[nodiscard]] AcquisitionHeader copy_header(const ISMRMRD::AcquisitionHeader& source) {
  AcquisitionHeader result;
  result.version = source.version;
  result.flags = source.flags;
  result.measurement_uid = source.measurement_uid;
  result.scan_counter = source.scan_counter;
  result.acquisition_time_stamp = source.acquisition_time_stamp;
  std::copy(std::begin(source.physiology_time_stamp), std::end(source.physiology_time_stamp),
            result.physiology_time_stamp.begin());
  result.number_of_samples = source.number_of_samples;
  result.available_channels = source.available_channels;
  result.active_channels = source.active_channels;
  std::copy(std::begin(source.channel_mask), std::end(source.channel_mask), result.channel_mask.begin());
  result.discard_pre = source.discard_pre;
  result.discard_post = source.discard_post;
  result.center_sample = source.center_sample;
  result.encoding_space_ref = source.encoding_space_ref;
  result.trajectory_dimensions = source.trajectory_dimensions;
  result.sample_time_us = source.sample_time_us;
  std::copy(std::begin(source.position), std::end(source.position), result.position.begin());
  std::copy(std::begin(source.read_dir), std::end(source.read_dir), result.read_dir.begin());
  std::copy(std::begin(source.phase_dir), std::end(source.phase_dir), result.phase_dir.begin());
  std::copy(std::begin(source.slice_dir), std::end(source.slice_dir), result.slice_dir.begin());
  std::copy(std::begin(source.patient_table_position), std::end(source.patient_table_position),
            result.patient_table_position.begin());
  result.index = copy_encoding_counters(source.idx);
  std::copy(std::begin(source.user_int), std::end(source.user_int), result.user_int.begin());
  std::copy(std::begin(source.user_float), std::end(source.user_float), result.user_float.begin());
  return result;
}

} // namespace

struct DatasetReader::Impl {
  Impl(const std::filesystem::path& file, const std::string& group)
      : filename(file.string()), dataset(filename.c_str(), group.c_str(), false) {}

  std::string filename;
  ISMRMRD::Dataset dataset;
};

DatasetReader::DatasetReader() = default;
DatasetReader::~DatasetReader() = default;
DatasetReader::DatasetReader(DatasetReader&&) noexcept = default;
DatasetReader& DatasetReader::operator=(DatasetReader&&) noexcept = default;

bool DatasetReader::open(const std::filesystem::path& file, std::string group, std::string& error) {
  error.clear();
  if (file.empty()) {
    error = "ISMRMRD input path must not be empty.";
    return false;
  }
  if (group.empty()) {
    error = "ISMRMRD dataset group must not be empty.";
    return false;
  }

  try {
    auto next = std::make_unique<Impl>(file, group);
    DatasetMetadata next_metadata;
    next_metadata.group = std::move(group);
    next->dataset.readHeader(next_metadata.xml_header);
    next_metadata.acquisition_count = next->dataset.getNumberOfAcquisitions();
    impl_ = std::move(next);
    metadata_ = std::move(next_metadata);
    return true;
  } catch (const std::exception& exception) {
    impl_.reset();
    metadata_ = {};
    error = "Unable to open ISMRMRD input '" + file.string() + "': " + exception.what();
    return false;
  }
}

bool DatasetReader::is_open() const noexcept {
  return impl_ != nullptr;
}

const DatasetMetadata& DatasetReader::metadata() const noexcept {
  return metadata_;
}

AcquisitionIterationResult DatasetReader::for_each_acquisition(const AcquisitionConsumer& consumer,
                                                               std::string& error) {
  error.clear();
  if (!impl_) {
    error = "Cannot read ISMRMRD acquisitions before opening a dataset.";
    return AcquisitionIterationResult::failed;
  }
  if (!consumer) {
    error = "ISMRMRD acquisition consumer must not be empty.";
    return AcquisitionIterationResult::failed;
  }

  try {
    for (std::uint32_t index = 0; index < metadata_.acquisition_count; ++index) {
      ISMRMRD::Acquisition acquisition;
      impl_->dataset.readAcquisition(index, acquisition);

      const auto sample_count = acquisition.getNumberOfDataElements();
      const auto trajectory_count = acquisition.getNumberOfTrajElements();
      const auto samples = sample_count == 0U
                             ? std::span<const std::complex<float>>{}
                             : std::span<const std::complex<float>>(acquisition.getDataPtr(), sample_count);
      const auto trajectory = trajectory_count == 0U
                                ? std::span<const float>{}
                                : std::span<const float>(acquisition.getTrajPtr(), trajectory_count);
      if (!consumer({copy_header(acquisition.getHead()), samples, trajectory})) {
        return AcquisitionIterationResult::stopped;
      }
    }
    return AcquisitionIterationResult::completed;
  } catch (const std::exception& exception) {
    error = "Unable to read ISMRMRD acquisition: " + std::string(exception.what());
    return AcquisitionIterationResult::failed;
  }
}

} // namespace ksj::ismrmrd
