#include "common.hpp"

#include <cmath>
#include <complex>

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T> void rss_from_column_major_coils(const Matrix<std::complex<T>>& input, Vector<T>& output) {
  for (std::size_t voxel = 0; voxel < input.rows(); ++voxel) {
    auto sum = T{};
    for (std::size_t coil = 0; coil < input.cols(); ++coil) {
      const auto value = input(voxel, coil);
      sum += value.real() * value.real() + value.imag() * value.imag();
    }
    output(voxel) = std::sqrt(sum);
  }
}

template <typename T> void rss_from_row_major_voxels(const Image<std::complex<T>>& input, Vector<T>& output) {
  for (std::size_t voxel = 0; voxel < input.rows(); ++voxel) {
    auto sum = T{};
    for (std::size_t coil = 0; coil < input.cols(); ++coil) {
      const auto value = input(voxel, coil);
      sum += value.real() * value.real() + value.imag() * value.imag();
    }
    output(voxel) = std::sqrt(sum);
  }
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    for (const auto coils : config.coils) {
      auto column_major = make_matrix<std::complex<T>>(size, coils);
      auto row_major = make_image<std::complex<T>>(size, coils);
      auto output = make_vector<T>(size);
      ksj::research::cpp_numerics_performance::fill_matrix(column_major);
      ksj::research::cpp_numerics_performance::fill_image(row_major);

      const auto column_major_measurement = ksj::research::cpp_numerics_performance::measure(config, [&] {
        rss_from_column_major_coils(column_major, output);
        ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
      });
      ksj::research::cpp_numerics_performance::print_row("rss_coil_combine", "column_major_matrix", type_name, size,
                                                         coils, config, column_major_measurement,
                                                         ksj::research::cpp_numerics_performance::checksum(output));

      const auto row_major_measurement = ksj::research::cpp_numerics_performance::measure(config, [&] {
        rss_from_row_major_voxels(row_major, output);
        ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
      });
      ksj::research::cpp_numerics_performance::print_row("rss_coil_combine", "row_major_voxel_coils", type_name, size,
                                                         coils, config, row_major_measurement,
                                                         ksj::research::cpp_numerics_performance::checksum(output));
    }
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config,
    "usage: ksj_numerics_perf_rss_coil_combine [--iterations N] [--trials N] [--sizes A,B,C] [--coils A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
