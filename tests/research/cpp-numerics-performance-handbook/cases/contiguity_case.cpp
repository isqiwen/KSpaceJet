#include "common.hpp"

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T>
void row_major_contiguous_fma(const Image<T>& lhs, const Image<T>& rhs, Image<T>& output, const T scale) {
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = lhs(row, col) * rhs(row, col) + scale;
    }
  }
}

template <typename T>
void row_major_strided_fma(const Image<T>& lhs, const Image<T>& rhs, Image<T>& output, const T scale) {
  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      output(row, col) = lhs(row, col) * rhs(row, col) + scale;
    }
  }
}

template <typename T>
void column_major_contiguous_fma(const Matrix<T>& lhs, const Matrix<T>& rhs, Matrix<T>& output, const T scale) {
  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      output(row, col) = lhs(row, col) * rhs(row, col) + scale;
    }
  }
}

template <typename T>
void column_major_strided_fma(const Matrix<T>& lhs, const Matrix<T>& rhs, Matrix<T>& output, const T scale) {
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = lhs(row, col) * rhs(row, col) + scale;
    }
  }
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    if (size > 2048U) {
      continue;
    }

    auto row_lhs = make_image<T>(size, size);
    auto row_rhs = make_image<T>(size, size);
    auto row_output = make_image<T>(size, size);
    auto column_lhs = make_matrix<T>(size, size);
    auto column_rhs = make_matrix<T>(size, size);
    auto column_output = make_matrix<T>(size, size);
    fill_image(row_lhs);
    fill_image(row_rhs);
    fill_matrix(column_lhs);
    fill_matrix(column_rhs);

    const auto row_contiguous = measure(config, [&] {
      row_major_contiguous_fma(row_lhs, row_rhs, row_output, T{0.125});
      do_not_optimize(row_output.data()[0]);
    });
    print_row("contiguity", "row_major_contiguous_fma", type_name, size, 0, config, row_contiguous,
              checksum(row_output));

    const auto row_strided = measure(config, [&] {
      row_major_strided_fma(row_lhs, row_rhs, row_output, T{0.125});
      do_not_optimize(row_output.data()[0]);
    });
    print_row("contiguity", "row_major_strided_fma", type_name, size, 0, config, row_strided, checksum(row_output));

    const auto column_contiguous = measure(config, [&] {
      column_major_contiguous_fma(column_lhs, column_rhs, column_output, T{0.125});
      do_not_optimize(column_output.data()[0]);
    });
    print_row("contiguity", "column_major_contiguous_fma", type_name, size, 0, config, column_contiguous,
              checksum(column_output));

    const auto column_strided = measure(config, [&] {
      column_major_strided_fma(column_lhs, column_rhs, column_output, T{0.125});
      do_not_optimize(column_output.data()[0]);
    });
    print_row("contiguity", "column_major_strided_fma", type_name, size, 0, config, column_strided,
              checksum(column_output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_contiguity [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
