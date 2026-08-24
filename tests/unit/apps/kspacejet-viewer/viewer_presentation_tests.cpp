#include "inspection_session.hpp"
#include "viewer_presentation.hpp"
#include "viewer_theme.hpp"
#include "viewer_window.hpp"
#include "visualization_derivative_export.hpp"

#include "kspacejet/recon/graph/canonical_json.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QWidget>

#include <hdf5.h>
#include <ismrmrd/dataset.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kDatasetGroup{"dataset"};
constexpr std::string_view kImageSeries{"viewer_series"};
constexpr std::string_view kXmlHeader{"<ismrmrdHeader xmlns=\"http://www.ismrm.org/ISMRMRD\"><experimentalConditions>"
                                      "<H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz>"
                                      "</experimentalConditions></ismrmrdHeader>"};
constexpr std::uint16_t kSourceImageWidth = 2'049U;
constexpr std::uint16_t kSourceImageHeight = 1'025U;

[[nodiscard]] std::filesystem::path native_path(const QString& value) {
#ifdef _WIN32
  return std::filesystem::path(value.toStdWString());
#else
  const auto utf8 = value.toUtf8();
  return std::filesystem::path(utf8.constData());
#endif
}

void append_synthetic_image(ISMRMRD::Dataset& dataset) {
  ISMRMRD::Image<float> image(kSourceImageWidth, kSourceImageHeight, 1U, 1U);
  std::fill(image.getDataPtr(), image.getDataPtr() + image.getNumberOfDataElements(), 0.0F);
  image(0U, 0U, 0U, 0U) = 1.0F;
  image(kSourceImageWidth - 1U, kSourceImageHeight - 1U, 0U, 0U) = 100.0F;
  auto& header = image.getHead();
  header.image_index = 5U;
  header.image_series_index = 7U;
  image.setAttributeString("");
  dataset.appendImage(std::string(kImageSeries), image);
}

void write_synthetic_dataset(const std::filesystem::path& path, const std::string_view group = kDatasetGroup,
                             const bool write_image = true) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(group).c_str(), true);
  dataset.writeHeader(std::string(kXmlHeader));

  ISMRMRD::Acquisition acquisition(3U, 2U, 2U);
  acquisition.scan_counter() = 17U;
  acquisition.idx().slice = 3U;
  for (std::uint16_t channel = 0U; channel < acquisition.active_channels(); ++channel) {
    for (std::uint16_t sample = 0U; sample < acquisition.number_of_samples(); ++sample) {
      const auto value = static_cast<float>(sample + channel * acquisition.number_of_samples());
      acquisition.data(sample, channel) = {value, 10.0F + value};
    }
  }
  for (std::uint16_t sample = 0U; sample < acquisition.number_of_samples(); ++sample) {
    acquisition.traj(0U, sample) = static_cast<float>(sample);
    acquisition.traj(1U, sample) = static_cast<float>(sample + 10U);
  }
  dataset.appendAcquisition(acquisition);

  if (write_image) {
    append_synthetic_image(dataset);
  }
}

void write_synthetic_image_artifact(const std::filesystem::path& path, const std::string_view group = kDatasetGroup) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(group).c_str(), true);
  dataset.writeHeader(std::string(kXmlHeader));
  append_synthetic_image(dataset);
}

void create_empty_root_group(const std::filesystem::path& path, const std::string_view group) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto empty_group = H5Gcreate2(file, std::string(group).c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(empty_group, 0);
  EXPECT_GE(H5Gclose(empty_group), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

[[nodiscard]] QString valid_pipeline_definition() {
  return QStringLiteral(R"json(
{
  "kind": "PipelineDefinition",
  "pipeline": {
    "id": "org.example.viewer-test",
    "display_name": "Viewer parse-only test"
  },
  "input_profile": {
    "kind": "ismrmrd-hdf5",
    "dataset_group": "dataset"
  },
  "allowed_profiles": ["offline-reference"],
  "parameters": {},
  "provider_requirements": [
    {
      "alias": "example",
      "provider_id": "org.example.viewer-provider"
    }
  ],
  "nodes": [
    {
      "id": "inspect",
      "operator": {
        "provider": "example",
        "id": "inspect"
      },
      "config": {}
    }
  ],
  "edges": [],
  "bindings": {
    "ingress": [
      {
        "id": "kspace",
        "type": "ksj.kspace-frame",
        "to": {
          "node": "inspect",
          "port": "kspace"
        }
      }
    ],
    "egress": [
      {
        "id": "images",
        "type": "ksj.image-frame",
        "from": {
          "node": "inspect",
          "port": "image"
        }
      }
    ],
    "calibration": [],
    "merge": []
  },
  "annotations": {}
}
)json");
}

void write_file(const QString& path, const QByteArray& contents) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << file.errorString().toStdString();
  ASSERT_EQ(file.write(contents), static_cast<qint64>(contents.size())) << file.errorString().toStdString();
}

[[nodiscard]] QByteArray read_file(const QString& path) {
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << file.errorString().toStdString();
  return file.readAll();
}

[[nodiscard]] QApplication& viewer_application() {
  if (auto* application = qobject_cast<QApplication*>(QCoreApplication::instance()); application != nullptr) {
    return *application;
  }

  static int argc = 1;
  static char application_name[] = "ksj-viewer-presentation-tests";
  static char* argv[] = {application_name, nullptr};
  static QApplication application(argc, argv);
  return application;
}

template <typename Widget>
[[nodiscard]] Widget* find_named_widget(QWidget& parent, std::initializer_list<const char*> object_names) {
  for (const auto* object_name : object_names) {
    if (auto* widget = parent.findChild<Widget*>(QString::fromLatin1(object_name)); widget != nullptr) {
      return widget;
    }
  }
  return nullptr;
}

} // namespace

TEST(KSpaceJetViewerWindow, PresentsHdfViewInspiredReadOnlyWorkbenchAtDesktopSize) {
  auto& application = viewer_application();
  ksj::viewer::apply_viewer_theme(application);

  ksj::viewer::ViewerWindow window;
  window.resize(1'280, 800);
  window.show();
  application.processEvents();

  EXPECT_EQ(window.size(), QSize(1'280, 800));
  EXPECT_GT(application.palette().color(QPalette::Active, QPalette::Window).lightness(), 128);

  EXPECT_NE(find_named_widget<QWidget>(window, {"viewerRoot"}), nullptr);
  EXPECT_NE(find_named_widget<QWidget>(window, {"viewerMenuBar", "viewerToolbar"}), nullptr);
  EXPECT_NE(find_named_widget<QWidget>(window, {"viewerFileBar"}), nullptr);
  EXPECT_NE(find_named_widget<QWidget>(window, {"offlineReadonlyBadge"}), nullptr);

  const auto* source_file_bar = find_named_widget<QComboBox>(window, {"sourceFileBar"});
  ASSERT_NE(source_file_bar, nullptr);
  EXPECT_TRUE(source_file_bar->isEditable());
  EXPECT_NE(find_named_widget<QAbstractButton>(window, {"recentSourcesButton"}), nullptr);
  EXPECT_NE(find_named_widget<QAbstractButton>(window, {"clearFileBarButton"}), nullptr);

  const auto* object_inspector = find_named_widget<QTabWidget>(window, {"objectInspector"});
  ASSERT_NE(object_inspector, nullptr);
  EXPECT_EQ(object_inspector->count(), 2);
  EXPECT_EQ(object_inspector->tabText(0), QStringLiteral("Object Attribute Info"));
  EXPECT_EQ(object_inspector->tabText(1), QStringLiteral("General Object Info"));
  EXPECT_EQ(object_inspector->currentIndex(), 1);
  EXPECT_NE(find_named_widget<QScrollArea>(window, {"generalObjectInfo"}), nullptr);
  EXPECT_NE(find_named_widget<QLineEdit>(window, {"objectNameField"}), nullptr);
  EXPECT_NE(find_named_widget<QLineEdit>(window, {"objectPathField"}), nullptr);
  EXPECT_NE(find_named_widget<QLineEdit>(window, {"objectTypeField"}), nullptr);
  EXPECT_NE(find_named_widget<QLineEdit>(window, {"objectAccessField"}), nullptr);
  EXPECT_NE(find_named_widget<QTableWidget>(window, {"objectSemanticInfoTable"}), nullptr);
  EXPECT_NE(find_named_widget<QTableWidget>(window, {"objectMembersInfoTable"}), nullptr);
  EXPECT_NE(find_named_widget<QTableWidget>(window, {"objectAttributesInfo"}), nullptr);

  const auto* data_views = find_named_widget<QTabWidget>(window, {"viewerDataViews"});
  ASSERT_NE(data_views, nullptr);
  EXPECT_EQ(data_views->count(), 4);
  EXPECT_EQ(data_views->tabText(0), QStringLiteral("XML"));
  EXPECT_FALSE(data_views->isVisible());
  EXPECT_EQ(find_named_widget<QWidget>(window, {"metadataOverviewCard"}), nullptr);
  EXPECT_EQ(find_named_widget<QWidget>(window, {"metadataSeriesCard"}), nullptr);

  EXPECT_NE(window.findChild<QAction*>(QStringLiteral("openMrdAction")), nullptr);
  EXPECT_NE(window.findChild<QAction*>(QStringLiteral("openPipelineAction")), nullptr);
  const auto* close_source = window.findChild<QAction*>(QStringLiteral("closeMrdAction"));
  const auto* inspect_object = window.findChild<QAction*>(QStringLiteral("inspectObjectAction"));
  const auto* open_as = window.findChild<QAction*>(QStringLiteral("openAsAction"));
  const auto* copy_path = window.findChild<QAction*>(QStringLiteral("copyObjectPathAction"));
  ASSERT_NE(close_source, nullptr);
  ASSERT_NE(inspect_object, nullptr);
  ASSERT_NE(open_as, nullptr);
  ASSERT_NE(copy_path, nullptr);
  EXPECT_FALSE(close_source->isEnabled());
  EXPECT_FALSE(inspect_object->isEnabled());
  EXPECT_FALSE(open_as->isEnabled());
  EXPECT_FALSE(copy_path->isEnabled());

  const auto* dataset_navigation = find_named_widget<QTreeWidget>(window, {"semanticObjectTree"});
  ASSERT_NE(dataset_navigation, nullptr);
  ASSERT_EQ(dataset_navigation->topLevelItemCount(), 2);
  EXPECT_TRUE(dataset_navigation->topLevelItem(0)->text(0).contains(QStringLiteral("No ISMRMRD source open")));
  EXPECT_FALSE(dataset_navigation->topLevelItem(0)->flags().testFlag(Qt::ItemIsEnabled));
  EXPECT_TRUE(dataset_navigation->topLevelItem(1)->text(0).contains(QStringLiteral("PipelineDefinition")));

  const auto* export_button = find_named_widget<QAbstractButton>(window, {"exportDisplayButton"});
  ASSERT_NE(export_button, nullptr);
  EXPECT_FALSE(export_button->isEnabled());
  const auto* kspace_inspect = find_named_widget<QAbstractButton>(window, {"kspaceInspectButton"});
  ASSERT_NE(kspace_inspect, nullptr);
  EXPECT_FALSE(kspace_inspect->isEnabled());
  const auto* image_inspect = find_named_widget<QAbstractButton>(window, {"imageInspectButton"});
  ASSERT_NE(image_inspect, nullptr);
  EXPECT_FALSE(image_inspect->isEnabled());
  const auto* image_cine = find_named_widget<QAbstractButton>(window, {"imageCineButton"});
  ASSERT_NE(image_cine, nullptr);
  EXPECT_FALSE(image_cine->isEnabled());
  const auto* auto_window = find_named_widget<QCheckBox>(window, {"imageAutoWindow"});
  ASSERT_NE(auto_window, nullptr);
  EXPECT_TRUE(auto_window->isChecked());
  EXPECT_FALSE(auto_window->isEnabled());
  const auto* window_center = find_named_widget<QDoubleSpinBox>(window, {"imageWindowCenter"});
  const auto* window_width = find_named_widget<QDoubleSpinBox>(window, {"imageWindowWidth"});
  const auto* image_zoom = find_named_widget<QSpinBox>(window, {"imageZoomPercent"});
  ASSERT_NE(window_center, nullptr);
  ASSERT_NE(window_width, nullptr);
  ASSERT_NE(image_zoom, nullptr);
  EXPECT_FALSE(window_center->isEnabled());
  EXPECT_FALSE(window_width->isEnabled());
  EXPECT_FALSE(image_zoom->isEnabled());

  EXPECT_NE(find_named_widget<QWidget>(window, {"metadataEmptyState"}), nullptr);
  const auto* info_panel = find_named_widget<QPlainTextEdit>(window, {"viewerInfoPanel"});
  ASSERT_NE(info_panel, nullptr);
  EXPECT_TRUE(info_panel->toPlainText().contains(QStringLiteral("read-only"), Qt::CaseInsensitive));

  window.close();
  application.processEvents();
}

TEST(KSpaceJetViewerWindow, SelectsSemanticObjectsBeforeExplicitInspectOpensTheirTypedView) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-workbench.mrd"));
  write_synthetic_dataset(native_path(dataset_path), "dataset_1", false);
  write_synthetic_dataset(native_path(dataset_path), "dataset_2", true);

  auto& application = viewer_application();
  ksj::viewer::apply_viewer_theme(application);
  ksj::viewer::ViewerWindow window;
  window.resize(1'280, 800);
  window.show();
  application.processEvents();

  QString error;
  ASSERT_TRUE(window.open_mrd_source(dataset_path, error)) << error.toStdString();
  application.processEvents();

  const auto* source_file_bar = find_named_widget<QComboBox>(window, {"sourceFileBar"});
  auto* tree = find_named_widget<QTreeWidget>(window, {"semanticObjectTree"});
  const auto* data_views = find_named_widget<QTabWidget>(window, {"viewerDataViews"});
  auto* object_path = find_named_widget<QLineEdit>(window, {"objectPathField"});
  auto* attributes_status = find_named_widget<QLabel>(window, {"objectAttributesStatus"});
  auto* attributes = find_named_widget<QTableWidget>(window, {"objectAttributesInfo"});
  auto* inspect = window.findChild<QAction*>(QStringLiteral("inspectObjectAction"));
  auto* close_source = window.findChild<QAction*>(QStringLiteral("closeMrdAction"));
  ASSERT_NE(source_file_bar, nullptr);
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(data_views, nullptr);
  ASSERT_NE(object_path, nullptr);
  ASSERT_NE(attributes_status, nullptr);
  ASSERT_NE(attributes, nullptr);
  ASSERT_NE(inspect, nullptr);
  ASSERT_NE(close_source, nullptr);
  EXPECT_GE(source_file_bar->findText(dataset_path), 0);
  ASSERT_EQ(tree->topLevelItemCount(), 2);
  EXPECT_FALSE(data_views->isVisible());

  const auto find_second_container = [tree]() -> QTreeWidgetItem* {
    auto* source_item = tree->topLevelItem(0);
    if (source_item == nullptr) {
      return nullptr;
    }
    for (int index = 0; index < source_item->childCount(); ++index) {
      auto* candidate = source_item->child(index);
      if (candidate != nullptr && candidate->text(0).contains(QStringLiteral("/dataset_2"))) {
        return candidate;
      }
    }
    return nullptr;
  };
  const auto find_semantic_child = [](QTreeWidgetItem* parent, const QString& prefix) -> QTreeWidgetItem* {
    if (parent == nullptr) {
      return nullptr;
    }
    for (int index = 0; index < parent->childCount(); ++index) {
      auto* candidate = parent->child(index);
      if (candidate != nullptr && candidate->text(0).startsWith(prefix)) {
        return candidate;
      }
    }
    return nullptr;
  };

  auto* second_container = find_second_container();
  ASSERT_NE(second_container, nullptr);
  auto* header_item = find_semantic_child(second_container, QStringLiteral("Header / XML"));
  ASSERT_NE(header_item, nullptr);

  const auto initial_data_view = data_views->currentIndex();
  tree->setCurrentItem(header_item);
  application.processEvents();
  EXPECT_EQ(data_views->currentIndex(), initial_data_view);
  EXPECT_FALSE(data_views->isVisible());
  EXPECT_EQ(object_path->text(), QStringLiteral("/dataset_2/xml"));
  EXPECT_TRUE(inspect->isEnabled());

  inspect->trigger();
  application.processEvents();
  EXPECT_TRUE(data_views->isVisible());
  EXPECT_EQ(data_views->currentIndex(), 0);
  const auto* xml_preview = find_named_widget<QPlainTextEdit>(window, {"metadataXmlPreview"});
  ASSERT_NE(xml_preview, nullptr);
  EXPECT_TRUE(xml_preview->toPlainText().contains(QStringLiteral("ismrmrdHeader")));

  second_container = find_second_container();
  ASSERT_NE(second_container, nullptr);
  auto* images_item = find_semantic_child(second_container, QStringLiteral("Images"));
  ASSERT_NE(images_item, nullptr);

  tree->setCurrentItem(images_item);
  application.processEvents();
  EXPECT_EQ(data_views->currentIndex(), initial_data_view);
  EXPECT_TRUE(data_views->isVisible());
  EXPECT_TRUE(object_path->text().contains(QStringLiteral("/dataset_2")));
  EXPECT_TRUE(inspect->isEnabled());

  inspect->trigger();
  application.processEvents();
  EXPECT_TRUE(data_views->isVisible());
  EXPECT_EQ(data_views->currentIndex(), 2);
  EXPECT_EQ(attributes->columnCount(), 4);
  EXPECT_EQ(attributes->rowCount(), 0);
  EXPECT_TRUE(attributes_status->text().contains(QStringLiteral("no MetaAttributes"), Qt::CaseInsensitive));

  close_source->trigger();
  application.processEvents();
  ASSERT_EQ(tree->topLevelItemCount(), 2);
  EXPECT_TRUE(tree->topLevelItem(0)->text(0).contains(QStringLiteral("No ISMRMRD source open")));
  EXPECT_FALSE(close_source->isEnabled());
  EXPECT_FALSE(data_views->isVisible());

  window.close();
  application.processEvents();
}

TEST(KSpaceJetViewerPresentation, InspectsMetadataKspaceAndImagesAsBoundedDisplayDerivatives) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-source.mrd"));
  write_synthetic_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset"));
  const auto opened_source_path = session.source_path();
  EXPECT_FALSE(session.open_mrd(QString{}, error));
  EXPECT_EQ(error, QStringLiteral("an ISMRMRD file path is required"));
  EXPECT_TRUE(session.is_open());
  EXPECT_EQ(session.source_path(), opened_source_path);

  const auto metadata = ksj::viewer::make_metadata_presentation(session);
  EXPECT_TRUE(metadata.summary.contains(QStringLiteral("Acquisitions: 1")));
  EXPECT_TRUE(metadata.xml_preview.contains(QStringLiteral("ismrmrdHeader")));
  ASSERT_EQ(metadata.csv_rows.size(), 1);
  EXPECT_EQ(metadata.csv_rows.front().front(), QString::fromLatin1(kImageSeries.data(), kImageSeries.size()));
  EXPECT_EQ(metadata.details.value(QStringLiteral("view")).toString(), QStringLiteral("metadata"));
  EXPECT_EQ(metadata.details.value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));
  EXPECT_EQ(metadata.details.value(QStringLiteral("source")).toString(),
            QStringLiteral("%1 (container /dataset)").arg(opened_source_path));

  ksj::viewer::KspacePresentation kspace;
  ASSERT_TRUE(ksj::viewer::make_kspace_presentation(session, 0U, kspace, error)) << error.toStdString();
  EXPECT_FALSE(kspace.image.isNull());
  EXPECT_EQ(kspace.image.size(), QSize(3, 2));
  EXPECT_TRUE(kspace.summary.contains(QStringLiteral("not a reconstructed image")));
  EXPECT_EQ(kspace.details.value(QStringLiteral("representation")).toString(),
            QStringLiteral("acquisition magnitude projection; not reconstructed image"));
  EXPECT_EQ(kspace.details.value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));

  ksj::viewer::ImagePresentation image;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(session,
                                                   QString::fromLatin1(kImageSeries.data(), kImageSeries.size()), 0U,
                                                   0U, 0U, ksj::viewer::ImageDisplaySettings{}, image, error))
    << error.toStdString();
  EXPECT_EQ(image.dimensions, (std::array<std::uint16_t, 4U>{kSourceImageWidth, kSourceImageHeight, 1U, 1U}));
  EXPECT_FALSE(image.image.isNull());
  EXPECT_LT(image.image.width(), static_cast<int>(kSourceImageWidth));
  EXPECT_LE(image.image.width(), 2'048);
  EXPECT_LE(image.image.height(), 2'048);
  EXPECT_LE(image.image.sizeInBytes(), static_cast<qsizetype>(2U * 1024U * 1024U));
  EXPECT_LE(image.csv_rows.size(), 4'096);
  EXPECT_EQ(image.details.value(QStringLiteral("view")).toString(), QStringLiteral("image"));
  EXPECT_EQ(image.details.value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));

  ksj::viewer::ImagePresentation manual_window_image;
  const ksj::viewer::ImageDisplaySettings manual_window{
    .auto_window = false,
    .window_center = 0.5,
    .window_width = 1.0,
  };
  ASSERT_TRUE(ksj::viewer::make_image_presentation(session,
                                                   QString::fromLatin1(kImageSeries.data(), kImageSeries.size()), 0U,
                                                   0U, 0U, manual_window, manual_window_image, error))
    << error.toStdString();
  EXPECT_FALSE(manual_window_image.auto_window);
  EXPECT_EQ(manual_window_image.details.value(QStringLiteral("window_mode")).toString(), QStringLiteral("manual"));
  EXPECT_DOUBLE_EQ(manual_window_image.applied_window_center, 0.5);
  EXPECT_DOUBLE_EQ(manual_window_image.applied_window_width, 1.0);
  EXPECT_NE(manual_window_image.image.pixelColor(0, 0), image.image.pixelColor(0, 0));

  const ksj::viewer::ImageDisplaySettings invalid_window{
    .auto_window = false,
    .window_center = 0.0,
    .window_width = 0.0,
  };
  EXPECT_FALSE(ksj::viewer::make_image_presentation(session,
                                                    QString::fromLatin1(kImageSeries.data(), kImageSeries.size()), 0U,
                                                    0U, 0U, invalid_window, manual_window_image, error));
  EXPECT_EQ(error, QStringLiteral("image window width must be finite and greater than zero"));
}

TEST(KSpaceJetViewerPresentation, DiscoversAndSwitchesOnlyReadableStandardContainers) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-multigroup.mrd"));
  write_synthetic_dataset(native_path(dataset_path), "dataset_1", false);
  write_synthetic_dataset(native_path(dataset_path), "dataset_2", true);
  create_empty_root_group(native_path(dataset_path), "dataset");

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();
  const auto& containers = session.available_containers();
  const auto find_container = [&containers](const std::string_view path) {
    return std::find_if(containers.begin(), containers.end(), [path](const auto& descriptor) {
      return descriptor.path == std::string(path);
    });
  };
  const auto raw_container_1 = find_container("/dataset_1");
  ASSERT_NE(raw_container_1, containers.end());
  EXPECT_TRUE(raw_container_1->has_header);
  EXPECT_TRUE(raw_container_1->has_acquisitions);
  EXPECT_FALSE(raw_container_1->has_images);

  const auto image_container_2 = find_container("/dataset_2/viewer_series");
  ASSERT_NE(image_container_2, containers.end());
  EXPECT_FALSE(image_container_2->has_header);
  EXPECT_TRUE(image_container_2->has_images);

  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset_1"));
  EXPECT_TRUE(session.metadata().image_series.empty());

  ASSERT_TRUE(session.select_container(QStringLiteral("/dataset_2"), error)) << error.toStdString();
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset_2"));
  ASSERT_EQ(session.metadata().image_series.size(), 1U);

  ASSERT_TRUE(session.select_container(QStringLiteral("/dataset_2/viewer_series"), error)) << error.toStdString();
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset_2/viewer_series"));
  EXPECT_EQ(session.metadata().acquisition_count, 0U);
  ASSERT_EQ(session.metadata().image_series.size(), 1U);
  EXPECT_EQ(session.metadata().image_series.front().series_id, std::string(kImageSeries));

  ksj::viewer::ImagePresentation standalone_image;
  ASSERT_TRUE(
    ksj::viewer::make_image_presentation(session, QString::fromLatin1(kImageSeries.data(), kImageSeries.size()), 0U, 0U,
                                         0U, ksj::viewer::ImageDisplaySettings{}, standalone_image, error))
    << error.toStdString();
  EXPECT_FALSE(standalone_image.image.isNull());

  EXPECT_FALSE(session.select_container(QStringLiteral("/dataset"), error));
  EXPECT_EQ(error, QStringLiteral("the selected container is not a readable standard ISMRMRD container"));
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset_2/viewer_series"));
  EXPECT_EQ(session.metadata().image_series.size(), 1U);
}

TEST(KSpaceJetViewerPresentation, OpensPureStandardImageArtifactWithoutAcquisitions) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto artifact_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-image-artifact.mrd"));
  write_synthetic_image_artifact(native_path(artifact_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(artifact_path, error)) << error.toStdString();
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset"));
  EXPECT_EQ(session.metadata().acquisition_count, 0U);
  ASSERT_EQ(session.metadata().image_series.size(), 1U);

  const auto& containers = session.available_containers();
  const auto container = std::find_if(containers.begin(), containers.end(), [](const auto& descriptor) {
    return descriptor.path == "/dataset";
  });
  ASSERT_NE(container, containers.end());
  EXPECT_TRUE(container->has_header);
  EXPECT_FALSE(container->has_acquisitions);
  EXPECT_TRUE(container->has_images);

  ksj::viewer::ImagePresentation image;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(session,
                                                   QString::fromLatin1(kImageSeries.data(), kImageSeries.size()), 0U,
                                                   0U, 0U, ksj::viewer::ImageDisplaySettings{}, image, error))
    << error.toStdString();
  EXPECT_FALSE(image.image.isNull());
  EXPECT_EQ(image.dimensions, (std::array<std::uint16_t, 4U>{kSourceImageWidth, kSourceImageHeight, 1U, 1U}));
}

TEST(KSpaceJetViewerPresentation, ParsesOnlyBoundedPipelineDefinitions) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto pipeline_path = QDir(temporary_directory.path()).filePath(QStringLiteral("pipeline.json"));
  write_file(pipeline_path, valid_pipeline_definition().toUtf8());

  ksj::viewer::PipelinePresentation presentation;
  QString error;
  ASSERT_TRUE(ksj::viewer::load_pipeline_presentation(pipeline_path, presentation, error)) << error.toStdString();
  EXPECT_TRUE(presentation.summary.contains(QStringLiteral("Parsed only")));
  EXPECT_TRUE(presentation.summary.contains(QStringLiteral("no Provider resolution")));
  EXPECT_TRUE(presentation.canonical_json.contains(QStringLiteral("org.example.viewer-test")));
  EXPECT_EQ(presentation.details.value(QStringLiteral("view")).toString(), QStringLiteral("pipeline"));
  EXPECT_EQ(presentation.details.value(QStringLiteral("input_profile")).toString(), QStringLiteral("ismrmrd-hdf5"));
  EXPECT_EQ(presentation.details.value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));

  const auto over_limit_path = QDir(temporary_directory.path()).filePath(QStringLiteral("over-limit.json"));
  const auto over_limit_bytes =
    static_cast<qsizetype>(ksj::recon::graph::kPipelineDefinitionJsonParseLimits.max_document_bytes + 1U);
  write_file(over_limit_path, QByteArray(over_limit_bytes, ' '));
  EXPECT_FALSE(ksj::viewer::load_pipeline_presentation(over_limit_path, presentation, error));
  EXPECT_TRUE(error.contains(QStringLiteral("exceeds the")));
  EXPECT_TRUE(error.contains(QStringLiteral("parser limit")));
}

TEST(KSpaceJetViewerPresentation, ExportsOnlyLabelledVisualizationDerivatives) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();

  ksj::viewer::VisualizationDerivative derivative;
  derivative.view_name = QStringLiteral("synthetic-image");
  derivative.source_description = QStringLiteral("synthetic ISMRMRD source");
  derivative.image = QImage(3, 2, QImage::Format_Grayscale8);
  ASSERT_FALSE(derivative.image.isNull());
  derivative.image.fill(127U);
  derivative.csv_columns = {QStringLiteral("x"), QStringLiteral("magnitude")};
  derivative.csv_rows = {{QStringLiteral("0"), QStringLiteral("1.25")}};
  derivative.details.insert(QStringLiteral("view"), QStringLiteral("synthetic-image"));

  const QDir directory(temporary_directory.path());
  QString error;

  const auto png_path = directory.filePath(QStringLiteral("derivative.png"));
  ASSERT_TRUE(ksj::viewer::export_visualization_derivative(derivative, png_path,
                                                           ksj::viewer::VisualizationExportFormat::png, error))
    << error.toStdString();
  QImageReader png_reader(png_path);
  ASSERT_TRUE(png_reader.canRead()) << png_reader.errorString().toStdString();
  EXPECT_EQ(png_reader.text(QStringLiteral("KSpaceJet.ArtifactKind")), QStringLiteral("visualization-derivative"));
  EXPECT_EQ(png_reader.text(QStringLiteral("KSpaceJet.ViewName")), derivative.view_name);
  EXPECT_EQ(png_reader.text(QStringLiteral("KSpaceJet.SourceDescription")), derivative.source_description);
  const auto decoded_png = png_reader.read();
  ASSERT_FALSE(decoded_png.isNull()) << png_reader.errorString().toStdString();
  EXPECT_EQ(decoded_png.size(), derivative.image.size());

  const auto svg_path = directory.filePath(QStringLiteral("derivative.svg"));
  ASSERT_TRUE(ksj::viewer::export_visualization_derivative(derivative, svg_path,
                                                           ksj::viewer::VisualizationExportFormat::svg, error))
    << error.toStdString();
  const auto svg = read_file(svg_path);
  EXPECT_TRUE(svg.contains("KSpaceJet.ArtifactKind"));
  EXPECT_TRUE(svg.contains("visualization-derivative"));
  EXPECT_TRUE(svg.contains("synthetic ISMRMRD source"));

  const auto csv_path = directory.filePath(QStringLiteral("derivative.csv"));
  ASSERT_TRUE(ksj::viewer::export_visualization_derivative(derivative, csv_path,
                                                           ksj::viewer::VisualizationExportFormat::csv, error))
    << error.toStdString();
  const auto csv = read_file(csv_path);
  EXPECT_TRUE(csv.contains("# KSpaceJet.ArtifactKind=visualization-derivative"));
  EXPECT_TRUE(csv.contains("# KSpaceJet.ViewName=\"synthetic-image\""));
  EXPECT_TRUE(csv.contains("# KSpaceJet.SourceDescription=\"synthetic ISMRMRD source\""));

  const auto json_path = directory.filePath(QStringLiteral("derivative.json"));
  ASSERT_TRUE(ksj::viewer::export_visualization_derivative(derivative, json_path,
                                                           ksj::viewer::VisualizationExportFormat::json, error))
    << error.toStdString();
  QJsonParseError parse_error;
  const auto json = QJsonDocument::fromJson(read_file(json_path), &parse_error);
  ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << parse_error.errorString().toStdString();
  ASSERT_TRUE(json.isObject());
  EXPECT_EQ(json.object().value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));
  EXPECT_EQ(json.object().value(QStringLiteral("view_name")).toString(), derivative.view_name);
  EXPECT_EQ(json.object().value(QStringLiteral("source_description")).toString(), derivative.source_description);

  const auto rejected_mrd_path = directory.filePath(QStringLiteral("not-an-artifact.mrd"));
  EXPECT_FALSE(ksj::viewer::export_visualization_derivative(derivative, rejected_mrd_path,
                                                            ksj::viewer::VisualizationExportFormat::png, error));
  EXPECT_TRUE(error.contains(QStringLiteral("MRI artifact extensions")));
  EXPECT_FALSE(QFile::exists(rejected_mrd_path));

  const auto mismatched_path = directory.filePath(QStringLiteral("wrong-extension.json"));
  EXPECT_FALSE(ksj::viewer::export_visualization_derivative(derivative, mismatched_path,
                                                            ksj::viewer::VisualizationExportFormat::png, error));
  EXPECT_TRUE(error.contains(QStringLiteral("must use the .png extension")));
  EXPECT_FALSE(QFile::exists(mismatched_path));

  auto no_image = derivative;
  no_image.image = {};
  const auto no_image_path = directory.filePath(QStringLiteral("missing-image.png"));
  EXPECT_FALSE(ksj::viewer::export_visualization_derivative(no_image, no_image_path,
                                                            ksj::viewer::VisualizationExportFormat::png, error));
  EXPECT_TRUE(error.contains(QStringLiteral("require a non-null image")));
  EXPECT_FALSE(QFile::exists(no_image_path));
}
