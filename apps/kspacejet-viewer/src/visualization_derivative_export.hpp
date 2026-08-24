#pragma once

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace ksj::viewer {

// These formats describe local, derived visualizations only. They must never
// be interpreted as ISMRMRD image artifacts or reconstruction output.
enum class VisualizationExportFormat {
  png,
  svg,
  csv,
  json,
};

// The presentation layer is responsible for bounding every image and table
// before it creates this value. The exporter writes those display derivatives
// as-is and does not read, reconstruct, or manufacture MRI payloads.
struct VisualizationDerivative {
  QString view_name;
  QString source_description;
  QImage image;
  QStringList csv_columns;
  QList<QStringList> csv_rows;
  QJsonObject details;
};

// Returns the required filename suffix, including its leading dot. An invalid
// enum value returns an empty string.
[[nodiscard]] QString visualization_export_extension(VisualizationExportFormat format);

// Publishes a local display derivative atomically. `error` is cleared on
// success and receives a deterministic diagnostic on failure.
[[nodiscard]] bool export_visualization_derivative(const VisualizationDerivative& derivative,
                                                   const QString& destination, VisualizationExportFormat format,
                                                   QString& error);

} // namespace ksj::viewer
