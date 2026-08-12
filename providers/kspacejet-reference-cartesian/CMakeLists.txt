ksj_include_project_bootstrap()

set(PROJ_NAME ksj_reference_cartesian_provider)

# This intentionally contains only a small open conformance/reference
# operator.  It links the versioned C ABI headers but no KSpaceJet C++ runtime,
# so the host and plugin remain separated by Provider ABI v1.
add_library(${PROJ_NAME} SHARED "${CMAKE_CURRENT_LIST_DIR}/src/reference_cartesian_provider.cpp")

target_compile_features(${PROJ_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${PROJ_NAME} PRIVATE KSJ_PROVIDER_BUILDING_PLUGIN=1)
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options KSpaceJet::provider_sdk)

set_target_properties(
  ${PROJ_NAME}
  PROPERTIES OUTPUT_NAME "ksj-reference-cartesian"
             CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
             WINDOWS_EXPORT_ALL_SYMBOLS ON
             FOLDER "providers")

ksj_set_library_runtime_path(${PROJ_NAME})

# A Provider is loaded by an explicit trusted path, rather than by the general
# library search path.  Put both Unix .so files and Windows .dll files beneath
# one well-known installation root.  The Windows archive is an import library,
# not a static Provider implementation.
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
endif()
