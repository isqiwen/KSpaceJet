#include "linalg_benchmark_common.hpp"

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_linalg_backend_benchmark [--iterations N] [--sizes 16,32,64]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();

  ksj::benchmarks::linalg_benchmarks::run_real_benchmarks_float(config);
  ksj::benchmarks::linalg_benchmarks::run_real_benchmarks_double(config);
  ksj::benchmarks::linalg_benchmarks::run_complex_benchmarks_float(config);
  ksj::benchmarks::linalg_benchmarks::run_complex_benchmarks_double(config);
  ksj::benchmarks::linalg_benchmarks::run_complex_policy_gate_benchmarks_float(config);
  ksj::benchmarks::linalg_benchmarks::run_complex_policy_gate_benchmarks_double(config);
  ksj::benchmarks::linalg_benchmarks::run_ecalib_svd_benchmarks_complex_float(config);
  return 0;
}
