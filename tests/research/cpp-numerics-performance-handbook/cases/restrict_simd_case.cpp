#include "common.hpp"

namespace {

using namespace ksj::research::cpp_numerics_performance;

#if defined(__GNUC__) || defined(__clang__)
#define KSJ_RESEARCH_RESTRICT __restrict__
#else
#define KSJ_RESEARCH_RESTRICT
#endif

template <typename T> void plain_pointer_loop(const T* lhs, const T* rhs, T* output, std::size_t size, T scale) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = lhs[i] * rhs[i] + scale;
  }
}

template <typename T>
void restrict_simd_loop(const T* KSJ_RESEARCH_RESTRICT lhs, const T* KSJ_RESEARCH_RESTRICT rhs,
                        T* KSJ_RESEARCH_RESTRICT output, std::size_t size, T scale) {
#if defined(_OPENMP)
#pragma omp simd
#endif
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = lhs[i] * rhs[i] + scale;
  }
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<T>(size);
    auto rhs = make_vector<T>(size);
    auto output = make_vector<T>(size);
    ksj::research::cpp_numerics_performance::fill_vector(lhs);
    ksj::research::cpp_numerics_performance::fill_vector(rhs);

    const auto plain = ksj::research::cpp_numerics_performance::measure(config, [&] {
      plain_pointer_loop(lhs.data(), rhs.data(), output.data(), output.size(), T{0.125});
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("restrict_simd", "plain_pointer_loop", type_name, size, 0,
                                                       config, plain,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    const auto hinted = ksj::research::cpp_numerics_performance::measure(config, [&] {
      restrict_simd_loop(lhs.data(), rhs.data(), output.data(), output.size(), T{0.125});
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("restrict_simd", "restrict_omp_simd", type_name, size, 0, config,
                                                       hinted,
                                                       ksj::research::cpp_numerics_performance::checksum(output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_restrict_simd [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}

#undef KSJ_RESEARCH_RESTRICT
