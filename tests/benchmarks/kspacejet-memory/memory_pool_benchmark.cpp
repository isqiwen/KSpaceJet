#include "kspacejet/memory/memory.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

enum class OutputFormat {
  table,
  csv,
};

struct Config {
  std::size_t threads{std::max<std::size_t>(1, std::thread::hardware_concurrency())};
  std::size_t iterations{20000};
  std::size_t trials{5};
  std::size_t bytes{4096};
};

struct BenchmarkResult {
  std::string_view name;
  std::size_t threads{0};
  std::size_t iterations{0};
  std::size_t bytes{0};
  std::uint64_t total_ops{0};
  double elapsed_ms{0.0};
};

struct BenchmarkSummary {
  std::string_view name;
  std::size_t threads{0};
  std::size_t iterations{0};
  std::size_t trials{0};
  std::size_t bytes{0};
  std::uint64_t total_ops{0};
  double mean_elapsed_ms{0.0};
  double mean_ops_per_sec{0.0};
  double mean_ns_per_op{0.0};
  double min_ns_per_op{0.0};
  double max_ns_per_op{0.0};
};

OutputFormat& output_format() {
  static OutputFormat format = OutputFormat::table;
  return format;
}

std::filesystem::path& report_path() {
  static std::filesystem::path path;
  return path;
}

std::ofstream& report_stream() {
  static std::ofstream stream;
  return stream;
}

[[nodiscard]] ksj::memory::MemoryPoolOptions benchmark_memory_pool_options() {
  ksj::memory::MemoryPoolOptions options;
  options.size_classes = {
    64ULL * 1024ULL,
    1ULL * 1024ULL * 1024ULL,
    2ULL * 1024ULL * 1024ULL,
    4ULL * 1024ULL * 1024ULL,
    8ULL * 1024ULL * 1024ULL,
    16ULL * 1024ULL * 1024ULL,
    32ULL * 1024ULL * 1024ULL,
    64ULL * 1024ULL * 1024ULL,
    128ULL * 1024ULL * 1024ULL,
    256ULL * 1024ULL * 1024ULL,
    512ULL * 1024ULL * 1024ULL,
  };
  options.size_class_block_counts = {1024, 64, 32, 16, 8, 4, 2, 1, 1, 1, 1};
  return options;
}

[[nodiscard]] bool parse_size(std::string_view text, std::size_t& value) {
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

[[nodiscard]] std::filesystem::path executable_path(char** argv) {
#if defined(__linux__)
  std::error_code error;
  const auto self_path = std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error && !self_path.empty()) {
    return self_path;
  }
#endif
  return std::filesystem::absolute(std::filesystem::path(argv[0]));
}

[[nodiscard]] std::string_view report_extension() {
  return output_format() == OutputFormat::csv ? ".csv" : ".txt";
}

void open_report_file(char** argv) {
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

void parse_args(int argc, char** argv, Config& config) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    auto read_value = [&](std::size_t& target) {
      if (i + 1 >= argc || !parse_size(argv[i + 1], target)) {
        std::cerr << "invalid value for " << arg << '\n';
        std::exit(2);
      }
      ++i;
    };

    if (arg == "--threads") {
      read_value(config.threads);
    } else if (arg == "--iterations") {
      read_value(config.iterations);
    } else if (arg == "--trials") {
      read_value(config.trials);
    } else if (arg == "--bytes") {
      read_value(config.bytes);
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
      std::cout << "usage: ksj_memory_benchmark [--threads N] [--iterations N] [--bytes N]\n";
      std::cout << "  --trials N\n";
      std::cout << "  --format table|csv\n";
      std::cout << "  --csv\n";
      std::cout << "  report: <executable-dir>/reports/<executable-name>.txt|.csv\n";
      std::exit(0);
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      std::exit(2);
    }
  }

  config.threads = std::max<std::size_t>(1, config.threads);
  config.iterations = std::max<std::size_t>(1, config.iterations);
  config.trials = std::max<std::size_t>(1, config.trials);
  config.bytes = std::max<std::size_t>(1, config.bytes);
  open_report_file(argv);
}

[[nodiscard]] ksj::memory::AllocationRequest make_request(const std::size_t bytes, const std::size_t worker_index,
                                                          const bool direct) {
  ksj::memory::AllocationRequest request;
  request.bytes = bytes;
  request.worker_index = worker_index;
  request.properties.label = direct ? "benchmark.direct" : "benchmark.pool";
  request.properties.locality = ksj::memory::Locality::worker_local;
  request.properties.allocator =
    direct ? ksj::memory::AllocatorKind::host_direct : ksj::memory::AllocatorKind::host_pool;
  request.properties.alignment = ksj::memory::NumaHostSpace::cache_line_size();
  return request;
}

void touch(ksj::memory::MemoryLease& lease, const std::size_t iteration) {
  if (!lease || lease.size() == 0) {
    return;
  }
  const auto value = std::byte{static_cast<unsigned char>(iteration & 0xFFU)};
  lease.data()[0] = value;
  lease.data()[lease.size() - 1U] = value;
}

template <typename Function>
[[nodiscard]] BenchmarkResult time_case(std::string_view name, std::size_t threads, std::size_t iterations,
                                        std::size_t bytes, Function&& function) {
  const auto start = std::chrono::steady_clock::now();
  std::forward<Function>(function)();
  const auto end = std::chrono::steady_clock::now();
  return BenchmarkResult{
    .name = name,
    .threads = threads,
    .iterations = iterations,
    .bytes = bytes,
    .total_ops = static_cast<std::uint64_t>(threads) * static_cast<std::uint64_t>(iterations),
    .elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count(),
  };
}

[[nodiscard]] BenchmarkResult run_pool_single_thread(const Config& config) {
  auto& memory = ksj::memory::MemoryBroker::instance();
  memory.trim();
  return time_case("pool_single_thread", 1, config.iterations, config.bytes, [&] {
    for (std::size_t i = 0; i < config.iterations; ++i) {
      auto lease = memory.acquire(make_request(config.bytes, 0, false));
      touch(lease, i);
    }
  });
}

[[nodiscard]] BenchmarkResult run_pool_multi_thread(const Config& config) {
  auto& memory = ksj::memory::MemoryBroker::instance();
  memory.trim();
  std::atomic<std::size_t> ready{0};
  std::atomic<bool> start{false};
  std::vector<std::thread> workers;
  workers.reserve(config.threads);

  auto result = time_case("pool_multi_thread", config.threads, config.iterations, config.bytes, [&] {
    for (std::size_t worker = 0; worker < config.threads; ++worker) {
      workers.emplace_back([&, worker] {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }

        for (std::size_t i = 0; i < config.iterations; ++i) {
          auto lease = memory.acquire(make_request(config.bytes, worker, false));
          touch(lease, i + worker);
        }
      });
    }

    while (ready.load(std::memory_order_acquire) != config.threads) {
      std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (auto& worker : workers) {
      worker.join();
    }
  });

  return result;
}

[[nodiscard]] BenchmarkResult run_direct_single_thread(const Config& config) {
  auto& memory = ksj::memory::MemoryBroker::instance();
  memory.trim();
  return time_case("direct_single_thread", 1, config.iterations, config.bytes, [&] {
    for (std::size_t i = 0; i < config.iterations; ++i) {
      auto lease = memory.acquire(make_request(config.bytes, 0, true));
      touch(lease, i);
    }
  });
}

[[nodiscard]] double ns_per_op(const BenchmarkResult& result) {
  return result.total_ops == 0 ? 0.0 : (result.elapsed_ms * 1000000.0) / static_cast<double>(result.total_ops);
}

template <typename Function> [[nodiscard]] BenchmarkSummary summarize_case(const Config& config, Function&& function) {
  std::vector<BenchmarkResult> samples;
  samples.reserve(config.trials);
  function();
  for (std::size_t trial = 0; trial < config.trials; ++trial) {
    samples.push_back(function());
  }

  const auto first = samples.front();
  double elapsed_sum = 0.0;
  double ns_sum = 0.0;
  double min_ns = ns_per_op(first);
  double max_ns = min_ns;
  for (const auto& sample : samples) {
    const auto sample_ns = ns_per_op(sample);
    elapsed_sum += sample.elapsed_ms;
    ns_sum += sample_ns;
    min_ns = std::min(min_ns, sample_ns);
    max_ns = std::max(max_ns, sample_ns);
  }

  const auto mean_elapsed = elapsed_sum / static_cast<double>(samples.size());
  const auto mean_ns = ns_sum / static_cast<double>(samples.size());
  const auto seconds = mean_elapsed / 1000.0;
  const auto ops_per_sec = seconds == 0.0 ? 0.0 : static_cast<double>(first.total_ops) / seconds;
  return BenchmarkSummary{
    .name = first.name,
    .threads = first.threads,
    .iterations = first.iterations,
    .trials = config.trials,
    .bytes = first.bytes,
    .total_ops = first.total_ops,
    .mean_elapsed_ms = mean_elapsed,
    .mean_ops_per_sec = ops_per_sec,
    .mean_ns_per_op = mean_ns,
    .min_ns_per_op = min_ns,
    .max_ns_per_op = max_ns,
  };
}

inline constexpr std::string_view kTableGap = "  ";
inline constexpr int kCaseColumnWidth = 20;
inline constexpr int kThreadsColumnWidth = 7;
inline constexpr int kIterationsColumnWidth = 10;
inline constexpr int kTrialsColumnWidth = 6;
inline constexpr int kBytesColumnWidth = 8;
inline constexpr int kOpsColumnWidth = 11;
inline constexpr int kMetricColumnWidth = 13;
inline constexpr int kTableColumnCount = 11;
inline constexpr int kTableWidth = kCaseColumnWidth + kThreadsColumnWidth + kIterationsColumnWidth +
                                   kTrialsColumnWidth + kBytesColumnWidth + kOpsColumnWidth + (kMetricColumnWidth * 5) +
                                   (static_cast<int>(kTableGap.size()) * (kTableColumnCount - 1));

void write_csv_header(std::ostream& output) {
  output << "case,threads,iterations,trials,bytes,total_ops,mean_elapsed_ms,mean_ops_per_sec,mean_ns_per_op,"
            "min_ns_per_op,max_ns_per_op\n";
}

void write_table_header(std::ostream& output) {
  output << std::left << std::setw(kCaseColumnWidth) << "case" << kTableGap << std::right
         << std::setw(kThreadsColumnWidth) << "threads" << kTableGap << std::setw(kIterationsColumnWidth)
         << "iterations" << kTableGap << std::setw(kTrialsColumnWidth) << "trials" << kTableGap
         << std::setw(kBytesColumnWidth) << "bytes" << kTableGap << std::setw(kOpsColumnWidth) << "total_ops"
         << kTableGap << std::setw(kMetricColumnWidth) << "mean(ns/op)" << kTableGap << std::setw(kMetricColumnWidth)
         << "min(ns/op)" << kTableGap << std::setw(kMetricColumnWidth) << "max(ns/op)" << kTableGap
         << std::setw(kMetricColumnWidth) << "elapsed(ms)" << kTableGap << std::setw(kMetricColumnWidth) << "ops/sec"
         << '\n';
  output << std::string(kTableWidth, '-') << '\n';
}

void print_header() {
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

void write_csv_result(std::ostream& output, const BenchmarkSummary& result) {
  output << result.name << ',' << result.threads << ',' << result.iterations << ',' << result.trials << ','
         << result.bytes << ',' << result.total_ops << ',' << result.mean_elapsed_ms << ',' << result.mean_ops_per_sec
         << ',' << result.mean_ns_per_op << ',' << result.min_ns_per_op << ',' << result.max_ns_per_op << '\n';
}

void write_table_result(std::ostream& output, const BenchmarkSummary& result) {
  output << std::left << std::setw(kCaseColumnWidth) << result.name << kTableGap << std::right
         << std::setw(kThreadsColumnWidth) << result.threads << kTableGap << std::setw(kIterationsColumnWidth)
         << result.iterations << kTableGap << std::setw(kTrialsColumnWidth) << result.trials << kTableGap
         << std::setw(kBytesColumnWidth) << result.bytes << kTableGap << std::setw(kOpsColumnWidth) << result.total_ops
         << kTableGap << std::fixed << std::setprecision(2) << std::setw(kMetricColumnWidth) << result.mean_ns_per_op
         << kTableGap << std::setw(kMetricColumnWidth) << result.min_ns_per_op << kTableGap
         << std::setw(kMetricColumnWidth) << result.max_ns_per_op << kTableGap << std::setprecision(3)
         << std::setw(kMetricColumnWidth) << result.mean_elapsed_ms << kTableGap << std::setprecision(2)
         << std::setw(kMetricColumnWidth) << result.mean_ops_per_sec << '\n';
}

void print_result(const BenchmarkSummary& result) {
  if (output_format() == OutputFormat::csv) {
    write_csv_result(std::cout, result);
    if (report_stream()) {
      write_csv_result(report_stream(), result);
    }
    return;
  }

  write_table_result(std::cout, result);
  if (report_stream()) {
    write_table_result(report_stream(), result);
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  parse_args(argc, argv, config);
  if (!ksj::memory::MemoryBroker::configure_instance(benchmark_memory_pool_options())) {
    throw std::runtime_error("kspacejet-memory benchmark configured memory broker too late");
  }

  print_header();
  print_result(summarize_case(config, [&] {
    return run_pool_single_thread(config);
  }));
  print_result(summarize_case(config, [&] {
    return run_pool_multi_thread(config);
  }));
  print_result(summarize_case(config, [&] {
    return run_direct_single_thread(config);
  }));
  return 0;
}
