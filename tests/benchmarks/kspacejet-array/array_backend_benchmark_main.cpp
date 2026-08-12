#include "array_benchmark_common.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

[[nodiscard]] ksj::benchmarks::Config capped_config(const ksj::benchmarks::Config& config,
                                                    const std::size_t max_elements) {
  ksj::benchmarks::Config capped = config;
  std::vector<std::size_t> sizes;
  sizes.reserve(config.sizes.size());
  std::copy_if(config.sizes.begin(), config.sizes.end(), std::back_inserter(sizes),
               [max_elements](const std::size_t size) {
                 return size <= max_elements;
               });
  if (sizes.empty()) {
    sizes.push_back(max_elements);
  }
  capped.sizes = std::move(sizes);
  return capped;
}

void print_size_selection(const ksj::benchmarks::Config& config) {
  std::cerr << "[kspacejet-array-benchmark] element-count sweep:";
  for (const auto size : config.sizes) {
    std::cerr << ' ' << size;
  }
  std::cerr << " (max elements " << ksj::benchmarks::array_benchmarks::kMaxArrayBenchmarkElements << ")\n";
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_array_backend_benchmark [--iterations N] [--sizes 16,32,64]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();

  const auto element_count_config =
    capped_config(config, ksj::benchmarks::array_benchmarks::kMaxArrayBenchmarkElements);
  print_size_selection(element_count_config);

  ksj::benchmarks::array_benchmarks::run_basic_benchmarks_float(element_count_config);
  ksj::benchmarks::array_benchmarks::run_basic_benchmarks_double(element_count_config);
  ksj::benchmarks::array_benchmarks::run_complex_benchmarks_float(element_count_config);
  ksj::benchmarks::array_benchmarks::run_complex_benchmarks_double(element_count_config);
  ksj::benchmarks::array_benchmarks::run_difference_benchmarks_complex_float(element_count_config);
  ksj::benchmarks::array_benchmarks::run_channel_volume_benchmarks_complex_float(element_count_config);
  ksj::benchmarks::array_benchmarks::run_calibration_benchmarks_complex_float(element_count_config);
  return 0;
}
