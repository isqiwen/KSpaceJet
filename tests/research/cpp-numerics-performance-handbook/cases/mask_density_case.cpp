#include "common.hpp"

#include <vector>

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T> void dense_branchless_mask(const Vector<T>& input, const Vector<T>& mask, Vector<T>& output) {
  for (std::size_t i = 0; i < input.size(); ++i) {
    output(i) = input(i) * mask(i);
  }
}

template <typename T>
void sparse_index_mask(const Vector<T>& input, const std::vector<std::size_t>& active_indices, Vector<T>& output) {
  std::fill_n(output.data(), output.size(), T{});
  for (const auto index : active_indices) {
    output(index) = input(index);
  }
}

template <typename T>
void sparse_index_mask_precleared(const Vector<T>& input, const std::vector<std::size_t>& active_indices,
                                  Vector<T>& output) {
  for (const auto index : active_indices) {
    output(index) = input(index);
  }
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<T>(size);
    auto mask = make_vector<T>(size);
    auto output = make_vector<T>(size);
    std::vector<std::size_t> active_indices;
    active_indices.reserve(size / 16U + 1U);
    ksj::research::cpp_numerics_performance::fill_vector(input);
    for (std::size_t i = 0; i < size; ++i) {
      const bool active = (i % 16U) == 0U;
      mask(i) = active ? T{1} : T{};
      if (active) {
        active_indices.push_back(i);
      }
    }

    const auto dense = ksj::research::cpp_numerics_performance::measure(config, [&] {
      dense_branchless_mask(input, mask, output);
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("mask_density", "dense_branchless_scan", type_name, size, 0,
                                                       config, dense,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    const auto sparse = ksj::research::cpp_numerics_performance::measure(config, [&] {
      sparse_index_mask(input, active_indices, output);
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("mask_density", "sparse_index_list", type_name, size, 0, config,
                                                       sparse,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    std::fill_n(output.data(), output.size(), T{});
    const auto sparse_precleared = ksj::research::cpp_numerics_performance::measure(config, [&] {
      sparse_index_mask_precleared(input, active_indices, output);
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("mask_density", "sparse_index_precleared", type_name, size, 0,
                                                       config, sparse_precleared,
                                                       ksj::research::cpp_numerics_performance::checksum(output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_mask_density [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
