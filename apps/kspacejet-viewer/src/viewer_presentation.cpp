#include "viewer_presentation.hpp"

#include "kspacejet/recon/execution_profile.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"
#include "kspacejet/recon/scan_descriptor.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ksj::ismrmrd::ImageDataType;
using ksj::ismrmrd::ImagePixelsView;

constexpr int kKspaceMaximumDimension = 2'048;
constexpr std::size_t kKspaceMaximumPixels = 2U * 1024U * 1024U;
constexpr std::size_t kKspaceMaximumSourceLines = 16'384U;
constexpr std::size_t kKspaceMaximumSourceComplexValues = 32U * 1024U * 1024U;
constexpr int kImageMaximumDimension = 2'048;
constexpr std::size_t kImageMaximumPixels = 2U * 1024U * 1024U;
constexpr std::size_t kMaximumExportRows = 4'096U;
constexpr qsizetype kMaximumXmlPreviewCharacters = 128U * 1024U;
constexpr qsizetype kMaximumMetadataValuePreviewCharacters = 256U;

// ISMRMRD acquisition flag values are one-based bit positions in the standard
// header. Keep this private to the inspection-only default-view predicate: the
// Viewer never changes an acquisition's flags or uses this as runtime routing.
enum class StandardAcquisitionFlag : std::uint8_t {
  noise_measurement = 19U,
  parallel_calibration = 20U,
  parallel_calibration_and_imaging = 21U,
  navigation = 23U,
  phase_correction = 24U,
  high_performance_feedback = 26U,
  dummy_scan = 27U,
  realtime_feedback = 28U,
  surface_coil_correction = 29U,
  phase_stabilization_reference = 30U,
  phase_stabilization = 31U,
};

[[nodiscard]] constexpr std::uint64_t standard_acquisition_flag_mask(const StandardAcquisitionFlag flag) noexcept {
  return std::uint64_t{1U} << (static_cast<std::uint8_t>(flag) - 1U);
}

[[nodiscard]] constexpr bool has_standard_acquisition_flag(const std::uint64_t flags,
                                                           const StandardAcquisitionFlag flag) noexcept {
  return (flags & standard_acquisition_flag_mask(flag)) != 0U;
}

using CartesianKspaceAcquisitionKind = ksj::viewer::CartesianKspaceAcquisitionKind;

[[nodiscard]] constexpr bool
cartesian_kspace_acquisition_kind_is_imaging(const CartesianKspaceAcquisitionKind kind) noexcept {
  return kind == CartesianKspaceAcquisitionKind::imaging;
}

// This is deliberately a flag-membership predicate rather than a priority
// bucket. A standard acquisition that carries both navigation and
// surface-coil-correction flags belongs to both explicit auxiliary views, but
// never to the default Imaging data view.
[[nodiscard]] bool cartesian_kspace_acquisition_kind_matches(const ksj::ismrmrd::AcquisitionHeader& header,
                                                             const CartesianKspaceAcquisitionKind kind) noexcept {
  const auto flags = header.flags;
  const auto combined_calibration_and_imaging =
    has_standard_acquisition_flag(flags, StandardAcquisitionFlag::parallel_calibration_and_imaging);
  const auto calibration_only = !combined_calibration_and_imaging &&
                                has_standard_acquisition_flag(flags, StandardAcquisitionFlag::parallel_calibration);
  const auto has_auxiliary_membership =
    calibration_only || has_standard_acquisition_flag(flags, StandardAcquisitionFlag::noise_measurement) ||
    has_standard_acquisition_flag(flags, StandardAcquisitionFlag::navigation) ||
    has_standard_acquisition_flag(flags, StandardAcquisitionFlag::phase_correction) ||
    has_standard_acquisition_flag(flags, StandardAcquisitionFlag::high_performance_feedback) ||
    has_standard_acquisition_flag(flags, StandardAcquisitionFlag::realtime_feedback) ||
    has_standard_acquisition_flag(flags, StandardAcquisitionFlag::dummy_scan) ||
    has_standard_acquisition_flag(flags, StandardAcquisitionFlag::surface_coil_correction) ||
    has_standard_acquisition_flag(flags, StandardAcquisitionFlag::phase_stabilization_reference) ||
    has_standard_acquisition_flag(flags, StandardAcquisitionFlag::phase_stabilization);

  switch (kind) {
    case CartesianKspaceAcquisitionKind::imaging:
      return !has_auxiliary_membership;
    case CartesianKspaceAcquisitionKind::noise_measurement:
      return has_standard_acquisition_flag(flags, StandardAcquisitionFlag::noise_measurement);
    case CartesianKspaceAcquisitionKind::parallel_calibration:
      return calibration_only;
    case CartesianKspaceAcquisitionKind::navigation:
      return has_standard_acquisition_flag(flags, StandardAcquisitionFlag::navigation);
    case CartesianKspaceAcquisitionKind::phase_correction:
      return has_standard_acquisition_flag(flags, StandardAcquisitionFlag::phase_correction);
    case CartesianKspaceAcquisitionKind::high_performance_feedback:
      return has_standard_acquisition_flag(flags, StandardAcquisitionFlag::high_performance_feedback);
    case CartesianKspaceAcquisitionKind::realtime_feedback:
      return has_standard_acquisition_flag(flags, StandardAcquisitionFlag::realtime_feedback);
    case CartesianKspaceAcquisitionKind::dummy_scan:
      return has_standard_acquisition_flag(flags, StandardAcquisitionFlag::dummy_scan);
    case CartesianKspaceAcquisitionKind::surface_coil_correction:
      return has_standard_acquisition_flag(flags, StandardAcquisitionFlag::surface_coil_correction);
    case CartesianKspaceAcquisitionKind::phase_stabilization_reference:
      return has_standard_acquisition_flag(flags, StandardAcquisitionFlag::phase_stabilization_reference);
    case CartesianKspaceAcquisitionKind::phase_stabilization:
      return has_standard_acquisition_flag(flags, StandardAcquisitionFlag::phase_stabilization);
    case CartesianKspaceAcquisitionKind::count:
      return false;
  }
  return false;
}

[[nodiscard]] QString cartesian_kspace_acquisition_kind_label(const CartesianKspaceAcquisitionKind kind) {
  switch (kind) {
    case CartesianKspaceAcquisitionKind::imaging:
      return QStringLiteral("Imaging data");
    case CartesianKspaceAcquisitionKind::noise_measurement:
      return QStringLiteral("Noise measurement");
    case CartesianKspaceAcquisitionKind::parallel_calibration:
      return QStringLiteral("Parallel calibration");
    case CartesianKspaceAcquisitionKind::navigation:
      return QStringLiteral("Navigation");
    case CartesianKspaceAcquisitionKind::phase_correction:
      return QStringLiteral("Phase correction");
    case CartesianKspaceAcquisitionKind::high_performance_feedback:
      return QStringLiteral("High-performance feedback");
    case CartesianKspaceAcquisitionKind::realtime_feedback:
      return QStringLiteral("Real-time feedback");
    case CartesianKspaceAcquisitionKind::dummy_scan:
      return QStringLiteral("Dummy scan");
    case CartesianKspaceAcquisitionKind::surface_coil_correction:
      return QStringLiteral("Surface-coil correction");
    case CartesianKspaceAcquisitionKind::phase_stabilization_reference:
      return QStringLiteral("Phase-stabilization reference");
    case CartesianKspaceAcquisitionKind::phase_stabilization:
      return QStringLiteral("Phase stabilization");
    case CartesianKspaceAcquisitionKind::count:
      break;
  }
  return QStringLiteral("Acquisition data");
}

[[nodiscard]] QString cartesian_kspace_acquisition_kind_identifier(const CartesianKspaceAcquisitionKind kind) {
  switch (kind) {
    case CartesianKspaceAcquisitionKind::imaging:
      return QStringLiteral("imaging");
    case CartesianKspaceAcquisitionKind::noise_measurement:
      return QStringLiteral("noise_measurement");
    case CartesianKspaceAcquisitionKind::parallel_calibration:
      return QStringLiteral("parallel_calibration");
    case CartesianKspaceAcquisitionKind::navigation:
      return QStringLiteral("navigation");
    case CartesianKspaceAcquisitionKind::phase_correction:
      return QStringLiteral("phase_correction");
    case CartesianKspaceAcquisitionKind::high_performance_feedback:
      return QStringLiteral("high_performance_feedback");
    case CartesianKspaceAcquisitionKind::realtime_feedback:
      return QStringLiteral("realtime_feedback");
    case CartesianKspaceAcquisitionKind::dummy_scan:
      return QStringLiteral("dummy_scan");
    case CartesianKspaceAcquisitionKind::surface_coil_correction:
      return QStringLiteral("surface_coil_correction");
    case CartesianKspaceAcquisitionKind::phase_stabilization_reference:
      return QStringLiteral("phase_stabilization_reference");
    case CartesianKspaceAcquisitionKind::phase_stabilization:
      return QStringLiteral("phase_stabilization");
    case CartesianKspaceAcquisitionKind::count:
      break;
  }
  return QStringLiteral("acquisition_data");
}

struct CartesianKspaceAcquisitionKindSummary final {
  std::array<std::size_t, static_cast<std::size_t>(CartesianKspaceAcquisitionKind::count)> counts{};

  void record(const ksj::ismrmrd::AcquisitionHeader& header) noexcept {
    for (std::size_t index = 0U; index < counts.size(); ++index) {
      const auto kind = static_cast<CartesianKspaceAcquisitionKind>(index);
      if (cartesian_kspace_acquisition_kind_matches(header, kind)) {
        ++counts[index];
      }
    }
  }

  [[nodiscard]] std::size_t count(const CartesianKspaceAcquisitionKind kind) const noexcept {
    return counts.at(static_cast<std::size_t>(kind));
  }

  [[nodiscard]] QString description_excluding(const CartesianKspaceAcquisitionKind selected_kind) const {
    QStringList parts;
    for (std::size_t index = 0U; index < counts.size(); ++index) {
      const auto kind = static_cast<CartesianKspaceAcquisitionKind>(index);
      const auto matching_count = counts.at(index);
      if (kind == selected_kind || matching_count == 0U) {
        continue;
      }
      parts.append(QStringLiteral("%1 (%2)")
                     .arg(cartesian_kspace_acquisition_kind_label(kind))
                     .arg(static_cast<qulonglong>(matching_count)));
    }
    return parts.join(QStringLiteral(", "));
  }

  [[nodiscard]] QJsonObject json_excluding(const CartesianKspaceAcquisitionKind selected_kind) const {
    QJsonObject result;
    for (std::size_t index = 0U; index < counts.size(); ++index) {
      const auto kind = static_cast<CartesianKspaceAcquisitionKind>(index);
      if (kind != selected_kind) {
        result.insert(cartesian_kspace_acquisition_kind_identifier(kind), static_cast<int>(counts.at(index)));
      }
    }
    return result;
  }
};

struct DisplayExtent {
  int width = 0;
  int height = 0;
};

[[nodiscard]] QString to_qstring(const std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString source_description(const ksj::viewer::InspectionSession& session) {
  return QStringLiteral("%1 (container %2)").arg(session.source_path(), session.container_path());
}

[[nodiscard]] QString preview(const QString& value, const qsizetype maximum_characters) {
  if (value.size() <= maximum_characters) {
    return value;
  }
  return QStringLiteral("%1\n\n[Viewer preview truncated at %2 characters.]")
    .arg(value.left(maximum_characters))
    .arg(maximum_characters);
}

[[nodiscard]] std::optional<DisplayExtent> make_display_extent(const std::size_t source_width,
                                                               const std::size_t source_height,
                                                               const int maximum_dimension,
                                                               const std::size_t maximum_pixels) {
  if (source_width == 0U || source_height == 0U || maximum_dimension <= 0 || maximum_pixels == 0U) {
    return std::nullopt;
  }

  auto width = std::min(source_width, static_cast<std::size_t>(maximum_dimension));
  auto height = std::min(source_height, static_cast<std::size_t>(maximum_dimension));
  while (width > 0U && height > 0U && width > maximum_pixels / height) {
    if (width >= height && width > 1U) {
      width = (width + 1U) / 2U;
    } else if (height > 1U) {
      height = (height + 1U) / 2U;
    } else {
      return std::nullopt;
    }
  }

  if (width > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      height > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  return DisplayExtent{.width = static_cast<int>(width), .height = static_cast<int>(height)};
}

[[nodiscard]] std::size_t source_index_for_display(const int display_index, const int display_count,
                                                   const std::size_t source_count) {
  if (display_index <= 0 || display_count <= 1 || source_count <= 1U) {
    return 0U;
  }
  const auto numerator = static_cast<std::size_t>(display_index) * (source_count - 1U);
  return numerator / static_cast<std::size_t>(display_count - 1);
}

[[nodiscard]] bool checked_multiply(const std::size_t left, const std::size_t right, std::size_t& result) {
  if (left == 0U || right == 0U) {
    result = 0U;
    return true;
  }
  if (left > std::numeric_limits<std::size_t>::max() / right) {
    return false;
  }
  result = left * right;
  return true;
}

[[nodiscard]] bool checked_add(const std::size_t left, const std::size_t right, std::size_t& result) {
  if (left > std::numeric_limits<std::size_t>::max() - right) {
    return false;
  }
  result = left + right;
  return true;
}

using CartesianKspaceAxes = ksj::viewer::CartesianKspaceAxes;
using CartesianKspaceCatalog = ksj::viewer::CartesianKspaceCatalog;
using CartesianKspaceCatalogEntry = ksj::viewer::CartesianKspaceCatalogEntry;
using CartesianKspaceCoordinate = ksj::viewer::CartesianKspaceCoordinate;
using CartesianKspaceDimension = ksj::viewer::CartesianKspaceDimension;

constexpr std::array kCartesianKspaceDimensions{
  CartesianKspaceDimension::readout,
  CartesianKspaceDimension::phase_encode,
  CartesianKspaceDimension::coil,
  CartesianKspaceDimension::encoding_space,
  CartesianKspaceDimension::partition,
  CartesianKspaceDimension::average,
  CartesianKspaceDimension::slice,
  CartesianKspaceDimension::contrast,
  CartesianKspaceDimension::physiological_phase,
  CartesianKspaceDimension::repetition,
  CartesianKspaceDimension::set,
  CartesianKspaceDimension::segment,
  CartesianKspaceDimension::user_0,
  CartesianKspaceDimension::user_1,
  CartesianKspaceDimension::user_2,
  CartesianKspaceDimension::user_3,
  CartesianKspaceDimension::user_4,
  CartesianKspaceDimension::user_5,
  CartesianKspaceDimension::user_6,
  CartesianKspaceDimension::user_7,
};

constexpr std::size_t kKspaceMaximumObservedValues = 16'384U;

[[nodiscard]] constexpr bool is_known_cartesian_kspace_dimension(const CartesianKspaceDimension dimension) noexcept {
  return static_cast<std::size_t>(dimension) < ksj::viewer::kCartesianKspaceDimensionCount;
}

[[nodiscard]] bool is_display_axis(const CartesianKspaceAxes axes, const CartesianKspaceDimension dimension) noexcept {
  return axes.x == dimension || axes.y == dimension;
}

[[nodiscard]] int cartesian_header_dimension_value(const ksj::ismrmrd::AcquisitionHeader& header,
                                                   const CartesianKspaceDimension dimension) noexcept {
  switch (dimension) {
    case CartesianKspaceDimension::phase_encode:
      return static_cast<int>(header.index.kspace_encode_step_1);
    case CartesianKspaceDimension::encoding_space:
      return static_cast<int>(header.encoding_space_ref);
    case CartesianKspaceDimension::partition:
      return static_cast<int>(header.index.kspace_encode_step_2);
    case CartesianKspaceDimension::average:
      return static_cast<int>(header.index.average);
    case CartesianKspaceDimension::slice:
      return static_cast<int>(header.index.slice);
    case CartesianKspaceDimension::contrast:
      return static_cast<int>(header.index.contrast);
    case CartesianKspaceDimension::physiological_phase:
      return static_cast<int>(header.index.phase);
    case CartesianKspaceDimension::repetition:
      return static_cast<int>(header.index.repetition);
    case CartesianKspaceDimension::set:
      return static_cast<int>(header.index.set);
    case CartesianKspaceDimension::segment:
      return static_cast<int>(header.index.segment);
    case CartesianKspaceDimension::user_0:
    case CartesianKspaceDimension::user_1:
    case CartesianKspaceDimension::user_2:
    case CartesianKspaceDimension::user_3:
    case CartesianKspaceDimension::user_4:
    case CartesianKspaceDimension::user_5:
    case CartesianKspaceDimension::user_6:
    case CartesianKspaceDimension::user_7:
      return static_cast<int>(header.index.user.at(
        static_cast<std::size_t>(static_cast<int>(dimension) - static_cast<int>(CartesianKspaceDimension::user_0))));
    case CartesianKspaceDimension::readout:
    case CartesianKspaceDimension::coil:
    case CartesianKspaceDimension::count:
      return 0;
  }
  return 0;
}

[[nodiscard]] CartesianKspaceCoordinate
cartesian_header_coordinate(const ksj::ismrmrd::AcquisitionHeader& header) noexcept {
  CartesianKspaceCoordinate coordinate;
  for (const auto dimension : kCartesianKspaceDimensions) {
    if (dimension != CartesianKspaceDimension::readout && dimension != CartesianKspaceDimension::coil) {
      ksj::viewer::set_cartesian_kspace_coordinate_value(coordinate, dimension,
                                                         cartesian_header_dimension_value(header, dimension));
    }
  }
  return coordinate;
}

struct CartesianHeaderLimitBinding final {
  ksj::recon::EncodingLimitDimension dimension;
  const char* standard_name;
};

[[nodiscard]] constexpr std::optional<CartesianHeaderLimitBinding>
cartesian_header_limit_binding(const CartesianKspaceDimension dimension) noexcept {
  using EncodingLimitDimension = ksj::recon::EncodingLimitDimension;
  switch (dimension) {
    case CartesianKspaceDimension::phase_encode:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::kspace_encode_step_1, "kspace_encode_step_1"};
    case CartesianKspaceDimension::partition:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::kspace_encode_step_2, "kspace_encode_step_2"};
    case CartesianKspaceDimension::average:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::average, "average"};
    case CartesianKspaceDimension::slice:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::slice, "slice"};
    case CartesianKspaceDimension::contrast:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::contrast, "contrast"};
    case CartesianKspaceDimension::physiological_phase:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::phase, "phase"};
    case CartesianKspaceDimension::repetition:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::repetition, "repetition"};
    case CartesianKspaceDimension::set:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::set, "set"};
    case CartesianKspaceDimension::segment:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::segment, "segment"};
    case CartesianKspaceDimension::user_0:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::user_0, "user_0"};
    case CartesianKspaceDimension::user_1:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::user_1, "user_1"};
    case CartesianKspaceDimension::user_2:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::user_2, "user_2"};
    case CartesianKspaceDimension::user_3:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::user_3, "user_3"};
    case CartesianKspaceDimension::user_4:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::user_4, "user_4"};
    case CartesianKspaceDimension::user_5:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::user_5, "user_5"};
    case CartesianKspaceDimension::user_6:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::user_6, "user_6"};
    case CartesianKspaceDimension::user_7:
      return CartesianHeaderLimitBinding{EncodingLimitDimension::user_7, "user_7"};
    case CartesianKspaceDimension::readout:
    case CartesianKspaceDimension::coil:
    case CartesianKspaceDimension::encoding_space:
    case CartesianKspaceDimension::count:
      return std::nullopt;
  }
  return std::nullopt;
}

[[nodiscard]] bool cartesian_readout_range(const ksj::ismrmrd::AcquisitionHeader& header,
                                           std::int32_t& first_coordinate, std::int32_t& last_coordinate,
                                           QString& error) {
  const auto sample_count = static_cast<std::uint32_t>(header.number_of_samples);
  const auto discarded =
    static_cast<std::uint32_t>(header.discard_pre) + static_cast<std::uint32_t>(header.discard_post);
  if (sample_count == 0U || header.active_channels == 0U || discarded >= sample_count) {
    error = QStringLiteral("a Cartesian acquisition has no usable readout samples or active coils");
    return false;
  }
  if (header.center_sample >= header.number_of_samples) {
    error = QStringLiteral("a Cartesian acquisition center_sample is outside its readout");
    return false;
  }

  first_coordinate = static_cast<std::int32_t>(header.discard_pre) - static_cast<std::int32_t>(header.center_sample);
  last_coordinate = static_cast<std::int32_t>(sample_count - header.discard_post - 1U) -
                    static_cast<std::int32_t>(header.center_sample);
  return first_coordinate <= last_coordinate;
}

[[nodiscard]] bool cartesian_header_is_renderable(const ksj::ismrmrd::AcquisitionHeader& header,
                                                  const std::vector<ksj::recon::EncodingDescriptor>& encodings,
                                                  const CartesianKspaceAcquisitionKind acquisition_kind,
                                                  std::int32_t& readout_minimum, std::int32_t& readout_maximum,
                                                  QString& error) {
  if (header.trajectory_dimensions != 0U) {
    error = QStringLiteral("a selected Cartesian acquisition contains trajectory coordinates");
    return false;
  }
  const auto encoding_index = static_cast<std::size_t>(header.encoding_space_ref);
  if (encoding_index >= encodings.size()) {
    error = QStringLiteral("a Cartesian acquisition encoding_space_ref is not declared by the standard ISMRMRD XML");
    return false;
  }
  const auto& encoding = encodings.at(encoding_index);
  if (encoding.trajectory() != ksj::recon::TrajectoryType::cartesian) {
    error = QStringLiteral("a selected acquisition is not Cartesian in the standard ISMRMRD XML");
    return false;
  }
  if (cartesian_kspace_acquisition_kind_is_imaging(acquisition_kind)) {
    for (const auto dimension : kCartesianKspaceDimensions) {
      const auto binding = cartesian_header_limit_binding(dimension);
      if (!binding.has_value()) {
        continue;
      }
      const auto& limit = encoding.limits().at(binding->dimension);
      if (!limit.has_value()) {
        continue;
      }
      if (limit->minimum() > std::numeric_limits<std::uint16_t>::max() ||
          limit->maximum() > std::numeric_limits<std::uint16_t>::max()) {
        error = QStringLiteral("the standard ISMRMRD Cartesian %1 limit is outside the acquisition header range")
                  .arg(QLatin1String(binding->standard_name));
        return false;
      }
      const auto value = cartesian_header_dimension_value(header, dimension);
      if (value < static_cast<int>(limit->minimum()) || value > static_cast<int>(limit->maximum())) {
        error = QStringLiteral("a Cartesian acquisition %1 is outside the XML encoding limit")
                  .arg(QLatin1String(binding->standard_name));
        return false;
      }
    }
  }
  return cartesian_readout_range(header, readout_minimum, readout_maximum, error);
}

[[nodiscard]] bool append_observed_value(std::set<int>& values, const int value, QString& error) {
  values.insert(value);
  if (values.size() > kKspaceMaximumObservedValues) {
    error = QStringLiteral("a Cartesian K-space dimension exceeds the bounded observed-coordinate limit (%1)")
              .arg(static_cast<qulonglong>(kKspaceMaximumObservedValues));
    return false;
  }
  return true;
}

struct CartesianCatalogDiscovery final {
  CartesianKspaceCatalog catalog;
  CartesianKspaceAcquisitionKindSummary kind_summary;
  QString first_unrenderable_error;
};

[[nodiscard]] bool discover_cartesian_kspace_catalog(ksj::viewer::InspectionSession& session,
                                                     const std::vector<ksj::recon::EncodingDescriptor>& encodings,
                                                     const CartesianKspaceAcquisitionKind acquisition_kind,
                                                     CartesianCatalogDiscovery& discovery, QString& error) {
  discovery = {};
  error.clear();
  std::array<std::set<int>, ksj::viewer::kCartesianKspaceDimensionCount> observed_values;
  std::size_t source_complex_values = 0U;
  QString limit_error;
  std::string reader_error;
  const auto iteration = session.reader().for_each_acquisition_header(
    [&](const ksj::ismrmrd::InspectionAcquisitionHeaderRecord& record) {
      discovery.kind_summary.record(record.header);
      if (!cartesian_kspace_acquisition_kind_matches(record.header, acquisition_kind)) {
        return true;
      }
      std::int32_t readout_minimum = 0;
      std::int32_t readout_maximum = 0;
      QString candidate_error;
      if (!cartesian_header_is_renderable(record.header, encodings, acquisition_kind, readout_minimum, readout_maximum,
                                          candidate_error)) {
        if (discovery.first_unrenderable_error.isEmpty()) {
          discovery.first_unrenderable_error = candidate_error;
        }
        return true;
      }
      if (discovery.catalog.entries.size() >= static_cast<qsizetype>(kKspaceMaximumSourceLines)) {
        limit_error = QStringLiteral("the selected Cartesian K-space data exceeds the bounded acquisition line limit");
        return false;
      }
      const auto usable_samples = static_cast<std::size_t>(readout_maximum - readout_minimum + 1);
      std::size_t line_complex_values = 0U;
      std::size_t next_complex_values = 0U;
      if (!checked_multiply(usable_samples, static_cast<std::size_t>(record.header.active_channels),
                            line_complex_values) ||
          !checked_add(source_complex_values, line_complex_values, next_complex_values) ||
          next_complex_values > kKspaceMaximumSourceComplexValues) {
        limit_error =
          QStringLiteral("the selected Cartesian K-space data exceeds the bounded source complex-value limit");
        return false;
      }
      source_complex_values = next_complex_values;

      auto coordinate = cartesian_header_coordinate(record.header);
      for (std::int32_t value = readout_minimum; value <= readout_maximum; ++value) {
        if (!append_observed_value(
              observed_values.at(ksj::viewer::cartesian_kspace_dimension_index(CartesianKspaceDimension::readout)),
              value, limit_error)) {
          return false;
        }
      }
      for (std::uint16_t channel = 0U; channel < record.header.active_channels; ++channel) {
        if (!append_observed_value(
              observed_values.at(ksj::viewer::cartesian_kspace_dimension_index(CartesianKspaceDimension::coil)),
              static_cast<int>(channel), limit_error)) {
          return false;
        }
      }
      for (const auto dimension : kCartesianKspaceDimensions) {
        if (dimension == CartesianKspaceDimension::readout || dimension == CartesianKspaceDimension::coil) {
          continue;
        }
        if (!append_observed_value(observed_values.at(ksj::viewer::cartesian_kspace_dimension_index(dimension)),
                                   ksj::viewer::cartesian_kspace_coordinate_value(coordinate, dimension),
                                   limit_error)) {
          return false;
        }
      }
      discovery.catalog.entries.append({.coordinate = coordinate,
                                        .readout_minimum = readout_minimum,
                                        .readout_maximum = readout_maximum,
                                        .active_channel_count = record.header.active_channels,
                                        .source_ordinal = record.ordinal});
      return true;
    },
    reader_error);
  if (!limit_error.isEmpty()) {
    error = limit_error;
    return false;
  }
  if (iteration != ksj::ismrmrd::InspectionIterationResult::completed) {
    error = reader_error.empty() ? QStringLiteral("Cartesian K-space header discovery did not complete")
                                 : to_qstring(reader_error);
    return false;
  }
  if (discovery.catalog.entries.isEmpty()) {
    if (!discovery.first_unrenderable_error.isEmpty()) {
      error = QStringLiteral("the selected %1 data has no renderable Cartesian acquisition: %2")
                .arg(cartesian_kspace_acquisition_kind_label(acquisition_kind), discovery.first_unrenderable_error);
    } else if (discovery.kind_summary.count(acquisition_kind) == 0U) {
      error = QStringLiteral("the active standard ISMRMRD container has no %1 acquisitions")
                .arg(cartesian_kspace_acquisition_kind_label(acquisition_kind));
    } else {
      error = QStringLiteral("the selected %1 data has no renderable Cartesian acquisition")
                .arg(cartesian_kspace_acquisition_kind_label(acquisition_kind));
    }
    return false;
  }
  for (const auto dimension : kCartesianKspaceDimensions) {
    QList<int> values;
    values.reserve(
      static_cast<qsizetype>(observed_values.at(ksj::viewer::cartesian_kspace_dimension_index(dimension)).size()));
    for (const auto value : observed_values.at(ksj::viewer::cartesian_kspace_dimension_index(dimension))) {
      values.append(value);
    }
    discovery.catalog.dimensions.append({.dimension = dimension, .observed_values = std::move(values)});
  }
  discovery.catalog.initial_coordinate = discovery.catalog.entries.front().coordinate;
  ksj::viewer::set_cartesian_kspace_coordinate_value(discovery.catalog.initial_coordinate,
                                                     CartesianKspaceDimension::readout,
                                                     discovery.catalog.entries.front().readout_minimum);
  ksj::viewer::set_cartesian_kspace_coordinate_value(discovery.catalog.initial_coordinate,
                                                     CartesianKspaceDimension::coil, 0);
  discovery.catalog.matching_acquisition_count = discovery.kind_summary.count(acquisition_kind);
  return true;
}

[[nodiscard]] std::size_t display_bin_for_source_coordinate(const std::size_t source_offset,
                                                            const std::size_t source_count, const int display_count) {
  if (source_count <= 1U || display_count <= 1) {
    return 0U;
  }
  const auto result = source_offset * static_cast<std::size_t>(display_count) / source_count;
  return std::min(result, static_cast<std::size_t>(display_count - 1));
}

[[nodiscard]] const QList<int>* catalog_dimension_values(const CartesianKspaceCatalog& catalog,
                                                         const CartesianKspaceDimension dimension) {
  const auto found =
    std::find_if(catalog.dimensions.cbegin(), catalog.dimensions.cend(), [dimension](const auto& item) {
      return item.dimension == dimension;
    });
  return found == catalog.dimensions.cend() ? nullptr : &found->observed_values;
}

[[nodiscard]] bool entry_supports_coordinate(const CartesianKspaceCatalogEntry& entry, const CartesianKspaceAxes axes,
                                             const CartesianKspaceCoordinate& requested) noexcept {
  for (const auto dimension : kCartesianKspaceDimensions) {
    if (is_display_axis(axes, dimension)) {
      continue;
    }
    const auto value = ksj::viewer::cartesian_kspace_coordinate_value(requested, dimension);
    if (dimension == CartesianKspaceDimension::readout) {
      if (value < entry.readout_minimum || value > entry.readout_maximum) {
        return false;
      }
    } else if (dimension == CartesianKspaceDimension::coil) {
      if (value < 0 || value >= static_cast<int>(entry.active_channel_count)) {
        return false;
      }
    } else if (ksj::viewer::cartesian_kspace_coordinate_value(entry.coordinate, dimension) != value) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool entry_supports_dimension_value(const CartesianKspaceCatalogEntry& entry,
                                                  const CartesianKspaceDimension dimension, const int value) noexcept {
  if (dimension == CartesianKspaceDimension::readout) {
    return value >= entry.readout_minimum && value <= entry.readout_maximum;
  }
  if (dimension == CartesianKspaceDimension::coil) {
    return value >= 0 && value < static_cast<int>(entry.active_channel_count);
  }
  return ksj::viewer::cartesian_kspace_coordinate_value(entry.coordinate, dimension) == value;
}

[[nodiscard]] CartesianKspaceCoordinate resolved_coordinate_from_entry(const CartesianKspaceCatalogEntry& entry,
                                                                       const CartesianKspaceAxes axes,
                                                                       CartesianKspaceCoordinate requested) {
  for (const auto dimension : kCartesianKspaceDimensions) {
    if (is_display_axis(axes, dimension)) {
      continue;
    }
    if (dimension == CartesianKspaceDimension::readout) {
      const auto requested_value = ksj::viewer::cartesian_kspace_coordinate_value(requested, dimension);
      if (requested_value < entry.readout_minimum || requested_value > entry.readout_maximum) {
        ksj::viewer::set_cartesian_kspace_coordinate_value(requested, dimension, entry.readout_minimum);
      }
    } else if (dimension == CartesianKspaceDimension::coil) {
      const auto requested_value = ksj::viewer::cartesian_kspace_coordinate_value(requested, dimension);
      if (requested_value < 0 || requested_value >= static_cast<int>(entry.active_channel_count)) {
        ksj::viewer::set_cartesian_kspace_coordinate_value(requested, dimension, 0);
      }
    } else {
      ksj::viewer::set_cartesian_kspace_coordinate_value(
        requested, dimension, ksj::viewer::cartesian_kspace_coordinate_value(entry.coordinate, dimension));
    }
  }
  return requested;
}

[[nodiscard]] QJsonObject cartesian_coordinate_json(const CartesianKspaceCoordinate& coordinate,
                                                    const CartesianKspaceAxes axes, const bool include_axes) {
  QJsonObject result;
  for (const auto dimension : kCartesianKspaceDimensions) {
    if (!include_axes && is_display_axis(axes, dimension)) {
      continue;
    }
    result.insert(ksj::viewer::cartesian_kspace_dimension_identifier(dimension),
                  is_display_axis(axes, dimension)
                    ? QJsonValue(QStringLiteral(":"))
                    : QJsonValue(ksj::viewer::cartesian_kspace_coordinate_value(coordinate, dimension)));
  }
  return result;
}

[[nodiscard]] QJsonArray axis_values_json(const std::vector<int>& values) {
  QJsonArray result;
  for (const auto value : values) {
    result.append(value);
  }
  return result;
}

[[nodiscard]] const char* image_data_type_name(const ImageDataType data_type) noexcept {
  switch (data_type) {
    case ImageDataType::unsigned_integer_16:
      return "uint16";
    case ImageDataType::signed_integer_16:
      return "int16";
    case ImageDataType::unsigned_integer_32:
      return "uint32";
    case ImageDataType::signed_integer_32:
      return "int32";
    case ImageDataType::real_32:
      return "float32";
    case ImageDataType::real_64:
      return "float64";
    case ImageDataType::complex_32:
      return "complex64";
    case ImageDataType::complex_64:
      return "complex128";
  }
  return "unknown";
}

[[nodiscard]] bool image_data_type_is_complex(const ImageDataType data_type) noexcept {
  return data_type == ImageDataType::complex_32 || data_type == ImageDataType::complex_64;
}

[[nodiscard]] std::size_t image_value_bytes(const ImageDataType data_type) noexcept {
  switch (data_type) {
    case ImageDataType::unsigned_integer_16:
      return sizeof(std::uint16_t);
    case ImageDataType::signed_integer_16:
      return sizeof(std::int16_t);
    case ImageDataType::unsigned_integer_32:
      return sizeof(std::uint32_t);
    case ImageDataType::signed_integer_32:
      return sizeof(std::int32_t);
    case ImageDataType::real_32:
      return sizeof(float);
    case ImageDataType::real_64:
      return sizeof(double);
    case ImageDataType::complex_32:
      return sizeof(std::complex<float>);
    case ImageDataType::complex_64:
      return sizeof(std::complex<double>);
  }
  return 0U;
}

template <typename Value> [[nodiscard]] Value read_image_value(const ImagePixelsView& view, const std::size_t index) {
  Value value{};
  std::memcpy(&value, view.pixels.data() + index * sizeof(Value), sizeof(Value));
  return value;
}

struct ImageSampleComponents final {
  double magnitude{0.0};
  double real{0.0};
  double imaginary{0.0};
  double phase{0.0};
};

[[nodiscard]] ImageSampleComponents image_sample_components(const ImagePixelsView& view, const std::size_t index) {
  const auto from_real_value = [](const double value) {
    return ImageSampleComponents{.magnitude = std::abs(value), .real = value, .imaginary = 0.0, .phase = 0.0};
  };
  const auto from_complex_value = [](const auto value) {
    const auto real = static_cast<double>(value.real());
    const auto imaginary = static_cast<double>(value.imag());
    return ImageSampleComponents{.magnitude = static_cast<double>(std::abs(value)),
                                 .real = real,
                                 .imaginary = imaginary,
                                 .phase = std::atan2(imaginary, real)};
  };

  switch (view.data_type) {
    case ImageDataType::unsigned_integer_16:
      return from_real_value(static_cast<double>(read_image_value<std::uint16_t>(view, index)));
    case ImageDataType::signed_integer_16:
      return from_real_value(static_cast<double>(read_image_value<std::int16_t>(view, index)));
    case ImageDataType::unsigned_integer_32:
      return from_real_value(static_cast<double>(read_image_value<std::uint32_t>(view, index)));
    case ImageDataType::signed_integer_32:
      return from_real_value(static_cast<double>(read_image_value<std::int32_t>(view, index)));
    case ImageDataType::real_32:
      return from_real_value(static_cast<double>(read_image_value<float>(view, index)));
    case ImageDataType::real_64:
      return from_real_value(read_image_value<double>(view, index));
    case ImageDataType::complex_32:
      return from_complex_value(read_image_value<std::complex<float>>(view, index));
    case ImageDataType::complex_64:
      return from_complex_value(read_image_value<std::complex<double>>(view, index));
  }
  return {};
}

[[nodiscard]] QJsonArray dimensions_to_json(const std::array<std::uint16_t, 4>& dimensions) {
  QJsonArray result;
  for (const auto dimension : dimensions) {
    result.append(static_cast<int>(dimension));
  }
  return result;
}

constexpr std::array<ksj::viewer::ImageDimension, ksj::viewer::kImageDimensionCount> kImageDimensions{
  ksj::viewer::ImageDimension::x,
  ksj::viewer::ImageDimension::y,
  ksj::viewer::ImageDimension::z,
  ksj::viewer::ImageDimension::channel,
};

[[nodiscard]] bool image_axis_is_selected(const ksj::viewer::ImageAxes axes,
                                          const ksj::viewer::ImageDimension dimension) noexcept {
  return axes.x == dimension || axes.y == dimension;
}

[[nodiscard]] bool image_axes_are_valid(const ksj::viewer::ImageAxes axes) noexcept {
  return axes.x != axes.y && ksj::viewer::image_dimension_index(axes.x) < ksj::viewer::kImageDimensionCount &&
         ksj::viewer::image_dimension_index(axes.y) < ksj::viewer::kImageDimensionCount;
}

[[nodiscard]] QString image_dimension_identifier_text(const ksj::viewer::ImageDimension dimension) {
  switch (dimension) {
    case ksj::viewer::ImageDimension::x:
      return QStringLiteral("x");
    case ksj::viewer::ImageDimension::y:
      return QStringLiteral("y");
    case ksj::viewer::ImageDimension::z:
      return QStringLiteral("z");
    case ksj::viewer::ImageDimension::channel:
      return QStringLiteral("channel");
    case ksj::viewer::ImageDimension::count:
      break;
  }
  return {};
}

[[nodiscard]] std::size_t image_linear_index(const std::array<std::uint16_t, 4>& dimensions,
                                             const ksj::viewer::ImageCoordinate& coordinate) {
  const auto x =
    static_cast<std::size_t>(ksj::viewer::image_coordinate_value(coordinate, ksj::viewer::ImageDimension::x));
  const auto y =
    static_cast<std::size_t>(ksj::viewer::image_coordinate_value(coordinate, ksj::viewer::ImageDimension::y));
  const auto z =
    static_cast<std::size_t>(ksj::viewer::image_coordinate_value(coordinate, ksj::viewer::ImageDimension::z));
  const auto channel =
    static_cast<std::size_t>(ksj::viewer::image_coordinate_value(coordinate, ksj::viewer::ImageDimension::channel));
  return x + static_cast<std::size_t>(dimensions[0]) *
               (y + static_cast<std::size_t>(dimensions[1]) * (z + static_cast<std::size_t>(dimensions[2]) * channel));
}

[[nodiscard]] QJsonObject image_coordinate_json(const ksj::viewer::ImageCoordinate& coordinate,
                                                const ksj::viewer::ImageAxes axes, const bool include_axes) {
  QJsonObject result;
  for (const auto dimension : kImageDimensions) {
    if (!include_axes && image_axis_is_selected(axes, dimension)) {
      continue;
    }
    result.insert(image_dimension_identifier_text(dimension),
                  include_axes && image_axis_is_selected(axes, dimension)
                    ? QJsonValue(QStringLiteral(":"))
                    : QJsonValue(static_cast<int>(ksj::viewer::image_coordinate_value(coordinate, dimension))));
  }
  return result;
}

[[nodiscard]] QString image_fixed_coordinates_text(const ksj::viewer::ImageCoordinate& coordinate,
                                                   const ksj::viewer::ImageAxes axes) {
  QStringList values;
  for (const auto dimension : kImageDimensions) {
    if (!image_axis_is_selected(axes, dimension)) {
      values.append(QStringLiteral("%1=%2")
                      .arg(image_dimension_identifier_text(dimension))
                      .arg(ksj::viewer::image_coordinate_value(coordinate, dimension)));
    }
  }
  return values.isEmpty() ? QStringLiteral("none") : values.join(QStringLiteral(", "));
}

[[nodiscard]] QJsonObject image_details(const ksj::ismrmrd::InspectionImageRecord& record,
                                        const std::array<std::uint16_t, 4>& dimensions,
                                        const ksj::viewer::ImageAxes axes,
                                        const ksj::viewer::ImageCoordinate& coordinate,
                                        const ksj::viewer::ArrShowDisplayComponent component, const QString& source) {
  QJsonObject result;
  result.insert(QStringLiteral("artifact_kind"), QStringLiteral("visualization-derivative"));
  result.insert(QStringLiteral("view"), QStringLiteral("image"));
  result.insert(QStringLiteral("source"), source);
  result.insert(QStringLiteral("series_id"), to_qstring(record.locator.series_id));
  result.insert(QStringLiteral("ordinal"), static_cast<int>(record.locator.ordinal));
  result.insert(QStringLiteral("data_type"), QString::fromLatin1(image_data_type_name(record.header.data_type)));
  result.insert(QStringLiteral("display_component"), ksj::viewer::arrshow_display_component_identifier(component));
  result.insert(QStringLiteral("component_semantics"), ksj::viewer::arrshow_display_component_semantics(component));
  result.insert(QStringLiteral("dimensions"), dimensions_to_json(dimensions));
  result.insert(QStringLiteral("axis_x"), image_dimension_identifier_text(axes.x));
  result.insert(QStringLiteral("axis_y"), image_dimension_identifier_text(axes.y));
  result.insert(QStringLiteral("plane_coordinates"), image_coordinate_json(coordinate, axes, true));
  result.insert(QStringLiteral("fixed_coordinates"), image_coordinate_json(coordinate, axes, false));
  result.insert(QStringLiteral("image_index"), static_cast<int>(record.header.image_index));
  result.insert(QStringLiteral("image_series_index"), static_cast<int>(record.header.image_series_index));

  QJsonArray attributes;
  for (const auto& attribute : record.meta_attributes) {
    QJsonObject item;
    item.insert(QStringLiteral("name"), to_qstring(attribute.name));
    QJsonArray values;
    for (const auto& value : attribute.values) {
      values.append(to_qstring(value));
    }
    item.insert(QStringLiteral("values"), values);
    attributes.append(item);
  }
  result.insert(QStringLiteral("meta_attributes"), attributes);
  return result;
}

[[nodiscard]] QString image_summary(const ksj::ismrmrd::InspectionImageRecord& record,
                                    const std::array<std::uint16_t, 4>& dimensions, const ksj::viewer::ImageAxes axes,
                                    const ksj::viewer::ImageCoordinate& coordinate,
                                    const ksj::viewer::ArrShowDisplayComponent component) {
  QStringList lines;
  lines << QStringLiteral("ISMRMRD image: series %1, storage ordinal %2")
             .arg(to_qstring(record.locator.series_id))
             .arg(record.locator.ordinal)
        << QStringLiteral("Type: %1; dimensions [x, y, z, channel] = [%2, %3, %4, %5]")
             .arg(QString::fromLatin1(image_data_type_name(record.header.data_type)))
             .arg(dimensions[0])
             .arg(dimensions[1])
             .arg(dimensions[2])
             .arg(dimensions[3])
        << QStringLiteral("Displayed plane: x=%1, y=%2; fixed coordinates: %3; image index=%4, series index=%5")
             .arg(image_dimension_identifier_text(axes.x), image_dimension_identifier_text(axes.y),
                  image_fixed_coordinates_text(coordinate, axes))
             .arg(record.header.image_index)
             .arg(record.header.image_series_index)
        << QStringLiteral("Display component: %1; %2.")
             .arg(ksj::viewer::arrshow_display_component_label(component),
                  ksj::viewer::arrshow_display_component_semantics(component));
  if (record.meta_attributes.empty()) {
    lines << QStringLiteral("MetaAttributes: none");
  } else {
    lines << QStringLiteral("MetaAttributes:");
    for (const auto& attribute : record.meta_attributes) {
      QStringList values;
      for (const auto& value : attribute.values) {
        values << preview(to_qstring(value), kMaximumMetadataValuePreviewCharacters);
      }
      lines << QStringLiteral("  %1 = %2").arg(to_qstring(attribute.name), values.join(QStringLiteral(" | ")));
    }
  }
  return lines.join(QLatin1Char('\n'));
}

[[nodiscard]] QString parameter_type_name(const ksj::recon::graph::PipelineParameterType type) {
  using ksj::recon::graph::PipelineParameterType;
  switch (type) {
    case PipelineParameterType::boolean:
      return QStringLiteral("boolean");
    case PipelineParameterType::integer:
      return QStringLiteral("integer");
    case PipelineParameterType::string:
      return QStringLiteral("string");
    case PipelineParameterType::enumeration:
      return QStringLiteral("enum");
  }
  return QStringLiteral("unknown");
}

[[nodiscard]] QString pipeline_graph_node_key(const QString& prefix, const std::string_view id) {
  return prefix + QLatin1Char(':') + to_qstring(id);
}

[[nodiscard]] QString pipeline_graph_node_kind_name(const ksj::viewer::PipelineGraphNodeKind kind) {
  using ksj::viewer::PipelineGraphNodeKind;
  switch (kind) {
    case PipelineGraphNodeKind::ingress:
      return QStringLiteral("ingress");
    case PipelineGraphNodeKind::operator_node:
      return QStringLiteral("operator");
    case PipelineGraphNodeKind::egress:
      return QStringLiteral("egress");
  }
  return QStringLiteral("unknown");
}

[[nodiscard]] QString pipeline_graph_edge_kind_name(const ksj::viewer::PipelineGraphEdgeKind kind) {
  using ksj::viewer::PipelineGraphEdgeKind;
  switch (kind) {
    case PipelineGraphEdgeKind::ingress:
      return QStringLiteral("ingress");
    case PipelineGraphEdgeKind::data:
      return QStringLiteral("data");
    case PipelineGraphEdgeKind::egress:
      return QStringLiteral("egress");
    case PipelineGraphEdgeKind::calibration:
      return QStringLiteral("calibration");
  }
  return QStringLiteral("unknown");
}

} // namespace

namespace ksj::viewer {

bool image_arrshow_component_supported(const ArrShowDisplayComponent component,
                                       const ksj::ismrmrd::ImageDataType data_type) noexcept {
  switch (component) {
    case ArrShowDisplayComponent::magnitude:
    case ArrShowDisplayComponent::real:
    case ArrShowDisplayComponent::imaginary:
    case ArrShowDisplayComponent::complex:
    case ArrShowDisplayComponent::phase:
      return !arrshow_display_component_requires_complex(component) || image_data_type_is_complex(data_type);
  }
  return false;
}

QString image_dimension_identifier(const ImageDimension dimension) {
  return image_dimension_identifier_text(dimension);
}

MetadataPresentation make_metadata_presentation(const InspectionSession& session) {
  MetadataPresentation presentation;
  if (!session.is_open()) {
    presentation.summary = QStringLiteral("No standard ISMRMRD dataset is open.");
    presentation.xml_preview = QStringLiteral("Open a .mrd file to inspect its standard XML header.");
    return presentation;
  }

  const auto& metadata = session.metadata();
  presentation.summary = QStringLiteral("Source: %1\nAcquisitions: %2\nImage series: %3")
                           .arg(source_description(session))
                           .arg(metadata.acquisition_count)
                           .arg(metadata.image_series.size());
  presentation.xml_preview = preview(to_qstring(metadata.xml_header), kMaximumXmlPreviewCharacters);
  presentation.csv_columns = {QStringLiteral("series_id"), QStringLiteral("image_count")};

  QJsonArray image_series;
  for (const auto& series : metadata.image_series) {
    const auto series_id = to_qstring(series.series_id);
    presentation.csv_rows.append({series_id, QString::number(series.image_count)});
    QJsonObject item;
    item.insert(QStringLiteral("series_id"), series_id);
    item.insert(QStringLiteral("image_count"), static_cast<int>(series.image_count));
    image_series.append(item);
  }

  presentation.details.insert(QStringLiteral("artifact_kind"), QStringLiteral("visualization-derivative"));
  presentation.details.insert(QStringLiteral("view"), QStringLiteral("metadata"));
  presentation.details.insert(QStringLiteral("source"), source_description(session));
  presentation.details.insert(QStringLiteral("container_path"), to_qstring(metadata.group));
  presentation.details.insert(QStringLiteral("acquisition_count"), static_cast<int>(metadata.acquisition_count));
  presentation.details.insert(QStringLiteral("image_series"), image_series);
  presentation.details.insert(QStringLiteral("xml_preview"), presentation.xml_preview);
  return presentation;
}

QString cartesian_kspace_dimension_identifier(const CartesianKspaceDimension dimension) {
  switch (dimension) {
    case CartesianKspaceDimension::readout:
      return QStringLiteral("readout");
    case CartesianKspaceDimension::phase_encode:
      return QStringLiteral("phase-encode");
    case CartesianKspaceDimension::coil:
      return QStringLiteral("coil");
    case CartesianKspaceDimension::encoding_space:
      return QStringLiteral("encoding-space");
    case CartesianKspaceDimension::partition:
      return QStringLiteral("partition");
    case CartesianKspaceDimension::average:
      return QStringLiteral("average");
    case CartesianKspaceDimension::slice:
      return QStringLiteral("slice");
    case CartesianKspaceDimension::contrast:
      return QStringLiteral("contrast");
    case CartesianKspaceDimension::physiological_phase:
      return QStringLiteral("physiological-phase");
    case CartesianKspaceDimension::repetition:
      return QStringLiteral("repetition");
    case CartesianKspaceDimension::set:
      return QStringLiteral("set");
    case CartesianKspaceDimension::segment:
      return QStringLiteral("segment");
    case CartesianKspaceDimension::user_0:
    case CartesianKspaceDimension::user_1:
    case CartesianKspaceDimension::user_2:
    case CartesianKspaceDimension::user_3:
    case CartesianKspaceDimension::user_4:
    case CartesianKspaceDimension::user_5:
    case CartesianKspaceDimension::user_6:
    case CartesianKspaceDimension::user_7:
      return QStringLiteral("user-%1").arg(static_cast<int>(dimension) -
                                           static_cast<int>(CartesianKspaceDimension::user_0));
    case CartesianKspaceDimension::count:
      break;
  }
  return {};
}

bool cartesian_kspace_catalog(InspectionSession& session, const CartesianKspaceAcquisitionKind acquisition_kind,
                              CartesianKspaceCatalog& catalog, QString& error) {
  catalog = {};
  error.clear();
  if (!session.is_open()) {
    error = QStringLiteral("open a standard ISMRMRD dataset before inspecting Cartesian K-space");
    return false;
  }
  const auto& metadata = session.metadata();
  if (metadata.acquisition_count == 0U) {
    error = QStringLiteral("the active standard ISMRMRD container has no acquisitions");
    return false;
  }
  const auto scan = ksj::recon::ScanDescriptor::parse_ismrmrd_xml(metadata.xml_header);
  if (!scan.ok()) {
    error = QStringLiteral("the standard ISMRMRD XML header cannot describe a Cartesian K-space view: %1")
              .arg(to_qstring(scan.status().message()));
    return false;
  }
  CartesianCatalogDiscovery discovery;
  if (!discover_cartesian_kspace_catalog(session, scan.value().encodings(), acquisition_kind, discovery, error)) {
    return false;
  }
  catalog = std::move(discovery.catalog);
  return true;
}

bool cartesian_kspace_acquisition_kind_options(InspectionSession& session,
                                               QList<CartesianKspaceAcquisitionKindOption>& options, QString& error) {
  options.clear();
  error.clear();
  if (!session.is_open()) {
    error = QStringLiteral("open a standard ISMRMRD dataset before inspecting Cartesian K-space");
    return false;
  }
  CartesianKspaceAcquisitionKindSummary summary;
  std::string reader_error;
  const auto iteration = session.reader().for_each_acquisition_header(
    [&summary](const ksj::ismrmrd::InspectionAcquisitionHeaderRecord& record) {
      summary.record(record.header);
      return true;
    },
    reader_error);
  if (iteration != ksj::ismrmrd::InspectionIterationResult::completed) {
    error = reader_error.empty() ? QStringLiteral("Cartesian K-space acquisition-type discovery did not complete")
                                 : to_qstring(reader_error);
    return false;
  }
  for (std::size_t index = 0U; index < static_cast<std::size_t>(CartesianKspaceAcquisitionKind::count); ++index) {
    const auto kind = static_cast<CartesianKspaceAcquisitionKind>(index);
    if (summary.count(kind) == 0U) {
      continue;
    }
    CartesianKspaceCatalog catalog;
    QString catalog_error;
    if (!cartesian_kspace_catalog(session, kind, catalog, catalog_error)) {
      continue;
    }
    options.append({.kind = kind,
                    .label = cartesian_kspace_acquisition_kind_label(kind),
                    .matching_acquisition_count = summary.count(kind)});
  }
  if (options.isEmpty()) {
    error = QStringLiteral("the active standard ISMRMRD container has no renderable Cartesian acquisition data");
    return false;
  }
  return true;
}

bool resolve_cartesian_kspace_coordinate(const CartesianKspaceCatalog& catalog, const CartesianKspaceAxes axes,
                                         const CartesianKspaceCoordinate requested,
                                         const std::optional<CartesianKspaceDimension> changed_dimension,
                                         CartesianKspaceCoordinate& resolved, QString& error) {
  resolved = {};
  error.clear();
  if (!is_known_cartesian_kspace_dimension(axes.x) || !is_known_cartesian_kspace_dimension(axes.y)) {
    error = QStringLiteral("choose known Cartesian K-space display dimensions");
    return false;
  }
  if (axes.x == axes.y) {
    error = QStringLiteral("choose two distinct Cartesian K-space display dimensions");
    return false;
  }
  if (catalog.entries.isEmpty()) {
    error = QStringLiteral("the Cartesian K-space coordinate catalog is empty");
    return false;
  }
  if (changed_dimension.has_value()) {
    if (is_display_axis(axes, changed_dimension.value())) {
      error = QStringLiteral("a display axis uses ':' and cannot be selected as a fixed coordinate");
      return false;
    }
    const auto* values = catalog_dimension_values(catalog, changed_dimension.value());
    const auto requested_value = cartesian_kspace_coordinate_value(requested, changed_dimension.value());
    if (values == nullptr || !values->contains(requested_value)) {
      error = QStringLiteral("the requested Cartesian K-space coordinate is not observed in this acquisition type");
      return false;
    }
  }

  const CartesianKspaceCatalogEntry* selected = nullptr;
  for (const auto& entry : catalog.entries) {
    if (entry_supports_coordinate(entry, axes, requested)) {
      selected = &entry;
      break;
    }
  }
  if (selected == nullptr) {
    std::size_t best_similarity = 0U;
    for (const auto& entry : catalog.entries) {
      if (changed_dimension.has_value() &&
          !entry_supports_dimension_value(entry, changed_dimension.value(),
                                          cartesian_kspace_coordinate_value(requested, changed_dimension.value()))) {
        continue;
      }
      std::size_t similarity = 0U;
      for (const auto dimension : kCartesianKspaceDimensions) {
        if (is_display_axis(axes, dimension)) {
          continue;
        }
        if (entry_supports_dimension_value(entry, dimension, cartesian_kspace_coordinate_value(requested, dimension))) {
          ++similarity;
        }
      }
      if (selected == nullptr || similarity > best_similarity) {
        selected = &entry;
        best_similarity = similarity;
      }
    }
  }
  if (selected == nullptr) {
    error = QStringLiteral("no observed Cartesian K-space acquisition supports the requested sparse coordinate");
    return false;
  }
  resolved = resolved_coordinate_from_entry(*selected, axes, requested);
  return true;
}

bool make_cartesian_kspace_presentation(InspectionSession& session, const CartesianKspaceRequest request,
                                        KspacePresentation& presentation, QString& error) {
  presentation = {};
  error.clear();
  if (!is_known_cartesian_kspace_dimension(request.axes.x) || !is_known_cartesian_kspace_dimension(request.axes.y)) {
    error = QStringLiteral("choose known Cartesian K-space display dimensions");
    return false;
  }
  if (request.axes.x == request.axes.y) {
    error = QStringLiteral("choose two distinct Cartesian K-space display dimensions");
    return false;
  }
  CartesianKspaceCatalog catalog;
  if (!cartesian_kspace_catalog(session, request.acquisition_kind, catalog, error)) {
    return false;
  }
  CartesianKspaceCoordinate coordinate;
  if (!resolve_cartesian_kspace_coordinate(catalog, request.axes, request.coordinate, std::nullopt, coordinate,
                                           error)) {
    return false;
  }
  const auto scan = ksj::recon::ScanDescriptor::parse_ismrmrd_xml(session.metadata().xml_header);
  if (!scan.ok()) {
    error = QStringLiteral("the standard ISMRMRD XML header cannot describe a Cartesian K-space view: %1")
              .arg(to_qstring(scan.status().message()));
    return false;
  }

  std::vector<const CartesianKspaceCatalogEntry*> lines;
  lines.reserve(static_cast<std::size_t>(catalog.entries.size()));
  std::set<int> axis_x_values_set;
  std::set<int> axis_y_values_set;
  const auto append_axis_values = [&error, &request, &scan](std::set<int>& values,
                                                            const CartesianKspaceCatalogEntry& entry,
                                                            const CartesianKspaceDimension dimension) {
    const auto append = [&values, &error](const int value) {
      values.insert(value);
      if (values.size() > kKspaceMaximumObservedValues) {
        error = QStringLiteral("a Cartesian K-space display axis exceeds the bounded observed-coordinate limit (%1)")
                  .arg(static_cast<qulonglong>(kKspaceMaximumObservedValues));
        return false;
      }
      return true;
    };
    if (dimension == CartesianKspaceDimension::readout) {
      for (std::int32_t value = entry.readout_minimum; value <= entry.readout_maximum; ++value) {
        if (!append(value)) {
          return false;
        }
      }
      return true;
    }
    if (dimension == CartesianKspaceDimension::coil) {
      for (std::uint16_t channel = 0U; channel < entry.active_channel_count; ++channel) {
        if (!append(static_cast<int>(channel))) {
          return false;
        }
      }
      return true;
    }
    if (dimension == CartesianKspaceDimension::phase_encode &&
        cartesian_kspace_acquisition_kind_is_imaging(request.acquisition_kind)) {
      const auto encoding_index = static_cast<std::size_t>(
        cartesian_kspace_coordinate_value(entry.coordinate, CartesianKspaceDimension::encoding_space));
      if (encoding_index >= scan.value().encodings().size()) {
        error =
          QStringLiteral("a Cartesian acquisition encoding_space_ref is not declared by the standard ISMRMRD XML");
        return false;
      }
      const auto& limit = scan.value()
                            .encodings()
                            .at(encoding_index)
                            .limits()
                            .at(ksj::recon::EncodingLimitDimension::kspace_encode_step_1);
      if (limit.has_value()) {
        if (limit->minimum() > std::numeric_limits<std::uint16_t>::max() ||
            limit->maximum() > std::numeric_limits<std::uint16_t>::max()) {
          error = QStringLiteral("the standard ISMRMRD Cartesian kspace_encode_step_1 limit is outside the acquisition "
                                 "header range");
          return false;
        }
        const auto minimum = static_cast<int>(limit->minimum());
        const auto maximum = static_cast<int>(limit->maximum());
        for (auto value = minimum; value <= maximum; ++value) {
          if (!append(value)) {
            return false;
          }
        }
        return true;
      }
    }
    return append(cartesian_kspace_coordinate_value(entry.coordinate, dimension));
  };
  for (const auto& entry : catalog.entries) {
    if (!entry_supports_coordinate(entry, request.axes, coordinate)) {
      continue;
    }
    lines.push_back(&entry);
    if (!append_axis_values(axis_x_values_set, entry, request.axes.x) ||
        !append_axis_values(axis_y_values_set, entry, request.axes.y)) {
      return false;
    }
  }
  const auto expand_phase_encode_axis = [&error](std::set<int>& values) {
    if (values.empty()) {
      return true;
    }
    const auto minimum = *values.cbegin();
    const auto maximum = *values.crbegin();
    const auto extent = static_cast<std::size_t>(maximum - minimum) + 1U;
    if (extent > kKspaceMaximumObservedValues) {
      error = QStringLiteral("a Cartesian K-space phase-encode display axis exceeds the bounded coordinate limit (%1)")
                .arg(static_cast<qulonglong>(kKspaceMaximumObservedValues));
      return false;
    }
    for (auto value = minimum; value <= maximum; ++value) {
      values.insert(value);
    }
    return true;
  };
  if ((request.axes.x == CartesianKspaceDimension::phase_encode && !expand_phase_encode_axis(axis_x_values_set)) ||
      (request.axes.y == CartesianKspaceDimension::phase_encode && !expand_phase_encode_axis(axis_y_values_set))) {
    return false;
  }
  if (lines.empty() || axis_x_values_set.empty() || axis_y_values_set.empty()) {
    error = QStringLiteral("no Cartesian acquisitions match the selected sparse coordinate and display axes");
    return false;
  }
  const std::vector<int> axis_x_values(axis_x_values_set.cbegin(), axis_x_values_set.cend());
  const std::vector<int> axis_y_values(axis_y_values_set.cbegin(), axis_y_values_set.cend());
  const auto extent =
    make_display_extent(axis_x_values.size(), axis_y_values.size(), kKspaceMaximumDimension, kKspaceMaximumPixels);
  if (!extent.has_value()) {
    error = QStringLiteral("the Cartesian K-space plane cannot produce a bounded display extent");
    return false;
  }
  std::size_t display_cells = 0U;
  if (!checked_multiply(static_cast<std::size_t>(extent->width), static_cast<std::size_t>(extent->height),
                        display_cells)) {
    error = QStringLiteral("the Cartesian K-space display cell count overflows");
    return false;
  }
  std::vector<double> real_sums(display_cells, 0.0);
  std::vector<double> imaginary_sums(display_cells, 0.0);
  std::vector<std::uint32_t> contribution_counts(display_cells, 0U);
  const auto axis_source_index = [](const std::vector<int>& values, const int value) -> std::optional<std::size_t> {
    const auto found = std::lower_bound(values.cbegin(), values.cend(), value);
    return found == values.cend() || *found != value
             ? std::nullopt
             : std::optional<std::size_t>{static_cast<std::size_t>(std::distance(values.cbegin(), found))};
  };
  const auto sample_dimension_value = [](const ksj::ismrmrd::AcquisitionHeader& header,
                                         const CartesianKspaceDimension dimension, const int readout, const int coil) {
    if (dimension == CartesianKspaceDimension::readout) {
      return readout;
    }
    if (dimension == CartesianKspaceDimension::coil) {
      return coil;
    }
    return cartesian_header_dimension_value(header, dimension);
  };
  std::size_t source_complex_values = 0U;
  QString payload_error;
  std::string reader_error;
  for (const auto* line : lines) {
    reader_error.clear();
    const auto payload_iteration = session.reader().visit_acquisition(
      line->source_ordinal,
      [&](const ksj::ismrmrd::InspectionAcquisitionView& acquisition) {
        std::int32_t readout_minimum = 0;
        std::int32_t readout_maximum = 0;
        QString header_error;
        if (!cartesian_kspace_acquisition_kind_matches(acquisition.header, request.acquisition_kind) ||
            !cartesian_header_is_renderable(acquisition.header, scan.value().encodings(), request.acquisition_kind,
                                            readout_minimum, readout_maximum, header_error) ||
            cartesian_header_coordinate(acquisition.header) != line->coordinate ||
            readout_minimum != line->readout_minimum || readout_maximum != line->readout_maximum ||
            acquisition.header.active_channels != line->active_channel_count ||
            !entry_supports_coordinate(*line, request.axes, coordinate)) {
          payload_error = QStringLiteral("the ISMRMRD source changed while the Cartesian K-space plane was read");
          return false;
        }
        const auto sample_count = static_cast<std::size_t>(acquisition.header.number_of_samples);
        const auto channel_count = static_cast<std::size_t>(acquisition.header.active_channels);
        std::size_t expected_samples = 0U;
        if (!checked_multiply(sample_count, channel_count, expected_samples) ||
            acquisition.samples.size() != expected_samples) {
          payload_error = QStringLiteral("a Cartesian acquisition payload does not match its sample/channel header");
          return false;
        }
        const auto first_channel =
          request.axes.x == CartesianKspaceDimension::coil || request.axes.y == CartesianKspaceDimension::coil
            ? std::size_t{0U}
            : static_cast<std::size_t>(cartesian_kspace_coordinate_value(coordinate, CartesianKspaceDimension::coil));
        const auto past_last_channel =
          request.axes.x == CartesianKspaceDimension::coil || request.axes.y == CartesianKspaceDimension::coil
            ? channel_count
            : first_channel + 1U;
        if (past_last_channel > channel_count) {
          payload_error = QStringLiteral("the selected raw coil is outside a matching acquisition's active channels");
          return false;
        }
        for (auto sample = static_cast<std::size_t>(acquisition.header.discard_pre);
             sample < sample_count - static_cast<std::size_t>(acquisition.header.discard_post); ++sample) {
          const auto readout = static_cast<int>(sample) - static_cast<int>(acquisition.header.center_sample);
          if (!is_display_axis(request.axes, CartesianKspaceDimension::readout) &&
              readout != cartesian_kspace_coordinate_value(coordinate, CartesianKspaceDimension::readout)) {
            continue;
          }
          for (auto channel = first_channel; channel < past_last_channel; ++channel) {
            const auto x_value =
              sample_dimension_value(acquisition.header, request.axes.x, readout, static_cast<int>(channel));
            const auto y_value =
              sample_dimension_value(acquisition.header, request.axes.y, readout, static_cast<int>(channel));
            const auto x_source = axis_source_index(axis_x_values, x_value);
            const auto y_source = axis_source_index(axis_y_values, y_value);
            if (!x_source.has_value() || !y_source.has_value()) {
              payload_error = QStringLiteral("a Cartesian acquisition does not fit the indexed display geometry");
              return false;
            }
            const auto display_x =
              display_bin_for_source_coordinate(x_source.value(), axis_x_values.size(), extent->width);
            const auto display_y =
              display_bin_for_source_coordinate(y_source.value(), axis_y_values.size(), extent->height);
            const auto display_index = display_y * static_cast<std::size_t>(extent->width) + display_x;
            const auto value = acquisition.samples[sample + channel * sample_count];
            const auto real = static_cast<double>(value.real());
            const auto imaginary = static_cast<double>(value.imag());
            if (!std::isfinite(real) || !std::isfinite(imaginary) ||
                contribution_counts[display_index] == std::numeric_limits<std::uint32_t>::max() ||
                !std::isfinite(real_sums[display_index] + real) ||
                !std::isfinite(imaginary_sums[display_index] + imaginary)) {
              payload_error = QStringLiteral("Cartesian K-space display aggregation exceeds its bounded numeric range");
              return false;
            }
            real_sums[display_index] += real;
            imaginary_sums[display_index] += imaginary;
            ++contribution_counts[display_index];
            std::size_t next_source_complex_values = 0U;
            if (!checked_add(source_complex_values, 1U, next_source_complex_values)) {
              payload_error = QStringLiteral("the selected Cartesian K-space source value count overflows");
              return false;
            }
            source_complex_values = next_source_complex_values;
          }
        }
        return true;
      },
      reader_error);
    if (!payload_error.isEmpty()) {
      error = payload_error;
      return false;
    }
    if (payload_iteration != ksj::ismrmrd::InspectionIterationResult::completed) {
      error = reader_error.empty() ? QStringLiteral("Cartesian K-space payload reading did not complete")
                                   : to_qstring(reader_error);
      return false;
    }
  }

  std::vector<double> real_values;
  std::vector<double> imaginary_values;
  std::vector<double> magnitudes;
  std::vector<double> phase_degrees;
  real_values.reserve(display_cells);
  imaginary_values.reserve(display_cells);
  magnitudes.reserve(display_cells);
  phase_degrees.reserve(display_cells);
  std::size_t occupied_display_cells = 0U;
  std::size_t duplicate_display_cells = 0U;
  for (std::size_t index = 0U; index < display_cells; ++index) {
    const auto contributions = contribution_counts[index];
    const auto real = contributions == 0U ? 0.0 : real_sums[index] / static_cast<double>(contributions);
    const auto imaginary = contributions == 0U ? 0.0 : imaginary_sums[index] / static_cast<double>(contributions);
    const auto magnitude = std::hypot(real, imaginary);
    if (!std::isfinite(real) || !std::isfinite(imaginary) || !std::isfinite(magnitude)) {
      error = QStringLiteral("Cartesian K-space display aggregation produced a non-finite complex value");
      return false;
    }
    if (contributions != 0U) {
      ++occupied_display_cells;
      duplicate_display_cells += contributions > 1U ? 1U : 0U;
    }
    real_values.push_back(real);
    imaginary_values.push_back(imaginary);
    magnitudes.push_back(magnitude);
    phase_degrees.push_back(std::atan2(imaginary, real) * 180.0 / std::numbers::pi_v<double>);
  }
  QString render_error;
  ArrShowDisplayResult display_result;
  if (!render_arrshow_display(real_values, imaginary_values, extent->width, extent->height, request.display_settings,
                              display_result, render_error)) {
    error = render_error;
    return false;
  }
  presentation.image = display_result.image;

  const auto axis_bin_value = [](const std::vector<int>& values, const int display_index, const int display_count,
                                 const bool upper) {
    const auto source_count = values.size();
    const auto ceiling_divide = [](const std::size_t numerator, const std::size_t denominator) {
      return numerator / denominator + (numerator % denominator == 0U ? 0U : 1U);
    };
    const auto lower =
      ceiling_divide(static_cast<std::size_t>(display_index) * source_count, static_cast<std::size_t>(display_count));
    const auto upper_exclusive = ceiling_divide(static_cast<std::size_t>(display_index + 1) * source_count,
                                                static_cast<std::size_t>(display_count));
    const auto offset = upper ? upper_exclusive - 1U : lower;
    return values.at(std::min(offset, source_count - 1U));
  };
  presentation.csv_columns = {QStringLiteral("display_x"),
                              QStringLiteral("display_y"),
                              QStringLiteral("axis_x_coordinate_min"),
                              QStringLiteral("axis_x_coordinate_max"),
                              QStringLiteral("axis_y_coordinate_min"),
                              QStringLiteral("axis_y_coordinate_max"),
                              QStringLiteral("real"),
                              QStringLiteral("imaginary"),
                              QStringLiteral("magnitude"),
                              QStringLiteral("phase_degrees"),
                              QStringLiteral("contribution_count")};
  if (request.display_settings.component == ArrShowDisplayComponent::complex ||
      request.display_settings.component == ArrShowDisplayComponent::phase) {
    presentation.csv_columns.append({QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue")});
  }
  for (int display_y = 0; display_y < extent->height && presentation.csv_rows.size() < kMaximumExportRows;
       ++display_y) {
    for (int display_x = 0; display_x < extent->width && presentation.csv_rows.size() < kMaximumExportRows;
         ++display_x) {
      const auto display_index = static_cast<std::size_t>(display_y) * static_cast<std::size_t>(extent->width) +
                                 static_cast<std::size_t>(display_x);
      QStringList row{QString::number(display_x),
                      QString::number(display_y),
                      QString::number(axis_bin_value(axis_x_values, display_x, extent->width, false)),
                      QString::number(axis_bin_value(axis_x_values, display_x, extent->width, true)),
                      QString::number(axis_bin_value(axis_y_values, display_y, extent->height, false)),
                      QString::number(axis_bin_value(axis_y_values, display_y, extent->height, true)),
                      QString::number(real_values[display_index], 'g', 12),
                      QString::number(imaginary_values[display_index], 'g', 12),
                      QString::number(magnitudes[display_index], 'g', 12),
                      QString::number(phase_degrees[display_index], 'g', 12),
                      QString::number(contribution_counts[display_index])};
      if (request.display_settings.component == ArrShowDisplayComponent::complex ||
          request.display_settings.component == ArrShowDisplayComponent::phase) {
        const auto colour = presentation.image.pixel(display_x, display_y);
        row.append({QString::number(qRed(colour)), QString::number(qGreen(colour)), QString::number(qBlue(colour))});
      }
      presentation.csv_rows.append(row);
    }
  }

  const auto axis_x_name = cartesian_kspace_dimension_identifier(request.axes.x);
  const auto axis_y_name = cartesian_kspace_dimension_identifier(request.axes.y);
  QStringList fixed_coordinates;
  for (const auto dimension : kCartesianKspaceDimensions) {
    if (!is_display_axis(request.axes, dimension)) {
      fixed_coordinates.append(QStringLiteral("%1=%2")
                                 .arg(cartesian_kspace_dimension_identifier(dimension))
                                 .arg(cartesian_kspace_coordinate_value(coordinate, dimension)));
    }
  }
  const auto acquisition_kind_label = cartesian_kspace_acquisition_kind_label(request.acquisition_kind);
  presentation.summary =
    QStringLiteral("Selected ISMRMRD acquisition type: %1.\nRaw Cartesian K-space from %2 matching acquisition(s).\n"
                   "Axes: %3 × %4; source grid %5 × %6, display grid %7 × %8.\n"
                   "Fixed observed coordinates: %9.\n"
                   "Display: %10. %11 occupied, %12 empty, %13 with multiple contributions; a one-to-one display cell "
                   "is its raw complex sample, while a repeated/bounded-downsampled cell is the disclosed complex "
                   "arithmetic mean.\nNo FFT, gridding, RSS, log-intensity transform, or reconstruction is applied.")
      .arg(acquisition_kind_label)
      .arg(static_cast<qulonglong>(lines.size()))
      .arg(axis_x_name, axis_y_name)
      .arg(static_cast<qulonglong>(axis_x_values.size()))
      .arg(static_cast<qulonglong>(axis_y_values.size()))
      .arg(extent->width)
      .arg(extent->height)
      .arg(fixed_coordinates.join(QStringLiteral(", ")))
      .arg(arrshow_display_component_label(request.display_settings.component))
      .arg(static_cast<qulonglong>(occupied_display_cells))
      .arg(static_cast<qulonglong>(display_cells - occupied_display_cells))
      .arg(static_cast<qulonglong>(duplicate_display_cells));
  presentation.summary.append(cartesian_kspace_acquisition_kind_is_imaging(request.acquisition_kind)
                                ? QStringLiteral("\nThe standard XML imaging phase-encode limit is enforced before "
                                                 "any axis projection.")
                                : QStringLiteral("\nThis is explicitly selected auxiliary raw data; its phase-encode "
                                                 "coordinates are observed rather than limited by an imaging range."));

  QJsonObject source_grid;
  source_grid.insert(QStringLiteral("axis_x_dimension"), axis_x_name);
  source_grid.insert(QStringLiteral("axis_y_dimension"), axis_y_name);
  source_grid.insert(QStringLiteral("width"), static_cast<int>(axis_x_values.size()));
  source_grid.insert(QStringLiteral("height"), static_cast<int>(axis_y_values.size()));
  source_grid.insert(QStringLiteral("axis_x_values"), axis_values_json(axis_x_values));
  source_grid.insert(QStringLiteral("axis_y_values"), axis_values_json(axis_y_values));
  QJsonObject display_grid;
  display_grid.insert(QStringLiteral("width"), extent->width);
  display_grid.insert(QStringLiteral("height"), extent->height);
  display_grid.insert(QStringLiteral("downsampled"), extent->width != static_cast<int>(axis_x_values.size()) ||
                                                       extent->height != static_cast<int>(axis_y_values.size()));
  presentation.details.insert(QStringLiteral("artifact_kind"), QStringLiteral("visualization-derivative"));
  presentation.details.insert(QStringLiteral("view"), QStringLiteral("cartesian-k-space"));
  presentation.details.insert(QStringLiteral("representation"),
                              QStringLiteral("raw Cartesian ISMRMRD complex grid along two selected dimensions; no "
                                             "FFT, gridding, RSS, log-intensity transform, or reconstruction"));
  presentation.details.insert(QStringLiteral("source"), source_description(session));
  presentation.details.insert(QStringLiteral("container_path"), to_qstring(session.metadata().group));
  presentation.details.insert(QStringLiteral("axis_x"), axis_x_name);
  presentation.details.insert(QStringLiteral("axis_y"), axis_y_name);
  presentation.details.insert(QStringLiteral("fixed_coordinates"),
                              cartesian_coordinate_json(coordinate, request.axes, false));
  presentation.details.insert(QStringLiteral("coordinate_roles"),
                              cartesian_coordinate_json(coordinate, request.axes, true));
  presentation.details.insert(QStringLiteral("source_grid"), source_grid);
  presentation.details.insert(QStringLiteral("display_grid"), display_grid);
  presentation.details.insert(QStringLiteral("acquisition_kind"),
                              cartesian_kspace_acquisition_kind_identifier(request.acquisition_kind));
  presentation.details.insert(QStringLiteral("acquisition_kind_label"), acquisition_kind_label);
  presentation.details.insert(QStringLiteral("acquisition_kind_is_imaging"),
                              cartesian_kspace_acquisition_kind_is_imaging(request.acquisition_kind));
  presentation.details.insert(QStringLiteral("phase_encode_range_source"),
                              cartesian_kspace_acquisition_kind_is_imaging(request.acquisition_kind)
                                ? QStringLiteral("xml_imaging_encoding_limit")
                                : QStringLiteral("observed_selected_acquisition_kind"));
  presentation.details.insert(QStringLiteral("matching_acquisition_count"), static_cast<int>(lines.size()));
  presentation.details.insert(QStringLiteral("source_complex_values"), static_cast<int>(source_complex_values));
  presentation.details.insert(QStringLiteral("csv_rows_returned"), static_cast<int>(presentation.csv_rows.size()));
  presentation.details.insert(QStringLiteral("csv_truncated"), display_cells > kMaximumExportRows);
  presentation.details.insert(QStringLiteral("occupied_display_cells"), static_cast<int>(occupied_display_cells));
  presentation.details.insert(QStringLiteral("empty_display_cells"),
                              static_cast<int>(display_cells - occupied_display_cells));
  presentation.details.insert(QStringLiteral("multi_contribution_display_cells"),
                              static_cast<int>(duplicate_display_cells));
  presentation.details.insert(QStringLiteral("display_component"),
                              arrshow_display_component_identifier(request.display_settings.component));
  presentation.details.insert(QStringLiteral("component_semantics"),
                              arrshow_display_component_semantics(request.display_settings.component));
  presentation.details.insert(QStringLiteral("display_engine"), QStringLiteral("arrshow-port"));
  presentation.details.insert(QStringLiteral("range_calculation"),
                              arrshow_range_calculation_identifier(request.display_settings.range_calculation));
  presentation.details.insert(QStringLiteral("range_percentile"), request.display_settings.percentile);
  presentation.details.insert(QStringLiteral("phase_representation"),
                              arrshow_phase_representation_identifier(request.display_settings.phase_representation));
  presentation.details.insert(QStringLiteral("coil_mode"), is_display_axis(request.axes, CartesianKspaceDimension::coil)
                                                             ? QStringLiteral("axis")
                                                             : QStringLiteral("single-coil"));
  if (!is_display_axis(request.axes, CartesianKspaceDimension::coil)) {
    presentation.details.insert(QStringLiteral("coil_channel"),
                                cartesian_kspace_coordinate_value(coordinate, CartesianKspaceDimension::coil));
  }
  presentation.details.insert(QStringLiteral("source_minimum"), display_result.source_minimum);
  presentation.details.insert(QStringLiteral("source_maximum"), display_result.source_maximum);
  presentation.details.insert(QStringLiteral("window_mode"),
                              arrshow_window_persistence_identifier(display_result.window_persistence));
  presentation.details.insert(QStringLiteral("window_center"), display_result.applied_window_center);
  presentation.details.insert(QStringLiteral("window_width"), display_result.applied_window_width);
  presentation.details.insert(QStringLiteral("aggregation"),
                              QStringLiteral("complex arithmetic mean only for repeated or bounded-downsampled display "
                                             "cells"));
  if (request.display_settings.component == ArrShowDisplayComponent::complex ||
      request.display_settings.component == ArrShowDisplayComponent::phase) {
    presentation.details.insert(QStringLiteral("phase_colormap"), QStringLiteral("arrshow-martin-phase-256"));
    presentation.details.insert(
      QStringLiteral("csv_colour_columns"),
      QStringLiteral("C/W-dependent RGB visualization derivative; raw complex CSV columns remain source values"));
  }
  if (request.display_settings.component == ArrShowDisplayComponent::phase) {
    presentation.details.insert(QStringLiteral("phase_unit"),
                                arrshow_phase_representation_identifier(request.display_settings.phase_representation));
  }
  presentation.component = request.display_settings.component;
  presentation.display_settings = request.display_settings;
  arrshow_set_active_window_value(presentation.display_settings, display_result.source_minimum,
                                  display_result.source_maximum, display_result.applied_window_center,
                                  display_result.applied_window_width);
  presentation.source_minimum = display_result.source_minimum;
  presentation.source_maximum = display_result.source_maximum;
  presentation.applied_window_center = display_result.applied_window_center;
  presentation.applied_window_width = display_result.applied_window_width;
  presentation.window_persistence = display_result.window_persistence;
  return true;
}

bool make_image_presentation(InspectionSession& session, ImageRequest request, ImagePresentation& presentation,
                             QString& error) {
  presentation = {};
  error.clear();
  if (!session.is_open()) {
    error = QStringLiteral("open a standard ISMRMRD dataset before inspecting an image");
    return false;
  }
  if (request.series_id.trimmed().isEmpty()) {
    error = QStringLiteral("an ISMRMRD image series is required");
    return false;
  }
  if (!image_axes_are_valid(request.axes) || request.axes.x != ImageDimension::x ||
      request.axes.y != ImageDimension::y) {
    error = QStringLiteral("the standard image viewer fixes its native X and Y dimensions as the display plane");
    return false;
  }

  const auto utf8_series = request.series_id.trimmed().toUtf8();
  const ksj::ismrmrd::ImageLocator locator{
    .series_id = std::string(utf8_series.constData(), static_cast<std::size_t>(utf8_series.size())),
    .ordinal = request.ordinal};

  QString callback_error;
  std::string reader_error;
  const auto result = session.reader().with_image_pixels(
    locator,
    [&session, request, &presentation, &callback_error](const ksj::ismrmrd::InspectionImageRecord& record,
                                                        const ImagePixelsView& pixels) {
      const auto dimensions = pixels.dimensions;
      for (const auto dimension : kImageDimensions) {
        const auto dimension_index = image_dimension_index(dimension);
        if (dimensions[dimension_index] == 0U) {
          callback_error = QStringLiteral("an ISMRMRD image has an empty standard pixel dimension");
          return false;
        }
        if (!image_axis_is_selected(request.axes, dimension) &&
            image_coordinate_value(request.coordinate, dimension) >= dimensions[dimension_index]) {
          callback_error = QStringLiteral("a requested fixed image dimension value is outside its standard extent");
          return false;
        }
      }

      const auto bytes_per_value = image_value_bytes(pixels.data_type);
      std::size_t element_count = 0U;
      std::size_t expected_bytes = 0U;
      if (bytes_per_value == 0U || !checked_multiply(dimensions[0], dimensions[1], element_count) ||
          !checked_multiply(element_count, dimensions[2], element_count) ||
          !checked_multiply(element_count, dimensions[3], element_count) ||
          !checked_multiply(element_count, bytes_per_value, expected_bytes) || pixels.pixels.size() != expected_bytes) {
        callback_error = QStringLiteral("ISMRMRD image pixels do not match their standard dimensions and data type");
        return false;
      }

      const auto source_width = dimensions[image_dimension_index(request.axes.x)];
      const auto source_height = dimensions[image_dimension_index(request.axes.y)];
      const auto extent = make_display_extent(source_width, source_height, kImageMaximumDimension, kImageMaximumPixels);
      if (!extent.has_value()) {
        callback_error = QStringLiteral("ISMRMRD image cannot produce a bounded display extent");
        return false;
      }

      const auto display_pixels = static_cast<std::size_t>(extent->width) * static_cast<std::size_t>(extent->height);
      std::vector<double> real_values;
      std::vector<double> imaginary_values;
      real_values.reserve(display_pixels);
      imaginary_values.reserve(display_pixels);
      for (int display_y = 0; display_y < extent->height; ++display_y) {
        const auto y = source_index_for_display(display_y, extent->height, source_height);
        for (int display_x = 0; display_x < extent->width; ++display_x) {
          const auto x = source_index_for_display(display_x, extent->width, source_width);
          auto sample_coordinate = request.coordinate;
          set_image_coordinate_value(sample_coordinate, request.axes.x, static_cast<std::uint16_t>(x));
          set_image_coordinate_value(sample_coordinate, request.axes.y, static_cast<std::uint16_t>(y));
          const auto index = image_linear_index(dimensions, sample_coordinate);
          const auto sample = image_sample_components(pixels, index);
          real_values.push_back(sample.real);
          imaginary_values.push_back(sample.imaginary);
        }
      }

      auto effective_settings = request.display_settings;
      if (!image_data_type_is_complex(pixels.data_type) &&
          arrshow_display_component_requires_complex(effective_settings.component)) {
        // arrShow's lockImagAndPhase moves a real input from Im/Com/Pha back
        // to Real rather than inventing a zero-imaginary complex display.
        effective_settings.component = ArrShowDisplayComponent::real;
      }

      QString render_error;
      ArrShowDisplayResult display_result;
      if (!render_arrshow_display(real_values, imaginary_values, extent->width, extent->height, effective_settings,
                                  display_result, render_error)) {
        callback_error = render_error;
        return false;
      }
      presentation.image = display_result.image;

      presentation.csv_columns = {
        QStringLiteral("axis_x_coordinate"), QStringLiteral("axis_y_coordinate"), QStringLiteral("real"),
        QStringLiteral("imaginary"),         QStringLiteral("magnitude"),         QStringLiteral("phase_degrees"),
        QStringLiteral("phase_radians")};
      const auto colour_encoded = effective_settings.component == ArrShowDisplayComponent::complex ||
                                  effective_settings.component == ArrShowDisplayComponent::phase;
      if (colour_encoded) {
        presentation.csv_columns.append({QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue")});
      }
      for (int display_y = 0; display_y < extent->height; ++display_y) {
        const auto y = source_index_for_display(display_y, extent->height, source_height);
        for (int display_x = 0; display_x < extent->width; ++display_x) {
          if (presentation.csv_rows.size() >= static_cast<qsizetype>(kMaximumExportRows)) {
            break;
          }
          const auto x = source_index_for_display(display_x, extent->width, source_width);
          const auto display_index = static_cast<std::size_t>(display_y) * static_cast<std::size_t>(extent->width) +
                                     static_cast<std::size_t>(display_x);
          const auto real = real_values[display_index];
          const auto imaginary = imaginary_values[display_index];
          const auto phase_radians = std::atan2(imaginary, real);
          QStringList row{QString::number(x),
                          QString::number(y),
                          QString::number(real, 'g', 12),
                          QString::number(imaginary, 'g', 12),
                          QString::number(std::hypot(real, imaginary), 'g', 12),
                          QString::number(phase_radians * 180.0 / std::numbers::pi_v<double>, 'g', 12),
                          QString::number(phase_radians, 'g', 12)};
          if (colour_encoded) {
            const auto colour = presentation.image.pixel(display_x, display_y);
            row.append(
              {QString::number(qRed(colour)), QString::number(qGreen(colour)), QString::number(qBlue(colour))});
          }
          presentation.csv_rows.append(row);
        }
        if (presentation.csv_rows.size() >= static_cast<qsizetype>(kMaximumExportRows)) {
          break;
        }
      }

      presentation.source_dimensions = dimensions;
      presentation.axes = request.axes;
      presentation.coordinate = request.coordinate;
      presentation.component = effective_settings.component;
      presentation.display_settings = effective_settings;
      arrshow_set_active_window_value(presentation.display_settings, display_result.source_minimum,
                                      display_result.source_maximum, display_result.applied_window_center,
                                      display_result.applied_window_width);
      presentation.source_minimum = display_result.source_minimum;
      presentation.source_maximum = display_result.source_maximum;
      presentation.applied_window_center = display_result.applied_window_center;
      presentation.applied_window_width = display_result.applied_window_width;
      presentation.window_persistence = display_result.window_persistence;
      presentation.summary =
        image_summary(record, dimensions, request.axes, request.coordinate, effective_settings.component);
      presentation.summary +=
        QStringLiteral("\narrShow display: %1; range %2; C/W = %3 / %4.")
          .arg(arrshow_display_component_semantics(effective_settings.component),
               arrshow_range_calculation_label(effective_settings.range_calculation, effective_settings.percentile))
          .arg(display_result.applied_window_center, 0, 'g', 8)
          .arg(display_result.applied_window_width, 0, 'g', 8);
      presentation.details = image_details(record, dimensions, request.axes, request.coordinate,
                                           effective_settings.component, source_description(session));
      QJsonObject source_grid;
      source_grid.insert(QStringLiteral("axis_x_dimension"), image_dimension_identifier_text(request.axes.x));
      source_grid.insert(QStringLiteral("axis_y_dimension"), image_dimension_identifier_text(request.axes.y));
      source_grid.insert(QStringLiteral("width"), static_cast<int>(source_width));
      source_grid.insert(QStringLiteral("height"), static_cast<int>(source_height));
      presentation.details.insert(QStringLiteral("source_grid"), source_grid);
      presentation.details.insert(QStringLiteral("display_width"), extent->width);
      presentation.details.insert(QStringLiteral("display_height"), extent->height);
      presentation.details.insert(QStringLiteral("display_engine"), QStringLiteral("arrshow-port"));
      presentation.details.insert(QStringLiteral("range_calculation"),
                                  arrshow_range_calculation_identifier(effective_settings.range_calculation));
      presentation.details.insert(QStringLiteral("range_percentile"), effective_settings.percentile);
      presentation.details.insert(QStringLiteral("phase_representation"),
                                  arrshow_phase_representation_identifier(effective_settings.phase_representation));
      presentation.details.insert(QStringLiteral("source_minimum"), display_result.source_minimum);
      presentation.details.insert(QStringLiteral("source_maximum"), display_result.source_maximum);
      presentation.details.insert(QStringLiteral("window_mode"),
                                  arrshow_window_persistence_identifier(display_result.window_persistence));
      presentation.details.insert(QStringLiteral("window_center"), display_result.applied_window_center);
      presentation.details.insert(QStringLiteral("window_width"), display_result.applied_window_width);
      if (colour_encoded) {
        presentation.details.insert(QStringLiteral("phase_colormap"), QStringLiteral("arrshow-martin-phase-256"));
        presentation.details.insert(
          QStringLiteral("csv_colour_columns"),
          QStringLiteral("C/W-dependent RGB visualization derivative; raw image CSV columns remain source values"));
      }
      if (effective_settings.component == ArrShowDisplayComponent::complex) {
        presentation.details.insert(QStringLiteral("colour_mapping"),
                                    QStringLiteral("arrshow-martin-phase-times-magnitude-window"));
        presentation.details.insert(QStringLiteral("brightness_window_component"), QStringLiteral("magnitude"));
      } else if (effective_settings.component == ArrShowDisplayComponent::phase) {
        presentation.details.insert(QStringLiteral("phase_unit"),
                                    arrshow_phase_representation_identifier(effective_settings.phase_representation));
      }
      if (effective_settings.component != request.display_settings.component) {
        presentation.details.insert(QStringLiteral("requested_display_component"),
                                    arrshow_display_component_identifier(request.display_settings.component));
        presentation.details.insert(QStringLiteral("component_fallback"),
                                    QStringLiteral("real-source-uses-arrshow-real"));
      }
      presentation.details.insert(QStringLiteral("csv_rows_returned"), static_cast<int>(presentation.csv_rows.size()));
      presentation.details.insert(QStringLiteral("csv_truncated"), display_pixels > kMaximumExportRows);
      return true;
    },
    reader_error);

  if (!callback_error.isEmpty()) {
    error = callback_error;
    return false;
  }
  if (result != ksj::ismrmrd::InspectionIterationResult::completed) {
    error =
      reader_error.empty() ? QStringLiteral("ISMRMRD image inspection did not complete") : to_qstring(reader_error);
    return false;
  }
  return true;
}

bool load_pipeline_presentation(const QString& file_path, PipelinePresentation& presentation, QString& error) {
  presentation = {};
  error.clear();

  const auto trimmed_path = file_path.trimmed();
  if (trimmed_path.isEmpty()) {
    error = QStringLiteral("a PipelineDefinition JSON file path is required");
    return false;
  }
  QFile file(trimmed_path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("cannot open PipelineDefinition file: %1").arg(file.errorString());
    return false;
  }

  constexpr auto maximum_bytes =
    static_cast<qint64>(ksj::recon::graph::kPipelineDefinitionJsonParseLimits.max_document_bytes);
  const auto document = file.read(maximum_bytes + 1);
  if (document.size() > maximum_bytes) {
    error = QStringLiteral("PipelineDefinition exceeds the %1-byte parser limit").arg(maximum_bytes);
    return false;
  }
  if (file.error() != QFileDevice::NoError) {
    error = QStringLiteral("cannot read PipelineDefinition file: %1").arg(file.errorString());
    return false;
  }

  const auto parsed = ksj::recon::graph::PipelineDefinition::parse_json(
    std::string_view(document.constData(), static_cast<std::size_t>(document.size())));
  if (!parsed.ok()) {
    error = to_qstring(parsed.status().message());
    return false;
  }
  const auto& pipeline = parsed.value();

  presentation.summary =
    QStringLiteral("Pipeline %1 (%2)\nInput profile: ismrmrd-hdf5 / %3\nParsed only: no Provider resolution, "
                   "graph compilation, loading, or execution is performed.")
      .arg(to_qstring(pipeline.id()), to_qstring(pipeline.display_name()),
           to_qstring(pipeline.input_profile().dataset_group));
  presentation.canonical_json = to_qstring(pipeline.canonical_json());
  presentation.csv_columns = {QStringLiteral("node_id"), QStringLiteral("provider_alias"),
                              QStringLiteral("operator_id"), QStringLiteral("canonical_configuration")};

  for (const auto& ingress : pipeline.ingress_ports()) {
    presentation.graph_nodes.append(
      {.key = pipeline_graph_node_key(QStringLiteral("ingress"), ingress.id),
       .kind = PipelineGraphNodeKind::ingress,
       .title = QStringLiteral("Ingress: %1").arg(to_qstring(ingress.id)),
       .detail = QStringLiteral("%1 → %2.%3")
                   .arg(to_qstring(ingress.type), to_qstring(ingress.to.node), to_qstring(ingress.to.port))});
  }

  for (const auto& node : pipeline.nodes()) {
    presentation.graph_nodes.append({.key = pipeline_graph_node_key(QStringLiteral("node"), node.id),
                                     .kind = PipelineGraphNodeKind::operator_node,
                                     .title = to_qstring(node.id),
                                     .detail = QStringLiteral("Provider alias: %1\nOperator: %2")
                                                 .arg(to_qstring(node.provider_alias), to_qstring(node.operator_id))});
  }

  for (const auto& egress : pipeline.egress_ports()) {
    presentation.graph_nodes.append(
      {.key = pipeline_graph_node_key(QStringLiteral("egress"), egress.id),
       .kind = PipelineGraphNodeKind::egress,
       .title = QStringLiteral("Egress: %1").arg(to_qstring(egress.id)),
       .detail = QStringLiteral("%1.%2 → %3")
                   .arg(to_qstring(egress.from.node), to_qstring(egress.from.port), to_qstring(egress.type))});
  }

  for (const auto& ingress : pipeline.ingress_ports()) {
    presentation.graph_edges.append({.id = to_qstring(ingress.id),
                                     .kind = PipelineGraphEdgeKind::ingress,
                                     .source_key = pipeline_graph_node_key(QStringLiteral("ingress"), ingress.id),
                                     .source_port = to_qstring(ingress.id),
                                     .target_key = pipeline_graph_node_key(QStringLiteral("node"), ingress.to.node),
                                     .target_port = to_qstring(ingress.to.port)});
  }

  for (const auto& edge : pipeline.edges()) {
    presentation.graph_edges.append({.id = to_qstring(edge.id),
                                     .kind = PipelineGraphEdgeKind::data,
                                     .source_key = pipeline_graph_node_key(QStringLiteral("node"), edge.from.node),
                                     .source_port = to_qstring(edge.from.port),
                                     .target_key = pipeline_graph_node_key(QStringLiteral("node"), edge.to.node),
                                     .target_port = to_qstring(edge.to.port)});
  }

  for (const auto& calibration : pipeline.calibration_bindings()) {
    for (const auto& consumer : calibration.consumers) {
      presentation.graph_edges.append(
        {.id = QStringLiteral("%1:%2.%3")
                 .arg(to_qstring(calibration.id), to_qstring(consumer.node), to_qstring(consumer.port)),
         .kind = PipelineGraphEdgeKind::calibration,
         .source_key = pipeline_graph_node_key(QStringLiteral("node"), calibration.producer.node),
         .source_port = to_qstring(calibration.producer.port),
         .target_key = pipeline_graph_node_key(QStringLiteral("node"), consumer.node),
         .target_port = to_qstring(consumer.port)});
    }
  }

  for (const auto& egress : pipeline.egress_ports()) {
    presentation.graph_edges.append({.id = to_qstring(egress.id),
                                     .kind = PipelineGraphEdgeKind::egress,
                                     .source_key = pipeline_graph_node_key(QStringLiteral("node"), egress.from.node),
                                     .source_port = to_qstring(egress.from.port),
                                     .target_key = pipeline_graph_node_key(QStringLiteral("egress"), egress.id),
                                     .target_port = to_qstring(egress.id)});
  }

  QJsonArray nodes;
  for (const auto& node : pipeline.nodes()) {
    presentation.csv_rows.append({to_qstring(node.id), to_qstring(node.provider_alias), to_qstring(node.operator_id),
                                  to_qstring(node.canonical_config)});
    QJsonObject item;
    item.insert(QStringLiteral("id"), to_qstring(node.id));
    item.insert(QStringLiteral("provider_alias"), to_qstring(node.provider_alias));
    item.insert(QStringLiteral("operator_id"), to_qstring(node.operator_id));
    item.insert(QStringLiteral("canonical_configuration"), to_qstring(node.canonical_config));
    nodes.append(item);
  }

  QJsonArray profiles;
  for (const auto profile : pipeline.allowed_profiles()) {
    profiles.append(to_qstring(ksj::recon::to_string(profile)));
  }
  QJsonArray parameters;
  for (const auto& parameter : pipeline.parameters()) {
    QJsonObject item;
    item.insert(QStringLiteral("name"), to_qstring(parameter.name));
    item.insert(QStringLiteral("type"), parameter_type_name(parameter.type));
    item.insert(QStringLiteral("default"), to_qstring(parameter.canonical_default_json));
    parameters.append(item);
  }

  QJsonArray graph_nodes;
  for (const auto& node : presentation.graph_nodes) {
    QJsonObject item;
    item.insert(QStringLiteral("key"), node.key);
    item.insert(QStringLiteral("kind"), pipeline_graph_node_kind_name(node.kind));
    item.insert(QStringLiteral("title"), node.title);
    item.insert(QStringLiteral("detail"), node.detail);
    graph_nodes.append(item);
  }

  QJsonArray graph_edges;
  for (const auto& edge : presentation.graph_edges) {
    QJsonObject item;
    item.insert(QStringLiteral("id"), edge.id);
    item.insert(QStringLiteral("kind"), pipeline_graph_edge_kind_name(edge.kind));
    item.insert(QStringLiteral("source_key"), edge.source_key);
    item.insert(QStringLiteral("source_port"), edge.source_port);
    item.insert(QStringLiteral("target_key"), edge.target_key);
    item.insert(QStringLiteral("target_port"), edge.target_port);
    graph_edges.append(item);
  }

  presentation.details.insert(QStringLiteral("artifact_kind"), QStringLiteral("visualization-derivative"));
  presentation.details.insert(QStringLiteral("view"), QStringLiteral("pipeline"));
  presentation.details.insert(QStringLiteral("graph_kind"), QStringLiteral("authored-dag"));
  presentation.details.insert(QStringLiteral("source"), trimmed_path);
  presentation.details.insert(QStringLiteral("pipeline_id"), to_qstring(pipeline.id()));
  presentation.details.insert(QStringLiteral("display_name"), to_qstring(pipeline.display_name()));
  presentation.details.insert(QStringLiteral("input_profile"), QStringLiteral("ismrmrd-hdf5"));
  presentation.details.insert(QStringLiteral("dataset_group"), to_qstring(pipeline.input_profile().dataset_group));
  presentation.details.insert(QStringLiteral("artifact_digest"), to_qstring(pipeline.artifact_digest().value()));
  presentation.details.insert(QStringLiteral("allowed_profiles"), profiles);
  presentation.details.insert(QStringLiteral("parameters"), parameters);
  presentation.details.insert(QStringLiteral("nodes"), nodes);
  presentation.details.insert(QStringLiteral("graph_nodes"), graph_nodes);
  presentation.details.insert(QStringLiteral("graph_edges"), graph_edges);
  presentation.details.insert(QStringLiteral("edge_count"), static_cast<int>(pipeline.edges().size()));
  presentation.details.insert(QStringLiteral("ingress_count"), static_cast<int>(pipeline.ingress_ports().size()));
  presentation.details.insert(QStringLiteral("egress_count"), static_cast<int>(pipeline.egress_ports().size()));
  return true;
}

} // namespace ksj::viewer
