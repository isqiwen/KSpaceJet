ksj_include_project_bootstrap()

set(PROJ_NAME ksj_nufft)

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/src/bart/bart_nufft2.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_nufft_direct_nudft.cpp" "${CMAKE_CURRENT_LIST_DIR}/src/nufft.cpp")
add_library(KSpaceJet::nufft ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options
  PUBLIC KSpaceJet::array KSpaceJet::numerics_constants)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(BART CONFIG QUIET)
endif()
if(TARGET BART::bart)
  target_compile_definitions(${PROJ_NAME} PRIVATE KSJ_NUFFT_HAS_BART=1)
  target_link_libraries(${PROJ_NAME} PRIVATE BART::bart)
endif()
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
