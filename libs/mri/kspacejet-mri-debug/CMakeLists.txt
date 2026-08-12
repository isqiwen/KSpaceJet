ksj_include_project_bootstrap()

set(PROJ_NAME ksj_mri_debug)

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/mri/debug/array_analysis.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/mri/debug/array_dump.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/mri/debug/cfl_dump.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/mri/debug/debug.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/mri/debug/debug_event.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/array_dump.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/debug_event.cpp")
add_library(KSpaceJet::mri_debug ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options KSpaceJet::matio_headers matio::matio
  PUBLIC KSpaceJet::core KSpaceJet::array $<LINK_ONLY:matio::matio>)
target_include_directories(${PROJ_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

set_target_properties(
  ${PROJ_NAME}
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET default
             VISIBILITY_INLINES_HIDDEN OFF
             WINDOWS_EXPORT_ALL_SYMBOLS ON)
ksj_set_library_runtime_path(${PROJ_NAME})
ksj_install_target(${PROJ_NAME})
