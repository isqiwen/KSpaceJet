#pragma once

#include <string>
#include <string_view>

namespace ksj::base {

struct VersionInfo {
  std::string_view component_name;
  std::string_view version;
};

struct BuildInfo {
  std::string_view project_name;
  std::string_view version;
  std::string_view compiler;
  std::string_view build_type;
};

[[nodiscard]] BuildInfo build_info() noexcept;
[[nodiscard]] std::string build_summary();
[[nodiscard]] VersionInfo component_version(std::string_view component_name) noexcept;

} // namespace ksj::base
