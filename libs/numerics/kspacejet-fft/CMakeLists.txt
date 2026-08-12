ksj_include_project_bootstrap()

find_package(Intel CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)

set(PROJ_NAME ksj_fft)

add_library(ksj_fft_constants INTERFACE)
add_library(KSpaceJet::fft_constants ALIAS ksj_fft_constants)
target_compile_features(ksj_fft_constants INTERFACE cxx_std_20)
target_include_directories(ksj_fft_constants INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/src/eigen/eigen_fft_transforms.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_fft_descriptors.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_fft_transforms.cpp")
add_library(KSpaceJet::fft ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options
          KSpaceJet::shared_library_link_options
          Eigen3::Eigen
          Intel::headers
          Intel::mkl_rt
          Intel::ipps
          Intel::ippcore
  PUBLIC KSpaceJet::fft_constants KSpaceJet::array)
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
