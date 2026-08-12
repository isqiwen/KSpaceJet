#include "common.hpp"

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T> [[nodiscard]] T dynamic_3x3_batch(const Vector<T>& x, const Vector<T>& y, const Vector<T>& z) {
  Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> matrix(3, 3);
  matrix << T{1}, static_cast<T>(0.1), static_cast<T>(0.2), static_cast<T>(0.3), T{1}, static_cast<T>(0.4),
    static_cast<T>(0.5), static_cast<T>(0.6), T{1};
  Eigen::Matrix<T, Eigen::Dynamic, 1> vector(3);
  T checksum{};
  for (std::size_t i = 0; i < x.size(); ++i) {
    vector(0) = x(i);
    vector(1) = y(i);
    vector(2) = z(i);
    const auto result = matrix * vector;
    checksum += result(0) + result(1) + result(2);
  }
  return checksum;
}

template <typename T> [[nodiscard]] T fixed_3x3_batch(const Vector<T>& x, const Vector<T>& y, const Vector<T>& z) {
  Eigen::Matrix<T, 3, 3> matrix;
  matrix << T{1}, static_cast<T>(0.1), static_cast<T>(0.2), static_cast<T>(0.3), T{1}, static_cast<T>(0.4),
    static_cast<T>(0.5), static_cast<T>(0.6), T{1};
  Eigen::Matrix<T, 3, 1> vector;
  T checksum{};
  for (std::size_t i = 0; i < x.size(); ++i) {
    vector(0) = x(i);
    vector(1) = y(i);
    vector(2) = z(i);
    const auto result = matrix * vector;
    checksum += result(0) + result(1) + result(2);
  }
  return checksum;
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto x = make_vector<T>(size);
    auto y = make_vector<T>(size);
    auto z = make_vector<T>(size);
    ksj::research::cpp_numerics_performance::fill_vector(x);
    ksj::research::cpp_numerics_performance::fill_vector(y);
    ksj::research::cpp_numerics_performance::fill_vector(z);

    T checksum = {};
    const auto dynamic = ksj::research::cpp_numerics_performance::measure(config, [&] {
      checksum = dynamic_3x3_batch(x, y, z);
      ksj::research::cpp_numerics_performance::do_not_optimize(checksum);
    });
    ksj::research::cpp_numerics_performance::print_row("fixed_size", "dynamic_3x3_eigen", type_name, size, 0, config,
                                                       dynamic, static_cast<double>(checksum));

    const auto fixed = ksj::research::cpp_numerics_performance::measure(config, [&] {
      checksum = fixed_3x3_batch(x, y, z);
      ksj::research::cpp_numerics_performance::do_not_optimize(checksum);
    });
    ksj::research::cpp_numerics_performance::print_row("fixed_size", "fixed_3x3_eigen", type_name, size, 0, config,
                                                       fixed, static_cast<double>(checksum));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_fixed_size [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
