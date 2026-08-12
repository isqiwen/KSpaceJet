#include "common.hpp"

#include <complex>

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T>
void aos_complex_multiply(const Vector<std::complex<T>>& lhs, const Vector<std::complex<T>>& rhs,
                          Vector<std::complex<T>>& output) {
  for (std::size_t i = 0; i < output.size(); ++i) {
    output(i) = lhs(i) * rhs(i);
  }
}

template <typename T>
void soa_complex_multiply(const Vector<T>& lhs_real, const Vector<T>& lhs_imag, const Vector<T>& rhs_real,
                          const Vector<T>& rhs_imag, Vector<T>& output_real, Vector<T>& output_imag) {
  for (std::size_t i = 0; i < output_real.size(); ++i) {
    output_real(i) = lhs_real(i) * rhs_real(i) - lhs_imag(i) * rhs_imag(i);
    output_imag(i) = lhs_real(i) * rhs_imag(i) + lhs_imag(i) * rhs_real(i);
  }
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs_aos = make_vector<std::complex<T>>(size);
    auto rhs_aos = make_vector<std::complex<T>>(size);
    auto output_aos = make_vector<std::complex<T>>(size);
    auto lhs_real = make_vector<T>(size);
    auto lhs_imag = make_vector<T>(size);
    auto rhs_real = make_vector<T>(size);
    auto rhs_imag = make_vector<T>(size);
    auto output_real = make_vector<T>(size);
    auto output_imag = make_vector<T>(size);
    ksj::research::cpp_numerics_performance::fill_vector(lhs_aos);
    ksj::research::cpp_numerics_performance::fill_vector(rhs_aos);
    for (std::size_t i = 0; i < size; ++i) {
      lhs_real(i) = lhs_aos(i).real();
      lhs_imag(i) = lhs_aos(i).imag();
      rhs_real(i) = rhs_aos(i).real();
      rhs_imag(i) = rhs_aos(i).imag();
    }

    const auto aos = ksj::research::cpp_numerics_performance::measure(config, [&] {
      aos_complex_multiply(lhs_aos, rhs_aos, output_aos);
      ksj::research::cpp_numerics_performance::do_not_optimize(output_aos.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("complex_layout", "aos_std_complex", type_name, size, 0, config,
                                                       aos,
                                                       ksj::research::cpp_numerics_performance::checksum(output_aos));

    const auto soa = ksj::research::cpp_numerics_performance::measure(config, [&] {
      soa_complex_multiply(lhs_real, lhs_imag, rhs_real, rhs_imag, output_real, output_imag);
      ksj::research::cpp_numerics_performance::do_not_optimize(output_real.data()[0]);
    });
    const auto checksum = ksj::research::cpp_numerics_performance::checksum(output_real) +
                          ksj::research::cpp_numerics_performance::checksum(output_imag);
    ksj::research::cpp_numerics_performance::print_row("complex_layout", "soa_real_imag", type_name, size, 0, config,
                                                       soa, checksum);
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_complex_layout [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
