add_executable(
  ksj_array_backend_benchmark
  array_backend_benchmark_main.cpp array_basic_benchmark.cpp array_calibration_benchmark.cpp
  array_channel_volume_benchmark.cpp array_complex_benchmark.cpp array_difference_benchmark.cpp)

ksj_target_enable_benchmark_defaults(ksj_array_backend_benchmark)
if(KSJ_ENABLE_NATIVE_ARCH_OPTIMIZATION)
  target_compile_options(ksj_array_backend_benchmark
                         PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-march=native>")
endif()
target_include_directories(ksj_array_backend_benchmark PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")
target_link_libraries(ksj_array_backend_benchmark PRIVATE KSpaceJet::array)
ksj_set_executable_runtime_path(ksj_array_backend_benchmark)
