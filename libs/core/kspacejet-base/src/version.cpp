#include "kspacejet/base/generated_version.hpp"
#include "kspacejet/base/version.hpp"

namespace ksj::base {

BuildInfo build_info() noexcept {
  return BuildInfo{
    KSJ_BASE_PROJECT_NAME,
    KSJ_BASE_PROJECT_VERSION,
    KSJ_BASE_COMPILER_ID " " KSJ_BASE_COMPILER_VERSION,
    KSJ_BASE_BUILD_TYPE,
  };
}

std::string build_summary() {
  const auto info = build_info();
  std::string summary;
  summary.reserve(info.project_name.size() + info.version.size() + info.compiler.size() + info.build_type.size() + 16);
  summary.append(info.project_name);
  summary.push_back(' ');
  summary.append(info.version);
  summary.append(" [");
  summary.append(info.compiler);
  summary.append(", ");
  summary.append(info.build_type);
  summary.push_back(']');
  return summary;
}

VersionInfo component_version(std::string_view component_name) noexcept {
  return VersionInfo{
    .component_name = component_name,
    .version = KSJ_BASE_PROJECT_VERSION,
  };
}

} // namespace ksj::base
