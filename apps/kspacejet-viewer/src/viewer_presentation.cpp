#include "viewer_presentation.hpp"

#include "kspacejet/recon/execution_profile.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"

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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ksj::ismrmrd::ImageDataType;
using ksj::ismrmrd::ImagePixelsView;

constexpr int kKspaceMaximumWidth = 1'024;
constexpr int kKspaceMaximumHeight = 256;
constexpr int kImageMaximumDimension = 2'048;
constexpr std::size_t kImageMaximumPixels = 2U * 1024U * 1024U;
constexpr std::size_t kMaximumExportRows = 4'096U;
constexpr qsizetype kMaximumXmlPreviewCharacters = 128U * 1024U;
constexpr qsizetype kMaximumMetadataValuePreviewCharacters = 256U;

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

struct DisplayWindow {
  double center{0.0};
  double width{0.0};
};

struct AppliedDisplayWindow {
  double source_minimum{0.0};
  double source_maximum{0.0};
  double center{0.0};
  double width{0.0};
};

[[nodiscard]] QImage render_magnitudes(const std::vector<double>& magnitudes, const DisplayExtent extent,
                                       const std::optional<DisplayWindow> requested_window,
                                       AppliedDisplayWindow* applied_window, QString& error) {
  error.clear();
  std::size_t expected_count = 0U;
  if (extent.width <= 0 || extent.height <= 0 ||
      !checked_multiply(static_cast<std::size_t>(extent.width), static_cast<std::size_t>(extent.height),
                        expected_count) ||
      magnitudes.size() != expected_count) {
    error = QStringLiteral("display magnitude projection has an invalid bounded extent");
    return {};
  }

  QImage image(extent.width, extent.height, QImage::Format_Grayscale8);
  if (image.isNull()) {
    error = QStringLiteral("Qt could not allocate the bounded grayscale display image");
    return {};
  }

  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();
  for (const auto magnitude : magnitudes) {
    if (std::isfinite(magnitude)) {
      minimum = std::min(minimum, magnitude);
      maximum = std::max(maximum, magnitude);
    }
  }
  const auto has_finite_values = std::isfinite(minimum) && std::isfinite(maximum);
  const auto has_range = has_finite_values && maximum > minimum;
  auto window_center = 0.0;
  auto window_width = 0.0;
  auto low = 0.0;
  auto high = 0.0;
  if (requested_window.has_value()) {
    window_center = requested_window->center;
    window_width = requested_window->width;
    if (!std::isfinite(window_center) || !std::isfinite(window_width) || window_width <= 0.0) {
      error = QStringLiteral("image window width must be finite and greater than zero");
      return {};
    }
    const auto half_width = window_width * 0.5;
    low = window_center - half_width;
    high = window_center + half_width;
    if (!std::isfinite(low) || !std::isfinite(high) || high <= low) {
      error = QStringLiteral("image window center and width are outside the supported display range");
      return {};
    }
  } else if (has_finite_values) {
    window_center = minimum + (maximum - minimum) * 0.5;
    window_width = maximum - minimum;
    low = minimum;
    high = maximum;
  }

  if (applied_window != nullptr) {
    *applied_window = {.source_minimum = has_finite_values ? minimum : 0.0,
                       .source_maximum = has_finite_values ? maximum : 0.0,
                       .center = window_center,
                       .width = window_width};
  }

  for (int y = 0; y < extent.height; ++y) {
    auto* line = image.scanLine(y);
    for (int x = 0; x < extent.width; ++x) {
      const auto index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(extent.width) + static_cast<std::size_t>(x);
      const auto magnitude = magnitudes[index];
      auto normalized = 0.0;
      if (std::isfinite(magnitude) && high > low) {
        normalized = (magnitude - low) / (high - low);
      } else if (std::isfinite(magnitude) && !requested_window.has_value() && maximum > 0.0) {
        normalized = 1.0;
      }
      line[x] = static_cast<uchar>(std::clamp(normalized, 0.0, 1.0) * 255.0);
    }
  }
  return image;
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

[[nodiscard]] double image_magnitude(const ImagePixelsView& view, const std::size_t index) {
  switch (view.data_type) {
    case ImageDataType::unsigned_integer_16:
      return static_cast<double>(read_image_value<std::uint16_t>(view, index));
    case ImageDataType::signed_integer_16:
      return std::abs(static_cast<double>(read_image_value<std::int16_t>(view, index)));
    case ImageDataType::unsigned_integer_32:
      return static_cast<double>(read_image_value<std::uint32_t>(view, index));
    case ImageDataType::signed_integer_32:
      return std::abs(static_cast<double>(read_image_value<std::int32_t>(view, index)));
    case ImageDataType::real_32:
      return std::abs(static_cast<double>(read_image_value<float>(view, index)));
    case ImageDataType::real_64:
      return std::abs(read_image_value<double>(view, index));
    case ImageDataType::complex_32:
      return static_cast<double>(std::abs(read_image_value<std::complex<float>>(view, index)));
    case ImageDataType::complex_64:
      return std::abs(read_image_value<std::complex<double>>(view, index));
  }
  return 0.0;
}

[[nodiscard]] QJsonArray dimensions_to_json(const std::array<std::uint16_t, 4>& dimensions) {
  QJsonArray result;
  for (const auto dimension : dimensions) {
    result.append(static_cast<int>(dimension));
  }
  return result;
}

[[nodiscard]] QJsonObject image_details(const ksj::ismrmrd::InspectionImageRecord& record,
                                        const std::array<std::uint16_t, 4>& dimensions, const std::uint16_t z_index,
                                        const std::uint16_t channel_index, const QString& source) {
  QJsonObject result;
  result.insert(QStringLiteral("artifact_kind"), QStringLiteral("visualization-derivative"));
  result.insert(QStringLiteral("view"), QStringLiteral("image"));
  result.insert(QStringLiteral("source"), source);
  result.insert(QStringLiteral("series_id"), to_qstring(record.locator.series_id));
  result.insert(QStringLiteral("ordinal"), static_cast<int>(record.locator.ordinal));
  result.insert(QStringLiteral("data_type"), QString::fromLatin1(image_data_type_name(record.header.data_type)));
  result.insert(QStringLiteral("dimensions"), dimensions_to_json(dimensions));
  result.insert(QStringLiteral("z_index"), static_cast<int>(z_index));
  result.insert(QStringLiteral("channel_index"), static_cast<int>(channel_index));
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
                                    const std::array<std::uint16_t, 4>& dimensions, const std::uint16_t z_index,
                                    const std::uint16_t channel_index) {
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
        << QStringLiteral("Displayed plane: z=%1, channel=%2; image index=%3, series index=%4")
             .arg(z_index)
             .arg(channel_index)
             .arg(record.header.image_index)
             .arg(record.header.image_series_index);
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

} // namespace

namespace ksj::viewer {

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

bool make_kspace_presentation(InspectionSession& session, const std::uint32_t ordinal, KspacePresentation& presentation,
                              QString& error) {
  presentation = {};
  error.clear();
  if (!session.is_open()) {
    error = QStringLiteral("open a standard ISMRMRD dataset before inspecting an acquisition");
    return false;
  }

  QString callback_error;
  std::string reader_error;
  const auto result = session.reader().visit_acquisition(
    ordinal,
    [&session, ordinal, &presentation, &callback_error](const ksj::ismrmrd::InspectionAcquisitionView& acquisition) {
      const auto sample_count = static_cast<std::size_t>(acquisition.header.number_of_samples);
      const auto channel_count = static_cast<std::size_t>(acquisition.header.active_channels);
      std::size_t expected_samples = 0U;
      if (sample_count == 0U || channel_count == 0U ||
          !checked_multiply(sample_count, channel_count, expected_samples) ||
          acquisition.samples.size() != expected_samples) {
        callback_error = QStringLiteral("ISMRMRD acquisition has an invalid sample/channel shape");
        return false;
      }

      const auto extent = make_display_extent(sample_count, channel_count, kKspaceMaximumWidth,
                                              static_cast<std::size_t>(kKspaceMaximumWidth) *
                                                static_cast<std::size_t>(kKspaceMaximumHeight));
      if (!extent.has_value()) {
        callback_error = QStringLiteral("ISMRMRD acquisition cannot produce a bounded display extent");
        return false;
      }

      std::vector<double> magnitudes;
      magnitudes.reserve(static_cast<std::size_t>(extent->width) * static_cast<std::size_t>(extent->height));
      presentation.csv_columns = {QStringLiteral("sample"), QStringLiteral("channel"), QStringLiteral("magnitude"),
                                  QStringLiteral("real"), QStringLiteral("imaginary")};
      for (int display_channel = 0; display_channel < extent->height; ++display_channel) {
        const auto channel = source_index_for_display(display_channel, extent->height, channel_count);
        for (int display_sample = 0; display_sample < extent->width; ++display_sample) {
          const auto sample = source_index_for_display(display_sample, extent->width, sample_count);
          const auto value = acquisition.samples[sample + channel * sample_count];
          magnitudes.push_back(static_cast<double>(std::abs(value)));
          if (presentation.csv_rows.size() < static_cast<qsizetype>(kMaximumExportRows)) {
            presentation.csv_rows.append(
              {QString::number(sample), QString::number(channel), QString::number(std::abs(value), 'g', 12),
               QString::number(value.real(), 'g', 12), QString::number(value.imag(), 'g', 12)});
          }
        }
      }

      QString render_error;
      presentation.image = render_magnitudes(magnitudes, *extent, std::nullopt, nullptr, render_error);
      if (presentation.image.isNull()) {
        callback_error = render_error;
        return false;
      }
      presentation.summary =
        QStringLiteral("Acquisition %1: %2 samples × %3 active channels; magnitude projection only, not a "
                       "reconstructed image.")
          .arg(ordinal)
          .arg(sample_count)
          .arg(channel_count);
      presentation.details.insert(QStringLiteral("artifact_kind"), QStringLiteral("visualization-derivative"));
      presentation.details.insert(QStringLiteral("view"), QStringLiteral("k-space"));
      presentation.details.insert(QStringLiteral("representation"),
                                  QStringLiteral("acquisition magnitude projection; not reconstructed image"));
      presentation.details.insert(QStringLiteral("source"), source_description(session));
      presentation.details.insert(QStringLiteral("acquisition_ordinal"), static_cast<int>(ordinal));
      presentation.details.insert(QStringLiteral("number_of_samples"), static_cast<int>(sample_count));
      presentation.details.insert(QStringLiteral("active_channels"), static_cast<int>(channel_count));
      presentation.details.insert(QStringLiteral("trajectory_dimensions"),
                                  static_cast<int>(acquisition.header.trajectory_dimensions));
      presentation.details.insert(QStringLiteral("display_width"), extent->width);
      presentation.details.insert(QStringLiteral("display_height"), extent->height);
      return true;
    },
    reader_error);

  if (!callback_error.isEmpty()) {
    error = callback_error;
    return false;
  }
  if (result != ksj::ismrmrd::InspectionIterationResult::completed) {
    error = reader_error.empty() ? QStringLiteral("ISMRMRD acquisition inspection did not complete")
                                 : to_qstring(reader_error);
    return false;
  }
  return true;
}

bool make_image_presentation(InspectionSession& session, const QString& series_id, const std::uint32_t ordinal,
                             const std::uint16_t z_index, const std::uint16_t channel_index,
                             const ImageDisplaySettings display_settings, ImagePresentation& presentation,
                             QString& error) {
  presentation = {};
  error.clear();
  if (!session.is_open()) {
    error = QStringLiteral("open a standard ISMRMRD dataset before inspecting an image");
    return false;
  }
  if (series_id.trimmed().isEmpty()) {
    error = QStringLiteral("an ISMRMRD image series is required");
    return false;
  }
  if (!display_settings.auto_window &&
      (!std::isfinite(display_settings.window_center) || !std::isfinite(display_settings.window_width) ||
       display_settings.window_width <= 0.0)) {
    error = QStringLiteral("image window width must be finite and greater than zero");
    return false;
  }

  const auto utf8_series = series_id.trimmed().toUtf8();
  const ksj::ismrmrd::ImageLocator locator{
    .series_id = std::string(utf8_series.constData(), static_cast<std::size_t>(utf8_series.size())),
    .ordinal = ordinal};
  const auto requested_window = display_settings.auto_window
                                  ? std::optional<DisplayWindow>{}
                                  : std::optional<DisplayWindow>{{.center = display_settings.window_center,
                                                                  .width = display_settings.window_width}};

  QString callback_error;
  std::string reader_error;
  const auto result = session.reader().with_image_pixels(
    locator,
    [&session, z_index, channel_index, requested_window, &presentation,
     &callback_error](const ksj::ismrmrd::InspectionImageRecord& record, const ImagePixelsView& pixels) {
      const auto dimensions = pixels.dimensions;
      if (dimensions[0] == 0U || dimensions[1] == 0U || dimensions[2] == 0U || dimensions[3] == 0U ||
          z_index >= dimensions[2] || channel_index >= dimensions[3]) {
        callback_error = QStringLiteral("the requested ISMRMRD image z/channel index is outside its standard axes");
        return false;
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

      const auto extent =
        make_display_extent(dimensions[0], dimensions[1], kImageMaximumDimension, kImageMaximumPixels);
      if (!extent.has_value()) {
        callback_error = QStringLiteral("ISMRMRD image cannot produce a bounded display extent");
        return false;
      }

      std::vector<double> magnitudes;
      magnitudes.reserve(static_cast<std::size_t>(extent->width) * static_cast<std::size_t>(extent->height));
      presentation.csv_columns = {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("magnitude")};
      for (int display_y = 0; display_y < extent->height; ++display_y) {
        const auto y = source_index_for_display(display_y, extent->height, dimensions[1]);
        for (int display_x = 0; display_x < extent->width; ++display_x) {
          const auto x = source_index_for_display(display_x, extent->width, dimensions[0]);
          const auto index =
            x + static_cast<std::size_t>(dimensions[0]) *
                  (y + static_cast<std::size_t>(dimensions[1]) *
                         (static_cast<std::size_t>(z_index) +
                          static_cast<std::size_t>(dimensions[2]) * static_cast<std::size_t>(channel_index)));
          const auto magnitude = image_magnitude(pixels, index);
          magnitudes.push_back(magnitude);
          if (presentation.csv_rows.size() < static_cast<qsizetype>(kMaximumExportRows)) {
            presentation.csv_rows.append({QString::number(x), QString::number(y), QString::number(magnitude, 'g', 12)});
          }
        }
      }

      QString render_error;
      AppliedDisplayWindow applied_window;
      presentation.image = render_magnitudes(magnitudes, *extent, requested_window, &applied_window, render_error);
      if (presentation.image.isNull()) {
        callback_error = render_error;
        return false;
      }
      presentation.dimensions = dimensions;
      presentation.source_minimum = applied_window.source_minimum;
      presentation.source_maximum = applied_window.source_maximum;
      presentation.applied_window_center = applied_window.center;
      presentation.applied_window_width = applied_window.width;
      presentation.auto_window = !requested_window.has_value();
      presentation.summary = image_summary(record, dimensions, z_index, channel_index);
      presentation.details = image_details(record, dimensions, z_index, channel_index, source_description(session));
      presentation.details.insert(QStringLiteral("display_width"), extent->width);
      presentation.details.insert(QStringLiteral("display_height"), extent->height);
      presentation.details.insert(QStringLiteral("source_minimum"), applied_window.source_minimum);
      presentation.details.insert(QStringLiteral("source_maximum"), applied_window.source_maximum);
      presentation.details.insert(QStringLiteral("window_mode"),
                                  requested_window.has_value() ? QStringLiteral("manual") : QStringLiteral("auto"));
      presentation.details.insert(QStringLiteral("window_center"), applied_window.center);
      presentation.details.insert(QStringLiteral("window_width"), applied_window.width);
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

  presentation.details.insert(QStringLiteral("artifact_kind"), QStringLiteral("visualization-derivative"));
  presentation.details.insert(QStringLiteral("view"), QStringLiteral("pipeline"));
  presentation.details.insert(QStringLiteral("source"), trimmed_path);
  presentation.details.insert(QStringLiteral("pipeline_id"), to_qstring(pipeline.id()));
  presentation.details.insert(QStringLiteral("display_name"), to_qstring(pipeline.display_name()));
  presentation.details.insert(QStringLiteral("input_profile"), QStringLiteral("ismrmrd-hdf5"));
  presentation.details.insert(QStringLiteral("dataset_group"), to_qstring(pipeline.input_profile().dataset_group));
  presentation.details.insert(QStringLiteral("artifact_digest"), to_qstring(pipeline.artifact_digest().value()));
  presentation.details.insert(QStringLiteral("allowed_profiles"), profiles);
  presentation.details.insert(QStringLiteral("parameters"), parameters);
  presentation.details.insert(QStringLiteral("nodes"), nodes);
  presentation.details.insert(QStringLiteral("edge_count"), static_cast<int>(pipeline.edges().size()));
  presentation.details.insert(QStringLiteral("ingress_count"), static_cast<int>(pipeline.ingress_ports().size()));
  presentation.details.insert(QStringLiteral("egress_count"), static_cast<int>(pipeline.egress_ports().size()));
  return true;
}

} // namespace ksj::viewer
