#include "benchmark_common.hpp"

#include "kspacejet/signal/signal.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_filters.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_windows.hpp"
#include "kspacejet/special/detail/intel/intel_special_functions.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Shape2D {
  std::size_t rows{};
  std::size_t cols{};
};

[[nodiscard]] std::size_t largest_divisor_at_most(const std::size_t value, const std::size_t limit) noexcept {
  const auto capped_limit = std::min(value, limit);
  for (std::size_t candidate = capped_limit; candidate > 1U; --candidate) {
    if (value % candidate == 0U) {
      return candidate;
    }
  }
  return 1U;
}

[[nodiscard]] std::size_t integer_sqrt_floor(const std::size_t value) noexcept {
  std::size_t root = 1U;
  while ((root + 1U) <= value / (root + 1U)) {
    ++root;
  }
  return root;
}

[[nodiscard]] Shape2D element_count_shape2d(const std::size_t element_count) noexcept {
  const auto rows = largest_divisor_at_most(element_count, integer_sqrt_floor(element_count));
  return {rows, element_count / rows};
}

template <typename BackendCall> void require_backend(std::string_view name, BackendCall&& backend_call) {
  if (!backend_call()) {
    throw std::runtime_error(std::string{name} + " backend was not available");
  }
}

[[nodiscard]] std::string_view window_kind_name(const ksj::signal::WindowKind kind) noexcept {
  switch (kind) {
    case ksj::signal::WindowKind::rectangular:
      return "rectangular";
    case ksj::signal::WindowKind::hann:
      return "hann";
    case ksj::signal::WindowKind::hamming:
      return "hamming";
    case ksj::signal::WindowKind::blackman:
      return "blackman";
  }
  return "unknown";
}

template <typename T> void fill_phase_vector(ksj::array::PooledVector<T>& phase) {
  const auto period = T{2} * std::numbers::pi_v<T>;
  for (std::size_t index = 0; index < phase.size(); ++index) {
    auto value = std::fmod(static_cast<T>(index) * static_cast<T>(0.013) + static_cast<T>(0.25), period);
    if (value > std::numbers::pi_v<T>) {
      value -= period;
    }
    phase(index) = value;
  }
}

template <typename T> void fill_phase_image(ksj::array::PooledImage<T>& phase) {
  const auto period = T{2} * std::numbers::pi_v<T>;
  for (std::size_t row = 0; row < phase.rows(); ++row) {
    for (std::size_t col = 0; col < phase.cols(); ++col) {
      auto value =
        std::fmod(static_cast<T>(row) * static_cast<T>(0.18) + static_cast<T>(col) * static_cast<T>(0.27), period);
      if (value > std::numbers::pi_v<T>) {
        value -= period;
      }
      phase(row, col) = value;
    }
  }
}

template <typename T>
[[nodiscard]] double checksum_pair(const ksj::array::PooledVector<T>& lhs, const ksj::array::PooledVector<T>& rhs) {
  return ksj::benchmarks::checksum(lhs) + ksj::benchmarks::checksum(rhs) * 0.125;
}

template <typename T>
[[nodiscard]] double checksum_complex_pair(const ksj::array::PooledImage<std::complex<T>>& lhs,
                                           const ksj::array::PooledImage<std::complex<T>>& rhs) {
  double checksum = 0.0;
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    checksum += static_cast<double>(lhs.data()[index].real() + lhs.data()[index].imag());
    checksum += static_cast<double>(rhs.data()[index].real() + rhs.data()[index].imag()) * 0.125;
  }
  return checksum;
}

[[nodiscard]] ksj::benchmarks::RowMetadata output_row_metadata(const std::string_view case_name,
                                                               const std::string_view backend) {
  if (backend.starts_with("eigen") || backend.starts_with("manual")) {
    return ksj::benchmarks::reference_row(case_name, "output_reuse");
  }
  return ksj::benchmarks::candidate_row(case_name, "output_reuse");
}

template <typename T, typename Function>
void print_vector_measurement(std::string_view case_name, std::string_view backend, std::string_view type_name,
                              const std::size_t size, const ksj::benchmarks::Config& config,
                              const ksj::array::PooledVector<T>& output, Function&& function) {
  const auto measurement = ksj::benchmarks::measure(config, [&] {
    function();
    ksj::benchmarks::do_not_optimize(output.data()[0]);
  });
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement,
                             ksj::benchmarks::checksum(output), output_row_metadata(case_name, backend));
}

template <typename T, typename Function>
void print_pair_measurement(std::string_view case_name, std::string_view backend, std::string_view type_name,
                            const std::size_t size, const ksj::benchmarks::Config& config,
                            const ksj::array::PooledVector<T>& lhs, const ksj::array::PooledVector<T>& rhs,
                            Function&& function) {
  const auto measurement = ksj::benchmarks::measure(config, [&] {
    function();
    ksj::benchmarks::do_not_optimize(lhs.data()[0]);
    ksj::benchmarks::do_not_optimize(rhs.data()[0]);
  });
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum_pair(lhs, rhs),
                             output_row_metadata(case_name, backend));
}

template <typename T, typename Function>
void print_complex_pair_measurement(std::string_view case_name, std::string_view backend, std::string_view type_name,
                                    const std::size_t size, const ksj::benchmarks::Config& config,
                                    const ksj::array::PooledImage<std::complex<T>>& lhs,
                                    const ksj::array::PooledImage<std::complex<T>>& rhs, Function&& function) {
  const auto measurement = ksj::benchmarks::measure(config, [&] {
    function();
    ksj::benchmarks::do_not_optimize(lhs.data()[0]);
    ksj::benchmarks::do_not_optimize(rhs.data()[0]);
  });
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum_complex_pair(lhs, rhs),
                             output_row_metadata(case_name, backend));
}

template <typename T>
void mkl_vml_cos_window(ksj::array::VectorView<T> output, const ksj::signal::WindowKind kind,
                        ksj::array::PooledVector<T>& phase, ksj::array::PooledVector<T>& phase2,
                        ksj::array::PooledVector<T>& cos1, ksj::array::PooledVector<T>& cos2) {
  if (output.empty()) {
    return;
  }

  const auto size = output.size();
  if (kind == ksj::signal::WindowKind::rectangular || size == 1U) {
    ksj::array::fill(output, T{1});
    return;
  }

  const auto phase_scale = T{2} * std::numbers::pi_v<T> / static_cast<T>(size - 1U);
  for (std::size_t index = 0; index < size; ++index) {
    const auto value = phase_scale * static_cast<T>(index);
    phase(index) = value;
    phase2(index) = T{2} * value;
  }

  require_backend("mkl_vml_cos_window", [&] {
    return ksj::special::detail::intel::cos(ksj::array::as_const_view(phase.view()), cos1.view());
  });

  if (kind == ksj::signal::WindowKind::blackman) {
    require_backend("mkl_vml_cos_window", [&] {
      return ksj::special::detail::intel::cos(ksj::array::as_const_view(phase2.view()), cos2.view());
    });
  }

  for (std::size_t index = 0; index < size; ++index) {
    switch (kind) {
      case ksj::signal::WindowKind::rectangular:
        output(index) = T{1};
        break;
      case ksj::signal::WindowKind::hann:
        output(index) = T{0.5} - T{0.5} * cos1(index);
        break;
      case ksj::signal::WindowKind::hamming:
        output(index) = static_cast<T>(0.54) - static_cast<T>(0.46) * cos1(index);
        break;
      case ksj::signal::WindowKind::blackman:
        output(index) = static_cast<T>(0.42) - static_cast<T>(0.5) * cos1(index) + static_cast<T>(0.08) * cos2(index);
        break;
    }
  }
}

template <typename T>
void mkl_vml_exponential_window(ksj::array::VectorView<T> output, ksj::array::PooledVector<T>& exponent_input,
                                const T alpha, const T exponent) {
  if (output.empty()) {
    return;
  }

  const auto center = static_cast<T>(output.size() - 1U) / T{2};
  const auto denom = output.size() > 1U ? static_cast<T>(output.size() - 1U) : T{1};
  for (std::size_t index = 0; index < output.size(); ++index) {
    const auto normalized = std::abs((static_cast<T>(index) - center) / denom);
    const auto powered = exponent == T{2} ? normalized * normalized : std::pow(normalized, exponent);
    exponent_input(index) = -alpha * powered;
  }

  require_backend("mkl_vml_exponential_window", [&] {
    return ksj::special::detail::intel::exp(ksj::array::as_const_view(exponent_input.view()), output);
  });
}

template <typename T>
void mkl_vml_fermi_window(ksj::array::VectorView<T> output, ksj::array::PooledVector<T>& exponent_input, const T radius,
                          const T width) {
  if (width <= T{}) {
    throw std::invalid_argument("mkl_vml_fermi_window width must be positive");
  }
  if (output.empty()) {
    return;
  }

  const auto center = static_cast<T>(output.size() - 1U) / T{2};
  for (std::size_t index = 0; index < output.size(); ++index) {
    exponent_input(index) = (std::abs(static_cast<T>(index) - center) - radius) / width;
  }

  require_backend("mkl_vml_fermi_window", [&] {
    return ksj::special::detail::intel::exp(ksj::array::as_const_view(exponent_input.view()), output);
  });

  for (std::size_t index = 0; index < output.size(); ++index) {
    output(index) = T{1} / (T{1} + output(index));
  }
}

template <typename T>
void mkl_vml_fermi_bandpass_window(ksj::array::VectorView<T> output, ksj::array::PooledVector<T>& highpass_input,
                                   ksj::array::PooledVector<T>& lowpass_input,
                                   ksj::array::PooledVector<T>& highpass_exp, ksj::array::PooledVector<T>& lowpass_exp,
                                   const T low_radius, const T high_radius, const T width) {
  if (low_radius < T{} || high_radius <= low_radius) {
    throw std::invalid_argument("mkl_vml_fermi_bandpass_window requires 0 <= low_radius < high_radius");
  }
  if (width <= T{}) {
    throw std::invalid_argument("mkl_vml_fermi_bandpass_window width must be positive");
  }
  if (output.empty()) {
    return;
  }

  const auto center = static_cast<T>(output.size() - 1U) / T{2};
  for (std::size_t index = 0; index < output.size(); ++index) {
    const auto distance = std::abs(static_cast<T>(index) - center);
    highpass_input(index) = (low_radius - distance) / width;
    lowpass_input(index) = (distance - high_radius) / width;
  }

  require_backend("mkl_vml_fermi_bandpass_window", [&] {
    return ksj::special::detail::intel::exp(ksj::array::as_const_view(highpass_input.view()), highpass_exp.view());
  });
  require_backend("mkl_vml_fermi_bandpass_window", [&] {
    return ksj::special::detail::intel::exp(ksj::array::as_const_view(lowpass_input.view()), lowpass_exp.view());
  });

  for (std::size_t index = 0; index < output.size(); ++index) {
    const auto highpass = T{1} / (T{1} + highpass_exp(index));
    const auto lowpass = T{1} / (T{1} + lowpass_exp(index));
    output(index) = highpass * lowpass;
  }
}

template <typename T>
void mkl_vml_dual_fermi_band_window(ksj::array::VectorView<T> output, ksj::array::PooledVector<T>& left_input,
                                    ksj::array::PooledVector<T>& right_input, ksj::array::PooledVector<T>& left_exp,
                                    ksj::array::PooledVector<T>& right_exp, const T center_offset, const T radius,
                                    const T width) {
  if (center_offset < T{} || radius < T{}) {
    throw std::invalid_argument("mkl_vml_dual_fermi_band_window center_offset and radius must be non-negative");
  }
  if (width <= T{}) {
    throw std::invalid_argument("mkl_vml_dual_fermi_band_window width must be positive");
  }
  if (output.empty()) {
    return;
  }

  const auto center = static_cast<T>(output.size() - 1U) / T{2};
  const auto left_center = center - center_offset;
  const auto right_center = center + center_offset;
  for (std::size_t index = 0; index < output.size(); ++index) {
    const auto position = static_cast<T>(index);
    left_input(index) = (std::abs(position - left_center) - radius) / width;
    right_input(index) = (std::abs(position - right_center) - radius) / width;
  }

  require_backend("mkl_vml_dual_fermi_band_window", [&] {
    return ksj::special::detail::intel::exp(ksj::array::as_const_view(left_input.view()), left_exp.view());
  });
  require_backend("mkl_vml_dual_fermi_band_window", [&] {
    return ksj::special::detail::intel::exp(ksj::array::as_const_view(right_input.view()), right_exp.view());
  });

  for (std::size_t index = 0; index < output.size(); ++index) {
    const auto left = T{1} / (T{1} + left_exp(index));
    const auto right = T{1} / (T{1} + right_exp(index));
    output(index) = std::max(left, right);
  }
}

template <typename T>
void manual_phase_table(ksj::array::VectorView<const T> phase, ksj::array::VectorView<T> sin_output,
                        ksj::array::VectorView<T> cos_output) {
  for (std::size_t index = 0; index < phase.size(); ++index) {
    const auto value = phase(index);
    sin_output(index) = std::sin(value);
    cos_output(index) = std::cos(value);
  }
}

template <typename T>
void mkl_vml_phase_table(ksj::array::VectorView<const T> phase, ksj::array::VectorView<T> sin_output,
                         ksj::array::VectorView<T> cos_output) {
  require_backend("mkl_vml_phase_table", [&] {
    return ksj::special::detail::intel::sin(phase, sin_output);
  });
  require_backend("mkl_vml_phase_table", [&] {
    return ksj::special::detail::intel::cos(phase, cos_output);
  });
}

template <typename T>
void manual_complex_phase_table(ksj::array::ImageView<const T> phase, ksj::array::ImageView<std::complex<T>> sin_phase,
                                ksj::array::ImageView<std::complex<T>> cos_phase) {
  for (std::size_t row = 0; row < phase.rows(); ++row) {
    for (std::size_t col = 0; col < phase.cols(); ++col) {
      const auto value = phase(row, col);
      sin_phase(row, col) = {std::sin(value), T{}};
      cos_phase(row, col) = {std::cos(value), T{}};
    }
  }
}

template <typename T>
void mkl_vml_complex_phase_table(ksj::array::ImageView<const T> phase, ksj::array::VectorView<T> sin_real,
                                 ksj::array::VectorView<T> cos_real, ksj::array::ImageView<std::complex<T>> sin_phase,
                                 ksj::array::ImageView<std::complex<T>> cos_phase) {
  const auto phase_flat = ksj::array::VectorView<const T>(phase.data(), phase.size());
  mkl_vml_phase_table<T>(phase_flat, sin_real, cos_real);

  for (std::size_t index = 0; index < phase.size(); ++index) {
    sin_phase.data()[index] = {sin_real(index), T{}};
    cos_phase.data()[index] = {cos_real(index), T{}};
  }
}

template <typename T>
void run_standard_window_cases(std::string_view type_name, const ksj::benchmarks::Config& config,
                               const std::size_t size) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  auto phase = ksj::array::make_pooled_vector<T>(size);
  auto phase2 = ksj::array::make_pooled_vector<T>(size);
  auto cos1 = ksj::array::make_pooled_vector<T>(size);
  auto cos2 = ksj::array::make_pooled_vector<T>(size);
  ksj::benchmarks::require_pooled_storage("window_output", output);
  ksj::benchmarks::require_pooled_storage("window_phase", phase);
  ksj::benchmarks::require_pooled_storage("window_phase2", phase2);
  ksj::benchmarks::require_pooled_storage("window_cos1", cos1);
  ksj::benchmarks::require_pooled_storage("window_cos2", cos2);

  for (const auto kind :
       {ksj::signal::WindowKind::hann, ksj::signal::WindowKind::hamming, ksj::signal::WindowKind::blackman}) {
    const auto case_name = std::string{"window_"} + std::string{window_kind_name(kind)};
    print_vector_measurement<T>(case_name, "eigen_detail", type_name, size, config, output, [&] {
      ksj::signal::detail::eigen::window(output.view(), kind);
    });
    print_vector_measurement<T>(case_name, "intel_ipp_detail", type_name, size, config, output, [&] {
      require_backend("intel_ipp_window", [&] {
        return ksj::signal::detail::intel::window(output.view(), kind);
      });
    });
    print_vector_measurement<T>(case_name, "mkl_vml_candidate", type_name, size, config, output, [&] {
      mkl_vml_cos_window<T>(output.view(), kind, phase, phase2, cos1, cos2);
    });
    print_vector_measurement<T>(case_name, "public_policy", type_name, size, config, output, [&] {
      ksj::signal::window(output.view(), kind);
    });
  }
}

template <typename T>
void run_exponential_window_cases(std::string_view type_name, const ksj::benchmarks::Config& config,
                                  const std::size_t size) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  auto input_a = ksj::array::make_pooled_vector<T>(size);
  auto input_b = ksj::array::make_pooled_vector<T>(size);
  auto scratch_a = ksj::array::make_pooled_vector<T>(size);
  auto scratch_b = ksj::array::make_pooled_vector<T>(size);
  ksj::benchmarks::require_pooled_storage("exponential_window_output", output);
  ksj::benchmarks::require_pooled_storage("exponential_window_input_a", input_a);
  ksj::benchmarks::require_pooled_storage("exponential_window_input_b", input_b);
  ksj::benchmarks::require_pooled_storage("exponential_window_scratch_a", scratch_a);
  ksj::benchmarks::require_pooled_storage("exponential_window_scratch_b", scratch_b);

  print_vector_measurement<T>("window_exponential", "eigen_detail", type_name, size, config, output, [&] {
    ksj::signal::detail::eigen::exponential_window(output.view(), T{8}, T{2});
  });
  print_vector_measurement<T>("window_exponential", "mkl_vml_candidate", type_name, size, config, output, [&] {
    mkl_vml_exponential_window<T>(output.view(), input_a, T{8}, T{2});
  });
  print_vector_measurement<T>("window_exponential", "public_api", type_name, size, config, output, [&] {
    ksj::signal::exponential_window(output.view(), T{8}, T{2});
  });

  const auto fermi_radius = static_cast<T>(size) * static_cast<T>(0.35);
  print_vector_measurement<T>("window_fermi", "eigen_detail", type_name, size, config, output, [&] {
    ksj::signal::detail::eigen::fermi_window(output.view(), fermi_radius, T{4});
  });
  print_vector_measurement<T>("window_fermi", "mkl_vml_candidate", type_name, size, config, output, [&] {
    mkl_vml_fermi_window<T>(output.view(), input_a, fermi_radius, T{4});
  });
  print_vector_measurement<T>("window_fermi", "public_api", type_name, size, config, output, [&] {
    ksj::signal::fermi_window(output.view(), fermi_radius, T{4});
  });

  const auto low_radius = static_cast<T>(size) * static_cast<T>(0.12);
  const auto high_radius = static_cast<T>(size) * static_cast<T>(0.36);
  print_vector_measurement<T>("window_fermi_bandpass", "eigen_detail", type_name, size, config, output, [&] {
    ksj::signal::detail::eigen::fermi_bandpass_window(output.view(), low_radius, high_radius, T{4});
  });
  print_vector_measurement<T>("window_fermi_bandpass", "mkl_vml_candidate", type_name, size, config, output, [&] {
    mkl_vml_fermi_bandpass_window<T>(output.view(), input_a, input_b, scratch_a, scratch_b, low_radius, high_radius,
                                     T{4});
  });
  print_vector_measurement<T>("window_fermi_bandpass", "public_api", type_name, size, config, output, [&] {
    ksj::signal::fermi_bandpass_window(output.view(), low_radius, high_radius, T{4});
  });

  const auto center_offset = static_cast<T>(size) * static_cast<T>(0.2);
  const auto dual_radius = static_cast<T>(size) * static_cast<T>(0.06);
  print_vector_measurement<T>("window_dual_fermi_band", "eigen_detail", type_name, size, config, output, [&] {
    ksj::signal::detail::eigen::dual_fermi_band_window(output.view(), center_offset, dual_radius, T{3});
  });
  print_vector_measurement<T>("window_dual_fermi_band", "mkl_vml_candidate", type_name, size, config, output, [&] {
    mkl_vml_dual_fermi_band_window<T>(output.view(), input_a, input_b, scratch_a, scratch_b, center_offset, dual_radius,
                                      T{3});
  });
  print_vector_measurement<T>("window_dual_fermi_band", "public_api", type_name, size, config, output, [&] {
    ksj::signal::dual_fermi_band_window(output.view(), center_offset, dual_radius, T{3});
  });
}

template <typename T>
void run_phase_table_cases(std::string_view type_name, const ksj::benchmarks::Config& config, const std::size_t size) {
  auto phase = ksj::array::make_pooled_vector<T>(size);
  auto sin_output = ksj::array::make_pooled_vector<T>(size);
  auto cos_output = ksj::array::make_pooled_vector<T>(size);
  ksj::benchmarks::require_pooled_storage("phase", phase);
  ksj::benchmarks::require_pooled_storage("sin_output", sin_output);
  ksj::benchmarks::require_pooled_storage("cos_output", cos_output);
  fill_phase_vector(phase);

  const auto phase_view = ksj::array::as_const_view(phase.view());
  print_pair_measurement<T>("phase_table_sincos_real", "manual_fused", type_name, size, config, sin_output, cos_output,
                            [&] {
                              manual_phase_table<T>(phase_view, sin_output.view(), cos_output.view());
                            });
  print_pair_measurement<T>("phase_table_sincos_real", "mkl_vml_detail", type_name, size, config, sin_output,
                            cos_output, [&] {
                              mkl_vml_phase_table<T>(phase_view, sin_output.view(), cos_output.view());
                            });

  const auto shape = element_count_shape2d(size);
  auto phase_image = ksj::array::make_pooled_image<T>(shape.rows, shape.cols);
  auto sin_real = ksj::array::make_pooled_vector<T>(size);
  auto cos_real = ksj::array::make_pooled_vector<T>(size);
  auto sin_phase = ksj::array::make_pooled_image<std::complex<T>>(shape.rows, shape.cols);
  auto cos_phase = ksj::array::make_pooled_image<std::complex<T>>(shape.rows, shape.cols);
  ksj::benchmarks::require_pooled_storage("phase_image", phase_image);
  ksj::benchmarks::require_pooled_storage("sin_real", sin_real);
  ksj::benchmarks::require_pooled_storage("cos_real", cos_real);
  ksj::benchmarks::require_pooled_storage("sin_phase", sin_phase);
  ksj::benchmarks::require_pooled_storage("cos_phase", cos_phase);
  fill_phase_image(phase_image);

  const auto phase_image_view = ksj::array::as_const_view(phase_image.view());
  print_complex_pair_measurement<T>(
    "phase_table_sincos_complex_image", "manual_fused", type_name, size, config, sin_phase, cos_phase, [&] {
      manual_complex_phase_table<T>(phase_image_view, sin_phase.view(), cos_phase.view());
    });
  print_complex_pair_measurement<T>("phase_table_sincos_complex_image", "mkl_vml_real_then_complex", type_name, size,
                                    config, sin_phase, cos_phase, [&] {
                                      mkl_vml_complex_phase_table<T>(phase_image_view, sin_real.view(), cos_real.view(),
                                                                     sin_phase.view(), cos_phase.view());
                                    });
}

template <typename T> void run_for_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    run_standard_window_cases<T>(type_name, config, size);
    run_exponential_window_cases<T>(type_name, config, size);
    run_phase_table_cases<T>(type_name, config, size);
  }
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(
    argc, argv, config, "usage: ksj_signal_vml_candidate_benchmark [--iterations N] [--sizes 64,256,1024,4096,16384]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
