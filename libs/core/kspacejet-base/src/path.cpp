#include "kspacejet/base/path.hpp"

#include <cctype>
#include <filesystem>

namespace ksj::base::path {
namespace {

[[nodiscard]] constexpr bool is_separator(const char ch) noexcept {
  return ch == '/' || ch == '\\';
}

[[nodiscard]] std::size_t root_length(std::string_view path) {
  return std::filesystem::path(path).root_path().string().size();
}

[[nodiscard]] bool looks_like_windows_absolute_path(std::string_view value) {
  return value.size() >= 3 && std::isalpha(static_cast<unsigned char>(value[0])) != 0 && value[1] == ':' &&
         (value[2] == '\\' || value[2] == '/');
}

} // namespace

bool has_trailing_separator(std::string_view path) noexcept {
  return !path.empty() && is_separator(path.back());
}

std::string trim_trailing_separator(std::string_view path) {
  std::string trimmed{path};
  const std::size_t min_length = root_length(path);
  while (trimmed.size() > min_length && has_trailing_separator(trimmed)) {
    trimmed.pop_back();
  }
  return trimmed;
}

std::string ensure_trailing_separator(std::string_view path) {
  if (path.empty()) {
    return {};
  }

  std::string normalized = trim_trailing_separator(path);
  if (!has_trailing_separator(normalized)) {
    normalized += std::filesystem::path::preferred_separator;
  }
  return normalized;
}

std::string join(std::string_view lhs, std::string_view rhs) {
  if (lhs.empty()) {
    return std::string{rhs};
  }
  if (rhs.empty()) {
    return std::string{lhs};
  }
  return (std::filesystem::path(lhs) / std::filesystem::path(rhs)).string();
}

std::string basename(std::string_view path) {
  const auto filename = std::filesystem::path(trim_trailing_separator(path)).filename().string();
  return filename.empty() ? std::string{path} : filename;
}

std::string normalize(const std::filesystem::path& path) {
  return path.lexically_normal().string();
}

bool exists(const std::filesystem::path& path) noexcept {
  std::error_code error;
  return std::filesystem::exists(path, error);
}

bool ensure_directory_exists(const std::filesystem::path& path) noexcept {
  if (path.empty()) {
    return false;
  }

  std::error_code error;
  if (std::filesystem::exists(path, error)) {
    return std::filesystem::is_directory(path, error);
  }

  if (!std::filesystem::create_directories(path, error) && error) {
    return false;
  }

  return std::filesystem::is_directory(path, error);
}

bool ensure_parent_directory_exists(const std::filesystem::path& path) noexcept {
  const std::filesystem::path parent = path.parent_path();
  return parent.empty() ? false : ensure_directory_exists(parent);
}

bool is_absolute_path_like(const std::filesystem::path& path) {
  return path.is_absolute() || looks_like_windows_absolute_path(path.string());
}

std::string sanitize_component(std::string_view value) {
  std::string sanitized{value};
  for (char& ch : sanitized) {
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-' && ch != '.') {
      ch = '_';
    }
  }
  return sanitized;
}

std::string format_prepare_directory_error(std::string_view path, std::string_view reason) {
  std::string message{"Failed to prepare KSpaceJet output folder"};
  if (!path.empty()) {
    message += " [";
    message += path;
    message += "]";
  }
  if (!reason.empty()) {
    message += ": ";
    message += reason;
  }
  return message;
}

} // namespace ksj::base::path
