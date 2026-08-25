#include "kspacejet/ismrmrd/inspection_reader.hpp"

#include <hdf5.h>
#include <ismrmrd/dataset.h>
#include <ismrmrd/meta.h>
#include <ismrmrd/waveform.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

constexpr std::string_view kDatasetGroup{"dataset"};
constexpr std::string_view kImageSeries{"image_inspection"};
constexpr std::string_view kXmlHeader{"<ismrmrdHeader xmlns=\"http://www.ismrm.org/ISMRMRD\"><experimentalConditions>"
                                      "<H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz>"
                                      "</experimentalConditions></ismrmrdHeader>"};

[[nodiscard]] std::filesystem::path make_test_dataset_path(const std::string_view name) {
  const auto directory = std::filesystem::temp_directory_path() / "ksj_ismrmrd_inspection_reader_tests";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  const auto path = directory / (std::string(name) + ".mrd");
  std::filesystem::remove(path, error);
  return path;
}

[[nodiscard]] float expected_pixel(const std::uint16_t x, const std::uint16_t y, const std::uint16_t z,
                                   const std::uint16_t channel) {
  return static_cast<float>(x) + 10.0F * static_cast<float>(y) + 100.0F * static_cast<float>(z) +
         1000.0F * static_cast<float>(channel);
}

void append_acquisition(ISMRMRD::Dataset& dataset, const std::uint32_t scan_counter) {
  ISMRMRD::Acquisition acquisition(3U, 2U, 2U);
  acquisition.measurement_uid() = 17U;
  acquisition.scan_counter() = scan_counter;
  acquisition.acquisition_time_stamp() = 29U + scan_counter;
  acquisition.idx().slice = 3U;
  acquisition.idx().repetition = 5U;
  acquisition.discard_pre() = 1U;
  acquisition.center_sample() = 2U;
  acquisition.sample_time_us() = 4.5F;
  for (std::uint16_t channel = 0U; channel < acquisition.active_channels(); ++channel) {
    for (std::uint16_t sample = 0U; sample < acquisition.number_of_samples(); ++sample) {
      const auto value = static_cast<float>(sample + channel * acquisition.number_of_samples() + scan_counter * 10U);
      acquisition.data(sample, channel) = {value, 100.0F + value};
    }
  }
  for (std::uint16_t sample = 0U; sample < acquisition.number_of_samples(); ++sample) {
    acquisition.traj(0U, sample) = static_cast<float>(sample);
    acquisition.traj(1U, sample) = static_cast<float>(10U + sample);
  }
  dataset.appendAcquisition(acquisition);
}

void append_image(ISMRMRD::Dataset& dataset, const bool malformed_attributes) {
  ISMRMRD::Image<float> image(2U, 3U, 4U, 2U);
  auto& header = image.getHead();
  header.measurement_uid = 61U;
  header.field_of_view[0] = 180.0F;
  header.field_of_view[1] = 190.0F;
  header.field_of_view[2] = 200.0F;
  header.position[0] = 1.0F;
  header.position[1] = 2.0F;
  header.position[2] = 3.0F;
  header.image_type = ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE;
  header.image_index = 7U;
  header.image_series_index = 11U;
  header.user_int[0] = 13;
  header.user_float[0] = 17.5F;
  for (std::uint16_t channel = 0U; channel < image.getNumberOfChannels(); ++channel) {
    for (std::uint16_t z = 0U; z < image.getMatrixSizeZ(); ++z) {
      for (std::uint16_t y = 0U; y < image.getMatrixSizeY(); ++y) {
        for (std::uint16_t x = 0U; x < image.getMatrixSizeX(); ++x) {
          image(x, y, z, channel) = expected_pixel(x, y, z, channel);
        }
      }
    }
  }

  if (malformed_attributes) {
    image.setAttributeString("<not-meta-attributes>");
  } else {
    ISMRMRD::MetaContainer metadata;
    metadata.set("DataRole", "Image");
    metadata.append("WindowCenter", "10");
    metadata.append("WindowCenter", "20");
    std::ostringstream serialized;
    ISMRMRD::serialize(metadata, serialized);
    image.setAttributeString(serialized.str());
  }
  dataset.appendImage(std::string(kImageSeries), image);
}

void append_waveform(ISMRMRD::Dataset& dataset) {
  ISMRMRD::Waveform waveform(3U, 2U);
  waveform.head.measurement_uid = 23U;
  waveform.head.scan_counter = 29U;
  waveform.head.time_stamp = 31U;
  waveform.head.sample_time_us = 2.5F;
  waveform.head.waveform_id = 7U;
  for (std::size_t index = 0U; index < waveform.size(); ++index) {
    waveform.data[index] = static_cast<std::uint32_t>(index + 1U);
  }
  dataset.appendWaveform(waveform);
}

template <typename Pixel> [[nodiscard]] std::array<Pixel, 2U> typed_image_values() {
  if constexpr (std::is_same_v<Pixel, std::complex<float>>) {
    return {Pixel{17.25F, -3.5F}, Pixel{-29.75F, 8.125F}};
  } else if constexpr (std::is_same_v<Pixel, std::complex<double>>) {
    return {Pixel{17.25, -3.5}, Pixel{-29.75, 8.125}};
  } else {
    return {static_cast<Pixel>(17), static_cast<Pixel>(31)};
  }
}

template <typename Pixel> void append_typed_image(ISMRMRD::Dataset& dataset, const std::string_view series_id) {
  ISMRMRD::Image<Pixel> image(2U, 1U, 1U, 1U);
  const auto values = typed_image_values<Pixel>();
  image(0U, 0U, 0U, 0U) = values[0];
  image(1U, 0U, 0U, 0U) = values[1];
  image.setAttributeString("");
  dataset.appendImage(std::string(series_id), image);
}

void write_standard_dataset(const std::filesystem::path& path, const std::string_view group,
                            const bool malformed_attributes) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(group).c_str(), true);
  dataset.writeHeader(std::string(kXmlHeader));
  append_acquisition(dataset, 23U);
  append_acquisition(dataset, 24U);
  append_image(dataset, malformed_attributes);
}

void write_standard_dataset(const std::filesystem::path& path, const bool malformed_attributes = false) {
  write_standard_dataset(path, kDatasetGroup, malformed_attributes);
}

void write_standard_image_artifact(const std::filesystem::path& path, const std::string_view group = kDatasetGroup) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(group).c_str(), true);
  dataset.writeHeader(std::string(kXmlHeader));
  append_image(dataset, false);
}

void append_standard_waveform(const std::filesystem::path& path, const std::string_view group) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(group).c_str(), false);
  append_waveform(dataset);
}

void add_nonstandard_waveforms_dataset(const std::filesystem::path& path, const std::string_view group) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto type = H5Tcreate(H5T_COMPOUND, sizeof(std::uint32_t));
  ASSERT_GE(type, 0);
  ASSERT_GE(H5Tinsert(type, "not_a_waveform_header", 0U, H5T_NATIVE_UINT32), 0);
  const std::array<hsize_t, 1U> dimensions{1U};
  const auto dataspace = H5Screate_simple(static_cast<int>(dimensions.size()), dimensions.data(), nullptr);
  ASSERT_GE(dataspace, 0);
  const auto path_text = "/" + std::string(group) + "/waveforms";
  const auto dataset = H5Dcreate2(file, path_text.c_str(), type, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(dataset, 0);
  const std::uint32_t value = 1U;
  ASSERT_GE(H5Dwrite(dataset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, &value), 0);
  EXPECT_GE(H5Dclose(dataset), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Tclose(type), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void create_empty_root_group(const std::filesystem::path& path, const std::string_view group) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto group_id = H5Gcreate2(file, std::string(group).c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(group_id, 0);
  EXPECT_GE(H5Gclose(group_id), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void create_empty_hdf5_file(const std::filesystem::path& path) {
  const auto filename = path.string();
  const auto file = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void write_uint32_object_attribute(const std::filesystem::path& path, const std::string_view object_path,
                                   const std::string_view attribute_name, const std::uint32_t value) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto object = H5Oopen(file, std::string(object_path).c_str(), H5P_DEFAULT);
  ASSERT_GE(object, 0);
  const auto dataspace = H5Screate(H5S_SCALAR);
  ASSERT_GE(dataspace, 0);
  const auto attribute =
    H5Acreate2(object, std::string(attribute_name).c_str(), H5T_NATIVE_UINT32, dataspace, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(attribute, 0);
  ASSERT_GE(H5Awrite(attribute, H5T_NATIVE_UINT32, &value), 0);
  EXPECT_GE(H5Aclose(attribute), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Oclose(object), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void write_uint32_array_object_attribute(const std::filesystem::path& path, const std::string_view object_path,
                                         const std::string_view attribute_name,
                                         const std::span<const std::uint32_t> values) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto object = H5Oopen(file, std::string(object_path).c_str(), H5P_DEFAULT);
  ASSERT_GE(object, 0);
  const hsize_t element_count = values.size();
  const auto dataspace = H5Screate_simple(1, &element_count, nullptr);
  ASSERT_GE(dataspace, 0);
  const auto attribute =
    H5Acreate2(object, std::string(attribute_name).c_str(), H5T_NATIVE_UINT32, dataspace, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(attribute, 0);
  ASSERT_GE(H5Awrite(attribute, H5T_NATIVE_UINT32, values.data()), 0);
  EXPECT_GE(H5Aclose(attribute), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Oclose(object), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void write_fixed_string_object_attribute(const std::filesystem::path& path, const std::string_view object_path,
                                         const std::string_view attribute_name, const std::string_view value) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto object = H5Oopen(file, std::string(object_path).c_str(), H5P_DEFAULT);
  ASSERT_GE(object, 0);
  const auto type = H5Tcopy(H5T_C_S1);
  ASSERT_GE(type, 0);
  ASSERT_GE(H5Tset_size(type, value.size() + 1U), 0);
  ASSERT_GE(H5Tset_strpad(type, H5T_STR_NULLTERM), 0);
  const auto dataspace = H5Screate(H5S_SCALAR);
  ASSERT_GE(dataspace, 0);
  const auto attribute =
    H5Acreate2(object, std::string(attribute_name).c_str(), type, dataspace, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(attribute, 0);
  const std::string value_copy(value);
  ASSERT_GE(H5Awrite(attribute, type, value_copy.c_str()), 0);
  EXPECT_GE(H5Aclose(attribute), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Tclose(type), 0);
  EXPECT_GE(H5Oclose(object), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void write_compound_object_attribute(const std::filesystem::path& path, const std::string_view object_path,
                                     const std::string_view attribute_name) {
  struct Pair {
    std::uint32_t first;
    std::uint32_t second;
  };

  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto object = H5Oopen(file, std::string(object_path).c_str(), H5P_DEFAULT);
  ASSERT_GE(object, 0);
  const auto type = H5Tcreate(H5T_COMPOUND, sizeof(Pair));
  ASSERT_GE(type, 0);
  ASSERT_GE(H5Tinsert(type, "first", offsetof(Pair, first), H5T_NATIVE_UINT32), 0);
  ASSERT_GE(H5Tinsert(type, "second", offsetof(Pair, second), H5T_NATIVE_UINT32), 0);
  const auto dataspace = H5Screate(H5S_SCALAR);
  ASSERT_GE(dataspace, 0);
  const auto attribute =
    H5Acreate2(object, std::string(attribute_name).c_str(), type, dataspace, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(attribute, 0);
  const Pair value{.first = 7U, .second = 9U};
  ASSERT_GE(H5Awrite(attribute, type, &value), 0);
  EXPECT_GE(H5Aclose(attribute), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Tclose(type), 0);
  EXPECT_GE(H5Oclose(object), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void move_root_group_below_root(const std::filesystem::path& path, const std::string_view root_group,
                                const std::string_view container, const std::string_view nested_group) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto container_id = H5Gcreate2(file, std::string(container).c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(container_id, 0);
  EXPECT_GE(H5Gclose(container_id), 0);
  const std::string source_path = "/" + std::string(root_group);
  const std::string destination_path = "/" + std::string(container) + "/" + std::string(nested_group);
  EXPECT_GE(H5Lmove(file, source_path.c_str(), file, destination_path.c_str(), H5P_DEFAULT, H5P_DEFAULT), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void write_all_image_data_types_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(kDatasetGroup).c_str(), true);
  dataset.writeHeader(std::string(kXmlHeader));
  append_typed_image<std::uint16_t>(dataset, "type_u16");
  append_typed_image<std::int16_t>(dataset, "type_i16");
  append_typed_image<std::uint32_t>(dataset, "type_u32");
  append_typed_image<std::int32_t>(dataset, "type_i32");
  append_typed_image<float>(dataset, "type_f32");
  append_typed_image<double>(dataset, "type_f64");
  append_typed_image<std::complex<float>>(dataset, "type_cf32");
  append_typed_image<std::complex<double>>(dataset, "type_cf64");
}

void replace_image_pixels_with_signed_int32(const std::filesystem::path& path) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  constexpr std::string_view data_path{"/dataset/image_inspection/data"};
  ASSERT_GE(H5Ldelete(file, std::string(data_path).c_str(), H5P_DEFAULT), 0);
  const std::array<hsize_t, 5U> dimensions{1U, 2U, 4U, 3U, 2U};
  const auto dataspace = H5Screate_simple(static_cast<int>(dimensions.size()), dimensions.data(), nullptr);
  ASSERT_GE(dataspace, 0);
  const auto dataset = H5Dcreate2(file, std::string(data_path).c_str(), H5T_NATIVE_INT32, dataspace, H5P_DEFAULT,
                                  H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(dataset, 0);
  const std::vector<std::int32_t> pixels(48U, 7);
  ASSERT_GE(H5Dwrite(dataset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, pixels.data()), 0);
  EXPECT_GE(H5Dclose(dataset), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void replace_xml_with_rank_two_variable_string_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  constexpr std::string_view xml_path{"/dataset/xml"};
  ASSERT_GE(H5Ldelete(file, std::string(xml_path).c_str(), H5P_DEFAULT), 0);
  const std::array<hsize_t, 2U> dimensions{1U, 1U};
  const auto dataspace = H5Screate_simple(static_cast<int>(dimensions.size()), dimensions.data(), nullptr);
  ASSERT_GE(dataspace, 0);
  const auto string_type = H5Tcopy(H5T_C_S1);
  ASSERT_GE(string_type, 0);
  ASSERT_GE(H5Tset_size(string_type, H5T_VARIABLE), 0);
  const auto dataset =
    H5Dcreate2(file, std::string(xml_path).c_str(), string_type, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(dataset, 0);
  EXPECT_GE(H5Dclose(dataset), 0);
  EXPECT_GE(H5Tclose(string_type), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void replace_xml_with_fixed_length_string_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  constexpr std::string_view xml_path{"/dataset/xml"};
  ASSERT_GE(H5Ldelete(file, std::string(xml_path).c_str(), H5P_DEFAULT), 0);
  const std::array<hsize_t, 1U> dimensions{1U};
  const auto dataspace = H5Screate_simple(static_cast<int>(dimensions.size()), dimensions.data(), nullptr);
  ASSERT_GE(dataspace, 0);
  const auto string_type = H5Tcopy(H5T_C_S1);
  ASSERT_GE(string_type, 0);
  ASSERT_GE(H5Tset_size(string_type, 128U), 0);
  const auto dataset =
    H5Dcreate2(file, std::string(xml_path).c_str(), string_type, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(dataset, 0);
  EXPECT_GE(H5Dclose(dataset), 0);
  EXPECT_GE(H5Tclose(string_type), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

[[nodiscard]] hid_t make_reordered_padded_compound_type(const hid_t source_type) {
  if (H5Tget_class(source_type) != H5T_COMPOUND) {
    return H5I_INVALID_HID;
  }
  const auto member_count = H5Tget_nmembers(source_type);
  if (member_count <= 0) {
    return H5I_INVALID_HID;
  }
  std::size_t storage_size = 0U;
  for (int member_index = 0; member_index < member_count; ++member_index) {
    const auto member_type = H5Tget_member_type(source_type, static_cast<unsigned>(member_index));
    if (member_type < 0) {
      return H5I_INVALID_HID;
    }
    const auto member_size = H5Tget_size(member_type);
    static_cast<void>(H5Tclose(member_type));
    if (member_size > 4096U - storage_size - 1U) {
      return H5I_INVALID_HID;
    }
    storage_size += member_size + 1U;
  }
  const auto reordered_type = H5Tcreate(H5T_COMPOUND, storage_size);
  if (reordered_type < 0) {
    return H5I_INVALID_HID;
  }
  std::size_t next_offset = 0U;
  for (int member_index = member_count - 1; member_index >= 0; --member_index) {
    char* member_name = H5Tget_member_name(source_type, static_cast<unsigned>(member_index));
    const auto member_type = H5Tget_member_type(source_type, static_cast<unsigned>(member_index));
    const auto member_size = member_type >= 0 ? H5Tget_size(member_type) : 0U;
    const auto inserted = member_name != nullptr && member_type >= 0 &&
                          H5Tinsert(reordered_type, member_name, next_offset, member_type) >= 0;
    if (member_type >= 0) {
      static_cast<void>(H5Tclose(member_type));
    }
    if (member_name != nullptr) {
      static_cast<void>(H5free_memory(member_name));
    }
    if (!inserted) {
      static_cast<void>(H5Tclose(reordered_type));
      return H5I_INVALID_HID;
    }
    next_offset += member_size + 1U;
  }
  return reordered_type;
}

void replace_image_header_with_reordered_padded_layout(const std::filesystem::path& path) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  constexpr std::string_view header_path{"/dataset/image_inspection/header"};
  const auto old_dataset = H5Dopen2(file, std::string(header_path).c_str(), H5P_DEFAULT);
  ASSERT_GE(old_dataset, 0);
  const auto source_type = H5Dget_type(old_dataset);
  ASSERT_GE(source_type, 0);
  const auto source_space = H5Dget_space(old_dataset);
  ASSERT_GE(source_space, 0);
  const auto creation_properties = H5Dget_create_plist(old_dataset);
  ASSERT_GE(creation_properties, 0);
  std::array<hsize_t, 1U> dimensions{};
  ASSERT_EQ(H5Sget_simple_extent_ndims(source_space), 1);
  ASSERT_GE(H5Sget_simple_extent_dims(source_space, dimensions.data(), nullptr), 0);
  ASSERT_EQ(dimensions.front(), 1U);
  ISMRMRD::ISMRMRD_ImageHeader header{};
  ASSERT_GE(H5Dread(old_dataset, source_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, &header), 0);
  const auto reordered_type = make_reordered_padded_compound_type(source_type);
  ASSERT_GE(reordered_type, 0);
  ASSERT_GE(H5Dclose(old_dataset), 0);
  ASSERT_GE(H5Ldelete(file, std::string(header_path).c_str(), H5P_DEFAULT), 0);
  const auto replacement = H5Dcreate2(file, std::string(header_path).c_str(), reordered_type, source_space, H5P_DEFAULT,
                                      creation_properties, H5P_DEFAULT);
  ASSERT_GE(replacement, 0);
  ASSERT_GE(H5Dwrite(replacement, source_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, &header), 0);
  EXPECT_GE(H5Dclose(replacement), 0);
  EXPECT_GE(H5Tclose(reordered_type), 0);
  EXPECT_GE(H5Pclose(creation_properties), 0);
  EXPECT_GE(H5Sclose(source_space), 0);
  EXPECT_GE(H5Tclose(source_type), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void replace_image_header_with_incomplete_compound(const std::filesystem::path& path) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  constexpr std::string_view header_path{"/dataset/image_inspection/header"};
  ASSERT_GE(H5Ldelete(file, std::string(header_path).c_str(), H5P_DEFAULT), 0);
  const std::array<hsize_t, 1U> dimensions{1U};
  const auto dataspace = H5Screate_simple(static_cast<int>(dimensions.size()), dimensions.data(), nullptr);
  ASSERT_GE(dataspace, 0);
  const auto header_type = H5Tcreate(H5T_COMPOUND, sizeof(std::uint16_t));
  ASSERT_GE(header_type, 0);
  ASSERT_GE(H5Tinsert(header_type, "version", 0U, H5T_NATIVE_UINT16), 0);
  const auto dataset =
    H5Dcreate2(file, std::string(header_path).c_str(), header_type, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(dataset, 0);
  const std::uint16_t version = 1U;
  ASSERT_GE(H5Dwrite(dataset, header_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, &version), 0);
  EXPECT_GE(H5Dclose(dataset), 0);
  EXPECT_GE(H5Tclose(header_type), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void replace_acquisition_data_with_int32_vlen(const std::filesystem::path& path) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  constexpr std::string_view data_path{"/dataset/data"};
  const auto old_dataset = H5Dopen2(file, std::string(data_path).c_str(), H5P_DEFAULT);
  ASSERT_GE(old_dataset, 0);
  const auto source_type = H5Dget_type(old_dataset);
  ASSERT_GE(source_type, 0);
  const auto source_space = H5Dget_space(old_dataset);
  ASSERT_GE(source_space, 0);
  const auto creation_properties = H5Dget_create_plist(old_dataset);
  ASSERT_GE(creation_properties, 0);
  std::array<hsize_t, 1U> dimensions{};
  ASSERT_EQ(H5Sget_simple_extent_ndims(source_space), 1);
  ASSERT_GE(H5Sget_simple_extent_dims(source_space, dimensions.data(), nullptr), 0);
  ASSERT_EQ(H5Tget_class(source_type), H5T_COMPOUND);
  const auto record_bytes = H5Tget_size(source_type);
  ASSERT_GT(record_bytes, 0U);
  const auto storage_words =
    (record_bytes * static_cast<std::size_t>(dimensions.front()) + sizeof(std::max_align_t) - 1U) /
    sizeof(std::max_align_t);
  std::vector<std::max_align_t> records(storage_words);
  ASSERT_GE(H5Dread(old_dataset, source_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, records.data()), 0);

  const auto head_index = H5Tget_member_index(source_type, "head");
  const auto trajectory_index = H5Tget_member_index(source_type, "traj");
  const auto data_index = H5Tget_member_index(source_type, "data");
  ASSERT_GE(head_index, 0);
  ASSERT_GE(trajectory_index, 0);
  ASSERT_GE(data_index, 0);
  const auto head_type = H5Tget_member_type(source_type, static_cast<unsigned>(head_index));
  const auto trajectory_type = H5Tget_member_type(source_type, static_cast<unsigned>(trajectory_index));
  const auto int32_vlen_type = H5Tvlen_create(H5T_NATIVE_INT32);
  ASSERT_GE(head_type, 0);
  ASSERT_GE(trajectory_type, 0);
  ASSERT_GE(int32_vlen_type, 0);
  const auto replacement_type = H5Tcreate(H5T_COMPOUND, record_bytes);
  ASSERT_GE(replacement_type, 0);
  ASSERT_GE(H5Tinsert(replacement_type, "head", H5Tget_member_offset(source_type, static_cast<unsigned>(head_index)),
                      head_type),
            0);
  ASSERT_GE(H5Tinsert(replacement_type, "traj",
                      H5Tget_member_offset(source_type, static_cast<unsigned>(trajectory_index)), trajectory_type),
            0);
  ASSERT_GE(H5Tinsert(replacement_type, "data", H5Tget_member_offset(source_type, static_cast<unsigned>(data_index)),
                      int32_vlen_type),
            0);
  ASSERT_GE(H5Tclose(head_type), 0);
  ASSERT_GE(H5Tclose(trajectory_type), 0);
  ASSERT_GE(H5Tclose(int32_vlen_type), 0);
  ASSERT_GE(H5Dclose(old_dataset), 0);
  ASSERT_GE(H5Ldelete(file, std::string(data_path).c_str(), H5P_DEFAULT), 0);
  const auto replacement = H5Dcreate2(file, std::string(data_path).c_str(), replacement_type, source_space, H5P_DEFAULT,
                                      creation_properties, H5P_DEFAULT);
  ASSERT_GE(replacement, 0);
  ASSERT_GE(H5Dwrite(replacement, source_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, records.data()), 0);
  EXPECT_GE(H5Dvlen_reclaim(source_type, source_space, H5P_DEFAULT, records.data()), 0);
  EXPECT_GE(H5Dclose(replacement), 0);
  EXPECT_GE(H5Tclose(replacement_type), 0);
  EXPECT_GE(H5Pclose(creation_properties), 0);
  EXPECT_GE(H5Sclose(source_space), 0);
  EXPECT_GE(H5Tclose(source_type), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void replace_acquisition_header_with_incomplete_compound(const std::filesystem::path& path) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  constexpr std::string_view data_path{"/dataset/data"};
  ASSERT_GE(H5Ldelete(file, std::string(data_path).c_str(), H5P_DEFAULT), 0);
  const std::array<hsize_t, 1U> dimensions{2U};
  const auto dataspace = H5Screate_simple(static_cast<int>(dimensions.size()), dimensions.data(), nullptr);
  ASSERT_GE(dataspace, 0);
  const auto record_type = H5Tcreate(H5T_COMPOUND, sizeof(std::uint16_t));
  ASSERT_GE(record_type, 0);
  ASSERT_GE(H5Tinsert(record_type, "head", 0U, H5T_NATIVE_UINT16), 0);
  const auto dataset =
    H5Dcreate2(file, std::string(data_path).c_str(), record_type, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(dataset, 0);
  const std::array<std::uint16_t, 2U> records{1U, 1U};
  ASSERT_GE(H5Dwrite(dataset, record_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, records.data()), 0);
  EXPECT_GE(H5Dclose(dataset), 0);
  EXPECT_GE(H5Tclose(record_type), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void replace_acquisition_header_with_reordered_padded_layout(const std::filesystem::path& path) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  constexpr std::string_view data_path{"/dataset/data"};
  const auto old_dataset = H5Dopen2(file, std::string(data_path).c_str(), H5P_DEFAULT);
  ASSERT_GE(old_dataset, 0);
  const auto source_type = H5Dget_type(old_dataset);
  ASSERT_GE(source_type, 0);
  const auto source_space = H5Dget_space(old_dataset);
  ASSERT_GE(source_space, 0);
  const auto creation_properties = H5Dget_create_plist(old_dataset);
  ASSERT_GE(creation_properties, 0);
  std::array<hsize_t, 1U> dimensions{};
  ASSERT_EQ(H5Sget_simple_extent_ndims(source_space), 1);
  ASSERT_GE(H5Sget_simple_extent_dims(source_space, dimensions.data(), nullptr), 0);
  ASSERT_EQ(H5Tget_class(source_type), H5T_COMPOUND);
  const auto record_bytes = H5Tget_size(source_type);
  ASSERT_GT(record_bytes, 0U);
  const auto storage_words =
    (record_bytes * static_cast<std::size_t>(dimensions.front()) + sizeof(std::max_align_t) - 1U) /
    sizeof(std::max_align_t);
  std::vector<std::max_align_t> records(storage_words);
  ASSERT_GE(H5Dread(old_dataset, source_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, records.data()), 0);

  const auto head_index = H5Tget_member_index(source_type, "head");
  const auto trajectory_index = H5Tget_member_index(source_type, "traj");
  const auto data_index = H5Tget_member_index(source_type, "data");
  ASSERT_GE(head_index, 0);
  ASSERT_GE(trajectory_index, 0);
  ASSERT_GE(data_index, 0);
  const auto source_head_type = H5Tget_member_type(source_type, static_cast<unsigned>(head_index));
  const auto trajectory_type = H5Tget_member_type(source_type, static_cast<unsigned>(trajectory_index));
  const auto data_type = H5Tget_member_type(source_type, static_cast<unsigned>(data_index));
  ASSERT_GE(source_head_type, 0);
  ASSERT_GE(trajectory_type, 0);
  ASSERT_GE(data_type, 0);
  const auto reordered_head_type = make_reordered_padded_compound_type(source_head_type);
  ASSERT_GE(reordered_head_type, 0);
  const auto head_size_delta = H5Tget_size(reordered_head_type) - H5Tget_size(source_head_type);
  ASSERT_GT(head_size_delta, 0U);
  const auto replacement_type = H5Tcreate(H5T_COMPOUND, record_bytes + head_size_delta);
  ASSERT_GE(replacement_type, 0);
  ASSERT_GE(H5Tinsert(replacement_type, "head", H5Tget_member_offset(source_type, static_cast<unsigned>(head_index)),
                      reordered_head_type),
            0);
  ASSERT_GE(H5Tinsert(replacement_type, "traj",
                      H5Tget_member_offset(source_type, static_cast<unsigned>(trajectory_index)) + head_size_delta,
                      trajectory_type),
            0);
  ASSERT_GE(H5Tinsert(replacement_type, "data",
                      H5Tget_member_offset(source_type, static_cast<unsigned>(data_index)) + head_size_delta,
                      data_type),
            0);
  ASSERT_GE(H5Tclose(source_head_type), 0);
  ASSERT_GE(H5Tclose(trajectory_type), 0);
  ASSERT_GE(H5Tclose(data_type), 0);
  ASSERT_GE(H5Tclose(reordered_head_type), 0);
  ASSERT_GE(H5Dclose(old_dataset), 0);
  ASSERT_GE(H5Ldelete(file, std::string(data_path).c_str(), H5P_DEFAULT), 0);
  const auto replacement = H5Dcreate2(file, std::string(data_path).c_str(), replacement_type, source_space, H5P_DEFAULT,
                                      creation_properties, H5P_DEFAULT);
  ASSERT_GE(replacement, 0);
  ASSERT_GE(H5Dwrite(replacement, source_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, records.data()), 0);
  EXPECT_GE(H5Dvlen_reclaim(source_type, source_space, H5P_DEFAULT, records.data()), 0);
  EXPECT_GE(H5Dclose(replacement), 0);
  EXPECT_GE(H5Tclose(replacement_type), 0);
  EXPECT_GE(H5Pclose(creation_properties), 0);
  EXPECT_GE(H5Sclose(source_space), 0);
  EXPECT_GE(H5Tclose(source_type), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

[[nodiscard]] const ksj::ismrmrd::MetaAttribute*
find_attribute(const std::vector<ksj::ismrmrd::MetaAttribute>& attributes, const std::string_view name) {
  const auto found = std::find_if(attributes.begin(), attributes.end(), [name](const auto& attribute) {
    return attribute.name == name;
  });
  return found == attributes.end() ? nullptr : &*found;
}

} // namespace

TEST(KSpaceJetInspectionReader, DiscoversBoundedStandardMrdContainers) {
  const auto path = make_test_dataset_path("discover_standard_mrd_containers");
  write_standard_dataset(path, "dataset_1", false);
  write_standard_dataset(path, "dataset_2", false);
  create_empty_root_group(path, kDatasetGroup);

  std::vector<ksj::ismrmrd::InspectionMrdContainerDescriptor> containers;
  std::string error;
  ASSERT_TRUE(ksj::ismrmrd::InspectionReader::discover_mrd_containers(path, {}, containers, error)) << error;
  ASSERT_EQ(containers.size(), 4U);
  EXPECT_EQ(containers[0].path, "/dataset_1");
  EXPECT_TRUE(containers[0].has_header);
  EXPECT_TRUE(containers[0].has_acquisitions);
  EXPECT_TRUE(containers[0].has_images);
  EXPECT_FALSE(containers[0].has_waveforms);
  EXPECT_EQ(containers[0].acquisition_count, 2U);
  EXPECT_EQ(containers[0].image_series_count, 1U);
  EXPECT_EQ(containers[1].path, "/dataset_1/image_inspection");
  EXPECT_FALSE(containers[1].has_header);
  EXPECT_TRUE(containers[1].has_images);
  EXPECT_EQ(containers[1].image_series_count, 1U);
  EXPECT_EQ(containers[2].path, "/dataset_2");
  EXPECT_EQ(containers[3].path, "/dataset_2/image_inspection");

  for (const auto& descriptor : containers) {
    if (!descriptor.has_header) {
      continue;
    }
    ksj::ismrmrd::InspectionReader reader;
    ASSERT_TRUE(reader.open(path, descriptor.path, {}, error)) << error;
    EXPECT_EQ(descriptor.acquisition_count, reader.metadata().acquisition_count);
    EXPECT_EQ(descriptor.image_series_count, reader.metadata().image_series.size());
  }

  auto limits = ksj::ismrmrd::InspectionReadLimits{};
  limits.max_hdf5_group_links = 2U;
  containers.push_back({.path = "/stale"});
  EXPECT_FALSE(ksj::ismrmrd::InspectionReader::discover_mrd_containers(path, limits, containers, error));
  EXPECT_TRUE(containers.empty());
  EXPECT_EQ(error, "No readable standard ISMRMRD containers were found.");
}

TEST(KSpaceJetInspectionReader, ReportsWhenNoReadableStandardMrdContainerExists) {
  const auto path = make_test_dataset_path("discover_no_standard_mrd_containers");
  create_empty_hdf5_file(path);
  create_empty_root_group(path, kDatasetGroup);

  std::vector<ksj::ismrmrd::InspectionMrdContainerDescriptor> containers{{.path = "/stale"}};
  std::string error;
  EXPECT_FALSE(ksj::ismrmrd::InspectionReader::discover_mrd_containers(path, {}, containers, error));
  EXPECT_TRUE(containers.empty());
  EXPECT_EQ(error, "No readable standard ISMRMRD containers were found.");
}

TEST(KSpaceJetInspectionReader, ClassifiesOnlyStandardWaveformDatasets) {
  const auto path = make_test_dataset_path("discover_standard_waveforms");
  write_standard_dataset(path, "standard_waveforms", false);
  append_standard_waveform(path, "standard_waveforms");
  write_standard_dataset(path, "nonstandard_waveforms", false);
  add_nonstandard_waveforms_dataset(path, "nonstandard_waveforms");

  std::vector<ksj::ismrmrd::InspectionMrdContainerDescriptor> containers;
  std::string error;
  ASSERT_TRUE(ksj::ismrmrd::InspectionReader::discover_mrd_containers(path, {}, containers, error)) << error;

  const auto standard = std::find_if(containers.begin(), containers.end(), [](const auto& descriptor) {
    return descriptor.path == "/standard_waveforms";
  });
  ASSERT_NE(standard, containers.end());
  EXPECT_TRUE(standard->has_header);
  EXPECT_TRUE(standard->has_waveforms);

  const auto nonstandard = std::find_if(containers.begin(), containers.end(), [](const auto& descriptor) {
    return descriptor.path == "/nonstandard_waveforms";
  });
  ASSERT_NE(nonstandard, containers.end());
  EXPECT_TRUE(nonstandard->has_header);
  EXPECT_FALSE(nonstandard->has_waveforms);

  ksj::ismrmrd::InspectionReader reader;
  ASSERT_TRUE(reader.open(path, "/standard_waveforms", {}, error)) << error;
  std::vector<ksj::ismrmrd::InspectionObjectAttributeDescriptor> attributes;
  EXPECT_TRUE(reader.read_object_attributes({.kind = ksj::ismrmrd::InspectionObjectKind::waveforms}, attributes, error))
    << error;
  EXPECT_TRUE(attributes.empty());

  ASSERT_TRUE(reader.open(path, "/nonstandard_waveforms", {}, error)) << error;
  EXPECT_FALSE(
    reader.read_object_attributes({.kind = ksj::ismrmrd::InspectionObjectKind::waveforms}, attributes, error));
  EXPECT_EQ(error, "The selected ISMRMRD container has no standard waveform object.");
  EXPECT_TRUE(attributes.empty());
}

TEST(KSpaceJetInspectionReader, OpensStandardImageArtifactWithoutRawAcquisitions) {
  const auto path = make_test_dataset_path("standard_image_artifact");
  write_standard_image_artifact(path, "results");

  std::vector<ksj::ismrmrd::InspectionMrdContainerDescriptor> containers;
  std::string error;
  ASSERT_TRUE(ksj::ismrmrd::InspectionReader::discover_mrd_containers(path, {}, containers, error)) << error;
  ASSERT_EQ(containers.size(), 2U);
  EXPECT_EQ(containers[0].path, "/results");
  EXPECT_TRUE(containers[0].has_header);
  EXPECT_FALSE(containers[0].has_acquisitions);
  EXPECT_TRUE(containers[0].has_images);
  EXPECT_EQ(containers[0].acquisition_count, 0U);
  EXPECT_EQ(containers[0].image_series_count, 1U);
  EXPECT_EQ(containers[1].path, "/results/image_inspection");
  EXPECT_FALSE(containers[1].has_header);
  EXPECT_TRUE(containers[1].has_images);

  ksj::ismrmrd::InspectionReader reader;
  ASSERT_TRUE(reader.open(path, "/results", {}, error)) << error;
  EXPECT_EQ(reader.metadata().group, "/results");
  EXPECT_EQ(reader.metadata().acquisition_count, 0U);
  ASSERT_EQ(reader.metadata().image_series.size(), 1U);
  EXPECT_EQ(reader.metadata().image_series.front().series_id, kImageSeries);
}

TEST(KSpaceJetInspectionReader, DiscoversNestedRawAndStandaloneImageContainers) {
  const auto path = make_test_dataset_path("discover_nested_mrd_containers");
  write_standard_dataset(path, "dataset_1", false);
  write_standard_dataset(path, "dataset_2", false);
  move_root_group_below_root(path, "dataset_2", "container", "nested");

  std::vector<ksj::ismrmrd::InspectionMrdContainerDescriptor> containers;
  std::string error;
  ASSERT_TRUE(ksj::ismrmrd::InspectionReader::discover_mrd_containers(path, {}, containers, error)) << error;
  ASSERT_EQ(containers.size(), 4U);
  EXPECT_EQ(containers[0].path, "/container/nested");
  EXPECT_TRUE(containers[0].has_header);
  EXPECT_TRUE(containers[0].has_acquisitions);
  EXPECT_EQ(containers[1].path, "/container/nested/image_inspection");
  EXPECT_FALSE(containers[1].has_header);
  EXPECT_TRUE(containers[1].has_images);
  EXPECT_EQ(containers[2].path, "/dataset_1");
  EXPECT_EQ(containers[3].path, "/dataset_1/image_inspection");

  auto depth_limited = ksj::ismrmrd::InspectionReadLimits{};
  depth_limited.max_hdf5_group_depth = 1U;
  ASSERT_TRUE(ksj::ismrmrd::InspectionReader::discover_mrd_containers(path, depth_limited, containers, error)) << error;
  ASSERT_EQ(containers.size(), 1U);
  EXPECT_EQ(containers.front().path, "/dataset_1");

  ksj::ismrmrd::InspectionReader nested_reader;
  EXPECT_TRUE(nested_reader.open(path, "/container/nested", {}, error)) << error;

  ksj::ismrmrd::InspectionReader standalone_image_reader;
  ASSERT_TRUE(standalone_image_reader.open(path, "/dataset_1/image_inspection", {}, error)) << error;
  EXPECT_EQ(standalone_image_reader.metadata().group, "/dataset_1/image_inspection");
  EXPECT_TRUE(standalone_image_reader.metadata().xml_header.empty());
  EXPECT_EQ(standalone_image_reader.metadata().acquisition_count, 0U);
  ASSERT_EQ(standalone_image_reader.metadata().image_series.size(), 1U);
  EXPECT_EQ(standalone_image_reader.metadata().image_series.front().series_id, "image_inspection");
  ksj::ismrmrd::InspectionImageRecord image_record;
  EXPECT_TRUE(
    standalone_image_reader.read_image_record({.series_id = "image_inspection", .ordinal = 0U}, image_record, error))
    << error;
  EXPECT_EQ(image_record.header.image_index, 7U);

  std::vector<ksj::ismrmrd::InspectionObjectAttributeDescriptor> attributes;
  EXPECT_FALSE(standalone_image_reader.read_object_attributes(
    {.kind = ksj::ismrmrd::InspectionObjectKind::acquisitions}, attributes, error));
  EXPECT_EQ(error, "A standalone ISMRMRD image series has no acquisition object.");
  EXPECT_TRUE(attributes.empty());
}

TEST(KSpaceJetInspectionReader, ReadsBoundedNativeHdf5ObjectAttributesWithoutConflatingImageMetaAttributes) {
  const auto path = make_test_dataset_path("native_hdf5_object_attributes");
  write_standard_dataset(path);
  write_uint32_object_attribute(path, "/dataset/data", "acquisition_count_hint", 42U);
  const std::array<std::uint32_t, 3U> coil_order{2U, 4U, 8U};
  write_uint32_array_object_attribute(path, "/dataset/data", "coil_order", coil_order);
  write_fixed_string_object_attribute(path, "/dataset/xml", "description", "synthetic header");
  write_compound_object_attribute(path, "/dataset/data", "compound_details");

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;

  std::vector<ksj::ismrmrd::InspectionObjectAttributeDescriptor> attributes;
  ASSERT_TRUE(reader.read_object_attributes({.kind = ksj::ismrmrd::InspectionObjectKind::container}, attributes, error))
    << error;
  EXPECT_TRUE(attributes.empty());

  ASSERT_TRUE(reader.read_object_attributes({.kind = ksj::ismrmrd::InspectionObjectKind::xml}, attributes, error))
    << error;
  ASSERT_EQ(attributes.size(), 1U);
  EXPECT_EQ(attributes.front().name, "description");
  EXPECT_TRUE(attributes.front().type_name.starts_with("string"));
  EXPECT_TRUE(attributes.front().dimensions.empty());
  EXPECT_EQ(attributes.front().element_count, 1U);
  EXPECT_EQ(attributes.front().value_preview, "synthetic header");
  EXPECT_EQ(attributes.front().value_preview_state, ksj::ismrmrd::InspectionAttributeValuePreviewState::available);

  ASSERT_TRUE(
    reader.read_object_attributes({.kind = ksj::ismrmrd::InspectionObjectKind::acquisitions}, attributes, error))
    << error;
  ASSERT_EQ(attributes.size(), 3U);
  const auto find_attribute = [&attributes](const std::string_view name) {
    return std::find_if(attributes.begin(), attributes.end(), [name](const auto& attribute) {
      return attribute.name == name;
    });
  };
  const auto acquisition_hint = find_attribute("acquisition_count_hint");
  ASSERT_NE(acquisition_hint, attributes.end());
  EXPECT_EQ(acquisition_hint->type_name, "32-bit unsigned integer");
  EXPECT_EQ(acquisition_hint->element_count, 1U);
  EXPECT_EQ(acquisition_hint->value_preview, "42");
  EXPECT_EQ(acquisition_hint->value_preview_state, ksj::ismrmrd::InspectionAttributeValuePreviewState::available);
  const auto coil_order_attribute = find_attribute("coil_order");
  ASSERT_NE(coil_order_attribute, attributes.end());
  EXPECT_EQ(coil_order_attribute->dimensions, std::vector<std::uint64_t>({3U}));
  EXPECT_EQ(coil_order_attribute->element_count, 3U);
  EXPECT_EQ(coil_order_attribute->value_preview, "[2, 4, 8]");
  EXPECT_EQ(coil_order_attribute->value_preview_state, ksj::ismrmrd::InspectionAttributeValuePreviewState::available);
  const auto compound_attribute = find_attribute("compound_details");
  ASSERT_NE(compound_attribute, attributes.end());
  EXPECT_EQ(compound_attribute->type_name, "compound");
  EXPECT_EQ(compound_attribute->value_preview_state, ksj::ismrmrd::InspectionAttributeValuePreviewState::unsupported);
  EXPECT_TRUE(compound_attribute->value_preview.empty());

  ASSERT_TRUE(reader.read_object_attributes(
    {.kind = ksj::ismrmrd::InspectionObjectKind::image_series, .image_series_id = std::string(kImageSeries)},
    attributes, error))
    << error;
  EXPECT_TRUE(attributes.empty());

  EXPECT_FALSE(reader.read_object_attributes(
    {.kind = ksj::ismrmrd::InspectionObjectKind::image_series, .image_series_id = "unknown"}, attributes, error));
  EXPECT_EQ(error, "ISMRMRD image series was not found.");
  EXPECT_TRUE(attributes.empty());

  auto preview_limits = ksj::ismrmrd::InspectionReadLimits{};
  preview_limits.max_hdf5_attribute_preview_bytes = 4U;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), preview_limits, error)) << error;
  ASSERT_TRUE(reader.read_object_attributes({.kind = ksj::ismrmrd::InspectionObjectKind::xml}, attributes, error))
    << error;
  ASSERT_EQ(attributes.size(), 1U);
  EXPECT_EQ(attributes.front().value_preview, "synt");
  EXPECT_EQ(attributes.front().value_preview_state, ksj::ismrmrd::InspectionAttributeValuePreviewState::truncated);
}

TEST(KSpaceJetInspectionReader, BoundsNativeHdf5ObjectAttributeEnumerationAndPreviews) {
  const auto path = make_test_dataset_path("native_hdf5_object_attribute_limits");
  write_standard_dataset(path);
  write_uint32_object_attribute(path, "/dataset/data", "acquisition_count_hint", 42U);
  write_uint32_object_attribute(path, "/dataset/data", "second_attribute", 43U);

  auto limits = ksj::ismrmrd::InspectionReadLimits{};
  limits.max_hdf5_object_attributes = 1U;
  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), limits, error)) << error;
  std::vector<ksj::ismrmrd::InspectionObjectAttributeDescriptor> attributes;
  EXPECT_FALSE(
    reader.read_object_attributes({.kind = ksj::ismrmrd::InspectionObjectKind::acquisitions}, attributes, error));
  EXPECT_EQ(error, "HDF5 object attribute count exceeds inspection limit.");
  EXPECT_TRUE(attributes.empty());

  limits.max_hdf5_object_attributes = 2U;
  limits.max_hdf5_attribute_value_bytes = 3U;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), limits, error)) << error;
  ASSERT_TRUE(
    reader.read_object_attributes({.kind = ksj::ismrmrd::InspectionObjectKind::acquisitions}, attributes, error))
    << error;
  ASSERT_EQ(attributes.size(), 2U);
  for (const auto& attribute : attributes) {
    EXPECT_EQ(attribute.value_preview, "preview omitted: inspection limit");
    EXPECT_EQ(attribute.value_preview_state, ksj::ismrmrd::InspectionAttributeValuePreviewState::truncated);
  }

  const auto element_path = make_test_dataset_path("native_hdf5_object_attribute_element_limit");
  write_standard_dataset(element_path);
  const std::array<std::uint32_t, 3U> values{3U, 5U, 7U};
  write_uint32_array_object_attribute(element_path, "/dataset/data", "three_values", values);
  limits = {};
  limits.max_hdf5_attribute_elements = 2U;
  ASSERT_TRUE(reader.open(element_path, std::string(kDatasetGroup), limits, error)) << error;
  ASSERT_TRUE(
    reader.read_object_attributes({.kind = ksj::ismrmrd::InspectionObjectKind::acquisitions}, attributes, error))
    << error;
  ASSERT_EQ(attributes.size(), 1U);
  EXPECT_EQ(attributes.front().dimensions, std::vector<std::uint64_t>({3U}));
  EXPECT_EQ(attributes.front().element_count, 3U);
  EXPECT_EQ(attributes.front().value_preview, "preview omitted: inspection limit");
  EXPECT_EQ(attributes.front().value_preview_state, ksj::ismrmrd::InspectionAttributeValuePreviewState::truncated);
}

TEST(KSpaceJetInspectionReader, ReadsStandardAcquisitionsImagesAxesAndMetaAttributes) {
  const auto path = make_test_dataset_path("standard");
  write_standard_dataset(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  ASSERT_TRUE(reader.is_open());
  EXPECT_EQ(reader.metadata().group, kDatasetGroup);
  EXPECT_EQ(reader.metadata().xml_header, kXmlHeader);
  EXPECT_EQ(reader.metadata().acquisition_count, 2U);
  ASSERT_EQ(reader.metadata().image_series.size(), 1U);
  EXPECT_EQ(reader.metadata().image_series.front().series_id, kImageSeries);
  EXPECT_EQ(reader.metadata().image_series.front().image_count, 1U);

  std::vector<ksj::ismrmrd::InspectionAcquisitionView> views;
  std::vector<std::complex<float>> samples;
  std::vector<float> trajectory;
  EXPECT_EQ(reader.for_each_acquisition(
              [&](const ksj::ismrmrd::InspectionAcquisitionView& acquisition) {
                views.push_back({.ordinal = acquisition.ordinal, .header = acquisition.header});
                if (acquisition.ordinal == 0U) {
                  samples.assign(acquisition.samples.begin(), acquisition.samples.end());
                  trajectory.assign(acquisition.trajectory.begin(), acquisition.trajectory.end());
                }
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::completed)
    << error;
  ASSERT_EQ(views.size(), 2U);
  EXPECT_EQ(views[0].ordinal, 0U);
  EXPECT_EQ(views[0].header.scan_counter, 23U);
  EXPECT_EQ(views[1].ordinal, 1U);
  EXPECT_EQ(views[1].header.scan_counter, 24U);
  EXPECT_EQ(views[0].header.number_of_samples, 3U);
  EXPECT_EQ(views[0].header.active_channels, 2U);
  EXPECT_EQ(views[0].header.trajectory_dimensions, 2U);
  ASSERT_EQ(samples.size(), 6U);
  EXPECT_EQ(samples[0], std::complex<float>(230.0F, 330.0F));
  EXPECT_EQ(samples[5], std::complex<float>(235.0F, 335.0F));
  ASSERT_EQ(trajectory.size(), 6U);
  EXPECT_FLOAT_EQ(trajectory[0], 0.0F);
  EXPECT_FLOAT_EQ(trajectory[1], 10.0F);
  EXPECT_FLOAT_EQ(trajectory[5], 12.0F);

  const ksj::ismrmrd::ImageLocator locator{.series_id = std::string(kImageSeries), .ordinal = 0U};
  ksj::ismrmrd::InspectionImageRecord record;
  ASSERT_TRUE(reader.read_image_record(locator, record, error)) << error;
  EXPECT_EQ(record.locator.series_id, kImageSeries);
  EXPECT_EQ(record.locator.ordinal, 0U);
  EXPECT_EQ(record.header.data_type, ksj::ismrmrd::ImageDataType::real_32);
  EXPECT_EQ(record.header.matrix_size, (std::array<std::uint16_t, 3U>{2U, 3U, 4U}));
  EXPECT_EQ(record.header.channels, 2U);
  EXPECT_EQ(record.header.field_of_view_mm, (std::array<float, 3U>{180.0F, 190.0F, 200.0F}));
  EXPECT_EQ(record.header.image_index, 7U);
  EXPECT_EQ(record.header.image_series_index, 11U);
  EXPECT_EQ(record.header.user_int[0], 13);
  EXPECT_FLOAT_EQ(record.header.user_float[0], 17.5F);
  const auto* window_center = find_attribute(record.meta_attributes, "WindowCenter");
  ASSERT_NE(window_center, nullptr);
  EXPECT_EQ(window_center->values, (std::vector<std::string>{"10", "20"}));

  std::vector<float> pixels;
  EXPECT_EQ(
    reader.with_image_pixels(
      locator,
      [&](const ksj::ismrmrd::InspectionImageRecord& callback_record, const ksj::ismrmrd::ImagePixelsView& image) {
        EXPECT_EQ(callback_record.header.image_index, 7U);
        EXPECT_EQ(image.data_type, ksj::ismrmrd::ImageDataType::real_32);
        EXPECT_EQ(image.dimensions, (std::array<std::uint16_t, 4U>{2U, 3U, 4U, 2U}));
        constexpr auto expected_bytes = 2U * 3U * 4U * 2U * sizeof(float);
        EXPECT_EQ(image.pixels.size(), expected_bytes);
        if (image.pixels.size() != expected_bytes) {
          return false;
        }
        pixels.resize(image.pixels.size() / sizeof(float));
        std::memcpy(pixels.data(), image.pixels.data(), image.pixels.size());
        return true;
      },
      error),
    ksj::ismrmrd::InspectionIterationResult::completed)
    << error;
  ASSERT_EQ(pixels.size(), 48U);
  for (std::uint16_t channel = 0U; channel < 2U; ++channel) {
    for (std::uint16_t z = 0U; z < 4U; ++z) {
      for (std::uint16_t y = 0U; y < 3U; ++y) {
        for (std::uint16_t x = 0U; x < 2U; ++x) {
          const auto offset = static_cast<std::size_t>(x) +
                              2U * (static_cast<std::size_t>(y) +
                                    3U * (static_cast<std::size_t>(z) + 4U * static_cast<std::size_t>(channel)));
          EXPECT_FLOAT_EQ(pixels[offset], expected_pixel(x, y, z, channel));
        }
      }
    }
  }

  EXPECT_EQ(reader.with_image_pixels(
              locator,
              [](const auto&, const auto&) {
                return false;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::stopped)
    << error;

  bool nested_open_succeeded = true;
  std::string nested_open_error;
  EXPECT_EQ(reader.visit_acquisition(
              0U,
              [&](const auto&) {
                nested_open_succeeded = reader.open(path, std::string(kDatasetGroup), {}, nested_open_error);
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::completed)
    << error;
  EXPECT_FALSE(nested_open_succeeded);
  EXPECT_EQ(nested_open_error, "ISMRMRD inspection reader does not support nested reads.");
  EXPECT_TRUE(reader.is_open());
}

TEST(KSpaceJetInspectionReader, ReadsAcquisitionHeadersWithoutPayloadMaterialization) {
  const auto path = make_test_dataset_path("acquisition_header_only");
  write_standard_dataset(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;

  ksj::ismrmrd::AcquisitionHeader header;
  ASSERT_TRUE(reader.read_acquisition_header(0U, header, error)) << error;
  EXPECT_EQ(header.measurement_uid, 17U);
  EXPECT_EQ(header.scan_counter, 23U);
  EXPECT_EQ(header.acquisition_time_stamp, 52U);
  EXPECT_EQ(header.number_of_samples, 3U);
  EXPECT_EQ(header.active_channels, 2U);
  EXPECT_EQ(header.trajectory_dimensions, 2U);
  EXPECT_EQ(header.discard_pre, 1U);
  EXPECT_EQ(header.center_sample, 2U);
  EXPECT_EQ(header.index.slice, 3U);
  EXPECT_EQ(header.index.repetition, 5U);

  bool nested_succeeded = true;
  std::string nested_error;
  header.scan_counter = 99U;
  EXPECT_EQ(reader.visit_acquisition(
              0U,
              [&](const ksj::ismrmrd::InspectionAcquisitionView&) {
                nested_succeeded = reader.read_acquisition_header(0U, header, nested_error);
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::completed)
    << error;
  EXPECT_FALSE(nested_succeeded);
  EXPECT_EQ(nested_error, "ISMRMRD inspection reader does not support nested reads.");
  EXPECT_EQ(header.scan_counter, 0U);
}

TEST(KSpaceJetInspectionReader, IteratesAllAcquisitionHeadersAsOwnedRecords) {
  const auto path = make_test_dataset_path("acquisition_header_iteration");
  write_standard_dataset(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;

  std::vector<ksj::ismrmrd::InspectionAcquisitionHeaderRecord> records;
  EXPECT_EQ(reader.for_each_acquisition_header(
              [&records](const ksj::ismrmrd::InspectionAcquisitionHeaderRecord& record) {
                records.push_back(record);
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::completed)
    << error;
  ASSERT_EQ(records.size(), 2U);
  EXPECT_EQ(records[0].ordinal, 0U);
  EXPECT_EQ(records[0].header.scan_counter, 23U);
  EXPECT_EQ(records[0].header.number_of_samples, 3U);
  EXPECT_EQ(records[0].header.active_channels, 2U);
  EXPECT_EQ(records[1].ordinal, 1U);
  EXPECT_EQ(records[1].header.scan_counter, 24U);

  std::uint32_t visited = 0U;
  EXPECT_EQ(reader.for_each_acquisition_header(
              [&visited](const ksj::ismrmrd::InspectionAcquisitionHeaderRecord&) {
                ++visited;
                return false;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::stopped)
    << error;
  EXPECT_EQ(visited, 1U);
  EXPECT_TRUE(error.empty());
}

TEST(KSpaceJetInspectionReader, IteratesAcquisitionHeadersWithoutPayloadAndRejectsClosedOrNestedReads) {
  ksj::ismrmrd::InspectionReader closed_reader;
  std::string error;
  EXPECT_EQ(closed_reader.for_each_acquisition_header(
              [](const ksj::ismrmrd::InspectionAcquisitionHeaderRecord&) {
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::failed);
  EXPECT_EQ(error, "ISMRMRD inspection reader is not open.");

  const auto path = make_test_dataset_path("acquisition_header_iteration_malformed_payload");
  write_standard_dataset(path);
  replace_acquisition_data_with_int32_vlen(path);

  ksj::ismrmrd::InspectionReader reader;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  std::vector<std::uint32_t> scan_counters;
  EXPECT_EQ(reader.for_each_acquisition_header(
              [&scan_counters](const ksj::ismrmrd::InspectionAcquisitionHeaderRecord& record) {
                scan_counters.push_back(record.header.scan_counter);
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::completed)
    << error;
  EXPECT_EQ(scan_counters, (std::vector<std::uint32_t>{23U, 24U}));

  auto nested_result = ksj::ismrmrd::InspectionIterationResult::completed;
  std::string nested_error;
  EXPECT_EQ(reader.for_each_acquisition_header(
              [&reader, &nested_result, &nested_error](const ksj::ismrmrd::InspectionAcquisitionHeaderRecord&) {
                nested_result = reader.for_each_acquisition_header(
                  [](const ksj::ismrmrd::InspectionAcquisitionHeaderRecord&) {
                    return true;
                  },
                  nested_error);
                return false;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::stopped)
    << error;
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(nested_result, ksj::ismrmrd::InspectionIterationResult::failed);
  EXPECT_EQ(nested_error, "ISMRMRD inspection reader does not support nested reads.");
}

TEST(KSpaceJetInspectionReader, RejectsInvalidAndMalformedAcquisitionHeadersBeforeExposure) {
  const auto path = make_test_dataset_path("invalid_acquisition_header");
  write_standard_dataset(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  ksj::ismrmrd::AcquisitionHeader header;
  header.scan_counter = 99U;
  EXPECT_FALSE(reader.read_acquisition_header(2U, header, error));
  EXPECT_EQ(error, "ISMRMRD acquisition ordinal is outside the dataset.");
  EXPECT_EQ(header.scan_counter, 0U);

  const auto malformed_path = make_test_dataset_path("malformed_acquisition_header");
  write_standard_dataset(malformed_path);
  replace_acquisition_header_with_incomplete_compound(malformed_path);
  ASSERT_TRUE(reader.open(malformed_path, std::string(kDatasetGroup), {}, error)) << error;
  header.scan_counter = 99U;
  EXPECT_FALSE(reader.read_acquisition_header(0U, header, error));
  EXPECT_EQ(error, "ISMRMRD acquisition header is malformed.");
  EXPECT_EQ(header.scan_counter, 0U);
}

TEST(KSpaceJetInspectionReader, ReadsAcquisitionHeaderWhenVlenPayloadIsMalformed) {
  const auto path = make_test_dataset_path("acquisition_header_with_malformed_payload");
  write_standard_dataset(path);
  replace_acquisition_data_with_int32_vlen(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  ksj::ismrmrd::AcquisitionHeader header;
  ASSERT_TRUE(reader.read_acquisition_header(0U, header, error)) << error;
  EXPECT_EQ(header.scan_counter, 23U);
  EXPECT_EQ(reader.visit_acquisition(
              0U,
              [](const ksj::ismrmrd::InspectionAcquisitionView&) {
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::failed);
  EXPECT_EQ(error, "ISMRMRD acquisition payload is malformed.");
}

TEST(KSpaceJetInspectionReader, EnforcesHeaderAcquisitionImageAndMetadataLimitsBeforePayloadExposure) {
  const auto path = make_test_dataset_path("limits");
  write_standard_dataset(path);

  std::string error;
  ksj::ismrmrd::InspectionReader reader;
  auto limits = ksj::ismrmrd::InspectionReadLimits{};
  limits.max_xml_header_bytes = 1U;
  EXPECT_FALSE(reader.open(path, std::string(kDatasetGroup), limits, error));
  EXPECT_EQ(error, "ISMRMRD XML header exceeds inspection limit.");

  limits = {};
  limits.max_acquisition_count = 1U;
  EXPECT_FALSE(reader.open(path, std::string(kDatasetGroup), limits, error));
  EXPECT_EQ(error, "ISMRMRD acquisition count exceeds inspection limit.");

  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  const ksj::ismrmrd::ImageLocator locator{.series_id = std::string(kImageSeries), .ordinal = 0U};

  limits = {};
  limits.max_acquisition_payload_bytes = 8U;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), limits, error)) << error;
  EXPECT_EQ(reader.visit_acquisition(
              0U,
              [](const auto&) {
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::failed);
  EXPECT_EQ(error, "ISMRMRD acquisition payload exceeds inspection limit.");

  limits = {};
  limits.max_image_payload_bytes = 8U;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), limits, error)) << error;
  EXPECT_EQ(reader.with_image_pixels(
              locator,
              [](const auto&, const auto&) {
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::failed);
  EXPECT_EQ(error, "ISMRMRD image payload exceeds inspection limit.");

  limits = {};
  limits.max_image_attribute_bytes = 1U;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), limits, error)) << error;
  ksj::ismrmrd::InspectionImageRecord record;
  EXPECT_FALSE(reader.read_image_record(locator, record, error));
  EXPECT_EQ(error, "ISMRMRD image attributes exceed inspection limit.");

  limits = {};
  limits.max_meta_attribute_entries = 1U;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), limits, error)) << error;
  EXPECT_FALSE(reader.read_image_record(locator, record, error));
  EXPECT_EQ(error, "ISMRMRD image MetaAttributes exceed inspection limits.");
}

TEST(KSpaceJetInspectionReader, RejectsMalformedMetaAttributesAndUnknownImageLocatorsDeterministically) {
  const auto path = make_test_dataset_path("malformed_attributes");
  write_standard_dataset(path, true);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;

  ksj::ismrmrd::InspectionImageRecord record;
  EXPECT_FALSE(reader.read_image_record({.series_id = std::string(kImageSeries), .ordinal = 0U}, record, error));
  EXPECT_EQ(error, "ISMRMRD image MetaAttributes are malformed.");

  const auto valid_path = make_test_dataset_path("locator_bounds");
  write_standard_dataset(valid_path);
  ASSERT_TRUE(reader.open(valid_path, std::string(kDatasetGroup), {}, error)) << error;
  EXPECT_FALSE(reader.read_image_record({.series_id = "missing", .ordinal = 0U}, record, error));
  EXPECT_EQ(error, "ISMRMRD image series was not found.");
  EXPECT_FALSE(reader.read_image_record({.series_id = std::string(kImageSeries), .ordinal = 1U}, record, error));
  EXPECT_EQ(error, "ISMRMRD image ordinal is outside its series.");
}

TEST(KSpaceJetInspectionReader, PreservesAllStandardImageDataTypesWithoutAssumingFloatPixels) {
  const auto path = make_test_dataset_path("all_image_data_types");
  write_all_image_data_types_dataset(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  ASSERT_EQ(reader.metadata().image_series.size(), 8U);
  const auto expect_type = [&]<typename Pixel>(const std::string_view series_id,
                                               const ksj::ismrmrd::ImageDataType data_type) {
    const ksj::ismrmrd::ImageLocator locator{.series_id = std::string(series_id), .ordinal = 0U};
    ksj::ismrmrd::InspectionImageRecord record;
    ASSERT_TRUE(reader.read_image_record(locator, record, error)) << error;
    EXPECT_EQ(record.header.data_type, data_type);
    const auto expected_pixels = typed_image_values<Pixel>();
    EXPECT_EQ(
      reader.with_image_pixels(
        locator,
        [&](const ksj::ismrmrd::InspectionImageRecord& callback_record, const ksj::ismrmrd::ImagePixelsView& pixels) {
          EXPECT_EQ(callback_record.header.data_type, data_type);
          EXPECT_EQ(pixels.data_type, data_type);
          EXPECT_EQ(pixels.dimensions, (std::array<std::uint16_t, 4U>{2U, 1U, 1U, 1U}));
          EXPECT_EQ(pixels.pixels.size(), sizeof(expected_pixels));
          if (pixels.pixels.size() == sizeof(expected_pixels)) {
            EXPECT_EQ(std::memcmp(pixels.pixels.data(), expected_pixels.data(), sizeof(expected_pixels)), 0);
          }
          return true;
        },
        error),
      ksj::ismrmrd::InspectionIterationResult::completed)
      << error;
  };
  expect_type.template operator()<std::uint16_t>("type_u16", ksj::ismrmrd::ImageDataType::unsigned_integer_16);
  expect_type.template operator()<std::int16_t>("type_i16", ksj::ismrmrd::ImageDataType::signed_integer_16);
  expect_type.template operator()<std::uint32_t>("type_u32", ksj::ismrmrd::ImageDataType::unsigned_integer_32);
  expect_type.template operator()<std::int32_t>("type_i32", ksj::ismrmrd::ImageDataType::signed_integer_32);
  expect_type.template operator()<float>("type_f32", ksj::ismrmrd::ImageDataType::real_32);
  expect_type.template operator()<double>("type_f64", ksj::ismrmrd::ImageDataType::real_64);
  expect_type.template operator()<std::complex<float>>("type_cf32", ksj::ismrmrd::ImageDataType::complex_32);
  expect_type.template operator()<std::complex<double>>("type_cf64", ksj::ismrmrd::ImageDataType::complex_64);
}

TEST(KSpaceJetInspectionReader, RejectsImagePixelTypeThatDoesNotMatchItsStandardHeader) {
  const auto path = make_test_dataset_path("image_pixel_type_mismatch");
  write_standard_dataset(path);
  replace_image_pixels_with_signed_int32(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  const ksj::ismrmrd::ImageLocator locator{.series_id = std::string(kImageSeries), .ordinal = 0U};
  EXPECT_EQ(reader.with_image_pixels(
              locator,
              [](const auto&, const auto&) {
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::failed);
  EXPECT_EQ(error, "ISMRMRD image pixel dataset is malformed.");
}

TEST(KSpaceJetInspectionReader, RejectsNonStandardRankedRecordDatasetsBeforeSingleRecordReads) {
  const auto path = make_test_dataset_path("rank_two_xml");
  write_standard_dataset(path);
  replace_xml_with_rank_two_variable_string_dataset(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  EXPECT_FALSE(reader.open(path, std::string(kDatasetGroup), {}, error));
  EXPECT_EQ(error, "ISMRMRD HDF5 variable-length string is malformed.");
}

TEST(KSpaceJetInspectionReader, RejectsFixedLengthStringsBeforeVariableStringMaterialization) {
  const auto path = make_test_dataset_path("fixed_length_xml");
  write_standard_dataset(path);
  replace_xml_with_fixed_length_string_dataset(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  EXPECT_FALSE(reader.open(path, std::string(kDatasetGroup), {}, error));
  EXPECT_EQ(error, "ISMRMRD HDF5 variable-length string is malformed.");
}

TEST(KSpaceJetInspectionReader, ReadsStandardImageHeadersWithReorderedPaddedHdf5Storage) {
  const auto path = make_test_dataset_path("reordered_padded_image_header");
  write_standard_dataset(path);
  replace_image_header_with_reordered_padded_layout(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  ksj::ismrmrd::InspectionImageRecord record;
  ASSERT_TRUE(reader.read_image_record({.series_id = std::string(kImageSeries), .ordinal = 0U}, record, error))
    << error;
  EXPECT_EQ(record.header.measurement_uid, 61U);
  EXPECT_EQ(record.header.matrix_size, (std::array<std::uint16_t, 3U>{2U, 3U, 4U}));
  EXPECT_EQ(record.header.channels, 2U);
  EXPECT_EQ(record.header.image_index, 7U);
  EXPECT_EQ(record.header.image_series_index, 11U);
  std::vector<float> pixels;
  EXPECT_EQ(
    reader.with_image_pixels(
      {.series_id = std::string(kImageSeries), .ordinal = 0U},
      [&](const ksj::ismrmrd::InspectionImageRecord& callback_record, const ksj::ismrmrd::ImagePixelsView& image) {
        EXPECT_EQ(callback_record.header.matrix_size, (std::array<std::uint16_t, 3U>{2U, 3U, 4U}));
        EXPECT_EQ(image.dimensions, (std::array<std::uint16_t, 4U>{2U, 3U, 4U, 2U}));
        pixels.resize(image.pixels.size() / sizeof(float));
        std::memcpy(pixels.data(), image.pixels.data(), image.pixels.size());
        return true;
      },
      error),
    ksj::ismrmrd::InspectionIterationResult::completed)
    << error;
  ASSERT_EQ(pixels.size(), 48U);
  EXPECT_FLOAT_EQ(pixels.front(), 0.0F);
  EXPECT_FLOAT_EQ(pixels.back(), expected_pixel(1U, 2U, 3U, 1U));
}

TEST(KSpaceJetInspectionReader, RejectsIncompleteImageHeaderCompoundsBeforeImagePayloadRead) {
  const auto path = make_test_dataset_path("incomplete_image_header");
  write_standard_dataset(path);
  replace_image_header_with_incomplete_compound(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  ksj::ismrmrd::InspectionImageRecord record;
  EXPECT_FALSE(reader.read_image_record({.series_id = std::string(kImageSeries), .ordinal = 0U}, record, error));
  EXPECT_EQ(error, "ISMRMRD image header is malformed.");
}

TEST(KSpaceJetInspectionReader, RejectsAcquisitionVlenElementsThatAreNotStandardFloatStorage) {
  const auto path = make_test_dataset_path("int32_acquisition_vlen");
  write_standard_dataset(path);
  replace_acquisition_data_with_int32_vlen(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  EXPECT_EQ(reader.visit_acquisition(
              0U,
              [](const auto&) {
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::failed);
  EXPECT_EQ(error, "ISMRMRD acquisition payload is malformed.");
}

TEST(KSpaceJetInspectionReader, ReadsStandardAcquisitionHeadersWithReorderedPaddedHdf5Storage) {
  const auto path = make_test_dataset_path("reordered_padded_acquisition_header");
  write_standard_dataset(path);
  replace_acquisition_header_with_reordered_padded_layout(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  std::vector<std::uint32_t> scan_counters;
  EXPECT_EQ(reader.for_each_acquisition(
              [&](const ksj::ismrmrd::InspectionAcquisitionView& acquisition) {
                scan_counters.push_back(acquisition.header.scan_counter);
                EXPECT_EQ(acquisition.samples.size(), 6U);
                EXPECT_EQ(acquisition.trajectory.size(), 6U);
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::completed)
    << error;
  EXPECT_EQ(scan_counters, (std::vector<std::uint32_t>{23U, 24U}));
}

TEST(KSpaceJetInspectionReader, KeepsActiveReadStorageAliveWhenConsumerMovesTheReader) {
  const auto path = make_test_dataset_path("move_reader_during_callback");
  write_standard_dataset(path);

  ksj::ismrmrd::InspectionReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, std::string(kDatasetGroup), {}, error)) << error;
  std::uint32_t observed = 0U;
  EXPECT_EQ(reader.for_each_acquisition(
              [&](const ksj::ismrmrd::InspectionAcquisitionView&) {
                ++observed;
                if (observed == 1U) {
                  reader = ksj::ismrmrd::InspectionReader{};
                }
                return true;
              },
              error),
            ksj::ismrmrd::InspectionIterationResult::completed)
    << error;
  EXPECT_EQ(observed, 2U);
  EXPECT_FALSE(reader.is_open());
}
