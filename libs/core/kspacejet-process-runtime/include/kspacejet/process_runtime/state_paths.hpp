#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ksj::process_runtime::state_paths {

namespace fs = std::filesystem;

[[nodiscard]] fs::path resolve_relative_to_state_dir(const fs::path& path);

void set_current_run_output_root(const std::string& path);
void clear_current_run_output_root();
[[nodiscard]] std::string current_run_output_root();

void set_debug_dir_override(const std::string& path);
void set_debug_slice_dump_dir_override(const std::string& path);
void set_debug_matrix_dump_dir_override(const std::string& path);
void clear_debug_path_overrides();

[[nodiscard]] std::string runtime_output_root_dir_path();
[[nodiscard]] std::string configured_output_dir_path(std::string_view configured_path,
                                                     std::string_view default_relative_dir);
[[nodiscard]] std::string logging_base_dir_path();
[[nodiscard]] std::string current_executable_logger_name();
[[nodiscard]] std::string current_executable_relative_log_path();
[[nodiscard]] std::string results_dir_path();
[[nodiscard]] std::string default_recon_output_dir_path();
[[nodiscard]] std::string runtime_cache_dir_path();
[[nodiscard]] std::string runtime_cache_path(std::string_view relative_or_absolute_path);
[[nodiscard]] std::string debug_dir_path();
[[nodiscard]] std::string debug_report_dir_path();
[[nodiscard]] std::string debug_algorithm_dir_path();
[[nodiscard]] std::string debug_subdir_path(std::string_view relative_subdir);
[[nodiscard]] std::string debug_report_path(std::string_view relative_or_absolute_path);
[[nodiscard]] std::string debug_algorithm_path(std::string_view relative_or_absolute_path);
[[nodiscard]] std::string resolve_debug_path(std::string_view relative_or_absolute_path);
[[nodiscard]] std::string debug_slice_dump_dir_path();
[[nodiscard]] std::string debug_matrix_dump_dir_path();

} // namespace ksj::process_runtime::state_paths
