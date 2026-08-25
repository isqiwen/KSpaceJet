#pragma once

#include "inspection_session.hpp"

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>

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
  QStringList csv_columns;
  QList<QStringList> csv_rows;
  QJsonObject details;
};

// The selected acquisition supplies the non-planar ISMRMRD frame counters
// (encoding, slice, contrast, phase, repetition, etc.) for a raw Cartesian
// k-space plane. A negative coil channel requests RSS across every active
// coil. This is a display request only: it neither edits nor retains source
// acquisitions.
struct CartesianKspaceRequest {
  std::uint32_t reference_acquisition_ordinal{0U};
  std::int32_t coil_channel{-1};
};

// Window/level is applied only while producing one bounded display derivative.
// It does not retain source pixels or modify the standard ISMRMRD artifact.
struct ImageDisplaySettings {
  bool auto_window{true};
  double window_center{0.0};
  double window_width{0.0};
};

struct ImagePresentation {
  QString summary;
  QImage image;
  std::array<std::uint16_t, 4> dimensions{};
  double source_minimum{0.0};
  double source_maximum{0.0};
  double applied_window_center{0.0};
  double applied_window_width{0.0};
  bool auto_window{true};
  QStringList csv_columns;
  QList<QStringList> csv_rows;
  QJsonObject details;
};

struct PipelinePresentation {
  QString summary;
  QString canonical_json;
  QStringList csv_columns;
  QList<QStringList> csv_rows;
  QJsonObject details;
};

[[nodiscard]] MetadataPresentation make_metadata_presentation(const InspectionSession& session);

// Builds a bounded raw Cartesian ISMRMRD acquisition grid. The x axis is the
// centered readout coordinate and y is kspace_encode_step_1. It deliberately
// rejects non-Cartesian acquisitions rather than presenting their trajectory
// samples as though they were a Cartesian matrix.
[[nodiscard]] bool make_cartesian_kspace_presentation(InspectionSession& session, CartesianKspaceRequest request,
                                                      KspacePresentation& presentation, QString& error);

[[nodiscard]] bool make_image_presentation(InspectionSession& session, const QString& series_id, std::uint32_t ordinal,
                                           std::uint16_t z_index, std::uint16_t channel_index,
                                           ImageDisplaySettings display_settings, ImagePresentation& presentation,
                                           QString& error);

[[nodiscard]] bool load_pipeline_presentation(const QString& file_path, PipelinePresentation& presentation,
                                              QString& error);

} // namespace ksj::viewer
