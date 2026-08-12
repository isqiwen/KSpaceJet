#pragma once

#include "eigen_research_adapter.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <Eigen/Dense>

#include "kspacejet/numerics/runtime.hpp"

namespace ksj::research::cpp_numerics_performance {

inline constexpr std::size_t kCacheLineAlignment = 64U;

template <typename T, std::size_t Alignment = kCacheLineAlignment> class AlignedAllocator {
public:
  using value_type = T;

  AlignedAllocator() noexcept = default;

  template <typename U> constexpr AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  [[nodiscard]] T* allocate(std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_array_new_length();
    }
    return static_cast<T*>(::operator new(count * sizeof(T), std::align_val_t{Alignment}));
  }

  void deallocate(T* pointer, std::size_t) noexcept { ::operator delete(pointer, std::align_val_t{Alignment}); }

  template <typename U> struct rebind {
    using other = AlignedAllocator<U, Alignment>;
  };
};

template <typename T, typename U, std::size_t Alignment>
[[nodiscard]] constexpr bool operator==(const AlignedAllocator<T, Alignment>&,
                                        const AlignedAllocator<U, Alignment>&) noexcept {
  return true;
}

template <typename T, typename U, std::size_t Alignment>
[[nodiscard]] constexpr bool operator!=(const AlignedAllocator<T, Alignment>& lhs,
                                        const AlignedAllocator<U, Alignment>& rhs) noexcept {
  return !(lhs == rhs);
}

template <typename T> class Vector {
public:
  using value_type = T;
  using storage_type = std::vector<T, AlignedAllocator<T>>;
  using eigen_map_type = Eigen::Map<Eigen::Matrix<T, Eigen::Dynamic, 1>, Eigen::Aligned>;
  using const_eigen_map_type = Eigen::Map<const Eigen::Matrix<T, Eigen::Dynamic, 1>, Eigen::Aligned>;

  Vector() = default;
  explicit Vector(std::size_t size) : storage_(size) {}

  [[nodiscard]] T* data() noexcept { return storage_.data(); }
  [[nodiscard]] const T* data() const noexcept { return storage_.data(); }
  [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }
  [[nodiscard]] bool empty() const noexcept { return storage_.empty(); }
  [[nodiscard]] std::size_t rows() const noexcept { return storage_.size(); }
  [[nodiscard]] std::size_t cols() const noexcept { return 1U; }

  [[nodiscard]] T& operator()(std::size_t index) noexcept { return storage_[index]; }
  [[nodiscard]] const T& operator()(std::size_t index) const noexcept { return storage_[index]; }

  [[nodiscard]] eigen_map_type as_eigen() noexcept { return eigen_map_type(data(), static_cast<Eigen::Index>(size())); }

  [[nodiscard]] const_eigen_map_type as_eigen() const noexcept {
    return const_eigen_map_type(data(), static_cast<Eigen::Index>(size()));
  }

private:
  storage_type storage_{};
};

template <typename T, int Layout> class Dense2D {
public:
  using value_type = T;
  using storage_type = std::vector<T, AlignedAllocator<T>>;
  using eigen_matrix_type = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Layout>;
  using eigen_map_type = Eigen::Map<eigen_matrix_type, Eigen::Aligned>;
  using const_eigen_map_type = Eigen::Map<const eigen_matrix_type, Eigen::Aligned>;

  Dense2D() = default;
  Dense2D(std::size_t rows, std::size_t cols) : storage_(rows * cols), rows_(rows), cols_(cols) {}

  [[nodiscard]] T* data() noexcept { return storage_.data(); }
  [[nodiscard]] const T* data() const noexcept { return storage_.data(); }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }
  [[nodiscard]] bool empty() const noexcept { return storage_.empty(); }

  [[nodiscard]] T& operator()(std::size_t row, std::size_t col) noexcept { return data()[offset(row, col)]; }

  [[nodiscard]] const T& operator()(std::size_t row, std::size_t col) const noexcept {
    return data()[offset(row, col)];
  }

  [[nodiscard]] eigen_map_type as_eigen() noexcept {
    return eigen_map_type(data(), static_cast<Eigen::Index>(rows_), static_cast<Eigen::Index>(cols_));
  }

  [[nodiscard]] const_eigen_map_type as_eigen() const noexcept {
    return const_eigen_map_type(data(), static_cast<Eigen::Index>(rows_), static_cast<Eigen::Index>(cols_));
  }

private:
  [[nodiscard]] std::size_t offset(std::size_t row, std::size_t col) const noexcept {
    if constexpr (Layout == Eigen::ColMajor) {
      return row + col * rows_;
    } else {
      return row * cols_ + col;
    }
  }

  storage_type storage_{};
  std::size_t rows_{0};
  std::size_t cols_{0};
};

template <typename T> using Matrix = Dense2D<T, Eigen::ColMajor>;
template <typename T> using Image = Dense2D<T, Eigen::RowMajor>;

template <typename T> [[nodiscard]] Vector<T> make_vector(std::size_t size) {
  return Vector<T>(size);
}

template <typename T> [[nodiscard]] Matrix<T> make_matrix(std::size_t rows, std::size_t cols) {
  return Matrix<T>(rows, cols);
}

template <typename T> [[nodiscard]] Image<T> make_image(std::size_t rows, std::size_t cols) {
  return Image<T>(rows, cols);
}

enum class OutputFormat {
  table,
  csv,
};

struct Config {
  std::vector<std::size_t> sizes{1024, 4096, 16384, 65536};
  std::vector<std::size_t> coils{8, 16, 32};
  std::size_t iterations{20};
  std::size_t trials{5};
  OutputFormat output_format{OutputFormat::table};
  std::filesystem::path report_path{};
  mutable std::ofstream report_stream{};
};

struct Measurement {
  double mean_ns{0.0};
  double min_ns{0.0};
  double max_ns{0.0};
};

inline void initialize_numerics_runtime() {
  static const bool configured = [] {
    ksj::numerics::initialize_numerics_runtime();
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

[[nodiscard]] inline std::vector<std::size_t> parse_sizes(std::string_view text, std::string_view option_name) {
  std::vector<std::size_t> values;
  while (!text.empty()) {
    const auto comma = text.find(',');
    const auto token = text.substr(0, comma);
    std::size_t value = 0;
    if (!parse_size(token, value) || value == 0U) {
      std::cerr << "invalid " << option_name << " value\n";
      std::exit(2);
    }
    values.push_back(value);
    if (comma == std::string_view::npos) {
      break;
    }
    text.remove_prefix(comma + 1U);
  }
  return values;
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

[[nodiscard]] inline std::string_view report_extension(OutputFormat output_format) {
  return output_format == OutputFormat::csv ? ".csv" : ".txt";
}

inline void open_report_file(char** argv, Config& config) {
  const auto exe_path = executable_path(argv);
  const auto report_dir = exe_path.parent_path() / "reports";
  std::error_code error;
  std::filesystem::create_directories(report_dir, error);
  if (error) {
    throw std::runtime_error("failed to create report directory: " + report_dir.string() + ": " + error.message());
  }

  config.report_path =
    report_dir / (exe_path.filename().string() + std::string(report_extension(config.output_format)));
  config.report_stream.open(config.report_path, std::ios::out | std::ios::trunc);
  if (!config.report_stream) {
    throw std::runtime_error("failed to open report file: " + config.report_path.string());
  }

  std::filesystem::permissions(config.report_path,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                 std::filesystem::perms::group_read | std::filesystem::perms::others_read,
                               std::filesystem::perm_options::replace, error);
}

inline void parse_args(int argc, char** argv, Config& config, std::string_view usage) {
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
    } else if (arg == "--sizes") {
      if (i + 1 >= argc) {
        std::cerr << "missing --sizes value\n";
        std::exit(2);
      }
      config.sizes = parse_sizes(argv[++i], "--sizes");
    } else if (arg == "--coils") {
      if (i + 1 >= argc) {
        std::cerr << "missing --coils value\n";
        std::exit(2);
      }
      config.coils = parse_sizes(argv[++i], "--coils");
    } else if (arg == "--csv") {
      config.output_format = OutputFormat::csv;
    } else if (arg == "--format") {
      if (i + 1 >= argc) {
        std::cerr << "missing --format value\n";
        std::exit(2);
      }
      const std::string_view value(argv[++i]);
      if (value == "table") {
        config.output_format = OutputFormat::table;
      } else if (value == "csv") {
        config.output_format = OutputFormat::csv;
      } else {
        std::cerr << "invalid --format value: " << value << '\n';
        std::exit(2);
      }
    } else if (arg == "--help") {
      std::cout << usage << '\n';
      std::cout << "  --format table|csv\n";
      std::cout << "  --csv\n";
      std::cout << "  report: <executable-dir>/reports/<executable-name>.txt|.csv\n";
      std::exit(0);
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      std::exit(2);
    }
  }

  open_report_file(argv, config);
}

template <typename T> inline void do_not_optimize(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "g"(value) : "memory");
#else
  volatile const T* sink = &value;
  (void)sink;
#endif
}

template <typename Function> [[nodiscard]] double time_ns_per_iter(std::size_t iterations, Function&& function) {
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    std::forward<Function>(function)();
    asm volatile("" ::: "memory");
  }
  const auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::nano>(end - start).count() / static_cast<double>(iterations);
}

template <typename Function> [[nodiscard]] Measurement measure(const Config& config, Function&& function) {
  std::vector<double> samples;
  samples.reserve(config.trials);
  std::forward<Function>(function)();
  for (std::size_t trial = 0; trial < config.trials; ++trial) {
    samples.push_back(time_ns_per_iter(config.iterations, function));
  }

  const auto sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  const auto [min_it, max_it] = std::minmax_element(samples.begin(), samples.end());
  return Measurement{sum / static_cast<double>(samples.size()), *min_it, *max_it};
}

inline void require_cache_line_aligned(std::string_view name, const void* ptr) {
  if (ptr == nullptr) {
    throw std::runtime_error("research object has null storage: " + std::string{name});
  }
  const auto address = reinterpret_cast<std::uintptr_t>(ptr);
  if (address % kCacheLineAlignment != 0U) {
    throw std::runtime_error("research object is not cache-line aligned: " + std::string{name});
  }
}

template <typename T> inline void fill_vector(Vector<T>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    vector(i) = static_cast<T>(static_cast<double>((i % 251U) + 1U) * 0.125);
  }
}

template <typename T> inline void fill_vector(Vector<std::complex<T>>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    const auto real = static_cast<T>(static_cast<double>((i % 251U) + 1U) * 0.125);
    const auto imag = static_cast<T>(static_cast<double>((i % 127U) + 1U) * 0.0625);
    vector(i) = {real, imag};
  }
}

template <typename T> inline void fill_matrix(Matrix<T>& matrix) {
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      matrix(row, col) =
        static_cast<T>(static_cast<double>((row % 251U) + 1U) * 0.25 + static_cast<double>((col % 127U) + 1U) * 0.125);
    }
  }
}

template <typename T> inline void fill_matrix(Matrix<std::complex<T>>& matrix) {
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      const auto real =
        static_cast<T>(static_cast<double>((row % 251U) + 1U) * 0.25 + static_cast<double>((col % 127U) + 1U) * 0.125);
      const auto imag = static_cast<T>(static_cast<double>((row % 97U) + 1U) * 0.0625 +
                                       static_cast<double>((col % 67U) + 1U) * 0.03125);
      matrix(row, col) = {real, imag};
    }
  }
}

template <typename T> inline void fill_image(Image<T>& image) {
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) =
        static_cast<T>(static_cast<double>((row % 251U) + 1U) * 0.25 + static_cast<double>((col % 127U) + 1U) * 0.125);
    }
  }
}

template <typename T> inline void fill_image(Image<std::complex<T>>& image) {
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      const auto real =
        static_cast<T>(static_cast<double>((row % 251U) + 1U) * 0.25 + static_cast<double>((col % 127U) + 1U) * 0.125);
      const auto imag = static_cast<T>(static_cast<double>((row % 97U) + 1U) * 0.0625 +
                                       static_cast<double>((col % 67U) + 1U) * 0.03125);
      image(row, col) = {real, imag};
    }
  }
}

template <typename T> [[nodiscard]] inline double checksum(const Vector<T>& vector) {
  return static_cast<double>(as_eigen(vector).sum());
}

template <typename T> [[nodiscard]] inline double checksum(const Vector<std::complex<T>>& vector) {
  const auto value = as_eigen(vector).sum();
  return static_cast<double>(value.real() + value.imag());
}

template <typename T> [[nodiscard]] inline double checksum(const Matrix<T>& matrix) {
  return static_cast<double>(as_eigen(matrix).sum());
}

template <typename T> [[nodiscard]] inline double checksum(const Image<T>& image) {
  return static_cast<double>(as_eigen(image).sum());
}

template <typename T> [[nodiscard]] inline double checksum(const Image<std::complex<T>>& image) {
  const auto value = as_eigen(image).sum();
  return static_cast<double>(value.real() + value.imag());
}

inline constexpr std::string_view kTableGap = "  ";
inline constexpr int kCaseColumnWidth = 32;
inline constexpr int kVariantColumnWidth = 36;
inline constexpr int kTypeColumnWidth = 13;
inline constexpr int kSizeColumnWidth = 7;
inline constexpr int kCoilsColumnWidth = 5;
inline constexpr int kIterationsColumnWidth = 4;
inline constexpr int kTrialsColumnWidth = 6;
inline constexpr int kTimingColumnWidth = 11;
inline constexpr int kChecksumColumnWidth = 16;
inline constexpr int kTableColumnCount = 11;
inline constexpr int kTableWidth = kCaseColumnWidth + kVariantColumnWidth + kTypeColumnWidth + kSizeColumnWidth +
                                   kCoilsColumnWidth + kIterationsColumnWidth + kTrialsColumnWidth +
                                   (kTimingColumnWidth * 3) + kChecksumColumnWidth +
                                   (static_cast<int>(kTableGap.size()) * (kTableColumnCount - 1));

inline void write_csv_header(std::ostream& output) {
  output << "case,variant,type,size,coils,iterations,trials,mean_ns,min_ns,max_ns,checksum\n";
}

inline void write_table_header(std::ostream& output) {
  output << std::left << std::setw(kCaseColumnWidth) << "case" << kTableGap << std::setw(kVariantColumnWidth)
         << "variant" << kTableGap << std::setw(kTypeColumnWidth) << "type" << kTableGap << std::right
         << std::setw(kSizeColumnWidth) << "size" << kTableGap << std::setw(kCoilsColumnWidth) << "coils" << kTableGap
         << std::setw(kIterationsColumnWidth) << "iter" << kTableGap << std::setw(kTrialsColumnWidth) << "trials"
         << kTableGap << std::setw(kTimingColumnWidth) << "mean(ns)" << kTableGap << std::setw(kTimingColumnWidth)
         << "min(ns)" << kTableGap << std::setw(kTimingColumnWidth) << "max(ns)" << kTableGap
         << std::setw(kChecksumColumnWidth) << "checksum" << '\n';
  output << std::string(kTableWidth, '-') << '\n';
}

inline void print_header(const Config& config) {
  if (config.output_format == OutputFormat::csv) {
    write_csv_header(std::cout);
    if (config.report_stream) {
      write_csv_header(config.report_stream);
    }
    return;
  }

  write_table_header(std::cout);
  if (config.report_stream) {
    write_table_header(config.report_stream);
  }
}

inline void write_csv_row(std::ostream& output, std::string_view case_name, std::string_view variant,
                          std::string_view type_name, std::size_t size, std::size_t coils, const Config& config,
                          const Measurement& measurement, double checksum_value) {
  output << case_name << ',' << variant << ',' << type_name << ',' << size << ',' << coils << ',' << config.iterations
         << ',' << config.trials << ',' << measurement.mean_ns << ',' << measurement.min_ns << ',' << measurement.max_ns
         << ',' << checksum_value << '\n';
}

inline void write_table_row(std::ostream& output, std::string_view case_name, std::string_view variant,
                            std::string_view type_name, std::size_t size, std::size_t coils, const Config& config,
                            const Measurement& measurement, double checksum_value) {
  output << std::left << std::setw(kCaseColumnWidth) << case_name << kTableGap << std::setw(kVariantColumnWidth)
         << variant << kTableGap << std::setw(kTypeColumnWidth) << type_name << kTableGap << std::right
         << std::setw(kSizeColumnWidth) << size << kTableGap << std::setw(kCoilsColumnWidth) << coils << kTableGap
         << std::setw(kIterationsColumnWidth) << config.iterations << kTableGap << std::setw(kTrialsColumnWidth)
         << config.trials << kTableGap << std::fixed << std::setprecision(2) << std::setw(kTimingColumnWidth)
         << measurement.mean_ns << kTableGap << std::setw(kTimingColumnWidth) << measurement.min_ns << kTableGap
         << std::setw(kTimingColumnWidth) << measurement.max_ns << kTableGap << std::setprecision(6)
         << std::setw(kChecksumColumnWidth) << checksum_value << '\n';
}

inline void print_row(std::string_view case_name, std::string_view variant, std::string_view type_name,
                      std::size_t size, std::size_t coils, const Config& config, const Measurement& measurement,
                      double checksum_value) {
  if (config.output_format == OutputFormat::csv) {
    write_csv_row(std::cout, case_name, variant, type_name, size, coils, config, measurement, checksum_value);
    if (config.report_stream) {
      write_csv_row(config.report_stream, case_name, variant, type_name, size, coils, config, measurement,
                    checksum_value);
    }
    return;
  }

  write_table_row(std::cout, case_name, variant, type_name, size, coils, config, measurement, checksum_value);
  if (config.report_stream) {
    write_table_row(config.report_stream, case_name, variant, type_name, size, coils, config, measurement,
                    checksum_value);
  }
}

} // namespace ksj::research::cpp_numerics_performance
