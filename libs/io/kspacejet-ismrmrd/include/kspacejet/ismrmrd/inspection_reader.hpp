#pragma once

#include "kspacejet/ismrmrd/dataset_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ksj::ismrmrd {

// These limits are deliberately per inspection operation. The reader does not
// retain acquisition or image payloads; limits protect the one source element
// that can be materialized for a synchronous inspection callback.
struct InspectionReadLimits {
  std::size_t max_xml_header_bytes{4U * 1024U * 1024U};
  std::uint32_t max_acquisition_count{1'000'000U};
  std::size_t max_acquisition_payload_bytes{64U * 1024U * 1024U};
  std::uint32_t max_hdf5_group_links{1'024U};
  std::uint32_t max_hdf5_group_count{4'096U};
  std::uint32_t max_hdf5_group_depth{16U};
  std::size_t max_hdf5_group_path_bytes{4U * 1024U};
  // Bounds HDF5 group-name materialization during container discovery.
  std::size_t max_hdf5_group_name_bytes{256U};
  std::uint32_t max_image_series{128U};
  std::size_t max_image_series_name_bytes{256U};
  std::uint32_t max_images_per_series{100'000U};
  std::size_t max_image_payload_bytes{256U * 1024U * 1024U};
  std::size_t max_image_attribute_bytes{1U * 1024U * 1024U};
  std::size_t max_meta_attribute_entries{1'024U};
  std::size_t max_meta_attribute_name_bytes{4U * 1024U};
  std::size_t max_meta_attribute_value_bytes{64U * 1024U};
};

struct InspectionImageSeriesDescriptor {
  std::string series_id;
  std::uint32_t image_count{0U};
};

// A compact, read-only structural summary of one standard MRD container. Path
// is an absolute HDF5 path. It intentionally excludes XML and all raw
// acquisition, image, and waveform payloads.
struct InspectionMrdContainerDescriptor {
  std::string path;
  bool has_header{false};
  bool has_acquisitions{false};
  bool has_waveforms{false};
  bool has_images{false};
  std::uint32_t acquisition_count{0U};
  std::uint32_t image_series_count{0U};
};

// XML and image-series descriptors are owned by the reader and remain valid
// until its next successful open or destruction. They never contain pixel or
// acquisition payloads.
struct InspectionDatasetMetadata {
  std::string group;
  std::string xml_header;
  std::uint32_t acquisition_count{0U};
  std::vector<InspectionImageSeriesDescriptor> image_series;
};

// `ordinal` is the zero-based storage ordinal in the ISMRMRD acquisition
// dataset. Samples and trajectory are borrowed for the duration of one
// callback only.
struct InspectionAcquisitionView {
  std::uint32_t ordinal{0U};
  AcquisitionHeader header;

  // ISMRMRD sample layout: samples[sample + channel * number_of_samples].
  std::span<const std::complex<float>> samples;
  // ISMRMRD trajectory layout: trajectory[sample * trajectory_dimensions + dimension].
  std::span<const float> trajectory;
};

// An owned header-only acquisition record. Unlike InspectionAcquisitionView,
// this never exposes sample or trajectory storage and may safely be retained
// by a caller that builds a bounded acquisition index.
struct InspectionAcquisitionHeaderRecord {
  std::uint32_t ordinal{0U};
  AcquisitionHeader header;
};

enum class ImageDataType : std::uint16_t {
  unsigned_integer_16 = 1U,
  signed_integer_16 = 2U,
  unsigned_integer_32 = 3U,
  signed_integer_32 = 4U,
  real_32 = 5U,
  real_64 = 6U,
  complex_32 = 7U,
  complex_64 = 8U,
};

// A project-owned copy of the standard ISMRMRD image header. `matrix_size`
// uses the standard [x, y, z] order and field_of_view_mm uses the same axes.
struct InspectionImageHeader {
  std::uint16_t version{0U};
  ImageDataType data_type{ImageDataType::real_32};
  std::uint64_t flags{0U};
  std::uint32_t measurement_uid{0U};
  std::array<std::uint16_t, 3> matrix_size{};
  std::array<float, 3> field_of_view_mm{};
  std::uint16_t channels{0U};
  std::array<float, 3> position{};
  std::array<float, 3> read_dir{};
  std::array<float, 3> phase_dir{};
  std::array<float, 3> slice_dir{};
  std::array<float, 3> patient_table_position{};
  std::uint16_t average{0U};
  std::uint16_t slice{0U};
  std::uint16_t contrast{0U};
  std::uint16_t phase{0U};
  std::uint16_t repetition{0U};
  std::uint16_t set{0U};
  std::uint32_t acquisition_time_stamp{0U};
  std::array<std::uint32_t, 3> physiology_time_stamp{};
  std::uint16_t image_type{0U};
  std::uint16_t image_index{0U};
  std::uint16_t image_series_index{0U};
  std::array<std::int32_t, 8> user_int{};
  std::array<float, 8> user_float{};
  std::uint32_t attribute_string_bytes{0U};
};

// ISMRMRD MetaAttributes support multiple values for one name. Keep that
// standard semantic instead of flattening it into a JSON/object map.
struct MetaAttribute {
  std::string name;
  std::vector<std::string> values;
};

// Storage ordinal is intentionally separate from InspectionImageHeader::image_index.
// For example, KSpaceJet writes the first record at ordinal 0 with image_index 1.
struct ImageLocator {
  std::string series_id;
  std::uint32_t ordinal{0U};
};

// Header and MetaAttributes are owned values and may outlive callbacks. This
// operation does not materialize image pixels.
struct InspectionImageRecord {
  ImageLocator locator;
  InspectionImageHeader header;
  std::vector<MetaAttribute> meta_attributes;
};

// Pixel bytes remain in ISMRMRD binding storage and are valid only for the
// ImagePixelConsumer invocation. Their logical axis order is
// [x, y, z, channel], with x fastest:
// x + X * (y + Y * (z + Z * channel)).
struct ImagePixelsView {
  ImageDataType data_type{ImageDataType::real_32};
  std::array<std::uint16_t, 4> dimensions{};
  std::span<const std::byte> pixels;
};

enum class InspectionIterationResult {
  completed,
  stopped,
  failed,
};

using InspectionAcquisitionConsumer = std::function<bool(const InspectionAcquisitionView&)>;
using InspectionAcquisitionHeaderConsumer = std::function<bool(const InspectionAcquisitionHeaderRecord&)>;
using ImagePixelConsumer = std::function<bool(const InspectionImageRecord&, const ImagePixelsView&)>;

// Read-only, move-only inspection facade for one standard ISMRMRD HDF5
// dataset. It owns no raw payloads and exposes neither HDF5 nor upstream
// ISMRMRD types. Reads are synchronous and non-reentrant. Moving or assigning
// the reader from a consumer cannot invalidate the callback-scoped source
// storage of the in-flight read.
class InspectionReader {
public:
  InspectionReader();
  ~InspectionReader();

  InspectionReader(const InspectionReader&) = delete;
  InspectionReader& operator=(const InspectionReader&) = delete;
  InspectionReader(InspectionReader&&) noexcept;
  InspectionReader& operator=(InspectionReader&&) noexcept;

  // Recursively enumerates bounded HDF5 groups and returns standard header
  // containers that pass this reader's `open` preflight plus structurally
  // valid standalone image-series groups. Results contain no raw payload,
  // use absolute HDF5 paths, are sorted by path, and do not change any reader
  // instance state.
  [[nodiscard]] static bool discover_mrd_containers(const std::filesystem::path& file, InspectionReadLimits limits,
                                                    std::vector<InspectionMrdContainerDescriptor>& containers,
                                                    std::string& error);

  // Opens one discovered standard MRD container. `container_path` may name a
  // normal XML-header container or a standalone standard image-series group;
  // neither form is required to live below a path named "/dataset".
  [[nodiscard]] bool open(const std::filesystem::path& file, std::string container_path, InspectionReadLimits limits,
                          std::string& error);
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] const InspectionDatasetMetadata& metadata() const noexcept;

  // Reads one copied standard acquisition header after named-field HDF5
  // preflight. It never materializes the acquisition samples or trajectory.
  // On failure `header` is cleared and no partial header escapes.
  [[nodiscard]] bool read_acquisition_header(std::uint32_t ordinal, AcquisitionHeader& header, std::string& error);

  // Iterates copied standard acquisition headers without materializing any
  // acquisition sample or trajectory payload. A false consumer return is a
  // normal stopped result. Each record is an owned value and may be retained
  // by the consumer subject to its own bounds.
  [[nodiscard]] InspectionIterationResult
  for_each_acquisition_header(const InspectionAcquisitionHeaderConsumer& consumer, std::string& error);

  // `visit_acquisition` reads exactly one zero-based acquisition. A false
  // consumer return is a normal stopped result.
  [[nodiscard]] InspectionIterationResult
  visit_acquisition(std::uint32_t ordinal, const InspectionAcquisitionConsumer& consumer, std::string& error);
  [[nodiscard]] InspectionIterationResult for_each_acquisition(const InspectionAcquisitionConsumer& consumer,
                                                               std::string& error);

  // Resolves one named ISMRMRD image series and zero-based storage ordinal.
  // On failure `record` is cleared and no partial header or attributes escape.
  [[nodiscard]] bool read_image_record(const ImageLocator& locator, InspectionImageRecord& record, std::string& error);

  // Reads pixels only after bounded header/data preflight. The record and
  // pixels view are valid only while `consumer` executes; callers that need
  // long-lived UI state must make their own explicitly bounded copy.
  [[nodiscard]] InspectionIterationResult with_image_pixels(const ImageLocator& locator,
                                                            const ImagePixelConsumer& consumer, std::string& error);

private:
  struct Impl;

  std::shared_ptr<Impl> impl_;
  InspectionDatasetMetadata metadata_;
  InspectionReadLimits limits_;
};

} // namespace ksj::ismrmrd
