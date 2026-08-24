find_package(Qt6 6.8 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

set(KSJ_VIEWER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/apps/kspacejet-viewer/src")

# Keep the focused presentation test below the application tree without turning its app-local implementation into a
# library target. The production executable is intentionally excluded from the dedicated unit-test tree.
ksj_add_gtest(
  ksj_viewer_presentation_tests
  MODULE
  apps.kspacejet-viewer
  SOURCES
  viewer_presentation_tests.cpp
  "${KSJ_VIEWER_SOURCE_DIR}/inspection_session.cpp"
  "${KSJ_VIEWER_SOURCE_DIR}/viewer_presentation.cpp"
  "${KSJ_VIEWER_SOURCE_DIR}/visualization_derivative_export.cpp"
  "${KSJ_VIEWER_SOURCE_DIR}/viewer_theme.cpp"
  "${KSJ_VIEWER_SOURCE_DIR}/viewer_theme.hpp"
  "${KSJ_VIEWER_SOURCE_DIR}/viewer_window.cpp"
  "${KSJ_VIEWER_SOURCE_DIR}/viewer_window.hpp"
  LINK_LIBS
  KSpaceJet::ismrmrd
  KSpaceJet::recon_graph
  ISMRMRD::ISMRMRD
  hdf5::hdf5
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  INCLUDE_DIRS
  "${KSJ_VIEWER_SOURCE_DIR}"
  LABELS
  apps
  kspacejet-viewer
  qt)

# QApplication loads the Windows platform plugin dynamically; deploy it next to the focused test just as the production
# viewer deploys it next to the app.
ksj_deploy_qt_widgets_runtime(ksj_viewer_presentation_tests)
