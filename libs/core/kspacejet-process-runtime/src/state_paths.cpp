#include "kspacejet/process_runtime/state_paths.hpp"

#include <mutex>

#include "kspacejet/base/path.hpp"
#include "kspacejet/config/runtime_config.hpp"
#include "kspacejet/process_runtime/executable_layout.hpp"
#include "kspacejet/process_runtime/runtime_config.hpp"

namespace ksj::process_runtime::state_paths {
namespace {

std::mutex& current_run_output_root_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::string& current_run_output_root_storage() {
  static std::string path;
  return path;
}

std::mutex& debug_paths_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::string& debug_dir_override_storage() {
  static std::string path;
  return path;
}

std::string& debug_slice_dump_dir_override_storage() {
  static std::string path;
  return path;
}

std::string& debug_matrix_dump_dir_override_storage() {
  static std::string path;
  return path;
}

[[nodiscard]] std::string resolve_optional_state_relative_path(std::string_view configured_path) {
  if (configured_path.empty()) {
    return std::string();
  }

  const fs::path path(configured_path);
  if (ksj::base::path::is_absolute_path_like(path)) {
    return ksj::base::path::normalize(path);
  }

  return ksj::base::path::normalize(resolve_relative_to_state_dir(path));
}

[[nodiscard]] const ksj::config::DebugConfig* current_debug_config() {
  const auto& cached_config = ksj::process_runtime::runtime_config::current_runtime_config();
  return cached_config.config.has_value() ? &cached_config.config->debug : nullptr;
}

[[nodiscard]] const ksj::config::OutputPathConfig* current_output_path_config() {
  const auto& cached_config = ksj::process_runtime::runtime_config::current_runtime_config();
  return cached_config.config.has_value() ? &cached_config.config->output : nullptr;
}

[[nodiscard]] std::string output_dir_path(std::string_view configured_path, std::string_view default_relative_dir) {
  const fs::path path(configured_path.empty() ? default_relative_dir : configured_path);
  return ksj::base::path::normalize(resolve_relative_to_state_dir(path));
}

[[nodiscard]] std::string resolve_optional_debug_relative_path(std::string_view configured_path) {
  if (configured_path.empty()) {
    return std::string();
  }

  const fs::path path(configured_path);
  if (ksj::base::path::is_absolute_path_like(path)) {
    return ksj::base::path::normalize(path);
  }

  const std::string debug_root = debug_dir_path();
  return debug_root.empty() ? std::string() : ksj::base::path::normalize(fs::path(debug_root) / path);
}

[[nodiscard]] std::string configured_debug_subdir_path(std::string_view configured_path,
                                                       std::string_view default_relative_subdir) {
  if (!configured_path.empty()) {
    return resolve_optional_debug_relative_path(configured_path);
  }
  return debug_subdir_path(default_relative_subdir);
}

} // namespace

fs::path resolve_relative_to_state_dir(const fs::path& path) {
  const fs::path runtime_output_root_dir = fs::path(runtime_output_root_dir_path());
  if (path.empty()) {
    return runtime_output_root_dir;
  }
  return ksj::base::path::is_absolute_path_like(path) ? path : runtime_output_root_dir / path;
}

void set_current_run_output_root(const std::string& path) {
  std::lock_guard<std::mutex> lock(current_run_output_root_mutex());
  current_run_output_root_storage() = path.empty() ? std::string() : ksj::base::path::normalize(path);
}

void clear_current_run_output_root() {
  std::lock_guard<std::mutex> lock(current_run_output_root_mutex());
  current_run_output_root_storage().clear();
}

std::string current_run_output_root() {
  std::lock_guard<std::mutex> lock(current_run_output_root_mutex());
  return current_run_output_root_storage();
}

void set_debug_dir_override(const std::string& path) {
  std::lock_guard<std::mutex> lock(debug_paths_mutex());
  debug_dir_override_storage() = path.empty() ? std::string() : ksj::base::path::normalize(path);
}

void set_debug_slice_dump_dir_override(const std::string& path) {
  std::lock_guard<std::mutex> lock(debug_paths_mutex());
  debug_slice_dump_dir_override_storage() = path.empty() ? std::string() : ksj::base::path::normalize(path);
}

void set_debug_matrix_dump_dir_override(const std::string& path) {
  std::lock_guard<std::mutex> lock(debug_paths_mutex());
  debug_matrix_dump_dir_override_storage() = path.empty() ? std::string() : ksj::base::path::normalize(path);
}

void clear_debug_path_overrides() {
  std::lock_guard<std::mutex> lock(debug_paths_mutex());
  debug_dir_override_storage().clear();
  debug_slice_dump_dir_override_storage().clear();
  debug_matrix_dump_dir_override_storage().clear();
}

std::string runtime_output_root_dir_path() {
  static const std::string runtime_output_root_dir = [] {
    const auto& cached_config = ksj::process_runtime::runtime_config::current_runtime_config();
    if (cached_config.config.has_value()) {
      const fs::path configured_path(cached_config.config->runtime_output_root_dir);
      if (!configured_path.empty()) {
        const fs::path resolved_path = ksj::base::path::is_absolute_path_like(configured_path)
                                         ? configured_path
                                         : executable_layout::runtime_layout_root() / configured_path;
        return ksj::base::path::normalize(resolved_path);
      }
    }

    return ksj::base::path::normalize(executable_layout::runtime_layout_root());
  }();

  return runtime_output_root_dir;
}

std::string configured_output_dir_path(std::string_view configured_path, std::string_view default_relative_dir) {
  return output_dir_path(configured_path, default_relative_dir);
}

std::string logging_base_dir_path() {
  if (const ksj::config::OutputPathConfig* const output_config = current_output_path_config();
      output_config != nullptr) {
    return output_dir_path(output_config->log_dir, "logs");
  }
  return output_dir_path("", "logs");
}

std::string current_executable_logger_name() {
  const std::string executable_name = runtime_config::current_executable_name();
  return executable_name.empty() ? std::string("KSpaceJet") : executable_name;
}

std::string current_executable_relative_log_path() {
  return ksj::base::path::normalize(fs::path(logging_base_dir_path()) / (current_executable_logger_name() + ".log"));
}

std::string results_dir_path() {
  if (const ksj::config::OutputPathConfig* const output_config = current_output_path_config();
      output_config != nullptr) {
    return output_dir_path(output_config->results_dir, "results");
  }
  return output_dir_path("", "results");
}

std::string default_recon_output_dir_path() {
  return results_dir_path();
}

std::string runtime_cache_dir_path() {
  return ksj::base::path::normalize(executable_layout::executable_dir() / "runtime_cache");
}

std::string runtime_cache_path(std::string_view relative_or_absolute_path) {
  const fs::path path(relative_or_absolute_path);
  if (ksj::base::path::is_absolute_path_like(path)) {
    return ksj::base::path::normalize(path);
  }

  return ksj::base::path::normalize(fs::path(runtime_cache_dir_path()) / path);
}

std::string debug_dir_path() {
  {
    std::lock_guard<std::mutex> lock(debug_paths_mutex());
    const std::string configured_path = debug_dir_override_storage();
    if (!configured_path.empty()) {
      return resolve_optional_state_relative_path(configured_path);
    }
  }

  if (const ksj::config::DebugConfig* const debug_config = current_debug_config();
      debug_config != nullptr && !debug_config->root_dir.empty()) {
    return resolve_optional_state_relative_path(debug_config->root_dir);
  }

  const std::string current_root = current_run_output_root();
  return current_root.empty() ? ksj::base::path::normalize(resolve_relative_to_state_dir("debug"))
                              : ksj::base::path::normalize(fs::path(current_root) / "debug");
}

std::string debug_subdir_path(std::string_view relative_subdir) {
  const std::string debug_dir = debug_dir_path();
  return debug_dir.empty() ? std::string()
                           : ksj::base::path::normalize(fs::path(debug_dir) / fs::path(relative_subdir));
}

std::string debug_report_dir_path() {
  if (const ksj::config::DebugConfig* const debug_config = current_debug_config(); debug_config != nullptr) {
    return configured_debug_subdir_path(debug_config->report_dir, "reports");
  }
  return debug_subdir_path("reports");
}

std::string debug_algorithm_dir_path() {
  if (const ksj::config::DebugConfig* const debug_config = current_debug_config(); debug_config != nullptr) {
    return configured_debug_subdir_path(debug_config->algorithm_dir, "algorithms");
  }
  return debug_subdir_path("algorithms");
}

std::string debug_report_path(std::string_view relative_or_absolute_path) {
  const fs::path path(relative_or_absolute_path);
  if (ksj::base::path::is_absolute_path_like(path)) {
    return ksj::base::path::normalize(path);
  }

  const std::string report_dir = debug_report_dir_path();
  return report_dir.empty() ? std::string() : ksj::base::path::normalize(fs::path(report_dir) / path);
}

std::string debug_algorithm_path(std::string_view relative_or_absolute_path) {
  const fs::path path(relative_or_absolute_path);
  if (ksj::base::path::is_absolute_path_like(path)) {
    return ksj::base::path::normalize(path);
  }

  const std::string algorithm_dir = debug_algorithm_dir_path();
  return algorithm_dir.empty() ? std::string() : ksj::base::path::normalize(fs::path(algorithm_dir) / path);
}

std::string resolve_debug_path(std::string_view relative_or_absolute_path) {
  const fs::path path(relative_or_absolute_path);
  if (ksj::base::path::is_absolute_path_like(path)) {
    return ksj::base::path::normalize(path);
  }

  const std::string debug_dir = debug_dir_path();
  return debug_dir.empty() ? std::string() : ksj::base::path::normalize(fs::path(debug_dir) / path);
}

std::string debug_slice_dump_dir_path() {
  std::string configured_path;
  std::string debug_root_override;
  {
    std::lock_guard<std::mutex> lock(debug_paths_mutex());
    configured_path = debug_slice_dump_dir_override_storage();
    debug_root_override = debug_dir_override_storage();
  }
  if (!configured_path.empty()) {
    return resolve_optional_debug_relative_path(configured_path);
  }
  if (const ksj::config::DebugConfig* const debug_config = current_debug_config(); debug_config != nullptr) {
    return debug_config->slice_dump_dir.empty() ? debug_dir_path()
                                                : resolve_optional_debug_relative_path(debug_config->slice_dump_dir);
  }
  return debug_root_override.empty() ? debug_subdir_path("slices")
                                     : resolve_optional_state_relative_path(debug_root_override);
}

std::string debug_matrix_dump_dir_path() {
  std::string configured_path;
  std::string debug_root_override;
  {
    std::lock_guard<std::mutex> lock(debug_paths_mutex());
    configured_path = debug_matrix_dump_dir_override_storage();
    debug_root_override = debug_dir_override_storage();
  }
  if (!configured_path.empty()) {
    return resolve_optional_debug_relative_path(configured_path);
  }
  if (const ksj::config::DebugConfig* const debug_config = current_debug_config(); debug_config != nullptr) {
    return debug_config->matrix_dump_dir.empty() ? debug_dir_path()
                                                 : resolve_optional_debug_relative_path(debug_config->matrix_dump_dir);
  }
  return debug_root_override.empty() ? debug_subdir_path("matrices")
                                     : resolve_optional_state_relative_path(debug_root_override);
}

} // namespace ksj::process_runtime::state_paths
