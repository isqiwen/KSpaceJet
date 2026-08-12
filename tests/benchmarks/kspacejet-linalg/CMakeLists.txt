add_executable(
  ksj_linalg_backend_benchmark
  linalg_backend_benchmark_main.cpp linalg_complex_benchmark.cpp linalg_complex_policy_benchmark.cpp
  linalg_ecalib_benchmark.cpp linalg_real_benchmark.cpp)

ksj_target_enable_benchmark_defaults(ksj_linalg_backend_benchmark)
ksj_target_enable_openmp_compile_only(ksj_linalg_backend_benchmark)
target_include_directories(ksj_linalg_backend_benchmark PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")
target_compile_options(ksj_linalg_backend_benchmark PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>)
set_source_files_properties(linalg_complex_benchmark.cpp PROPERTIES COMPILE_OPTIONS
                                                                    "$<$<CXX_COMPILER_ID:GNU>:-Wno-null-dereference>")
set_source_files_properties(linalg_ecalib_benchmark.cpp PROPERTIES COMPILE_OPTIONS
                                                                   "$<$<CXX_COMPILER_ID:GNU>:-Wno-null-dereference>")
target_link_libraries(ksj_linalg_backend_benchmark PRIVATE KSpaceJet::linalg KSpaceJet::stats)
ksj_set_executable_runtime_path(ksj_linalg_backend_benchmark)

add_executable(ksj_linalg_ecalib_benchmark linalg_ecalib_benchmark_main.cpp linalg_ecalib_benchmark.cpp)

ksj_target_enable_benchmark_defaults(ksj_linalg_ecalib_benchmark)
target_include_directories(ksj_linalg_ecalib_benchmark PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")
set_source_files_properties(linalg_ecalib_benchmark.cpp PROPERTIES COMPILE_OPTIONS
                                                                   "$<$<CXX_COMPILER_ID:GNU>:-Wno-null-dereference>")
target_link_libraries(ksj_linalg_ecalib_benchmark PRIVATE KSpaceJet::linalg KSpaceJet::stats)
ksj_set_executable_runtime_path(ksj_linalg_ecalib_benchmark)
