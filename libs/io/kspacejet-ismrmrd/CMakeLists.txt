ksj_include_project_bootstrap()

find_package(ISMRMRD CONFIG REQUIRED)

set(PROJ_NAME ksj_ismrmrd)

add_library(${PROJ_NAME} SHARED "${CMAKE_CURRENT_LIST_DIR}/src/dataset_reader.cpp")
add_library(KSpaceJet::ismrmrd ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJ_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options ISMRMRD::ISMRMRD)

set_target_properties(
  ${PROJ_NAME}
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET default
             VISIBILITY_INLINES_HIDDEN OFF
             WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(UNIX AND NOT APPLE)
  target_link_options(${PROJ_NAME} PRIVATE "LINKER:-z,defs")
endif()

ksj_set_library_runtime_path(${PROJ_NAME})
ksj_install_target(${PROJ_NAME})
