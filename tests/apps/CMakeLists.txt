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
  LABELS
  app
  cli
  protocol)

add_subdirectory(kspacejet-cli)
