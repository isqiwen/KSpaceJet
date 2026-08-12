#include "common.hpp"

#include <algorithm>

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T> void materialize_fftshift(const Vector<T>& input, Vector<T>& output) {
  const auto size = input.size();
  const auto shift = (size + 1U) / 2U;
  const auto tail = size - shift;
  std::copy_n(input.data() + shift, tail, output.data());
  std::copy_n(input.data(), shift, output.data() + tail);
}

template <typename T> [[nodiscard]] T sum_after_materialized_shift(const Vector<T>& shifted) {
  T sum{};
  for (std::size_t i = 0; i < shifted.size(); ++i) {
    sum += shifted(i);
  }
  return sum;
}

template <typename T> [[nodiscard]] T sum_with_shifted_index(const Vector<T>& input) {
  const auto size = input.size();
  const auto shift = (size + 1U) / 2U;
  T sum{};
  for (std::size_t i = 0; i < size; ++i) {
    const auto source = i + shift < size ? i + shift : i + shift - size;
    sum += input(source);
  }
  return sum;
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<T>(size);
    auto shifted = make_vector<T>(size);
    ksj::research::cpp_numerics_performance::fill_vector(input);

    T checksum = {};
    const auto materialized = ksj::research::cpp_numerics_performance::measure(config, [&] {
      materialize_fftshift(input, shifted);
      checksum = sum_after_materialized_shift(shifted);
      ksj::research::cpp_numerics_performance::do_not_optimize(checksum);
    });
    ksj::research::cpp_numerics_performance::print_row("fftshift_access", "materialize_then_read", type_name, size, 0,
                                                       config, materialized, static_cast<double>(checksum));

    const auto indexed = ksj::research::cpp_numerics_performance::measure(config, [&] {
      checksum = sum_with_shifted_index(input);
      ksj::research::cpp_numerics_performance::do_not_optimize(checksum);
    });
    ksj::research::cpp_numerics_performance::print_row("fftshift_access", "shifted_index_read", type_name, size, 0,
                                                       config, indexed, static_cast<double>(checksum));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_fftshift_access [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
