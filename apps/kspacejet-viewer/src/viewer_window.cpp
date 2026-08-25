#include "viewer_window.hpp"

#include <QAction>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QToolBar>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

const QSize kMaximumUiPixmapSize{1600, 1200};
const QSize kMaximumZoomedPixmapSize{4096, 4096};
constexpr int kMaximumAttributePreviewCharacters = 50;

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

[[nodiscard]] bool inspection_object_locator(const SemanticObjectKind kind,
                                             ksj::ismrmrd::InspectionObjectLocator& locator) {
  switch (kind) {
    case SemanticObjectKind::container:
      locator = {.kind = ksj::ismrmrd::InspectionObjectKind::container};
      return true;
    case SemanticObjectKind::header:
      locator = {.kind = ksj::ismrmrd::InspectionObjectKind::xml};
      return true;
    case SemanticObjectKind::acquisitions:
      locator = {.kind = ksj::ismrmrd::InspectionObjectKind::acquisitions};
      return true;
    case SemanticObjectKind::waveforms:
      locator = {.kind = ksj::ismrmrd::InspectionObjectKind::waveforms};
      return true;
    case SemanticObjectKind::source_file:
    case SemanticObjectKind::images:
    case SemanticObjectKind::pipeline:
      return false;
  }
  return false;
}

[[nodiscard]] QString attribute_array_size(const ksj::ismrmrd::InspectionObjectAttributeDescriptor& attribute) {
  if (attribute.dimensions.empty()) {
    return QStringLiteral("1");
  }
  QStringList dimensions;
  dimensions.reserve(static_cast<qsizetype>(attribute.dimensions.size()));
  for (const auto dimension : attribute.dimensions) {
    dimensions.append(QString::number(static_cast<qulonglong>(dimension)));
  }
  return dimensions.join(QStringLiteral(" × "));
}

[[nodiscard]] QString attribute_preview(const ksj::ismrmrd::InspectionObjectAttributeDescriptor& attribute) {
  using State = ksj::ismrmrd::InspectionAttributeValuePreviewState;
  QString preview;
  switch (attribute.value_preview_state) {
    case State::available:
      preview =
        attribute.value_preview.empty()
          ? QObject::tr("(empty)")
          : QString::fromUtf8(attribute.value_preview.data(), static_cast<qsizetype>(attribute.value_preview.size()));
      break;
    case State::truncated:
      preview =
        QString::fromUtf8(attribute.value_preview.data(), static_cast<qsizetype>(attribute.value_preview.size())) +
        QStringLiteral("…");
      break;
    case State::unsupported:
      preview = QObject::tr("(unsupported preview)");
      break;
  }
  if (preview.size() > kMaximumAttributePreviewCharacters) {
    preview = QStringLiteral("%1…").arg(preview.left(kMaximumAttributePreviewCharacters - 1));
  }
  return preview;
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

void repolish(QWidget* widget) {
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  widget->update();
}

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

void set_display_image(QLabel* label, const QImage& image, const QString& empty_text, const int zoom_percent = 0) {
  label->setProperty("hasDisplayImage", !image.isNull());
  repolish(label);
  if (image.isNull()) {
    label->setPixmap({});
    label->setText(empty_text);
    if (zoom_percent > 0) {
      label->setMinimumSize(360, 300);
      label->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }
    return;
  }

  label->setText({});
  const auto pixmap = QPixmap::fromImage(image);
  if (zoom_percent > 0) {
    const auto scaled_width = std::max(1, (image.width() * zoom_percent) / 100);
    const auto scaled_height = std::max(1, (image.height() * zoom_percent) / 100);
    const auto display_size = QSize{scaled_width, scaled_height}.boundedTo(kMaximumZoomedPixmapSize);
    const auto displayed = pixmap.scaled(display_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    label->setPixmap(displayed);
    label->setFixedSize(displayed.size());
    return;
  }

  label->setMinimumSize(QSize{});
  label->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  const auto available_size = label->contentsRect()
                                .size()
                                .boundedTo(kMaximumUiPixmapSize)
                                .expandedTo(QSize(320, 240))
                                .boundedTo(kMaximumUiPixmapSize);
  label->setPixmap(pixmap.scaled(available_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
  file_menu->addAction(close_mrd_action_);
  file_menu->addSeparator();
  file_menu->addAction(open_pipeline_action_);
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

  auto* window_menu = menuBar()->addMenu(tr("&Window"));
  window_menu->setObjectName(QStringLiteral("viewerWindowMenu"));
  close_typed_view_action_ = window_menu->addAction(tr("Close &typed data view"));
  close_typed_view_action_->setObjectName(QStringLiteral("closeTypedDataViewAction"));
  close_typed_view_action_->setToolTip(tr("Return to the selected object's HDFView-style information pane"));
  close_typed_view_action_->setEnabled(false);

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
  connect(close_typed_view_action_, &QAction::triggered, this, [this] {
    set_typed_data_visible(false);
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
      tr("1. Open a local standard ISMRMRD file from File or the file bar.\n"
         "2. Select a verified semantic object in the hierarchy. Selection only updates the inspector.\n"
         "3. Use Inspect, Open As..., a double click, or the context menu to open a bounded typed view.\n"
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

  auto* file_bar = new QFrame(workspace_root);
  file_bar->setObjectName(QStringLiteral("viewerFileBar"));
  file_bar->setProperty("surfaceRole", QStringLiteral("filebar"));
  auto* file_layout = new QHBoxLayout(file_bar);
  file_layout->setContentsMargins(4, 2, 4, 2);
  file_layout->setSpacing(4);
  recent_sources_button_ = new QToolButton(file_bar);
  recent_sources_button_->setObjectName(QStringLiteral("recentSourcesButton"));
  recent_sources_button_->setText(tr("Recent Files"));
  recent_sources_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  file_layout->addWidget(recent_sources_button_);
  source_file_bar_ = new QComboBox(file_bar);
  source_file_bar_->setObjectName(QStringLiteral("sourceFileBar"));
  source_file_bar_->setEditable(true);
  source_file_bar_->setInsertPolicy(QComboBox::NoInsert);
  source_file_bar_->setMinimumContentsLength(42);
  source_file_bar_->setToolTip(tr("Enter or choose a local standard ISMRMRD file path. URLs are not supported."));
  file_layout->addWidget(source_file_bar_, 1);
  clear_file_bar_button_ = new QToolButton(file_bar);
  clear_file_bar_button_->setObjectName(QStringLiteral("clearFileBarButton"));
  clear_file_bar_button_->setText(tr("Clear Text"));
  clear_file_bar_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  clear_file_bar_button_->setToolTip(tr("Clear the file-bar text without closing the current source."));
  file_layout->addWidget(clear_file_bar_button_);
  auto* read_only_badge = make_text(tr("READ-ONLY"), "modeBadge", file_bar);
  read_only_badge->setObjectName(QStringLiteral("fileBarReadonlyBadge"));
  read_only_badge->setAlignment(Qt::AlignCenter);
  file_layout->addWidget(read_only_badge);
  root_layout->addWidget(file_bar);

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

  details_splitter_ = new QSplitter(Qt::Vertical, main_splitter);
  details_splitter_->setObjectName(QStringLiteral("viewerDetailsSplitter"));
  details_splitter_->setChildrenCollapsible(false);

  auto* inspector_surface = new QFrame(details_splitter_);
  inspector_surface->setObjectName(QStringLiteral("objectInspectorSurface"));
  inspector_surface->setProperty("surfaceRole", QStringLiteral("panel"));
  auto* inspector_layout = new QVBoxLayout(inspector_surface);
  inspector_layout->setContentsMargins(0, 0, 0, 0);
  inspector_layout->setSpacing(0);
  object_inspector_ = new QTabWidget(inspector_surface);
  object_inspector_->setObjectName(QStringLiteral("objectInspector"));
  object_inspector_->setDocumentMode(true);

  auto* attributes_page = new QWidget(object_inspector_);
  auto* attributes_layout = new QVBoxLayout(attributes_page);
  attributes_layout->setContentsMargins(6, 6, 6, 6);
  attributes_layout->setSpacing(4);
  object_attributes_status_ =
    make_text(tr("Read-only, bounded native HDF5 attributes attached to the selected standard MRD object. "
                 "ISMRMRD image MetaAttributes are shown only in Image details."),
              "inspectorHint", attributes_page);
  object_attributes_status_->setObjectName(QStringLiteral("objectAttributesStatus"));
  attributes_layout->addWidget(object_attributes_status_);
  object_attributes_ = new QTableWidget(attributes_page);
  object_attributes_->setObjectName(QStringLiteral("objectAttributesInfo"));
  object_attributes_->setColumnCount(4);
  object_attributes_->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Array Size"), tr("Value [50]")});
  object_attributes_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  object_attributes_->setSelectionBehavior(QAbstractItemView::SelectRows);
  object_attributes_->setSelectionMode(QAbstractItemView::SingleSelection);
  object_attributes_->setAlternatingRowColors(true);
  object_attributes_->verticalHeader()->setVisible(false);
  object_attributes_->horizontalHeader()->setStretchLastSection(true);
  attributes_layout->addWidget(object_attributes_);
  object_inspector_->addTab(attributes_page, tr("Object Attribute Info"));

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
  // HDFView opens the general object summary first while retaining its
  // Object Attribute Info tab immediately beside it.
  object_inspector_->setCurrentIndex(1);
  inspector_layout->addWidget(object_inspector_, 1);

  typed_data_surface_ = new QFrame(details_splitter_);
  typed_data_surface_->setObjectName(QStringLiteral("viewerDataSurface"));
  typed_data_surface_->setProperty("surfaceRole", QStringLiteral("panel"));
  auto* data_layout = new QVBoxLayout(typed_data_surface_);
  data_layout->setContentsMargins(0, 0, 0, 0);
  tabs_ = new QTabWidget(typed_data_surface_);
  tabs_->setObjectName(QStringLiteral("viewerDataViews"));
  tabs_->setDocumentMode(true);
  data_layout->addWidget(tabs_);

  create_metadata_page();
  create_kspace_page();
  create_image_page();
  create_pipeline_page();

  details_splitter_->addWidget(inspector_surface);
  details_splitter_->addWidget(typed_data_surface_);
  details_splitter_->setStretchFactor(0, 1);
  details_splitter_->setStretchFactor(1, 1);
  typed_data_surface_->hide();
  details_splitter_->setSizes({840, 0});

  main_splitter->addWidget(tree_surface);
  main_splitter->addWidget(details_splitter_);
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
  connect(recent_sources_button_, &QToolButton::clicked, this, [this] {
    source_file_bar_->showPopup();
  });
  connect(clear_file_bar_button_, &QToolButton::clicked, this, [this] {
    source_file_bar_->setEditText({});
  });
  const auto open_file_bar_source = [this](const QString& file_path) {
    QString error;
    if (!open_mrd_source(file_path, error)) {
      QMessageBox::critical(this, tr("Cannot open ISMRMRD file"), error);
    }
  };
  connect(source_file_bar_, &QComboBox::textActivated, this, open_file_bar_source);
  connect(source_file_bar_->lineEdit(), &QLineEdit::returnPressed, this, [this, open_file_bar_source] {
    open_file_bar_source(source_file_bar_->currentText());
  });
  connect(tabs_, &QTabWidget::currentChanged, this, [this](const int index) {
    Q_UNUSED(index)
    update_export_availability();
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
  const auto page = make_workspace_page(tabs_, QStringLiteral("metadataPage"), tr("STANDARD ISMRMRD"), tr("XML header"),
                                        tr("Bounded read-only XML from the selected header object."));
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
  content_layout->addWidget(
    make_text(tr("Bounded preview only; the source file remains unchanged."), "sectionDetail", content));
  metadata_xml_ = make_read_only_text(content);
  metadata_xml_->setObjectName(QStringLiteral("metadataXmlPreview"));
  content_layout->addWidget(metadata_xml_, 1);

  metadata_stack_->addWidget(content);
  page.layout->addWidget(metadata_stack_, 1);
  tabs_->addTab(page.widget, tr("XML"));
}

void ViewerWindow::create_kspace_page() {
  const auto page =
    make_workspace_page(tabs_, QStringLiteral("kspacePage"), tr("CARTESIAN K-SPACE"), tr("K-space"),
                        tr("Render one bounded raw Cartesian ISMRMRD plane. This is not a reconstructed image."));

  auto* controls_card = make_surface(page.widget, QStringLiteral("kspaceControlsCard"), QStringLiteral("controls"));
  auto* controls = new QHBoxLayout(controls_card);
  controls->setContentsMargins(16, 12, 16, 12);
  controls->setSpacing(10);
  controls->addWidget(make_text(tr("Reference acquisition"), "controlLabel", controls_card));
  acquisition_ordinal_ = new QSpinBox(controls_card);
  acquisition_ordinal_->setObjectName(QStringLiteral("acquisitionOrdinal"));
  acquisition_ordinal_->setRange(0, 0);
  controls->addWidget(acquisition_ordinal_);
  controls->addWidget(make_text(tr("Coil"), "controlLabel", controls_card));
  kspace_coil_ = new QComboBox(controls_card);
  kspace_coil_->setObjectName(QStringLiteral("kspaceCoilSelector"));
  kspace_coil_->setMinimumContentsLength(20);
  controls->addWidget(kspace_coil_);
  render_kspace_button_ = new QToolButton(controls_card);
  render_kspace_button_->setObjectName(QStringLiteral("renderKspaceButton"));
  render_kspace_button_->setText(tr("Render Cartesian K-space"));
  render_kspace_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  render_kspace_button_->setProperty("buttonRole", QStringLiteral("primary"));
  render_kspace_button_->setMinimumHeight(34);
  controls->addWidget(render_kspace_button_);
  controls->addStretch(1);
  page.layout->addWidget(controls_card);
  connect(render_kspace_button_, &QToolButton::clicked, this, [this] {
    load_kspace();
  });
  connect(acquisition_ordinal_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
    kspace_presentation_ = {};
    refresh_kspace_controls();
    refresh_kspace();
  });
  connect(kspace_coil_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    kspace_presentation_ = {};
    refresh_kspace();
  });

  auto* view_splitter = new QSplitter(Qt::Horizontal, page.widget);
  view_splitter->setObjectName(QStringLiteral("kspaceViewSplitter"));
  view_splitter->setChildrenCollapsible(false);
  auto* canvas_card = make_surface(view_splitter, QStringLiteral("kspaceCanvasCard"), QStringLiteral("canvas"));
  auto* canvas_layout = new QVBoxLayout(canvas_card);
  canvas_layout->setContentsMargins(18, 16, 18, 18);
  canvas_layout->setSpacing(8);
  add_card_heading(canvas_layout, canvas_card, tr("Cartesian K-space magnitude"),
                   tr("x: centered readout; y: k-space encode step 1; log10(1 + RMS magnitude)."));
  kspace_image_ = new QLabel(canvas_card);
  kspace_image_->setObjectName(QStringLiteral("kspaceCanvas"));
  kspace_image_->setAlignment(Qt::AlignCenter);
  kspace_image_->setWordWrap(true);
  kspace_image_->setMinimumSize(360, 300);
  kspace_image_->setProperty("surfaceRole", QStringLiteral("imageCanvas"));
  canvas_layout->addWidget(kspace_image_, 1);
  view_splitter->addWidget(canvas_card);

  auto* detail_card = make_surface(view_splitter, QStringLiteral("kspaceDetailsCard"));
  auto* detail_layout = new QVBoxLayout(detail_card);
  detail_layout->setContentsMargins(18, 16, 18, 18);
  detail_layout->setSpacing(8);
  add_card_heading(detail_layout, detail_card, tr("K-space plane details"),
                   tr("Frame selection, aggregation and display bounds for the raw Cartesian plane."));
  kspace_summary_ = make_read_only_text(detail_card);
  kspace_summary_->setObjectName(QStringLiteral("kspaceSummary"));
  kspace_summary_->setMinimumHeight(110);
  kspace_summary_->setMaximumHeight(180);
  detail_layout->addWidget(kspace_summary_);
  detail_layout->addWidget(make_text(tr("Reference acquisition header"), "cardTitle", detail_card));
  acquisition_header_table_ = new QTableWidget(detail_card);
  acquisition_header_table_->setObjectName(QStringLiteral("acquisitionHeaderTable"));
  acquisition_header_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  acquisition_header_table_->setSelectionMode(QAbstractItemView::NoSelection);
  acquisition_header_table_->setAlternatingRowColors(true);
  acquisition_header_table_->verticalHeader()->setVisible(false);
  detail_layout->addWidget(acquisition_header_table_, 1);
  view_splitter->addWidget(detail_card);
  view_splitter->setStretchFactor(0, 3);
  view_splitter->setStretchFactor(1, 2);
  view_splitter->setSizes({640, 360});
  page.layout->addWidget(view_splitter, 1);
  tabs_->addTab(page.widget, tr("K-space"));
}

void ViewerWindow::create_image_page() {
  const auto page =
    make_workspace_page(tabs_, QStringLiteral("imagePage"), tr("STANDARD IMAGE"), tr("Image"),
                        tr("Read one selected ISMRMRD image plane and retain only a bounded display derivative."));

  auto* controls_card = make_surface(page.widget, QStringLiteral("imageControlsCard"), QStringLiteral("controls"));
  auto* controls = new QHBoxLayout(controls_card);
  controls->setContentsMargins(16, 12, 16, 12);
  controls->setSpacing(10);
  controls->addWidget(make_text(tr("Series"), "controlLabel", controls_card));
  image_series_ = new QComboBox(controls_card);
  image_series_->setObjectName(QStringLiteral("imageSeries"));
  image_series_->setMinimumContentsLength(10);
  controls->addWidget(image_series_, 1);
  controls->addWidget(make_text(tr("Ordinal"), "controlLabel", controls_card));
  image_ordinal_ = new QSpinBox(controls_card);
  image_ordinal_->setObjectName(QStringLiteral("imageOrdinal"));
  image_ordinal_->setRange(0, 0);
  controls->addWidget(image_ordinal_);
  controls->addWidget(make_text(tr("Z"), "controlLabel", controls_card));
  image_z_ = new QSpinBox(controls_card);
  image_z_->setObjectName(QStringLiteral("imageZ"));
  image_z_->setRange(0, 0);
  controls->addWidget(image_z_);
  controls->addWidget(make_text(tr("Channel"), "controlLabel", controls_card));
  image_channel_ = new QSpinBox(controls_card);
  image_channel_->setObjectName(QStringLiteral("imageChannel"));
  image_channel_->setRange(0, 0);
  controls->addWidget(image_channel_);
  image_cine_button_ = new QToolButton(controls_card);
  image_cine_button_->setObjectName(QStringLiteral("imageCineButton"));
  image_cine_button_->setText(tr("Play cine"));
  image_cine_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  image_cine_button_->setToolTip(tr("Advance the selected image ordinal without retaining source pixels."));
  controls->addWidget(image_cine_button_);
  load_image_button_ = new QToolButton(controls_card);
  load_image_button_->setObjectName(QStringLiteral("imageInspectButton"));
  load_image_button_->setText(tr("Inspect image"));
  load_image_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  load_image_button_->setProperty("buttonRole", QStringLiteral("primary"));
  load_image_button_->setMinimumHeight(34);
  controls->addWidget(load_image_button_);
  page.layout->addWidget(controls_card);

  auto* display_controls_card =
    make_surface(page.widget, QStringLiteral("imageDisplayControlsCard"), QStringLiteral("controls"));
  auto* display_controls = new QHBoxLayout(display_controls_card);
  display_controls->setContentsMargins(16, 8, 16, 8);
  display_controls->setSpacing(10);
  image_auto_window_ = new QCheckBox(tr("Auto window"), display_controls_card);
  image_auto_window_->setObjectName(QStringLiteral("imageAutoWindow"));
  image_auto_window_->setChecked(true);
  image_auto_window_->setToolTip(tr("Use the selected plane's bounded magnitude range."));
  display_controls->addWidget(image_auto_window_);
  display_controls->addWidget(make_text(tr("Center"), "controlLabel", display_controls_card));
  image_window_center_ = new QDoubleSpinBox(display_controls_card);
  image_window_center_->setObjectName(QStringLiteral("imageWindowCenter"));
  image_window_center_->setDecimals(6);
  image_window_center_->setRange(-1.0e100, 1.0e100);
  image_window_center_->setKeyboardTracking(false);
  image_window_center_->setToolTip(tr("Manual window center. Click Inspect image to apply it."));
  display_controls->addWidget(image_window_center_);
  display_controls->addWidget(make_text(tr("Width"), "controlLabel", display_controls_card));
  image_window_width_ = new QDoubleSpinBox(display_controls_card);
  image_window_width_->setObjectName(QStringLiteral("imageWindowWidth"));
  image_window_width_->setDecimals(6);
  image_window_width_->setRange(std::numeric_limits<double>::min(), 1.0e100);
  image_window_width_->setValue(1.0);
  image_window_width_->setKeyboardTracking(false);
  image_window_width_->setToolTip(tr("Manual window width. Click Inspect image to apply it."));
  display_controls->addWidget(image_window_width_);
  display_controls->addWidget(make_text(tr("Zoom"), "controlLabel", display_controls_card));
  image_zoom_percent_ = new QSpinBox(display_controls_card);
  image_zoom_percent_->setObjectName(QStringLiteral("imageZoomPercent"));
  image_zoom_percent_->setRange(25, 400);
  image_zoom_percent_->setSingleStep(25);
  image_zoom_percent_->setSuffix(QStringLiteral("%"));
  image_zoom_percent_->setValue(100);
  image_zoom_percent_->setToolTip(tr("Scale only the bounded display derivative; source pixels are not retained."));
  display_controls->addWidget(image_zoom_percent_);
  display_controls->addStretch(1);
  page.layout->addWidget(display_controls_card);

  connect(image_series_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    stop_image_cine();
    update_image_controls();
  });
  connect(image_ordinal_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
    stop_image_cine();
    update_image_controls();
  });
  connect(image_auto_window_, &QCheckBox::toggled, this, [this] {
    update_control_state();
  });
  connect(image_zoom_percent_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
    if (!image_presentation_.image.isNull()) {
      refresh_image();
    }
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
    load_image();
  });

  auto* view_splitter = new QSplitter(Qt::Horizontal, page.widget);
  view_splitter->setObjectName(QStringLiteral("imageViewSplitter"));
  view_splitter->setChildrenCollapsible(false);
  auto* canvas_card = make_surface(view_splitter, QStringLiteral("imageCanvasCard"), QStringLiteral("canvas"));
  auto* canvas_layout = new QVBoxLayout(canvas_card);
  canvas_layout->setContentsMargins(18, 16, 18, 18);
  canvas_layout->setSpacing(8);
  add_card_heading(canvas_layout, canvas_card, tr("Selected image plane"),
                   tr("Standard image data displayed without creating a new MRI artifact."));
  image_image_ = new QLabel(canvas_card);
  image_image_->setObjectName(QStringLiteral("imageCanvas"));
  image_image_->setAlignment(Qt::AlignCenter);
  image_image_->setWordWrap(true);
  image_image_->setMinimumSize(360, 300);
  image_image_->setProperty("surfaceRole", QStringLiteral("imageCanvas"));
  image_image_->setMouseTracking(true);
  image_image_->installEventFilter(this);
  image_scroll_area_ = new QScrollArea(canvas_card);
  image_scroll_area_->setObjectName(QStringLiteral("imageCanvasScrollArea"));
  image_scroll_area_->setWidget(image_image_);
  image_scroll_area_->setWidgetResizable(false);
  image_scroll_area_->setAlignment(Qt::AlignCenter);
  image_scroll_area_->setMinimumSize(360, 300);
  canvas_layout->addWidget(image_scroll_area_, 1);
  image_pixel_probe_ =
    make_text(tr("Pointer: inspect an image to probe the current display derivative."), "cardDetail", canvas_card);
  image_pixel_probe_->setObjectName(QStringLiteral("imagePixelProbe"));
  canvas_layout->addWidget(image_pixel_probe_);
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
  tabs_->addTab(page.widget, tr("Image"));
}

void ViewerWindow::create_pipeline_page() {
  const auto page = make_workspace_page(tabs_, QStringLiteral("pipelinePage"), tr("AUTHORED PIPELINE"), tr("Pipeline"),
                                        tr("Read and validate a PipelineDefinition document without resolving, "
                                           "loading, compiling, or executing a Provider."));
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

  auto* json_card = make_surface(content, QStringLiteral("pipelineCanonicalJsonCard"));
  auto* json_layout = new QVBoxLayout(json_card);
  json_layout->setContentsMargins(18, 16, 18, 18);
  json_layout->setSpacing(8);
  add_card_heading(json_layout, json_card, tr("Canonical JSON"),
                   tr("Read-only normalized presentation of the authored PipelineDefinition."));
  pipeline_canonical_json_ = make_read_only_text(json_card);
  pipeline_canonical_json_->setObjectName(QStringLiteral("pipelineCanonicalJson"));
  json_layout->addWidget(pipeline_canonical_json_, 1);
  content_layout->addWidget(json_card, 1);
  pipeline_stack_->addWidget(content);

  page.layout->addWidget(pipeline_stack_, 1);
  tabs_->addTab(page.widget, tr("Pipeline"));
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
    load_kspace();
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
      load_kspace();
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
    QMessageBox::warning(this, tr("Cannot select ISMRMRD container"), error);
    rebuild_dataset_navigation();
    return;
  }
  set_workspace_view(view);
  if (view == WorkspaceView::kspace) {
    refresh_acquisition_header_table();
  }
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
      QStringList semantic_labels;
      if (descriptor.has_acquisitions) {
        semantic_labels.append(QStringLiteral("[RAW]"));
      }
      if (descriptor.has_images) {
        semantic_labels.append(QStringLiteral("[IMAGE]"));
      }
      if (descriptor.has_waveforms) {
        semantic_labels.append(QStringLiteral("[WAVEFORM]"));
      }
      if (semantic_labels.isEmpty() && descriptor.has_header) {
        semantic_labels.append(QStringLiteral("[HEADER]"));
      }
      auto* container_item =
        make_child(source_item,
                   tr("%1  %2  -  %3 acquisition(s), %4 image series")
                     .arg(semantic_labels.join(QLatin1Char(' ')), container_path)
                     .arg(descriptor.acquisition_count)
                     .arg(descriptor.image_series_count),
                   SemanticObjectKind::container, container_path, WorkspaceView::metadata, true,
                   descriptor.has_header
                     ? tr("Verified standard ISMRMRD container: %1").arg(container_path)
                     : tr("Verified standalone standard ISMRMRD image-series container: %1").arg(container_path));

      make_child(container_item, tr("Header / XML"), SemanticObjectKind::header, container_path,
                 WorkspaceView::metadata, descriptor.has_header,
                 descriptor.has_header ? tr("Inspect the bounded standard XML header.")
                                       : tr("This container has no standard XML header."));
      make_child(container_item, tr("Acquisitions / K-space"), SemanticObjectKind::acquisitions, container_path,
                 WorkspaceView::kspace, descriptor.has_acquisitions && descriptor.acquisition_count > 0U,
                 descriptor.has_acquisitions ? tr("Render a bounded raw Cartesian k-space plane or inspect its header.")
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
  update_selection_actions();
}

void ViewerWindow::update_object_inspector(QTreeWidgetItem* item) {
  if (object_general_ == nullptr || object_name_field_ == nullptr || object_path_field_ == nullptr ||
      object_type_field_ == nullptr || object_access_field_ == nullptr || object_semantics_table_ == nullptr ||
      object_members_table_ == nullptr || object_attributes_status_ == nullptr || object_attributes_ == nullptr) {
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
  const auto set_attribute_rows = [this](const QList<QStringList>& rows) {
    set_table_contents(object_attributes_, {tr("Name"), tr("Type"), tr("Array Size"), tr("Value [50]")}, rows);
    object_attributes_->resizeRowsToContents();
  };
  if (item == nullptr) {
    object_name_field_->setText(tr("No object selected"));
    object_path_field_->clear();
    object_type_field_->setText(tr("ISMRMRD semantic object"));
    object_access_field_->setText(tr("Read-only"));
    set_semantics_rows({{tr("Status:"), tr("Select an object in the ISMRMRD file hierarchy.")}});
    set_members_rows({});
    object_attributes_status_->setText(tr("No standard ISMRMRD object is selected."));
    set_attribute_rows({});
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
          {tr("head.idx"), tr("ISMRMRD encoding counters"), tr("standard fields")},
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

  ksj::ismrmrd::InspectionObjectLocator locator;
  if (!inspection_object_locator(kind, locator)) {
    if (kind == SemanticObjectKind::images) {
      object_attributes_status_->setText(
        tr("Images is a semantic collection, not one HDF5 object. Its ISMRMRD Image MetaAttributes are displayed "
           "only in the Image details view."));
    } else if (kind == SemanticObjectKind::pipeline) {
      object_attributes_status_->setText(
        tr("PipelineDefinition is JSON rather than an HDF5 object and has no HDF5 attributes."));
    } else {
      object_attributes_status_->setText(
        tr("Select a standard MRD container or one of its concrete semantic objects to inspect HDF5 attributes."));
    }
    set_attribute_rows({});
    return;
  }

  std::vector<ksj::ismrmrd::InspectionObjectAttributeDescriptor> attributes;
  QString attribute_error;
  if (!inspection_session_.read_object_attributes(container_path, locator, attributes, attribute_error)) {
    object_attributes_status_->setText(
      tr("Unable to read bounded HDF5 attributes for %1: %2").arg(path, attribute_error));
    set_attribute_rows({});
    return;
  }

  QList<QStringList> rows;
  rows.reserve(static_cast<qsizetype>(attributes.size()));
  for (const auto& attribute : attributes) {
    rows.append({QString::fromUtf8(attribute.name.data(), static_cast<qsizetype>(attribute.name.size())),
                 QString::fromUtf8(attribute.type_name.data(), static_cast<qsizetype>(attribute.type_name.size())),
                 attribute_array_size(attribute), attribute_preview(attribute)});
  }
  if (rows.isEmpty()) {
    object_attributes_status_->setText(tr("No HDF5 attributes are attached to %1.").arg(path));
  } else {
    object_attributes_status_->setText(
      tr("%1 HDF5 attribute(s) attached to %2. Values are bounded read-only previews.").arg(rows.size()).arg(path));
  }
  set_attribute_rows(rows);
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
  image_presentation_ = {};
}

void ViewerWindow::set_typed_data_visible(const bool visible) {
  if (typed_data_surface_ == nullptr || details_splitter_ == nullptr) {
    return;
  }
  typed_data_surface_->setVisible(visible);
  details_splitter_->setSizes(visible ? QList<int>{420, 420} : QList<int>{840, 0});
  if (close_typed_view_action_ != nullptr) {
    close_typed_view_action_->setEnabled(visible);
  }
  update_export_availability();
}

void ViewerWindow::set_workspace_view(const WorkspaceView view) {
  if (tabs_ == nullptr) {
    return;
  }
  auto index = static_cast<int>(view);
  if (view == WorkspaceView::image &&
      (!inspection_session_.is_open() || inspection_session_.metadata().image_series.empty())) {
    index = static_cast<int>(WorkspaceView::metadata);
  }
  if (view == WorkspaceView::metadata) {
    metadata_view_open_ = inspection_session_.is_open();
    metadata_xml_->setPlainText(metadata_view_open_ ? metadata_presentation_.xml_preview : QString{});
    metadata_stack_->setCurrentIndex(metadata_view_open_ ? 1 : 0);
  }
  set_typed_data_visible(true);
  tabs_->setCurrentIndex(index);
  update_export_availability();
}

void ViewerWindow::open_mrd() {
  const auto file_path = QFileDialog::getOpenFileName(this, tr("Open standard ISMRMRD file"), {},
                                                      tr("ISMRMRD files (*.mrd *.h5 *.hdf5 *.ismrmrd)"));
  if (file_path.isEmpty()) {
    return;
  }

  QString error;
  if (!open_mrd_source(file_path, error)) {
    QMessageBox::critical(this, tr("Cannot open ISMRMRD file"), error);
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
  metadata_view_open_ = false;
  clear_dataset_derivatives();
  refresh_metadata();
  refresh_kspace();
  refresh_image();
  set_typed_data_visible(false);
  if (source_file_bar_ != nullptr) {
    const QSignalBlocker blocker(source_file_bar_);
    const auto source_path = inspection_session_.source_path();
    if (source_file_bar_->findText(source_path, Qt::MatchExactly) < 0) {
      source_file_bar_->insertItem(0, source_path);
    }
    source_file_bar_->setEditText(source_path);
  }
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
  set_typed_data_visible(false);
  if (source_file_bar_ != nullptr) {
    const QSignalBlocker blocker(source_file_bar_);
    source_file_bar_->setEditText({});
  }
  append_info(tr("Closed read-only source: %1").arg(closed_path));
}

void ViewerWindow::open_pipeline() {
  const auto file_path =
    QFileDialog::getOpenFileName(this, tr("Open PipelineDefinition"), {}, tr("Pipeline JSON (*.json);;All files (*)"));
  if (file_path.isEmpty()) {
    return;
  }

  QString error;
  if (!open_pipeline_source(file_path, error)) {
    QMessageBox::critical(this, tr("Cannot open PipelineDefinition"), error);
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
  refresh_pipeline();
  set_workspace_view(WorkspaceView::pipeline);
  rebuild_dataset_navigation();
  append_info(tr("Parsed PipelineDefinition without resolving or executing it."));
  return true;
}

void ViewerWindow::load_kspace() {
  if (kspace_coil_ == nullptr || kspace_coil_->currentIndex() < 0) {
    QMessageBox::warning(this, tr("Cannot render Cartesian K-space"),
                         tr("Choose a reference acquisition and coil display mode first."));
    return;
  }

  const CartesianKspaceRequest request{
    .reference_acquisition_ordinal = static_cast<std::uint32_t>(acquisition_ordinal_->value()),
    .coil_channel = kspace_coil_->currentData().toInt(),
  };
  KspacePresentation next_presentation;
  QString error;
  if (!make_cartesian_kspace_presentation(inspection_session_, request, next_presentation, error)) {
    QMessageBox::warning(this, tr("Cannot render Cartesian K-space"), error);
    return;
  }
  kspace_presentation_ = std::move(next_presentation);
  refresh_kspace();
  append_info(tr("Rendered a bounded raw Cartesian k-space plane; it is not a reconstructed image."));
}

void ViewerWindow::load_image() {
  if (image_series_->currentText().isEmpty()) {
    QMessageBox::warning(this, tr("Cannot inspect image"), tr("Choose an ISMRMRD image series first."));
    return;
  }

  QString error;
  const ImageDisplaySettings display_settings{
    .auto_window = image_auto_window_->isChecked(),
    .window_center = image_window_center_->value(),
    .window_width = image_window_width_->value(),
  };
  if (!make_image_presentation(
        inspection_session_, image_series_->currentText(), static_cast<std::uint32_t>(image_ordinal_->value()),
        static_cast<std::uint16_t>(image_z_->value()), static_cast<std::uint16_t>(image_channel_->value()),
        display_settings, image_presentation_, error)) {
    QMessageBox::warning(this, tr("Cannot inspect image"), error);
    return;
  }
  {
    const QSignalBlocker z_blocker(image_z_);
    const QSignalBlocker channel_blocker(image_channel_);
    image_z_->setRange(0, std::max(0, static_cast<int>(image_presentation_.dimensions[2]) - 1));
    image_channel_->setRange(0, std::max(0, static_cast<int>(image_presentation_.dimensions[3]) - 1));
  }
  if (image_presentation_.auto_window) {
    const QSignalBlocker center_blocker(image_window_center_);
    const QSignalBlocker width_blocker(image_window_width_);
    image_window_center_->setValue(image_presentation_.applied_window_center);
    image_window_width_->setValue(
      std::max(image_presentation_.applied_window_width, std::numeric_limits<double>::min()));
  }
  refresh_image();
  append_info(tr("Created a bounded image display derivative from one standard ISMRMRD image plane."));
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
    QMessageBox::warning(this, tr("Cannot export display derivative"), error);
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
    QMessageBox::critical(this, tr("Cannot export display derivative"), error);
    return;
  }
  append_info(tr("Exported a visualization derivative. It is not an ISMRMRD MRI artifact: %1").arg(destination));
}

void ViewerWindow::refresh_metadata() {
  metadata_presentation_ = make_metadata_presentation(inspection_session_);
  if (metadata_view_open_ && inspection_session_.is_open()) {
    metadata_xml_->setPlainText(metadata_presentation_.xml_preview);
    metadata_stack_->setCurrentIndex(1);
  } else {
    metadata_xml_->clear();
    metadata_stack_->setCurrentIndex(0);
  }

  const QSignalBlocker acquisition_blocker(acquisition_ordinal_);
  const QSignalBlocker image_series_blocker(image_series_);
  acquisition_ordinal_->setRange(0, 0);
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
  acquisition_ordinal_->setRange(0, bounded_spin_maximum(metadata.acquisition_count));
  for (const auto& series : metadata.image_series) {
    image_series_->addItem(QString::fromUtf8(series.series_id.data(), static_cast<qsizetype>(series.series_id.size())));
  }
  refresh_kspace_controls();
  update_image_controls();
  update_source_context();
  rebuild_dataset_navigation();
  update_export_availability();
}

void ViewerWindow::refresh_acquisition_header_table() {
  if (acquisition_header_table_ == nullptr) {
    return;
  }

  QStringList columns{tr("Field"), tr("Value")};
  QList<QStringList> rows;
  if (!inspection_session_.is_open()) {
    rows.append({tr("Status"), tr("Open a standard ISMRMRD source to inspect acquisition headers.")});
  } else if (inspection_session_.metadata().acquisition_count == 0U) {
    rows.append({tr("Status"), tr("The active container has no standard acquisitions.")});
  } else {
    ksj::ismrmrd::AcquisitionHeader header;
    std::string error;
    const auto ordinal = static_cast<std::uint32_t>(acquisition_ordinal_->value());
    if (!inspection_session_.reader().read_acquisition_header(ordinal, header, error)) {
      rows.append({tr("Status"), tr("Cannot read header: %1").arg(QString::fromUtf8(error))});
    } else {
      rows.append({tr("Storage ordinal"), QString::number(ordinal)});
      rows.append(
        {tr("Flags"), QStringLiteral("0x%1").arg(QString::number(static_cast<qulonglong>(header.flags), 16))});
      rows.append({tr("Encoding space ref"), QString::number(header.encoding_space_ref)});
      rows.append({tr("Samples"), QString::number(header.number_of_samples)});
      rows.append({tr("Active channels"), QString::number(header.active_channels)});
      rows.append({tr("Trajectory dimensions"), QString::number(header.trajectory_dimensions)});
      rows.append({tr("k-space encode step 1"), QString::number(header.index.kspace_encode_step_1)});
      rows.append({tr("k-space encode step 2"), QString::number(header.index.kspace_encode_step_2)});
      rows.append({tr("Slice / contrast / phase"),
                   tr("%1 / %2 / %3").arg(header.index.slice).arg(header.index.contrast).arg(header.index.phase)});
      rows.append({tr("Repetition / set"), tr("%1 / %2").arg(header.index.repetition).arg(header.index.set)});
    }
  }
  set_table_contents(acquisition_header_table_, columns, rows);
}

void ViewerWindow::refresh_kspace_controls() {
  if (kspace_coil_ == nullptr) {
    return;
  }

  const auto previously_selected_coil = kspace_coil_->currentIndex() >= 0 ? kspace_coil_->currentData().toInt() : -1;
  const QSignalBlocker coil_blocker(kspace_coil_);
  kspace_coil_->clear();
  if (!inspection_session_.is_open() || inspection_session_.metadata().acquisition_count == 0U) {
    kspace_coil_->addItem(tr("RSS (all active coils)"), -1);
    update_control_state();
    return;
  }

  ksj::ismrmrd::AcquisitionHeader header;
  std::string error;
  const auto ordinal = static_cast<std::uint32_t>(acquisition_ordinal_->value());
  if (!inspection_session_.reader().read_acquisition_header(ordinal, header, error)) {
    kspace_coil_->addItem(tr("RSS (header unavailable)"), -1);
    update_control_state();
    return;
  }

  kspace_coil_->addItem(tr("RSS (all %1 active coils)").arg(header.active_channels), -1);
  for (std::uint16_t channel = 0U; channel < header.active_channels; ++channel) {
    kspace_coil_->addItem(tr("Coil %1").arg(channel), static_cast<int>(channel));
  }
  const auto restored_index = kspace_coil_->findData(previously_selected_coil);
  kspace_coil_->setCurrentIndex(restored_index >= 0 ? restored_index : 0);
  update_control_state();
}

void ViewerWindow::refresh_kspace() {
  set_display_image(kspace_image_, kspace_presentation_.image,
                    tr("Select a reference acquisition and render a Cartesian k-space plane."));
  if (kspace_presentation_.details.isEmpty()) {
    kspace_summary_->setPlainText(inspection_session_.is_open()
                                    ? tr("Choose a reference acquisition, select RSS or one coil, then render a raw "
                                         "Cartesian k-space plane. Non-Cartesian trajectories are not shown as grids.")
                                    : tr("Open a standard ISMRMRD source to render raw Cartesian k-space."));
  } else {
    kspace_summary_->setPlainText(kspace_presentation_.summary);
  }
  refresh_acquisition_header_table();
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

void ViewerWindow::update_image_pixel_probe(const QPoint& position) {
  if (image_pixel_probe_ == nullptr || image_image_ == nullptr || image_presentation_.image.isNull()) {
    return;
  }
  const auto pixmap = image_image_->pixmap(Qt::ReturnByValue);
  if (pixmap.isNull() || pixmap.width() <= 0 || pixmap.height() <= 0) {
    return;
  }
  const auto contents = image_image_->contentsRect();
  const auto origin_x = contents.x() + std::max(0, (contents.width() - pixmap.width()) / 2);
  const auto origin_y = contents.y() + std::max(0, (contents.height() - pixmap.height()) / 2);
  const auto local_x = position.x() - origin_x;
  const auto local_y = position.y() - origin_y;
  if (local_x < 0 || local_y < 0 || local_x >= pixmap.width() || local_y >= pixmap.height()) {
    image_pixel_probe_->setText(tr("Pointer: outside the current display derivative."));
    return;
  }
  const auto source_x = std::clamp((local_x * image_presentation_.image.width()) / pixmap.width(), 0,
                                   image_presentation_.image.width() - 1);
  const auto source_y = std::clamp((local_y * image_presentation_.image.height()) / pixmap.height(), 0,
                                   image_presentation_.image.height() - 1);
  const auto intensity = image_presentation_.image.pixelColor(source_x, source_y).red();
  image_pixel_probe_->setText(
    tr("Display derivative pixel: x=%1, y=%2, intensity=%3").arg(source_x).arg(source_y).arg(intensity));
}

void ViewerWindow::refresh_image() {
  const auto zoom_percent = image_zoom_percent_ == nullptr ? 100 : image_zoom_percent_->value();
  set_display_image(image_image_, image_presentation_.image,
                    tr("Open a dataset, select a series, and choose an image plane."), zoom_percent);
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
  pipeline_stack_->setCurrentIndex(pipeline_presentation_.details.isEmpty() ? 0 : 1);
  update_source_context();
  update_export_availability();
}

void ViewerWindow::update_image_controls() {
  const QSignalBlocker ordinal_blocker(image_ordinal_);
  const QSignalBlocker z_blocker(image_z_);
  const QSignalBlocker channel_blocker(image_channel_);
  image_ordinal_->setRange(0, 0);
  image_z_->setRange(0, 0);
  image_channel_->setRange(0, 0);
  if (!inspection_session_.is_open() || image_series_->currentText().isEmpty()) {
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
    image_z_->setRange(0, std::max(0, static_cast<int>(record.header.matrix_size[2]) - 1));
    image_channel_->setRange(0, std::max(0, static_cast<int>(record.header.channels) - 1));
  }
  update_control_state();
}

void ViewerWindow::update_source_context() {
  if (source_file_bar_ != nullptr && inspection_session_.is_open()) {
    const QSignalBlocker blocker(source_file_bar_);
    source_file_bar_->setEditText(inspection_session_.source_path());
  }
  update_object_inspector(dataset_navigation_ == nullptr ? nullptr : dataset_navigation_->currentItem());
  update_selection_actions();
}

void ViewerWindow::update_control_state() {
  const auto dataset_is_open = inspection_session_.is_open();
  const auto has_acquisitions = dataset_is_open && inspection_session_.metadata().acquisition_count > 0U;
  const auto has_images = dataset_is_open && image_series_->count() > 0;
  if (tabs_ != nullptr) {
    tabs_->setTabVisible(static_cast<int>(WorkspaceView::image), has_images);
    if (!has_images && tabs_->currentIndex() == static_cast<int>(WorkspaceView::image)) {
      tabs_->setCurrentIndex(static_cast<int>(WorkspaceView::metadata));
    }
  }
  acquisition_ordinal_->setEnabled(has_acquisitions);
  kspace_coil_->setEnabled(has_acquisitions);
  render_kspace_button_->setEnabled(has_acquisitions && kspace_coil_->count() > 0);
  image_series_->setEnabled(has_images);
  image_ordinal_->setEnabled(has_images);
  image_z_->setEnabled(has_images);
  image_channel_->setEnabled(has_images);
  image_auto_window_->setEnabled(has_images);
  image_zoom_percent_->setEnabled(has_images);
  const auto manual_window = has_images && !image_auto_window_->isChecked();
  image_window_center_->setEnabled(manual_window);
  image_window_width_->setEnabled(manual_window);
  image_cine_button_->setEnabled(has_images && image_ordinal_->maximum() > 0);
  if (!has_images || image_ordinal_->maximum() <= 0) {
    stop_image_cine();
  }
  load_image_button_->setEnabled(has_images);
}

void ViewerWindow::update_export_availability() {
  bool can_export = false;
  if (tabs_ != nullptr && typed_data_surface_ != nullptr && !typed_data_surface_->isHidden()) {
    switch (tabs_->currentIndex()) {
      case 0:
        can_export = metadata_view_open_ && inspection_session_.is_open();
        break;
      case 1:
        can_export = !kspace_presentation_.details.isEmpty();
        break;
      case 2:
        can_export = !image_presentation_.details.isEmpty();
        break;
      case 3:
        can_export = !pipeline_presentation_.details.isEmpty();
        break;
      default:
        break;
    }
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

bool ViewerWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == image_image_ && event != nullptr &&
      (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress)) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    update_image_pixel_probe(mouse_event->position().toPoint());
  }
  return QMainWindow::eventFilter(watched, event);
}

bool ViewerWindow::current_derivative(VisualizationDerivative& derivative, QString& error) const {
  derivative = {};
  error.clear();

  if (tabs_ == nullptr || typed_data_surface_ == nullptr || typed_data_surface_->isHidden()) {
    error = tr("Explicitly inspect a standard object before exporting its visualization derivative.");
    return false;
  }

  const auto active_tab = tabs_->currentIndex();
  if (active_tab == 0) {
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
  if (active_tab == 1) {
    if (kspace_presentation_.details.isEmpty()) {
      error = tr("Render a Cartesian k-space plane before exporting its display derivative.");
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
  if (active_tab == 2) {
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
  if (active_tab == 3) {
    if (pipeline_presentation_.details.isEmpty()) {
      error = tr("Open a PipelineDefinition before exporting its display derivative.");
      return false;
    }
    derivative.view_name = QStringLiteral("pipeline");
    derivative.source_description = pipeline_presentation_.details.value(QStringLiteral("source")).toString();
    derivative.csv_columns = pipeline_presentation_.csv_columns;
    derivative.csv_rows = pipeline_presentation_.csv_rows;
    derivative.details = pipeline_presentation_.details;
    return true;
  }

  error = tr("The current viewer tab cannot be exported.");
  return false;
}

} // namespace ksj::viewer
