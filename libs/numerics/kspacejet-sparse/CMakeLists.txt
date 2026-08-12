ksj_include_project_bootstrap()

find_package(Eigen3 CONFIG REQUIRED)
find_package(Intel CONFIG REQUIRED)

set(PROJ_NAME ksj_sparse)

add_library(${PROJ_NAME} SHARED "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_sparse_operations.cpp"
                                "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_sparse_operations.cpp")
add_library(KSpaceJet::sparse ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options KSpaceJet::shared_library_link_options KSpaceJet::base_abi_definitions Eigen3::Eigen
          Intel::headers Intel::mkl_rt
  PUBLIC KSpaceJet::array)
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
