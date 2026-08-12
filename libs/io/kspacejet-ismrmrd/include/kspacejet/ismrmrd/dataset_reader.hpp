#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace ksj::ismrmrd {

struct EncodingCounters {
  std::uint16_t kspace_encode_step_1 = 0;
  std::uint16_t kspace_encode_step_2 = 0;
  std::uint16_t average = 0;
  std::uint16_t slice = 0;
  std::uint16_t contrast = 0;
  std::uint16_t phase = 0;
  std::uint16_t repetition = 0;
  std::uint16_t set = 0;
  std::uint16_t segment = 0;
  std::array<std::uint16_t, 8> user{};
};

struct AcquisitionHeader {
  std::uint16_t version = 0;
  std::uint64_t flags = 0;
  std::uint32_t measurement_uid = 0;
  std::uint32_t scan_counter = 0;
  std::uint32_t acquisition_time_stamp = 0;
  std::array<std::uint32_t, 3> physiology_time_stamp{};
  std::uint16_t number_of_samples = 0;
  std::uint16_t available_channels = 0;
  std::uint16_t active_channels = 0;
  std::array<std::uint64_t, 16> channel_mask{};
  std::uint16_t discard_pre = 0;
  std::uint16_t discard_post = 0;
  std::uint16_t center_sample = 0;
  std::uint16_t encoding_space_ref = 0;
  std::uint16_t trajectory_dimensions = 0;
  float sample_time_us = 0.0F;
  std::array<float, 3> position{};
  std::array<float, 3> read_dir{};
  std::array<float, 3> phase_dir{};
  std::array<float, 3> slice_dir{};
  std::array<float, 3> patient_table_position{};
  EncodingCounters index{};
  std::array<std::int32_t, 8> user_int{};
  std::array<float, 8> user_float{};
};

struct AcquisitionView {
  AcquisitionHeader header;

  // ISMRMRD sample layout: samples[sample + channel * number_of_samples].
  std::span<const std::complex<float>> samples;
  // ISMRMRD trajectory layout: trajectory[sample * trajectory_dimensions + dimension].
  std::span<const float> trajectory;
};

struct DatasetMetadata {
  std::string group;
  std::string xml_header;
  std::uint32_t acquisition_count = 0;
};

enum class AcquisitionIterationResult {
  completed,
  stopped,
  failed,
};

using AcquisitionConsumer = std::function<bool(const AcquisitionView&)>;

// A streaming facade over an ISMRMRD HDF5 acquisition group. The acquisition
// views passed to the consumer are valid only for that consumer invocation.
class DatasetReader {
public:
  DatasetReader();
  ~DatasetReader();

  DatasetReader(const DatasetReader&) = delete;
  DatasetReader& operator=(const DatasetReader&) = delete;
  DatasetReader(DatasetReader&&) noexcept;
  DatasetReader& operator=(DatasetReader&&) noexcept;

  [[nodiscard]] bool open(const std::filesystem::path& file, std::string group, std::string& error);
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] const DatasetMetadata& metadata() const noexcept;

  // A false return from consumer is a normal early stop, reported as
  // AcquisitionIterationResult::stopped. I/O and format errors are reported
  // as failed and described in error.
  [[nodiscard]] AcquisitionIterationResult for_each_acquisition(const AcquisitionConsumer& consumer,
                                                                std::string& error);

private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
  DatasetMetadata metadata_;
};

} // namespace ksj::ismrmrd
