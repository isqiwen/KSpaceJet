project_kit_add_test(
  NAME
  apps.logging_contract
  COMMAND
  "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_LIST_DIR}/application_logging_contract_tests.py"
  LABELS
  app
  logging
  contract)

project_kit_add_test(
  NAME
  apps.viewer_windows_gui_contract
  COMMAND
  "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_LIST_DIR}/viewer_windows_gui_contract_tests.py"
  LABELS
  app
  platform
  viewer
  windows
  contract)

project_kit_add_test(
  NAME
  apps.json_cli_protocol
  COMMAND
  "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_LIST_DIR}/application_json_protocol_tests.py"
  "$<TARGET_FILE:ksj_cli>"
  "$<TARGET_FILE:ksj_gateway>"
  "$<TARGET_FILE:ksj_recon>"
  "$<TARGET_FILE:ksj_research>"
  "$<TARGET_FILE:ksj_viewer>"
  LABELS
  app
  cli
  protocol)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # This must launch the build-tree Viewer without conanrun.sh. It proves the staged ELF closure, qt.conf and offscreen
  # platform plugin are sufficient on a headless host.
  project_kit_add_test(
    NAME
    apps.viewer_runtime_smoke
    TARGET
    ksj_viewer
    ARGS
    --ui-smoke
    --format
    json
    ENVIRONMENT
    "LD_LIBRARY_PATH="
    "QT_PLUGIN_PATH="
    "ICU_DATA="
    "QT_QPA_PLATFORM=offscreen"
    LABELS
    app
    runtime
    qt
    viewer)
endif()

add_subdirectory(kspacejet-cli)
