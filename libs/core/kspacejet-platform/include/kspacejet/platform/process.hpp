#pragma once

#include <filesystem>

namespace ksj::platform {

[[nodiscard]] std::filesystem::path current_working_directory();
[[nodiscard]] std::filesystem::path executable_path();

} // namespace ksj::platform
