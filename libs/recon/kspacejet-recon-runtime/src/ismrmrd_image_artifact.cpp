#include "kspacejet/recon/runtime/ismrmrd_image_artifact_sink.hpp"

#include "kspacejet/base/file.hpp"
#include "kspacejet/base/path.hpp"
#include "kspacejet/recon/runtime/synchronous_graph_executor.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <ismrmrd/dataset.h>
#include <ismrmrd/meta.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <thread>

namespace ksj::recon::runtime {
namespace {

using ksj::base::Status;

constexpr char kDatasetGroup[] = "dataset";
constexpr char kImageSeries[] = "image_0";
constexpr std::uint16_t kImageIndex = 1U;
constexpr std::uint16_t kImageSeriesIndex = 0U;

[[nodiscard]] bool has_mrd_extension(const std::filesystem::path& path) {
  return path.extension() == ".mrd";
}

[[nodiscard]] Status checked_pixel_count(const IsmrmrdMagnitudeImageArtifactDescriptor& descriptor,
                                         std::size_t& pixel_count) {
  if (descriptor.rows == 0U || descriptor.cols == 0U) {
    return Status::InvalidArgument("ISMRMRD image artifact requires nonzero rows and columns");
  }
  if (descriptor.rows > std::numeric_limits<std::uint16_t>::max() ||
      descriptor.cols > std::numeric_limits<std::uint16_t>::max()) {
    return Status::InvalidArgument("ISMRMRD image artifact matrix exceeds the standard uint16 dimensions");
  }
  const auto rows = static_cast<std::size_t>(descriptor.rows);
  const auto cols = static_cast<std::size_t>(descriptor.cols);
  if (rows > std::numeric_limits<std::size_t>::max() / cols) {
    return Status::InvalidArgument("ISMRMRD image artifact pixel count overflows size_t");
  }
  pixel_count = rows * cols;
  if (pixel_count > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
    return Status::InvalidArgument("ISMRMRD image artifact payload size overflows size_t");
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_artifact(const IsmrmrdMagnitudeImageArtifactDescriptor& descriptor,
                                       const ksj::base::ConstByteSpan float32_payload, std::size_t& pixel_count) {
  if (descriptor.source_xml.empty()) {
    return Status::InvalidArgument("ISMRMRD image artifact requires the validated source XML header");
  }
  const auto dimensions = checked_pixel_count(descriptor, pixel_count);
  if (!dimensions.ok())
    return dimensions;
  const auto expected_bytes = pixel_count * sizeof(float);
  if (float32_payload.size() != expected_bytes) {
    return Status::InvalidArgument("ISMRMRD image artifact payload does not match its declared float32 matrix");
  }
  const auto valid_fov = [](const double value) {
    return std::isfinite(value) && value > 0.0 && value <= std::numeric_limits<float>::max();
  };
  if (!valid_fov(descriptor.field_of_view_mm.x) || !valid_fov(descriptor.field_of_view_mm.y) ||
      !valid_fov(descriptor.field_of_view_mm.z)) {
    return Status::ValidationError("ISMRMRD image artifact requires a finite positive reconstruction field of view");
  }
  for (std::size_t index = 0U; index < pixel_count; ++index) {
    float value = 0.0F;
    std::memcpy(&value, float32_payload.data() + index * sizeof(float), sizeof(value));
    if (!std::isfinite(value) || value < 0.0F) {
      return Status::ValidationError("ISMRMRD magnitude image artifact rejects negative or non-finite float32 pixels");
    }
  }
  for (const auto& [name, value] : descriptor.provenance_attributes) {
    if (name.empty() || value.empty()) {
      return Status::InvalidArgument("ISMRMRD image artifact provenance attributes require nonempty names and values");
    }
    if (name == "DataRole" || name == "ImageNumber") {
      return Status::InvalidArgument("ISMRMRD image artifact reserves DataRole and ImageNumber metadata");
    }
  }
  return Status::Ok();
}

[[nodiscard]] std::filesystem::path make_temporary_sibling(const std::filesystem::path& output_file) {
  static std::atomic<std::uint64_t> sequence{0U};
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
  const auto ticket = sequence.fetch_add(1U, std::memory_order_relaxed);
  auto temporary = output_file;
  temporary += ".tmp.";
  temporary += std::to_string(timestamp);
  temporary += ".";
  temporary += std::to_string(thread_id);
  temporary += ".";
  temporary += std::to_string(ticket);
  return temporary;
}

[[nodiscard]] Status make_output_parent(const std::filesystem::path& output_file) {
  const auto parent = output_file.parent_path();
  if (parent.empty())
    return Status::Ok();
  if (!ksj::base::path::ensure_directory_exists(parent)) {
    return Status::IoError(
      ksj::base::path::format_prepare_directory_error(parent.string(), "parent directory is not writable"));
  }
  return Status::Ok();
}

void copy_source_header(const ksj::ismrmrd::AcquisitionHeader& source, ISMRMRD::Image<float>& image,
                        const FieldOfViewMm& field_of_view_mm) {
  auto& header = image.getHead();
  header.measurement_uid = source.measurement_uid;
  header.field_of_view[0] = static_cast<float>(field_of_view_mm.x);
  header.field_of_view[1] = static_cast<float>(field_of_view_mm.y);
  header.field_of_view[2] = static_cast<float>(field_of_view_mm.z);
  std::copy(source.position.begin(), source.position.end(), std::begin(header.position));
  std::copy(source.read_dir.begin(), source.read_dir.end(), std::begin(header.read_dir));
  std::copy(source.phase_dir.begin(), source.phase_dir.end(), std::begin(header.phase_dir));
  std::copy(source.slice_dir.begin(), source.slice_dir.end(), std::begin(header.slice_dir));
  std::copy(source.patient_table_position.begin(), source.patient_table_position.end(),
            std::begin(header.patient_table_position));
  header.average = source.index.average;
  header.slice = source.index.slice;
  header.contrast = source.index.contrast;
  header.phase = source.index.phase;
  header.repetition = source.index.repetition;
  header.set = source.index.set;
  header.acquisition_time_stamp = source.acquisition_time_stamp;
  std::copy(source.physiology_time_stamp.begin(), source.physiology_time_stamp.end(),
            std::begin(header.physiology_time_stamp));
  header.image_type = ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE;
  header.image_index = kImageIndex;
  header.image_series_index = kImageSeriesIndex;
}

[[nodiscard]] Status verify_temporary_artifact(const std::filesystem::path& path,
                                               const IsmrmrdMagnitudeImageArtifactDescriptor& descriptor,
                                               const ksj::base::ConstByteSpan float32_payload,
                                               const std::size_t pixel_count) {
  try {
    const auto filename = path.string();
    ISMRMRD::Dataset dataset(filename.c_str(), kDatasetGroup, false);
    std::string source_xml;
    dataset.readHeader(source_xml);
    if (source_xml != descriptor.source_xml) {
      return Status::IoError("ISMRMRD image artifact readback did not preserve its source XML header");
    }
    if (dataset.getNumberOfImages(kImageSeries) != 1U) {
      return Status::IoError("ISMRMRD image artifact readback did not contain exactly one image_0 image");
    }
    ISMRMRD::Image<float> image;
    dataset.readImage(kImageSeries, 0U, image);
    const auto& header = image.getHead();
    if (image.getDataType() != ISMRMRD::ISMRMRD_FLOAT || header.matrix_size[0] != descriptor.cols ||
        header.matrix_size[1] != descriptor.rows || header.matrix_size[2] != 1U || header.channels != 1U ||
        header.image_type != ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE || header.image_index != kImageIndex ||
        header.image_series_index != kImageSeriesIndex || image.getNumberOfDataElements() != pixel_count ||
        header.measurement_uid != descriptor.source_acquisition.measurement_uid ||
        header.acquisition_time_stamp != descriptor.source_acquisition.acquisition_time_stamp ||
        header.average != descriptor.source_acquisition.index.average ||
        header.slice != descriptor.source_acquisition.index.slice ||
        header.contrast != descriptor.source_acquisition.index.contrast ||
        header.phase != descriptor.source_acquisition.index.phase ||
        header.repetition != descriptor.source_acquisition.index.repetition ||
        header.set != descriptor.source_acquisition.index.set ||
        header.field_of_view[0] != static_cast<float>(descriptor.field_of_view_mm.x) ||
        header.field_of_view[1] != static_cast<float>(descriptor.field_of_view_mm.y) ||
        header.field_of_view[2] != static_cast<float>(descriptor.field_of_view_mm.z) ||
        !std::equal(descriptor.source_acquisition.position.begin(), descriptor.source_acquisition.position.end(),
                    std::begin(header.position)) ||
        !std::equal(descriptor.source_acquisition.read_dir.begin(), descriptor.source_acquisition.read_dir.end(),
                    std::begin(header.read_dir)) ||
        !std::equal(descriptor.source_acquisition.phase_dir.begin(), descriptor.source_acquisition.phase_dir.end(),
                    std::begin(header.phase_dir)) ||
        !std::equal(descriptor.source_acquisition.slice_dir.begin(), descriptor.source_acquisition.slice_dir.end(),
                    std::begin(header.slice_dir)) ||
        !std::equal(descriptor.source_acquisition.patient_table_position.begin(),
                    descriptor.source_acquisition.patient_table_position.end(),
                    std::begin(header.patient_table_position)) ||
        !std::equal(descriptor.source_acquisition.physiology_time_stamp.begin(),
                    descriptor.source_acquisition.physiology_time_stamp.end(),
                    std::begin(header.physiology_time_stamp))) {
      return Status::IoError("ISMRMRD image artifact readback header does not match the requested image profile");
    }
    if (std::memcmp(image.getDataPtr(), float32_payload.data(), float32_payload.size()) != 0) {
      return Status::IoError("ISMRMRD image artifact readback pixels do not match the reconstructed output");
    }
    std::string attributes;
    image.getAttributeString(attributes);
    ISMRMRD::MetaContainer metadata;
    ISMRMRD::deserialize(attributes.c_str(), metadata);
    if (metadata.length("DataRole") != 1U || std::string(metadata.as_str("DataRole")) != "Image" ||
        metadata.length("ImageNumber") != 1U || std::string(metadata.as_str("ImageNumber")) != "1") {
      return Status::IoError("ISMRMRD image artifact readback metadata is incomplete");
    }
    std::map<std::string, std::size_t, std::less<>> occurrences;
    for (const auto& [name, value] : descriptor.provenance_attributes) {
      const auto occurrence = occurrences[name]++;
      if (metadata.length(name.c_str()) <= occurrence ||
          std::string(metadata.as_str(name.c_str(), occurrence)) != value) {
        return Status::IoError("ISMRMRD image artifact readback provenance does not match the requested output");
      }
    }
    return Status::Ok();
  } catch (const std::exception& exception) {
    return Status::IoError("unable to verify ISMRMRD image artifact '" + path.string() + "': " + exception.what());
  } catch (...) {
    return Status::IoError("unable to verify ISMRMRD image artifact '" + path.string() + "'");
  }
}

void remove_temporary_file(const std::filesystem::path& path) noexcept {
  std::string error;
  static_cast<void>(ksj::base::file::remove_file(path, error));
}

} // namespace

IsmrmrdImageArtifactSink::IsmrmrdImageArtifactSink(std::filesystem::path output_file,
                                                   IsmrmrdMagnitudeImageArtifactDescriptor descriptor)
    : output_file_(std::move(output_file)), descriptor_(std::move(descriptor)) {}

const std::filesystem::path& IsmrmrdImageArtifactSink::output_file() const noexcept {
  return output_file_;
}

Status IsmrmrdImageArtifactSink::commit(EgressInputLease& image) {
  if (published_) {
    return Status::StateError("ISMRMRD image artifact Sink has already published its terminal image");
  }
  if (output_file_.empty()) {
    return Status::InvalidArgument("ISMRMRD image artifact Sink requires an output path");
  }
  if (!has_mrd_extension(output_file_)) {
    return Status::InvalidArgument("ISMRMRD image artifact Sink output must use the .mrd extension");
  }
  auto expected_type = types::image_frame();
  if (!expected_type.ok()) {
    return expected_type.status();
  }
  auto payload = image.payload();
  if (!payload.ok()) {
    return payload.status();
  }
  auto metadata = image.metadata();
  if (!metadata.ok()) {
    return metadata.status();
  }
  if (image.type_descriptor() == nullptr || !image.type_descriptor()->exactly_matches(expected_type.value()) ||
      !metadata.value().empty()) {
    return Status::ValidationError("ISMRMRD image artifact Sink requires one exact ksj.image-frame payload without "
                                   "graph metadata");
  }
  std::size_t pixel_count = 0U;
  const auto valid = validate_artifact(descriptor_, payload.value(), pixel_count);
  if (!valid.ok())
    return valid;
  const auto parent = make_output_parent(output_file_);
  if (!parent.ok())
    return parent;

  const auto temporary = make_temporary_sibling(output_file_);
  std::error_code exists_error;
  if (std::filesystem::exists(temporary, exists_error) || exists_error) {
    return Status::IoError("unable to allocate a unique temporary ISMRMRD image artifact path: " + temporary.string());
  }

  try {
    {
      const auto temporary_filename = temporary.string();
      ISMRMRD::Dataset dataset(temporary_filename.c_str(), kDatasetGroup, true);
      dataset.writeHeader(descriptor_.source_xml);

      ISMRMRD::Image<float> output_image(static_cast<std::uint16_t>(descriptor_.cols),
                                         static_cast<std::uint16_t>(descriptor_.rows), 1U, 1U);
      copy_source_header(descriptor_.source_acquisition, output_image, descriptor_.field_of_view_mm);
      std::memcpy(output_image.getDataPtr(), payload.value().data(), payload.value().size());

      ISMRMRD::MetaContainer metadata;
      metadata.set("DataRole", "Image");
      metadata.set("ImageNumber", "1");
      for (const auto& [name, value] : descriptor_.provenance_attributes) {
        metadata.append(name.c_str(), value.c_str());
      }
      std::ostringstream serialized_metadata;
      ISMRMRD::serialize(metadata, serialized_metadata);
      output_image.setAttributeString(serialized_metadata.str());
      dataset.appendImage(kImageSeries, output_image);
    }
  } catch (const std::exception& exception) {
    remove_temporary_file(temporary);
    return Status::IoError("unable to write ISMRMRD image artifact '" + output_file_.string() +
                           "': " + exception.what());
  } catch (...) {
    remove_temporary_file(temporary);
    return Status::IoError("unable to write ISMRMRD image artifact '" + output_file_.string() + "'");
  }

  const auto verified = verify_temporary_artifact(temporary, descriptor_, payload.value(), pixel_count);
  if (!verified.ok()) {
    remove_temporary_file(temporary);
    return verified;
  }
  std::string replace_error;
  if (!ksj::base::file::replace_file(temporary, output_file_, replace_error)) {
    remove_temporary_file(temporary);
    return Status::IoError("unable to publish ISMRMRD image artifact '" + output_file_.string() +
                           "': " + replace_error);
  }
  published_ = true;
  return image.acknowledge_consumed();
}

} // namespace ksj::recon::runtime
