ksj_include_project_bootstrap()

# ISMRMRD is deliberately private to this library.  Public reconstruction model values are KSpaceJet types so Provider
# authors and downstream runtime code never need to include an ISMRMRD vendor header.
find_package(ISMRMRD CONFIG REQUIRED)

set(PROJ_NAME ksj_recon_model)

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/artifact_json.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/model.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/include/kspacejet/recon/node_planning_requirements.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/artifact_json.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/model.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/run_record.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/resource_vector.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/type_descriptor.cpp")
add_library(KSpaceJet::recon_model ALIAS ${PROJ_NAME})
add_dependencies(${PROJ_NAME} ksj_type_registry_check)

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJ_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options ISMRMRD::ISMRMRD
          nlohmann_json::nlohmann_json
  PUBLIC KSpaceJet::core
         # The C++ headers remain vendor-free, but ELF consumers must retain ISMRMRD's transitive shared-library closure
         # (HDF5/pugixml).  LINK_ONLY propagates that link requirement without publishing vendor include directories as
         # part of the reconstruction-model API.
         "$<LINK_ONLY:ISMRMRD::ISMRMRD>")

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

# The reconstruction model is a public C++ API. Keep the installed include tree in lockstep with the shared library,
# including the public JSON artifact API.
if(KSJ_ENABLE_INSTALL_RULES)
  install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
