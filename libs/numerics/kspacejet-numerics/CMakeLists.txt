ksj_include_project_bootstrap()

find_package(Intel CONFIG REQUIRED)

set(PROJ_NAME ksj_numerics)

add_library(ksj_numerics_constants INTERFACE)
add_library(KSpaceJet::numerics_constants ALIAS ksj_numerics_constants)
target_compile_features(ksj_numerics_constants INTERFACE cxx_std_20)
target_include_directories(ksj_numerics_constants INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")

add_library(ksj_numerics_runtime SHARED "${CMAKE_CURRENT_LIST_DIR}/src/runtime.cpp")
add_library(KSpaceJet::numerics_runtime ALIAS ksj_numerics_runtime)

target_compile_features(ksj_numerics_runtime PUBLIC cxx_std_20)
target_link_libraries(
  ksj_numerics_runtime
  PRIVATE KSpaceJet::compile_options
          KSpaceJet::shared_library_link_options
          KSpaceJet::base_abi_definitions
          KSpaceJet::intel_compat_definitions
          Intel::headers
          Intel::ippi
          Intel::ipps
          Intel::ippcore
          Intel::ippcore_tl_omp
          Intel::iomp5
          Intel::mkl_rt)
target_include_directories(ksj_numerics_runtime PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
set_target_properties(
  ksj_numerics_runtime
  PROPERTIES CXX_EXTENSIONS NO
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET default
             VISIBILITY_INLINES_HIDDEN OFF
             WINDOWS_EXPORT_ALL_SYMBOLS ON)
ksj_set_library_runtime_path(ksj_numerics_runtime)
ksj_install_target(ksj_numerics_runtime)

add_library(${PROJ_NAME} INTERFACE)
add_library(KSpaceJet::numerics ALIAS ${PROJ_NAME})

target_compile_features(${PROJ_NAME} INTERFACE cxx_std_20)
target_link_libraries(
  ${PROJ_NAME}
  INTERFACE KSpaceJet::numerics_constants
            KSpaceJet::numerics_runtime
            KSpaceJet::array
            KSpaceJet::linalg
            KSpaceJet::fft
            KSpaceJet::signal
            KSpaceJet::image
            KSpaceJet::stats
            KSpaceJet::optimization
            KSpaceJet::special
            KSpaceJet::sparse)
target_include_directories(${PROJ_NAME} INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
