#pragma once

#include "arrshow_display.hpp"
#include "inspection_session.hpp"

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ksj::viewer {

// All values in these types are bounded display data. In particular, image
// and acquisition samples are converted while the reader callback is active;
// no ISMRMRD payload view is retained by a presentation.
struct MetadataPresentation {
  QString summary;
  QString xml_preview;
  QStringList csv_columns;
  QList<QStringList> csv_rows;
  QJsonObject details;
};

struct KspacePresentation {
  QString summary;
  QImage image;
  ArrShowDisplayComponent component{ArrShowDisplayComponent::complex};
  ArrShowDisplaySettings display_settings{};
  double source_minimum{0.0};
  double source_maximum{0.0};
  double applied_window_center{0.0};
  double applied_window_width{0.0};
  ArrShowWindowPersistence window_persistence{ArrShowWindowPersistence::relative};
  QStringList csv_columns;
  QList<QStringList> csv_rows;
  QJsonObject details;
};

// A K-space acquisition kind is a standard-flag membership filter, not a
// storage-record selector. A record with more than one standard auxiliary
// flag may deliberately match more than one auxiliary kind. Imaging data is
// preferred by the UI when renderable and includes
// parallel-calibration-and-imaging records.
enum class CartesianKspaceAcquisitionKind : std::uint8_t {
  imaging,
  noise_measurement,
  parallel_calibration,
  navigation,
  phase_correction,
  high_performance_feedback,
  realtime_feedback,
  dummy_scan,
  surface_coil_correction,
  phase_stabilization_reference,
  phase_stabilization,
  count,
};

struct CartesianKspaceAcquisitionKindOption {
  CartesianKspaceAcquisitionKind kind{CartesianKspaceAcquisitionKind::imaging};
  QString label;
  std::size_t matching_acquisition_count{0U};
};

// These are the complete raw Cartesian dimensions which can participate in a
// Viewer plane. `readout` comes from a sample's centered coordinate, `coil`
// from its channel, and the remaining values from the ISMRMRD acquisition
// header. The Viewer always chooses exactly two distinct dimensions as a
// display plane; all others are fixed to observed source coordinates.
enum class CartesianKspaceDimension : std::uint8_t {
  readout,
  phase_encode,
  coil,
  encoding_space,
  partition,
  average,
  slice,
  contrast,
  physiological_phase,
  repetition,
  set,
  segment,
  user_0,
  user_1,
  user_2,
  user_3,
  user_4,
  user_5,
  user_6,
  user_7,
  count,
};

inline constexpr std::size_t kCartesianKspaceDimensionCount = static_cast<std::size_t>(CartesianKspaceDimension::count);

[[nodiscard]] constexpr std::size_t
cartesian_kspace_dimension_index(const CartesianKspaceDimension dimension) noexcept {
  return static_cast<std::size_t>(dimension);
}

// A coordinate is not a storage-record selector. It holds only semantic
// coordinate values; catalog resolution below replaces a sparse, unavailable
// combination with an actually observed compatible tuple before rendering.
struct CartesianKspaceCoordinate final {
  std::array<int, kCartesianKspaceDimensionCount> values{};

  friend constexpr bool operator==(const CartesianKspaceCoordinate&,
                                   const CartesianKspaceCoordinate&) noexcept = default;
};

[[nodiscard]] constexpr int cartesian_kspace_coordinate_value(const CartesianKspaceCoordinate& coordinate,
                                                              const CartesianKspaceDimension dimension) noexcept {
  return coordinate.values.at(cartesian_kspace_dimension_index(dimension));
}

constexpr void set_cartesian_kspace_coordinate_value(CartesianKspaceCoordinate& coordinate,
                                                     const CartesianKspaceDimension dimension,
                                                     const int value) noexcept {
  coordinate.values.at(cartesian_kspace_dimension_index(dimension)) = value;
}

struct CartesianKspaceAxes final {
  CartesianKspaceDimension x{CartesianKspaceDimension::readout};
  CartesianKspaceDimension y{CartesianKspaceDimension::phase_encode};

  friend constexpr bool operator==(const CartesianKspaceAxes&, const CartesianKspaceAxes&) noexcept = default;
};

// Header-only information used to resolve sparse coordinate selection. The
// source ordinal is private ordering/input metadata, never a Viewer-visible
// selection concept and never permits retaining a payload view.
struct CartesianKspaceCatalogEntry final {
  CartesianKspaceCoordinate coordinate{};
  std::int32_t readout_minimum{0};
  std::int32_t readout_maximum{0};
  std::uint16_t active_channel_count{0U};
  std::uint32_t source_ordinal{0U};
};

struct CartesianKspaceDimensionValues final {
  CartesianKspaceDimension dimension{CartesianKspaceDimension::readout};
  QList<int> observed_values;
};

// Bounded, header-only catalog for a standard acquisition-type membership.
// It does not materialize or retain acquisition payload samples.
struct CartesianKspaceCatalog final {
  QList<CartesianKspaceDimensionValues> dimensions;
  QList<CartesianKspaceCatalogEntry> entries;
  CartesianKspaceCoordinate initial_coordinate{};
  std::size_t matching_acquisition_count{0U};
};

// A raw Cartesian presentation has exactly two named display axes. For
// example, `x=readout, y=coil` with `phase_encode=0` renders RO × Co from the
// actual raw acquisition samples. It neither edits nor retains source data.
struct CartesianKspaceRequest {
  CartesianKspaceAcquisitionKind acquisition_kind{CartesianKspaceAcquisitionKind::imaging};
  CartesianKspaceAxes axes{};
  CartesianKspaceCoordinate coordinate{};
  ArrShowDisplaySettings display_settings{};
};

// Image settings use the same arrShow display vocabulary and independent
// normal/phase C/W state as raw K-space. The data-type gate below mirrors
// arrShow: a real image exposes Magnitude and Real only.
using ImageDisplaySettings = ArrShowDisplaySettings;

// Standard ISMRMRD image pixels use the native storage order [x, y, z,
// channel].  Image presentation shares the arrShow dimension strip with raw
// K-space, but its native X/Y columns are fixed ':' axes; Z and channel are
// index selectors only. This type deliberately does not include the
// image-series storage ordinal; that ordinal selects an image record, not a
// pixel dimension inside that record.
enum class ImageDimension : std::uint8_t {
  x,
  y,
  z,
  channel,
  count,
};

inline constexpr std::size_t kImageDimensionCount = static_cast<std::size_t>(ImageDimension::count);

[[nodiscard]] constexpr std::size_t image_dimension_index(const ImageDimension dimension) noexcept {
  return static_cast<std::size_t>(dimension);
}

struct ImageCoordinate final {
  std::array<std::uint16_t, kImageDimensionCount> values{};

  friend constexpr bool operator==(const ImageCoordinate&, const ImageCoordinate&) noexcept = default;
};

[[nodiscard]] constexpr std::uint16_t image_coordinate_value(const ImageCoordinate& coordinate,
                                                             const ImageDimension dimension) noexcept {
  return coordinate.values.at(image_dimension_index(dimension));
}

constexpr void set_image_coordinate_value(ImageCoordinate& coordinate, const ImageDimension dimension,
                                          const std::uint16_t value) noexcept {
  coordinate.values.at(image_dimension_index(dimension)) = value;
}

struct ImageAxes final {
  ImageDimension x{ImageDimension::x};
  ImageDimension y{ImageDimension::y};

  friend constexpr bool operator==(const ImageAxes&, const ImageAxes&) noexcept = default;
};

struct ImageRequest final {
  QString series_id;
  std::uint32_t ordinal{0U};
  ImageAxes axes{};
  ImageCoordinate coordinate{};
  ImageDisplaySettings display_settings{};
};

[[nodiscard]] bool image_arrshow_component_supported(ArrShowDisplayComponent component,
                                                     ksj::ismrmrd::ImageDataType data_type) noexcept;

[[nodiscard]] QString image_dimension_identifier(ImageDimension dimension);

struct ImagePresentation {
  QString summary;
  QImage image;
  ArrShowDisplayComponent component{ArrShowDisplayComponent::complex};
  ArrShowDisplaySettings display_settings{};
  std::array<std::uint16_t, kImageDimensionCount> source_dimensions{};
  ImageAxes axes{};
  ImageCoordinate coordinate{};
  double source_minimum{0.0};
  double source_maximum{0.0};
  double applied_window_center{0.0};
  double applied_window_width{0.0};
  ArrShowWindowPersistence window_persistence{ArrShowWindowPersistence::relative};
  QStringList csv_columns;
  QList<QStringList> csv_rows;
  QJsonObject details;
};

// The pipeline graph is a bounded, parse-only projection of an authored
// PipelineDefinition. It intentionally has no resolved Provider contract,
// resource, scheduling, or runtime state.
enum class PipelineGraphNodeKind {
  ingress,
  operator_node,
  egress,
};

enum class PipelineGraphEdgeKind {
  ingress,
  data,
  egress,
  calibration,
};

struct PipelineGraphNode {
  QString key;
  PipelineGraphNodeKind kind{PipelineGraphNodeKind::operator_node};
  QString title;
  QString detail;
};

struct PipelineGraphEdge {
  QString id;
  PipelineGraphEdgeKind kind{PipelineGraphEdgeKind::data};
  QString source_key;
  QString source_port;
  QString target_key;
  QString target_port;
};

struct PipelinePresentation {
  QString summary;
  QString canonical_json;
  QList<PipelineGraphNode> graph_nodes;
  QList<PipelineGraphEdge> graph_edges;
  QStringList csv_columns;
  QList<QStringList> csv_rows;
  QJsonObject details;
};

[[nodiscard]] MetadataPresentation make_metadata_presentation(const InspectionSession& session);

// Lists only standard-flag membership kinds that resolve to a bounded raw
// Cartesian coordinate catalog without materializing payloads. Imaging is
// first when renderable; otherwise the first renderable auxiliary type is the
// UI default. Counts can overlap when one source record has multiple auxiliary
// flags.
[[nodiscard]] bool cartesian_kspace_acquisition_kind_options(InspectionSession& session,
                                                             QList<CartesianKspaceAcquisitionKindOption>& options,
                                                             QString& error);

[[nodiscard]] QString cartesian_kspace_dimension_identifier(CartesianKspaceDimension dimension);

// Discovers all actual coordinate values and compatible source tuples for one
// standard acquisition kind. Imaging data retains the XML phase-encode hard
// bound; an explicitly selected auxiliary kind uses its observed range. It
// still rejects non-Cartesian acquisitions rather than presenting trajectory
// samples as a Cartesian matrix.
[[nodiscard]] bool cartesian_kspace_catalog(InspectionSession& session, CartesianKspaceAcquisitionKind acquisition_kind,
                                            CartesianKspaceCatalog& catalog, QString& error);

// Resolves a requested coordinate to one supported by the sparse catalog.
// `changed_dimension` is the value a user just selected and must be preserved
// when possible; an axis itself is never fixed. This is pure header metadata
// resolution and never reads a payload.
[[nodiscard]] bool resolve_cartesian_kspace_coordinate(const CartesianKspaceCatalog& catalog, CartesianKspaceAxes axes,
                                                       CartesianKspaceCoordinate requested,
                                                       std::optional<CartesianKspaceDimension> changed_dimension,
                                                       CartesianKspaceCoordinate& resolved, QString& error);

// Builds a bounded raw Cartesian ISMRMRD grid along request.axes.x ×
// request.axes.y. Imaging data retains the XML ky hard bound; all non-axis
// coordinates are matched only against actually observed source tuples. No
// FFT, gridding, RSS, log transform, or reconstruction is performed.
[[nodiscard]] bool make_cartesian_kspace_presentation(InspectionSession& session, CartesianKspaceRequest request,
                                                      KspacePresentation& presentation, QString& error);

// Builds one bounded native ISMRMRD image X × Y plane. The request carries
// X/Y explicitly so the shared presentation representation stays uniform,
// but any alternate plane is rejected; Z and channel come from
// `request.coordinate`.
[[nodiscard]] bool make_image_presentation(InspectionSession& session, ImageRequest request,
                                           ImagePresentation& presentation, QString& error);

[[nodiscard]] bool load_pipeline_presentation(const QString& file_path, PipelinePresentation& presentation,
                                              QString& error);

} // namespace ksj::viewer
