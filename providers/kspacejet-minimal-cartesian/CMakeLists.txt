ksj_include_project_bootstrap()

set(PROJ_NAME ksj_minimal_cartesian_provider)

# This plugin intentionally keeps its reconstruction kernel inside the
# Provider ABI boundary.  It does not link ksj-recon or any host runtime
# implementation: the only data access is through ABI v1 firing leases,
# output grants, and caller-provided scratch.
add_library(${PROJ_NAME} SHARED "${CMAKE_CURRENT_LIST_DIR}/src/minimal_cartesian_provider.cpp")

target_compile_features(${PROJ_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${PROJ_NAME} PRIVATE KSJ_PROVIDER_BUILDING_PLUGIN=1)
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options KSpaceJet::provider_sdk)

set_target_properties(
  ${PROJ_NAME}
  PROPERTIES OUTPUT_NAME "ksj-minimal-cartesian"
             CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
             WINDOWS_EXPORT_ALL_SYMBOLS ON
             FOLDER "providers")

ksj_set_library_runtime_path(${PROJ_NAME})

if(KSJ_ENABLE_INSTALL_RULES)
  project_kit_install_targets(
    TARGETS
    ${PROJ_NAME}
    RUNTIME_DEPENDENCY_SET
    ${KSJ_RUNTIME_DEPENDENCY_SET}
    RUNTIME_DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/kspacejet/providers"
    LIBRARY_DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/kspacejet/providers"
    ARCHIVE_DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/kspacejet/providers")

  install(
    FILES "${CMAKE_CURRENT_LIST_DIR}/contracts/cartesian_ifft2_single_coil-v1.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/kspacejet/providers/kspacejet-minimal-cartesian/contracts")
endif()
