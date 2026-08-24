#include "visualization_derivative_export.hpp"

#include <QByteArray>
#include <QBuffer>
#include <QFileInfo>
#include <QImageWriter>
#include <QIODevice>
#include <QJsonDocument>
#include <QSaveFile>

namespace {

using ksj::viewer::VisualizationDerivative;
using ksj::viewer::VisualizationExportFormat;

constexpr auto kArtifactKind = "visualization-derivative";

[[nodiscard]] bool is_mri_artifact_extension(const QString& extension) {
  return extension.compare(QStringLiteral("mrd"), Qt::CaseInsensitive) == 0 ||
         extension.compare(QStringLiteral("h5"), Qt::CaseInsensitive) == 0 ||
         extension.compare(QStringLiteral("hdf5"), Qt::CaseInsensitive) == 0 ||
         extension.compare(QStringLiteral("ismrmrd"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] QString xml_escape(QString value) {
  value.replace(u'&', QStringLiteral("&amp;"));
  value.replace(u'<', QStringLiteral("&lt;"));
  value.replace(u'>', QStringLiteral("&gt;"));
  value.replace(u'\'', QStringLiteral("&apos;"));
  value.replace(u'\"', QStringLiteral("&quot;"));
  return value;
}

[[nodiscard]] QString csv_field(QString value) {
  value.replace(u'\"', QStringLiteral("\"\""));
  return QStringLiteral("\"") + value + QStringLiteral("\"");
}

void append_csv_row(QByteArray& output, const QStringList& row) {
  for (qsizetype index = 0; index < row.size(); ++index) {
    if (index != 0) {
      output.append(',');
    }
    output.append(csv_field(row.at(index)).toUtf8());
  }
  output.append('\n');
}

[[nodiscard]] bool publish_bytes(const QString& destination, const QByteArray& contents, QString& error) {
  QSaveFile output(destination);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly)) {
    error = QStringLiteral("Could not open the visualization export destination.");
    return false;
  }

  if (output.write(contents) != static_cast<qint64>(contents.size())) {
    output.cancelWriting();
    error = QStringLiteral("Could not write the visualization derivative.");
    return false;
  }

  if (!output.commit()) {
    error = QStringLiteral("Could not atomically publish the visualization derivative.");
    return false;
  }

  return true;
}

[[nodiscard]] bool write_png(const VisualizationDerivative& derivative, const QString& destination, QString& error) {
  QSaveFile output(destination);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly)) {
    error = QStringLiteral("Could not open the visualization export destination.");
    return false;
  }

  QImageWriter writer(&output, QByteArrayLiteral("png"));
  writer.setText(QStringLiteral("KSpaceJet.ArtifactKind"), QString::fromLatin1(kArtifactKind));
  writer.setText(QStringLiteral("KSpaceJet.ViewName"), derivative.view_name);
  writer.setText(QStringLiteral("KSpaceJet.SourceDescription"), derivative.source_description);
  if (!writer.canWrite() || !writer.write(derivative.image)) {
    output.cancelWriting();
    error = QStringLiteral("Could not write the PNG visualization derivative.");
    return false;
  }

  if (!output.commit()) {
    error = QStringLiteral("Could not atomically publish the visualization derivative.");
    return false;
  }

  return true;
}

[[nodiscard]] bool encode_png_data_uri(const VisualizationDerivative& derivative, QByteArray& result, QString& error) {
  QBuffer buffer(&result);
  if (!buffer.open(QIODevice::WriteOnly)) {
    error = QStringLiteral("Could not encode the SVG visualization derivative image.");
    return false;
  }

  QImageWriter writer(&buffer, QByteArrayLiteral("png"));
  writer.setText(QStringLiteral("KSpaceJet.ArtifactKind"), QString::fromLatin1(kArtifactKind));
  writer.setText(QStringLiteral("KSpaceJet.ViewName"), derivative.view_name);
  writer.setText(QStringLiteral("KSpaceJet.SourceDescription"), derivative.source_description);
  if (!writer.canWrite() || !writer.write(derivative.image)) {
    error = QStringLiteral("Could not encode the SVG visualization derivative image.");
    return false;
  }

  return true;
}

[[nodiscard]] bool write_svg(const VisualizationDerivative& derivative, const QString& destination, QString& error) {
  QByteArray encoded_image;
  if (!encode_png_data_uri(derivative, encoded_image, error)) {
    return false;
  }

  const auto view_name = xml_escape(derivative.view_name);
  const auto source_description = xml_escape(derivative.source_description);
  const auto width = QString::number(derivative.image.width());
  const auto height = QString::number(derivative.image.height());
  const auto image_data = QString::fromLatin1(encoded_image.toBase64());
  const auto document = QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="%1" height="%2" viewBox="0 0 %1 %2">
  <title>KSpaceJet visualization derivative</title>
  <metadata>
    <ksj:provenance xmlns:ksj="https://kspacejet.org/ns/visualization-derivative">
      <ksj:field name="KSpaceJet.ArtifactKind">visualization-derivative</ksj:field>
      <ksj:field name="KSpaceJet.ViewName">%3</ksj:field>
      <ksj:field name="KSpaceJet.SourceDescription">%4</ksj:field>
    </ksj:provenance>
  </metadata>
  <image width="%1" height="%2" href="data:image/png;base64,%5"/>
</svg>
)")
                          .arg(width, height, view_name, source_description, image_data)
                          .toUtf8();
  return publish_bytes(destination, document, error);
}

[[nodiscard]] bool write_csv(const VisualizationDerivative& derivative, const QString& destination, QString& error) {
  if (!derivative.csv_rows.isEmpty() && derivative.csv_columns.isEmpty()) {
    error = QStringLiteral("CSV visualization export rows require column names.");
    return false;
  }
  for (const auto& row : derivative.csv_rows) {
    if (row.size() != derivative.csv_columns.size()) {
      error = QStringLiteral("CSV visualization export rows must match the column count.");
      return false;
    }
  }

  QByteArray document;
  document += "# KSpaceJet.ArtifactKind=visualization-derivative\n";
  document += "# KSpaceJet.ViewName=";
  document += csv_field(derivative.view_name).toUtf8();
  document += '\n';
  document += "# KSpaceJet.SourceDescription=";
  document += csv_field(derivative.source_description).toUtf8();
  document += '\n';
  if (!derivative.csv_columns.isEmpty()) {
    append_csv_row(document, derivative.csv_columns);
    for (const auto& row : derivative.csv_rows) {
      append_csv_row(document, row);
    }
  }

  return publish_bytes(destination, document, error);
}

[[nodiscard]] bool write_json(const VisualizationDerivative& derivative, const QString& destination, QString& error) {
  QJsonObject document;
  document.insert(QStringLiteral("schema"), QStringLiteral("ksj.visualization-derivative"));
  document.insert(QStringLiteral("artifact_kind"), QString::fromLatin1(kArtifactKind));
  document.insert(QStringLiteral("view_name"), derivative.view_name);
  document.insert(QStringLiteral("source_description"), derivative.source_description);
  document.insert(QStringLiteral("details"), derivative.details);
  return publish_bytes(destination, QJsonDocument(document).toJson(QJsonDocument::Indented), error);
}

[[nodiscard]] bool validate_destination(const QString& destination, const VisualizationExportFormat format,
                                        QString& error) {
  if (destination.isEmpty()) {
    error = QStringLiteral("The export destination must not be empty.");
    return false;
  }

  const auto suffix = QFileInfo(destination).suffix();
  if (is_mri_artifact_extension(suffix)) {
    error = QStringLiteral("MRI artifact extensions are not valid visualization export destinations.");
    return false;
  }

  const auto expected_extension = ksj::viewer::visualization_export_extension(format);
  if (expected_extension.isEmpty()) {
    error = QStringLiteral("The visualization export format is not supported.");
    return false;
  }

  if (suffix.compare(expected_extension.mid(1), Qt::CaseInsensitive) != 0) {
    error = QStringLiteral("The export destination must use the %1 extension.").arg(expected_extension);
    return false;
  }

  return true;
}

} // namespace

namespace ksj::viewer {

QString visualization_export_extension(const VisualizationExportFormat format) {
  switch (format) {
    case VisualizationExportFormat::png:
      return QStringLiteral(".png");
    case VisualizationExportFormat::svg:
      return QStringLiteral(".svg");
    case VisualizationExportFormat::csv:
      return QStringLiteral(".csv");
    case VisualizationExportFormat::json:
      return QStringLiteral(".json");
  }
  return {};
}

bool export_visualization_derivative(const VisualizationDerivative& derivative, const QString& destination,
                                     const VisualizationExportFormat format, QString& error) {
  error.clear();
  if (!validate_destination(destination, format, error)) {
    return false;
  }

  if ((format == VisualizationExportFormat::png || format == VisualizationExportFormat::svg) &&
      derivative.image.isNull()) {
    error = QStringLiteral("PNG and SVG visualization exports require a non-null image.");
    return false;
  }

  switch (format) {
    case VisualizationExportFormat::png:
      return write_png(derivative, destination, error);
    case VisualizationExportFormat::svg:
      return write_svg(derivative, destination, error);
    case VisualizationExportFormat::csv:
      return write_csv(derivative, destination, error);
    case VisualizationExportFormat::json:
      return write_json(derivative, destination, error);
  }

  error = QStringLiteral("The visualization export format is not supported.");
  return false;
}

} // namespace ksj::viewer
