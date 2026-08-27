#include "kspacejet/ismrmrd/inspection_reader.hpp"

#include <hdf5.h>
#include <ismrmrd/dataset.h>
#include <ismrmrd/meta.h>
#include <ismrmrd/waveform.h>

#include <algorithm>
#include <array>
#include <complex>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::ismrmrd {
namespace {

constexpr std::size_t kMaximumStandardHeaderStorageBytes = 4096U;

class H5Handle final {
public:
  using Closer = herr_t (*)(hid_t);

  H5Handle() = default;
  H5Handle(const hid_t identifier, const Closer closer) : identifier_(identifier), closer_(closer) {}
  ~H5Handle() { reset(); }

  H5Handle(const H5Handle&) = delete;
  H5Handle& operator=(const H5Handle&) = delete;

  H5Handle(H5Handle&& other) noexcept
      : identifier_(std::exchange(other.identifier_, H5I_INVALID_HID)), closer_(std::exchange(other.closer_, nullptr)) {
  }

  H5Handle& operator=(H5Handle&& other) noexcept {
    if (this != &other) {
      reset();
      identifier_ = std::exchange(other.identifier_, H5I_INVALID_HID);
      closer_ = std::exchange(other.closer_, nullptr);
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return identifier_ >= 0; }

  [[nodiscard]] hid_t get() const noexcept { return identifier_; }

  void reset() noexcept {
    if (valid() && closer_ != nullptr) {
      static_cast<void>(closer_(identifier_));
    }
    identifier_ = H5I_INVALID_HID;
    closer_ = nullptr;
  }

private:
  hid_t identifier_{H5I_INVALID_HID};
  Closer closer_{nullptr};
};

[[nodiscard]] H5Handle compound_member_type(const hid_t compound_type, const std::string_view member_name) {
  const std::string member_name_owned(member_name);
  const auto member_index = H5Tget_member_index(compound_type, member_name_owned.c_str());
  return {member_index >= 0 ? H5Tget_member_type(compound_type, static_cast<unsigned>(member_index)) : H5I_INVALID_HID,
          H5Tclose};
}

[[nodiscard]] bool matches_scalar_type(const hid_t source_type, const H5T_class_t expected_class,
                                       const std::size_t expected_bytes,
                                       const H5T_sign_t expected_sign = H5T_SGN_ERROR) {
  H5Handle native_type{H5Tget_native_type(source_type, H5T_DIR_ASCEND), H5Tclose};
  if (!native_type.valid() || H5Tget_class(native_type.get()) != expected_class ||
      H5Tget_size(native_type.get()) != expected_bytes ||
      H5Tget_precision(native_type.get()) != expected_bytes * std::numeric_limits<unsigned char>::digits) {
    return false;
  }
  return expected_class != H5T_INTEGER || H5Tget_sign(native_type.get()) == expected_sign;
}

[[nodiscard]] bool matches_array_type(const hid_t source_type, const hsize_t expected_count,
                                      const H5T_class_t expected_element_class,
                                      const std::size_t expected_element_bytes,
                                      const H5T_sign_t expected_element_sign = H5T_SGN_ERROR) {
  if (H5Tget_class(source_type) != H5T_ARRAY || H5Tget_array_ndims(source_type) != 1) {
    return false;
  }
  std::array<hsize_t, 1U> dimensions{};
  if (H5Tget_array_dims2(source_type, dimensions.data()) < 0 || dimensions.front() != expected_count) {
    return false;
  }
  H5Handle element_type{H5Tget_super(source_type), H5Tclose};
  return element_type.valid() &&
         matches_scalar_type(element_type.get(), expected_element_class, expected_element_bytes, expected_element_sign);
}

[[nodiscard]] bool compound_member_matches_scalar(const hid_t compound_type, const std::string_view member_name,
                                                  const H5T_class_t expected_class, const std::size_t expected_bytes,
                                                  const H5T_sign_t expected_sign = H5T_SGN_ERROR) {
  H5Handle member_type = compound_member_type(compound_type, member_name);
  return member_type.valid() && matches_scalar_type(member_type.get(), expected_class, expected_bytes, expected_sign);
}

[[nodiscard]] bool compound_member_matches_array(const hid_t compound_type, const std::string_view member_name,
                                                 const hsize_t expected_count, const H5T_class_t expected_element_class,
                                                 const std::size_t expected_element_bytes,
                                                 const H5T_sign_t expected_element_sign = H5T_SGN_ERROR) {
  H5Handle member_type = compound_member_type(compound_type, member_name);
  return member_type.valid() && matches_array_type(member_type.get(), expected_count, expected_element_class,
                                                   expected_element_bytes, expected_element_sign);
}

[[nodiscard]] bool insert_array_member(const hid_t compound_type, const char* member_name,
                                       const std::size_t member_offset, const hid_t element_type,
                                       const hsize_t element_count) {
  const std::array<hsize_t, 1U> dimensions{element_count};
  H5Handle array_type{H5Tarray_create2(element_type, static_cast<unsigned>(dimensions.size()), dimensions.data()),
                      H5Tclose};
  return array_type.valid() && H5Tinsert(compound_type, member_name, member_offset, array_type.get()) >= 0;
}

[[nodiscard]] H5Handle make_encoding_counters_memory_type() {
  H5Handle type{H5Tcreate(H5T_COMPOUND, sizeof(ISMRMRD::ISMRMRD_EncodingCounters)), H5Tclose};
  if (!type.valid()) {
    return type;
  }
  const bool populated =
    H5Tinsert(type.get(), "kspace_encode_step_1", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, kspace_encode_step_1),
              H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "kspace_encode_step_2", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, kspace_encode_step_2),
              H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "average", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, average), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "slice", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, slice), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "contrast", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, contrast), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "phase", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, phase), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "repetition", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, repetition), H5T_NATIVE_UINT16) >=
      0 &&
    H5Tinsert(type.get(), "set", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, set), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "segment", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, segment), H5T_NATIVE_UINT16) >= 0 &&
    insert_array_member(type.get(), "user", offsetof(ISMRMRD::ISMRMRD_EncodingCounters, user), H5T_NATIVE_UINT16,
                        ISMRMRD::ISMRMRD_USER_INTS);
  if (!populated) {
    type.reset();
  }
  return type;
}

[[nodiscard]] bool matches_encoding_counters_type(const hid_t source_type) {
  return H5Tget_class(source_type) == H5T_COMPOUND && H5Tget_nmembers(source_type) == 10 &&
         compound_member_matches_scalar(source_type, "kspace_encode_step_1", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "kspace_encode_step_2", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "average", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "slice", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "contrast", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "phase", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "repetition", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "set", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "segment", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_array(source_type, "user", ISMRMRD::ISMRMRD_USER_INTS, H5T_INTEGER,
                                       sizeof(std::uint16_t), H5T_SGN_NONE);
}

[[nodiscard]] H5Handle make_image_header_memory_type() {
  H5Handle type{H5Tcreate(H5T_COMPOUND, sizeof(ISMRMRD::ISMRMRD_ImageHeader)), H5Tclose};
  if (!type.valid()) {
    return type;
  }
  const bool populated =
    H5Tinsert(type.get(), "version", offsetof(ISMRMRD::ISMRMRD_ImageHeader, version), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "data_type", offsetof(ISMRMRD::ISMRMRD_ImageHeader, data_type), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "flags", offsetof(ISMRMRD::ISMRMRD_ImageHeader, flags), H5T_NATIVE_UINT64) >= 0 &&
    H5Tinsert(type.get(), "measurement_uid", offsetof(ISMRMRD::ISMRMRD_ImageHeader, measurement_uid),
              H5T_NATIVE_UINT32) >= 0 &&
    insert_array_member(type.get(), "matrix_size", offsetof(ISMRMRD::ISMRMRD_ImageHeader, matrix_size),
                        H5T_NATIVE_UINT16, 3U) &&
    insert_array_member(type.get(), "field_of_view", offsetof(ISMRMRD::ISMRMRD_ImageHeader, field_of_view),
                        H5T_NATIVE_FLOAT, 3U) &&
    H5Tinsert(type.get(), "channels", offsetof(ISMRMRD::ISMRMRD_ImageHeader, channels), H5T_NATIVE_UINT16) >= 0 &&
    insert_array_member(type.get(), "position", offsetof(ISMRMRD::ISMRMRD_ImageHeader, position), H5T_NATIVE_FLOAT,
                        3U) &&
    insert_array_member(type.get(), "read_dir", offsetof(ISMRMRD::ISMRMRD_ImageHeader, read_dir), H5T_NATIVE_FLOAT,
                        3U) &&
    insert_array_member(type.get(), "phase_dir", offsetof(ISMRMRD::ISMRMRD_ImageHeader, phase_dir), H5T_NATIVE_FLOAT,
                        3U) &&
    insert_array_member(type.get(), "slice_dir", offsetof(ISMRMRD::ISMRMRD_ImageHeader, slice_dir), H5T_NATIVE_FLOAT,
                        3U) &&
    insert_array_member(type.get(), "patient_table_position",
                        offsetof(ISMRMRD::ISMRMRD_ImageHeader, patient_table_position), H5T_NATIVE_FLOAT, 3U) &&
    H5Tinsert(type.get(), "average", offsetof(ISMRMRD::ISMRMRD_ImageHeader, average), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "slice", offsetof(ISMRMRD::ISMRMRD_ImageHeader, slice), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "contrast", offsetof(ISMRMRD::ISMRMRD_ImageHeader, contrast), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "phase", offsetof(ISMRMRD::ISMRMRD_ImageHeader, phase), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "repetition", offsetof(ISMRMRD::ISMRMRD_ImageHeader, repetition), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "set", offsetof(ISMRMRD::ISMRMRD_ImageHeader, set), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "acquisition_time_stamp", offsetof(ISMRMRD::ISMRMRD_ImageHeader, acquisition_time_stamp),
              H5T_NATIVE_UINT32) >= 0 &&
    insert_array_member(type.get(), "physiology_time_stamp",
                        offsetof(ISMRMRD::ISMRMRD_ImageHeader, physiology_time_stamp), H5T_NATIVE_UINT32,
                        ISMRMRD::ISMRMRD_PHYS_STAMPS) &&
    H5Tinsert(type.get(), "image_type", offsetof(ISMRMRD::ISMRMRD_ImageHeader, image_type), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "image_index", offsetof(ISMRMRD::ISMRMRD_ImageHeader, image_index), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "image_series_index", offsetof(ISMRMRD::ISMRMRD_ImageHeader, image_series_index),
              H5T_NATIVE_UINT16) >= 0 &&
    insert_array_member(type.get(), "user_int", offsetof(ISMRMRD::ISMRMRD_ImageHeader, user_int), H5T_NATIVE_INT32,
                        ISMRMRD::ISMRMRD_USER_INTS) &&
    insert_array_member(type.get(), "user_float", offsetof(ISMRMRD::ISMRMRD_ImageHeader, user_float), H5T_NATIVE_FLOAT,
                        ISMRMRD::ISMRMRD_USER_FLOATS) &&
    H5Tinsert(type.get(), "attribute_string_len", offsetof(ISMRMRD::ISMRMRD_ImageHeader, attribute_string_len),
              H5T_NATIVE_UINT32) >= 0;
  if (!populated) {
    type.reset();
  }
  return type;
}

[[nodiscard]] bool matches_image_header_type(const hid_t source_type) {
  return H5Tget_class(source_type) == H5T_COMPOUND && H5Tget_size(source_type) <= kMaximumStandardHeaderStorageBytes &&
         H5Tget_nmembers(source_type) == 26 &&
         compound_member_matches_scalar(source_type, "version", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "data_type", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "flags", H5T_INTEGER, sizeof(std::uint64_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "measurement_uid", H5T_INTEGER, sizeof(std::uint32_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_array(source_type, "matrix_size", 3U, H5T_INTEGER, sizeof(std::uint16_t),
                                       H5T_SGN_NONE) &&
         compound_member_matches_array(source_type, "field_of_view", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_scalar(source_type, "channels", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_array(source_type, "position", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_array(source_type, "read_dir", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_array(source_type, "phase_dir", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_array(source_type, "slice_dir", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_array(source_type, "patient_table_position", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_scalar(source_type, "average", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "slice", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "contrast", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "phase", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "repetition", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "set", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "acquisition_time_stamp", H5T_INTEGER, sizeof(std::uint32_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_array(source_type, "physiology_time_stamp", ISMRMRD::ISMRMRD_PHYS_STAMPS, H5T_INTEGER,
                                       sizeof(std::uint32_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "image_type", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "image_index", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "image_series_index", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_array(source_type, "user_int", ISMRMRD::ISMRMRD_USER_INTS, H5T_INTEGER,
                                       sizeof(std::int32_t), H5T_SGN_2) &&
         compound_member_matches_array(source_type, "user_float", ISMRMRD::ISMRMRD_USER_FLOATS, H5T_FLOAT,
                                       sizeof(float)) &&
         compound_member_matches_scalar(source_type, "attribute_string_len", H5T_INTEGER, sizeof(std::uint32_t),
                                        H5T_SGN_NONE);
}

[[nodiscard]] bool matches_waveform_header_type(const hid_t source_type) {
  return H5Tget_class(source_type) == H5T_COMPOUND && H5Tget_size(source_type) <= kMaximumStandardHeaderStorageBytes &&
         H5Tget_nmembers(source_type) == 9 &&
         compound_member_matches_scalar(source_type, "version", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "flags", H5T_INTEGER, sizeof(std::uint64_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "measurement_uid", H5T_INTEGER, sizeof(std::uint32_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "scan_counter", H5T_INTEGER, sizeof(std::uint32_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "time_stamp", H5T_INTEGER, sizeof(std::uint32_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "number_of_samples", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "channels", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "sample_time_us", H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_scalar(source_type, "waveform_id", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE);
}

[[nodiscard]] bool matches_uint32_vlen_type(const hid_t source_type) {
  H5Handle element_type{H5Tget_class(source_type) == H5T_VLEN ? H5Tget_super(source_type) : H5I_INVALID_HID, H5Tclose};
  return element_type.valid() &&
         matches_scalar_type(element_type.get(), H5T_INTEGER, sizeof(std::uint32_t), H5T_SGN_NONE);
}

[[nodiscard]] bool matches_waveform_type(const hid_t source_type) {
  if (H5Tget_class(source_type) != H5T_COMPOUND || H5Tget_nmembers(source_type) != 2) {
    return false;
  }
  H5Handle header_type = compound_member_type(source_type, "head");
  H5Handle data_type = compound_member_type(source_type, "data");
  return header_type.valid() && data_type.valid() && matches_waveform_header_type(header_type.get()) &&
         matches_uint32_vlen_type(data_type.get());
}

[[nodiscard]] H5Handle make_acquisition_header_memory_type() {
  H5Handle type{H5Tcreate(H5T_COMPOUND, sizeof(ISMRMRD::ISMRMRD_AcquisitionHeader)), H5Tclose};
  H5Handle encoding_type = make_encoding_counters_memory_type();
  if (!type.valid() || !encoding_type.valid()) {
    type.reset();
    return type;
  }
  const bool populated =
    H5Tinsert(type.get(), "version", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, version), H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "flags", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, flags), H5T_NATIVE_UINT64) >= 0 &&
    H5Tinsert(type.get(), "measurement_uid", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, measurement_uid),
              H5T_NATIVE_UINT32) >= 0 &&
    H5Tinsert(type.get(), "scan_counter", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, scan_counter),
              H5T_NATIVE_UINT32) >= 0 &&
    H5Tinsert(type.get(), "acquisition_time_stamp",
              offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, acquisition_time_stamp), H5T_NATIVE_UINT32) >= 0 &&
    insert_array_member(type.get(), "physiology_time_stamp",
                        offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, physiology_time_stamp), H5T_NATIVE_UINT32,
                        ISMRMRD::ISMRMRD_PHYS_STAMPS) &&
    H5Tinsert(type.get(), "number_of_samples", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, number_of_samples),
              H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "available_channels", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, available_channels),
              H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "active_channels", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, active_channels),
              H5T_NATIVE_UINT16) >= 0 &&
    insert_array_member(type.get(), "channel_mask", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, channel_mask),
                        H5T_NATIVE_UINT64, ISMRMRD::ISMRMRD_CHANNEL_MASKS) &&
    H5Tinsert(type.get(), "discard_pre", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, discard_pre),
              H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "discard_post", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, discard_post),
              H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "center_sample", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, center_sample),
              H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "encoding_space_ref", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, encoding_space_ref),
              H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "trajectory_dimensions", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, trajectory_dimensions),
              H5T_NATIVE_UINT16) >= 0 &&
    H5Tinsert(type.get(), "sample_time_us", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, sample_time_us),
              H5T_NATIVE_FLOAT) >= 0 &&
    insert_array_member(type.get(), "position", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, position),
                        H5T_NATIVE_FLOAT, 3U) &&
    insert_array_member(type.get(), "read_dir", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, read_dir),
                        H5T_NATIVE_FLOAT, 3U) &&
    insert_array_member(type.get(), "phase_dir", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, phase_dir),
                        H5T_NATIVE_FLOAT, 3U) &&
    insert_array_member(type.get(), "slice_dir", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, slice_dir),
                        H5T_NATIVE_FLOAT, 3U) &&
    insert_array_member(type.get(), "patient_table_position",
                        offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, patient_table_position), H5T_NATIVE_FLOAT, 3U) &&
    H5Tinsert(type.get(), "idx", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, idx), encoding_type.get()) >= 0 &&
    insert_array_member(type.get(), "user_int", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, user_int),
                        H5T_NATIVE_INT32, ISMRMRD::ISMRMRD_USER_INTS) &&
    insert_array_member(type.get(), "user_float", offsetof(ISMRMRD::ISMRMRD_AcquisitionHeader, user_float),
                        H5T_NATIVE_FLOAT, ISMRMRD::ISMRMRD_USER_FLOATS);
  if (!populated) {
    type.reset();
  }
  return type;
}

[[nodiscard]] bool matches_acquisition_header_type(const hid_t source_type) {
  if (H5Tget_class(source_type) != H5T_COMPOUND || H5Tget_size(source_type) > kMaximumStandardHeaderStorageBytes ||
      H5Tget_nmembers(source_type) != 24) {
    return false;
  }
  H5Handle encoding_type = compound_member_type(source_type, "idx");
  return compound_member_matches_scalar(source_type, "version", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "flags", H5T_INTEGER, sizeof(std::uint64_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "measurement_uid", H5T_INTEGER, sizeof(std::uint32_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "scan_counter", H5T_INTEGER, sizeof(std::uint32_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "acquisition_time_stamp", H5T_INTEGER, sizeof(std::uint32_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_array(source_type, "physiology_time_stamp", ISMRMRD::ISMRMRD_PHYS_STAMPS, H5T_INTEGER,
                                       sizeof(std::uint32_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "number_of_samples", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "available_channels", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "active_channels", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_array(source_type, "channel_mask", ISMRMRD::ISMRMRD_CHANNEL_MASKS, H5T_INTEGER,
                                       sizeof(std::uint64_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "discard_pre", H5T_INTEGER, sizeof(std::uint16_t), H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "discard_post", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "center_sample", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "encoding_space_ref", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "trajectory_dimensions", H5T_INTEGER, sizeof(std::uint16_t),
                                        H5T_SGN_NONE) &&
         compound_member_matches_scalar(source_type, "sample_time_us", H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_array(source_type, "position", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_array(source_type, "read_dir", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_array(source_type, "phase_dir", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_array(source_type, "slice_dir", 3U, H5T_FLOAT, sizeof(float)) &&
         compound_member_matches_array(source_type, "patient_table_position", 3U, H5T_FLOAT, sizeof(float)) &&
         encoding_type.valid() && matches_encoding_counters_type(encoding_type.get()) &&
         compound_member_matches_array(source_type, "user_int", ISMRMRD::ISMRMRD_USER_INTS, H5T_INTEGER,
                                       sizeof(std::int32_t), H5T_SGN_2) &&
         compound_member_matches_array(source_type, "user_float", ISMRMRD::ISMRMRD_USER_FLOATS, H5T_FLOAT,
                                       sizeof(float));
}

class InspectionDataset final : public ISMRMRD::Dataset {
public:
  using ISMRMRD::Dataset::Dataset;

  [[nodiscard]] hid_t hdf5_file_id() const noexcept { return dset_.fileid; }
};

[[nodiscard]] std::string make_dataset_path(const std::string_view group, const std::string_view leaf) {
  std::string result(group);
  if (result.empty() || result.back() != '/') {
    result.push_back('/');
  }
  result.append(leaf);
  return result;
}

struct ImageSeriesLocation {
  std::string parent_group;
  std::string series_id;
};

// A standalone standard image-series group contains `header`, `attributes`,
// and `data` directly. The upstream ISMRMRD binding opens images relative to
// the parent Dataset group, so split the discovered HDF5 path without
// imposing a `/dataset` convention.
[[nodiscard]] bool split_image_series_path(const std::string_view path, ImageSeriesLocation& location) {
  location = {};
  if (path.empty() || path == "/" || path.back() == '/') {
    return false;
  }
  const auto separator = path.find_last_of('/');
  if (separator == std::string_view::npos) {
    location.parent_group = "/";
    location.series_id = path;
  } else if (separator == 0U) {
    location.parent_group = "/";
    location.series_id = path.substr(1U);
  } else {
    location.parent_group = path.substr(0U, separator);
    location.series_id = path.substr(separator + 1U);
  }
  return !location.series_id.empty();
}

[[nodiscard]] bool validate_limits(const InspectionReadLimits& limits, std::string& error) {
  if (limits.max_xml_header_bytes == 0U || limits.max_acquisition_count == 0U ||
      limits.max_acquisition_payload_bytes == 0U || limits.max_hdf5_group_links == 0U ||
      limits.max_hdf5_group_count == 0U || limits.max_hdf5_group_depth == 0U ||
      limits.max_hdf5_group_path_bytes == 0U || limits.max_hdf5_group_name_bytes == 0U ||
      limits.max_image_series == 0U || limits.max_image_series_name_bytes == 0U || limits.max_images_per_series == 0U ||
      limits.max_image_payload_bytes == 0U || limits.max_image_attribute_bytes == 0U ||
      limits.max_meta_attribute_entries == 0U || limits.max_meta_attribute_name_bytes == 0U ||
      limits.max_meta_attribute_value_bytes == 0U) {
    error = "ISMRMRD inspection limits must be nonzero.";
    return false;
  }
  return true;
}

[[nodiscard]] bool checked_multiply(const std::size_t left, const std::size_t right, std::size_t& result) {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

[[nodiscard]] bool checked_add(const std::size_t left, const std::size_t right, std::size_t& result) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] bool h5_link_exists(const hid_t location, const std::string& path, bool& exists) {
  const auto result = H5Lexists(location, path.c_str(), H5P_DEFAULT);
  if (result < 0) {
    return false;
  }
  exists = result > 0;
  return true;
}

struct IndexedSelection {
  H5Handle file_space;
  H5Handle memory_space;
  std::vector<hsize_t> extent;
};

[[nodiscard]] bool select_index(const hid_t dataset, const std::uint32_t ordinal, IndexedSelection& selection) {
  H5Handle file_space{H5Dget_space(dataset), H5Sclose};
  if (!file_space.valid()) {
    return false;
  }
  const auto rank = H5Sget_simple_extent_ndims(file_space.get());
  // All standard ISMRMRD record, XML, and attribute datasets addressed by
  // this helper are one-dimensional. Reject any other rank before H5Dread:
  // selecting a trailing extent would otherwise write more than this reader's
  // single-record buffers can hold.
  if (rank != 1) {
    return false;
  }
  std::vector<hsize_t> extent(static_cast<std::size_t>(rank));
  if (H5Sget_simple_extent_dims(file_space.get(), extent.data(), nullptr) < 0 || ordinal >= extent.front()) {
    return false;
  }

  std::vector<hsize_t> start(static_cast<std::size_t>(rank), 0U);
  std::vector<hsize_t> count = extent;
  start.front() = ordinal;
  count.front() = 1U;
  if (H5Sselect_hyperslab(file_space.get(), H5S_SELECT_SET, start.data(), nullptr, count.data(), nullptr) < 0) {
    return false;
  }

  H5Handle memory_space{H5Screate_simple(rank, count.data(), nullptr), H5Sclose};
  if (!memory_space.valid()) {
    return false;
  }
  selection.file_space = std::move(file_space);
  selection.memory_space = std::move(memory_space);
  selection.extent = std::move(extent);
  return true;
}

[[nodiscard]] bool dataset_record_count(const hid_t file_id, const std::string& path, hsize_t& count) {
  H5Handle dataset{H5Dopen2(file_id, path.c_str(), H5P_DEFAULT), H5Dclose};
  if (!dataset.valid()) {
    return false;
  }
  H5Handle dataspace{H5Dget_space(dataset.get()), H5Sclose};
  if (!dataspace.valid() || H5Sget_simple_extent_ndims(dataspace.get()) != 1) {
    return false;
  }
  std::array<hsize_t, 1> dimensions{};
  if (H5Sget_simple_extent_dims(dataspace.get(), dimensions.data(), nullptr) < 0) {
    return false;
  }
  count = dimensions.front();
  return true;
}

// The ISMRMRD acquisition record stores `traj` and `data` as distinct HDF5
// VLEN members. Query each member independently before the upstream binding
// reads it: the binding exposes only the header-derived logical sizes after its
// read, while HDF5 is the sole authority for the physical VLEN sizes.
[[nodiscard]] bool selected_float_vlen_member_bytes(const hid_t file_id, const std::string& path,
                                                    const std::uint32_t ordinal, const std::string_view member_name,
                                                    std::size_t& bytes) {
  H5Handle dataset{H5Dopen2(file_id, path.c_str(), H5P_DEFAULT), H5Dclose};
  H5Handle file_type{dataset.valid() ? H5Dget_type(dataset.get()) : H5I_INVALID_HID, H5Tclose};
  if (!dataset.valid() || !file_type.valid() || H5Tget_class(file_type.get()) != H5T_COMPOUND) {
    return false;
  }
  const std::string member_name_owned(member_name);
  const auto member_index = H5Tget_member_index(file_type.get(), member_name_owned.c_str());
  H5Handle member_type{member_index >= 0 ? H5Tget_member_type(file_type.get(), static_cast<unsigned>(member_index))
                                         : H5I_INVALID_HID,
                       H5Tclose};
  H5Handle element_type{member_type.valid() ? H5Tget_super(member_type.get()) : H5I_INVALID_HID, H5Tclose};
  if (!member_type.valid() || !element_type.valid() || H5Tget_class(member_type.get()) != H5T_VLEN ||
      !matches_scalar_type(element_type.get(), H5T_FLOAT, sizeof(float))) {
    return false;
  }

  H5Handle memory_vlen_type{H5Tvlen_create(H5T_NATIVE_FLOAT), H5Tclose};
  H5Handle memory_type{memory_vlen_type.valid() ? H5Tcreate(H5T_COMPOUND, H5Tget_size(memory_vlen_type.get()))
                                                : H5I_INVALID_HID,
                       H5Tclose};
  if (!memory_vlen_type.valid() || !memory_type.valid() ||
      H5Tinsert(memory_type.get(), member_name_owned.c_str(), 0U, memory_vlen_type.get()) < 0) {
    return false;
  }
  IndexedSelection selection;
  if (!select_index(dataset.get(), ordinal, selection)) {
    return false;
  }
  hsize_t requested_bytes = 0U;
  if (H5Dvlen_get_buf_size(dataset.get(), memory_type.get(), selection.file_space.get(), &requested_bytes) < 0 ||
      requested_bytes > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  bytes = static_cast<std::size_t>(requested_bytes);
  return true;
}

[[nodiscard]] bool read_vlen_string(const hid_t file_id, const std::string& path, const std::uint32_t ordinal,
                                    const std::size_t byte_limit, const std::string_view limit_error,
                                    std::string& value, std::string& error) {
  value.clear();
  H5Handle dataset{H5Dopen2(file_id, path.c_str(), H5P_DEFAULT), H5Dclose};
  H5Handle file_type{dataset.valid() ? H5Dget_type(dataset.get()) : H5I_INVALID_HID, H5Tclose};
  H5Handle memory_type{H5Tcopy(H5T_C_S1), H5Tclose};
  if (!dataset.valid() || !file_type.valid() || H5Tget_class(file_type.get()) != H5T_STRING ||
      H5Tis_variable_str(file_type.get()) <= 0 || !memory_type.valid() ||
      H5Tset_size(memory_type.get(), H5T_VARIABLE) < 0) {
    error = "ISMRMRD HDF5 variable-length string is malformed.";
    return false;
  }
  IndexedSelection selection;
  if (!select_index(dataset.get(), ordinal, selection)) {
    error = "ISMRMRD HDF5 variable-length string is malformed.";
    return false;
  }
  hsize_t requested_bytes = 0U;
  if (H5Dvlen_get_buf_size(dataset.get(), memory_type.get(), selection.file_space.get(), &requested_bytes) < 0 ||
      requested_bytes > std::numeric_limits<std::size_t>::max()) {
    error = "ISMRMRD HDF5 variable-length string is malformed.";
    return false;
  }
  const auto preflight_bytes = static_cast<std::size_t>(requested_bytes);
  std::size_t allocation_limit = 0U;
  if (!checked_add(byte_limit, 1U, allocation_limit) || preflight_bytes == 0U || preflight_bytes > allocation_limit) {
    error = std::string(limit_error);
    return false;
  }
  H5Handle transfer{H5Pcreate(H5P_DATASET_XFER), H5Pclose};
  if (!transfer.valid()) {
    error = "ISMRMRD HDF5 variable-length string could not be read.";
    return false;
  }

  char* raw = nullptr;
  if (H5Dread(dataset.get(), memory_type.get(), selection.memory_space.get(), selection.file_space.get(),
              transfer.get(), &raw) < 0 ||
      raw == nullptr) {
    error = "ISMRMRD HDF5 variable-length string could not be read.";
    return false;
  }
  const auto* terminator = static_cast<const char*>(std::memchr(raw, '\0', preflight_bytes));
  std::string copied;
  if (terminator != nullptr) {
    copied.assign(raw, static_cast<std::size_t>(terminator - raw));
  }
  const auto reclaimed =
    H5Dvlen_reclaim(memory_type.get(), selection.memory_space.get(), transfer.get(), static_cast<void*>(&raw));
  if (terminator == nullptr || reclaimed < 0 || copied.size() > byte_limit) {
    error = "ISMRMRD HDF5 variable-length string is malformed.";
    return false;
  }
  value = std::move(copied);
  return true;
}

[[nodiscard]] bool read_image_header(const hid_t file_id, const std::string& path, const std::uint32_t ordinal,
                                     ISMRMRD::ISMRMRD_ImageHeader& header, std::string& error) {
  H5Handle dataset{H5Dopen2(file_id, path.c_str(), H5P_DEFAULT), H5Dclose};
  H5Handle file_type{dataset.valid() ? H5Dget_type(dataset.get()) : H5I_INVALID_HID, H5Tclose};
  H5Handle memory_type = make_image_header_memory_type();
  if (!dataset.valid() || !file_type.valid() || !memory_type.valid() || !matches_image_header_type(file_type.get())) {
    error = "ISMRMRD image header is malformed.";
    return false;
  }
  IndexedSelection selection;
  if (!select_index(dataset.get(), ordinal, selection)) {
    error = "ISMRMRD image ordinal is outside its series.";
    return false;
  }
  if (H5Dread(dataset.get(), memory_type.get(), selection.memory_space.get(), selection.file_space.get(), H5P_DEFAULT,
              &header) < 0) {
    error = "ISMRMRD image header could not be read.";
    return false;
  }
  return true;
}

[[nodiscard]] bool read_acquisition_header_impl(const hid_t file_id, const std::string& path,
                                                const std::uint32_t ordinal, ISMRMRD::ISMRMRD_AcquisitionHeader& header,
                                                std::string& error) {
  H5Handle dataset{H5Dopen2(file_id, path.c_str(), H5P_DEFAULT), H5Dclose};
  H5Handle file_type{dataset.valid() ? H5Dget_type(dataset.get()) : H5I_INVALID_HID, H5Tclose};
  if (!dataset.valid() || !file_type.valid() || H5Tget_class(file_type.get()) != H5T_COMPOUND) {
    error = "ISMRMRD acquisition dataset is malformed.";
    return false;
  }
  const auto member_index = H5Tget_member_index(file_type.get(), "head");
  H5Handle member_type{member_index >= 0 ? H5Tget_member_type(file_type.get(), static_cast<unsigned>(member_index))
                                         : H5I_INVALID_HID,
                       H5Tclose};
  H5Handle header_memory_type = make_acquisition_header_memory_type();
  H5Handle memory_type{H5Tcreate(H5T_COMPOUND, sizeof(header)), H5Tclose};
  if (!member_type.valid() || !header_memory_type.valid() || !memory_type.valid() ||
      !matches_acquisition_header_type(member_type.get()) ||
      H5Tinsert(memory_type.get(), "head", 0U, header_memory_type.get()) < 0) {
    error = "ISMRMRD acquisition header is malformed.";
    return false;
  }
  IndexedSelection selection;
  if (!select_index(dataset.get(), ordinal, selection)) {
    error = "ISMRMRD acquisition ordinal is outside the dataset.";
    return false;
  }
  if (H5Dread(dataset.get(), memory_type.get(), selection.memory_space.get(), selection.file_space.get(), H5P_DEFAULT,
              &header) < 0) {
    error = "ISMRMRD acquisition header could not be read.";
    return false;
  }
  return true;
}

struct PixelSpec {
  ImageDataType data_type;
  std::size_t bytes_per_element;
};

[[nodiscard]] bool pixel_spec(const std::uint16_t source, PixelSpec& result) {
  switch (source) {
    case ISMRMRD::ISMRMRD_USHORT:
      result = {ImageDataType::unsigned_integer_16, sizeof(std::uint16_t)};
      return true;
    case ISMRMRD::ISMRMRD_SHORT:
      result = {ImageDataType::signed_integer_16, sizeof(std::int16_t)};
      return true;
    case ISMRMRD::ISMRMRD_UINT:
      result = {ImageDataType::unsigned_integer_32, sizeof(std::uint32_t)};
      return true;
    case ISMRMRD::ISMRMRD_INT:
      result = {ImageDataType::signed_integer_32, sizeof(std::int32_t)};
      return true;
    case ISMRMRD::ISMRMRD_FLOAT:
      result = {ImageDataType::real_32, sizeof(float)};
      return true;
    case ISMRMRD::ISMRMRD_DOUBLE:
      result = {ImageDataType::real_64, sizeof(double)};
      return true;
    case ISMRMRD::ISMRMRD_CXFLOAT:
      result = {ImageDataType::complex_32, sizeof(float) * 2U};
      return true;
    case ISMRMRD::ISMRMRD_CXDOUBLE:
      result = {ImageDataType::complex_64, sizeof(double) * 2U};
      return true;
    default:
      return false;
  }
}

[[nodiscard]] bool matches_integer_pixel_type(const hid_t type, const std::size_t bytes, const H5T_sign_t sign) {
  return H5Tget_class(type) == H5T_INTEGER && H5Tget_size(type) == bytes && H5Tget_sign(type) == sign;
}

[[nodiscard]] bool matches_real_pixel_type(const hid_t type, const std::size_t bytes) {
  return H5Tget_class(type) == H5T_FLOAT && H5Tget_size(type) == bytes;
}

[[nodiscard]] bool matches_complex_pixel_type(const hid_t type, const std::size_t component_bytes) {
  if (H5Tget_class(type) != H5T_COMPOUND || H5Tget_size(type) != component_bytes * 2U || H5Tget_nmembers(type) != 2 ||
      H5Tget_member_offset(type, 0U) != 0U || H5Tget_member_offset(type, 1U) != component_bytes) {
    return false;
  }

  const std::array<std::string_view, 2U> expected_names{"real", "imag"};
  for (unsigned member_index = 0U; member_index < expected_names.size(); ++member_index) {
    char* member_name = H5Tget_member_name(type, member_index);
    const bool name_matches = member_name != nullptr && expected_names[member_index] == member_name;
    if (member_name != nullptr) {
      static_cast<void>(H5free_memory(member_name));
    }
    H5Handle member_type{H5Tget_member_type(type, member_index), H5Tclose};
    H5Handle native_member_type{
      member_type.valid() ? H5Tget_native_type(member_type.get(), H5T_DIR_ASCEND) : H5I_INVALID_HID, H5Tclose};
    if (!name_matches || !native_member_type.valid() ||
        !matches_real_pixel_type(native_member_type.get(), component_bytes)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool matches_image_pixel_type(const hid_t file_type, const PixelSpec& spec) {
  H5Handle native_type{H5Tget_native_type(file_type, H5T_DIR_ASCEND), H5Tclose};
  if (!native_type.valid()) {
    return false;
  }
  switch (spec.data_type) {
    case ImageDataType::unsigned_integer_16:
      return matches_integer_pixel_type(native_type.get(), sizeof(std::uint16_t), H5T_SGN_NONE);
    case ImageDataType::signed_integer_16:
      return matches_integer_pixel_type(native_type.get(), sizeof(std::int16_t), H5T_SGN_2);
    case ImageDataType::unsigned_integer_32:
      return matches_integer_pixel_type(native_type.get(), sizeof(std::uint32_t), H5T_SGN_NONE);
    case ImageDataType::signed_integer_32:
      return matches_integer_pixel_type(native_type.get(), sizeof(std::int32_t), H5T_SGN_2);
    case ImageDataType::real_32:
      return matches_real_pixel_type(native_type.get(), sizeof(float));
    case ImageDataType::real_64:
      return matches_real_pixel_type(native_type.get(), sizeof(double));
    case ImageDataType::complex_32:
      return matches_complex_pixel_type(native_type.get(), sizeof(float));
    case ImageDataType::complex_64:
      return matches_complex_pixel_type(native_type.get(), sizeof(double));
  }
  return false;
}

[[nodiscard]] bool pixel_bytes(const ISMRMRD::ISMRMRD_ImageHeader& header, const PixelSpec& spec, std::size_t& value) {
  if (header.matrix_size[0] == 0U || header.matrix_size[1] == 0U || header.matrix_size[2] == 0U ||
      header.channels == 0U) {
    return false;
  }
  std::size_t elements = header.matrix_size[0];
  if (!checked_multiply(elements, header.matrix_size[1], elements) ||
      !checked_multiply(elements, header.matrix_size[2], elements) ||
      !checked_multiply(elements, header.channels, elements) ||
      !checked_multiply(elements, spec.bytes_per_element, value)) {
    return false;
  }
  return true;
}

[[nodiscard]] AcquisitionHeader copy_acquisition_header(const ISMRMRD::ISMRMRD_AcquisitionHeader& source) {
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
  result.index.kspace_encode_step_1 = source.idx.kspace_encode_step_1;
  result.index.kspace_encode_step_2 = source.idx.kspace_encode_step_2;
  result.index.average = source.idx.average;
  result.index.slice = source.idx.slice;
  result.index.contrast = source.idx.contrast;
  result.index.phase = source.idx.phase;
  result.index.repetition = source.idx.repetition;
  result.index.set = source.idx.set;
  result.index.segment = source.idx.segment;
  std::copy(std::begin(source.idx.user), std::end(source.idx.user), result.index.user.begin());
  std::copy(std::begin(source.user_int), std::end(source.user_int), result.user_int.begin());
  std::copy(std::begin(source.user_float), std::end(source.user_float), result.user_float.begin());
  return result;
}

[[nodiscard]] InspectionImageHeader copy_image_header(const ISMRMRD::ISMRMRD_ImageHeader& source,
                                                      const PixelSpec& spec) {
  InspectionImageHeader result;
  result.version = source.version;
  result.data_type = spec.data_type;
  result.flags = source.flags;
  result.measurement_uid = source.measurement_uid;
  std::copy(std::begin(source.matrix_size), std::end(source.matrix_size), result.matrix_size.begin());
  std::copy(std::begin(source.field_of_view), std::end(source.field_of_view), result.field_of_view_mm.begin());
  result.channels = source.channels;
  std::copy(std::begin(source.position), std::end(source.position), result.position.begin());
  std::copy(std::begin(source.read_dir), std::end(source.read_dir), result.read_dir.begin());
  std::copy(std::begin(source.phase_dir), std::end(source.phase_dir), result.phase_dir.begin());
  std::copy(std::begin(source.slice_dir), std::end(source.slice_dir), result.slice_dir.begin());
  std::copy(std::begin(source.patient_table_position), std::end(source.patient_table_position),
            result.patient_table_position.begin());
  result.average = source.average;
  result.slice = source.slice;
  result.contrast = source.contrast;
  result.phase = source.phase;
  result.repetition = source.repetition;
  result.set = source.set;
  result.acquisition_time_stamp = source.acquisition_time_stamp;
  std::copy(std::begin(source.physiology_time_stamp), std::end(source.physiology_time_stamp),
            result.physiology_time_stamp.begin());
  result.image_type = source.image_type;
  result.image_index = source.image_index;
  result.image_series_index = source.image_series_index;
  std::copy(std::begin(source.user_int), std::end(source.user_int), result.user_int.begin());
  std::copy(std::begin(source.user_float), std::end(source.user_float), result.user_float.begin());
  result.attribute_string_bytes = source.attribute_string_len;
  return result;
}

[[nodiscard]] bool parse_meta_attributes(const std::string& xml, const InspectionReadLimits& limits,
                                         std::vector<MetaAttribute>& attributes, std::string& error) {
  attributes.clear();
  if (xml.empty()) {
    return true;
  }
  try {
    ISMRMRD::MetaContainer source;
    ISMRMRD::deserialize(xml.c_str(), source);
    std::size_t entry_count = 0U;
    std::vector<MetaAttribute> next;
    for (const auto& [name, values] : source) {
      if (name.empty() || name.size() > limits.max_meta_attribute_name_bytes ||
          values.size() > limits.max_meta_attribute_entries - entry_count) {
        error = "ISMRMRD image MetaAttributes exceed inspection limits.";
        return false;
      }
      MetaAttribute copied;
      copied.name = name;
      copied.values.reserve(values.size());
      for (const auto& value : values) {
        const std::string text(value.as_str());
        if (text.size() > limits.max_meta_attribute_value_bytes) {
          error = "ISMRMRD image MetaAttributes exceed inspection limits.";
          return false;
        }
        copied.values.push_back(text);
        ++entry_count;
      }
      next.push_back(std::move(copied));
    }
    attributes = std::move(next);
    return true;
  } catch (...) {
    error = "ISMRMRD image MetaAttributes are malformed.";
    return false;
  }
}

[[nodiscard]] bool enumerate_image_series(const hid_t file_id, const std::string& group,
                                          const InspectionReadLimits& limits,
                                          std::vector<InspectionImageSeriesDescriptor>& result, std::string& error) {
  H5Handle group_handle{H5Gopen2(file_id, group.c_str(), H5P_DEFAULT), H5Gclose};
  if (!group_handle.valid()) {
    error = "ISMRMRD dataset group was not found.";
    return false;
  }
  H5G_info_t group_info{};
  if (H5Gget_info(group_handle.get(), &group_info) < 0 || group_info.nlinks > limits.max_hdf5_group_links) {
    error = "ISMRMRD dataset group exceeds inspection limits.";
    return false;
  }

  std::vector<InspectionImageSeriesDescriptor> next;
  for (hsize_t link_index = 0U; link_index < group_info.nlinks; ++link_index) {
    const auto name_length =
      H5Lget_name_by_idx(group_handle.get(), ".", H5_INDEX_NAME, H5_ITER_INC, link_index, nullptr, 0U, H5P_DEFAULT);
    if (name_length < 0 || static_cast<std::size_t>(name_length) > limits.max_image_series_name_bytes) {
      error = "ISMRMRD dataset group link name exceeds inspection limits.";
      return false;
    }
    std::string name(static_cast<std::size_t>(name_length) + 1U, '\0');
    if (H5Lget_name_by_idx(group_handle.get(), ".", H5_INDEX_NAME, H5_ITER_INC, link_index, name.data(), name.size(),
                           H5P_DEFAULT) < 0) {
      error = "ISMRMRD dataset group could not be enumerated.";
      return false;
    }
    name.resize(static_cast<std::size_t>(name_length));
    if (name == "xml" || name == "data" || name == "waveforms" || name.starts_with("ksj_")) {
      continue;
    }

    H5Handle series_group{H5Gopen2(group_handle.get(), name.c_str(), H5P_DEFAULT), H5Gclose};
    if (!series_group.valid()) {
      continue;
    }
    const auto header_exists = H5Lexists(series_group.get(), "header", H5P_DEFAULT);
    const auto attributes_exists = H5Lexists(series_group.get(), "attributes", H5P_DEFAULT);
    const auto data_exists = H5Lexists(series_group.get(), "data", H5P_DEFAULT);
    if (header_exists < 0 || attributes_exists < 0 || data_exists < 0) {
      error = "ISMRMRD image series is malformed.";
      return false;
    }
    const auto component_count = static_cast<unsigned>(header_exists > 0) +
                                 static_cast<unsigned>(attributes_exists > 0) + static_cast<unsigned>(data_exists > 0);
    if (component_count == 0U) {
      continue;
    }
    if (component_count != 3U) {
      continue;
    }

    hsize_t image_count = 0U;
    if (!dataset_record_count(file_id, make_dataset_path(make_dataset_path(group, name), "header"), image_count) ||
        image_count > limits.max_images_per_series || image_count > std::numeric_limits<std::uint32_t>::max()) {
      error = "ISMRMRD image series exceeds inspection limits.";
      return false;
    }
    if (next.size() >= limits.max_image_series) {
      error = "ISMRMRD image series count exceeds inspection limits.";
      return false;
    }
    next.push_back({.series_id = std::move(name), .image_count = static_cast<std::uint32_t>(image_count)});
  }
  std::sort(next.begin(), next.end(), [](const auto& left, const auto& right) {
    return left.series_id < right.series_id;
  });
  result = std::move(next);
  return true;
}

// Discovery and open preflight never materialize pixels or acquisition
// payloads. A standalone ISMRMRD image-series group has its standard three
// direct datasets and can be opened without requiring an XML-header owner.
[[nodiscard]] bool inspect_standalone_image_series(const hid_t file_id, const std::string& group,
                                                   const InspectionReadLimits& limits, std::uint32_t& image_count) {
  image_count = 0U;
  bool header_exists = false;
  bool attributes_exists = false;
  bool data_exists = false;
  if (!h5_link_exists(file_id, make_dataset_path(group, "header"), header_exists) ||
      !h5_link_exists(file_id, make_dataset_path(group, "attributes"), attributes_exists) ||
      !h5_link_exists(file_id, make_dataset_path(group, "data"), data_exists) || !header_exists || !attributes_exists ||
      !data_exists) {
    return false;
  }

  const auto header_path = make_dataset_path(group, "header");
  H5Handle header_dataset{H5Dopen2(file_id, header_path.c_str(), H5P_DEFAULT), H5Dclose};
  H5Handle header_type{header_dataset.valid() ? H5Dget_type(header_dataset.get()) : H5I_INVALID_HID, H5Tclose};
  if (!header_dataset.valid() || !header_type.valid() || !matches_image_header_type(header_type.get())) {
    return false;
  }
  hsize_t native_image_count = 0U;
  if (!dataset_record_count(file_id, header_path, native_image_count) ||
      native_image_count > limits.max_images_per_series ||
      native_image_count > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  const auto attributes_path = make_dataset_path(group, "attributes");
  H5Handle attributes_dataset{H5Dopen2(file_id, attributes_path.c_str(), H5P_DEFAULT), H5Dclose};
  H5Handle attributes_type{attributes_dataset.valid() ? H5Dget_type(attributes_dataset.get()) : H5I_INVALID_HID,
                           H5Tclose};
  hsize_t attributes_count = 0U;
  if (!attributes_dataset.valid() || !attributes_type.valid() || H5Tget_class(attributes_type.get()) != H5T_STRING ||
      H5Tis_variable_str(attributes_type.get()) <= 0 ||
      !dataset_record_count(file_id, attributes_path, attributes_count) || attributes_count != native_image_count) {
    return false;
  }

  const auto data_path = make_dataset_path(group, "data");
  H5Handle data_dataset{H5Dopen2(file_id, data_path.c_str(), H5P_DEFAULT), H5Dclose};
  H5Handle data_space{data_dataset.valid() ? H5Dget_space(data_dataset.get()) : H5I_INVALID_HID, H5Sclose};
  if (!data_dataset.valid() || !data_space.valid() || H5Sget_simple_extent_ndims(data_space.get()) != 5) {
    return false;
  }
  std::array<hsize_t, 5U> dimensions{};
  if (H5Sget_simple_extent_dims(data_space.get(), dimensions.data(), nullptr) < 0 ||
      dimensions.front() != native_image_count) {
    return false;
  }

  image_count = static_cast<std::uint32_t>(native_image_count);
  return true;
}

[[nodiscard]] bool has_standard_waveforms(const hid_t file_id, const std::string& group) {
  const auto waveform_path = make_dataset_path(group, "waveforms");
  bool exists = false;
  if (!h5_link_exists(file_id, waveform_path, exists) || !exists) {
    return false;
  }
  H5Handle waveform_dataset{H5Dopen2(file_id, waveform_path.c_str(), H5P_DEFAULT), H5Dclose};
  H5Handle waveform_type{waveform_dataset.valid() ? H5Dget_type(waveform_dataset.get()) : H5I_INVALID_HID, H5Tclose};
  H5Handle waveform_space{waveform_dataset.valid() ? H5Dget_space(waveform_dataset.get()) : H5I_INVALID_HID, H5Sclose};
  return waveform_dataset.valid() && waveform_type.valid() && waveform_space.valid() &&
         H5Sget_simple_extent_ndims(waveform_space.get()) == 1 && matches_waveform_type(waveform_type.get());
}

[[nodiscard]] const InspectionImageSeriesDescriptor* find_image_series(const InspectionDatasetMetadata& metadata,
                                                                       const std::string& series_id) {
  const auto found = std::find_if(metadata.image_series.begin(), metadata.image_series.end(),
                                  [&](const InspectionImageSeriesDescriptor& descriptor) {
                                    return descriptor.series_id == series_id;
                                  });
  return found == metadata.image_series.end() ? nullptr : &*found;
}

[[nodiscard]] bool validate_image_data_layout(const hid_t file_id, const std::string& path,
                                              const InspectionImageSeriesDescriptor& series,
                                              const ISMRMRD::ISMRMRD_ImageHeader& header, const PixelSpec& spec,
                                              const InspectionReadLimits& limits, std::size_t& payload_bytes,
                                              std::string& error) {
  if (!pixel_bytes(header, spec, payload_bytes)) {
    error = "ISMRMRD image dimensions are invalid.";
    return false;
  }
  if (payload_bytes > limits.max_image_payload_bytes) {
    error = "ISMRMRD image payload exceeds inspection limit.";
    return false;
  }

  H5Handle dataset{H5Dopen2(file_id, path.c_str(), H5P_DEFAULT), H5Dclose};
  H5Handle file_type{dataset.valid() ? H5Dget_type(dataset.get()) : H5I_INVALID_HID, H5Tclose};
  H5Handle dataspace{dataset.valid() ? H5Dget_space(dataset.get()) : H5I_INVALID_HID, H5Sclose};
  if (!dataset.valid() || !file_type.valid() || !dataspace.valid() ||
      !matches_image_pixel_type(file_type.get(), spec) || H5Sget_simple_extent_ndims(dataspace.get()) != 5) {
    error = "ISMRMRD image pixel dataset is malformed.";
    return false;
  }
  std::array<hsize_t, 5> dimensions{};
  if (H5Sget_simple_extent_dims(dataspace.get(), dimensions.data(), nullptr) < 0 ||
      dimensions[0] != series.image_count || dimensions[1] != header.channels ||
      dimensions[2] != header.matrix_size[2] || dimensions[3] != header.matrix_size[1] ||
      dimensions[4] != header.matrix_size[0]) {
    error = "ISMRMRD image pixel dataset does not match its header.";
    return false;
  }
  return true;
}

[[nodiscard]] bool read_image_record_impl(InspectionDataset& dataset, const InspectionDatasetMetadata& metadata,
                                          const std::string_view image_series_parent_group,
                                          const InspectionReadLimits& limits, const ImageLocator& locator,
                                          InspectionImageRecord& record, std::string& error) {
  record = {};
  const auto* series = find_image_series(metadata, locator.series_id);
  if (series == nullptr) {
    error = "ISMRMRD image series was not found.";
    return false;
  }
  if (locator.ordinal >= series->image_count) {
    error = "ISMRMRD image ordinal is outside its series.";
    return false;
  }

  const auto series_path = make_dataset_path(image_series_parent_group, locator.series_id);
  ISMRMRD::ISMRMRD_ImageHeader raw_header{};
  if (!read_image_header(dataset.hdf5_file_id(), make_dataset_path(series_path, "header"), locator.ordinal, raw_header,
                         error)) {
    return false;
  }
  PixelSpec spec{};
  if (!pixel_spec(raw_header.data_type, spec)) {
    error = "ISMRMRD image data type is unsupported.";
    return false;
  }
  std::size_t ignored_pixel_bytes = 0U;
  if (!pixel_bytes(raw_header, spec, ignored_pixel_bytes)) {
    error = "ISMRMRD image dimensions are invalid.";
    return false;
  }

  std::string attributes_xml;
  if (!read_vlen_string(dataset.hdf5_file_id(), make_dataset_path(series_path, "attributes"), locator.ordinal,
                        limits.max_image_attribute_bytes, "ISMRMRD image attributes exceed inspection limit.",
                        attributes_xml, error)) {
    return false;
  }
  if (attributes_xml.size() != raw_header.attribute_string_len) {
    error = "ISMRMRD image attributes do not match their header.";
    return false;
  }
  std::vector<MetaAttribute> attributes;
  if (!parse_meta_attributes(attributes_xml, limits, attributes, error)) {
    return false;
  }

  record = {
    .locator = locator,
    .header = copy_image_header(raw_header, spec),
    .meta_attributes = std::move(attributes),
  };
  return true;
}

[[nodiscard]] bool acquisition_payload_bytes(const ISMRMRD::ISMRMRD_AcquisitionHeader& header, std::size_t& samples,
                                             std::size_t& trajectory, std::size_t& total) {
  samples = header.number_of_samples;
  trajectory = header.number_of_samples;
  if (!checked_multiply(samples, header.active_channels, samples) ||
      !checked_multiply(samples, sizeof(std::complex<float>), samples) ||
      !checked_multiply(trajectory, header.trajectory_dimensions, trajectory) ||
      !checked_multiply(trajectory, sizeof(float), trajectory) || !checked_add(samples, trajectory, total)) {
    return false;
  }
  return true;
}

[[nodiscard]] InspectionIterationResult
visit_acquisition_impl(InspectionDataset& dataset, const InspectionDatasetMetadata& metadata,
                       const InspectionReadLimits& limits, const std::uint32_t ordinal,
                       const InspectionAcquisitionConsumer& consumer, std::string& error) {
  const auto data_path = make_dataset_path(metadata.group, "data");
  ISMRMRD::ISMRMRD_AcquisitionHeader raw_header{};
  if (!read_acquisition_header_impl(dataset.hdf5_file_id(), data_path, ordinal, raw_header, error)) {
    return InspectionIterationResult::failed;
  }
  std::size_t sample_bytes = 0U;
  std::size_t trajectory_bytes = 0U;
  std::size_t expected_payload_bytes = 0U;
  if (!acquisition_payload_bytes(raw_header, sample_bytes, trajectory_bytes, expected_payload_bytes)) {
    error = "ISMRMRD acquisition payload size overflows.";
    return InspectionIterationResult::failed;
  }
  if (expected_payload_bytes > limits.max_acquisition_payload_bytes) {
    error = "ISMRMRD acquisition payload exceeds inspection limit.";
    return InspectionIterationResult::failed;
  }
  std::size_t actual_sample_bytes = 0U;
  std::size_t actual_trajectory_bytes = 0U;
  if (!selected_float_vlen_member_bytes(dataset.hdf5_file_id(), data_path, ordinal, "data", actual_sample_bytes) ||
      !selected_float_vlen_member_bytes(dataset.hdf5_file_id(), data_path, ordinal, "traj", actual_trajectory_bytes)) {
    error = "ISMRMRD acquisition payload is malformed.";
    return InspectionIterationResult::failed;
  }
  std::size_t actual_payload_bytes = 0U;
  if (!checked_add(actual_sample_bytes, actual_trajectory_bytes, actual_payload_bytes)) {
    error = "ISMRMRD acquisition payload size overflows.";
    return InspectionIterationResult::failed;
  }
  if (actual_payload_bytes > limits.max_acquisition_payload_bytes) {
    error = "ISMRMRD acquisition payload exceeds inspection limit.";
    return InspectionIterationResult::failed;
  }
  if (actual_sample_bytes != sample_bytes || actual_trajectory_bytes != trajectory_bytes ||
      actual_payload_bytes != expected_payload_bytes) {
    error = "ISMRMRD acquisition payload does not match its header.";
    return InspectionIterationResult::failed;
  }

  try {
    ISMRMRD::Acquisition acquisition;
    dataset.readAcquisition(ordinal, acquisition);
    if (acquisition.getNumberOfDataElements() * sizeof(std::complex<float>) != sample_bytes ||
        acquisition.getNumberOfTrajElements() * sizeof(float) != trajectory_bytes) {
      error = "ISMRMRD acquisition payload does not match its header.";
      return InspectionIterationResult::failed;
    }
    const auto sample_count = acquisition.getNumberOfDataElements();
    const auto trajectory_count = acquisition.getNumberOfTrajElements();
    const auto samples = sample_count == 0U
                           ? std::span<const std::complex<float>>{}
                           : std::span<const std::complex<float>>(acquisition.getDataPtr(), sample_count);
    const auto trajectory = trajectory_count == 0U ? std::span<const float>{}
                                                   : std::span<const float>(acquisition.getTrajPtr(), trajectory_count);
    const InspectionAcquisitionView view{
      .ordinal = ordinal,
      .header = copy_acquisition_header(raw_header),
      .samples = samples,
      .trajectory = trajectory,
    };
    try {
      return consumer(view) ? InspectionIterationResult::completed : InspectionIterationResult::stopped;
    } catch (...) {
      error = "ISMRMRD inspection acquisition consumer threw.";
      return InspectionIterationResult::failed;
    }
  } catch (...) {
    error = "ISMRMRD acquisition payload could not be read.";
    return InspectionIterationResult::failed;
  }
}

class OperationGuard final {
public:
  OperationGuard(bool& active, std::string& error) : active_(active) {
    if (active_) {
      error = "ISMRMRD inspection reader does not support nested reads.";
      return;
    }
    active_ = true;
    entered_ = true;
  }

  ~OperationGuard() {
    if (entered_) {
      active_ = false;
    }
  }

  [[nodiscard]] bool entered() const noexcept { return entered_; }

private:
  bool& active_;
  bool entered_{false};
};

} // namespace

struct InspectionReader::Impl {
  explicit Impl(std::unique_ptr<InspectionDataset> source_dataset, std::string next_image_series_parent_group,
                const bool next_standalone_image_series)
      : dataset(std::move(source_dataset)), image_series_parent_group(std::move(next_image_series_parent_group)),
        standalone_image_series(next_standalone_image_series) {}

  std::unique_ptr<InspectionDataset> dataset;
  std::string image_series_parent_group;
  bool standalone_image_series{false};
  bool operation_active{false};
};

InspectionReader::InspectionReader() = default;
InspectionReader::~InspectionReader() = default;
InspectionReader::InspectionReader(InspectionReader&&) noexcept = default;
InspectionReader& InspectionReader::operator=(InspectionReader&&) noexcept = default;

bool InspectionReader::discover_mrd_containers(const std::filesystem::path& file, const InspectionReadLimits limits,
                                               std::vector<InspectionMrdContainerDescriptor>& containers,
                                               std::string& error) {
  error.clear();
  containers.clear();
  if (file.empty()) {
    error = "ISMRMRD inspection input path must not be empty.";
    return false;
  }
  if (!validate_limits(limits, error)) {
    return false;
  }

  struct PendingGroup {
    std::string path;
    std::uint32_t depth{0U};
  };

  try {
    hid_t file_id = H5I_INVALID_HID;
    H5E_BEGIN_TRY {
      file_id = H5Fopen(file.string().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    }
    H5E_END_TRY;
    H5Handle source_file{file_id, H5Fclose};
    if (!source_file.valid()) {
      error = "Unable to open standard ISMRMRD HDF5 input.";
      return false;
    }

    std::vector<PendingGroup> pending{{.path = "/", .depth = 0U}};
    // A group can be reachable through multiple hard links, including a link
    // cycle. HDF5 1.14 identifies objects through opaque tokens rather than
    // the legacy address field, so retain the tokens we have already visited.
    std::vector<H5O_token_t> visited_group_tokens;
    visited_group_tokens.reserve(limits.max_hdf5_group_count);
    std::vector<InspectionMrdContainerDescriptor> next_containers;

    while (!pending.empty()) {
      const auto current = std::move(pending.back());
      pending.pop_back();

      hid_t group_id = H5I_INVALID_HID;
      H5E_BEGIN_TRY {
        group_id = H5Gopen2(source_file.get(), current.path.c_str(), H5P_DEFAULT);
      }
      H5E_END_TRY;
      H5Handle group_handle{group_id, H5Gclose};
      H5O_info2_t object_info{};
      if (!group_handle.valid() || H5Oget_info3(group_handle.get(), &object_info, H5O_INFO_BASIC) < 0) {
        continue;
      }
      bool previously_visited = false;
      for (const auto& visited_token : visited_group_tokens) {
        int comparison = 0;
        if (H5Otoken_cmp(source_file.get(), &visited_token, &object_info.token, &comparison) < 0) {
          containers.clear();
          error = "Unable to identify an ISMRMRD HDF5 group during inspection.";
          return false;
        }
        if (comparison == 0) {
          previously_visited = true;
          break;
        }
      }
      if (previously_visited) {
        continue;
      }
      if (visited_group_tokens.size() >= limits.max_hdf5_group_count) {
        containers.clear();
        error = "ISMRMRD HDF5 group count exceeds inspection limits.";
        return false;
      }
      visited_group_tokens.push_back(object_info.token);

      InspectionReader candidate;
      std::string candidate_error;
      if (candidate.open(file, current.path, limits, candidate_error)) {
        const auto& metadata = candidate.metadata();
        bool has_xml_header = false;
        bool has_acquisition_dataset = false;
        if (h5_link_exists(source_file.get(), make_dataset_path(current.path, "xml"), has_xml_header) &&
            h5_link_exists(source_file.get(), make_dataset_path(current.path, "data"), has_acquisition_dataset)) {
          next_containers.push_back(
            {.path = current.path,
             .has_header = has_xml_header,
             .has_acquisitions = has_xml_header && has_acquisition_dataset,
             .has_waveforms = has_xml_header && has_standard_waveforms(source_file.get(), current.path),
             .has_images = !metadata.image_series.empty(),
             .acquisition_count = metadata.acquisition_count,
             .image_series_count = static_cast<std::uint32_t>(metadata.image_series.size())});
        }
      }

      if (current.depth >= limits.max_hdf5_group_depth) {
        continue;
      }
      H5G_info_t group_info{};
      if (H5Gget_info(group_handle.get(), &group_info) < 0 ||
          group_info.nlinks > static_cast<hsize_t>(limits.max_hdf5_group_links)) {
        continue;
      }
      for (hsize_t link_index = 0U; link_index < group_info.nlinks; ++link_index) {
        const auto name_length =
          H5Lget_name_by_idx(group_handle.get(), ".", H5_INDEX_NAME, H5_ITER_INC, link_index, nullptr, 0U, H5P_DEFAULT);
        if (name_length <= 0 || static_cast<std::size_t>(name_length) > limits.max_hdf5_group_name_bytes) {
          continue;
        }
        std::string name(static_cast<std::size_t>(name_length) + 1U, '\0');
        if (H5Lget_name_by_idx(group_handle.get(), ".", H5_INDEX_NAME, H5_ITER_INC, link_index, name.data(),
                               name.size(), H5P_DEFAULT) < 0) {
          continue;
        }
        name.resize(static_cast<std::size_t>(name_length));

        H5L_info_t link_info{};
        if (H5Lget_info(group_handle.get(), name.c_str(), &link_info, H5P_DEFAULT) < 0 ||
            link_info.type != H5L_TYPE_HARD) {
          continue;
        }
        hid_t child_group_id = H5I_INVALID_HID;
        H5E_BEGIN_TRY {
          child_group_id = H5Gopen2(group_handle.get(), name.c_str(), H5P_DEFAULT);
        }
        H5E_END_TRY;
        H5Handle child_group{child_group_id, H5Gclose};
        if (!child_group.valid()) {
          continue;
        }
        std::string child_path = current.path == "/" ? "/" + name : current.path + "/" + name;
        if (child_path.size() > limits.max_hdf5_group_path_bytes) {
          continue;
        }
        pending.push_back({.path = std::move(child_path), .depth = current.depth + 1U});
      }
    }

    if (next_containers.empty()) {
      error = "No readable standard ISMRMRD containers were found.";
      return false;
    }
    std::sort(next_containers.begin(), next_containers.end(), [](const auto& left, const auto& right) {
      return left.path < right.path;
    });
    containers = std::move(next_containers);
    return true;
  } catch (...) {
    containers.clear();
    error = "Unable to open standard ISMRMRD HDF5 input.";
    return false;
  }
}

bool InspectionReader::open(const std::filesystem::path& file, std::string container_path,
                            const InspectionReadLimits limits, std::string& error) {
  error.clear();
  if (impl_ != nullptr && impl_->operation_active) {
    error = "ISMRMRD inspection reader does not support nested reads.";
    return false;
  }
  const auto clear_state = [this]() {
    impl_.reset();
    metadata_ = {};
    limits_ = {};
  };
  if (file.empty()) {
    clear_state();
    error = "ISMRMRD inspection input path must not be empty.";
    return false;
  }
  if (container_path.empty()) {
    clear_state();
    error = "ISMRMRD inspection container path must not be empty.";
    return false;
  }
  if (!validate_limits(limits, error)) {
    clear_state();
    return false;
  }

  try {
    auto source_dataset = std::make_unique<InspectionDataset>(file.string().c_str(), container_path.c_str(), false);
    if (source_dataset->hdf5_file_id() < 0) {
      clear_state();
      error = "Unable to open standard ISMRMRD HDF5 input.";
      return false;
    }
    const auto file_id = source_dataset->hdf5_file_id();
    const auto xml_path = make_dataset_path(container_path, "xml");
    bool xml_exists = false;
    if (!h5_link_exists(file_id, xml_path, xml_exists)) {
      clear_state();
      error = "ISMRMRD container could not be inspected.";
      return false;
    }

    if (xml_exists) {
      InspectionDatasetMetadata next_metadata;
      next_metadata.group = container_path;
      if (!read_vlen_string(file_id, xml_path, 0U, limits.max_xml_header_bytes,
                            "ISMRMRD XML header exceeds inspection limit.", next_metadata.xml_header, error)) {
        clear_state();
        return false;
      }

      const auto acquisition_path = make_dataset_path(container_path, "data");
      bool acquisitions_exist = false;
      if (!h5_link_exists(file_id, acquisition_path, acquisitions_exist)) {
        clear_state();
        error = "ISMRMRD acquisition dataset could not be inspected.";
        return false;
      }
      if (acquisitions_exist) {
        hsize_t acquisition_count = 0U;
        if (!dataset_record_count(file_id, acquisition_path, acquisition_count) ||
            acquisition_count > limits.max_acquisition_count ||
            acquisition_count > std::numeric_limits<std::uint32_t>::max()) {
          clear_state();
          error = "ISMRMRD acquisition count exceeds inspection limit.";
          return false;
        }
        next_metadata.acquisition_count = static_cast<std::uint32_t>(acquisition_count);
      }

      if (!enumerate_image_series(file_id, container_path, limits, next_metadata.image_series, error)) {
        clear_state();
        return false;
      }

      impl_ = std::make_shared<Impl>(std::move(source_dataset), container_path, false);
      metadata_ = std::move(next_metadata);
      limits_ = limits;
      return true;
    }

    std::uint32_t image_count = 0U;
    if (!inspect_standalone_image_series(file_id, container_path, limits, image_count)) {
      clear_state();
      error = "ISMRMRD container is neither a standard XML-header container nor a standard image series.";
      return false;
    }
    ImageSeriesLocation image_series_location;
    if (!split_image_series_path(container_path, image_series_location)) {
      clear_state();
      error = "ISMRMRD standalone image-series path is invalid.";
      return false;
    }
    auto image_dataset =
      std::make_unique<InspectionDataset>(file.string().c_str(), image_series_location.parent_group.c_str(), false);
    if (image_dataset->hdf5_file_id() < 0) {
      clear_state();
      error = "Unable to open standard ISMRMRD HDF5 input.";
      return false;
    }

    InspectionDatasetMetadata next_metadata;
    next_metadata.group = container_path;
    next_metadata.image_series.push_back(
      {.series_id = std::move(image_series_location.series_id), .image_count = image_count});
    impl_ = std::make_shared<Impl>(std::move(image_dataset), image_series_location.parent_group, true);
    metadata_ = std::move(next_metadata);
    limits_ = limits;
    return true;
  } catch (...) {
    clear_state();
    error = "Unable to open standard ISMRMRD HDF5 input.";
    return false;
  }
}

bool InspectionReader::is_open() const noexcept {
  return impl_ != nullptr;
}

const InspectionDatasetMetadata& InspectionReader::metadata() const noexcept {
  static const InspectionDatasetMetadata empty_metadata{};
  return impl_ == nullptr ? empty_metadata : metadata_;
}

bool InspectionReader::read_acquisition_header(const std::uint32_t ordinal, AcquisitionHeader& header,
                                               std::string& error) {
  error.clear();
  header = {};
  const auto impl = impl_;
  const auto metadata = metadata_;
  if (impl == nullptr) {
    error = "ISMRMRD inspection reader is not open.";
    return false;
  }
  if (ordinal >= metadata.acquisition_count) {
    error = "ISMRMRD acquisition ordinal is outside the dataset.";
    return false;
  }
  OperationGuard guard(impl->operation_active, error);
  if (!guard.entered()) {
    return false;
  }

  ISMRMRD::ISMRMRD_AcquisitionHeader raw_header{};
  if (!read_acquisition_header_impl(impl->dataset->hdf5_file_id(), make_dataset_path(metadata.group, "data"), ordinal,
                                    raw_header, error)) {
    return false;
  }
  header = copy_acquisition_header(raw_header);
  return true;
}

InspectionIterationResult
InspectionReader::for_each_acquisition_header(const InspectionAcquisitionHeaderConsumer& consumer, std::string& error) {
  error.clear();
  const auto impl = impl_;
  const auto metadata = metadata_;
  if (impl == nullptr) {
    error = "ISMRMRD inspection reader is not open.";
    return InspectionIterationResult::failed;
  }
  if (!consumer) {
    error = "ISMRMRD inspection acquisition header consumer must not be empty.";
    return InspectionIterationResult::failed;
  }
  OperationGuard guard(impl->operation_active, error);
  if (!guard.entered()) {
    return InspectionIterationResult::failed;
  }

  const auto data_path = make_dataset_path(metadata.group, "data");
  for (std::uint32_t ordinal = 0U; ordinal < metadata.acquisition_count; ++ordinal) {
    ISMRMRD::ISMRMRD_AcquisitionHeader raw_header{};
    if (!read_acquisition_header_impl(impl->dataset->hdf5_file_id(), data_path, ordinal, raw_header, error)) {
      return InspectionIterationResult::failed;
    }
    const InspectionAcquisitionHeaderRecord record{
      .ordinal = ordinal,
      .header = copy_acquisition_header(raw_header),
    };
    try {
      if (!consumer(record)) {
        return InspectionIterationResult::stopped;
      }
    } catch (...) {
      error = "ISMRMRD inspection acquisition header consumer threw.";
      return InspectionIterationResult::failed;
    }
  }
  return InspectionIterationResult::completed;
}

InspectionIterationResult InspectionReader::visit_acquisition(const std::uint32_t ordinal,
                                                              const InspectionAcquisitionConsumer& consumer,
                                                              std::string& error) {
  error.clear();
  const auto impl = impl_;
  const auto metadata = metadata_;
  const auto limits = limits_;
  if (impl == nullptr) {
    error = "ISMRMRD inspection reader is not open.";
    return InspectionIterationResult::failed;
  }
  if (!consumer) {
    error = "ISMRMRD inspection acquisition consumer must not be empty.";
    return InspectionIterationResult::failed;
  }
  if (ordinal >= metadata.acquisition_count) {
    error = "ISMRMRD acquisition ordinal is outside the dataset.";
    return InspectionIterationResult::failed;
  }
  OperationGuard guard(impl->operation_active, error);
  if (!guard.entered()) {
    return InspectionIterationResult::failed;
  }
  return visit_acquisition_impl(*impl->dataset, metadata, limits, ordinal, consumer, error);
}

InspectionIterationResult InspectionReader::for_each_acquisition(const InspectionAcquisitionConsumer& consumer,
                                                                 std::string& error) {
  error.clear();
  const auto impl = impl_;
  const auto metadata = metadata_;
  const auto limits = limits_;
  if (impl == nullptr) {
    error = "ISMRMRD inspection reader is not open.";
    return InspectionIterationResult::failed;
  }
  if (!consumer) {
    error = "ISMRMRD inspection acquisition consumer must not be empty.";
    return InspectionIterationResult::failed;
  }
  OperationGuard guard(impl->operation_active, error);
  if (!guard.entered()) {
    return InspectionIterationResult::failed;
  }
  for (std::uint32_t ordinal = 0U; ordinal < metadata.acquisition_count; ++ordinal) {
    const auto result = visit_acquisition_impl(*impl->dataset, metadata, limits, ordinal, consumer, error);
    if (result != InspectionIterationResult::completed) {
      return result;
    }
  }
  return InspectionIterationResult::completed;
}

bool InspectionReader::read_image_record(const ImageLocator& locator, InspectionImageRecord& record,
                                         std::string& error) {
  error.clear();
  record = {};
  const auto impl = impl_;
  const auto metadata = metadata_;
  const auto limits = limits_;
  if (impl == nullptr) {
    error = "ISMRMRD inspection reader is not open.";
    return false;
  }
  OperationGuard guard(impl->operation_active, error);
  if (!guard.entered()) {
    return false;
  }
  return read_image_record_impl(*impl->dataset, metadata, impl->image_series_parent_group, limits, locator, record,
                                error);
}

InspectionIterationResult InspectionReader::with_image_pixels(const ImageLocator& locator,
                                                              const ImagePixelConsumer& consumer, std::string& error) {
  error.clear();
  const auto impl = impl_;
  const auto metadata = metadata_;
  const auto limits = limits_;
  if (impl == nullptr) {
    error = "ISMRMRD inspection reader is not open.";
    return InspectionIterationResult::failed;
  }
  if (!consumer) {
    error = "ISMRMRD inspection image consumer must not be empty.";
    return InspectionIterationResult::failed;
  }
  OperationGuard guard(impl->operation_active, error);
  if (!guard.entered()) {
    return InspectionIterationResult::failed;
  }

  InspectionImageRecord record;
  if (!read_image_record_impl(*impl->dataset, metadata, impl->image_series_parent_group, limits, locator, record,
                              error)) {
    return InspectionIterationResult::failed;
  }
  const auto* series = find_image_series(metadata, locator.series_id);
  if (series == nullptr) {
    error = "ISMRMRD image series was not found.";
    return InspectionIterationResult::failed;
  }

  const auto series_path = make_dataset_path(impl->image_series_parent_group, locator.series_id);
  ISMRMRD::ISMRMRD_ImageHeader raw_header{};
  if (!read_image_header(impl->dataset->hdf5_file_id(), make_dataset_path(series_path, "header"), locator.ordinal,
                         raw_header, error)) {
    return InspectionIterationResult::failed;
  }
  PixelSpec spec{};
  if (!pixel_spec(raw_header.data_type, spec)) {
    error = "ISMRMRD image data type is unsupported.";
    return InspectionIterationResult::failed;
  }
  std::size_t payload_bytes = 0U;
  if (!validate_image_data_layout(impl->dataset->hdf5_file_id(), make_dataset_path(series_path, "data"), *series,
                                  raw_header, spec, limits, payload_bytes, error)) {
    return InspectionIterationResult::failed;
  }

  try {
    // The upstream binding owns the decoded standard ISMRMRD storage. Preflight
    // above has already bounded the header-declared shape, the physical HDF5
    // extent, type width, and attribute allocation before this call.
    ISMRMRD::Image<std::uint16_t> image;
    impl->dataset->readImage(locator.series_id, locator.ordinal, image);
    if (image.getDataSize() != payload_bytes || image.getHead().data_type != raw_header.data_type) {
      error = "ISMRMRD image pixels do not match their header.";
      return InspectionIterationResult::failed;
    }
    const auto* payload = reinterpret_cast<const std::byte*>(image.getDataPtr());
    const auto pixels =
      payload_bytes == 0U ? std::span<const std::byte>{} : std::span<const std::byte>(payload, payload_bytes);
    const ImagePixelsView view{
      .data_type = spec.data_type,
      .dimensions = {raw_header.matrix_size[0], raw_header.matrix_size[1], raw_header.matrix_size[2],
                     raw_header.channels},
      .pixels = pixels,
    };
    try {
      return consumer(record, view) ? InspectionIterationResult::completed : InspectionIterationResult::stopped;
    } catch (...) {
      error = "ISMRMRD inspection image consumer threw.";
      return InspectionIterationResult::failed;
    }
  } catch (...) {
    error = "ISMRMRD image pixels could not be read.";
    return InspectionIterationResult::failed;
  }
}

} // namespace ksj::ismrmrd
