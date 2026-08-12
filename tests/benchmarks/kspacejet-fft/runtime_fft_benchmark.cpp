#include "benchmark_common.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/fft/fft.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {

using Complex = ksj::base::cf32;
using Vector = ksj::array::PooledVector<Complex>;
using Matrix = ksj::array::PooledMatrix<Complex>;

inline constexpr std::size_t kRows = 32;
inline constexpr std::size_t kCols = 32;
inline constexpr std::size_t kSegments = 8;
inline constexpr int kFftRowMode = ksj::fft::kRowFft;
inline constexpr int kColumnMode = 1;

[[nodiscard]] ksj::fft::Direction legacy_direction(const bool inverse_fft) noexcept {
  return inverse_fft ? ksj::fft::Direction::forward : ksj::fft::Direction::inverse;
}

void fill_complex(Vector& vector) {
  for (std::size_t index = 0; index < vector.size(); ++index) {
    const auto real = static_cast<float>(static_cast<double>((index * 17U + 3U) % 251U) * 0.125);
    const auto imag = static_cast<float>(static_cast<double>((index * 13U + 7U) % 127U) * 0.0625);
    vector(index) = {real, imag};
  }
}

void fill_complex(Matrix& matrix) {
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      const auto real = static_cast<float>(static_cast<double>((row * 17U + col * 31U + 3U) % 251U) * 0.125);
      const auto imag = static_cast<float>(static_cast<double>((row * 13U + col * 19U + 7U) % 127U) * 0.0625);
      matrix(row, col) = {real, imag};
    }
  }
}

template <typename Container> void reset_from_seed(Container& output, const Container& seed) {
  std::copy(seed.data(), seed.data() + seed.size(), output.data());
}

[[nodiscard]] double checksum_vector(const Vector& vector) {
  double checksum = 0.0;
  for (std::size_t index = 0; index < vector.size(); ++index) {
    checksum += static_cast<double>(vector[index].real() + vector[index].imag());
  }
  return checksum;
}

[[nodiscard]] double checksum_matrix(const Matrix& matrix) {
  double checksum = 0.0;
  for (std::size_t index = 0; index < matrix.size(); ++index) {
    checksum += static_cast<double>(matrix.data()[index].real() + matrix.data()[index].imag());
  }
  return checksum;
}

void pack_view(ksj::array::VectorView<Complex> input, Vector& output) {
  output.resize(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    output[index] = input[index];
  }
}

void copy_to_view(ksj::array::VectorView<const Complex> input, ksj::array::VectorView<Complex> output) {
  for (std::size_t index = 0; index < input.size(); ++index) {
    output[index] = input[index];
  }
}

[[nodiscard]] bool legacy_fft_view_in_place(ksj::array::VectorView<Complex> view, ksj::fft::Fft1Plan<float>& plan,
                                            const bool preshift, const bool postshift) {
  try {
    Vector input;
    pack_view(view, input);

    ksj::array::VectorView<const Complex> transform_input = ksj::array::as_const_view(input.view());
    Vector shifted_input;
    if (preshift) {
      shifted_input.resize(view.size());
      ksj::fft::ifftshift(ksj::array::as_const_view(input.view()), shifted_input.view());
      transform_input = ksj::array::as_const_view(shifted_input.view());
    }

    Vector output;
    output.resize(view.size());
    plan.execute(transform_input, output.view());
    if (postshift) {
      ksj::fft::fftshift(ksj::array::as_const_view(output.view()), view);
    } else {
      copy_to_view(ksj::array::as_const_view(output.view()), view);
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

[[nodiscard]] bool legacy_fft_1d_in_place(ksj::array::VectorView<Complex> view, const bool inverse_fft,
                                          const bool preshift, const bool postshift) {
  if (view.empty()) {
    return false;
  }

  try {
    ksj::fft::Fft1Plan<float> plan(view.size(), legacy_direction(inverse_fft), ksj::fft::Normalization::orthonormal);
    return legacy_fft_view_in_place(view, plan, preshift, postshift);
  } catch (const std::exception&) {
    return false;
  }
}

[[nodiscard]] bool legacy_fft_2d_in_place(ksj::array::MatrixView<Complex> view, const int mode, const bool inverse_fft,
                                          const bool preshift, const bool postshift) {
  if (view.empty()) {
    return false;
  }

  const auto length = mode == kFftRowMode ? view.cols() : view.rows();
  if (length == 0U) {
    return false;
  }

  try {
    ksj::fft::Fft1Plan<float> plan(length, legacy_direction(inverse_fft), ksj::fft::Normalization::orthonormal);
    if (mode == kFftRowMode) {
      for (std::size_t row = 0; row < view.rows(); ++row) {
        if (!legacy_fft_view_in_place(view.row(row), plan, preshift, postshift)) {
          return false;
        }
      }
      return true;
    }

    for (std::size_t col = 0; col < view.cols(); ++col) {
      if (!legacy_fft_view_in_place(view.col(col), plan, preshift, postshift)) {
        return false;
      }
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

[[nodiscard]] bool legacy_fft_segmented_in_place(ksj::array::VectorView<Complex> view, const bool inverse_fft,
                                                 const std::size_t segments, const bool postshift) {
  if (view.empty() || segments == 0U) {
    return false;
  }
  const auto segment_length = view.size() / segments;
  if (segment_length == 0U) {
    return false;
  }

  try {
    ksj::fft::Fft1Plan<float> plan(segment_length, legacy_direction(inverse_fft), ksj::fft::Normalization::orthonormal);
    for (std::size_t segment = 0; segment < segments; ++segment) {
      const auto segment_start = segment * segment_length;
      if (!legacy_fft_view_in_place(view.subview(ksj::array::slice(segment_start, segment_start + segment_length)),
                                    plan, true, postshift)) {
        return false;
      }
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

template <typename Container, typename Function>
[[nodiscard]] ksj::benchmarks::Measurement measure_case(const ksj::benchmarks::Config& config, const Container& seed,
                                                        Container& data, Function&& function, double& checksum) {
  reset_from_seed(data, seed);
  return ksj::benchmarks::measure(config, [&] {
    if (!function(data)) {
      throw std::runtime_error("runtime FFT benchmark operation failed");
    }
    checksum = [&]() -> double {
      if constexpr (std::is_same_v<Container, Vector>) {
        return checksum_vector(data);
      } else {
        return checksum_matrix(data);
      }
    }();
    ksj::benchmarks::do_not_optimize(checksum);
  });
}

void print_pair(std::string_view case_name, const std::size_t size, const ksj::benchmarks::Config& config,
                const ksj::benchmarks::Measurement& legacy_ns, const double legacy_checksum,
                const ksj::benchmarks::Measurement& optimized_ns, const double optimized_checksum) {
  ksj::benchmarks::print_row(case_name, "legacy_pack_each", "complex_float", size, config, legacy_ns, legacy_checksum,
                             ksj::benchmarks::reference_row(case_name, "output_reuse"));
  ksj::benchmarks::print_row(case_name, "optimized_scratch", "complex_float", size, config, optimized_ns,
                             optimized_checksum, ksj::benchmarks::candidate_row(case_name, "output_reuse"));
}

void run_contiguous_row_benchmark(const std::size_t size, const ksj::benchmarks::Config& config) {
  auto seed = ksj::array::make_pooled_matrix<Complex>(kRows, size);
  auto legacy_data = ksj::array::make_pooled_matrix<Complex>(kRows, size);
  auto optimized_data = ksj::array::make_pooled_matrix<Complex>(kRows, size);
  fill_complex(seed);

  double legacy_checksum = 0.0;
  const auto legacy_ns = measure_case(
    config, seed, legacy_data,
    [](Matrix& data) {
      return legacy_fft_2d_in_place(data.view(), kFftRowMode, false, false, false);
    },
    legacy_checksum);

  double optimized_checksum = 0.0;
  const auto optimized_ns = measure_case(
    config, seed, optimized_data,
    [](Matrix& data) {
      ksj::fft::fft_inplace(data.view(), ksj::fft::dim_from_ksj_fft_mode(kFftRowMode), legacy_direction(false),
                            ksj::fft::Normalization::orthonormal, false, false);
      return true;
    },
    optimized_checksum);

  print_pair("runtime_fft_contiguous_rows", size, config, legacy_ns, legacy_checksum, optimized_ns, optimized_checksum);
}

void run_strided_col_benchmark(const std::size_t size, const ksj::benchmarks::Config& config) {
  auto seed = ksj::array::make_pooled_matrix<Complex>(size, kCols);
  auto legacy_data = ksj::array::make_pooled_matrix<Complex>(size, kCols);
  auto optimized_data = ksj::array::make_pooled_matrix<Complex>(size, kCols);
  fill_complex(seed);

  double legacy_checksum = 0.0;
  const auto legacy_ns = measure_case(
    config, seed, legacy_data,
    [](Matrix& data) {
      return legacy_fft_2d_in_place(data.view(), kColumnMode, false, false, false);
    },
    legacy_checksum);

  double optimized_checksum = 0.0;
  const auto optimized_ns = measure_case(
    config, seed, optimized_data,
    [](Matrix& data) {
      ksj::fft::fft_inplace(data.view(), ksj::fft::dim_from_ksj_fft_mode(kColumnMode), legacy_direction(false),
                            ksj::fft::Normalization::orthonormal, false, false);
      return true;
    },
    optimized_checksum);

  print_pair("runtime_fft_strided_cols", size, config, legacy_ns, legacy_checksum, optimized_ns, optimized_checksum);
}

void run_segmented_benchmark(const std::size_t size, const ksj::benchmarks::Config& config) {
  auto seed = ksj::array::make_pooled_vector<Complex>(size * kSegments);
  auto legacy_data = ksj::array::make_pooled_vector<Complex>(size * kSegments);
  auto optimized_data = ksj::array::make_pooled_vector<Complex>(size * kSegments);
  fill_complex(seed);

  double legacy_checksum = 0.0;
  const auto legacy_ns = measure_case(
    config, seed, legacy_data,
    [](Vector& data) {
      return legacy_fft_segmented_in_place(data.view(), false, kSegments, true);
    },
    legacy_checksum);

  double optimized_checksum = 0.0;
  const auto optimized_ns = measure_case(
    config, seed, optimized_data,
    [](Vector& data) {
      ksj::fft::fft_segmented_inplace(data.view(), kSegments, legacy_direction(false),
                                      ksj::fft::Normalization::orthonormal, true, true);
      return true;
    },
    optimized_checksum);

  print_pair("runtime_fft_segmented1d_x8", size, config, legacy_ns, legacy_checksum, optimized_ns, optimized_checksum);
}

void run_shift_benchmark(const std::size_t size, const ksj::benchmarks::Config& config) {
  auto seed = ksj::array::make_pooled_vector<Complex>(size);
  auto legacy_data = ksj::array::make_pooled_vector<Complex>(size);
  auto optimized_data = ksj::array::make_pooled_vector<Complex>(size);
  fill_complex(seed);

  double legacy_checksum = 0.0;
  const auto legacy_ns = measure_case(
    config, seed, legacy_data,
    [](Vector& data) {
      return legacy_fft_1d_in_place(data.view(), false, true, true);
    },
    legacy_checksum);

  double optimized_checksum = 0.0;
  const auto optimized_ns = measure_case(
    config, seed, optimized_data,
    [](Vector& data) {
      ksj::fft::fft_inplace(data.view(), legacy_direction(false), ksj::fft::Normalization::orthonormal, true, true);
      return true;
    },
    optimized_checksum);

  print_pair("runtime_fft_preshift_postshift", size, config, legacy_ns, legacy_checksum, optimized_ns,
             optimized_checksum);
}

void run_benchmarks(const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    run_contiguous_row_benchmark(size, config);
    run_strided_col_benchmark(size, config);
    run_segmented_benchmark(size, config);
    run_shift_benchmark(size, config);
  }
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_runtime_fft_benchmark [--iterations N] [--sizes 64,128,256]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();
  run_benchmarks(config);
  return 0;
}
