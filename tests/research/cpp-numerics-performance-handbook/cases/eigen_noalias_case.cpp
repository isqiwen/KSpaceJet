#include "common.hpp"

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    if (size > 1024U) {
      continue;
    }
    auto lhs = make_matrix<T>(size, size);
    auto rhs = make_matrix<T>(size, size);
    auto output = make_matrix<T>(size, size);
    ksj::research::cpp_numerics_performance::fill_matrix(lhs);
    ksj::research::cpp_numerics_performance::fill_matrix(rhs);

    const auto plain = ksj::research::cpp_numerics_performance::measure(config, [&] {
      as_eigen(output) = as_eigen(lhs) * as_eigen(rhs);
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("eigen_noalias", "plain_assignment", type_name, size, 0, config,
                                                       plain,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    const auto noalias = ksj::research::cpp_numerics_performance::measure(config, [&] {
      as_eigen(output).noalias() = as_eigen(lhs) * as_eigen(rhs);
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("eigen_noalias", "noalias_assignment", type_name, size, 0,
                                                       config, noalias,
                                                       ksj::research::cpp_numerics_performance::checksum(output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_eigen_noalias [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
