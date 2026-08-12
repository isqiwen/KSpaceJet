#pragma once

#include <filesystem>

namespace ksj::process_runtime::executable_layout {

[[nodiscard]] std::filesystem::path executable_dir();
[[nodiscard]] std::filesystem::path runtime_layout_root();

} // namespace ksj::process_runtime::executable_layout
