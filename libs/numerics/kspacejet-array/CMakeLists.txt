ksj_include_project_bootstrap()

find_package(Intel CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)

set(PROJ_NAME ksj_array)

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_array_complex.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_array_elementwise.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_array_reductions.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_array_storage.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_array_complex.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_array_elementwise.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_array_reductions.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_array_storage.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_array_vml.cpp")
add_library(KSpaceJet::array ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
if(KSJ_ENABLE_NATIVE_ARCH_OPTIMIZATION)
  target_compile_options(${PROJ_NAME} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-march=native>")
endif()
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options
          KSpaceJet::shared_library_link_options
          KSpaceJet::base_abi_definitions
          Intel::headers
          Intel::ipps
          Intel::ippvm
          Intel::ippcore
          Intel::mkl_rt
          Eigen3::Eigen
  PUBLIC KSpaceJet::core)
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
