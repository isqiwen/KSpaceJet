#include "kspacejet/process_runtime/executable_layout.hpp"

#include "kspacejet/platform/process.hpp"

namespace ksj::process_runtime::executable_layout {

std::filesystem::path executable_dir() {
  const std::filesystem::path path = ksj::platform::executable_path();
  return path.empty() ? ksj::platform::current_working_directory() : path.parent_path();
}

std::filesystem::path runtime_layout_root() {
  return executable_dir() / "..";
}

} // namespace ksj::process_runtime::executable_layout
