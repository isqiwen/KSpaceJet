#include "common.hpp"

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T>
void allocate_temporary_each_call(const Vector<T>& lhs, const Vector<T>& rhs, Vector<T>& output, const T scale) {
  auto temporary = make_vector<T>(lhs.size());
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    temporary(i) = lhs(i) * rhs(i);
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    output(i) = temporary(i) * scale;
  }
}

template <typename T>
void reuse_workspace(const Vector<T>& lhs, const Vector<T>& rhs, Vector<T>& workspace, Vector<T>& output,
                     const T scale) {
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    workspace(i) = lhs(i) * rhs(i);
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    output(i) = workspace(i) * scale;
  }
}

template <typename T>
void fused_no_temporary(const Vector<T>& lhs, const Vector<T>& rhs, Vector<T>& output, const T scale) {
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    output(i) = lhs(i) * rhs(i) * scale;
  }
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<T>(size);
    auto rhs = make_vector<T>(size);
    auto workspace = make_vector<T>(size);
    auto output = make_vector<T>(size);
    ksj::research::cpp_numerics_performance::fill_vector(lhs);
    ksj::research::cpp_numerics_performance::fill_vector(rhs);

    const auto allocated = ksj::research::cpp_numerics_performance::measure(config, [&] {
      allocate_temporary_each_call(lhs, rhs, output, T{0.5});
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("workspace_reuse", "allocate_each_call", type_name, size, 0,
                                                       config, allocated,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    const auto reused = ksj::research::cpp_numerics_performance::measure(config, [&] {
      reuse_workspace(lhs, rhs, workspace, output, T{0.5});
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("workspace_reuse", "reuse_workspace", type_name, size, 0, config,
                                                       reused,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    const auto fused = ksj::research::cpp_numerics_performance::measure(config, [&] {
      fused_no_temporary(lhs, rhs, output, T{0.5});
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("workspace_reuse", "fused_no_temporary", type_name, size, 0,
                                                       config, fused,
                                                       ksj::research::cpp_numerics_performance::checksum(output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_workspace_reuse [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
