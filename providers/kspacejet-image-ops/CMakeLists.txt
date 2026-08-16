ksj_include_project_bootstrap()

set(PROJ_NAME ksj_image_ops_provider)

# This plugin is deliberately a second Provider boundary rather than a helper linked into the Cartesian Provider. It
# groups small, stateless float32 image transforms with the same exact image ABI and synchronous lifecycle. Each
# Operator has its own implementation file; provider_entry.cpp is the only exported ABI entrypoint.
add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/src/provider_entry.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/provider_api.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/support/image_transform_config.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/operators/image_scale_float32.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/operators/image_offset_float32.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/operators/image_clamp_float32.cpp")

target_compile_features(${PROJ_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${PROJ_NAME} PRIVATE KSJ_PROVIDER_BUILDING_PLUGIN=1)
target_include_directories(${PROJ_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
target_link_libraries(${PROJ_NAME} PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options
                                           KSpaceJet::core KSpaceJet::provider_sdk)

set_target_properties(
  ${PROJ_NAME}
  PROPERTIES OUTPUT_NAME "ksj-image-ops"
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
    FILES "${CMAKE_CURRENT_LIST_DIR}/contracts/image_scale_float32.json"
          "${CMAKE_CURRENT_LIST_DIR}/contracts/image_offset_float32.json"
          "${CMAKE_CURRENT_LIST_DIR}/contracts/image_clamp_float32.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/kspacejet/providers/kspacejet-image-ops/contracts")
endif()
