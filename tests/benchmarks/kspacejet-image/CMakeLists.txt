add_executable(
  ksj_image_backend_benchmark image_backend_benchmark_main.cpp image_basic_benchmark.cpp image_filter_benchmark.cpp
                              image_policy_benchmark.cpp image_primitive_benchmark.cpp image_resize_benchmark.cpp)

ksj_target_enable_benchmark_defaults(ksj_image_backend_benchmark)
target_include_directories(ksj_image_backend_benchmark PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")
target_link_libraries(ksj_image_backend_benchmark PRIVATE KSpaceJet::image)
ksj_set_executable_runtime_path(ksj_image_backend_benchmark)
