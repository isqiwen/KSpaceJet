#pragma once

#include "inspection_session.hpp"
#include "viewer_presentation.hpp"
#include "visualization_derivative_export.hpp"

#include <QMainWindow>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QAction;
class QEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPoint;
class QScrollArea;
class QSplitter;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QToolButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace ksj::viewer {

class ViewerWindow final : public QMainWindow {
public:
  ViewerWindow();

  // App-local entry point used by the file action and widget tests. It opens
  // only a standard ISMRMRD source through InspectionSession; no generic HDF5
  // traversal or source-file mutation is exposed here.
  [[nodiscard]] bool open_mrd_source(const QString& file_path, QString& error);

  // App-local entry point used by the file action and widget tests. It parses
  // a local PipelineDefinition document only; it does not resolve, compile,
  // load, or execute a Provider.
  [[nodiscard]] bool open_pipeline_source(const QString& file_path, QString& error);

private:
  enum class WorkspaceView : int {
    metadata = 0,
    kspace = 1,
    image = 2,
    pipeline = 3,
  };

  void create_actions();
  void create_workbench();
  void create_metadata_page();
  void create_kspace_page();
  void create_image_page();
  void create_pipeline_page();

  void open_mrd();
  void close_mrd_source();
  void open_pipeline();
  void load_kspace();
  void load_image();
  void toggle_image_cine();
  void stop_image_cine();
  void export_current(VisualizationExportFormat format);
  void inspect_selected_object();
  void open_selected_object_as();
  void copy_selected_object_path();
  void show_object_context_menu(const QPoint& position);
  void activate_navigation_item(QTreeWidgetItem* item, WorkspaceView view);
  void rebuild_dataset_navigation();
  void clear_dataset_derivatives();
  void set_workspace_view(WorkspaceView view);
  void activate_workspace_view(WorkspaceView view);
  void update_workspace_tab_visibility(QTreeWidgetItem* item);
  [[nodiscard]] QWidget* workspace_page(WorkspaceView view) const;

  [[nodiscard]] bool activate_navigation_container(QTreeWidgetItem* item, QString& error);
  void update_object_inspector(QTreeWidgetItem* item);
  void update_selection_actions();
  void refresh_acquisition_header_table();
  void refresh_kspace_controls();
  void refresh_image_histogram();
  void update_image_pixel_probe(const QPoint& position);
  void refresh_metadata();
  void refresh_kspace();
  void refresh_image();
  void refresh_pipeline();
  void update_image_controls();
  void update_source_context();
  void update_control_state();
  void update_export_availability();
  void append_info(const QString& message);
  [[nodiscard]] bool current_derivative(VisualizationDerivative& derivative, QString& error) const;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

  InspectionSession inspection_session_;
  MetadataPresentation metadata_presentation_;
  KspacePresentation kspace_presentation_;
  ImagePresentation image_presentation_;
  PipelinePresentation pipeline_presentation_;

  QAction* open_mrd_action_ = nullptr;
  QAction* close_mrd_action_ = nullptr;
  QAction* open_pipeline_action_ = nullptr;
  QAction* inspect_object_action_ = nullptr;
  QAction* open_as_action_ = nullptr;
  QAction* copy_object_path_action_ = nullptr;
  QAction* export_png_action_ = nullptr;
  QAction* export_svg_action_ = nullptr;
  QAction* export_csv_action_ = nullptr;
  QAction* export_json_action_ = nullptr;
  QTabWidget* object_inspector_ = nullptr;
  QWidget* kspace_page_ = nullptr;
  QWidget* metadata_page_ = nullptr;
  QWidget* image_page_ = nullptr;
  QWidget* pipeline_page_ = nullptr;
  QToolButton* export_button_ = nullptr;
  QToolButton* recent_sources_button_ = nullptr;
  QToolButton* clear_file_bar_button_ = nullptr;
  QComboBox* source_file_bar_ = nullptr;
  QTreeWidget* dataset_navigation_ = nullptr;
  QScrollArea* object_general_ = nullptr;
  QLineEdit* object_name_field_ = nullptr;
  QLineEdit* object_path_field_ = nullptr;
  QLineEdit* object_type_field_ = nullptr;
  QLineEdit* object_access_field_ = nullptr;
  QTableWidget* object_semantics_table_ = nullptr;
  QTableWidget* object_members_table_ = nullptr;
  QLabel* object_attributes_status_ = nullptr;
  QTableWidget* object_attributes_ = nullptr;
  QPlainTextEdit* info_panel_ = nullptr;
  QStackedWidget* metadata_stack_ = nullptr;
  QPlainTextEdit* metadata_xml_ = nullptr;
  bool metadata_view_open_ = false;
  QSpinBox* acquisition_ordinal_ = nullptr;
  QComboBox* kspace_coil_ = nullptr;
  QToolButton* render_kspace_button_ = nullptr;
  QLabel* kspace_image_ = nullptr;
  QPlainTextEdit* kspace_summary_ = nullptr;
  QTableWidget* acquisition_header_table_ = nullptr;
  QComboBox* image_series_ = nullptr;
  QSpinBox* image_ordinal_ = nullptr;
  QSpinBox* image_z_ = nullptr;
  QSpinBox* image_channel_ = nullptr;
  QSpinBox* image_zoom_percent_ = nullptr;
  QCheckBox* image_auto_window_ = nullptr;
  QDoubleSpinBox* image_window_center_ = nullptr;
  QDoubleSpinBox* image_window_width_ = nullptr;
  QToolButton* image_cine_button_ = nullptr;
  QTimer* image_cine_timer_ = nullptr;
  QToolButton* load_image_button_ = nullptr;
  QScrollArea* image_scroll_area_ = nullptr;
  QLabel* image_image_ = nullptr;
  QLabel* image_pixel_probe_ = nullptr;
  QTableWidget* image_histogram_ = nullptr;
  QPlainTextEdit* image_summary_ = nullptr;
  QStackedWidget* pipeline_stack_ = nullptr;
  QPlainTextEdit* pipeline_summary_ = nullptr;
  QPlainTextEdit* pipeline_canonical_json_ = nullptr;
};

} // namespace ksj::viewer
