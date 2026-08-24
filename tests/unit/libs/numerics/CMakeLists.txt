add_subdirectory(kspacejet-array)
add_subdirectory(kspacejet-linalg)
add_subdirectory(kspacejet-fft)
add_subdirectory(kspacejet-signal)
add_subdirectory(kspacejet-image)
add_subdirectory(kspacejet-stats)
add_subdirectory(kspacejet-optimization)
add_subdirectory(kspacejet-special)
add_subdirectory(kspacejet-sparse)
add_subdirectory(kspacejet-numerics)
add_subdirectory(kspacejet-nufft)

find_package(Eigen3 CONFIG REQUIRED)

set(KSJ_NUMERICS_TEST_MEMORY_BROKER_SOURCE "${CMAKE_CURRENT_LIST_DIR}/test_memory_broker.cpp")
foreach(
  ksj_numerics_test_target IN
  ITEMS ksj_array_eigen_tests
        ksj_linalg_tests
        ksj_fft_tests
        ksj_signal_tests
        ksj_image_tests
        ksj_stats_tests
        ksj_optimization_tests
        ksj_special_tests
        ksj_sparse_tests
        ksj_nufft_tests
        ksj_numerics_header_tests)
  if(TARGET ${ksj_numerics_test_target})
    target_sources(${ksj_numerics_test_target} PRIVATE "${KSJ_NUMERICS_TEST_MEMORY_BROKER_SOURCE}")
    target_link_libraries(${ksj_numerics_test_target} PRIVATE Eigen3::Eigen KSpaceJet::core KSpaceJet::numerics_runtime)
  endif()
endforeach()
