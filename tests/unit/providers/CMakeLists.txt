add_subdirectory(kspacejet-kspace-conditioning)
add_subdirectory(kspacejet-cartesian-recon)
add_subdirectory(kspacejet-coil-combine)
add_subdirectory(kspacejet-noncartesian-recon)
add_subdirectory(kspacejet-image-ops)
add_subdirectory(kspacejet-calibration)

find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)

project_kit_add_test(
  NAME
  providers.provider_catalog_validation
  COMMAND
  "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_LIST_DIR}/provider_catalog_validation.py"
  --project-root
  "${CMAKE_SOURCE_DIR}"
  LABELS
  unit)

project_kit_add_test(
  NAME
  providers.provider_catalog_validation_tests
  COMMAND
  "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_LIST_DIR}/provider_catalog_validation_tests.py"
  LABELS
  unit)

project_kit_add_test(
  NAME
  providers.provider_bundle_identity_validation
  COMMAND
  "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_LIST_DIR}/provider_bundle_identity_validation.py"
  --project-root
  "${CMAKE_SOURCE_DIR}"
  LABELS
  unit)
