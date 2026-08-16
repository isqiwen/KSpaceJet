ksj_add_gtest(
  ksj_recon_model_tests
  MODULE
  recon.kspacejet-recon-model
  SOURCES
  artifact_json_tests.cpp
  model_tests.cpp
  run_record_tests.cpp
  type_resource_tests.cpp
  LINK_LIBS
  KSpaceJet::recon_model
  LABELS
  recon
  model)

find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)

project_kit_add_test(
  NAME
  recon.type_registry_validation
  COMMAND
  "${Python3_EXECUTABLE}"
  "${CMAKE_SOURCE_DIR}/tools/type_registry/generate.py"
  --project-root
  "${CMAKE_SOURCE_DIR}"
  --check
  LABELS
  unit)
