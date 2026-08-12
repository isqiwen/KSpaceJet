#pragma once

#include "kspacejet/base/types.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kspacejet/base/result.hpp"
#include "kspacejet/config/key_value_config.hpp"

namespace ksj::config {

struct CrashConfig {
  bool enabled = true;
  bool capture_terminate = true;
  bool use_altstack = true;
  bool print_readable_stack = true;
  bool launch_debugger_from_env = true;
  std::string debugger_env_var = "USE_GDB_ON_FAULT";
  std::size_t max_frames = 200;
};

struct MemoryPoolConfig {
  bool enabled = true;
  // Required only when memory pooling is enabled.
  std::vector<std::size_t> size_classes;
  // Required with size_classes when memory pooling is enabled: one block count per size class.
  std::vector<std::size_t> size_class_block_counts;
};

struct MemoryConfig {
  MemoryPoolConfig pool;
};

struct OutputPathConfig {
  std::string results_dir = "results";
  std::string log_dir = "logs";
};

struct DebugConfig {
  bool enabled = false;
  std::string categories;
  int dump_slice_index = -1;
  std::string root_dir;
  std::string report_dir;
  std::string algorithm_dir;
  std::string slice_dump_dir;
  std::string matrix_dump_dir;
};

struct LoggingConsoleConfig {
  bool enabled = true;
  std::string level = "info";
  bool console_color = false;
  std::string pattern = "[%^%l%$] [%t] [%s:%#] %v";
};

struct LoggingFileConfig {
  bool enabled = false;
  std::string level = "info";
  std::string path;
  ksj::base::u32 retention_days = 30;
  std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%#] %v";
};

struct LoggingConfig {
  std::string logger_name;
  std::string flush_level = "warn";
  std::size_t periodic_flush_ms = 1000;
  bool async = true;
  std::size_t queue_size = 8192;
  std::size_t async_worker_count = 1;
  std::string output_format = "text";
  LoggingConsoleConfig console;
  LoggingFileConfig file;
};

struct RuntimeConfig {
  std::string runtime_output_root_dir;
  std::string provider_config_file;
  CrashConfig crash;
  MemoryConfig memory;
  OutputPathConfig output;
  DebugConfig debug;
  LoggingConfig logging;
};

[[nodiscard]] ksj::base::Result<RuntimeConfig> runtime_config_from_key_value_config(const KeyValueConfig& config);
[[nodiscard]] ksj::base::Result<RuntimeConfig> parse_runtime_config(std::string_view text,
                                                                    std::string_view source_name = {});
[[nodiscard]] ksj::base::Result<RuntimeConfig> load_runtime_config_file(const std::string& path);

} // namespace ksj::config
