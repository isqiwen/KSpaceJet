ksj_include_project_bootstrap()

set(PROJ_NAME ksj_recon_graph)

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/graph/artifact_json.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/graph/canonical_json.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/graph/execution_plan_compiler.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/graph/pipeline_definition.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/artifact_json.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/canonical_json.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/execution_plan_compiler.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/execution_plan_verifier.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/pipeline_definition.cpp")
add_library(KSpaceJet::recon_graph ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJ_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options nlohmann_json::nlohmann_json
  PUBLIC KSpaceJet::core KSpaceJet::recon_contracts)

set_target_properties(
  ${PROJ_NAME}
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET default
             VISIBILITY_INLINES_HIDDEN OFF
             WINDOWS_EXPORT_ALL_SYMBOLS ON)

ksj_set_library_runtime_path(${PROJ_NAME})
ksj_install_target(${PROJ_NAME})
