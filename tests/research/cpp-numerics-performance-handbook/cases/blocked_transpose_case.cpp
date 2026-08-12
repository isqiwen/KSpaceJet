#include "common.hpp"

#include <algorithm>

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T> void naive_transpose(const Image<T>& input, Image<T>& output) {
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      output(col, row) = input(row, col);
    }
  }
}

template <typename T>
void blocked_transpose(const Image<T>& input, Image<T>& output, const std::size_t block_size = 32U) {
  for (std::size_t row_block = 0; row_block < input.rows(); row_block += block_size) {
    for (std::size_t col_block = 0; col_block < input.cols(); col_block += block_size) {
      const auto row_end = std::min(row_block + block_size, input.rows());
      const auto col_end = std::min(col_block + block_size, input.cols());
      for (std::size_t row = row_block; row < row_end; ++row) {
        for (std::size_t col = col_block; col < col_end; ++col) {
          output(col, row) = input(row, col);
        }
      }
    }
  }
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    if (size > 2048U) {
      continue;
    }
    auto input = make_image<T>(size, size);
    auto output = make_image<T>(size, size);
    for (std::size_t row = 0; row < input.rows(); ++row) {
      for (std::size_t col = 0; col < input.cols(); ++col) {
        input(row, col) = static_cast<T>((row + col + 1U) % 251U) * T{0.125};
      }
    }

    const auto naive = ksj::research::cpp_numerics_performance::measure(config, [&] {
      naive_transpose(input, output);
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("blocked_transpose", "naive_transpose", type_name, size, 0,
                                                       config, naive,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    const auto blocked = ksj::research::cpp_numerics_performance::measure(config, [&] {
      blocked_transpose(input, output);
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("blocked_transpose", "blocked_32x32", type_name, size, 0, config,
                                                       blocked,
                                                       ksj::research::cpp_numerics_performance::checksum(output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_blocked_transpose [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
