#pragma once

#include <filesystem>

#include "kspacejet/base/status.hpp"

namespace ksj::platform {

[[nodiscard]] ksj::base::Status archive_directory_to_zip(const std::filesystem::path& source_dir,
                                                         const std::filesystem::path& archive_path);

} // namespace ksj::platform
