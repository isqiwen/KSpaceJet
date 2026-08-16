ksj_include_project_bootstrap()

set(PROJ_NAME ksj_cartesian_recon_provider)

# This plugin intentionally keeps its reconstruction kernel inside the Provider ABI boundary.  It does not link
# ksj-recon or any host runtime implementation: the only data access is through firing leases, output grants, and
# caller-provided scratch.
add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/src/provider_entry.cpp" "${CMAKE_CURRENT_LIST_DIR}/src/provider_api.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/operators/cartesian_ifft2_coil_images.cpp")

target_compile_features(${PROJ_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${PROJ_NAME} PRIVATE KSJ_PROVIDER_BUILDING_PLUGIN=1)
target_include_directories(${PROJ_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
target_link_libraries(${PROJ_NAME} PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options
                                           KSpaceJet::core KSpaceJet::provider_sdk KSpaceJet::fft)

set_target_properties(
  ${PROJ_NAME}
  PROPERTIES OUTPUT_NAME "ksj-cartesian-recon"
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

  install(FILES "${CMAKE_CURRENT_LIST_DIR}/contracts/cartesian_ifft2_coil_images.json"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/kspacejet/providers/kspacejet-cartesian-recon/contracts")
endif()
