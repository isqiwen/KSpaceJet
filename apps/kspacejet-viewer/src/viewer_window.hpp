#pragma once

#include "arrshow_dimension_controls.hpp"
#include "inspection_session.hpp"
#include "viewer_presentation.hpp"
#include "visualization_derivative_export.hpp"

#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QString>

#include <array>
#include <optional>
#include <cstdint>

class QComboBox;
class QDoubleSpinBox;
class QAction;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPoint;
class QPointF;
class QScrollArea;
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

class InspectionCanvas;

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
  struct RecentFileEntry final {
    QString path;
    bool is_pipeline{false};
  };

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
  [[nodiscard]] QString initial_open_directory() const;
  void remember_successful_open_directory(const QString& file_path);
  void restore_recent_files();
  void persist_recent_files() const;
  void remember_recent_file(const QString& file_path, bool is_pipeline);
  void refresh_recent_files_menu();
  void open_recent_file(const QString& file_path, bool is_pipeline);
  void load_kspace(bool preserve_window_drag = false);
  void load_image(bool preserve_window_drag = false);
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
  void refresh_kspace_controls();
  void refresh_kspace_dimension_controls();
  void select_kspace_dimension_value(const QString& dimension_identifier, int value);
  void select_kspace_dimension_selection_tag(const QString& dimension_identifier,
                                             ArrShowDimensionSelectionTag selection_tag);
  void refresh_image_dimension_controls();
  void select_image_dimension_value(const QString& dimension_identifier, int value);
  void refresh_image_histogram();
  void update_image_pixel_probe(const QPoint& display_pixel);
  void update_kspace_pixel_probe(const QPoint& display_pixel);
  void step_kspace_plane(int step);
  void step_image_plane(int step);
  void adjust_image_window_from_drag(QPointF drag_delta, bool finished);
  void reset_image_window();
  void adjust_kspace_window_from_drag(QPointF drag_delta, bool finished);
  void reset_kspace_window();
  void apply_kspace_display_window();
  void sync_kspace_arrshow_controls();
  void sync_image_arrshow_controls();
  void update_kspace_arrshow_settings_from_controls();
  void update_image_arrshow_settings_from_controls();
  void clear_image_derivative_for_selection_change();
  void refresh_metadata();
  void refresh_metadata_content();
  void refresh_kspace();
  void refresh_image();
  void refresh_pipeline();
  void refresh_pipeline_graph();
  void update_image_controls();
  void update_source_context();
  void update_control_state();
  void update_export_availability();
  void append_info(const QString& message);
  [[nodiscard]] bool current_derivative(VisualizationDerivative& derivative, QString& error) const;
  [[nodiscard]] QImage pipeline_graph_image() const;

private:
  InspectionSession inspection_session_;
  MetadataPresentation metadata_presentation_;
  KspacePresentation kspace_presentation_;
  QString kspace_availability_error_;
  ImagePresentation image_presentation_;
  PipelinePresentation pipeline_presentation_;
  QString last_open_directory_;
  QList<RecentFileEntry> recent_files_;

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
  QToolButton* clear_file_bar_button_ = nullptr;
  QLineEdit* source_file_bar_ = nullptr;
  QMenu* recent_files_menu_ = nullptr;
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
  QLabel* metadata_xml_summary_ = nullptr;
  QTreeWidget* metadata_xml_outline_ = nullptr;
  QPlainTextEdit* metadata_xml_ = nullptr;
  bool metadata_view_open_ = false;
  QComboBox* kspace_acquisition_type_ = nullptr;
  ArrShowDimensionStrip* kspace_dimensions_ = nullptr;
  QComboBox* kspace_component_ = nullptr;
  QComboBox* kspace_phase_representation_ = nullptr;
  QComboBox* kspace_range_calculation_ = nullptr;
  QDoubleSpinBox* kspace_percentile_ = nullptr;
  QComboBox* kspace_window_persistence_ = nullptr;
  QDoubleSpinBox* kspace_window_center_ = nullptr;
  QDoubleSpinBox* kspace_window_width_ = nullptr;
  QSpinBox* kspace_zoom_percent_ = nullptr;
  QToolButton* kspace_reset_window_button_ = nullptr;
  QToolButton* kspace_reset_view_button_ = nullptr;
  InspectionCanvas* kspace_image_ = nullptr;
  QLabel* kspace_pixel_probe_ = nullptr;
  QComboBox* image_series_ = nullptr;
  QComboBox* image_component_ = nullptr;
  QComboBox* image_phase_representation_ = nullptr;
  QComboBox* image_range_calculation_ = nullptr;
  QDoubleSpinBox* image_percentile_ = nullptr;
  QSpinBox* image_ordinal_ = nullptr;
  ArrShowDimensionStrip* image_dimensions_ = nullptr;
  QSpinBox* image_zoom_percent_ = nullptr;
  QComboBox* image_window_persistence_ = nullptr;
  QDoubleSpinBox* image_window_center_ = nullptr;
  QDoubleSpinBox* image_window_width_ = nullptr;
  QToolButton* image_reset_window_button_ = nullptr;
  QToolButton* image_fit_button_ = nullptr;
  QToolButton* image_cine_button_ = nullptr;
  QTimer* image_cine_timer_ = nullptr;
  QToolButton* load_image_button_ = nullptr;
  InspectionCanvas* image_image_ = nullptr;
  QLabel* image_pixel_probe_ = nullptr;
  QTableWidget* image_histogram_ = nullptr;
  QPlainTextEdit* image_summary_ = nullptr;
  QStackedWidget* pipeline_stack_ = nullptr;
  QPlainTextEdit* pipeline_summary_ = nullptr;
  QGraphicsView* pipeline_graph_view_ = nullptr;
  QGraphicsScene* pipeline_graph_scene_ = nullptr;
  QPlainTextEdit* pipeline_canonical_json_ = nullptr;
  bool image_window_drag_active_ = false;
  double image_window_drag_center_ = 0.0;
  double image_window_drag_width_ = 1.0;
  bool kspace_window_drag_active_ = false;
  double kspace_window_drag_center_ = 0.0;
  double kspace_window_drag_width_ = 1.0;
  bool kspace_selector_update_active_ = false;
  bool image_selector_update_active_ = false;
  std::optional<CartesianKspaceCatalog> kspace_catalog_;
  std::optional<std::array<std::uint16_t, kImageDimensionCount>> image_source_dimensions_;
  ArrShowDimensionSelection selected_kspace_dimension_selection_{.first_identifier = QStringLiteral("readout"),
                                                                 .second_identifier = QStringLiteral("phase-encode")};
  CartesianKspaceAxes selected_kspace_axes_{};
  CartesianKspaceCoordinate selected_kspace_coordinate_{};
  ImageAxes selected_image_axes_{};
  ImageCoordinate selected_image_coordinate_{};
  ArrShowDisplaySettings kspace_display_settings_{};
  ArrShowDisplaySettings image_display_settings_{};
};

} // namespace ksj::viewer
