ksj_include_project_bootstrap()

find_package(Intel CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)

set(PROJ_NAME ksj_signal)

add_library(
  ${PROJ_NAME} SHARED
  "${CMAKE_CURRENT_LIST_DIR}/src/fft/fft_signal_convolution.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/fft/fft_signal_phase.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_signal_convolution.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_signal_filters.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_signal_vml_windows.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/intel/intel_signal_windows.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/opencv/opencv_signal_convolution.cpp")
add_library(KSpaceJet::signal ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} PUBLIC cxx_std_20)
target_link_libraries(
  ${PROJ_NAME}
  PRIVATE KSpaceJet::compile_options
          KSpaceJet::shared_library_link_options
          KSpaceJet::base_abi_definitions
          Intel::headers
          Intel::ippi
          Intel::ipps
          Intel::ippcore
          opencv_core
          opencv_imgproc
          KSpaceJet::special
  PUBLIC KSpaceJet::array KSpaceJet::fft)
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
