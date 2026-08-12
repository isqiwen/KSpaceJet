#include "common.hpp"

#include <complex>

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T>
void complex_mul_scale_mask_three_pass(const Vector<std::complex<T>>& lhs, const Vector<std::complex<T>>& rhs,
                                       const Vector<T>& mask, Vector<std::complex<T>>& tmp,
                                       Vector<std::complex<T>>& scaled, Vector<std::complex<T>>& output, T scale) {
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    tmp(i) = lhs(i) * rhs(i);
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    scaled(i) = tmp(i) * std::complex<T>{scale, T{}};
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    output(i) = mask(i) > T{0} ? scaled(i) : std::complex<T>{};
  }
}

template <typename T>
void complex_mul_scale_mask_fused(const Vector<std::complex<T>>& lhs, const Vector<std::complex<T>>& rhs,
                                  const Vector<T>& mask, Vector<std::complex<T>>& output, T scale) {
  const auto scale_value = std::complex<T>{scale, T{}};
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    output(i) = mask(i) > T{0} ? lhs(i) * rhs(i) * scale_value : std::complex<T>{};
  }
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<std::complex<T>>(size);
    auto rhs = make_vector<std::complex<T>>(size);
    auto mask = make_vector<T>(size);
    auto tmp = make_vector<std::complex<T>>(size);
    auto scaled = make_vector<std::complex<T>>(size);
    auto output = make_vector<std::complex<T>>(size);
    ksj::research::cpp_numerics_performance::fill_vector(lhs);
    ksj::research::cpp_numerics_performance::fill_vector(rhs);
    for (std::size_t i = 0; i < mask.size(); ++i) {
      mask(i) = (i % 3U) == 0U ? T{} : T{1};
    }

    const auto three_pass_measurement = ksj::research::cpp_numerics_performance::measure(config, [&] {
      complex_mul_scale_mask_three_pass(lhs, rhs, mask, tmp, scaled, output, T{0.5});
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("complex_mul_scale_mask", "three_memory_passes", type_name, size,
                                                       0, config, three_pass_measurement,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    const auto fused_measurement = ksj::research::cpp_numerics_performance::measure(config, [&] {
      complex_mul_scale_mask_fused(lhs, rhs, mask, output, T{0.5});
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("complex_mul_scale_mask", "single_fused_pass", type_name, size,
                                                       0, config, fused_measurement,
                                                       ksj::research::cpp_numerics_performance::checksum(output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config,
    "usage: ksj_numerics_perf_complex_mul_scale_mask [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
