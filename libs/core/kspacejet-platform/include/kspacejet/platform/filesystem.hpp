#pragma once

#include <filesystem>

#include "kspacejet/base/status.hpp"

namespace ksj::platform {

/// Atomically publishes an existing directory at a new, non-existing path.
///
/// The source and destination parent must be on the same filesystem. The
/// operation never replaces an existing destination and never falls back to a
/// copy. On success, source_directory no longer exists and
/// destination_directory names the published directory.
[[nodiscard]] ksj::base::Status publish_directory_no_replace(const std::filesystem::path& source_directory,
                                                             const std::filesystem::path& destination_directory);

} // namespace ksj::platform
