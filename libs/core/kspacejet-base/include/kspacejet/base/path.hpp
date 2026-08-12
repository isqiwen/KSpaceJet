#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ksj::base::path {

[[nodiscard]] bool has_trailing_separator(std::string_view path) noexcept;
[[nodiscard]] std::string trim_trailing_separator(std::string_view path);
[[nodiscard]] std::string ensure_trailing_separator(std::string_view path);
[[nodiscard]] std::string join(std::string_view lhs, std::string_view rhs);
[[nodiscard]] std::string basename(std::string_view path);
[[nodiscard]] std::string normalize(const std::filesystem::path& path);
[[nodiscard]] bool exists(const std::filesystem::path& path) noexcept;
[[nodiscard]] bool ensure_directory_exists(const std::filesystem::path& path) noexcept;
[[nodiscard]] bool ensure_parent_directory_exists(const std::filesystem::path& path) noexcept;
[[nodiscard]] bool is_absolute_path_like(const std::filesystem::path& path);
[[nodiscard]] std::string sanitize_component(std::string_view value);
[[nodiscard]] std::string format_prepare_directory_error(std::string_view path, std::string_view reason);

} // namespace ksj::base::path
