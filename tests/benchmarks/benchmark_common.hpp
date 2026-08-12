#pragma once

#include "eigen_benchmark_adapter.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/memory/memory_broker.hpp"
#include "kspacejet/memory/memory_space.hpp"
#include "kspacejet/numerics/runtime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ksj::benchmarks {

enum class OutputFormat {
  table,
  csv,
};

struct Config {
  std::vector<std::size_t> sizes{16, 32, 64, 128, 256, 512};
  std::size_t iterations{20};
  std::size_t trials{5};
  std::size_t min_sample_time_ns{1'000'000U};
};

struct Measurement {
  double mean_ns{0.0};
  double median_ns{0.0};
  double stddev_ns{0.0};
  double ci95_low_ns{0.0};
  double ci95_high_ns{0.0};
  double min_ns{0.0};
  double max_ns{0.0};
  std::size_t effective_iterations{0U};
};

enum class RowRole {
  oracle,
  reference,
  candidate,
  policy,
};

struct RowMetadata {
  constexpr RowMetadata(std::string_view comparison_group_value, std::string_view timing_scope_value,
                        const RowRole role_value, std::string_view selected_backend_value = {},
                        const double absolute_tolerance_value = -1.0, const double relative_tolerance_value = -1.0)
      : comparison_group(comparison_group_value), timing_scope(timing_scope_value), role(role_value),
        selected_backend(selected_backend_value), absolute_tolerance(absolute_tolerance_value),
        relative_tolerance(relative_tolerance_value) {}

  std::string_view comparison_group;
  std::string_view timing_scope;
  RowRole role;
  std::string_view selected_backend;
  double absolute_tolerance{-1.0};
  double relative_tolerance{-1.0};
};

[[nodiscard]] inline constexpr RowMetadata reference_row(std::string_view comparison_group,
                                                         std::string_view timing_scope,
                                                         const double absolute_tolerance = -1.0,
                                                         const double relative_tolerance = -1.0) {
  return {comparison_group, timing_scope, RowRole::reference, {}, absolute_tolerance, relative_tolerance};
}

[[nodiscard]] inline constexpr RowMetadata oracle_row(std::string_view comparison_group, std::string_view timing_scope,
                                                      const double absolute_tolerance = -1.0,
                                                      const double relative_tolerance = -1.0) {
  return {comparison_group, timing_scope, RowRole::oracle, {}, absolute_tolerance, relative_tolerance};
}

[[nodiscard]] inline constexpr RowMetadata candidate_row(std::string_view comparison_group,
                                                         std::string_view timing_scope,
                                                         const double absolute_tolerance = -1.0,
                                                         const double relative_tolerance = -1.0) {
  return {comparison_group, timing_scope, RowRole::candidate, {}, absolute_tolerance, relative_tolerance};
}

[[nodiscard]] inline constexpr RowMetadata policy_row(std::string_view comparison_group, std::string_view timing_scope,
                                                      std::string_view selected_backend,
                                                      const double absolute_tolerance = -1.0,
                                                      const double relative_tolerance = -1.0) {
  return {comparison_group, timing_scope, RowRole::policy, selected_backend, absolute_tolerance, relative_tolerance};
}

inline OutputFormat& output_format() {
  static OutputFormat format = OutputFormat::table;
  return format;
}

inline std::filesystem::path& report_path() {
  static std::filesystem::path path;
  return path;
}

inline std::ofstream& report_stream() {
  static std::ofstream stream;
  return stream;
}

inline void initialize_numerics_runtime() {
  static const bool configured = [] {
    ksj::numerics::initialize_numerics_runtime();
    return true;
  }();
  (void)configured;
}

inline void configure_direct_memory_broker() {
  static const bool configured = [] {
    ksj::memory::MemoryPoolOptions options;
    options.pooling_enabled = false;
    if (!ksj::memory::MemoryBroker::configure_instance(std::move(options))) {
      throw std::runtime_error("kspacejet-memory benchmark configured memory broker too late");
    }
    return true;
  }();
  (void)configured;
}

[[nodiscard]] inline bool parse_size(std::string_view text, std::size_t& value) {
  std::size_t parsed = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return false;
  }
  value = parsed;
  return true;
}

[[nodiscard]] inline std::vector<std::size_t> parse_sizes(std::string_view text) {
  std::vector<std::size_t> sizes;
  while (!text.empty()) {
    const auto comma = text.find(',');
    const auto token = text.substr(0, comma);
    std::size_t value = 0;
    if (!parse_size(token, value) || value == 0U) {
      std::cerr << "invalid --sizes value\n";
      std::exit(2);
    }
    sizes.push_back(value);
    if (comma == std::string_view::npos) {
      break;
    }
    text.remove_prefix(comma + 1U);
  }
  return sizes;
}

[[nodiscard]] inline std::filesystem::path executable_path(char** argv) {
#if defined(__linux__)
  std::error_code error;
  const auto self_path = std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error && !self_path.empty()) {
    return self_path;
  }
#endif
  return std::filesystem::absolute(std::filesystem::path(argv[0]));
}

[[nodiscard]] inline std::string_view report_extension() {
  return output_format() == OutputFormat::csv ? ".csv" : ".txt";
}

inline void open_report_file(char** argv) {
  const auto exe_path = executable_path(argv);
  const auto report_dir = exe_path.parent_path() / "reports";
  std::error_code error;
  std::filesystem::create_directories(report_dir, error);
  if (error) {
    throw std::runtime_error("failed to create report directory: " + report_dir.string() + ": " + error.message());
  }

  report_path() = report_dir / (exe_path.filename().string() + std::string(report_extension()));
  report_stream().open(report_path(), std::ios::out | std::ios::trunc);
  if (!report_stream()) {
    throw std::runtime_error("failed to open report file: " + report_path().string());
  }
}

inline void parse_args(int argc, char** argv, Config& config, std::string_view usage) {
  configure_direct_memory_broker();

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--iterations") {
      if (i + 1 >= argc || !parse_size(argv[i + 1], config.iterations) || config.iterations == 0U) {
        std::cerr << "invalid --iterations value\n";
        std::exit(2);
      }
      ++i;
    } else if (arg == "--trials") {
      if (i + 1 >= argc || !parse_size(argv[i + 1], config.trials) || config.trials == 0U) {
        std::cerr << "invalid --trials value\n";
        std::exit(2);
      }
      ++i;
    } else if (arg == "--min-sample-time-us") {
      std::size_t microseconds = 0U;
      if (i + 1 >= argc || !parse_size(argv[i + 1], microseconds) ||
          microseconds > std::numeric_limits<std::size_t>::max() / 1000U) {
        std::cerr << "invalid --min-sample-time-us value\n";
        std::exit(2);
      }
      config.min_sample_time_ns = microseconds * 1000U;
      ++i;
    } else if (arg == "--sizes") {
      if (i + 1 >= argc) {
        std::cerr << "missing --sizes value\n";
        std::exit(2);
      }
      config.sizes = parse_sizes(argv[++i]);
    } else if (arg == "--csv") {
      output_format() = OutputFormat::csv;
    } else if (arg == "--format") {
      if (i + 1 >= argc) {
        std::cerr << "missing --format value\n";
        std::exit(2);
      }
      const std::string_view value(argv[++i]);
      if (value == "table" || value == "txt") {
        output_format() = OutputFormat::table;
      } else if (value == "csv") {
        output_format() = OutputFormat::csv;
      } else {
        std::cerr << "invalid --format value: " << value << '\n';
        std::exit(2);
      }
    } else if (arg == "--help") {
      std::cout << usage << '\n';
      std::cout << "  --trials N\n";
      std::cout << "  --min-sample-time-us N\n";
      std::cout << "  --format table|csv\n";
      std::cout << "  --csv\n";
      std::cout << "  report: <executable-dir>/reports/<executable-name>.txt|.csv\n";
      std::exit(0);
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      std::exit(2);
    }
  }

  open_report_file(argv);
}

struct BatchTiming {
  double elapsed_ns{0.0};
  double ns_per_iteration{0.0};
};

template <typename Function> [[nodiscard]] BatchTiming time_batch(const std::size_t iterations, Function&& function) {
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    function();
    asm volatile("" ::: "memory");
  }
  const auto end = std::chrono::steady_clock::now();
  const auto elapsed_ns = std::chrono::duration<double, std::nano>(end - start).count();
  return {elapsed_ns, elapsed_ns / static_cast<double>(iterations)};
}

template <typename Function>
[[nodiscard]] std::size_t calibrated_iterations(const Config& config, Function&& function) {
  function();
  auto iterations = config.iterations;
  if (config.min_sample_time_ns == 0U) {
    return iterations;
  }

  auto timing = time_batch(iterations, function);
  while (timing.elapsed_ns < static_cast<double>(config.min_sample_time_ns)) {
    const auto target_ratio =
      timing.elapsed_ns > 0.0
        ? static_cast<std::size_t>(std::ceil(static_cast<double>(config.min_sample_time_ns) / timing.elapsed_ns))
        : 2U;
    const auto growth = std::clamp<std::size_t>(target_ratio, 2U, 1024U);
    if (iterations > std::numeric_limits<std::size_t>::max() / growth) {
      break;
    }
    iterations *= growth;
    timing = time_batch(iterations, function);
  }
  return iterations;
}

template <typename Function> [[nodiscard]] Measurement measure(const Config& config, Function&& function) {
  std::vector<double> samples;
  samples.reserve(config.trials);
  const auto iterations = calibrated_iterations(config, function);
  for (std::size_t trial = 0; trial < config.trials; ++trial) {
    samples.push_back(time_batch(iterations, function).ns_per_iteration);
  }

  const auto sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  const auto [min_it, max_it] = std::minmax_element(samples.begin(), samples.end());
  const double mean = sum / static_cast<double>(samples.size());

  auto sorted_samples = samples;
  std::sort(sorted_samples.begin(), sorted_samples.end());
  const std::size_t midpoint = sorted_samples.size() / 2U;
  const double median = sorted_samples.size() % 2U == 0U
                          ? (sorted_samples[midpoint - 1U] + sorted_samples[midpoint]) * 0.5
                          : sorted_samples[midpoint];

  double squared_error_sum = 0.0;
  for (const double sample : samples) {
    const double error = sample - mean;
    squared_error_sum += error * error;
  }
  const double stddev =
    samples.size() > 1U ? std::sqrt(squared_error_sum / static_cast<double>(samples.size() - 1U)) : 0.0;

  // Two-sided Student's t critical values for a 95% confidence interval of the mean.
  constexpr std::array<double, 31> t95{
    0.0,   12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228, 2.201, 2.179, 2.160, 2.145, 2.131,
    2.120, 2.110,  2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042,
  };
  const std::size_t degrees_of_freedom = samples.size() > 1U ? samples.size() - 1U : 0U;
  const double critical_value =
    degrees_of_freedom == 0U ? 0.0 : (degrees_of_freedom < t95.size() ? t95[degrees_of_freedom] : 1.96);
  const double margin = critical_value * stddev / std::sqrt(static_cast<double>(samples.size()));

  return Measurement{mean, median, stddev, mean - margin, mean + margin, *min_it, *max_it, iterations};
}

template <typename T> inline void do_not_optimize(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "g"(value) : "memory");
#else
  volatile const T* sink = &value;
  (void)sink;
#endif
}

inline void require_pooled_pointer(std::string_view name, const void* ptr) {
  if (ptr == nullptr) {
    throw std::runtime_error("benchmark object has null pooled storage: " + std::string{name});
  }

  const auto address = reinterpret_cast<std::uintptr_t>(ptr);
  const auto alignment = ksj::memory::NumaHostSpace::cache_line_size();
  if (address % alignment != 0U) {
    throw std::runtime_error("benchmark object is not cache-line aligned: " + std::string{name});
  }
}

template <typename T> void require_pooled_storage(std::string_view name, const ksj::array::PooledVector<T>& vector) {
  if (!vector.empty()) {
    require_pooled_pointer(name, vector.data());
  }
}

template <typename T> void require_pooled_storage(std::string_view name, const ksj::array::PooledMatrix<T>& matrix) {
  if (!matrix.empty()) {
    require_pooled_pointer(name, matrix.data());
  }
}

template <typename T> void require_pooled_storage(std::string_view name, const ksj::array::PooledImage<T>& image) {
  if (!image.empty()) {
    require_pooled_pointer(name, image.data());
  }
}

template <typename T> void require_pooled_storage(std::string_view name, const ksj::array::PooledCube<T>& cube) {
  if (!cube.empty()) {
    require_pooled_pointer(name, cube.data());
  }
}

template <typename T> void require_pooled_storage(std::string_view name, const ksj::array::PooledArray4D<T>& array) {
  if (!array.empty()) {
    require_pooled_pointer(name, array.data());
  }
}

template <typename T> void fill_vector(ksj::array::PooledVector<T>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    vector(i) = static_cast<T>(static_cast<double>((i % 251U) + 1U) * 0.125);
  }
}

template <typename T> void fill_vector(ksj::array::PooledVector<std::complex<T>>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    const auto real = static_cast<T>(static_cast<double>((i % 251U) + 1U) * 0.125);
    const auto imag = static_cast<T>(static_cast<double>((i % 127U) + 1U) * 0.0625);
    vector(i) = {real, imag};
  }
}

template <typename T> void fill_matrix(ksj::array::PooledMatrix<T>& matrix) {
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      matrix(row, col) =
        static_cast<T>(static_cast<double>((row % 251U) + 1U) * 0.25 + static_cast<double>((col % 127U) + 1U) * 0.125);
    }
  }
}

template <typename T> void fill_image(ksj::array::PooledImage<T>& image) {
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<T>(static_cast<double>((row * 17U + col * 31U) % 101U) / 100.0);
    }
  }
}

template <typename T> void fill_cube(ksj::array::PooledCube<T>& cube) {
  for (std::size_t dim0 = 0; dim0 < cube.dim0(); ++dim0) {
    for (std::size_t dim1 = 0; dim1 < cube.dim1(); ++dim1) {
      for (std::size_t dim2 = 0; dim2 < cube.dim2(); ++dim2) {
        cube(dim0, dim1, dim2) =
          static_cast<T>(static_cast<double>((dim0 * 17U + dim1 * 31U + dim2 * 7U) % 101U) / 100.0 + 1.0);
      }
    }
  }
}

template <typename T> void fill_array4d(ksj::array::PooledArray4D<T>& array) {
  for (std::size_t dim0 = 0; dim0 < array.dim0(); ++dim0) {
    for (std::size_t dim1 = 0; dim1 < array.dim1(); ++dim1) {
      for (std::size_t dim2 = 0; dim2 < array.dim2(); ++dim2) {
        for (std::size_t dim3 = 0; dim3 < array.dim3(); ++dim3) {
          array(dim0, dim1, dim2, dim3) = static_cast<T>(
            static_cast<double>((dim0 * 19U + dim1 * 17U + dim2 * 31U + dim3 * 7U) % 101U) / 100.0 + 1.0);
        }
      }
    }
  }
}

template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledVector<T>& vector) {
  double value = 0.0;
  for (std::size_t index = 0; index < vector.size(); ++index) {
    value += static_cast<double>(vector.data()[index]);
  }
  return value;
}

template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledVector<std::complex<T>>& vector) {
  double value = 0.0;
  for (std::size_t index = 0; index < vector.size(); ++index) {
    value += static_cast<double>(vector.data()[index].real()) + static_cast<double>(vector.data()[index].imag());
  }
  return value;
}

template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledMatrix<T>& matrix) {
  double value = 0.0;
  for (std::size_t index = 0; index < matrix.size(); ++index) {
    value += static_cast<double>(matrix.data()[index]);
  }
  return value;
}

template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledImage<T>& image) {
  double value = 0.0;
  for (std::size_t index = 0; index < image.size(); ++index) {
    value += static_cast<double>(image.data()[index]);
  }
  return value;
}

template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledCube<T>& cube) {
  double value = 0.0;
  for (std::size_t index = 0; index < cube.size(); ++index) {
    value += static_cast<double>(cube.data()[index]);
  }
  return value;
}

template <typename T> [[nodiscard]] double checksum(const ksj::array::PooledArray4D<T>& array) {
  double value = 0.0;
  for (std::size_t index = 0; index < array.size(); ++index) {
    value += static_cast<double>(array.data()[index]);
  }
  return value;
}

inline constexpr std::string_view kTableGap = "  ";
inline constexpr int kCaseColumnWidth = 32;
inline constexpr int kBackendColumnWidth = 16;
inline constexpr int kTypeColumnWidth = 14;
inline constexpr int kRoleColumnWidth = 10;
inline constexpr int kScopeColumnWidth = 16;
inline constexpr int kSizeColumnWidth = 7;
inline constexpr int kIterationsColumnWidth = 10;
inline constexpr int kTrialsColumnWidth = 6;
inline constexpr int kTimingColumnWidth = 12;
inline constexpr int kChecksumColumnWidth = 16;
inline constexpr int kTableColumnCount = 14;
inline constexpr int kTableWidth = kCaseColumnWidth + kBackendColumnWidth + kTypeColumnWidth + kRoleColumnWidth +
                                   kScopeColumnWidth + kSizeColumnWidth + kIterationsColumnWidth + kTrialsColumnWidth +
                                   (kTimingColumnWidth * 5) + kChecksumColumnWidth +
                                   (static_cast<int>(kTableGap.size()) * (kTableColumnCount - 1));

[[nodiscard]] inline constexpr std::string_view row_role_name(const RowRole role) {
  switch (role) {
    case RowRole::oracle:
      return "oracle";
    case RowRole::reference:
      return "reference";
    case RowRole::candidate:
      return "candidate";
    case RowRole::policy:
      return "policy";
  }
  throw std::logic_error("unknown benchmark row role");
}

inline void write_csv_header(std::ostream& output) {
  output << "case,comparison_group,backend,role,selected_backend,timing_scope,type,size,iterations,trials,mean_ns,"
            "median_ns,stddev_ns,ci95_low_ns,ci95_high_ns,min_ns,max_ns,checksum,abs_tolerance,rel_tolerance\n";
}

inline void write_table_header(std::ostream& output) {
  output << std::left << std::setw(kCaseColumnWidth) << "case" << kTableGap << std::setw(kBackendColumnWidth)
         << "backend" << kTableGap << std::setw(kTypeColumnWidth) << "type" << kTableGap << std::setw(kRoleColumnWidth)
         << "role" << kTableGap << std::setw(kScopeColumnWidth) << "scope" << kTableGap << std::right
         << std::setw(kSizeColumnWidth) << "size" << kTableGap << std::setw(kIterationsColumnWidth) << "iterations"
         << kTableGap << std::setw(kTrialsColumnWidth) << "trials" << kTableGap << std::setw(kTimingColumnWidth)
         << "median(ns)" << kTableGap << std::setw(kTimingColumnWidth) << "mean(ns)" << kTableGap
         << std::setw(kTimingColumnWidth) << "ci95 low" << kTableGap << std::setw(kTimingColumnWidth) << "ci95 high"
         << kTableGap << std::setw(kTimingColumnWidth) << "stddev(ns)" << kTableGap << std::setw(kChecksumColumnWidth)
         << "checksum" << '\n';
  output << std::string(kTableWidth, '-') << '\n';
}

inline void print_header() {
  if (output_format() == OutputFormat::csv) {
    write_csv_header(std::cout);
    if (report_stream()) {
      write_csv_header(report_stream());
    }
    return;
  }

  write_table_header(std::cout);
  if (report_stream()) {
    write_table_header(report_stream());
  }
}

inline void write_csv_row(std::ostream& output, std::string_view case_name, std::string_view backend,
                          std::string_view type_name, const std::size_t size, const std::size_t iterations,
                          const std::size_t trials, const Measurement& measurement, const double checksum_value,
                          const RowMetadata& metadata) {
  output << std::setprecision(std::numeric_limits<double>::max_digits10);
  output << case_name << ',' << metadata.comparison_group << ',' << backend << ',' << row_role_name(metadata.role)
         << ',' << metadata.selected_backend << ',' << metadata.timing_scope << ',' << type_name << ',' << size << ','
         << iterations << ',' << trials << ',' << measurement.mean_ns << ',' << measurement.median_ns << ','
         << measurement.stddev_ns << ',' << measurement.ci95_low_ns << ',' << measurement.ci95_high_ns << ','
         << measurement.min_ns << ',' << measurement.max_ns << ',' << checksum_value << ','
         << metadata.absolute_tolerance << ',' << metadata.relative_tolerance << '\n';
}

inline void write_progress(std::string_view case_name, std::string_view backend, std::string_view type_name,
                           const std::size_t size) {
  if (output_format() != OutputFormat::csv) {
    return;
  }
  std::cerr << "[kspacejet-benchmark] completed case=" << case_name << " backend=" << backend << " type=" << type_name
            << " size=" << size << '\n';
}

inline void write_table_row(std::ostream& output, std::string_view case_name, std::string_view backend,
                            std::string_view type_name, const std::size_t size, const std::size_t iterations,
                            const std::size_t trials, const Measurement& measurement, const double checksum_value,
                            const RowMetadata& metadata) {
  output << std::left << std::setw(kCaseColumnWidth) << case_name << kTableGap << std::setw(kBackendColumnWidth)
         << backend << kTableGap << std::setw(kTypeColumnWidth) << type_name << kTableGap << std::setw(kRoleColumnWidth)
         << row_role_name(metadata.role) << kTableGap << std::setw(kScopeColumnWidth) << metadata.timing_scope
         << kTableGap << std::right << std::setw(kSizeColumnWidth) << size << kTableGap
         << std::setw(kIterationsColumnWidth) << iterations << kTableGap << std::setw(kTrialsColumnWidth) << trials
         << kTableGap << std::fixed << std::setprecision(2) << std::setw(kTimingColumnWidth) << measurement.median_ns
         << kTableGap << std::setw(kTimingColumnWidth) << measurement.mean_ns << kTableGap
         << std::setw(kTimingColumnWidth) << measurement.ci95_low_ns << kTableGap << std::setw(kTimingColumnWidth)
         << measurement.ci95_high_ns << kTableGap << std::setw(kTimingColumnWidth) << measurement.stddev_ns << kTableGap
         << std::setprecision(6) << std::setw(kChecksumColumnWidth) << checksum_value << '\n';
}

inline void print_row(std::string_view case_name, std::string_view backend, std::string_view type_name,
                      const std::size_t size, const std::size_t iterations, const Measurement& measurement,
                      const double checksum_value, const RowMetadata& metadata) {
  if (output_format() == OutputFormat::csv) {
    write_csv_row(std::cout, case_name, backend, type_name, size, iterations, 1U, measurement, checksum_value,
                  metadata);
    if (report_stream()) {
      write_csv_row(report_stream(), case_name, backend, type_name, size, iterations, 1U, measurement, checksum_value,
                    metadata);
    }
    write_progress(case_name, backend, type_name, size);
    return;
  }

  write_table_row(std::cout, case_name, backend, type_name, size, iterations, 1U, measurement, checksum_value,
                  metadata);
  if (report_stream()) {
    write_table_row(report_stream(), case_name, backend, type_name, size, iterations, 1U, measurement, checksum_value,
                    metadata);
  }
}

inline void print_row(std::string_view case_name, std::string_view backend, std::string_view type_name,
                      const std::size_t size, const Config& config, const Measurement& measurement,
                      const double checksum_value, const RowMetadata& metadata) {
  const auto iterations = measurement.effective_iterations == 0U ? config.iterations : measurement.effective_iterations;
  if (output_format() == OutputFormat::csv) {
    write_csv_row(std::cout, case_name, backend, type_name, size, iterations, config.trials, measurement,
                  checksum_value, metadata);
    if (report_stream()) {
      write_csv_row(report_stream(), case_name, backend, type_name, size, iterations, config.trials, measurement,
                    checksum_value, metadata);
    }
    write_progress(case_name, backend, type_name, size);
    return;
  }

  write_table_row(std::cout, case_name, backend, type_name, size, iterations, config.trials, measurement,
                  checksum_value, metadata);
  if (report_stream()) {
    write_table_row(report_stream(), case_name, backend, type_name, size, iterations, config.trials, measurement,
                    checksum_value, metadata);
  }
}

} // namespace ksj::benchmarks
