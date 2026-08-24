ksj_add_gtest(
  ksj_recon_model_tests
  MODULE
  recon.kspacejet-recon-model
  SOURCES
  artifact_json_tests.cpp
  model_tests.cpp
  planning_input_artifacts_tests.cpp
  run_record_tests.cpp
  scan_facts_tests.cpp
  type_resource_tests.cpp
  LINK_LIBS
  KSpaceJet::recon_model
  LABELS
  recon
  model)

# Keep the checked-in artifact fixtures visible to both structural and semantic ScanFacts tests without copying MRI
# payload data into this tree.
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../fixtures" KSJ_RECON_FIXTURE_DIR)
target_compile_definitions(ksj_recon_model_tests PRIVATE KSJ_RECON_FIXTURE_DIR="${KSJ_RECON_FIXTURE_DIR}")

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
