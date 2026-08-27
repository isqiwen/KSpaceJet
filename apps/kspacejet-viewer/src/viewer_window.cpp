#include "viewer_window.hpp"

#include "arrshow_dimension_controls.hpp"
#include "inspection_canvas.hpp"

#include "kspacejet/logging/logging.hpp"

#include <QAction>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QByteArray>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QKeySequence>
#include <QLineF>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSizeF>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QToolButton>
#include <QToolBar>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr auto kViewerSettingsOrganization = "KSpaceJet";
constexpr auto kViewerSettingsApplication = "ksj-viewer";
constexpr auto kLastOpenDirectorySettingsKey = "file_dialogs/last_open_directory";
constexpr auto kRecentFilesSettingsKey = "file_dialogs/recent_files";
constexpr qsizetype kMaximumRecentFiles = 5;

[[nodiscard]] QSettings viewer_settings() {
  return {QSettings::IniFormat, QSettings::UserScope, QString::fromLatin1(kViewerSettingsOrganization),
          QString::fromLatin1(kViewerSettingsApplication)};
}

const QSize kMaximumUiPixmapSize{1600, 1200};
constexpr qsizetype kMaximumPipelineGraphSemanticItems = 512U;
constexpr qreal kPipelineGraphNodeWidth = 250.0;
constexpr qreal kPipelineGraphNodeHeight = 88.0;
constexpr qreal kPipelineGraphHorizontalGap = 110.0;
constexpr qreal kPipelineGraphVerticalGap = 42.0;
constexpr qreal kPipelineGraphMargin = 36.0;
constexpr qsizetype kMaximumXmlOutlineNodes = 2'048U;
constexpr qsizetype kMaximumXmlOutlineValueCharacters = 120U;
constexpr int kMaximumXmlFormattingDepth = 64;
constexpr qsizetype kMaximumXmlFormattingElements = 4'096U;

class XmlSyntaxHighlighter final : public QSyntaxHighlighter {
public:
  explicit XmlSyntaxHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}

protected:
  void highlightBlock(const QString& text) override {
    static const QRegularExpression instruction_expression{QStringLiteral(R"(<\?.*\?>)")};
    static const QRegularExpression tag_expression{QStringLiteral(R"(</?[\w:.-]+)")};
    static const QRegularExpression attribute_expression{QStringLiteral(R"(\b[\w:.-]+(?=\s*=))")};
    static const QRegularExpression value_expression{QStringLiteral(R"("[^"]*"|'[^']*')")};
    static const QRegularExpression comment_expression{QStringLiteral(R"(<!--.*-->)")};

    QTextCharFormat instruction_format;
    instruction_format.setForeground(QColor{QStringLiteral("#8050a0")});
    QTextCharFormat tag_format;
    tag_format.setForeground(QColor{QStringLiteral("#1a5e8a")});
    tag_format.setFontWeight(QFont::DemiBold);
    QTextCharFormat attribute_format;
    attribute_format.setForeground(QColor{QStringLiteral("#765b00")});
    QTextCharFormat value_format;
    value_format.setForeground(QColor{QStringLiteral("#8a2f17")});
    QTextCharFormat comment_format;
    comment_format.setForeground(QColor{QStringLiteral("#6c7b67")});
    comment_format.setFontItalic(true);

    const auto apply = [this, &text](const QRegularExpression& expression, const QTextCharFormat& format) {
      auto match = expression.globalMatch(text);
      while (match.hasNext()) {
        const auto next = match.next();
        setFormat(next.capturedStart(), next.capturedLength(), format);
      }
    };
    apply(instruction_expression, instruction_format);
    apply(tag_expression, tag_format);
    apply(attribute_expression, attribute_format);
    apply(value_expression, value_format);
    apply(comment_expression, comment_format);
  }
};

[[nodiscard]] QString xml_outline_preview(QString value) {
  value = value.simplified();
  if (value.size() <= kMaximumXmlOutlineValueCharacters) {
    return value;
  }
  return QStringLiteral("%1…").arg(value.left(kMaximumXmlOutlineValueCharacters - 1));
}

[[nodiscard]] bool xml_preview_is_safe_to_format(const QString& source) {
  // Pretty-print indentation grows with XML nesting. Keep the display derivative
  // bounded before QXmlStreamWriter allocates formatted text.
  QXmlStreamReader reader(source);
  int depth = 0;
  qsizetype elements = 0U;
  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (token == QXmlStreamReader::StartElement) {
      ++depth;
      ++elements;
      if (depth > kMaximumXmlFormattingDepth || elements > kMaximumXmlFormattingElements) {
        return false;
      }
    } else if (token == QXmlStreamReader::EndElement) {
      depth = std::max(0, depth - 1);
    } else if (token == QXmlStreamReader::Invalid) {
      return false;
    }
  }
  return !reader.hasError();
}

[[nodiscard]] QString format_xml_for_display(const QString& source) {
  if (source.trimmed().isEmpty() || !xml_preview_is_safe_to_format(source)) {
    return source;
  }

  QXmlStreamReader reader(source);
  QString formatted;
  QXmlStreamWriter writer(&formatted);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.tokenType() == QXmlStreamReader::Invalid) {
      break;
    }
    writer.writeCurrentToken(reader);
  }
  return reader.hasError() || writer.hasError() ? source : formatted;
}

void populate_xml_outline(QTreeWidget* outline, const QString& source) {
  if (outline == nullptr) {
    return;
  }

  outline->setUpdatesEnabled(false);
  outline->clear();
  if (source.trimmed().isEmpty()) {
    auto* empty = new QTreeWidgetItem(outline);
    empty->setText(0, QObject::tr("No XML header"));
    outline->setUpdatesEnabled(true);
    return;
  }

  QXmlStreamReader reader(source);
  std::vector<QTreeWidgetItem*> parents;
  qsizetype node_count = 0U;
  bool truncated = false;
  const auto add_item = [&parents, outline, &node_count, &truncated](const QString& name) -> QTreeWidgetItem* {
    if (node_count >= kMaximumXmlOutlineNodes) {
      truncated = true;
      return nullptr;
    }
    auto* item = parents.empty() ? new QTreeWidgetItem(outline) : new QTreeWidgetItem(parents.back());
    item->setText(0, name);
    ++node_count;
    return item;
  };
  const auto append_value = [](QTreeWidgetItem* item, const QString& value) {
    if (item == nullptr || value.isEmpty()) {
      return;
    }
    const auto current = item->text(1);
    const auto combined = current.isEmpty() ? value : QStringLiteral("%1 · %2").arg(current, value);
    item->setText(1, xml_outline_preview(combined));
    item->setToolTip(1, item->text(1));
  };

  while (!reader.atEnd() && !truncated) {
    const auto token = reader.readNext();
    switch (token) {
      case QXmlStreamReader::StartElement:
        {
          auto* item = add_item(reader.qualifiedName().toString());
          if (item == nullptr) {
            break;
          }
          QStringList attributes;
          for (const auto& attribute : reader.attributes()) {
            attributes.append(
              QStringLiteral("%1=\"%2\"")
                .arg(attribute.qualifiedName().toString(), xml_outline_preview(attribute.value().toString())));
          }
          append_value(item, attributes.join(QLatin1Char(' ')));
          parents.push_back(item);
          break;
        }
      case QXmlStreamReader::EndElement:
        if (!parents.empty()) {
          parents.pop_back();
        }
        break;
      case QXmlStreamReader::Characters:
        if (!reader.isWhitespace() && !parents.empty()) {
          append_value(parents.back(), xml_outline_preview(reader.text().toString()));
        }
        break;
      case QXmlStreamReader::Comment:
        {
          auto* item = add_item(QObject::tr("comment"));
          append_value(item, xml_outline_preview(reader.text().toString()));
          break;
        }
      case QXmlStreamReader::ProcessingInstruction:
        {
          auto* item = add_item(QObject::tr("processing instruction"));
          append_value(item, xml_outline_preview(reader.processingInstructionTarget().toString()));
          append_value(item, xml_outline_preview(reader.processingInstructionData().toString()));
          break;
        }
      case QXmlStreamReader::DTD:
        {
          auto* item = add_item(QObject::tr("document type"));
          append_value(item, xml_outline_preview(reader.text().toString()));
          break;
        }
      case QXmlStreamReader::EntityReference:
        if (!parents.empty()) {
          append_value(parents.back(), QStringLiteral("&%1;").arg(reader.name().toString()));
        }
        break;
      case QXmlStreamReader::NoToken:
      case QXmlStreamReader::Invalid:
      case QXmlStreamReader::StartDocument:
      case QXmlStreamReader::EndDocument:
        break;
    }
  }

  if (truncated) {
    auto* indicator = new QTreeWidgetItem(outline);
    indicator->setText(0, QObject::tr("Outline truncated"));
    indicator->setText(1, QObject::tr("Limited to %1 XML nodes").arg(kMaximumXmlOutlineNodes));
  } else if (reader.hasError()) {
    auto* error = new QTreeWidgetItem(outline);
    error->setText(0, QObject::tr("XML parse warning"));
    error->setText(1, reader.errorString());
    error->setToolTip(1, reader.errorString());
  }
  if (outline->topLevelItemCount() == 0) {
    auto* empty = new QTreeWidgetItem(outline);
    empty->setText(0, QObject::tr("No XML elements"));
  }
  outline->expandToDepth(2);
  outline->setUpdatesEnabled(true);
}

[[nodiscard]] QString metadata_xml_summary(const ksj::viewer::InspectionSession& session) {
  const auto& metadata = session.metadata();
  QStringList facts{QObject::tr("Container %1").arg(session.container_path()),
                    QObject::tr("XML %1 bytes").arg(static_cast<qulonglong>(metadata.xml_header.size())),
                    QObject::tr("%1 acquisitions").arg(metadata.acquisition_count),
                    QObject::tr("%1 image series").arg(metadata.image_series.size())};
  return facts.join(QStringLiteral("  ·  "));
}

void log_viewer_diagnostic(const ksj::logging::Level level, const std::string_view operation, const QString& message) {
  const auto message_utf8 = message.toUtf8();
  const std::string_view text{message_utf8.constData(), static_cast<std::size_t>(message_utf8.size())};
  KSJ_LOG(level, "Viewer {}: {}", operation, text);
}

void show_viewer_warning(QWidget* parent, const QString& title, const QString& message,
                         const std::string_view operation) {
  log_viewer_diagnostic(ksj::logging::Level::Warn, operation, message);
  QMessageBox::warning(parent, title, message);
}

void show_viewer_error(QWidget* parent, const QString& title, const QString& message,
                       const std::string_view operation) {
  log_viewer_diagnostic(ksj::logging::Level::Error, operation, message);
  QMessageBox::critical(parent, title, message);
}

enum class SemanticObjectKind : int {
  source_file,
  container,
  header,
  acquisitions,
  images,
  waveforms,
  pipeline,
};

constexpr int kNavigationKindRole = Qt::UserRole;
constexpr int kNavigationContainerRole = Qt::UserRole + 1;
constexpr int kNavigationDefaultViewRole = Qt::UserRole + 2;

[[nodiscard]] SemanticObjectKind semantic_object_kind(const QTreeWidgetItem* item) {
  if (item == nullptr) {
    return SemanticObjectKind::source_file;
  }
  return static_cast<SemanticObjectKind>(item->data(0, kNavigationKindRole).toInt());
}

[[nodiscard]] QString semantic_object_name(const SemanticObjectKind kind) {
  switch (kind) {
    case SemanticObjectKind::source_file:
      return QStringLiteral("ISMRMRD file");
    case SemanticObjectKind::container:
      return QStringLiteral("standard ISMRMRD container");
    case SemanticObjectKind::header:
      return QStringLiteral("ISMRMRD header");
    case SemanticObjectKind::acquisitions:
      return QStringLiteral("ISMRMRD acquisitions");
    case SemanticObjectKind::images:
      return QStringLiteral("ISMRMRD image series");
    case SemanticObjectKind::waveforms:
      return QStringLiteral("ISMRMRD waveforms");
    case SemanticObjectKind::pipeline:
      return QStringLiteral("Pipeline");
  }
  return QStringLiteral("object");
}

[[nodiscard]] ksj::viewer::CartesianKspaceAcquisitionKind
selected_kspace_acquisition_kind(const QComboBox* selector) noexcept {
  using Kind = ksj::viewer::CartesianKspaceAcquisitionKind;
  if (selector == nullptr || selector->currentIndex() < 0) {
    return Kind::imaging;
  }
  const auto encoded_kind = selector->currentData().toInt();
  if (encoded_kind < static_cast<int>(Kind::imaging) || encoded_kind >= static_cast<int>(Kind::count)) {
    return Kind::imaging;
  }
  return static_cast<Kind>(encoded_kind);
}

using KspaceDimension = ksj::viewer::CartesianKspaceDimension;

constexpr std::array kKspaceDimensions{
  KspaceDimension::readout,        KspaceDimension::phase_encode, KspaceDimension::coil,
  KspaceDimension::encoding_space, KspaceDimension::partition,    KspaceDimension::average,
  KspaceDimension::slice,          KspaceDimension::contrast,     KspaceDimension::physiological_phase,
  KspaceDimension::repetition,     KspaceDimension::set,          KspaceDimension::segment,
  KspaceDimension::user_0,         KspaceDimension::user_1,       KspaceDimension::user_2,
  KspaceDimension::user_3,         KspaceDimension::user_4,       KspaceDimension::user_5,
  KspaceDimension::user_6,         KspaceDimension::user_7,
};

[[nodiscard]] QString kspace_dimension_label(const KspaceDimension dimension) {
  switch (dimension) {
    case KspaceDimension::readout:
      return QObject::tr("Readout");
    case KspaceDimension::phase_encode:
      return QObject::tr("Phase encode");
    case KspaceDimension::coil:
      return QObject::tr("Raw coil");
    case KspaceDimension::encoding_space:
      return QObject::tr("Encoding space");
    case KspaceDimension::partition:
      return QObject::tr("Partition");
    case KspaceDimension::average:
      return QObject::tr("Average");
    case KspaceDimension::slice:
      return QObject::tr("Slice");
    case KspaceDimension::contrast:
      return QObject::tr("Contrast");
    case KspaceDimension::physiological_phase:
      return QObject::tr("Physiological phase");
    case KspaceDimension::repetition:
      return QObject::tr("Repetition");
    case KspaceDimension::set:
      return QObject::tr("Set");
    case KspaceDimension::segment:
      return QObject::tr("Segment");
    case KspaceDimension::user_0:
    case KspaceDimension::user_1:
    case KspaceDimension::user_2:
    case KspaceDimension::user_3:
    case KspaceDimension::user_4:
    case KspaceDimension::user_5:
    case KspaceDimension::user_6:
    case KspaceDimension::user_7:
      return QObject::tr("User %1").arg(static_cast<int>(dimension) - static_cast<int>(KspaceDimension::user_0));
    case KspaceDimension::count:
      break;
  }
  return {};
}

[[nodiscard]] QString kspace_dimension_abbreviation(const KspaceDimension dimension) {
  switch (dimension) {
    case KspaceDimension::readout:
      return QStringLiteral("RO");
    case KspaceDimension::phase_encode:
      return QStringLiteral("PE");
    case KspaceDimension::coil:
      return QStringLiteral("Co");
    case KspaceDimension::encoding_space:
      return QStringLiteral("Enc");
    case KspaceDimension::partition:
      return QStringLiteral("Par");
    case KspaceDimension::average:
      return QStringLiteral("Avg");
    case KspaceDimension::slice:
      return QStringLiteral("Slc");
    case KspaceDimension::contrast:
      return QStringLiteral("Con");
    case KspaceDimension::physiological_phase:
      return QStringLiteral("Pha");
    case KspaceDimension::repetition:
      return QStringLiteral("Rep");
    case KspaceDimension::set:
      return QStringLiteral("Set");
    case KspaceDimension::segment:
      return QStringLiteral("Seg");
    case KspaceDimension::user_0:
    case KspaceDimension::user_1:
    case KspaceDimension::user_2:
    case KspaceDimension::user_3:
    case KspaceDimension::user_4:
    case KspaceDimension::user_5:
    case KspaceDimension::user_6:
    case KspaceDimension::user_7:
      return QStringLiteral("U%1").arg(static_cast<int>(dimension) - static_cast<int>(KspaceDimension::user_0));
    case KspaceDimension::count:
      break;
  }
  return {};
}

[[nodiscard]] QString kspace_dimension_tool_tip(const KspaceDimension dimension) {
  switch (dimension) {
    case KspaceDimension::readout:
      return QObject::tr("Centered raw acquisition sample coordinate.");
    case KspaceDimension::phase_encode:
      return QObject::tr("Standard ISMRMRD kspace_encode_step_1 coordinate.");
    case KspaceDimension::coil:
      return QObject::tr("Raw acquisition channel coordinate; no coil combine is applied.");
    case KspaceDimension::encoding_space:
      return QObject::tr("Standard ISMRMRD encoding-space reference.");
    case KspaceDimension::partition:
      return QObject::tr("Standard partition (second phase-encoding) counter.");
    case KspaceDimension::average:
      return QObject::tr("Standard signal-average counter; a protocol may use it for NEX/NSA.");
    case KspaceDimension::slice:
      return QObject::tr("Standard imaging-slice counter.");
    case KspaceDimension::contrast:
      return QObject::tr("Standard contrast counter; a multi-echo source may use it for echo number.");
    case KspaceDimension::physiological_phase:
      return QObject::tr("Standard cardiac/physiological phase counter.");
    case KspaceDimension::repetition:
      return QObject::tr("Standard repetition/dynamic counter.");
    case KspaceDimension::set:
      return QObject::tr("Standard flow-encoding set counter.");
    case KspaceDimension::segment:
      return QObject::tr("Standard segmented-acquisition counter.");
    case KspaceDimension::user_0:
    case KspaceDimension::user_1:
    case KspaceDimension::user_2:
    case KspaceDimension::user_3:
    case KspaceDimension::user_4:
    case KspaceDimension::user_5:
    case KspaceDimension::user_6:
    case KspaceDimension::user_7:
      return QObject::tr("Source-defined user counter %1.")
        .arg(static_cast<int>(dimension) - static_cast<int>(KspaceDimension::user_0));
    case KspaceDimension::count:
      break;
  }
  return {};
}

[[nodiscard]] std::optional<KspaceDimension> kspace_dimension_from_identifier(const QString& identifier) {
  const auto found =
    std::find_if(kKspaceDimensions.cbegin(), kKspaceDimensions.cend(), [&identifier](const auto dimension) {
      return ksj::viewer::cartesian_kspace_dimension_identifier(dimension) == identifier;
    });
  return found == kKspaceDimensions.cend() ? std::nullopt : std::optional{*found};
}

[[nodiscard]] const QList<int>* kspace_dimension_values(const ksj::viewer::CartesianKspaceCatalog& catalog,
                                                        const KspaceDimension dimension) {
  const auto found =
    std::find_if(catalog.dimensions.cbegin(), catalog.dimensions.cend(), [dimension](const auto& item) {
      return item.dimension == dimension;
    });
  return found == catalog.dimensions.cend() ? nullptr : &found->observed_values;
}

[[nodiscard]] QList<ksj::viewer::ArrShowDimensionSpec> kspace_dimension_specs(
  const ksj::viewer::CartesianKspaceCatalog& catalog, const ksj::viewer::CartesianKspaceCoordinate& coordinate,
  const ksj::viewer::ArrShowDimensionSelection& selection, const ksj::viewer::KspacePresentation& presentation) {
  QList<ksj::viewer::ArrShowDimensionSpec> dimensions;
  const auto source_grid = presentation.details.value(QStringLiteral("source_grid")).toObject();
  const auto displayed_axis_x = presentation.details.value(QStringLiteral("axis_x")).toString();
  const auto displayed_axis_y = presentation.details.value(QStringLiteral("axis_y")).toString();
  for (const auto dimension : kKspaceDimensions) {
    const auto* values = kspace_dimension_values(catalog, dimension);
    if (values == nullptr || values->isEmpty()) {
      continue;
    }
    const auto identifier = ksj::viewer::cartesian_kspace_dimension_identifier(dimension);
    const auto selection_tag = ksj::viewer::arrshow_dimension_selection_tag(selection, identifier);
    if (selection_tag == ksj::viewer::ArrShowDimensionSelectionTag::none && values->size() < 2) {
      continue;
    }
    auto extent = static_cast<int>(values->size());
    // The K-space presentation itself records its normalized display axes,
    // so the rendered extent always follows column-order X/Y, never the
    // blue/red arrShow selection tag.
    if (displayed_axis_x == identifier) {
      extent = source_grid.value(QStringLiteral("width")).toInt(extent);
    } else if (displayed_axis_y == identifier) {
      extent = source_grid.value(QStringLiteral("height")).toInt(extent);
    }
    dimensions.append({.identifier = identifier,
                       .label = kspace_dimension_label(dimension),
                       .abbreviation = kspace_dimension_abbreviation(dimension),
                       .tool_tip = kspace_dimension_tool_tip(dimension),
                       .observed_values = *values,
                       .current_value = ksj::viewer::cartesian_kspace_coordinate_value(coordinate, dimension),
                       .displayed_extent = extent,
                       .selection_tag = selection_tag});
  }
  return dimensions;
}

[[nodiscard]] std::optional<ksj::viewer::CartesianKspaceAxes>
kspace_axes_in_column_order(const QList<ksj::viewer::ArrShowDimensionSpec>& dimensions) {
  const auto plane_axes = ksj::viewer::arrshow_plane_axes_in_column_order(dimensions);
  if (!plane_axes.has_value()) {
    return std::nullopt;
  }
  const auto x = kspace_dimension_from_identifier(plane_axes->x_identifier);
  const auto y = kspace_dimension_from_identifier(plane_axes->y_identifier);
  if (!x.has_value() || !y.has_value() || x.value() == y.value()) {
    return std::nullopt;
  }
  return ksj::viewer::CartesianKspaceAxes{.x = x.value(), .y = y.value()};
}

void normalize_kspace_dimension_selection(const ksj::viewer::CartesianKspaceCatalog& catalog,
                                          ksj::viewer::ArrShowDimensionSelection& selection) {
  QStringList available;
  for (const auto dimension : kKspaceDimensions) {
    const auto* values = kspace_dimension_values(catalog, dimension);
    if (values != nullptr && !values->isEmpty()) {
      available.append(ksj::viewer::cartesian_kspace_dimension_identifier(dimension));
    }
  }
  if (available.size() < 2) {
    selection = {};
    return;
  }
  const auto select_preferred = [&available](const QString& preferred, const QString& excluded) {
    if (preferred != excluded && available.contains(preferred)) {
      return preferred;
    }
    for (const auto& candidate : available) {
      if (candidate != excluded) {
        return candidate;
      }
    }
    return QString{};
  };
  if (!available.contains(selection.first_identifier)) {
    selection.first_identifier = select_preferred(QStringLiteral("readout"), {});
  }
  if (selection.second_identifier == selection.first_identifier || !available.contains(selection.second_identifier)) {
    selection.second_identifier = select_preferred(QStringLiteral("phase-encode"), selection.first_identifier);
  }
}

using ImageDimension = ksj::viewer::ImageDimension;

constexpr std::array kImageDimensions{
  ImageDimension::x,
  ImageDimension::y,
  ImageDimension::z,
  ImageDimension::channel,
};

[[nodiscard]] QString image_dimension_label(const ImageDimension dimension) {
  switch (dimension) {
    case ImageDimension::x:
      return QObject::tr("X");
    case ImageDimension::y:
      return QObject::tr("Y");
    case ImageDimension::z:
      return QObject::tr("Z");
    case ImageDimension::channel:
      return QObject::tr("Channel");
    case ImageDimension::count:
      break;
  }
  return {};
}

[[nodiscard]] QString image_dimension_abbreviation(const ImageDimension dimension) {
  switch (dimension) {
    case ImageDimension::x:
      return QStringLiteral("X");
    case ImageDimension::y:
      return QStringLiteral("Y");
    case ImageDimension::z:
      return QStringLiteral("Z");
    case ImageDimension::channel:
      return QStringLiteral("Ch");
    case ImageDimension::count:
      break;
  }
  return {};
}

[[nodiscard]] QString image_dimension_tool_tip(const ImageDimension dimension) {
  switch (dimension) {
    case ImageDimension::x:
      return QObject::tr("Standard ISMRMRD image X pixel dimension.");
    case ImageDimension::y:
      return QObject::tr("Standard ISMRMRD image Y pixel dimension.");
    case ImageDimension::z:
      return QObject::tr("Standard ISMRMRD image Z pixel dimension.");
    case ImageDimension::channel:
      return QObject::tr("Standard ISMRMRD image channel dimension.");
    case ImageDimension::count:
      break;
  }
  return {};
}

[[nodiscard]] std::optional<ImageDimension> image_dimension_from_identifier(const QString& identifier) {
  const auto found =
    std::find_if(kImageDimensions.cbegin(), kImageDimensions.cend(), [&identifier](const auto dimension) {
      return ksj::viewer::image_dimension_identifier(dimension) == identifier;
    });
  return found == kImageDimensions.cend() ? std::nullopt : std::optional{*found};
}

[[nodiscard]] QList<ksj::viewer::ArrShowDimensionSpec>
image_dimension_specs(const std::array<std::uint16_t, ksj::viewer::kImageDimensionCount>& source_dimensions,
                      const ksj::viewer::ImageCoordinate& coordinate,
                      const ksj::viewer::ImagePresentation& presentation) {
  QList<ksj::viewer::ArrShowDimensionSpec> dimensions;
  const auto is_current_presentation =
    presentation.source_dimensions == source_dimensions && presentation.axes == ksj::viewer::ImageAxes{};
  const auto source_grid =
    is_current_presentation ? presentation.details.value(QStringLiteral("source_grid")).toObject() : QJsonObject{};
  const auto displayed_axis_x =
    is_current_presentation ? presentation.details.value(QStringLiteral("axis_x")).toString() : QString{};
  const auto displayed_axis_y =
    is_current_presentation ? presentation.details.value(QStringLiteral("axis_y")).toString() : QString{};
  for (const auto dimension : kImageDimensions) {
    const auto identifier = ksj::viewer::image_dimension_identifier(dimension);
    // Standard image storage defines the image plane as its first two native
    // dimensions.  Unlike raw K-space, image Z/channel values are selectors,
    // not alternate geometric axes.
    const auto selection_tag = dimension == ImageDimension::x   ? ksj::viewer::ArrShowDimensionSelectionTag::first
                               : dimension == ImageDimension::y ? ksj::viewer::ArrShowDimensionSelectionTag::second
                                                                : ksj::viewer::ArrShowDimensionSelectionTag::none;
    const auto source_extent = source_dimensions.at(ksj::viewer::image_dimension_index(dimension));
    if (source_extent == 0U) {
      continue;
    }
    auto displayed_extent = static_cast<int>(source_extent);
    if (displayed_axis_x == identifier) {
      displayed_extent = source_grid.value(QStringLiteral("width")).toInt(displayed_extent);
    } else if (displayed_axis_y == identifier) {
      displayed_extent = source_grid.value(QStringLiteral("height")).toInt(displayed_extent);
    }
    QList<int> values;
    values.reserve(static_cast<qsizetype>(source_extent));
    for (std::uint16_t value = 0U; value < source_extent; ++value) {
      values.append(static_cast<int>(value));
    }
    dimensions.append({.identifier = identifier,
                       .label = image_dimension_label(dimension),
                       .abbreviation = image_dimension_abbreviation(dimension),
                       .tool_tip = image_dimension_tool_tip(dimension),
                       .observed_values = std::move(values),
                       .current_value = static_cast<int>(ksj::viewer::image_coordinate_value(coordinate, dimension)),
                       .displayed_extent = displayed_extent,
                       .selection_tag = selection_tag,
                       .selection_change_enabled = false});
  }
  return dimensions;
}

[[nodiscard]] std::optional<ksj::viewer::ImageAxes>
image_axes_in_column_order(const QList<ksj::viewer::ArrShowDimensionSpec>& dimensions) {
  const auto plane_axes = ksj::viewer::arrshow_plane_axes_in_column_order(dimensions);
  if (!plane_axes.has_value()) {
    return std::nullopt;
  }
  const auto x = image_dimension_from_identifier(plane_axes->x_identifier);
  const auto y = image_dimension_from_identifier(plane_axes->y_identifier);
  if (!x.has_value() || !y.has_value() || x.value() == y.value()) {
    return std::nullopt;
  }
  return ksj::viewer::ImageAxes{.x = x.value(), .y = y.value()};
}

void normalize_image_coordinate(const std::array<std::uint16_t, ksj::viewer::kImageDimensionCount>& source_dimensions,
                                ksj::viewer::ImageCoordinate& coordinate) {
  for (const auto dimension : kImageDimensions) {
    const auto extent = source_dimensions.at(ksj::viewer::image_dimension_index(dimension));
    if (extent > 0U) {
      if (ksj::viewer::image_coordinate_value(coordinate, dimension) >= extent) {
        ksj::viewer::set_image_coordinate_value(coordinate, dimension, static_cast<std::uint16_t>(extent - 1U));
      }
    }
  }
}

class OpenAsDialog final : public QDialog {
public:
  OpenAsDialog(const QString& object_name, const QStringList& choices, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("openAsDialog"));
    setWindowTitle(QObject::tr("Open As"));
    setModal(true);
    setMinimumWidth(360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);
    auto* description =
      new QLabel(QObject::tr("Open %1 using a supported, bounded inspection view.").arg(object_name), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    chooser_ = new QComboBox(this);
    chooser_->setObjectName(QStringLiteral("openAsChoice"));
    chooser_->addItems(choices);
    layout->addWidget(chooser_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("openAsButtons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
  }

  [[nodiscard]] int selected_choice() const noexcept { return chooser_ == nullptr ? -1 : chooser_->currentIndex(); }

private:
  QComboBox* chooser_ = nullptr;
};

struct WorkspacePage {
  QWidget* widget = nullptr;
  QVBoxLayout* layout = nullptr;
};

struct EmptyState {
  QFrame* widget = nullptr;
  QPushButton* action = nullptr;
};

[[nodiscard]] QLabel* make_text(const QString& text, const char* role, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setProperty("textRole", QString::fromLatin1(role));
  label->setWordWrap(true);
  return label;
}

[[nodiscard]] QFrame* make_surface(QWidget* parent, const QString& object_name = {},
                                   const QString& role = QStringLiteral("card")) {
  auto* surface = new QFrame(parent);
  surface->setFrameShape(QFrame::StyledPanel);
  surface->setProperty("surfaceRole", role);
  if (!object_name.isEmpty()) {
    surface->setObjectName(object_name);
  }
  return surface;
}

[[nodiscard]] QPlainTextEdit* make_read_only_text(QWidget* parent) {
  auto* text = new QPlainTextEdit(parent);
  text->setReadOnly(true);
  text->setFrameShape(QFrame::NoFrame);
  text->setLineWrapMode(QPlainTextEdit::NoWrap);
  text->setProperty("surfaceRole", QStringLiteral("code"));
  return text;
}

void add_card_heading(QVBoxLayout* layout, QWidget* parent, const QString& title, const QString& detail = {}) {
  layout->addWidget(make_text(title, "sectionTitle", parent));
  if (!detail.isEmpty()) {
    layout->addWidget(make_text(detail, "sectionDetail", parent));
  }
}

[[nodiscard]] WorkspacePage make_workspace_page(QTabWidget* parent, const QString& object_name, const QString& eyebrow,
                                                const QString& title, const QString& description) {
  Q_UNUSED(eyebrow)
  Q_UNUSED(title)
  Q_UNUSED(description)
  auto* page = new QWidget(parent);
  page->setObjectName(object_name);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(5);

  return {.widget = page, .layout = layout};
}

[[nodiscard]] EmptyState make_empty_state(QWidget* parent, const QString& object_name, const QString& marker,
                                          const QString& title, const QString& description,
                                          const QString& action_text) {
  auto* surface = make_surface(parent, object_name, QStringLiteral("empty"));
  surface->setProperty("emptyState", true);
  auto* layout = new QHBoxLayout(surface);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setSpacing(8);

  auto* marker_label = make_text(marker, "emptyMarker", surface);
  marker_label->setAlignment(Qt::AlignCenter);
  layout->addWidget(marker_label);
  auto* text_column = new QWidget(surface);
  auto* text_layout = new QVBoxLayout(text_column);
  text_layout->setContentsMargins(0, 0, 0, 0);
  text_layout->setSpacing(1);
  text_layout->addWidget(make_text(title, "emptyTitle", text_column));
  text_layout->addWidget(make_text(description, "emptyDescription", text_column));
  layout->addWidget(text_column, 1);

  auto* action = new QPushButton(action_text, surface);
  action->setProperty("buttonRole", QStringLiteral("primary"));
  action->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  layout->addWidget(action, 0, Qt::AlignVCenter);

  return {.widget = surface, .action = action};
}

void set_table_contents(QTableWidget* table, const QStringList& columns, const QList<QStringList>& rows) {
  table->clearContents();
  table->setColumnCount(columns.size());
  table->setHorizontalHeaderLabels(columns);
  table->setRowCount(rows.size());
  for (qsizetype row = 0; row < rows.size(); ++row) {
    const auto& values = rows.at(row);
    for (qsizetype column = 0; column < values.size() && column < columns.size(); ++column) {
      table->setItem(static_cast<int>(row), static_cast<int>(column), new QTableWidgetItem(values.at(column)));
    }
  }
  table->resizeColumnsToContents();
  table->horizontalHeader()->setStretchLastSection(true);
}

[[nodiscard]] QColor pipeline_graph_fill(const ksj::viewer::PipelineGraphNodeKind kind) {
  using ksj::viewer::PipelineGraphNodeKind;
  switch (kind) {
    case PipelineGraphNodeKind::ingress:
      return QColor(QStringLiteral("#e5f1fb"));
    case PipelineGraphNodeKind::operator_node:
      return QColor(QStringLiteral("#ffffff"));
    case PipelineGraphNodeKind::egress:
      return QColor(QStringLiteral("#e8f5ea"));
  }
  return QColor(QStringLiteral("#ffffff"));
}

[[nodiscard]] QString pipeline_graph_node_role(const ksj::viewer::PipelineGraphNodeKind kind) {
  using ksj::viewer::PipelineGraphNodeKind;
  switch (kind) {
    case PipelineGraphNodeKind::ingress:
      return QObject::tr("AUTHORED INGRESS");
    case PipelineGraphNodeKind::operator_node:
      return QObject::tr("AUTHORED OPERATOR");
    case PipelineGraphNodeKind::egress:
      return QObject::tr("AUTHORED EGRESS");
  }
  return QObject::tr("AUTHORED GRAPH");
}

[[nodiscard]] QPen pipeline_graph_edge_pen(const ksj::viewer::PipelineGraphEdgeKind kind) {
  using ksj::viewer::PipelineGraphEdgeKind;
  QPen pen;
  switch (kind) {
    case PipelineGraphEdgeKind::ingress:
      pen = QPen(QColor(QStringLiteral("#487a9f")), 1.5);
      break;
    case PipelineGraphEdgeKind::data:
      pen = QPen(QColor(QStringLiteral("#5f6f7d")), 1.5);
      break;
    case PipelineGraphEdgeKind::egress:
      pen = QPen(QColor(QStringLiteral("#4f8759")), 1.5);
      break;
    case PipelineGraphEdgeKind::calibration:
      pen = QPen(QColor(QStringLiteral("#8a6d3b")), 1.5, Qt::DashLine);
      break;
  }
  pen.setCosmetic(true);
  return pen;
}

[[nodiscard]] QString pipeline_graph_edge_caption(const ksj::viewer::PipelineGraphEdge& edge) {
  QString caption;
  if (!edge.id.isEmpty()) {
    caption = edge.id + QStringLiteral(": ");
  }
  caption += edge.source_port + QStringLiteral(" → ") + edge.target_port;
  if (edge.kind == ksj::viewer::PipelineGraphEdgeKind::calibration) {
    caption += QStringLiteral(" [calibration]");
  }
  return caption;
}

[[nodiscard]] QString pipeline_graph_elide(const QString& text, const QFont& font, const int maximum_width) {
  return QFontMetrics(font).elidedText(text, Qt::ElideRight, maximum_width);
}

void populate_pipeline_graph(QGraphicsScene& scene, const ksj::viewer::PipelinePresentation& presentation) {
  using ksj::viewer::PipelineGraphEdge;
  using ksj::viewer::PipelineGraphEdgeKind;
  using ksj::viewer::PipelineGraphNode;
  using ksj::viewer::PipelineGraphNodeKind;

  scene.clear();
  const auto semantic_item_count = presentation.graph_nodes.size() + presentation.graph_edges.size();
  QFont message_font;
  message_font.setPointSize(10);

  if (semantic_item_count == 0) {
    auto* message =
      scene.addSimpleText(QObject::tr("Open a PipelineDefinition to visualize its authored graph."), message_font);
    message->setBrush(QBrush(QColor(QStringLiteral("#5f5f5f"))));
    message->setPos(kPipelineGraphMargin, kPipelineGraphMargin);
    scene.setSceneRect(
      message->boundingRect().adjusted(0.0, 0.0, 2.0 * kPipelineGraphMargin, 2.0 * kPipelineGraphMargin));
    return;
  }
  if (semantic_item_count > kMaximumPipelineGraphSemanticItems) {
    const auto text = QObject::tr("The authored graph contains %1 semantic items, exceeding this viewer's %2-item "
                                  "visualization bound. Inspect the canonical JSON instead; no partial graph is shown.")
                        .arg(semantic_item_count)
                        .arg(kMaximumPipelineGraphSemanticItems);
    auto* message = scene.addText(text, message_font);
    message->setDefaultTextColor(QColor(QStringLiteral("#5f5f5f")));
    message->setTextWidth(620.0);
    message->setPos(kPipelineGraphMargin, kPipelineGraphMargin);
    scene.setSceneRect(
      message->boundingRect().adjusted(0.0, 0.0, 2.0 * kPipelineGraphMargin, 2.0 * kPipelineGraphMargin));
    return;
  }

  QHash<QString, PipelineGraphNodeKind> kinds;
  QHash<QString, int> layers;
  for (const auto& node : presentation.graph_nodes) {
    kinds.insert(node.key, node.kind);
    layers.insert(node.key, node.kind == PipelineGraphNodeKind::ingress ? 0 : 1);
  }

  for (qsizetype pass = 0; pass < presentation.graph_nodes.size(); ++pass) {
    bool changed = false;
    for (const auto& edge : presentation.graph_edges) {
      if (!layers.contains(edge.source_key) || !layers.contains(edge.target_key) ||
          kinds.value(edge.target_key) == PipelineGraphNodeKind::ingress) {
        continue;
      }
      const auto candidate_layer = layers.value(edge.source_key) + 1;
      if (candidate_layer > layers.value(edge.target_key)) {
        layers[edge.target_key] = candidate_layer;
        changed = true;
      }
    }
    if (!changed) {
      break;
    }
  }

  std::map<int, QList<const PipelineGraphNode*>> nodes_by_layer;
  int maximum_layer = 0;
  qsizetype maximum_rows = 0U;
  for (const auto& node : presentation.graph_nodes) {
    const auto layer = layers.value(node.key);
    nodes_by_layer[layer].append(&node);
    maximum_layer = std::max(maximum_layer, layer);
  }
  for (const auto& [layer, nodes] : nodes_by_layer) {
    Q_UNUSED(layer)
    maximum_rows = std::max(maximum_rows, static_cast<qsizetype>(nodes.size()));
  }

  QHash<QString, QRectF> node_bounds;
  for (const auto& [layer, nodes] : nodes_by_layer) {
    const auto node_gaps = nodes.size() > 1 ? nodes.size() - 1 : 0;
    const auto maximum_row_gaps = maximum_rows > 1U ? maximum_rows - 1U : 0U;
    const auto layer_height = static_cast<qreal>(nodes.size()) * kPipelineGraphNodeHeight +
                              static_cast<qreal>(node_gaps) * kPipelineGraphVerticalGap;
    const auto full_height = static_cast<qreal>(maximum_rows) * kPipelineGraphNodeHeight +
                             static_cast<qreal>(maximum_row_gaps) * kPipelineGraphVerticalGap;
    const auto x =
      kPipelineGraphMargin + static_cast<qreal>(layer) * (kPipelineGraphNodeWidth + kPipelineGraphHorizontalGap);
    auto y = kPipelineGraphMargin + (full_height - layer_height) / 2.0;
    for (const auto* node : nodes) {
      node_bounds.insert(node->key, QRectF(x, y, kPipelineGraphNodeWidth, kPipelineGraphNodeHeight));
      y += kPipelineGraphNodeHeight + kPipelineGraphVerticalGap;
    }
  }

  QFont edge_font;
  edge_font.setPointSize(8);
  const auto anchor_sort_key = [](const PipelineGraphEdge& edge, const bool outgoing) {
    const QChar separator(0x1FU);
    if (outgoing) {
      return edge.source_port + separator + edge.target_key + separator + edge.target_port + separator + edge.id;
    }
    return edge.target_port + separator + edge.source_key + separator + edge.source_port + separator + edge.id;
  };
  const auto anchor_offset = [](const qsizetype index, const qsizetype count) {
    return count <= 1 ? 0.0
                      : (static_cast<qreal>(index + 1) / static_cast<qreal>(count + 1) - 0.5) *
                          (kPipelineGraphNodeHeight - 24.0);
  };
  QHash<QString, QList<qsizetype>> outgoing_edges;
  QHash<QString, QList<qsizetype>> incoming_edges;
  for (qsizetype index = 0; index < presentation.graph_edges.size(); ++index) {
    const auto& edge = presentation.graph_edges.at(index);
    if (node_bounds.contains(edge.source_key) && node_bounds.contains(edge.target_key)) {
      outgoing_edges[edge.source_key].append(index);
      incoming_edges[edge.target_key].append(index);
    }
  }
  std::vector<qreal> source_offsets(static_cast<std::size_t>(presentation.graph_edges.size()), 0.0);
  std::vector<qreal> target_offsets(static_cast<std::size_t>(presentation.graph_edges.size()), 0.0);
  const auto assign_anchor_offsets = [&presentation, &anchor_sort_key,
                                      &anchor_offset](QHash<QString, QList<qsizetype>>& grouped_edges,
                                                      std::vector<qreal>& offsets, const bool outgoing) {
    for (auto group = grouped_edges.begin(); group != grouped_edges.end(); ++group) {
      auto& edge_indexes = group.value();
      std::sort(edge_indexes.begin(), edge_indexes.end(),
                [&presentation, &anchor_sort_key, outgoing](const qsizetype left, const qsizetype right) {
                  return QString::compare(anchor_sort_key(presentation.graph_edges.at(left), outgoing),
                                          anchor_sort_key(presentation.graph_edges.at(right), outgoing),
                                          Qt::CaseSensitive) < 0;
                });
      for (qsizetype slot = 0; slot < edge_indexes.size(); ++slot) {
        offsets[static_cast<std::size_t>(edge_indexes.at(slot))] = anchor_offset(slot, edge_indexes.size());
      }
    }
  };
  assign_anchor_offsets(outgoing_edges, source_offsets, true);
  assign_anchor_offsets(incoming_edges, target_offsets, false);

  for (qsizetype index = 0; index < presentation.graph_edges.size(); ++index) {
    const auto& edge = presentation.graph_edges.at(index);
    if (!node_bounds.contains(edge.source_key) || !node_bounds.contains(edge.target_key)) {
      continue;
    }
    const auto source = node_bounds.value(edge.source_key);
    const auto target = node_bounds.value(edge.target_key);
    const auto starts_left_of_target = source.center().x() <= target.center().x();
    const auto source_offset = source_offsets.at(static_cast<std::size_t>(index));
    const auto target_offset = target_offsets.at(static_cast<std::size_t>(index));
    const QPointF start = starts_left_of_target ? QPointF(source.right(), source.center().y() + source_offset)
                                                : QPointF(source.left(), source.center().y() + source_offset);
    const QPointF end = starts_left_of_target ? QPointF(target.left(), target.center().y() + target_offset)
                                              : QPointF(target.right(), target.center().y() + target_offset);
    const auto direction = starts_left_of_target ? 1.0 : -1.0;
    const auto horizontal_distance = std::abs(end.x() - start.x());
    const auto handle = std::max(32.0, horizontal_distance / 2.0);
    QPainterPath path(start);
    const QPointF final_control(end.x() - direction * handle, end.y());
    path.cubicTo(QPointF(start.x() + direction * handle, start.y()), final_control, end);
    const auto pen = pipeline_graph_edge_pen(edge.kind);
    auto* line = scene.addPath(path, pen);
    line->setData(0, edge.id);
    line->setZValue(0.0);

    const QLineF tangent(final_control, end);
    if (tangent.length() > 0.0) {
      const auto angle = std::atan2(tangent.dy(), tangent.dx());
      constexpr auto arrow_size = 8.0;
      constexpr auto arrow_spread = 0.52;
      QPolygonF arrow;
      arrow << end
            << QPointF(end.x() - arrow_size * std::cos(angle - arrow_spread),
                       end.y() - arrow_size * std::sin(angle - arrow_spread))
            << QPointF(end.x() - arrow_size * std::cos(angle + arrow_spread),
                       end.y() - arrow_size * std::sin(angle + arrow_spread));
      auto* arrow_item = scene.addPolygon(arrow, pen, QBrush(pen.color()));
      arrow_item->setZValue(1.0);
    }

    const auto caption = pipeline_graph_elide(pipeline_graph_edge_caption(edge), edge_font, 180);
    auto* label = scene.addSimpleText(caption, edge_font);
    label->setBrush(QBrush(QColor(QStringLiteral("#5f5f5f"))));
    label->setPos((start.x() + end.x()) / 2.0 - label->boundingRect().width() / 2.0,
                  (start.y() + end.y()) / 2.0 - label->boundingRect().height() - 4.0);
    label->setZValue(1.0);
  }

  QFont title_font;
  title_font.setPointSize(9);
  title_font.setBold(true);
  QFont detail_font;
  detail_font.setPointSize(8);
  QFont role_font;
  role_font.setPointSize(7);
  for (const auto& node : presentation.graph_nodes) {
    const auto bounds = node_bounds.value(node.key);
    QPainterPath outline;
    outline.addRoundedRect(bounds, 4.0, 4.0);
    auto* card =
      scene.addPath(outline, QPen(QColor(QStringLiteral("#8fa2b0")), 1.0), QBrush(pipeline_graph_fill(node.kind)));
    card->setZValue(2.0);

    auto* title = scene.addSimpleText(pipeline_graph_elide(node.title, title_font, 222), title_font);
    title->setBrush(QBrush(QColor(QStringLiteral("#202020"))));
    title->setPos(bounds.left() + 12.0, bounds.top() + 9.0);
    title->setZValue(3.0);

    const auto lines = node.detail.split(QLatin1Char('\n'));
    for (qsizetype index = 0; index < lines.size() && index < 2; ++index) {
      auto* detail = scene.addSimpleText(pipeline_graph_elide(lines.at(index), detail_font, 222), detail_font);
      detail->setBrush(QBrush(QColor(QStringLiteral("#4e5d68"))));
      detail->setPos(bounds.left() + 12.0, bounds.top() + 30.0 + static_cast<qreal>(index) * 15.0);
      detail->setZValue(3.0);
    }

    auto* role = scene.addSimpleText(pipeline_graph_node_role(node.kind), role_font);
    role->setBrush(QBrush(QColor(QStringLiteral("#5f6f7d"))));
    role->setPos(bounds.left() + 12.0, bounds.bottom() - role->boundingRect().height() - 7.0);
    role->setZValue(3.0);
  }

  const auto width = 2.0 * kPipelineGraphMargin + static_cast<qreal>(maximum_layer + 1) * kPipelineGraphNodeWidth +
                     static_cast<qreal>(maximum_layer) * kPipelineGraphHorizontalGap;
  const auto maximum_row_gaps = maximum_rows > 1U ? maximum_rows - 1U : 0U;
  const auto height = 2.0 * kPipelineGraphMargin + static_cast<qreal>(maximum_rows) * kPipelineGraphNodeHeight +
                      static_cast<qreal>(maximum_row_gaps) * kPipelineGraphVerticalGap;
  scene.setSceneRect(
    QRectF(0.0, 0.0, width, height).united(scene.itemsBoundingRect().adjusted(-12.0, -12.0, 12.0, 12.0)));
}

[[nodiscard]] QImage pipeline_graph_scene_image(QGraphicsScene* scene) {
  if (scene == nullptr || scene->items().isEmpty()) {
    return {};
  }
  const auto source_rect = scene->sceneRect();
  if (source_rect.isEmpty()) {
    return {};
  }
  auto image_size = source_rect.size().toSize();
  image_size.setWidth(std::max(1, image_size.width()));
  image_size.setHeight(std::max(1, image_size.height()));
  image_size = image_size.scaled(kMaximumUiPixmapSize, Qt::KeepAspectRatio);
  if (image_size.isEmpty()) {
    return {};
  }
  QImage image(image_size, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    return {};
  }
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  scene->render(&painter, QRectF(QPointF{}, QSizeF(image_size)), source_rect);
  return image;
}

[[nodiscard]] QString mrd_source_description(const ksj::viewer::InspectionSession& session) {
  return QStringLiteral("%1 (container %2)").arg(session.source_path(), session.container_path());
}

[[nodiscard]] int bounded_spin_maximum(const std::uint32_t count) {
  if (count <= 1U) {
    return 0;
  }
  const auto maximum = static_cast<std::uint64_t>(count) - 1U;
  return static_cast<int>(std::min<std::uint64_t>(maximum, std::numeric_limits<int>::max()));
}

[[nodiscard]] QString export_filter(const ksj::viewer::VisualizationExportFormat format) {
  using ksj::viewer::VisualizationExportFormat;
  switch (format) {
    case VisualizationExportFormat::png:
      return QStringLiteral("PNG image (*.png)");
    case VisualizationExportFormat::svg:
      return QStringLiteral("SVG image (*.svg)");
    case VisualizationExportFormat::csv:
      return QStringLiteral("CSV table (*.csv)");
    case VisualizationExportFormat::json:
      return QStringLiteral("JSON document (*.json)");
  }
  return QStringLiteral("All files (*)");
}

} // namespace

namespace ksj::viewer {

ViewerWindow::ViewerWindow() {
  setObjectName(QStringLiteral("viewerWindow"));
  setWindowTitle(tr("KSpaceJet Viewer"));
  setMinimumSize(1120, 720);
  resize(1360, 860);

  auto settings = viewer_settings();
  const auto remembered_directory = settings.value(QString::fromLatin1(kLastOpenDirectorySettingsKey)).toString();
  if (QDir(remembered_directory).exists()) {
    last_open_directory_ = QDir::cleanPath(remembered_directory);
  }
  restore_recent_files();

  create_actions();
  create_workbench();
  refresh_metadata();
  refresh_kspace();
  refresh_image();
  refresh_pipeline();
  append_info(tr("Offline, read-only inspection: open a standard .mrd file or PipelineDefinition."));
}

void ViewerWindow::create_actions() {
  menuBar()->setObjectName(QStringLiteral("viewerMenuBar"));
  menuBar()->setNativeMenuBar(false);

  auto* file_menu = menuBar()->addMenu(tr("&File"));
  file_menu->setObjectName(QStringLiteral("viewerFileMenu"));

  open_mrd_action_ = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open ISMRMRD &file..."), this);
  open_mrd_action_->setObjectName(QStringLiteral("openMrdAction"));
  open_mrd_action_->setShortcut(QKeySequence::Open);
  open_mrd_action_->setToolTip(tr("Open a local standard ISMRMRD file (Ctrl+O)"));
  close_mrd_action_ = new QAction(style()->standardIcon(QStyle::SP_DialogCloseButton), tr("&Close source"), this);
  close_mrd_action_->setObjectName(QStringLiteral("closeMrdAction"));
  close_mrd_action_->setShortcut(QKeySequence::Close);
  close_mrd_action_->setToolTip(tr("Close the active source and its display derivatives (Ctrl+W)"));
  close_mrd_action_->setEnabled(false);
  open_pipeline_action_ = new QAction(style()->standardIcon(QStyle::SP_FileIcon), tr("Open &pipeline..."), this);
  open_pipeline_action_->setObjectName(QStringLiteral("openPipelineAction"));
  file_menu->addAction(open_mrd_action_);
  file_menu->addAction(open_pipeline_action_);
  recent_files_menu_ = file_menu->addMenu(tr("Recent &Files"));
  recent_files_menu_->setObjectName(QStringLiteral("recentFilesMenu"));
  refresh_recent_files_menu();
  file_menu->addSeparator();
  file_menu->addAction(close_mrd_action_);
  file_menu->addSeparator();

  auto* export_menu = file_menu->addMenu(tr("Export current &display derivative"));
  export_menu->setObjectName(QStringLiteral("viewerFileExportMenu"));
  export_png_action_ = export_menu->addAction(tr("PNG..."));
  export_svg_action_ = export_menu->addAction(tr("SVG..."));
  export_csv_action_ = export_menu->addAction(tr("CSV..."));
  export_json_action_ = export_menu->addAction(tr("JSON..."));
  export_png_action_->setObjectName(QStringLiteral("exportPngAction"));
  export_svg_action_->setObjectName(QStringLiteral("exportSvgAction"));
  export_csv_action_->setObjectName(QStringLiteral("exportCsvAction"));
  export_json_action_->setObjectName(QStringLiteral("exportJsonAction"));
  file_menu->addSeparator();

  auto* quit_action = file_menu->addAction(tr("&Quit"));
  quit_action->setObjectName(QStringLiteral("quitAction"));
  quit_action->setShortcut(QKeySequence::Quit);

  auto* tools_menu = menuBar()->addMenu(tr("&Tools"));
  tools_menu->setObjectName(QStringLiteral("viewerToolsMenu"));
  inspect_object_action_ =
    new QAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("&Inspect selected object"), this);
  inspect_object_action_->setObjectName(QStringLiteral("inspectObjectAction"));
  inspect_object_action_->setToolTip(tr("Inspect the selected standard semantic object"));
  inspect_object_action_->setEnabled(false);
  open_as_action_ =
    new QAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), tr("Open selected object &as..."), this);
  open_as_action_->setObjectName(QStringLiteral("openAsAction"));
  open_as_action_->setToolTip(tr("Choose a bounded typed view for the selected object"));
  open_as_action_->setEnabled(false);
  copy_object_path_action_ = new QAction(tr("&Copy object path"), this);
  copy_object_path_action_->setObjectName(QStringLiteral("copyObjectPathAction"));
  copy_object_path_action_->setEnabled(false);
  tools_menu->addAction(inspect_object_action_);
  tools_menu->addAction(open_as_action_);
  tools_menu->addAction(copy_object_path_action_);

  auto* help_menu = menuBar()->addMenu(tr("&Help"));
  help_menu->setObjectName(QStringLiteral("viewerHelpMenu"));
  auto* viewer_guide_action =
    help_menu->addAction(style()->standardIcon(QStyle::SP_DialogHelpButton), tr("Viewer &guide"));
  viewer_guide_action->setObjectName(QStringLiteral("viewerGuideAction"));
  auto* scope_action = help_menu->addAction(tr("Viewer &scope"));
  scope_action->setObjectName(QStringLiteral("viewerScopeAction"));
  auto* about_action = help_menu->addAction(tr("&About KSpaceJet Viewer"));
  about_action->setObjectName(QStringLiteral("aboutViewerAction"));

  connect(open_mrd_action_, &QAction::triggered, this, [this] {
    open_mrd();
  });
  connect(close_mrd_action_, &QAction::triggered, this, [this] {
    close_mrd_source();
  });
  connect(open_pipeline_action_, &QAction::triggered, this, [this] {
    open_pipeline();
  });
  connect(inspect_object_action_, &QAction::triggered, this, [this] {
    inspect_selected_object();
  });
  connect(open_as_action_, &QAction::triggered, this, [this] {
    open_selected_object_as();
  });
  connect(copy_object_path_action_, &QAction::triggered, this, [this] {
    copy_selected_object_path();
  });
  connect(export_png_action_, &QAction::triggered, this, [this] {
    export_current(VisualizationExportFormat::png);
  });
  connect(export_svg_action_, &QAction::triggered, this, [this] {
    export_current(VisualizationExportFormat::svg);
  });
  connect(export_csv_action_, &QAction::triggered, this, [this] {
    export_current(VisualizationExportFormat::csv);
  });
  connect(export_json_action_, &QAction::triggered, this, [this] {
    export_current(VisualizationExportFormat::json);
  });
  connect(quit_action, &QAction::triggered, this, &QWidget::close);
  connect(scope_action, &QAction::triggered, this, [this] {
    QMessageBox::information(
      this, tr("KSpaceJet Viewer scope"),
      tr("KSpaceJet Viewer is a local, read-only ISMRMRD and PipelineDefinition inspection application.\n\n"
         "It shows only verified standard MRD semantic objects, opens bounded typed views on demand, and exports "
         "visualization derivatives only. It does not edit HDF5, reconstruct data, load Providers, or connect a "
         "gateway."));
  });
  connect(viewer_guide_action, &QAction::triggered, this, [this] {
    QMessageBox::information(
      this, tr("KSpaceJet Viewer guide"),
      tr("1. Open a local standard ISMRMRD file from File; reopen either source type from File → Recent Files.\n"
         "2. Select a verified semantic object in the hierarchy. Selection only updates the inspector.\n"
         "3. Use its contextual tab, Inspect, Open As..., a double click, or the context menu to open a bounded view.\n"
         "4. The lower Info panel records local read-only inspection actions.\n\n"
         "KSpaceJet Viewer does not edit HDF5, retain full raw payloads, or support URL loading."));
  });
  connect(about_action, &QAction::triggered, this, [this] {
    QMessageBox::about(this, tr("About KSpaceJet Viewer"),
                       tr("KSpaceJet Viewer\nRead-only standard ISMRMRD inspection workspace"));
  });

  auto* toolbar = addToolBar(tr("Viewer"));
  toolbar->setObjectName(QStringLiteral("viewerToolbar"));
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->addAction(open_mrd_action_);
  toolbar->addAction(close_mrd_action_);
  toolbar->addSeparator();
  toolbar->addAction(inspect_object_action_);
  toolbar->addAction(open_as_action_);
  toolbar->addSeparator();
  toolbar->addAction(open_pipeline_action_);
  toolbar->addSeparator();
  toolbar->addAction(viewer_guide_action);

  export_button_ = new QToolButton(toolbar);
  export_button_->setObjectName(QStringLiteral("exportDisplayButton"));
  export_button_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  export_button_->setToolTip(tr("Export current display derivative"));
  export_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  export_button_->setPopupMode(QToolButton::InstantPopup);
  auto* toolbar_export_menu = new QMenu(export_button_);
  toolbar_export_menu->addAction(export_png_action_);
  toolbar_export_menu->addAction(export_svg_action_);
  toolbar_export_menu->addAction(export_csv_action_);
  toolbar_export_menu->addAction(export_json_action_);
  export_button_->setMenu(toolbar_export_menu);
  toolbar->addWidget(export_button_);
}

void ViewerWindow::create_workbench() {
  auto* workspace_root = new QWidget(this);
  workspace_root->setObjectName(QStringLiteral("viewerRoot"));
  auto* root_layout = new QVBoxLayout(workspace_root);
  root_layout->setContentsMargins(0, 0, 0, 0);
  root_layout->setSpacing(0);

  auto* main_splitter = new QSplitter(Qt::Horizontal, workspace_root);
  main_splitter->setObjectName(QStringLiteral("viewerMainSplitter"));
  main_splitter->setChildrenCollapsible(false);

  auto* tree_surface = new QFrame(main_splitter);
  tree_surface->setObjectName(QStringLiteral("semanticTreeSurface"));
  tree_surface->setProperty("surfaceRole", QStringLiteral("panel"));
  tree_surface->setMinimumWidth(240);
  tree_surface->setMaximumWidth(480);
  auto* tree_layout = new QVBoxLayout(tree_surface);
  tree_layout->setContentsMargins(0, 0, 0, 0);
  tree_layout->setSpacing(0);
  dataset_navigation_ = new QTreeWidget(tree_surface);
  dataset_navigation_->setObjectName(QStringLiteral("semanticObjectTree"));
  dataset_navigation_->setHeaderLabel(tr("ISMRMRD File Hierarchy"));
  dataset_navigation_->setRootIsDecorated(true);
  dataset_navigation_->setAnimated(true);
  dataset_navigation_->setIndentation(16);
  dataset_navigation_->setSelectionMode(QAbstractItemView::SingleSelection);
  dataset_navigation_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_layout->addWidget(dataset_navigation_, 1);

  auto* inspector_surface = new QFrame(main_splitter);
  inspector_surface->setObjectName(QStringLiteral("objectInspectorSurface"));
  inspector_surface->setProperty("surfaceRole", QStringLiteral("panel"));
  auto* inspector_layout = new QVBoxLayout(inspector_surface);
  inspector_layout->setContentsMargins(0, 0, 0, 0);
  inspector_layout->setSpacing(0);
  object_inspector_ = new QTabWidget(inspector_surface);
  object_inspector_->setObjectName(QStringLiteral("objectInspector"));
  object_inspector_->setDocumentMode(true);

  object_general_ = new QScrollArea(object_inspector_);
  object_general_->setObjectName(QStringLiteral("generalObjectInfo"));
  object_general_->setFrameShape(QFrame::NoFrame);
  object_general_->setWidgetResizable(true);
  auto* general_content = new QWidget(object_general_);
  general_content->setObjectName(QStringLiteral("generalObjectInfoContent"));
  auto* general_layout = new QVBoxLayout(general_content);
  general_layout->setContentsMargins(6, 6, 6, 6);
  general_layout->setSpacing(6);
  const auto make_read_only_field = [general_content]() {
    auto* field = new QLineEdit(general_content);
    field->setReadOnly(true);
    field->setFrame(true);
    return field;
  };
  auto* identity_widget = new QWidget(general_content);
  identity_widget->setObjectName(QStringLiteral("objectIdentityInfo"));
  auto* identity_layout = new QFormLayout(identity_widget);
  identity_layout->setContentsMargins(0, 0, 0, 0);
  identity_layout->setHorizontalSpacing(4);
  identity_layout->setVerticalSpacing(2);
  identity_layout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  object_name_field_ = make_read_only_field();
  object_name_field_->setObjectName(QStringLiteral("objectNameField"));
  object_path_field_ = make_read_only_field();
  object_path_field_->setObjectName(QStringLiteral("objectPathField"));
  object_type_field_ = make_read_only_field();
  object_type_field_->setObjectName(QStringLiteral("objectTypeField"));
  object_access_field_ = make_read_only_field();
  object_access_field_->setObjectName(QStringLiteral("objectAccessField"));
  identity_layout->addRow(tr("Name:"), object_name_field_);
  identity_layout->addRow(tr("Path:"), object_path_field_);
  identity_layout->addRow(tr("Type:"), object_type_field_);
  identity_layout->addRow(tr("Access:"), object_access_field_);
  general_layout->addWidget(identity_widget);

  auto* semantic_group = new QGroupBox(tr("Dataset Dataspace and Datatype"), general_content);
  semantic_group->setObjectName(QStringLiteral("objectSemanticInfoGroup"));
  auto* semantic_layout = new QVBoxLayout(semantic_group);
  semantic_layout->setContentsMargins(5, 11, 5, 5);
  object_semantics_table_ = new QTableWidget(semantic_group);
  object_semantics_table_->setObjectName(QStringLiteral("objectSemanticInfoTable"));
  object_semantics_table_->setColumnCount(2);
  object_semantics_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  object_semantics_table_->setSelectionMode(QAbstractItemView::NoSelection);
  object_semantics_table_->setShowGrid(true);
  object_semantics_table_->horizontalHeader()->setVisible(false);
  object_semantics_table_->verticalHeader()->setVisible(false);
  object_semantics_table_->horizontalHeader()->setStretchLastSection(true);
  object_semantics_table_->setColumnWidth(0, 180);
  semantic_layout->addWidget(object_semantics_table_);
  general_layout->addWidget(semantic_group);

  auto* members_group = new QGroupBox(tr("Standard ISMRMRD Dataset Members"), general_content);
  members_group->setObjectName(QStringLiteral("objectMembersInfoGroup"));
  auto* members_layout = new QVBoxLayout(members_group);
  members_layout->setContentsMargins(5, 11, 5, 5);
  object_members_table_ = new QTableWidget(members_group);
  object_members_table_->setObjectName(QStringLiteral("objectMembersInfoTable"));
  object_members_table_->setColumnCount(3);
  object_members_table_->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Array Size")});
  object_members_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  object_members_table_->setSelectionMode(QAbstractItemView::NoSelection);
  object_members_table_->setAlternatingRowColors(true);
  object_members_table_->verticalHeader()->setVisible(false);
  object_members_table_->horizontalHeader()->setStretchLastSection(true);
  members_layout->addWidget(object_members_table_);
  general_layout->addWidget(members_group, 1);
  object_general_->setWidget(general_content);
  object_inspector_->addTab(object_general_, tr("General Object Info"));
  object_inspector_->setCurrentWidget(object_general_);
  inspector_layout->addWidget(object_inspector_, 1);

  create_kspace_page();
  create_metadata_page();
  create_image_page();
  create_pipeline_page();
  update_workspace_tab_visibility(nullptr);

  main_splitter->addWidget(tree_surface);
  main_splitter->addWidget(inspector_surface);
  main_splitter->setStretchFactor(0, 0);
  main_splitter->setStretchFactor(1, 1);
  main_splitter->setSizes({300, 1040});
  root_layout->addWidget(main_splitter, 1);

  auto* info_surface = new QFrame(workspace_root);
  info_surface->setObjectName(QStringLiteral("viewerInfoSurface"));
  info_surface->setProperty("surfaceRole", QStringLiteral("panel"));
  auto* info_layout = new QVBoxLayout(info_surface);
  info_layout->setContentsMargins(4, 2, 4, 3);
  info_layout->setSpacing(2);
  info_layout->addWidget(make_text(tr("Info"), "sectionTitle", info_surface));
  info_panel_ = make_read_only_text(info_surface);
  info_panel_->setObjectName(QStringLiteral("viewerInfoPanel"));
  info_panel_->setMinimumHeight(58);
  info_panel_->setMaximumHeight(78);
  info_panel_->setMaximumBlockCount(256);
  info_layout->addWidget(info_panel_);
  root_layout->addWidget(info_surface);
  setCentralWidget(workspace_root);

  connect(dataset_navigation_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* item, QTreeWidgetItem* previous_item) {
            Q_UNUSED(previous_item)
            update_object_inspector(item);
            update_workspace_tab_visibility(item);
            update_selection_actions();
          });
  connect(dataset_navigation_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, const int column) {
    Q_UNUSED(column)
    if (item != nullptr) {
      inspect_selected_object();
    }
  });
  connect(dataset_navigation_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) {
    show_object_context_menu(position);
  });
  connect(object_inspector_, &QTabWidget::currentChanged, this, [this](const int index) {
    if (object_inspector_ == nullptr) {
      return;
    }
    if (index == object_inspector_->indexOf(metadata_page_)) {
      activate_workspace_view(WorkspaceView::metadata);
    } else if (index == object_inspector_->indexOf(kspace_page_)) {
      activate_workspace_view(WorkspaceView::kspace);
    } else if (index == object_inspector_->indexOf(image_page_)) {
      activate_workspace_view(WorkspaceView::image);
    } else if (index == object_inspector_->indexOf(pipeline_page_)) {
      activate_workspace_view(WorkspaceView::pipeline);
    } else {
      update_export_availability();
    }
  });
  rebuild_dataset_navigation();

  auto* readonly_badge = make_text(tr("OFFLINE  /  READ-ONLY"), "modeBadge", statusBar());
  readonly_badge->setObjectName(QStringLiteral("offlineReadonlyBadge"));
  readonly_badge->setAlignment(Qt::AlignCenter);
  statusBar()->setObjectName(QStringLiteral("viewerStatusBar"));
  statusBar()->addPermanentWidget(readonly_badge);
  update_object_inspector(dataset_navigation_->currentItem());
  update_selection_actions();
}

void ViewerWindow::create_metadata_page() {
  const auto page = make_workspace_page(object_inspector_, QStringLiteral("metadataPage"), tr("STANDARD ISMRMRD"),
                                        tr("XML header"), tr("Bounded read-only XML from the selected header object."));
  metadata_page_ = page.widget;
  metadata_stack_ = new QStackedWidget(page.widget);
  metadata_stack_->setObjectName(QStringLiteral("metadataContentStack"));

  auto empty_state = make_empty_state(
    metadata_stack_, QStringLiteral("metadataEmptyState"), tr("XML"), tr("Select an XML header to inspect"),
    tr("Open a local standard ISMRMRD file, select its xml object, and choose Inspect."), tr("Open MRD dataset"));
  empty_state.action->setObjectName(QStringLiteral("metadataOpenMrdButton"));
  connect(empty_state.action, &QPushButton::clicked, open_mrd_action_, &QAction::trigger);
  metadata_stack_->addWidget(empty_state.widget);

  auto* content = new QWidget(metadata_stack_);
  content->setObjectName(QStringLiteral("metadataXmlContent"));
  auto* content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(5, 5, 5, 5);
  content_layout->setSpacing(4);
  content_layout->addWidget(make_text(tr("ISMRMRD Header XML"), "sectionTitle", content));
  metadata_xml_summary_ = make_text({}, "inspectorHint", content);
  metadata_xml_summary_->setObjectName(QStringLiteral("metadataXmlSummary"));
  metadata_xml_summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  content_layout->addWidget(metadata_xml_summary_);
  content_layout->addWidget(
    make_text(tr("Choose XML Tree or XML Text; the source file remains unchanged."), "sectionDetail", content));

  auto* view_modes = new QTabWidget(content);
  view_modes->setObjectName(QStringLiteral("metadataXmlViewModes"));
  view_modes->setDocumentMode(true);
  view_modes->setToolTip(tr("Switch between the bounded XML Tree and read-only XML Text presentation modes."));

  auto* outline_card = make_surface(view_modes, QStringLiteral("metadataXmlOutlineCard"), QStringLiteral("card"));
  auto* outline_layout = new QVBoxLayout(outline_card);
  outline_layout->setContentsMargins(5, 5, 5, 5);
  outline_layout->setSpacing(3);
  metadata_xml_outline_ = new QTreeWidget(outline_card);
  metadata_xml_outline_->setObjectName(QStringLiteral("metadataXmlOutline"));
  metadata_xml_outline_->setColumnCount(2);
  metadata_xml_outline_->setHeaderLabels({tr("Element"), tr("Value")});
  metadata_xml_outline_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  metadata_xml_outline_->setSelectionMode(QAbstractItemView::SingleSelection);
  metadata_xml_outline_->setAlternatingRowColors(true);
  metadata_xml_outline_->setUniformRowHeights(true);
  metadata_xml_outline_->setAllColumnsShowFocus(true);
  metadata_xml_outline_->setSortingEnabled(false);
  metadata_xml_outline_->header()->setStretchLastSection(true);
  metadata_xml_outline_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
  metadata_xml_outline_->setColumnWidth(0, 360);
  metadata_xml_outline_->setToolTip(
    tr("Bounded hierarchy of the selected standard ISMRMRD XML header. It does not edit or serialize source XML."));
  outline_layout->addWidget(metadata_xml_outline_, 1);
  view_modes->addTab(outline_card, tr("XML Tree"));

  auto* source_card = make_surface(view_modes, QStringLiteral("metadataXmlSourceCard"), QStringLiteral("card"));
  auto* source_layout = new QVBoxLayout(source_card);
  source_layout->setContentsMargins(5, 5, 5, 5);
  source_layout->setSpacing(3);
  metadata_xml_ = make_read_only_text(source_card);
  metadata_xml_->setObjectName(QStringLiteral("metadataXmlPreview"));
  metadata_xml_->setToolTip(
    tr("Read-only syntax-highlighted XML text. Presentation-only indentation never changes source XML."));
  new XmlSyntaxHighlighter(metadata_xml_->document());
  source_layout->addWidget(metadata_xml_, 1);
  view_modes->addTab(source_card, tr("XML Text"));
  view_modes->setCurrentIndex(0);
  content_layout->addWidget(view_modes, 1);

  metadata_stack_->addWidget(content);
  page.layout->addWidget(metadata_stack_, 1);
  object_inspector_->addTab(page.widget, tr("XML"));
}

void ViewerWindow::create_kspace_page() {
  const auto page =
    make_workspace_page(object_inspector_, QStringLiteral("kspacePage"), tr("CARTESIAN K-SPACE"), tr("K-space"),
                        tr("Display one bounded raw Cartesian two-axis plane. This is not a "
                           "reconstructed image."));
  kspace_page_ = page.widget;

  auto* controls_card = make_surface(page.widget, QStringLiteral("kspaceControlsCard"), QStringLiteral("controls"));
  auto* controls_layout = new QVBoxLayout(controls_card);
  controls_layout->setContentsMargins(10, 7, 10, 7);
  controls_layout->setSpacing(4);
  auto* display_controls_row = new QWidget(controls_card);
  display_controls_row->setObjectName(QStringLiteral("kspaceDisplayControlsRow"));
  auto* display_controls = new QHBoxLayout(display_controls_row);
  display_controls->setContentsMargins(0, 0, 0, 0);
  display_controls->setSpacing(7);
  display_controls->addWidget(make_text(tr("Data"), "controlLabel", display_controls_row));
  kspace_acquisition_type_ = new QComboBox(controls_card);
  kspace_acquisition_type_->setObjectName(QStringLiteral("kspaceAcquisitionTypeSelector"));
  kspace_acquisition_type_->setMinimumContentsLength(22);
  kspace_acquisition_type_->setToolTip(
    tr("ISMRMRD acquisition type. Imaging data is the default; auxiliary flag types are shown explicitly."));
  display_controls->addWidget(kspace_acquisition_type_);
  display_controls->addWidget(make_text(tr("Display"), "controlLabel", display_controls_row));
  kspace_component_ = new QComboBox(controls_card);
  kspace_component_->setObjectName(QStringLiteral("kspaceComponentSelector"));
  for (const auto component :
       {ArrShowDisplayComponent::magnitude, ArrShowDisplayComponent::real, ArrShowDisplayComponent::imaginary,
        ArrShowDisplayComponent::complex, ArrShowDisplayComponent::phase}) {
    kspace_component_->addItem(arrshow_display_component_label(component), static_cast<int>(component));
  }
  kspace_component_->setCurrentIndex(kspace_component_->findData(static_cast<int>(ArrShowDisplayComponent::complex)));
  kspace_component_->setToolTip(
    tr("arrShow complex chooser. Complex is martin_phase colour multiplied by magnitude after C/W."));
  display_controls->addWidget(kspace_component_);
  display_controls->addWidget(make_text(tr("Phase"), "controlLabel", display_controls_row));
  kspace_phase_representation_ = new QComboBox(controls_card);
  kspace_phase_representation_->setObjectName(QStringLiteral("kspacePhaseRepresentation"));
  for (const auto representation : {ArrShowPhaseRepresentation::degrees, ArrShowPhaseRepresentation::radians}) {
    kspace_phase_representation_->addItem(arrshow_phase_representation_label(representation),
                                          static_cast<int>(representation));
  }
  display_controls->addWidget(kspace_phase_representation_);
  display_controls->addWidget(make_text(tr("Range"), "controlLabel", display_controls_row));
  kspace_range_calculation_ = new QComboBox(controls_card);
  kspace_range_calculation_->setObjectName(QStringLiteral("kspaceRangeCalculation"));
  kspace_range_calculation_->addItem(
    arrshow_range_calculation_label(ArrShowRangeCalculation::minimum_maximum, kspace_display_settings_.percentile),
    static_cast<int>(ArrShowRangeCalculation::minimum_maximum));
  kspace_range_calculation_->addItem(
    arrshow_range_calculation_label(ArrShowRangeCalculation::percentile, kspace_display_settings_.percentile),
    static_cast<int>(ArrShowRangeCalculation::percentile));
  display_controls->addWidget(kspace_range_calculation_);
  kspace_percentile_ = new QDoubleSpinBox(controls_card);
  kspace_percentile_->setObjectName(QStringLiteral("kspaceRangePercentile"));
  kspace_percentile_->setRange(0.01, 100.0);
  kspace_percentile_->setDecimals(2);
  kspace_percentile_->setSuffix(QStringLiteral("%"));
  kspace_percentile_->setToolTip(tr("arrShow symmetric percentile range; disabled for min/max."));
  display_controls->addWidget(kspace_percentile_);
  display_controls->addStretch(1);
  display_controls->addWidget(make_text(tr("C/W"), "controlLabel", display_controls_row));
  kspace_window_persistence_ = new QComboBox(controls_card);
  kspace_window_persistence_->setObjectName(QStringLiteral("kspaceWindowPersistence"));
  for (const auto persistence :
       {ArrShowWindowPersistence::reset, ArrShowWindowPersistence::relative, ArrShowWindowPersistence::absolute}) {
    kspace_window_persistence_->addItem(arrshow_window_persistence_label(persistence), static_cast<int>(persistence));
  }
  kspace_window_persistence_->setCurrentIndex(
    kspace_window_persistence_->findData(static_cast<int>(ArrShowWindowPersistence::relative)));
  kspace_window_persistence_->setToolTip(
    tr("arrShow C/W policy after a Cartesian plane update: reset, keep relative, or keep absolute."));
  display_controls->addWidget(kspace_window_persistence_);
  kspace_window_center_ = new QDoubleSpinBox(controls_card);
  kspace_window_center_->setObjectName(QStringLiteral("kspaceWindowCenter"));
  kspace_window_center_->setDecimals(6);
  kspace_window_center_->setRange(-1.0e12, 1.0e12);
  kspace_window_center_->setSingleStep(0.1);
  kspace_window_center_->setPrefix(tr("C "));
  kspace_window_center_->setToolTip(
    tr("Center in the selected raw complex component's units. Complex C/W applies to magnitude."));
  display_controls->addWidget(kspace_window_center_);
  kspace_window_width_ = new QDoubleSpinBox(controls_card);
  kspace_window_width_->setObjectName(QStringLiteral("kspaceWindowWidth"));
  kspace_window_width_->setDecimals(6);
  kspace_window_width_->setRange(std::numeric_limits<double>::min(), 1.0e12);
  kspace_window_width_->setSingleStep(0.1);
  kspace_window_width_->setPrefix(tr("W "));
  kspace_window_width_->setToolTip(
    tr("Width in the active arrShow normal or phase window. Double-click the canvas to reset full C/W."));
  display_controls->addWidget(kspace_window_width_);
  kspace_reset_window_button_ = new QToolButton(controls_card);
  kspace_reset_window_button_->setObjectName(QStringLiteral("kspaceWindowResetButton"));
  kspace_reset_window_button_->setText(tr("Reset C/W"));
  kspace_reset_window_button_->setToolTip(tr("Restore full arrShow magnitude or phase C/W."));
  display_controls->addWidget(kspace_reset_window_button_);
  display_controls->addWidget(make_text(tr("View"), "controlLabel", display_controls_row));
  kspace_zoom_percent_ = new QSpinBox(controls_card);
  kspace_zoom_percent_->setObjectName(QStringLiteral("kspaceZoomPercent"));
  kspace_zoom_percent_->setRange(25, 800);
  kspace_zoom_percent_->setSingleStep(25);
  kspace_zoom_percent_->setSuffix(QStringLiteral("%"));
  kspace_zoom_percent_->setValue(100);
  kspace_zoom_percent_->setToolTip(tr("100% fits the bounded display derivative; Ctrl+wheel changes this view scale."));
  display_controls->addWidget(kspace_zoom_percent_);
  kspace_reset_view_button_ = new QToolButton(controls_card);
  kspace_reset_view_button_->setObjectName(QStringLiteral("kspaceViewResetButton"));
  kspace_reset_view_button_->setText(tr("Reset"));
  kspace_reset_view_button_->setToolTip(tr("Reset the view scale and clear pan."));
  display_controls->addWidget(kspace_reset_view_button_);
  controls_layout->addWidget(display_controls_row);

  auto* dimensions_row = new QWidget(controls_card);
  dimensions_row->setObjectName(QStringLiteral("kspaceDimensionsRow"));
  auto* dimensions_layout = new QHBoxLayout(dimensions_row);
  dimensions_layout->setContentsMargins(0, 0, 0, 0);
  dimensions_layout->setSpacing(7);
  auto* dimensions_label = make_text(tr("Dimensions"), "controlLabel", dimensions_row);
  dimensions_label->setObjectName(QStringLiteral("kspaceDimensionsLabel"));
  dimensions_label->setToolTip(tr("arrShow-style observed dimensions for the selected raw Cartesian K-space plane."));
  dimensions_layout->addWidget(dimensions_label, 0, Qt::AlignTop);
  kspace_dimensions_ = new ArrShowDimensionStrip(dimensions_row, QStringLiteral("kspace"));
  kspace_dimensions_->setObjectName(QStringLiteral("kspaceDimensions"));
  kspace_dimensions_->setToolTip(
    tr("arrShow-style dimensions: left-click replaces the blue ':' tag and right-click replaces the red ':' tag. "
       "Colour identifies the replacement target, not a column position. The two selected columns define X then Y "
       "in left-to-right order; remaining fields select observed ISMRMRD coordinates."));
  dimensions_layout->addWidget(kspace_dimensions_, 0, Qt::AlignTop);
  dimensions_layout->addStretch(1);
  controls_layout->addWidget(dimensions_row);
  page.layout->addWidget(controls_card);

  connect(kspace_acquisition_type_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](const int changed_index) {
            if (changed_index < 0 || kspace_selector_update_active_) {
              return;
            }
            const QScopedValueRollback<bool> selector_update(kspace_selector_update_active_, true);
            arrshow_prepare_active_window_for_new_plane(kspace_display_settings_);
            kspace_window_drag_active_ = false;
            kspace_availability_error_.clear();
            refresh_kspace_controls();
            if (kspace_catalog_.has_value()) {
              load_kspace();
            }
          });
  kspace_dimensions_->set_value_changed_callback([this](const QString& dimension_identifier, const int value) {
    select_kspace_dimension_value(dimension_identifier, value);
  });
  kspace_dimensions_->set_selection_tag_changed_callback(
    [this](const QString& dimension_identifier, const ArrShowDimensionSelectionTag selection_tag) {
      select_kspace_dimension_selection_tag(dimension_identifier, selection_tag);
    });
  connect(kspace_component_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    kspace_display_settings_.component = static_cast<ArrShowDisplayComponent>(kspace_component_->currentData().toInt());
    arrshow_prepare_active_window_for_new_plane(kspace_display_settings_);
    sync_kspace_arrshow_controls();
    if (!kspace_presentation_.details.isEmpty()) {
      load_kspace();
    }
    update_control_state();
  });
  connect(kspace_phase_representation_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    arrshow_convert_phase_window(kspace_display_settings_, static_cast<ArrShowPhaseRepresentation>(
                                                             kspace_phase_representation_->currentData().toInt()));
    if (arrshow_display_component_is_phase(kspace_display_settings_.component)) {
      arrshow_prepare_active_window_for_new_plane(kspace_display_settings_);
    }
    sync_kspace_arrshow_controls();
    if (!kspace_presentation_.details.isEmpty()) {
      load_kspace();
    }
  });
  connect(kspace_range_calculation_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    kspace_display_settings_.range_calculation =
      static_cast<ArrShowRangeCalculation>(kspace_range_calculation_->currentData().toInt());
    arrshow_prepare_active_window_for_new_plane(kspace_display_settings_);
    sync_kspace_arrshow_controls();
    if (!kspace_presentation_.details.isEmpty()) {
      load_kspace();
    }
  });
  connect(kspace_percentile_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] {
    kspace_display_settings_.percentile = kspace_percentile_->value();
    arrshow_prepare_active_window_for_new_plane(kspace_display_settings_);
    sync_kspace_arrshow_controls();
    if (!kspace_presentation_.details.isEmpty()) {
      load_kspace();
    }
  });
  connect(kspace_window_persistence_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    arrshow_active_window(kspace_display_settings_).persistence =
      static_cast<ArrShowWindowPersistence>(kspace_window_persistence_->currentData().toInt());
    arrshow_prepare_active_window_for_new_plane(kspace_display_settings_);
    sync_kspace_arrshow_controls();
    update_control_state();
    if (!kspace_window_drag_active_ && !kspace_presentation_.details.isEmpty()) {
      load_kspace();
    }
  });
  connect(kspace_window_center_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] {
    if (!kspace_window_drag_active_) {
      update_kspace_arrshow_settings_from_controls();
    }
    apply_kspace_display_window();
  });
  connect(kspace_window_width_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] {
    if (!kspace_window_drag_active_) {
      update_kspace_arrshow_settings_from_controls();
    }
    apply_kspace_display_window();
  });
  connect(kspace_reset_window_button_, &QToolButton::clicked, this, [this] {
    reset_kspace_window();
  });
  connect(kspace_zoom_percent_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](const int zoom_percent) {
    if (kspace_image_ != nullptr) {
      kspace_image_->set_zoom_percent(zoom_percent);
    }
  });
  connect(kspace_reset_view_button_, &QToolButton::clicked, this, [this] {
    if (kspace_image_ != nullptr) {
      kspace_image_->fit_to_view();
    }
    const QSignalBlocker blocker(kspace_zoom_percent_);
    kspace_zoom_percent_->setValue(100);
  });

  auto* canvas_card = make_surface(page.widget, QStringLiteral("kspaceCanvasCard"), QStringLiteral("canvas"));
  auto* canvas_layout = new QVBoxLayout(canvas_card);
  canvas_layout->setContentsMargins(10, 8, 10, 8);
  canvas_layout->setSpacing(4);
  add_card_heading(canvas_layout, canvas_card, tr("Raw Cartesian K-space"),
                   tr("Two selected arrShow dimensions · raw complex display · not a reconstructed image."));
  kspace_image_ = new InspectionCanvas(canvas_card);
  kspace_image_->setObjectName(QStringLiteral("kspaceCanvas"));
  kspace_image_->setAccessibleName(tr("Bounded raw Cartesian K-space display derivative"));
  kspace_image_->set_interaction_help(
    tr("Wheel or +/- active dimension · Left/Right dimension · Ctrl+wheel zoom · drag pan · middle drag C/W"));
  canvas_layout->addWidget(kspace_image_, 1);
  kspace_pixel_probe_ = make_text(tr("Pointer: open a K-space plane to probe it."), "canvasProbe", canvas_card);
  kspace_pixel_probe_->setObjectName(QStringLiteral("kspacePixelProbe"));
  canvas_layout->addWidget(kspace_pixel_probe_);
  kspace_image_->set_browse_step_callback([this](const int step) {
    step_kspace_plane(step);
  });
  kspace_image_->set_browse_dimension_callback([this](const int step) {
    if (kspace_dimensions_ != nullptr) {
      static_cast<void>(kspace_dimensions_->focus_relative_dimension(step));
    }
  });
  kspace_image_->set_probe_callback([this](const QPoint display_pixel) {
    update_kspace_pixel_probe(display_pixel);
  });
  kspace_image_->set_window_level_callback([this](const QPointF drag_delta, const bool finished) {
    adjust_kspace_window_from_drag(drag_delta, finished);
  });
  kspace_image_->set_reset_window_callback([this] {
    reset_kspace_window();
  });
  kspace_image_->set_zoom_changed_callback([this](const int zoom_percent) {
    const QSignalBlocker blocker(kspace_zoom_percent_);
    kspace_zoom_percent_->setValue(zoom_percent);
  });
  page.layout->addWidget(canvas_card, 1);
  object_inspector_->addTab(page.widget, tr("K-space"));
  sync_kspace_arrshow_controls();
}

void ViewerWindow::create_image_page() {
  const auto page =
    make_workspace_page(object_inspector_, QStringLiteral("imagePage"), tr("STANDARD IMAGE"), tr("Image"),
                        tr("Read one selected ISMRMRD image plane and retain only a bounded display derivative."));
  image_page_ = page.widget;

  auto* controls_card = make_surface(page.widget, QStringLiteral("imageControlsCard"), QStringLiteral("controls"));
  auto* controls_layout = new QVBoxLayout(controls_card);
  controls_layout->setContentsMargins(10, 7, 10, 7);
  controls_layout->setSpacing(4);
  auto* source_controls_row = new QWidget(controls_card);
  source_controls_row->setObjectName(QStringLiteral("imageSourceControlsRow"));
  auto* controls = new QHBoxLayout(source_controls_row);
  controls->setContentsMargins(0, 0, 0, 0);
  controls->setSpacing(7);
  controls->addWidget(make_text(tr("Series"), "controlLabel", source_controls_row));
  image_series_ = new QComboBox(controls_card);
  image_series_->setObjectName(QStringLiteral("imageSeries"));
  image_series_->setMinimumContentsLength(10);
  controls->addWidget(image_series_, 1);
  controls->addWidget(make_text(tr("Display"), "controlLabel", controls_card));
  image_component_ = new QComboBox(controls_card);
  image_component_->setObjectName(QStringLiteral("imageComponentSelector"));
  for (const auto component :
       {ArrShowDisplayComponent::magnitude, ArrShowDisplayComponent::real, ArrShowDisplayComponent::imaginary,
        ArrShowDisplayComponent::complex, ArrShowDisplayComponent::phase}) {
    image_component_->addItem(arrshow_display_component_label(component), static_cast<int>(component));
  }
  image_component_->setCurrentIndex(image_component_->findData(static_cast<int>(ArrShowDisplayComponent::complex)));
  image_component_->setToolTip(
    tr("arrShow complex chooser. Real image data exposes Magnitude and Real; complex data defaults to Complex."));
  controls->addWidget(image_component_);
  controls->addWidget(make_text(tr("Phase"), "controlLabel", controls_card));
  image_phase_representation_ = new QComboBox(controls_card);
  image_phase_representation_->setObjectName(QStringLiteral("imagePhaseRepresentation"));
  for (const auto representation : {ArrShowPhaseRepresentation::degrees, ArrShowPhaseRepresentation::radians}) {
    image_phase_representation_->addItem(arrshow_phase_representation_label(representation),
                                         static_cast<int>(representation));
  }
  controls->addWidget(image_phase_representation_);
  controls->addWidget(make_text(tr("Ordinal"), "controlLabel", controls_card));
  image_ordinal_ = new QSpinBox(controls_card);
  image_ordinal_->setObjectName(QStringLiteral("imageOrdinal"));
  image_ordinal_->setRange(0, 0);
  controls->addWidget(image_ordinal_);
  image_cine_button_ = new QToolButton(controls_card);
  image_cine_button_->setObjectName(QStringLiteral("imageCineButton"));
  image_cine_button_->setText(tr("Cine"));
  image_cine_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  image_cine_button_->setToolTip(tr("Advance the selected image ordinal without retaining source pixels."));
  controls->addWidget(image_cine_button_);
  load_image_button_ = new QToolButton(controls_card);
  load_image_button_->setObjectName(QStringLiteral("imageInspectButton"));
  load_image_button_->setText(tr("Inspect"));
  load_image_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  load_image_button_->setProperty("buttonRole", QStringLiteral("primary"));
  load_image_button_->setMinimumHeight(28);
  controls->addWidget(load_image_button_);
  controls_layout->addWidget(source_controls_row);

  auto* dimensions_row = new QWidget(controls_card);
  dimensions_row->setObjectName(QStringLiteral("imageDimensionsRow"));
  auto* dimensions_layout = new QHBoxLayout(dimensions_row);
  dimensions_layout->setContentsMargins(0, 0, 0, 0);
  dimensions_layout->setSpacing(7);
  auto* dimensions_label = make_text(tr("Dimensions"), "controlLabel", dimensions_row);
  dimensions_label->setObjectName(QStringLiteral("imageDimensionsLabel"));
  dimensions_label->setToolTip(tr("arrShow-style dimensions for the selected standard ISMRMRD image pixels."));
  dimensions_layout->addWidget(dimensions_label, 0, Qt::AlignTop);
  image_dimensions_ = new ArrShowDimensionStrip(dimensions_row, QStringLiteral("image"));
  image_dimensions_->setObjectName(QStringLiteral("imageDimensions"));
  image_dimensions_->setToolTip(
    tr("arrShow-style dimensions: native X and Y are the locked ':' display plane. Later columns select fixed "
       "standard image coordinates."));
  dimensions_layout->addWidget(image_dimensions_, 0, Qt::AlignTop);
  dimensions_layout->addStretch(1);
  controls_layout->addWidget(dimensions_row);
  page.layout->addWidget(controls_card);

  auto* display_controls_card =
    make_surface(page.widget, QStringLiteral("imageDisplayControlsCard"), QStringLiteral("controls"));
  auto* display_controls = new QHBoxLayout(display_controls_card);
  display_controls->setContentsMargins(10, 6, 10, 6);
  display_controls->setSpacing(7);
  display_controls->addWidget(make_text(tr("Range"), "controlLabel", display_controls_card));
  image_range_calculation_ = new QComboBox(display_controls_card);
  image_range_calculation_->setObjectName(QStringLiteral("imageRangeCalculation"));
  image_range_calculation_->addItem(
    arrshow_range_calculation_label(ArrShowRangeCalculation::minimum_maximum, image_display_settings_.percentile),
    static_cast<int>(ArrShowRangeCalculation::minimum_maximum));
  image_range_calculation_->addItem(
    arrshow_range_calculation_label(ArrShowRangeCalculation::percentile, image_display_settings_.percentile),
    static_cast<int>(ArrShowRangeCalculation::percentile));
  display_controls->addWidget(image_range_calculation_);
  image_percentile_ = new QDoubleSpinBox(display_controls_card);
  image_percentile_->setObjectName(QStringLiteral("imageRangePercentile"));
  image_percentile_->setRange(0.01, 100.0);
  image_percentile_->setDecimals(2);
  image_percentile_->setSuffix(QStringLiteral("%"));
  image_percentile_->setToolTip(tr("arrShow symmetric percentile range; disabled for min/max."));
  display_controls->addWidget(image_percentile_);
  display_controls->addWidget(make_text(tr("C/W across planes"), "controlLabel", display_controls_card));
  image_window_persistence_ = new QComboBox(display_controls_card);
  image_window_persistence_->setObjectName(QStringLiteral("imageWindowPersistence"));
  for (const auto persistence :
       {ArrShowWindowPersistence::reset, ArrShowWindowPersistence::relative, ArrShowWindowPersistence::absolute}) {
    image_window_persistence_->addItem(arrshow_window_persistence_label(persistence), static_cast<int>(persistence));
  }
  image_window_persistence_->setCurrentIndex(
    image_window_persistence_->findData(static_cast<int>(ArrShowWindowPersistence::relative)));
  image_window_persistence_->setToolTip(
    tr("arrShow C/W policy for another image plane: reset, keep relative, or keep absolute."));
  display_controls->addWidget(image_window_persistence_);
  display_controls->addWidget(make_text(tr("Center"), "controlLabel", display_controls_card));
  image_window_center_ = new QDoubleSpinBox(display_controls_card);
  image_window_center_->setObjectName(QStringLiteral("imageWindowCenter"));
  image_window_center_->setDecimals(6);
  image_window_center_->setRange(-1.0e100, 1.0e100);
  image_window_center_->setKeyboardTracking(false);
  image_window_center_->setToolTip(
    tr("Manual window center. Middle-drag in the canvas adjusts it; edits re-inspect the plane."));
  display_controls->addWidget(image_window_center_);
  display_controls->addWidget(make_text(tr("Width"), "controlLabel", display_controls_card));
  image_window_width_ = new QDoubleSpinBox(display_controls_card);
  image_window_width_->setObjectName(QStringLiteral("imageWindowWidth"));
  image_window_width_->setDecimals(6);
  image_window_width_->setRange(std::numeric_limits<double>::min(), 1.0e100);
  image_window_width_->setValue(1.0);
  image_window_width_->setKeyboardTracking(false);
  image_window_width_->setToolTip(
    tr("Manual window width. Middle-drag in the canvas adjusts it; edits re-inspect the plane."));
  display_controls->addWidget(image_window_width_);
  image_reset_window_button_ = new QToolButton(display_controls_card);
  image_reset_window_button_->setObjectName(QStringLiteral("imageWindowResetButton"));
  image_reset_window_button_->setText(tr("Reset C/W"));
  image_reset_window_button_->setToolTip(tr("Restore full arrShow C/W for the selected plane."));
  display_controls->addWidget(image_reset_window_button_);
  display_controls->addWidget(make_text(tr("View"), "controlLabel", display_controls_card));
  image_zoom_percent_ = new QSpinBox(display_controls_card);
  image_zoom_percent_->setObjectName(QStringLiteral("imageZoomPercent"));
  image_zoom_percent_->setRange(25, 800);
  image_zoom_percent_->setSingleStep(25);
  image_zoom_percent_->setSuffix(QStringLiteral("%"));
  image_zoom_percent_->setValue(100);
  image_zoom_percent_->setToolTip(tr("100% fits the bounded display derivative; Ctrl+wheel changes this view scale."));
  display_controls->addWidget(image_zoom_percent_);
  image_fit_button_ = new QToolButton(display_controls_card);
  image_fit_button_->setObjectName(QStringLiteral("imageFitButton"));
  image_fit_button_->setText(tr("Fit"));
  image_fit_button_->setToolTip(tr("Fit the bounded display derivative and clear pan."));
  display_controls->addWidget(image_fit_button_);
  display_controls->addStretch(1);
  page.layout->addWidget(display_controls_card);

  connect(image_series_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    stop_image_cine();
    update_image_controls();
    clear_image_derivative_for_selection_change();
  });
  connect(image_ordinal_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
    stop_image_cine();
    update_image_controls();
    clear_image_derivative_for_selection_change();
  });
  image_dimensions_->set_value_changed_callback([this](const QString& dimension_identifier, const int value) {
    select_image_dimension_value(dimension_identifier, value);
  });
  connect(image_component_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    image_display_settings_.component = static_cast<ArrShowDisplayComponent>(image_component_->currentData().toInt());
    sync_image_arrshow_controls();
    const auto had_derivative = !image_presentation_.details.isEmpty();
    clear_image_derivative_for_selection_change();
    update_control_state();
    if (had_derivative) {
      load_image();
    }
  });
  connect(image_phase_representation_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    arrshow_convert_phase_window(image_display_settings_, static_cast<ArrShowPhaseRepresentation>(
                                                            image_phase_representation_->currentData().toInt()));
    if (arrshow_display_component_is_phase(image_display_settings_.component)) {
      arrshow_prepare_active_window_for_new_plane(image_display_settings_);
    }
    sync_image_arrshow_controls();
    if (!image_presentation_.details.isEmpty()) {
      load_image();
    }
  });
  connect(image_range_calculation_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    image_display_settings_.range_calculation =
      static_cast<ArrShowRangeCalculation>(image_range_calculation_->currentData().toInt());
    arrshow_prepare_active_window_for_new_plane(image_display_settings_);
    sync_image_arrshow_controls();
    if (!image_presentation_.details.isEmpty()) {
      load_image();
    }
  });
  connect(image_percentile_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] {
    image_display_settings_.percentile = image_percentile_->value();
    arrshow_prepare_active_window_for_new_plane(image_display_settings_);
    sync_image_arrshow_controls();
    if (!image_presentation_.details.isEmpty()) {
      load_image();
    }
  });
  connect(image_window_persistence_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    arrshow_active_window(image_display_settings_).persistence =
      static_cast<ArrShowWindowPersistence>(image_window_persistence_->currentData().toInt());
    arrshow_prepare_active_window_for_new_plane(image_display_settings_);
    sync_image_arrshow_controls();
    update_control_state();
    if (!image_window_drag_active_ && !image_presentation_.details.isEmpty()) {
      load_image();
    }
  });
  connect(image_zoom_percent_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
    if (image_image_ != nullptr) {
      image_image_->set_zoom_percent(image_zoom_percent_->value());
    }
  });
  connect(image_window_center_, &QDoubleSpinBox::editingFinished, this, [this] {
    update_image_arrshow_settings_from_controls();
    if (!image_presentation_.details.isEmpty()) {
      load_image();
    }
  });
  connect(image_window_width_, &QDoubleSpinBox::editingFinished, this, [this] {
    update_image_arrshow_settings_from_controls();
    if (!image_presentation_.details.isEmpty()) {
      load_image();
    }
  });
  connect(image_reset_window_button_, &QToolButton::clicked, this, [this] {
    reset_image_window();
  });
  connect(image_fit_button_, &QToolButton::clicked, this, [this] {
    if (image_image_ != nullptr) {
      image_image_->fit_to_view();
    }
    const QSignalBlocker blocker(image_zoom_percent_);
    image_zoom_percent_->setValue(100);
  });
  connect(image_cine_button_, &QToolButton::clicked, this, [this] {
    toggle_image_cine();
  });
  connect(load_image_button_, &QToolButton::clicked, this, [this] {
    load_image();
  });
  image_cine_timer_ = new QTimer(this);
  image_cine_timer_->setInterval(250);
  connect(image_cine_timer_, &QTimer::timeout, this, [this] {
    if (!inspection_session_.is_open() || image_series_->currentText().isEmpty() || image_ordinal_->maximum() <= 0) {
      stop_image_cine();
      return;
    }
    const auto next_ordinal = image_ordinal_->value() >= image_ordinal_->maximum() ? 0 : image_ordinal_->value() + 1;
    const QSignalBlocker ordinal_blocker(image_ordinal_);
    image_ordinal_->setValue(next_ordinal);
    arrshow_prepare_active_window_for_new_plane(image_display_settings_);
    load_image();
  });

  auto* view_splitter = new QSplitter(Qt::Horizontal, page.widget);
  view_splitter->setObjectName(QStringLiteral("imageViewSplitter"));
  view_splitter->setChildrenCollapsible(false);
  auto* canvas_card = make_surface(view_splitter, QStringLiteral("imageCanvasCard"), QStringLiteral("canvas"));
  auto* canvas_layout = new QVBoxLayout(canvas_card);
  canvas_layout->setContentsMargins(10, 8, 10, 8);
  canvas_layout->setSpacing(4);
  add_card_heading(canvas_layout, canvas_card, tr("Selected image plane"),
                   tr("Bounded standard image derivative · no new MRI artifact is created."));
  image_image_ = new InspectionCanvas(canvas_card);
  image_image_->setObjectName(QStringLiteral("imageCanvas"));
  image_image_->setAccessibleName(tr("Bounded standard ISMRMRD image display derivative"));
  image_image_->set_interaction_help(
    tr("Wheel active dimension · Ctrl+wheel zoom · drag pan · middle drag C/W · double-click reset C/W"));
  canvas_layout->addWidget(image_image_, 1);
  image_pixel_probe_ =
    make_text(tr("Pointer: inspect an image to probe the current display derivative."), "canvasProbe", canvas_card);
  image_pixel_probe_->setObjectName(QStringLiteral("imagePixelProbe"));
  canvas_layout->addWidget(image_pixel_probe_);
  image_image_->set_browse_step_callback([this](const int step) {
    step_image_plane(step);
  });
  image_image_->set_probe_callback([this](const QPoint display_pixel) {
    update_image_pixel_probe(display_pixel);
  });
  image_image_->set_window_level_callback([this](const QPointF drag_delta, const bool finished) {
    adjust_image_window_from_drag(drag_delta, finished);
  });
  image_image_->set_reset_window_callback([this] {
    reset_image_window();
  });
  image_image_->set_zoom_changed_callback([this](const int zoom_percent) {
    const QSignalBlocker blocker(image_zoom_percent_);
    image_zoom_percent_->setValue(zoom_percent);
  });
  view_splitter->addWidget(canvas_card);

  auto* detail_card = make_surface(view_splitter, QStringLiteral("imageDetailsCard"));
  auto* detail_layout = new QVBoxLayout(detail_card);
  detail_layout->setContentsMargins(18, 16, 18, 18);
  detail_layout->setSpacing(8);
  add_card_heading(detail_layout, detail_card, tr("Image details"),
                   tr("Dimensions, sample type, header fields, and meta attributes."));
  image_summary_ = make_read_only_text(detail_card);
  image_summary_->setObjectName(QStringLiteral("imageSummary"));
  image_summary_->setMinimumHeight(112);
  image_summary_->setMaximumHeight(180);
  detail_layout->addWidget(image_summary_);
  detail_layout->addWidget(make_text(tr("Display-derivative histogram"), "cardTitle", detail_card));
  image_histogram_ = new QTableWidget(detail_card);
  image_histogram_->setObjectName(QStringLiteral("imageHistogram"));
  image_histogram_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  image_histogram_->setSelectionMode(QAbstractItemView::NoSelection);
  image_histogram_->setAlternatingRowColors(true);
  image_histogram_->verticalHeader()->setVisible(false);
  image_histogram_->setMaximumHeight(238);
  detail_layout->addWidget(image_histogram_, 1);
  view_splitter->addWidget(detail_card);
  view_splitter->setStretchFactor(0, 3);
  view_splitter->setStretchFactor(1, 2);
  view_splitter->setSizes({640, 360});
  page.layout->addWidget(view_splitter, 1);
  object_inspector_->addTab(page.widget, tr("Image"));
  sync_image_arrshow_controls();
}

void ViewerWindow::create_pipeline_page() {
  const auto page =
    make_workspace_page(object_inspector_, QStringLiteral("pipelinePage"), tr("AUTHORED PIPELINE"), tr("Pipeline"),
                        tr("Read and validate a PipelineDefinition document without resolving, loading, compiling, "
                           "or executing a Provider."));
  pipeline_page_ = page.widget;
  pipeline_stack_ = new QStackedWidget(page.widget);
  pipeline_stack_->setObjectName(QStringLiteral("pipelineContentStack"));

  auto empty_state = make_empty_state(
    pipeline_stack_, QStringLiteral("pipelineEmptyState"), tr("JSON"), tr("Open a PipelineDefinition"),
    tr("The viewer parses the authored document for inspection only. It does not execute reconstruction."),
    tr("Open pipeline"));
  empty_state.action->setObjectName(QStringLiteral("pipelineOpenButton"));
  connect(empty_state.action, &QPushButton::clicked, open_pipeline_action_, &QAction::trigger);
  pipeline_stack_->addWidget(empty_state.widget);

  auto* content = new QWidget(pipeline_stack_);
  content->setObjectName(QStringLiteral("pipelineContent"));
  auto* content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(0, 0, 0, 0);
  content_layout->setSpacing(14);

  auto* summary_card = make_surface(content, QStringLiteral("pipelineSummaryCard"));
  auto* summary_layout = new QVBoxLayout(summary_card);
  summary_layout->setContentsMargins(18, 16, 18, 16);
  summary_layout->setSpacing(8);
  add_card_heading(summary_layout, summary_card, tr("Parse-only summary"),
                   tr("This view shares the public PipelineDefinition parser; it does not become a runtime plan."));
  pipeline_summary_ = make_read_only_text(summary_card);
  pipeline_summary_->setObjectName(QStringLiteral("pipelineSummary"));
  pipeline_summary_->setMinimumHeight(78);
  pipeline_summary_->setMaximumHeight(120);
  summary_layout->addWidget(pipeline_summary_);
  content_layout->addWidget(summary_card);

  auto* graph_card = make_surface(content, QStringLiteral("pipelineGraphCard"), QStringLiteral("canvas"));
  auto* graph_layout = new QVBoxLayout(graph_card);
  graph_layout->setContentsMargins(18, 16, 18, 18);
  graph_layout->setSpacing(8);
  add_card_heading(graph_layout, graph_card, tr("Authored pipeline graph"),
                   tr("A read-only view of declared ingress, operators, egress, and explicit edges. It is not an "
                      "ExecutionPlan or runtime graph."));
  pipeline_graph_view_ = new QGraphicsView(graph_card);
  pipeline_graph_view_->setObjectName(QStringLiteral("pipelineGraphCanvas"));
  pipeline_graph_scene_ = new QGraphicsScene(pipeline_graph_view_);
  pipeline_graph_view_->setScene(pipeline_graph_scene_);
  pipeline_graph_view_->setRenderHint(QPainter::Antialiasing, true);
  pipeline_graph_view_->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
  pipeline_graph_view_->setDragMode(QGraphicsView::ScrollHandDrag);
  pipeline_graph_view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  pipeline_graph_view_->setResizeAnchor(QGraphicsView::AnchorViewCenter);
  pipeline_graph_view_->setBackgroundBrush(QBrush(Qt::white));
  pipeline_graph_view_->setMinimumHeight(260);
  pipeline_graph_view_->setProperty("surfaceRole", QStringLiteral("canvas"));
  graph_layout->addWidget(pipeline_graph_view_, 1);
  auto* graph_legend = make_text(tr("Blue: ingress · white: authored operator · green: egress · dashed: calibration"),
                                 "sectionDetail", graph_card);
  graph_legend->setObjectName(QStringLiteral("pipelineGraphLegend"));
  graph_layout->addWidget(graph_legend);
  content_layout->addWidget(graph_card, 3);

  auto* json_card = make_surface(content, QStringLiteral("pipelineCanonicalJsonCard"));
  auto* json_layout = new QVBoxLayout(json_card);
  json_layout->setContentsMargins(18, 16, 18, 18);
  json_layout->setSpacing(8);
  add_card_heading(json_layout, json_card, tr("Canonical JSON"),
                   tr("Read-only normalized presentation of the authored PipelineDefinition."));
  pipeline_canonical_json_ = make_read_only_text(json_card);
  pipeline_canonical_json_->setObjectName(QStringLiteral("pipelineCanonicalJson"));
  json_layout->addWidget(pipeline_canonical_json_, 1);
  content_layout->addWidget(json_card, 2);
  pipeline_stack_->addWidget(content);

  page.layout->addWidget(pipeline_stack_, 1);
  object_inspector_->addTab(page.widget, tr("Pipeline"));
}

void ViewerWindow::inspect_selected_object() {
  auto* item = dataset_navigation_ == nullptr ? nullptr : dataset_navigation_->currentItem();
  if (item == nullptr) {
    return;
  }

  const auto kind = semantic_object_kind(item);
  if (kind == SemanticObjectKind::waveforms) {
    QMessageBox::information(this, tr("Waveform inspection unavailable"),
                             tr("This source contains standard ISMRMRD waveform storage, but waveform sample "
                                "inspection is not implemented yet."));
    return;
  }
  if (kind == SemanticObjectKind::source_file) {
    append_info(tr("Select a verified ISMRMRD container or content object, then choose Inspect."));
    return;
  }

  const auto view_data = item->data(0, kNavigationDefaultViewRole);
  if (!view_data.isValid()) {
    return;
  }
  const auto view = static_cast<WorkspaceView>(view_data.toInt());
  activate_navigation_item(item, view);
  if (kind == SemanticObjectKind::acquisitions && inspection_session_.is_open()) {
    // Auxiliary-only and non-Cartesian containers deliberately have no
    // renderable raw-coil dimension. Opening their semantic object should
    // surface the actionable availability diagnostic in the K-space tab,
    // rather than opening a modal error dialog for a display that cannot be
    // requested.
    if (kspace_catalog_.has_value()) {
      load_kspace();
    } else {
      refresh_kspace();
    }
  } else if (kind == SemanticObjectKind::images && inspection_session_.is_open()) {
    load_image();
  }
}

void ViewerWindow::open_selected_object_as() {
  auto* item = dataset_navigation_ == nullptr ? nullptr : dataset_navigation_->currentItem();
  if (item == nullptr) {
    return;
  }

  const auto kind = semantic_object_kind(item);
  if (kind == SemanticObjectKind::waveforms) {
    QMessageBox::information(this, tr("Waveform inspection unavailable"),
                             tr("No bounded waveform sample view is implemented yet."));
    return;
  }
  if (kind == SemanticObjectKind::source_file) {
    append_info(tr("Select a verified container or semantic content object before choosing Open As."));
    return;
  }

  struct OpenAsTarget {
    QString label;
    WorkspaceView view;
    bool materializes_payload{false};
  };
  std::vector<OpenAsTarget> targets;
  switch (kind) {
    case SemanticObjectKind::container:
    case SemanticObjectKind::header:
      targets.push_back({tr("ISMRMRD XML header"), WorkspaceView::metadata, false});
      break;
    case SemanticObjectKind::acquisitions:
      targets.push_back({tr("Acquisition header table"), WorkspaceView::kspace, false});
      targets.push_back({tr("Cartesian K-space viewer"), WorkspaceView::kspace, true});
      break;
    case SemanticObjectKind::images:
      targets.push_back({tr("Standard image_x image viewer"), WorkspaceView::image, true});
      break;
    case SemanticObjectKind::pipeline:
      targets.push_back({tr("PipelineDefinition document"), WorkspaceView::pipeline, false});
      break;
    case SemanticObjectKind::source_file:
    case SemanticObjectKind::waveforms:
      return;
  }
  QStringList choices;
  for (const auto& target : targets) {
    choices.append(target.label);
  }
  OpenAsDialog dialog(semantic_object_name(kind), choices, this);
  if (dialog.exec() != QDialog::Accepted || dialog.selected_choice() < 0 ||
      dialog.selected_choice() >= static_cast<int>(targets.size())) {
    return;
  }

  const auto target = targets.at(static_cast<std::size_t>(dialog.selected_choice()));
  activate_navigation_item(item, target.view);
  if (target.materializes_payload) {
    if (kind == SemanticObjectKind::acquisitions) {
      if (kspace_catalog_.has_value()) {
        load_kspace();
      } else {
        refresh_kspace();
      }
    } else if (kind == SemanticObjectKind::images) {
      load_image();
    }
  }
}

void ViewerWindow::copy_selected_object_path() {
  auto* item = dataset_navigation_ == nullptr ? nullptr : dataset_navigation_->currentItem();
  if (item == nullptr) {
    return;
  }

  const auto kind = semantic_object_kind(item);
  QString path = item->data(0, kNavigationContainerRole).toString();
  if (kind == SemanticObjectKind::source_file) {
    path = inspection_session_.source_path();
  } else if (kind == SemanticObjectKind::pipeline) {
    path = pipeline_presentation_.details.value(QStringLiteral("source")).toString();
  }
  if (path.isEmpty()) {
    return;
  }
  QGuiApplication::clipboard()->setText(path);
  append_info(tr("Copied object path: %1").arg(path));
}

void ViewerWindow::show_object_context_menu(const QPoint& position) {
  if (dataset_navigation_ == nullptr) {
    return;
  }
  auto* item = dataset_navigation_->itemAt(position);
  if (item == nullptr || !(item->flags() & Qt::ItemIsEnabled)) {
    return;
  }
  dataset_navigation_->setCurrentItem(item);
  const auto kind = semantic_object_kind(item);

  QMenu menu(this);
  menu.setObjectName(QStringLiteral("semanticObjectContextMenu"));
  if (inspect_object_action_->isEnabled()) {
    menu.addAction(inspect_object_action_);
  }
  if (open_as_action_->isEnabled()) {
    menu.addAction(open_as_action_);
  }
  if (copy_object_path_action_->isEnabled()) {
    menu.addAction(copy_object_path_action_);
  }
  if (kind == SemanticObjectKind::source_file && close_mrd_action_->isEnabled()) {
    menu.addSeparator();
    menu.addAction(close_mrd_action_);
  }
  if (!menu.actions().isEmpty()) {
    menu.exec(dataset_navigation_->viewport()->mapToGlobal(position));
  }
}

void ViewerWindow::activate_navigation_item(QTreeWidgetItem* item, const WorkspaceView view) {
  if (item == nullptr) {
    return;
  }

  QString error;
  if (!activate_navigation_container(item, error)) {
    show_viewer_warning(this, tr("Cannot select ISMRMRD container"), error, "select_container");
    rebuild_dataset_navigation();
    return;
  }
  set_workspace_view(view);
}

bool ViewerWindow::activate_navigation_container(QTreeWidgetItem* item, QString& error) {
  error.clear();
  if (item == nullptr) {
    return true;
  }
  const auto requested_container = item->data(0, kNavigationContainerRole).toString();
  if (requested_container.isEmpty()) {
    return true;
  }
  if (!inspection_session_.is_open()) {
    error = tr("open an ISMRMRD source before inspecting its semantic objects");
    return false;
  }
  if (requested_container == inspection_session_.container_path()) {
    return true;
  }
  if (!inspection_session_.select_container(requested_container, error)) {
    return false;
  }

  metadata_view_open_ = false;
  clear_dataset_derivatives();
  refresh_metadata();
  refresh_kspace();
  refresh_image();
  append_info(tr("Opened standard ISMRMRD container %1. Previous display derivatives were cleared.")
                .arg(inspection_session_.container_path()));
  return true;
}

void ViewerWindow::rebuild_dataset_navigation() {
  if (dataset_navigation_ == nullptr) {
    return;
  }

  const auto* previous_item = dataset_navigation_->currentItem();
  const auto previous_kind = semantic_object_kind(previous_item);
  const auto previous_container =
    previous_item == nullptr ? QString{} : previous_item->data(0, kNavigationContainerRole).toString();
  const QSignalBlocker blocker(dataset_navigation_);
  dataset_navigation_->clear();
  QTreeWidgetItem* selected_item = nullptr;

  const auto select_if_previous = [&selected_item, previous_kind, &previous_container](QTreeWidgetItem* item) {
    if (item != nullptr && semantic_object_kind(item) == previous_kind &&
        item->data(0, kNavigationContainerRole).toString() == previous_container) {
      selected_item = item;
    }
  };
  const auto configure_object =
    [this, &select_if_previous](QTreeWidgetItem* item, const SemanticObjectKind kind, const QString& container_path,
                                const WorkspaceView default_view, const bool available, const QString& tool_tip) {
      item->setData(0, kNavigationKindRole, static_cast<int>(kind));
      item->setData(0, kNavigationContainerRole, container_path);
      item->setData(0, kNavigationDefaultViewRole, static_cast<int>(default_view));
      item->setToolTip(0, tool_tip);
      switch (kind) {
        case SemanticObjectKind::source_file:
        case SemanticObjectKind::header:
        case SemanticObjectKind::images:
        case SemanticObjectKind::pipeline:
          item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
          break;
        case SemanticObjectKind::container:
        case SemanticObjectKind::waveforms:
          item->setIcon(0, style()->standardIcon(QStyle::SP_DirOpenIcon));
          break;
        case SemanticObjectKind::acquisitions:
          item->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
          break;
      }
      if (!available) {
        item->setFlags(item->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable));
      }
      select_if_previous(item);
    };
  const auto make_root = [this, &configure_object](const QString& text, const SemanticObjectKind kind,
                                                   const QString& container_path, const WorkspaceView default_view,
                                                   const bool available, const QString& tool_tip) {
    auto* item = new QTreeWidgetItem(dataset_navigation_, {text});
    configure_object(item, kind, container_path, default_view, available, tool_tip);
    return item;
  };
  const auto make_child = [&configure_object](QTreeWidgetItem* parent, const QString& text,
                                              const SemanticObjectKind kind, const QString& container_path,
                                              const WorkspaceView default_view, const bool available,
                                              const QString& tool_tip) {
    auto* item = new QTreeWidgetItem(parent, {text});
    configure_object(item, kind, container_path, default_view, available, tool_tip);
    return item;
  };

  if (!inspection_session_.is_open()) {
    auto* empty_item =
      make_root(tr("No ISMRMRD source open"), SemanticObjectKind::source_file, {}, WorkspaceView::metadata, false,
                tr("Open a local standard ISMRMRD file to inspect its verified semantic objects."));
    selected_item = empty_item;
  } else {
    const auto source_path = inspection_session_.source_path();
    auto* source_item = make_root(QFileInfo(source_path).fileName(), SemanticObjectKind::source_file, {},
                                  WorkspaceView::metadata, true, source_path);
    source_item->setExpanded(true);
    if (selected_item == nullptr && previous_item == nullptr) {
      selected_item = source_item;
    }

    for (const auto& descriptor : inspection_session_.available_containers()) {
      const auto container_path =
        QString::fromUtf8(descriptor.path.data(), static_cast<qsizetype>(descriptor.path.size()));
      const auto container_description =
        descriptor.has_header
          ? tr("Verified standard ISMRMRD container: %1\nAcquisition records: %2\nImage series: %3\n"
               "XML header: %4\nWaveforms: %5")
              .arg(container_path)
              .arg(descriptor.acquisition_count)
              .arg(descriptor.image_series_count)
              .arg(descriptor.has_header ? tr("present") : tr("not present"))
              .arg(descriptor.has_waveforms ? tr("present") : tr("not present"))
          : tr("Verified standalone standard ISMRMRD image-series container: %1\nAcquisition records: %2\n"
               "Image series: %3\nXML header: %4\nWaveforms: %5")
              .arg(container_path)
              .arg(descriptor.acquisition_count)
              .arg(descriptor.image_series_count)
              .arg(descriptor.has_header ? tr("present") : tr("not present"))
              .arg(descriptor.has_waveforms ? tr("present") : tr("not present"));
      // Keep the navigation label to the HDF5 identity. The inspector and tooltip carry the counts and semantic detail;
      // child entries already make raw acquisitions, images and waveforms directly discoverable.
      auto* container_item = make_child(source_item, container_path, SemanticObjectKind::container, container_path,
                                        WorkspaceView::metadata, true, container_description);
      container_item->setData(0, Qt::AccessibleTextRole, container_path);
      container_item->setData(0, Qt::AccessibleDescriptionRole, container_description);

      make_child(container_item, tr("Header / XML"), SemanticObjectKind::header, container_path,
                 WorkspaceView::metadata, descriptor.has_header,
                 descriptor.has_header ? tr("Inspect the bounded standard XML header.")
                                       : tr("This container has no standard XML header."));
      make_child(container_item, tr("Acquisitions / K-space"), SemanticObjectKind::acquisitions, container_path,
                 WorkspaceView::kspace, descriptor.has_acquisitions && descriptor.acquisition_count > 0U,
                 descriptor.has_acquisitions
                   ? tr("Display a bounded raw Cartesian K-space plane or inspect its header.")
                   : tr("This container has no standard acquisitions."));
      if (descriptor.has_images && descriptor.image_series_count > 0U) {
        make_child(container_item, tr("Images"), SemanticObjectKind::images, container_path, WorkspaceView::image, true,
                   tr("Open a bounded standard ISMRMRD image_x view."));
      }
      if (descriptor.has_waveforms) {
        make_child(container_item, tr("Waveforms (discovered)"), SemanticObjectKind::waveforms, container_path,
                   WorkspaceView::metadata, false,
                   tr("Standard waveform storage is present, but sample inspection is unavailable."));
      }
      container_item->setExpanded(true);
    }
  }

  if (!pipeline_presentation_.details.isEmpty()) {
    auto* pipeline_item = make_root(tr("Pipeline"), SemanticObjectKind::pipeline, {}, WorkspaceView::pipeline, true,
                                    pipeline_presentation_.details.value(QStringLiteral("source")).toString());
    if (selected_item == nullptr && previous_kind == SemanticObjectKind::pipeline) {
      selected_item = pipeline_item;
    }
  }
  if (selected_item != nullptr) {
    dataset_navigation_->setCurrentItem(selected_item);
  }
  update_object_inspector(dataset_navigation_->currentItem());
  update_workspace_tab_visibility(dataset_navigation_->currentItem());
  update_selection_actions();
}

void ViewerWindow::update_object_inspector(QTreeWidgetItem* item) {
  if (object_general_ == nullptr || object_name_field_ == nullptr || object_path_field_ == nullptr ||
      object_type_field_ == nullptr || object_access_field_ == nullptr || object_semantics_table_ == nullptr ||
      object_members_table_ == nullptr) {
    return;
  }

  const auto set_semantics_rows = [this](const QList<QStringList>& rows) {
    set_table_contents(object_semantics_table_, {QString{}, QString{}}, rows);
    object_semantics_table_->horizontalHeader()->setVisible(false);
    object_semantics_table_->setColumnWidth(0, 180);
    object_semantics_table_->resizeRowsToContents();
    auto visible_height = object_semantics_table_->frameWidth() * 2;
    for (int row = 0; row < object_semantics_table_->rowCount(); ++row) {
      visible_height += object_semantics_table_->rowHeight(row);
    }
    object_semantics_table_->setFixedHeight(std::max(46, visible_height));
  };
  const auto set_members_rows = [this](const QList<QStringList>& rows) {
    set_table_contents(object_members_table_, {tr("Name"), tr("Type"), tr("Array Size")}, rows);
    object_members_table_->setMinimumHeight(rows.isEmpty() ? 46 : 176);
  };
  if (item == nullptr) {
    object_name_field_->setText(tr("No object selected"));
    object_path_field_->clear();
    object_type_field_->setText(tr("ISMRMRD semantic object"));
    object_access_field_->setText(tr("Read-only"));
    set_semantics_rows({{tr("Status:"), tr("Select an object in the ISMRMRD file hierarchy.")}});
    set_members_rows({});
    return;
  }

  const auto kind = semantic_object_kind(item);
  const auto container_path = item->data(0, kNavigationContainerRole).toString();
  const auto container_child_path = [&container_path](const QString& child) {
    if (container_path.isEmpty()) {
      return child;
    }
    return container_path == QStringLiteral("/") ? QStringLiteral("/%1").arg(child)
                                                 : QStringLiteral("%1/%2").arg(container_path, child);
  };

  QString name = item->text(0);
  QString path = container_path;
  QString type = semantic_object_name(kind);
  QList<QStringList> semantic_rows;
  QList<QStringList> member_rows;
  if (kind == SemanticObjectKind::source_file) {
    if (inspection_session_.is_open()) {
      name = QFileInfo(inspection_session_.source_path()).fileName();
      path = inspection_session_.source_path();
      type = tr("ISMRMRD HDF5 file");
      semantic_rows = {{tr("Active container:"), inspection_session_.container_path()},
                       {tr("Verified containers:"), QString::number(inspection_session_.available_containers().size())},
                       {tr("Inspection mode:"), tr("Recursive, bounded, read-only semantic discovery")}};
    } else {
      name = tr("No ISMRMRD source open");
      path.clear();
      type = tr("Local file source");
      semantic_rows = {{tr("Status:"), tr("Open a local standard ISMRMRD file to inspect it.")}};
    }
  } else if (kind == SemanticObjectKind::pipeline) {
    path = pipeline_presentation_.details.value(QStringLiteral("source")).toString();
    name = QFileInfo(path).fileName();
    type = tr("PipelineDefinition JSON document");
    semantic_rows = {{tr("Mode:"), tr("Parsed only")},
                     {tr("Provider resolution:"), tr("Unavailable")},
                     {tr("Compilation / execution:"), tr("Unavailable")}};
  } else {
    const auto found =
      std::find_if(inspection_session_.available_containers().begin(), inspection_session_.available_containers().end(),
                   [&container_path](const auto& descriptor) {
                     return QString::fromUtf8(descriptor.path.data(), static_cast<qsizetype>(descriptor.path.size())) ==
                            container_path;
                   });
    if (found != inspection_session_.available_containers().end()) {
      semantic_rows = {{tr("Container:"), container_path},
                       {tr("Acquisition records:"), QString::number(found->acquisition_count)},
                       {tr("Image series:"), QString::number(found->image_series_count)},
                       {tr("XML header:"), found->has_header ? tr("present") : tr("not present")},
                       {tr("Waveforms:"), found->has_waveforms ? tr("present") : tr("not present")}};
    }
    switch (kind) {
      case SemanticObjectKind::container:
        name = QFileInfo(container_path).fileName();
        type = tr("Standard ISMRMRD container");
        member_rows = {{tr("xml"), tr("UTF-8 XML header"), tr("1")},
                       {tr("data"), tr("ISMRMRD acquisition dataset"), tr("0 or more")},
                       {tr("waveforms"), tr("ISMRMRD waveform dataset"), tr("0 or more")},
                       {tr("image_x"), tr("Standard ISMRMRD image series"), tr("0 or more")}};
        break;
      case SemanticObjectKind::header:
        name = tr("xml");
        path = container_child_path(QStringLiteral("xml"));
        type = tr("ISMRMRD XML header dataset");
        member_rows = {{tr("xml"), tr("Variable-length UTF-8 XML"), tr("1")}};
        break;
      case SemanticObjectKind::acquisitions:
        name = tr("data");
        path = container_child_path(QStringLiteral("data"));
        type = tr("ISMRMRD acquisition dataset");
        member_rows = {
          {tr("head.version"), tr("16-bit unsigned integer"), tr("1")},
          {tr("head.flags"), tr("64-bit unsigned integer"), tr("1")},
          {tr("head.measurement_uid"), tr("32-bit unsigned integer"), tr("1")},
          {tr("head.scan_counter"), tr("32-bit unsigned integer"), tr("1")},
          {tr("head.physiology_time_stamp"), tr("32-bit unsigned integer"), tr("3")},
          {tr("head.number_of_samples"), tr("16-bit unsigned integer"), tr("1")},
          {tr("head.available_channels"), tr("16-bit unsigned integer"), tr("1")},
          {tr("head.active_channels"), tr("16-bit unsigned integer"), tr("1")},
          {tr("head.channel_mask"), tr("64-bit unsigned integer"), tr("16")},
          {tr("head.encoding_space_ref"), tr("16-bit unsigned integer"), tr("1")},
          {tr("head.trajectory_dimensions"), tr("16-bit unsigned integer"), tr("1")},
          {tr("head.position / read_dir / phase_dir / slice_dir"), tr("32-bit floating-point"), tr("3 each")},
          {tr("Encoding counters"), tr("ISMRMRD encoding counters"), tr("standard fields")},
          {tr("traj"), tr("Variable-length 32-bit floating-point"), tr("trajectory components")},
          {tr("data"), tr("Variable-length complex 32-bit floating-point"), tr("samples by channels")}};
        break;
      case SemanticObjectKind::images:
        name = tr("image_x");
        type = tr("Standard ISMRMRD image series");
        member_rows = {{tr("header"), tr("ISMRMRD ImageHeader"), tr("one per image")},
                       {tr("attributes"), tr("Variable-length MetaAttributes XML"), tr("one per image")},
                       {tr("data"), tr("ISMRMRD image pixels"), tr("one per image")}};
        break;
      case SemanticObjectKind::waveforms:
        name = tr("waveforms");
        path = container_child_path(QStringLiteral("waveforms"));
        type = tr("ISMRMRD waveform dataset");
        member_rows = {{tr("head"), tr("ISMRMRD waveform header"), tr("one per waveform")},
                       {tr("data"), tr("Variable-length 32-bit unsigned integer"), tr("one per waveform")}};
        break;
      case SemanticObjectKind::source_file:
      case SemanticObjectKind::pipeline:
        break;
    }
  }

  object_name_field_->setText(name);
  object_path_field_->setText(path);
  object_type_field_->setText(type);
  object_access_field_->setText(tr("Read-only standard ISMRMRD semantic view"));
  set_semantics_rows(semantic_rows);
  set_members_rows(member_rows);
}

void ViewerWindow::update_selection_actions() {
  auto* item = dataset_navigation_ == nullptr ? nullptr : dataset_navigation_->currentItem();
  const auto selection_enabled = item != nullptr && (item->flags() & Qt::ItemIsEnabled);
  const auto kind = semantic_object_kind(item);
  const auto supports_inspection =
    selection_enabled && kind != SemanticObjectKind::source_file && kind != SemanticObjectKind::waveforms;
  inspect_object_action_->setEnabled(supports_inspection);
  open_as_action_->setEnabled(supports_inspection);

  QString path;
  if (item != nullptr) {
    path = item->data(0, kNavigationContainerRole).toString();
    if (kind == SemanticObjectKind::source_file) {
      path = inspection_session_.source_path();
    } else if (kind == SemanticObjectKind::pipeline) {
      path = pipeline_presentation_.details.value(QStringLiteral("source")).toString();
    }
  }
  copy_object_path_action_->setEnabled(selection_enabled && !path.isEmpty());
  close_mrd_action_->setEnabled(inspection_session_.is_open());
}

void ViewerWindow::clear_dataset_derivatives() {
  stop_image_cine();
  kspace_presentation_ = {};
  kspace_availability_error_.clear();
  if (kspace_acquisition_type_ != nullptr) {
    const QSignalBlocker acquisition_type_blocker(kspace_acquisition_type_);
    const auto imaging_index =
      kspace_acquisition_type_->findData(static_cast<int>(CartesianKspaceAcquisitionKind::imaging));
    if (imaging_index >= 0) {
      kspace_acquisition_type_->setCurrentIndex(imaging_index);
    }
  }
  kspace_catalog_.reset();
  selected_kspace_dimension_selection_ = {.first_identifier = QStringLiteral("readout"),
                                          .second_identifier = QStringLiteral("phase-encode")};
  selected_kspace_axes_ = {};
  selected_kspace_coordinate_ = {};
  if (kspace_dimensions_ != nullptr) {
    kspace_dimensions_->set_dimensions({});
  }
  kspace_display_settings_.value_window.persistence = ArrShowWindowPersistence::relative;
  kspace_display_settings_.value_window.relative_center = 0.5;
  kspace_display_settings_.value_window.relative_width = 1.0;
  kspace_display_settings_.value_window.has_current_window = false;
  kspace_display_settings_.phase_window.persistence = ArrShowWindowPersistence::relative;
  kspace_display_settings_.phase_window.relative_center = 0.5;
  kspace_display_settings_.phase_window.relative_width = 1.0;
  kspace_display_settings_.phase_window.has_current_window = false;
  image_presentation_ = {};
  image_source_dimensions_.reset();
  selected_image_axes_ = {};
  selected_image_coordinate_ = {};
  if (image_dimensions_ != nullptr) {
    image_dimensions_->set_dimensions({});
  }
  image_display_settings_.value_window.persistence = ArrShowWindowPersistence::relative;
  image_display_settings_.value_window.relative_center = 0.5;
  image_display_settings_.value_window.relative_width = 1.0;
  image_display_settings_.value_window.has_current_window = false;
  image_display_settings_.phase_window.persistence = ArrShowWindowPersistence::relative;
  image_display_settings_.phase_window.relative_center = 0.5;
  image_display_settings_.phase_window.relative_width = 1.0;
  image_display_settings_.phase_window.has_current_window = false;
  image_window_drag_active_ = false;
  kspace_window_drag_active_ = false;
  sync_kspace_arrshow_controls();
  sync_image_arrshow_controls();
}

QWidget* ViewerWindow::workspace_page(const WorkspaceView view) const {
  switch (view) {
    case WorkspaceView::metadata:
      return metadata_page_;
    case WorkspaceView::kspace:
      return kspace_page_;
    case WorkspaceView::image:
      return image_page_;
    case WorkspaceView::pipeline:
      return pipeline_page_;
  }
  return nullptr;
}

void ViewerWindow::update_workspace_tab_visibility(QTreeWidgetItem* item) {
  if (object_inspector_ == nullptr) {
    return;
  }

  const auto kind = semantic_object_kind(item);
  const auto selection_enabled = item != nullptr && (item->flags() & Qt::ItemIsEnabled);
  const auto selected_container = item == nullptr ? QString{} : item->data(0, kNavigationContainerRole).toString();
  const auto is_active_mrd_object = selection_enabled && inspection_session_.is_open() &&
                                    !selected_container.isEmpty() &&
                                    selected_container == inspection_session_.container_path();

  const auto metadata_visible = is_active_mrd_object && kind == SemanticObjectKind::header;
  const auto kspace_visible = is_active_mrd_object && kind == SemanticObjectKind::acquisitions &&
                              inspection_session_.metadata().acquisition_count > 0U;
  const auto image_visible =
    is_active_mrd_object && kind == SemanticObjectKind::images && !inspection_session_.metadata().image_series.empty();
  const auto pipeline_visible = !pipeline_presentation_.details.isEmpty();

  const auto current_page = object_inspector_->currentWidget();
  const auto page_will_hide =
    (current_page == metadata_page_ && !metadata_visible) || (current_page == kspace_page_ && !kspace_visible) ||
    (current_page == image_page_ && !image_visible) || (current_page == pipeline_page_ && !pipeline_visible);
  const QSignalBlocker blocker(object_inspector_);
  const auto set_visible = [this](QWidget* page, const bool visible) {
    if (page == nullptr) {
      return;
    }
    const auto index = object_inspector_->indexOf(page);
    if (index >= 0) {
      object_inspector_->setTabVisible(index, visible);
    }
  };
  set_visible(kspace_page_, kspace_visible);
  set_visible(metadata_page_, metadata_visible);
  set_visible(image_page_, image_visible);
  set_visible(pipeline_page_, pipeline_visible);
  if (page_will_hide && object_general_ != nullptr) {
    object_inspector_->setCurrentWidget(object_general_);
  }
  update_export_availability();
}

void ViewerWindow::activate_workspace_view(const WorkspaceView view) {
  if (object_inspector_ == nullptr) {
    return;
  }
  if (view == WorkspaceView::metadata) {
    metadata_view_open_ = inspection_session_.is_open();
    refresh_metadata_content();
    metadata_stack_->setCurrentIndex(metadata_view_open_ ? 1 : 0);
  } else if (view == WorkspaceView::kspace) {
    if (kspace_presentation_.details.isEmpty() && kspace_catalog_.has_value()) {
      load_kspace();
    }
  }
  update_export_availability();
}

void ViewerWindow::set_workspace_view(const WorkspaceView view) {
  if (object_inspector_ == nullptr) {
    return;
  }

  const auto is_available = [this, view] {
    switch (view) {
      case WorkspaceView::metadata:
        return inspection_session_.is_open();
      case WorkspaceView::kspace:
        return inspection_session_.is_open() && inspection_session_.metadata().acquisition_count > 0U;
      case WorkspaceView::image:
        return inspection_session_.is_open() && !inspection_session_.metadata().image_series.empty();
      case WorkspaceView::pipeline:
        return !pipeline_presentation_.details.isEmpty();
    }
    return false;
  };
  if (!is_available()) {
    if (object_general_ != nullptr) {
      object_inspector_->setCurrentWidget(object_general_);
    }
    update_export_availability();
    return;
  }

  auto* page = workspace_page(view);
  if (page == nullptr) {
    return;
  }
  const auto index = object_inspector_->indexOf(page);
  if (index < 0) {
    return;
  }
  object_inspector_->setTabVisible(index, true);
  object_inspector_->setCurrentWidget(page);
  activate_workspace_view(view);
}

void ViewerWindow::open_mrd() {
  const auto file_path = QFileDialog::getOpenFileName(this, tr("Open standard ISMRMRD file"), initial_open_directory(),
                                                      tr("ISMRMRD files (*.mrd *.h5 *.hdf5 *.ismrmrd)"));
  if (file_path.isEmpty()) {
    return;
  }

  QString error;
  if (!open_mrd_source(file_path, error)) {
    show_viewer_error(this, tr("Cannot open ISMRMRD file"), error, "open_mrd");
  }
}

bool ViewerWindow::open_mrd_source(const QString& file_path, QString& error) {
  const auto trimmed_path = file_path.trimmed();
  if (trimmed_path.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
      trimmed_path.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
      trimmed_path.startsWith(QStringLiteral("ftp://"), Qt::CaseInsensitive)) {
    error = tr("KSpaceJet Viewer opens local standard ISMRMRD files only; URLs are not supported");
    return false;
  }
  if (!inspection_session_.open_mrd(trimmed_path, error)) {
    return false;
  }
  remember_successful_open_directory(inspection_session_.source_path());
  remember_recent_file(inspection_session_.source_path(), false);
  metadata_view_open_ = false;
  clear_dataset_derivatives();
  refresh_metadata();
  refresh_kspace();
  refresh_image();
  append_info(tr("Opened %1 and selected standard container %2. %3 readable container(s) are available; "
                 "payloads are read only when explicitly inspected.")
                .arg(inspection_session_.source_path(), inspection_session_.container_path())
                .arg(inspection_session_.available_containers().size()));
  return true;
}

void ViewerWindow::close_mrd_source() {
  if (!inspection_session_.is_open()) {
    return;
  }
  const auto closed_path = inspection_session_.source_path();
  stop_image_cine();
  inspection_session_ = InspectionSession{};
  metadata_view_open_ = false;
  clear_dataset_derivatives();
  refresh_metadata();
  refresh_kspace();
  refresh_image();
  append_info(tr("Closed read-only source: %1").arg(closed_path));
}

void ViewerWindow::open_pipeline() {
  const auto file_path = QFileDialog::getOpenFileName(this, tr("Open PipelineDefinition"), initial_open_directory(),
                                                      tr("Pipeline JSON (*.json);;All files (*)"));
  if (file_path.isEmpty()) {
    return;
  }

  QString error;
  if (!open_pipeline_source(file_path, error)) {
    show_viewer_error(this, tr("Cannot open PipelineDefinition"), error, "open_pipeline");
  }
}

bool ViewerWindow::open_pipeline_source(const QString& file_path, QString& error) {
  const auto trimmed_path = file_path.trimmed();
  if (trimmed_path.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
      trimmed_path.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
      trimmed_path.startsWith(QStringLiteral("ftp://"), Qt::CaseInsensitive)) {
    error = tr("KSpaceJet Viewer opens local PipelineDefinition JSON files only; URLs are not supported");
    return false;
  }

  PipelinePresentation next_presentation;
  if (!load_pipeline_presentation(trimmed_path, next_presentation, error)) {
    return false;
  }

  pipeline_presentation_ = next_presentation;
  remember_successful_open_directory(trimmed_path);
  remember_recent_file(trimmed_path, true);
  refresh_pipeline();
  rebuild_dataset_navigation();
  set_workspace_view(WorkspaceView::pipeline);
  append_info(tr("Parsed PipelineDefinition without resolving or executing it."));
  return true;
}

QString ViewerWindow::initial_open_directory() const {
  return !last_open_directory_.isEmpty() && QDir(last_open_directory_).exists() ? last_open_directory_ : QString{};
}

void ViewerWindow::remember_successful_open_directory(const QString& file_path) {
  const auto directory = QFileInfo(file_path).absoluteDir().absolutePath();
  if (directory.isEmpty() || !QDir(directory).exists()) {
    return;
  }
  last_open_directory_ = QDir::cleanPath(directory);
  auto settings = viewer_settings();
  settings.setValue(QString::fromLatin1(kLastOpenDirectorySettingsKey), last_open_directory_);
}

void ViewerWindow::restore_recent_files() {
  recent_files_.clear();
  auto settings = viewer_settings();
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(
    settings.value(QString::fromLatin1(kRecentFilesSettingsKey)).toString().toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isArray()) {
    return;
  }
  for (const auto& value : document.array()) {
    const auto record = value.toObject();
    const auto path = record.value(QStringLiteral("path")).toString().trimmed();
    const auto kind = record.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("mrd") && kind != QStringLiteral("pipeline")) {
      continue;
    }
    const auto is_pipeline = kind == QStringLiteral("pipeline");
    const QFileInfo file_info(path);
    if (path.isEmpty() || !file_info.isFile()) {
      continue;
    }
    const auto normalized_path = QDir::cleanPath(file_info.absoluteFilePath());
    const auto already_present =
      std::any_of(recent_files_.cbegin(), recent_files_.cend(), [&normalized_path](const auto& entry) {
        return entry.path == normalized_path;
      });
    if (already_present) {
      continue;
    }
    recent_files_.append({.path = normalized_path, .is_pipeline = is_pipeline});
    if (recent_files_.size() >= kMaximumRecentFiles) {
      break;
    }
  }
  persist_recent_files();
}

void ViewerWindow::persist_recent_files() const {
  QJsonArray records;
  for (const auto& entry : recent_files_) {
    QJsonObject record;
    record.insert(QStringLiteral("path"), entry.path);
    record.insert(QStringLiteral("kind"), entry.is_pipeline ? QStringLiteral("pipeline") : QStringLiteral("mrd"));
    records.append(record);
  }
  auto settings = viewer_settings();
  settings.setValue(QString::fromLatin1(kRecentFilesSettingsKey),
                    QString::fromUtf8(QJsonDocument(records).toJson(QJsonDocument::Compact)));
}

void ViewerWindow::remember_recent_file(const QString& file_path, const bool is_pipeline) {
  const QFileInfo file_info(file_path);
  if (!file_info.isFile()) {
    return;
  }
  const auto normalized_path = QDir::cleanPath(file_info.absoluteFilePath());
  recent_files_.erase(std::remove_if(recent_files_.begin(), recent_files_.end(),
                                     [&normalized_path](const auto& entry) {
                                       return entry.path == normalized_path;
                                     }),
                      recent_files_.end());
  recent_files_.prepend({.path = normalized_path, .is_pipeline = is_pipeline});
  while (recent_files_.size() > kMaximumRecentFiles) {
    recent_files_.removeLast();
  }
  persist_recent_files();
  refresh_recent_files_menu();
}

void ViewerWindow::refresh_recent_files_menu() {
  if (recent_files_menu_ == nullptr) {
    return;
  }
  recent_files_menu_->clear();
  recent_files_menu_->setEnabled(!recent_files_.isEmpty());
  for (qsizetype index = 0; index < recent_files_.size(); ++index) {
    const auto& entry = recent_files_.at(index);
    const auto source_kind = entry.is_pipeline ? tr("PipelineDefinition") : tr("ISMRMRD");
    const auto source_path = QDir::toNativeSeparators(entry.path);
    auto* action = recent_files_menu_->addAction(tr("%1: %2").arg(source_kind, source_path));
    action->setObjectName(QStringLiteral("recentFileAction%1").arg(index));
    action->setToolTip(tr("%1 source: %2").arg(source_kind, source_path));
    action->setData(entry.path);
    action->setProperty("isPipelineRecentFile", entry.is_pipeline);
    connect(action, &QAction::triggered, this, [this, file_path = entry.path, is_pipeline = entry.is_pipeline] {
      QTimer::singleShot(0, this, [this, file_path, is_pipeline] {
        open_recent_file(file_path, is_pipeline);
      });
    });
  }
}

void ViewerWindow::open_recent_file(const QString& file_path, const bool is_pipeline) {
  QString error;
  const auto opened = is_pipeline ? open_pipeline_source(file_path, error) : open_mrd_source(file_path, error);
  if (!opened) {
    show_viewer_error(this, is_pipeline ? tr("Cannot open PipelineDefinition") : tr("Cannot open ISMRMRD file"), error,
                      is_pipeline ? "open_pipeline" : "open_mrd");
  }
}

void ViewerWindow::update_kspace_arrshow_settings_from_controls() {
  if (kspace_component_ == nullptr || kspace_phase_representation_ == nullptr || kspace_range_calculation_ == nullptr ||
      kspace_percentile_ == nullptr || kspace_window_persistence_ == nullptr || kspace_window_center_ == nullptr ||
      kspace_window_width_ == nullptr) {
    return;
  }
  kspace_display_settings_.component = static_cast<ArrShowDisplayComponent>(kspace_component_->currentData().toInt());
  arrshow_convert_phase_window(kspace_display_settings_, static_cast<ArrShowPhaseRepresentation>(
                                                           kspace_phase_representation_->currentData().toInt()));
  kspace_display_settings_.range_calculation =
    static_cast<ArrShowRangeCalculation>(kspace_range_calculation_->currentData().toInt());
  kspace_display_settings_.percentile = kspace_percentile_->value();
  auto& window = arrshow_active_window(kspace_display_settings_);
  window.persistence = static_cast<ArrShowWindowPersistence>(kspace_window_persistence_->currentData().toInt());
  const auto center = kspace_window_center_->value();
  const auto width = kspace_window_width_->value();
  const auto active_component = arrshow_display_component_identifier(kspace_display_settings_.component);
  const auto has_matching_active_range =
    !kspace_presentation_.details.isEmpty() &&
    kspace_presentation_.details.value(QStringLiteral("display_component")).toString() == active_component &&
    (!arrshow_display_component_is_phase(kspace_display_settings_.component) ||
     kspace_presentation_.details.value(QStringLiteral("phase_representation")).toString() ==
       arrshow_phase_representation_identifier(kspace_display_settings_.phase_representation));
  if (has_matching_active_range) {
    arrshow_set_active_window_value(kspace_display_settings_, kspace_presentation_.source_minimum,
                                    kspace_presentation_.source_maximum, center, width);
  } else {
    window.center = center;
    window.width = width;
  }
}

void ViewerWindow::update_image_arrshow_settings_from_controls() {
  if (image_component_ == nullptr || image_phase_representation_ == nullptr || image_range_calculation_ == nullptr ||
      image_percentile_ == nullptr || image_window_persistence_ == nullptr || image_window_center_ == nullptr ||
      image_window_width_ == nullptr) {
    return;
  }
  image_display_settings_.component = static_cast<ArrShowDisplayComponent>(image_component_->currentData().toInt());
  arrshow_convert_phase_window(image_display_settings_, static_cast<ArrShowPhaseRepresentation>(
                                                          image_phase_representation_->currentData().toInt()));
  image_display_settings_.range_calculation =
    static_cast<ArrShowRangeCalculation>(image_range_calculation_->currentData().toInt());
  image_display_settings_.percentile = image_percentile_->value();
  auto& window = arrshow_active_window(image_display_settings_);
  window.persistence = static_cast<ArrShowWindowPersistence>(image_window_persistence_->currentData().toInt());
  const auto center = image_window_center_->value();
  const auto width = image_window_width_->value();
  const auto active_component = arrshow_display_component_identifier(image_display_settings_.component);
  const auto has_matching_active_range =
    !image_presentation_.details.isEmpty() &&
    image_presentation_.details.value(QStringLiteral("display_component")).toString() == active_component &&
    (!arrshow_display_component_is_phase(image_display_settings_.component) ||
     image_presentation_.details.value(QStringLiteral("phase_representation")).toString() ==
       arrshow_phase_representation_identifier(image_display_settings_.phase_representation));
  if (has_matching_active_range) {
    arrshow_set_active_window_value(image_display_settings_, image_presentation_.source_minimum,
                                    image_presentation_.source_maximum, center, width);
  } else {
    window.center = center;
    window.width = width;
  }
}

void ViewerWindow::sync_kspace_arrshow_controls() {
  if (kspace_component_ == nullptr || kspace_phase_representation_ == nullptr || kspace_range_calculation_ == nullptr ||
      kspace_percentile_ == nullptr || kspace_window_persistence_ == nullptr || kspace_window_center_ == nullptr ||
      kspace_window_width_ == nullptr) {
    return;
  }
  const QSignalBlocker component_blocker(kspace_component_);
  const QSignalBlocker phase_blocker(kspace_phase_representation_);
  const QSignalBlocker range_blocker(kspace_range_calculation_);
  const QSignalBlocker percentile_blocker(kspace_percentile_);
  const QSignalBlocker persistence_blocker(kspace_window_persistence_);
  const QSignalBlocker center_blocker(kspace_window_center_);
  const QSignalBlocker width_blocker(kspace_window_width_);
  const auto component_index = kspace_component_->findData(static_cast<int>(kspace_display_settings_.component));
  if (component_index >= 0) {
    kspace_component_->setCurrentIndex(component_index);
  }
  const auto phase_index =
    kspace_phase_representation_->findData(static_cast<int>(kspace_display_settings_.phase_representation));
  if (phase_index >= 0) {
    kspace_phase_representation_->setCurrentIndex(phase_index);
  }
  const auto range_index =
    kspace_range_calculation_->findData(static_cast<int>(kspace_display_settings_.range_calculation));
  if (range_index >= 0) {
    kspace_range_calculation_->setCurrentIndex(range_index);
  }
  kspace_range_calculation_->setItemText(
    1, arrshow_range_calculation_label(ArrShowRangeCalculation::percentile, kspace_display_settings_.percentile));
  kspace_percentile_->setValue(kspace_display_settings_.percentile);
  const auto& window = arrshow_active_window(kspace_display_settings_);
  const auto persistence_index = kspace_window_persistence_->findData(static_cast<int>(window.persistence));
  if (persistence_index >= 0) {
    kspace_window_persistence_->setCurrentIndex(persistence_index);
  }
  kspace_window_center_->setValue(window.center);
  kspace_window_width_->setValue(window.width > std::numeric_limits<double>::min() ? window.width : 1.0);
}

void ViewerWindow::sync_image_arrshow_controls() {
  if (image_component_ == nullptr || image_phase_representation_ == nullptr || image_range_calculation_ == nullptr ||
      image_percentile_ == nullptr || image_window_persistence_ == nullptr || image_window_center_ == nullptr ||
      image_window_width_ == nullptr) {
    return;
  }
  const QSignalBlocker component_blocker(image_component_);
  const QSignalBlocker phase_blocker(image_phase_representation_);
  const QSignalBlocker range_blocker(image_range_calculation_);
  const QSignalBlocker percentile_blocker(image_percentile_);
  const QSignalBlocker persistence_blocker(image_window_persistence_);
  const QSignalBlocker center_blocker(image_window_center_);
  const QSignalBlocker width_blocker(image_window_width_);
  const auto component_index = image_component_->findData(static_cast<int>(image_display_settings_.component));
  if (component_index >= 0) {
    image_component_->setCurrentIndex(component_index);
  }
  const auto phase_index =
    image_phase_representation_->findData(static_cast<int>(image_display_settings_.phase_representation));
  if (phase_index >= 0) {
    image_phase_representation_->setCurrentIndex(phase_index);
  }
  const auto range_index =
    image_range_calculation_->findData(static_cast<int>(image_display_settings_.range_calculation));
  if (range_index >= 0) {
    image_range_calculation_->setCurrentIndex(range_index);
  }
  image_range_calculation_->setItemText(
    1, arrshow_range_calculation_label(ArrShowRangeCalculation::percentile, image_display_settings_.percentile));
  image_percentile_->setValue(image_display_settings_.percentile);
  const auto& window = arrshow_active_window(image_display_settings_);
  const auto persistence_index = image_window_persistence_->findData(static_cast<int>(window.persistence));
  if (persistence_index >= 0) {
    image_window_persistence_->setCurrentIndex(persistence_index);
  }
  image_window_center_->setValue(window.center);
  image_window_width_->setValue(window.width > std::numeric_limits<double>::min() ? window.width : 1.0);
}

void ViewerWindow::load_kspace(const bool preserve_window_drag) {
  if (kspace_acquisition_type_ == nullptr || kspace_dimensions_ == nullptr || kspace_component_ == nullptr ||
      kspace_acquisition_type_->currentIndex() < 0 || !kspace_catalog_.has_value() ||
      selected_kspace_axes_.x == selected_kspace_axes_.y || kspace_component_->currentIndex() < 0) {
    const auto availability_error = kspace_availability_error_.isEmpty()
                                      ? tr("Choose an acquisition type, two distinct ':' axes, observed dimension "
                                           "values, and display component first.")
                                      : kspace_availability_error_;
    show_viewer_warning(this, tr("Cannot render Cartesian K-space"), availability_error, "render_kspace");
    return;
  }

  update_kspace_arrshow_settings_from_controls();
  const CartesianKspaceRequest request{
    .acquisition_kind = selected_kspace_acquisition_kind(kspace_acquisition_type_),
    .axes = selected_kspace_axes_,
    .coordinate = selected_kspace_coordinate_,
    .display_settings = kspace_display_settings_,
  };
  KspacePresentation next_presentation;
  QString error;
  if (!make_cartesian_kspace_presentation(inspection_session_, request, next_presentation, error)) {
    kspace_presentation_ = {};
    kspace_availability_error_ = error;
    refresh_kspace();
    show_viewer_warning(this, tr("Cannot render Cartesian K-space"), error, "render_kspace");
    return;
  }
  kspace_presentation_ = std::move(next_presentation);
  kspace_availability_error_.clear();
  kspace_display_settings_ = kspace_presentation_.display_settings;
  if (!preserve_window_drag) {
    kspace_window_drag_active_ = false;
  }
  refresh_kspace_dimension_controls();
  sync_kspace_arrshow_controls();
  update_control_state();
  refresh_kspace();
  if (!preserve_window_drag) {
    append_info(tr("Displayed one raw Cartesian complex K-space plane along the selected ':' dimensions for %1; no "
                   "FFT, RSS, or log transform was applied.")
                  .arg(kspace_acquisition_type_->currentText()));
  }
}

void ViewerWindow::load_image(const bool preserve_window_drag) {
  if (image_series_ == nullptr || image_ordinal_ == nullptr || image_dimensions_ == nullptr ||
      image_series_->currentText().isEmpty() || !image_source_dimensions_.has_value() ||
      selected_image_axes_.x == selected_image_axes_.y) {
    show_viewer_warning(this, tr("Cannot inspect image"),
                        tr("Choose an image series and valid fixed image coordinates first."), "inspect_image");
    return;
  }

  QString error;
  update_image_arrshow_settings_from_controls();
  const ImageRequest request{
    .series_id = image_series_->currentText(),
    .ordinal = static_cast<std::uint32_t>(image_ordinal_->value()),
    .axes = selected_image_axes_,
    .coordinate = selected_image_coordinate_,
    .display_settings = image_display_settings_,
  };
  if (!make_image_presentation(inspection_session_, request, image_presentation_, error)) {
    show_viewer_warning(this, tr("Cannot inspect image"), error, "inspect_image");
    return;
  }
  image_source_dimensions_ = image_presentation_.source_dimensions;
  image_display_settings_ = image_presentation_.display_settings;
  if (!preserve_window_drag) {
    image_window_drag_active_ = false;
  }
  refresh_image_dimension_controls();
  sync_image_arrshow_controls();
  update_control_state();
  refresh_image();
  if (!preserve_window_drag) {
    append_info(tr("Created a bounded image display derivative from one standard ISMRMRD image plane."));
  }
}

void ViewerWindow::toggle_image_cine() {
  if (image_cine_timer_ == nullptr || image_cine_button_ == nullptr) {
    return;
  }
  if (image_cine_timer_->isActive()) {
    stop_image_cine();
    return;
  }
  if (!inspection_session_.is_open() || image_series_->currentText().isEmpty() || image_ordinal_->maximum() <= 0) {
    return;
  }
  load_image();
  if (!image_presentation_.details.isEmpty()) {
    image_cine_timer_->start();
    image_cine_button_->setText(tr("Pause cine"));
  }
}

void ViewerWindow::stop_image_cine() {
  if (image_cine_timer_ != nullptr) {
    image_cine_timer_->stop();
  }
  if (image_cine_button_ != nullptr) {
    image_cine_button_->setText(tr("Play cine"));
  }
}

void ViewerWindow::export_current(const VisualizationExportFormat format) {
  VisualizationDerivative derivative;
  QString error;
  if (!current_derivative(derivative, error)) {
    show_viewer_warning(this, tr("Cannot export display derivative"), error, "export_display_derivative");
    return;
  }

  const auto extension = visualization_export_extension(format);
  const auto base_name = derivative.view_name.isEmpty() ? QStringLiteral("kspacejet-viewer") : derivative.view_name;
  const auto destination = QFileDialog::getSaveFileName(this, tr("Export visualization derivative"),
                                                        base_name + extension, export_filter(format));
  if (destination.isEmpty()) {
    return;
  }
  if (!export_visualization_derivative(derivative, destination, format, error)) {
    show_viewer_error(this, tr("Cannot export display derivative"), error, "export_display_derivative");
    return;
  }
  append_info(tr("Exported a visualization derivative. It is not an ISMRMRD MRI artifact: %1").arg(destination));
}

void ViewerWindow::refresh_metadata_content() {
  if (metadata_xml_ == nullptr || metadata_xml_outline_ == nullptr || metadata_xml_summary_ == nullptr) {
    return;
  }
  if (!metadata_view_open_ || !inspection_session_.is_open()) {
    metadata_xml_->clear();
    metadata_xml_outline_->clear();
    metadata_xml_summary_->clear();
    return;
  }

  metadata_xml_summary_->setText(metadata_xml_summary(inspection_session_));
  metadata_xml_->setPlainText(format_xml_for_display(metadata_presentation_.xml_preview));
  populate_xml_outline(metadata_xml_outline_, metadata_presentation_.xml_preview);
}

void ViewerWindow::refresh_metadata() {
  metadata_presentation_ = make_metadata_presentation(inspection_session_);
  refresh_metadata_content();
  metadata_stack_->setCurrentIndex(metadata_view_open_ && inspection_session_.is_open() ? 1 : 0);

  const QSignalBlocker image_series_blocker(image_series_);
  image_series_->clear();
  if (!inspection_session_.is_open()) {
    refresh_kspace_controls();
    update_image_controls();
    update_source_context();
    rebuild_dataset_navigation();
    update_export_availability();
    return;
  }

  const auto& metadata = inspection_session_.metadata();
  for (const auto& series : metadata.image_series) {
    image_series_->addItem(QString::fromUtf8(series.series_id.data(), static_cast<qsizetype>(series.series_id.size())));
  }
  refresh_kspace_controls();
  update_image_controls();
  update_source_context();
  rebuild_dataset_navigation();
  update_export_availability();
}

void ViewerWindow::refresh_kspace_controls() {
  if (kspace_acquisition_type_ == nullptr || kspace_dimensions_ == nullptr) {
    return;
  }

  const auto previously_selected_kind = selected_kspace_acquisition_kind(kspace_acquisition_type_);
  const auto previously_selected_selection = selected_kspace_dimension_selection_;
  const auto previously_selected_coordinate = selected_kspace_coordinate_;
  const QSignalBlocker acquisition_type_blocker(kspace_acquisition_type_);
  kspace_acquisition_type_->clear();
  kspace_catalog_.reset();
  selected_kspace_axes_ = {};
  selected_kspace_coordinate_ = {};
  kspace_dimensions_->set_dimensions({});
  kspace_availability_error_.clear();
  if (!inspection_session_.is_open() || inspection_session_.metadata().acquisition_count == 0U) {
    update_control_state();
    return;
  }

  QList<CartesianKspaceAcquisitionKindOption> acquisition_kind_options;
  QString error;
  if (!cartesian_kspace_acquisition_kind_options(inspection_session_, acquisition_kind_options, error)) {
    kspace_availability_error_ = error;
    update_control_state();
    return;
  }
  for (const auto& option : acquisition_kind_options) {
    kspace_acquisition_type_->addItem(
      tr("%1 (%2)").arg(option.label).arg(static_cast<qulonglong>(option.matching_acquisition_count)),
      static_cast<int>(option.kind));
  }
  const auto restored_type_index = kspace_acquisition_type_->findData(static_cast<int>(previously_selected_kind));
  const auto imaging_type_index =
    kspace_acquisition_type_->findData(static_cast<int>(CartesianKspaceAcquisitionKind::imaging));
  const auto default_type_index = imaging_type_index >= 0 ? imaging_type_index : 0;
  kspace_acquisition_type_->setCurrentIndex(restored_type_index >= 0 ? restored_type_index : default_type_index);

  CartesianKspaceCatalog catalog;
  if (!cartesian_kspace_catalog(inspection_session_, selected_kspace_acquisition_kind(kspace_acquisition_type_),
                                catalog, error)) {
    kspace_availability_error_ = error;
    update_control_state();
    return;
  }
  selected_kspace_dimension_selection_ = previously_selected_selection;
  normalize_kspace_dimension_selection(catalog, selected_kspace_dimension_selection_);
  const auto axes = kspace_axes_in_column_order(
    kspace_dimension_specs(catalog, previously_selected_coordinate, selected_kspace_dimension_selection_, {}));
  if (!axes.has_value()) {
    kspace_availability_error_ = tr("choose two observed K-space dimensions for the ':' display plane");
    update_control_state();
    return;
  }
  CartesianKspaceCoordinate resolved_coordinate;
  if (!resolve_cartesian_kspace_coordinate(catalog, axes.value(), previously_selected_coordinate, std::nullopt,
                                           resolved_coordinate, error) &&
      !resolve_cartesian_kspace_coordinate(catalog, axes.value(), catalog.initial_coordinate, std::nullopt,
                                           resolved_coordinate, error)) {
    kspace_availability_error_ = error;
    update_control_state();
    return;
  }
  kspace_catalog_ = std::move(catalog);
  selected_kspace_axes_ = axes.value();
  selected_kspace_coordinate_ = resolved_coordinate;
  refresh_kspace_dimension_controls();
  update_control_state();
}

void ViewerWindow::refresh_kspace_dimension_controls() {
  if (kspace_dimensions_ == nullptr) {
    return;
  }
  if (!kspace_catalog_.has_value()) {
    kspace_dimensions_->set_dimensions({});
    return;
  }
  const auto active_dimension = kspace_dimensions_->active_dimension_identifier();
  kspace_dimensions_->set_dimensions(kspace_dimension_specs(kspace_catalog_.value(), selected_kspace_coordinate_,
                                                            selected_kspace_dimension_selection_, kspace_presentation_),
                                     active_dimension);
}

void ViewerWindow::select_kspace_dimension_value(const QString& dimension_identifier, const int value) {
  if (kspace_selector_update_active_) {
    return;
  }
  const auto dimension = kspace_dimension_from_identifier(dimension_identifier);
  if (!kspace_catalog_.has_value() || !dimension.has_value() || dimension.value() == selected_kspace_axes_.x ||
      dimension.value() == selected_kspace_axes_.y) {
    return;
  }
  auto requested_coordinate = selected_kspace_coordinate_;
  set_cartesian_kspace_coordinate_value(requested_coordinate, dimension.value(), value);
  CartesianKspaceCoordinate resolved_coordinate;
  QString error;
  if (!resolve_cartesian_kspace_coordinate(kspace_catalog_.value(), selected_kspace_axes_, requested_coordinate,
                                           dimension, resolved_coordinate, error)) {
    kspace_availability_error_ = error;
    refresh_kspace();
    return;
  }
  if (resolved_coordinate == selected_kspace_coordinate_) {
    return;
  }

  const QScopedValueRollback<bool> selector_update(kspace_selector_update_active_, true);
  arrshow_prepare_active_window_for_new_plane(kspace_display_settings_);
  kspace_window_drag_active_ = false;
  kspace_availability_error_.clear();
  selected_kspace_coordinate_ = resolved_coordinate;
  refresh_kspace_dimension_controls();
  load_kspace();
}

void ViewerWindow::select_kspace_dimension_selection_tag(const QString& dimension_identifier,
                                                         const ArrShowDimensionSelectionTag selection_tag) {
  if (kspace_selector_update_active_) {
    return;
  }
  const auto dimension = kspace_dimension_from_identifier(dimension_identifier);
  if (!kspace_catalog_.has_value() || !dimension.has_value() || selection_tag == ArrShowDimensionSelectionTag::none) {
    return;
  }
  auto selection = selected_kspace_dimension_selection_;
  arrshow_update_dimension_selection(selection, dimension_identifier, selection_tag);
  normalize_kspace_dimension_selection(kspace_catalog_.value(), selection);
  const auto axes = kspace_axes_in_column_order(
    kspace_dimension_specs(kspace_catalog_.value(), selected_kspace_coordinate_, selection, kspace_presentation_));
  if (!axes.has_value()) {
    kspace_availability_error_ = tr("choose two observed K-space dimensions for the ':' display plane");
    refresh_kspace();
    return;
  }
  CartesianKspaceCoordinate resolved_coordinate;
  QString error;
  if (!resolve_cartesian_kspace_coordinate(kspace_catalog_.value(), axes.value(), selected_kspace_coordinate_,
                                           std::nullopt, resolved_coordinate, error)) {
    kspace_availability_error_ = error;
    refresh_kspace();
    return;
  }
  const QScopedValueRollback<bool> selector_update(kspace_selector_update_active_, true);
  arrshow_prepare_active_window_for_new_plane(kspace_display_settings_);
  kspace_window_drag_active_ = false;
  kspace_availability_error_.clear();
  selected_kspace_dimension_selection_ = std::move(selection);
  selected_kspace_axes_ = axes.value();
  selected_kspace_coordinate_ = resolved_coordinate;
  refresh_kspace_dimension_controls();
  load_kspace();
}

void ViewerWindow::refresh_kspace() {
  if (kspace_image_ != nullptr) {
    const auto empty_canvas_text = !kspace_availability_error_.isEmpty()
                                     ? kspace_availability_error_
                                     : tr("Open K-space to display the selected Cartesian plane.");
    kspace_image_->set_display_image(kspace_presentation_.image, empty_canvas_text, kspace_window_drag_active_);
    kspace_image_->set_zoom_percent(kspace_zoom_percent_ == nullptr ? 100 : kspace_zoom_percent_->value());
    const auto representation = kspace_presentation_.details.value(QStringLiteral("representation")).toString();
    const auto component = kspace_presentation_.details.value(QStringLiteral("display_component")).toString();
    kspace_image_->set_overlay_text(
      representation.isEmpty()
        ? tr("Raw Cartesian K-space · bounded display derivative · view %1%")
            .arg(kspace_zoom_percent_ == nullptr ? 100 : kspace_zoom_percent_->value())
        : tr("Raw Cartesian K-space · %1 · view %2%")
            .arg(component, QString::number(kspace_zoom_percent_ == nullptr ? 100 : kspace_zoom_percent_->value())));
    const auto has_active_dimension =
      kspace_dimensions_ != nullptr && !kspace_dimensions_->active_dimension_identifier().isEmpty();
    kspace_image_->set_interaction_help(
      has_active_dimension
        ? tr("Wheel or +/- active dimension · Left/Right dimension · Ctrl+wheel zoom · drag pan · middle drag C/W · "
             "double-click reset C/W")
        : tr("Ctrl+wheel zoom · drag pan · middle drag C/W · double-click reset C/W"));
  }
  if (kspace_presentation_.details.isEmpty() && kspace_pixel_probe_ != nullptr) {
    kspace_pixel_probe_->setText(tr("Pointer: open a K-space plane to probe it."));
  }
  update_export_availability();
}

void ViewerWindow::refresh_image_histogram() {
  if (image_histogram_ == nullptr) {
    return;
  }

  QStringList columns{tr("Display intensity"), tr("Pixels")};
  QList<QStringList> rows;
  if (image_presentation_.image.isNull()) {
    rows.append({tr("Status"), tr("No bounded display derivative is available.")});
    set_table_contents(image_histogram_, columns, rows);
    return;
  }

  constexpr std::size_t kHistogramBins = 32U;
  constexpr int kIntensityPerBin = 256 / static_cast<int>(kHistogramBins);
  std::array<std::uint32_t, kHistogramBins> counts{};
  const auto grayscale = image_presentation_.image.convertToFormat(QImage::Format_Grayscale8);
  for (int y = 0; y < grayscale.height(); ++y) {
    const auto* scanline = grayscale.constScanLine(y);
    for (int x = 0; x < grayscale.width(); ++x) {
      const auto bin =
        std::min<std::size_t>(static_cast<std::size_t>(scanline[x]) / kIntensityPerBin, kHistogramBins - 1U);
      ++counts[bin];
    }
  }
  for (std::size_t bin = 0U; bin < counts.size(); ++bin) {
    const auto lower = static_cast<int>(bin) * kIntensityPerBin;
    const auto upper = lower + kIntensityPerBin - 1;
    rows.append({tr("%1-%2").arg(lower).arg(upper), QString::number(counts[bin])});
  }
  set_table_contents(image_histogram_, columns, rows);
}

void ViewerWindow::update_image_pixel_probe(const QPoint& display_pixel) {
  if (image_pixel_probe_ == nullptr || image_presentation_.image.isNull()) {
    return;
  }
  if (display_pixel.x() < 0 || display_pixel.y() < 0 || display_pixel.x() >= image_presentation_.image.width() ||
      display_pixel.y() >= image_presentation_.image.height()) {
    image_pixel_probe_->setText(tr("Pointer: outside the current display derivative."));
    return;
  }
  const auto display_index =
    static_cast<qsizetype>(display_pixel.y()) * image_presentation_.image.width() + display_pixel.x();
  if (display_index >= 0 && display_index < image_presentation_.csv_rows.size()) {
    const auto& row = image_presentation_.csv_rows.at(display_index);
    const auto value_at = [this, &row](const QString& column) {
      const auto index = image_presentation_.csv_columns.indexOf(column);
      return index >= 0 && index < row.size() ? row.at(index) : QStringLiteral("n/a");
    };
    image_pixel_probe_->setText(tr("Raw image display cell: x=%1, y=%2, Re=%3, Im=%4, |z|=%5, phase=%6°")
                                  .arg(display_pixel.x())
                                  .arg(display_pixel.y())
                                  .arg(value_at(QStringLiteral("real")))
                                  .arg(value_at(QStringLiteral("imaginary")))
                                  .arg(value_at(QStringLiteral("magnitude")))
                                  .arg(value_at(QStringLiteral("phase_degrees"))));
    return;
  }
  const auto colour = image_presentation_.image.pixelColor(display_pixel);
  image_pixel_probe_->setText(tr("Display derivative pixel: x=%1, y=%2, RGB=(%3, %4, %5)")
                                .arg(display_pixel.x())
                                .arg(display_pixel.y())
                                .arg(colour.red())
                                .arg(colour.green())
                                .arg(colour.blue()));
}

void ViewerWindow::update_kspace_pixel_probe(const QPoint& display_pixel) {
  if (kspace_pixel_probe_ == nullptr || kspace_presentation_.image.isNull()) {
    return;
  }
  if (display_pixel.x() < 0 || display_pixel.y() < 0 || display_pixel.x() >= kspace_presentation_.image.width() ||
      display_pixel.y() >= kspace_presentation_.image.height()) {
    kspace_pixel_probe_->setText(tr("Pointer: outside the current display derivative."));
    return;
  }
  const auto display_index =
    static_cast<qsizetype>(display_pixel.y()) * kspace_presentation_.image.width() + display_pixel.x();
  if (display_index >= 0 && display_index < kspace_presentation_.csv_rows.size()) {
    const auto& row = kspace_presentation_.csv_rows.at(display_index);
    const auto value_at = [this, &row](const QString& column) {
      const auto index = kspace_presentation_.csv_columns.indexOf(column);
      return index >= 0 && index < row.size() ? row.at(index) : QStringLiteral("n/a");
    };
    const auto axis_x = kspace_presentation_.details.value(QStringLiteral("axis_x")).toString();
    const auto axis_y = kspace_presentation_.details.value(QStringLiteral("axis_y")).toString();
    kspace_pixel_probe_->setText(
      tr("Raw Cartesian cell: %1 x=%2, %3 y=%4, Re=%5, Im=%6, |z|=%7, phase=%8°, contributions=%9")
        .arg(axis_x.isEmpty() ? tr("axis") : axis_x)
        .arg(display_pixel.x())
        .arg(axis_y.isEmpty() ? tr("axis") : axis_y)
        .arg(display_pixel.y())
        .arg(value_at(QStringLiteral("real")))
        .arg(value_at(QStringLiteral("imaginary")))
        .arg(value_at(QStringLiteral("magnitude")))
        .arg(value_at(QStringLiteral("phase_degrees")))
        .arg(value_at(QStringLiteral("contribution_count"))));
    return;
  }
  const auto colour = kspace_presentation_.image.pixelColor(display_pixel);
  const auto axis_x = kspace_presentation_.details.value(QStringLiteral("axis_x")).toString();
  const auto axis_y = kspace_presentation_.details.value(QStringLiteral("axis_y")).toString();
  kspace_pixel_probe_->setText(tr("Display derivative pixel: %1 bin x=%2, %3 bin y=%4, RGB=(%5, %6, %7)")
                                 .arg(axis_x.isEmpty() ? tr("axis") : axis_x)
                                 .arg(display_pixel.x())
                                 .arg(axis_y.isEmpty() ? tr("axis") : axis_y)
                                 .arg(display_pixel.y())
                                 .arg(colour.red())
                                 .arg(colour.green())
                                 .arg(colour.blue()));
}

void ViewerWindow::clear_image_derivative_for_selection_change() {
  image_window_drag_active_ = false;
  arrshow_prepare_active_window_for_new_plane(image_display_settings_);
  image_presentation_ = {};
  refresh_image();
  if (image_pixel_probe_ != nullptr) {
    image_pixel_probe_->setText(tr("Selection changed — inspect the selected image plane."));
  }
}

void ViewerWindow::step_kspace_plane(const int step) {
  if (step != 0 && kspace_dimensions_ != nullptr) {
    static_cast<void>(kspace_dimensions_->step_active_dimension(step));
  }
}

void ViewerWindow::step_image_plane(const int step) {
  if (step != 0 && image_dimensions_ != nullptr) {
    static_cast<void>(image_dimensions_->step_active_dimension(step));
  }
}

void ViewerWindow::adjust_image_window_from_drag(const QPointF drag_delta, const bool finished) {
  if (image_presentation_.details.isEmpty() || image_window_center_ == nullptr || image_window_width_ == nullptr) {
    return;
  }
  if (!image_window_drag_active_) {
    if (drag_delta.isNull()) {
      return;
    }
    image_window_drag_active_ = true;
    image_window_drag_center_ = image_presentation_.applied_window_center;
    image_window_drag_width_ = std::max(image_presentation_.applied_window_width, std::numeric_limits<double>::min());
  }

  const auto data_width = std::max(image_presentation_.source_maximum - image_presentation_.source_minimum,
                                   std::numeric_limits<double>::min());
  const auto display_width = std::max(image_presentation_.image.width(), 1);
  const auto display_height = std::max(image_presentation_.image.height(), 1);
  // arrShow normalizes a middle drag by image dimensions, moves C vertically,
  // and adjusts W horizontally with its 4x width factor.
  const auto center = image_window_drag_center_ - drag_delta.y() * data_width / static_cast<double>(display_height);
  const auto width =
    std::clamp(image_window_drag_width_ + drag_delta.x() * 4.0 * data_width / static_cast<double>(display_width),
               std::numeric_limits<double>::min(), 1.0e100);
  arrshow_set_active_window_value(image_display_settings_, image_presentation_.source_minimum,
                                  image_presentation_.source_maximum, center, width);
  {
    const QSignalBlocker center_blocker(image_window_center_);
    const QSignalBlocker width_blocker(image_window_width_);
    image_window_center_->setValue(center);
    image_window_width_->setValue(width);
  }
  if (image_pixel_probe_ != nullptr) {
    image_pixel_probe_->setText(tr("C/W preview: center=%1, width=%2 — updating the bounded plane.")
                                  .arg(center, 0, 'g', 7)
                                  .arg(width, 0, 'g', 7));
  }
  if (finished) {
    image_window_drag_active_ = false;
    load_image();
  } else {
    load_image(true);
  }
}

void ViewerWindow::reset_image_window() {
  image_window_drag_active_ = false;
  if (!image_presentation_.details.isEmpty()) {
    const auto width = image_presentation_.source_maximum - image_presentation_.source_minimum;
    arrshow_set_active_window_value(image_display_settings_, image_presentation_.source_minimum,
                                    image_presentation_.source_maximum,
                                    image_presentation_.source_minimum + width * 0.5, width);
    sync_image_arrshow_controls();
    update_control_state();
    load_image();
  }
}

void ViewerWindow::apply_kspace_display_window() {
  if (kspace_presentation_.details.isEmpty() || kspace_window_center_ == nullptr || kspace_window_width_ == nullptr ||
      kspace_window_drag_active_) {
    return;
  }
  load_kspace();
}

void ViewerWindow::adjust_kspace_window_from_drag(const QPointF drag_delta, const bool finished) {
  if (kspace_presentation_.details.isEmpty() || kspace_window_center_ == nullptr || kspace_window_width_ == nullptr) {
    return;
  }
  if (!kspace_window_drag_active_) {
    if (drag_delta.isNull()) {
      return;
    }
    kspace_window_drag_active_ = true;
    kspace_window_drag_center_ = kspace_window_center_->value();
    kspace_window_drag_width_ = kspace_window_width_->value();
  }
  const auto data_width = std::max(kspace_presentation_.source_maximum - kspace_presentation_.source_minimum,
                                   std::numeric_limits<double>::min());
  const auto display_width = std::max(kspace_presentation_.image.width(), 1);
  const auto display_height = std::max(kspace_presentation_.image.height(), 1);
  const auto center = kspace_window_drag_center_ - drag_delta.y() * data_width / static_cast<double>(display_height);
  const auto width =
    std::max(std::numeric_limits<double>::min(),
             kspace_window_drag_width_ + drag_delta.x() * 4.0 * data_width / static_cast<double>(display_width));
  arrshow_set_active_window_value(kspace_display_settings_, kspace_presentation_.source_minimum,
                                  kspace_presentation_.source_maximum, center, width);
  {
    const QSignalBlocker center_blocker(kspace_window_center_);
    const QSignalBlocker width_blocker(kspace_window_width_);
    kspace_window_center_->setValue(center);
    kspace_window_width_->setValue(width);
  }
  if (finished) {
    kspace_window_drag_active_ = false;
    load_kspace();
  } else {
    load_kspace(true);
  }
  if (kspace_pixel_probe_ != nullptr && !finished) {
    kspace_pixel_probe_->setText(
      tr("Raw-component C/W preview: center=%1, width=%2.").arg(center, 0, 'g', 10).arg(width, 0, 'g', 10));
  }
}

void ViewerWindow::reset_kspace_window() {
  kspace_window_drag_active_ = false;
  if (!kspace_presentation_.details.isEmpty()) {
    const auto width = kspace_presentation_.source_maximum - kspace_presentation_.source_minimum;
    arrshow_set_active_window_value(kspace_display_settings_, kspace_presentation_.source_minimum,
                                    kspace_presentation_.source_maximum,
                                    kspace_presentation_.source_minimum + width * 0.5, width);
    sync_kspace_arrshow_controls();
    load_kspace();
  }
}

void ViewerWindow::refresh_image() {
  const auto zoom_percent = image_zoom_percent_ == nullptr ? 100 : image_zoom_percent_->value();
  if (image_image_ != nullptr) {
    image_image_->set_display_image(image_presentation_.image,
                                    tr("Open a dataset, select a series, and choose an image plane."),
                                    image_window_drag_active_);
    image_image_->set_zoom_percent(zoom_percent);
    const auto component = image_presentation_.details.value(QStringLiteral("display_component")).toString();
    const auto has_active_dimension =
      image_dimensions_ != nullptr && !image_dimensions_->active_dimension_identifier().isEmpty();
    image_image_->set_interaction_help(
      has_active_dimension
        ? tr("Wheel or +/- active dimension · Left/Right dimension · Ctrl+wheel zoom · drag pan · middle drag C/W · "
             "double-click reset C/W")
        : tr("Ctrl+wheel zoom · drag pan · middle drag C/W · double-click reset C/W"));
    image_image_->set_overlay_text(component.isEmpty()
                                     ? tr("Standard image · arrShow display derivative · view %1%").arg(zoom_percent)
                                     : tr("Standard image · arrShow %1 · view %2%").arg(component).arg(zoom_percent));
  }
  image_summary_->setPlainText(image_presentation_.summary);
  refresh_image_histogram();
  if (image_pixel_probe_ != nullptr && image_presentation_.image.isNull()) {
    image_pixel_probe_->setText(tr("Pointer: inspect an image to probe the current display derivative."));
  }
  update_object_inspector(dataset_navigation_ == nullptr ? nullptr : dataset_navigation_->currentItem());
  update_export_availability();
}

void ViewerWindow::refresh_pipeline() {
  pipeline_summary_->setPlainText(pipeline_presentation_.summary);
  pipeline_canonical_json_->setPlainText(pipeline_presentation_.canonical_json);
  refresh_pipeline_graph();
  pipeline_stack_->setCurrentIndex(pipeline_presentation_.details.isEmpty() ? 0 : 1);
  update_source_context();
  update_export_availability();
}

void ViewerWindow::refresh_pipeline_graph() {
  if (pipeline_graph_scene_ == nullptr) {
    return;
  }
  populate_pipeline_graph(*pipeline_graph_scene_, pipeline_presentation_);
  if (pipeline_graph_view_ != nullptr && !pipeline_graph_scene_->sceneRect().isEmpty()) {
    pipeline_graph_view_->fitInView(pipeline_graph_scene_->sceneRect(), Qt::KeepAspectRatio);
  }
}

void ViewerWindow::update_image_controls() {
  if (image_ordinal_ == nullptr || image_component_ == nullptr || image_dimensions_ == nullptr) {
    return;
  }
  const QSignalBlocker ordinal_blocker(image_ordinal_);
  const QSignalBlocker component_blocker(image_component_);
  image_ordinal_->setRange(0, 0);
  image_source_dimensions_.reset();
  selected_image_axes_ = {};
  image_dimensions_->set_dimensions({});
  if (!inspection_session_.is_open() || image_series_->currentText().isEmpty()) {
    image_component_->clear();
    image_component_->addItem(arrshow_display_component_label(ArrShowDisplayComponent::magnitude),
                              static_cast<int>(ArrShowDisplayComponent::magnitude));
    image_component_->addItem(arrshow_display_component_label(ArrShowDisplayComponent::real),
                              static_cast<int>(ArrShowDisplayComponent::real));
    // The disabled empty-state menu has no complex entries, but it must not
    // overwrite arrShow's Complex default before the first complex series is
    // opened. A real source later explicitly selects Real below.
    image_component_->setCurrentIndex(image_component_->findData(static_cast<int>(ArrShowDisplayComponent::real)));
    sync_image_arrshow_controls();
    update_control_state();
    return;
  }

  const auto selected_series = image_series_->currentText().toUtf8();
  for (const auto& series : inspection_session_.metadata().image_series) {
    const auto current = QString::fromUtf8(series.series_id.data(), static_cast<qsizetype>(series.series_id.size()));
    if (current == QString::fromUtf8(selected_series.constData(), selected_series.size())) {
      image_ordinal_->setRange(0, bounded_spin_maximum(series.image_count));
      break;
    }
  }

  const ksj::ismrmrd::ImageLocator locator{
    .series_id = std::string(selected_series.constData(), static_cast<std::size_t>(selected_series.size())),
    .ordinal = static_cast<std::uint32_t>(image_ordinal_->value())};
  ksj::ismrmrd::InspectionImageRecord record;
  std::string reader_error;
  if (inspection_session_.reader().read_image_record(locator, record, reader_error)) {
    const std::array<std::uint16_t, ksj::viewer::kImageDimensionCount> source_dimensions{
      record.header.matrix_size[0], record.header.matrix_size[1], record.header.matrix_size[2], record.header.channels};
    normalize_image_coordinate(source_dimensions, selected_image_coordinate_);
    image_source_dimensions_ = source_dimensions;
    refresh_image_dimension_controls();
    const auto previous_component = image_display_settings_.component;
    image_component_->clear();
    for (const auto component :
         {ArrShowDisplayComponent::magnitude, ArrShowDisplayComponent::real, ArrShowDisplayComponent::imaginary,
          ArrShowDisplayComponent::complex, ArrShowDisplayComponent::phase}) {
      if (image_arrshow_component_supported(component, record.header.data_type)) {
        image_component_->addItem(arrshow_display_component_label(component), static_cast<int>(component));
      }
    }
    const auto restored_index = image_component_->findData(static_cast<int>(previous_component));
    if (restored_index >= 0) {
      image_component_->setCurrentIndex(restored_index);
    } else {
      const auto real_index = image_component_->findData(static_cast<int>(ArrShowDisplayComponent::real));
      image_component_->setCurrentIndex(real_index >= 0 ? real_index : 0);
    }
    image_display_settings_.component = static_cast<ArrShowDisplayComponent>(image_component_->currentData().toInt());
  }
  sync_image_arrshow_controls();
  update_control_state();
}

void ViewerWindow::refresh_image_dimension_controls() {
  if (image_dimensions_ == nullptr) {
    return;
  }
  if (!image_source_dimensions_.has_value()) {
    image_dimensions_->set_dimensions({});
    return;
  }
  const auto active_dimension = image_dimensions_->active_dimension_identifier();
  const auto dimensions =
    image_dimension_specs(image_source_dimensions_.value(), selected_image_coordinate_, image_presentation_);
  const auto axes = image_axes_in_column_order(dimensions);
  if (axes.has_value()) {
    selected_image_axes_ = axes.value();
  } else {
    selected_image_axes_ = {};
  }
  image_dimensions_->set_dimensions(dimensions, active_dimension);
}

void ViewerWindow::select_image_dimension_value(const QString& dimension_identifier, const int value) {
  if (image_selector_update_active_ || !image_source_dimensions_.has_value() || value < 0) {
    return;
  }
  const auto dimension = image_dimension_from_identifier(dimension_identifier);
  if (!dimension.has_value() || dimension.value() == ImageDimension::x || dimension.value() == ImageDimension::y) {
    return;
  }
  const auto extent = image_source_dimensions_->at(ksj::viewer::image_dimension_index(dimension.value()));
  if (extent == 0U || value >= static_cast<int>(extent) ||
      ksj::viewer::image_coordinate_value(selected_image_coordinate_, dimension.value()) ==
        static_cast<std::uint16_t>(value)) {
    return;
  }

  const QScopedValueRollback<bool> selector_update(image_selector_update_active_, true);
  arrshow_prepare_active_window_for_new_plane(image_display_settings_);
  image_window_drag_active_ = false;
  ksj::viewer::set_image_coordinate_value(selected_image_coordinate_, dimension.value(),
                                          static_cast<std::uint16_t>(value));
  refresh_image_dimension_controls();
  load_image();
}

void ViewerWindow::update_source_context() {
  update_object_inspector(dataset_navigation_ == nullptr ? nullptr : dataset_navigation_->currentItem());
  update_workspace_tab_visibility(dataset_navigation_ == nullptr ? nullptr : dataset_navigation_->currentItem());
  update_selection_actions();
}

void ViewerWindow::update_control_state() {
  const auto dataset_is_open = inspection_session_.is_open();
  const auto has_acquisitions = dataset_is_open && inspection_session_.metadata().acquisition_count > 0U;
  const auto has_kspace_acquisition_type =
    has_acquisitions && kspace_acquisition_type_ != nullptr && kspace_acquisition_type_->count() > 0;
  const auto has_kspace_plane = has_acquisitions && kspace_catalog_.has_value();
  const auto has_cartesian_kspace = has_kspace_plane && selected_kspace_axes_.x != selected_kspace_axes_.y;
  kspace_acquisition_type_->setEnabled(has_kspace_acquisition_type);
  if (kspace_dimensions_ != nullptr) {
    kspace_dimensions_->setEnabled(has_kspace_plane);
  }
  const auto has_images = dataset_is_open && image_series_->count() > 0;
  kspace_component_->setEnabled(has_cartesian_kspace && kspace_component_->count() > 0);
  kspace_phase_representation_->setEnabled(has_cartesian_kspace);
  kspace_range_calculation_->setEnabled(has_cartesian_kspace);
  kspace_percentile_->setEnabled(has_cartesian_kspace &&
                                 kspace_display_settings_.range_calculation == ArrShowRangeCalculation::percentile);
  const auto has_kspace_presentation = !kspace_presentation_.details.isEmpty();
  kspace_window_persistence_->setEnabled(has_kspace_presentation);
  kspace_window_center_->setEnabled(has_kspace_presentation);
  kspace_window_width_->setEnabled(has_kspace_presentation);
  kspace_reset_window_button_->setEnabled(has_kspace_presentation);
  kspace_zoom_percent_->setEnabled(has_cartesian_kspace);
  kspace_reset_view_button_->setEnabled(has_cartesian_kspace);
  image_series_->setEnabled(has_images);
  image_component_->setEnabled(has_images && image_component_->count() > 0);
  const auto image_has_complex_component =
    image_component_->findData(static_cast<int>(ArrShowDisplayComponent::complex)) >= 0;
  image_phase_representation_->setEnabled(has_images && image_has_complex_component);
  image_range_calculation_->setEnabled(has_images);
  image_percentile_->setEnabled(has_images &&
                                image_display_settings_.range_calculation == ArrShowRangeCalculation::percentile);
  image_ordinal_->setEnabled(has_images);
  if (image_dimensions_ != nullptr) {
    image_dimensions_->setEnabled(has_images && image_source_dimensions_.has_value());
  }
  image_window_persistence_->setEnabled(has_images);
  image_zoom_percent_->setEnabled(has_images);
  image_reset_window_button_->setEnabled(has_images);
  image_fit_button_->setEnabled(has_images);
  image_window_center_->setEnabled(has_images);
  image_window_width_->setEnabled(has_images);
  image_cine_button_->setEnabled(has_images && image_ordinal_->maximum() > 0);
  if (!has_images || image_ordinal_->maximum() <= 0) {
    stop_image_cine();
  }
  load_image_button_->setEnabled(has_images);
  update_workspace_tab_visibility(dataset_navigation_ == nullptr ? nullptr : dataset_navigation_->currentItem());
}

void ViewerWindow::update_export_availability() {
  bool can_export = false;
  const auto* current_page = object_inspector_ == nullptr ? nullptr : object_inspector_->currentWidget();
  if (current_page == metadata_page_) {
    can_export = metadata_view_open_ && inspection_session_.is_open();
  } else if (current_page == kspace_page_) {
    can_export = !kspace_presentation_.details.isEmpty();
  } else if (current_page == image_page_) {
    can_export = !image_presentation_.details.isEmpty();
  } else if (current_page == pipeline_page_) {
    can_export = !pipeline_presentation_.details.isEmpty();
  }
  export_button_->setEnabled(can_export);
  for (auto* action : {export_png_action_, export_svg_action_, export_csv_action_, export_json_action_}) {
    action->setEnabled(can_export);
  }
}

void ViewerWindow::append_info(const QString& message) {
  if (message.isEmpty()) {
    return;
  }
  if (info_panel_ != nullptr) {
    info_panel_->appendPlainText(message);
  }
  statusBar()->showMessage(message, 8'000);
}

bool ViewerWindow::current_derivative(VisualizationDerivative& derivative, QString& error) const {
  derivative = {};
  error.clear();

  if (object_inspector_ == nullptr) {
    error = tr("Open a bounded inspection view before exporting its visualization derivative.");
    return false;
  }

  const auto* active_page = object_inspector_->currentWidget();
  if (active_page == metadata_page_) {
    if (!metadata_view_open_ || !inspection_session_.is_open()) {
      error = tr("Inspect an ISMRMRD XML header before exporting its visualization derivative.");
      return false;
    }
    derivative.view_name = QStringLiteral("metadata");
    derivative.source_description = mrd_source_description(inspection_session_);
    derivative.csv_columns = metadata_presentation_.csv_columns;
    derivative.csv_rows = metadata_presentation_.csv_rows;
    derivative.details = metadata_presentation_.details;
    return true;
  }
  if (active_page == kspace_page_) {
    if (kspace_presentation_.details.isEmpty()) {
      error = tr("Open a Cartesian K-space plane before exporting its display derivative.");
      return false;
    }
    derivative.view_name = QStringLiteral("k-space");
    derivative.source_description = mrd_source_description(inspection_session_);
    derivative.image = kspace_presentation_.image;
    derivative.csv_columns = kspace_presentation_.csv_columns;
    derivative.csv_rows = kspace_presentation_.csv_rows;
    derivative.details = kspace_presentation_.details;
    return true;
  }
  if (active_page == image_page_) {
    if (image_presentation_.details.isEmpty()) {
      error = tr("Inspect an image before exporting its display derivative.");
      return false;
    }
    derivative.view_name = QStringLiteral("image");
    derivative.source_description = mrd_source_description(inspection_session_);
    derivative.image = image_presentation_.image;
    derivative.csv_columns = image_presentation_.csv_columns;
    derivative.csv_rows = image_presentation_.csv_rows;
    derivative.details = image_presentation_.details;
    return true;
  }
  if (active_page == pipeline_page_) {
    if (pipeline_presentation_.details.isEmpty()) {
      error = tr("Open a PipelineDefinition before exporting its display derivative.");
      return false;
    }
    derivative.view_name = QStringLiteral("pipeline");
    derivative.source_description = pipeline_presentation_.details.value(QStringLiteral("source")).toString();
    derivative.image = pipeline_graph_image();
    derivative.csv_columns = pipeline_presentation_.csv_columns;
    derivative.csv_rows = pipeline_presentation_.csv_rows;
    derivative.details = pipeline_presentation_.details;
    return true;
  }

  error = tr("The current viewer tab cannot be exported.");
  return false;
}

QImage ViewerWindow::pipeline_graph_image() const {
  return pipeline_graph_scene_image(pipeline_graph_scene_);
}

} // namespace ksj::viewer
