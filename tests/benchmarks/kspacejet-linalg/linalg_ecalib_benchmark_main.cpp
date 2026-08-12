#include "linalg_benchmark_common.hpp"

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  config.sizes = {4U, 8U, 16U, 32U};
  config.iterations = 20U;
  config.trials = 10U;

  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_linalg_ecalib_benchmark [--iterations N] [--trials N] [--sizes 4,8,16,32]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();

  ksj::benchmarks::linalg_benchmarks::run_ecalib_svd_benchmarks_complex_float(config);
  return 0;
}
