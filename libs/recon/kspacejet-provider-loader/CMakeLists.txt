ksj_include_project_bootstrap()

set(PROJ_NAME ksj_provider_loader)

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/provider/loader/provider_loader.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/provider_loader.cpp")
add_library(KSpaceJet::provider_loader ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJ_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options
  PUBLIC KSpaceJet::core KSpaceJet::provider_sdk)

set_target_properties(
  ${PROJ_NAME}
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET default
             VISIBILITY_INLINES_HIDDEN OFF
             WINDOWS_EXPORT_ALL_SYMBOLS ON)

ksj_set_library_runtime_path(${PROJ_NAME})
ksj_install_target(${PROJ_NAME})

if(KSJ_ENABLE_INSTALL_RULES)
  install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
