#include "common.hpp"

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T> [[nodiscard]] T row_major_row_inner_sum(const Image<T>& image) {
  T sum{};
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      sum += image(row, col);
    }
  }
  return sum;
}

template <typename T> [[nodiscard]] T row_major_col_inner_sum(const Image<T>& image) {
  T sum{};
  for (std::size_t col = 0; col < image.cols(); ++col) {
    for (std::size_t row = 0; row < image.rows(); ++row) {
      sum += image(row, col);
    }
  }
  return sum;
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    if (size > 2048U) {
      continue;
    }
    auto image = make_image<T>(size, size);
    for (std::size_t row = 0; row < image.rows(); ++row) {
      for (std::size_t col = 0; col < image.cols(); ++col) {
        image(row, col) = static_cast<T>((row + col + 1U) % 251U) * T{0.125};
      }
    }

    T checksum = {};
    const auto row_inner = ksj::research::cpp_numerics_performance::measure(config, [&] {
      checksum = row_major_row_inner_sum(image);
      ksj::research::cpp_numerics_performance::do_not_optimize(checksum);
    });
    ksj::research::cpp_numerics_performance::print_row("loop_order", "row_major_row_inner", type_name, size, 0, config,
                                                       row_inner, static_cast<double>(checksum));

    const auto col_inner = ksj::research::cpp_numerics_performance::measure(config, [&] {
      checksum = row_major_col_inner_sum(image);
      ksj::research::cpp_numerics_performance::do_not_optimize(checksum);
    });
    ksj::research::cpp_numerics_performance::print_row("loop_order", "row_major_col_inner", type_name, size, 0, config,
                                                       col_inner, static_cast<double>(checksum));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_loop_order [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
