#include "common.hpp"

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T>
[[nodiscard]] T copy_roi_then_sum(const Image<T>& input, Image<T>& roi, std::size_t row0, std::size_t col0) {
  for (std::size_t row = 0; row < roi.rows(); ++row) {
    for (std::size_t col = 0; col < roi.cols(); ++col) {
      roi(row, col) = input(row0 + row, col0 + col);
    }
  }
  T sum{};
  for (std::size_t row = 0; row < roi.rows(); ++row) {
    for (std::size_t col = 0; col < roi.cols(); ++col) {
      sum += roi(row, col);
    }
  }
  return sum;
}

template <typename T>
[[nodiscard]] T direct_roi_sum(const Image<T>& input, std::size_t row0, std::size_t col0, std::size_t rows,
                               std::size_t cols) {
  T sum{};
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      sum += input(row0 + row, col0 + col);
    }
  }
  return sum;
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    if (size > 2048U) {
      continue;
    }
    auto input = make_image<T>(size, size);
    auto roi = make_image<T>(size / 2U, size / 2U);
    for (std::size_t row = 0; row < input.rows(); ++row) {
      for (std::size_t col = 0; col < input.cols(); ++col) {
        input(row, col) = static_cast<T>((row + col + 1U) % 251U) * T{0.125};
      }
    }

    T checksum = {};
    const auto materialized = ksj::research::cpp_numerics_performance::measure(config, [&] {
      checksum = copy_roi_then_sum(input, roi, size / 4U, size / 4U);
      ksj::research::cpp_numerics_performance::do_not_optimize(checksum);
    });
    ksj::research::cpp_numerics_performance::print_row("roi_materialization", "copy_roi_then_sum", type_name, size, 0,
                                                       config, materialized, static_cast<double>(checksum));

    const auto direct = ksj::research::cpp_numerics_performance::measure(config, [&] {
      checksum = direct_roi_sum(input, size / 4U, size / 4U, size / 2U, size / 2U);
      ksj::research::cpp_numerics_performance::do_not_optimize(checksum);
    });
    ksj::research::cpp_numerics_performance::print_row("roi_materialization", "direct_roi_view_sum", type_name, size, 0,
                                                       config, direct, static_cast<double>(checksum));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_roi_materialization [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
