find_package(Eigen3 CONFIG REQUIRED)
find_package(Intel CONFIG REQUIRED)

function(ksj_add_numerics_perf_handbook_case target_name source_file)
  add_executable(${target_name} ${source_file})
  ksj_target_enable_research_defaults(${target_name})
  target_include_directories(${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
  target_link_libraries(${target_name} PRIVATE Eigen3::Eigen Intel::headers Intel::mkl_rt KSpaceJet::numerics_runtime)
  ksj_set_executable_runtime_path(${target_name})
  ksj_install_target(${target_name})
  ksj_register_test_suite_executable(RESEARCH ${target_name})
endfunction()

function(ksj_add_numerics_perf_compute_scenarios_case target_name scenario_variant enable_avx512_intrinsics)
  add_executable(${target_name} cases/avx512_compute_scenarios_case.cpp)
  ksj_target_enable_defaults(${target_name})
  target_include_directories(${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
  target_link_libraries(${target_name} PRIVATE Eigen3::Eigen Intel::headers Intel::mkl_rt KSpaceJet::numerics_runtime)
  target_compile_definitions(
    ${target_name} PRIVATE KSJ_NUMERICS_PERF_SCENARIO_VARIANT="${scenario_variant}"
                           KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS=${enable_avx512_intrinsics})
  if(scenario_variant MATCHES "^avx512")
    target_compile_options(${target_name} PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-march=x86-64-v4>)
  endif()
  ksj_set_executable_runtime_path(${target_name})
  ksj_install_target(${target_name})
  ksj_register_test_suite_executable(RESEARCH ${target_name})
endfunction()

ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_matmul cases/matmul_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_workspace_reuse cases/workspace_reuse_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_complex_mul_scale_mask cases/complex_mul_scale_mask_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_loop_order cases/loop_order_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_contiguity cases/contiguity_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_fftshift_access cases/fftshift_access_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_eigen_noalias cases/eigen_noalias_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_restrict_simd cases/restrict_simd_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_alignment cases/alignment_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_complex_layout cases/complex_layout_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_mask_density cases/mask_density_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_cg_update cases/cg_update_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_rss_coil_combine cases/rss_coil_combine_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_blocked_transpose cases/blocked_transpose_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_roi_materialization cases/roi_materialization_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_branchless cases/branchless_case.cpp)
ksj_add_numerics_perf_handbook_case(ksj_numerics_perf_fixed_size cases/fixed_size_case.cpp)
ksj_add_numerics_perf_compute_scenarios_case(ksj_numerics_perf_compute_scenarios_baseline baseline 0)
ksj_add_numerics_perf_compute_scenarios_case(ksj_numerics_perf_compute_scenarios_avx512_auto_vectorized
                                             avx512_auto_vectorized 0)
ksj_add_numerics_perf_compute_scenarios_case(ksj_numerics_perf_compute_scenarios_avx512_intrinsics avx512_intrinsics 1)

add_custom_target(
  ksj_numerics_perf_compute_scenarios_avx512_compare
  DEPENDS ksj_numerics_perf_compute_scenarios_baseline ksj_numerics_perf_compute_scenarios_avx512_auto_vectorized
          ksj_numerics_perf_compute_scenarios_avx512_intrinsics)

add_custom_target(
  ksj_cpp_numerics_performance_handbook
  DEPENDS ksj_numerics_perf_matmul
          ksj_numerics_perf_workspace_reuse
          ksj_numerics_perf_complex_mul_scale_mask
          ksj_numerics_perf_loop_order
          ksj_numerics_perf_contiguity
          ksj_numerics_perf_fftshift_access
          ksj_numerics_perf_eigen_noalias
          ksj_numerics_perf_restrict_simd
          ksj_numerics_perf_alignment
          ksj_numerics_perf_complex_layout
          ksj_numerics_perf_mask_density
          ksj_numerics_perf_cg_update
          ksj_numerics_perf_rss_coil_combine
          ksj_numerics_perf_blocked_transpose
          ksj_numerics_perf_roi_materialization
          ksj_numerics_perf_branchless
          ksj_numerics_perf_fixed_size
          ksj_numerics_perf_compute_scenarios_avx512_compare)
