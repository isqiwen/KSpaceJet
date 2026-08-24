find_package(Qt6 6.8 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

ksj_add_application(
  TARGET
  ksj_viewer
  OUTPUT_NAME
  ksj-viewer
  SOURCES
  main.cpp
  src/inspection_session.cpp
  src/inspection_session.hpp
  src/viewer_presentation.cpp
  src/viewer_presentation.hpp
  src/viewer_theme.cpp
  src/viewer_theme.hpp
  src/visualization_derivative_export.cpp
  src/visualization_derivative_export.hpp
  src/viewer_window.cpp
  src/viewer_window.hpp
  INSTALL)

# The desktop process is an inspection client only. It intentionally does not link the reconstruction runtime, Provider
# loader/modules, gateway, research executable, or MRI debug implementation.
target_link_libraries(ksj_viewer PRIVATE KSpaceJet::ismrmrd KSpaceJet::recon_graph Qt6::Core Qt6::Gui Qt6::Widgets)

ksj_deploy_qt_widgets_runtime(ksj_viewer)
