#include "image_benchmark_common.hpp"

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_image_backend_benchmark [--iterations N] [--sizes 16,32,64]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();

  ksj::benchmarks::image_benchmarks::run_policy_benchmarks_float(config);
  ksj::benchmarks::image_benchmarks::run_basic_benchmarks_float(config);
  ksj::benchmarks::image_benchmarks::run_primitive_benchmarks_float(config);
  ksj::benchmarks::image_benchmarks::run_resize_benchmarks_float(config);
  ksj::benchmarks::image_benchmarks::run_filter_benchmarks_float(config);
  ksj::benchmarks::image_benchmarks::run_policy_benchmarks_double(config);
  ksj::benchmarks::image_benchmarks::run_basic_benchmarks_double(config);
  ksj::benchmarks::image_benchmarks::run_primitive_benchmarks_double(config);
  ksj::benchmarks::image_benchmarks::run_resize_benchmarks_double(config);
  ksj::benchmarks::image_benchmarks::run_filter_benchmarks_double(config);
  return 0;
}
