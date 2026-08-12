#pragma once

#include "kspacejet/base/file.hpp"
#include "kspacejet/base/path.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/process_runtime/runtime_config.hpp"
#include "kspacejet/process_runtime/state_paths.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdarg.h>
#include <string>
#include <string_view>

namespace ksj::process_runtime::debug_dump {
namespace fs = std::filesystem;

[[nodiscard]] inline const ksj::config::DebugConfig* CurrentDebugConfig() {
  const auto& cached_config = ksj::process_runtime::runtime_config::current_runtime_config();
  return cached_config.config.has_value() ? &cached_config.config->debug : nullptr;
}

[[nodiscard]] inline bool DebugDumpEnabled() {
  const ksj::config::DebugConfig* const debug_config = CurrentDebugConfig();
  return debug_config != nullptr && debug_config->enabled;
}

[[nodiscard]] inline bool IsCategorySeparator(const char ch) {
  return ch == ',' || ch == ';' || std::isspace(static_cast<unsigned char>(ch)) != 0;
}

[[nodiscard]] inline bool AsciiEqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool IsCategoryPathSeparator(const char ch) {
  return ch == '/' || ch == '\\';
}

[[nodiscard]] inline bool IsCategoryChildSeparator(const char ch) {
  return IsCategoryPathSeparator(ch) || ch == '_' || ch == '-' || ch == '.';
}

[[nodiscard]] inline bool AsciiStartsWithIgnoreCase(std::string_view value, std::string_view prefix) {
  return prefix.size() <= value.size() && AsciiEqualsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

[[nodiscard]] inline bool CategoryMatches(std::string_view token, std::string_view category) {
  if (token == "*" || AsciiEqualsIgnoreCase(token, "all") || AsciiEqualsIgnoreCase(token, category)) {
    return true;
  }

  if (AsciiStartsWithIgnoreCase(category, token) && category.size() > token.size() &&
      IsCategoryChildSeparator(category[token.size()])) {
    return true;
  }

  std::size_t offset = 0;
  while (offset < category.size()) {
    while (offset < category.size() && IsCategoryPathSeparator(category[offset])) {
      ++offset;
    }

    const std::size_t segment_begin = offset;
    while (offset < category.size() && !IsCategoryPathSeparator(category[offset])) {
      ++offset;
    }

    if (AsciiEqualsIgnoreCase(category.substr(segment_begin, offset - segment_begin), token)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool CategoryListContains(std::string_view categories, std::string_view category) {
  std::size_t offset = 0;
  while (offset < categories.size()) {
    while (offset < categories.size() && IsCategorySeparator(categories[offset])) {
      ++offset;
    }

    const std::size_t token_begin = offset;
    while (offset < categories.size() && !IsCategorySeparator(categories[offset])) {
      ++offset;
    }

    const std::string_view token = categories.substr(token_begin, offset - token_begin);
    if (!token.empty() && CategoryMatches(token, category)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool IsCategoryEnabled(std::string_view category) {
  const ksj::config::DebugConfig* const debug_config = CurrentDebugConfig();
  if (debug_config == nullptr || !debug_config->enabled) {
    return false;
  }
  return debug_config->categories.empty() || CategoryListContains(debug_config->categories, category);
}

[[nodiscard]] inline bool IsDebugDumpEnabledForCategory(std::string_view category) {
  return category.empty() ? DebugDumpEnabled() : IsCategoryEnabled(category);
}

[[nodiscard]] inline bool IsDebugDumpEnabledForOutput(std::string_view category_or_path) {
  return IsDebugDumpEnabledForCategory(category_or_path);
}

[[nodiscard]] inline int ConfiguredDebugSliceIndex() {
  const ksj::config::DebugConfig* const debug_config = CurrentDebugConfig();
  return debug_config != nullptr ? debug_config->dump_slice_index : -1;
}

[[nodiscard]] inline bool GetDebugSliceIndex(std::string_view category, int& slice_index) {
  if (!IsDebugDumpEnabledForCategory(category)) {
    return false;
  }

  slice_index = ConfiguredDebugSliceIndex();
  return true;
}

[[nodiscard]] inline bool ShouldDumpSlice(std::string_view category, const int slice_index) {
  if (!IsDebugDumpEnabledForCategory(category)) {
    return false;
  }
  const int configured_slice_index = ConfiguredDebugSliceIndex();
  return configured_slice_index < 0 || configured_slice_index == slice_index;
}

[[nodiscard]] inline std::string ResolveDebugDirectory(const char* dir_name) {
  const std::string value = dir_name == nullptr ? "" : dir_name;
  if (value.empty()) {
    return ksj::process_runtime::state_paths::debug_dir_path();
  }

  const fs::path path(value);
  if (ksj::base::path::is_absolute_path_like(path)) {
    return ksj::base::path::normalize(path);
  }
  return ksj::process_runtime::state_paths::debug_subdir_path(value);
}

[[nodiscard]] inline std::string ResolveDebugFile(const char* file_name) {
  const std::string value = file_name == nullptr ? "" : file_name;
  if (value.empty()) {
    return ksj::process_runtime::state_paths::debug_dir_path();
  }

  const fs::path path(value);
  if (ksj::base::path::is_absolute_path_like(path)) {
    return ksj::base::path::normalize(path);
  }
  return ksj::process_runtime::state_paths::resolve_debug_path(value);
}

[[nodiscard]] inline bool PrepareDebugDirectory(const std::string& dir_path) {
  return !dir_path.empty() && ksj::base::path::ensure_directory_exists(dir_path);
}

[[nodiscard]] inline bool PrepareDebugFilePath(const std::string& file_path) {
  return !file_path.empty() && ksj::base::path::ensure_parent_directory_exists(file_path);
}

[[nodiscard]] inline std::string ResolveDebugAlgorithmFile(std::string_view file_name) {
  const std::string resolved_path = ksj::process_runtime::state_paths::debug_algorithm_path(file_name);
  (void)PrepareDebugFilePath(resolved_path);
  return resolved_path;
}

[[nodiscard]] inline bool write_binary_file(const void* data, std::size_t element_size, std::size_t element_count,
                                            std::string_view file_name) {
  if (!IsDebugDumpEnabledForOutput(file_name)) {
    return false;
  }

  const std::string file_name_string(file_name);
  const std::string resolved_path = ResolveDebugFile(file_name_string.c_str());
  if (!PrepareDebugFilePath(resolved_path)) {
    KSJ_LOG_ERROR("Failed to prepare debug dump path [{}].", resolved_path);
    return false;
  }

  const auto result = ksj::base::file::write_binary_file_atomically(resolved_path, data, element_size, element_count);
  if (!result.complete()) {
    KSJ_LOG_ERROR("Failed to write full debug dump [{}]: wrote [{}] of [{}] elements. {}", resolved_path,
                  result.transferred_count, result.requested_count, result.error);
    return false;
  }

  KSJ_LOG_INFO("Output to [{}] successful.", resolved_path);
  return true;
}

} // namespace ksj::process_runtime::debug_dump

#ifndef KSJ_DEBUG_DUMP_ENABLED
#define KSJ_DEBUG_DUMP_ENABLED(category) (::ksj::process_runtime::debug_dump::IsDebugDumpEnabledForCategory(category))
#endif

#ifndef KSJ_DEBUG_DUMP_SLICE_ENABLED
#define KSJ_DEBUG_DUMP_SLICE_ENABLED(category, slice_index)                                                            \
  (::ksj::process_runtime::debug_dump::ShouldDumpSlice((category), (slice_index)))
#endif
