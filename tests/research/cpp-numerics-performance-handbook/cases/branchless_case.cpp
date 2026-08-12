#include "common.hpp"

#include <cmath>

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T> void branchy_soft_threshold(const Vector<T>& input, Vector<T>& output, T lambda) {
  for (std::size_t i = 0; i < input.size(); ++i) {
    const auto value = input(i);
    const auto magnitude = std::abs(value);
    output(i) = magnitude > lambda ? std::copysign(magnitude - lambda, value) : T{};
  }
}

template <typename T> void branchless_soft_threshold(const Vector<T>& input, Vector<T>& output, T lambda) {
  for (std::size_t i = 0; i < input.size(); ++i) {
    const auto value = input(i);
    const auto magnitude = std::abs(value);
    output(i) = std::copysign(std::max(magnitude - lambda, T{}), value);
  }
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<T>(size);
    auto output = make_vector<T>(size);
    for (std::size_t i = 0; i < input.size(); ++i) {
      input(i) = static_cast<T>(static_cast<int>(i % 257U) - 128) * static_cast<T>(0.01);
    }

    const auto branchy = ksj::research::cpp_numerics_performance::measure(config, [&] {
      branchy_soft_threshold(input, output, static_cast<T>(0.4));
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("branchless", "branchy_soft_threshold", type_name, size, 0,
                                                       config, branchy,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    const auto branchless = ksj::research::cpp_numerics_performance::measure(config, [&] {
      branchless_soft_threshold(input, output, static_cast<T>(0.4));
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("branchless", "branchless_soft_threshold", type_name, size, 0,
                                                       config, branchless,
                                                       ksj::research::cpp_numerics_performance::checksum(output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_branchless [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
