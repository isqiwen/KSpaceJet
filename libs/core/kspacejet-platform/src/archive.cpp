#include "kspacejet/platform/archive.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <array>
#endif

namespace ksj::platform {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string shell_quote(const fs::path& path) {
  std::string text = path.string();
  std::string quoted = "\"";
  for (char c : text) {
    if (c == '"') {
      quoted += "\\\"";
    } else {
      quoted += c;
    }
  }
  quoted += "\"";
  return quoted;
}

#if defined(_WIN32)
[[nodiscard]] std::string archive_command(const fs::path& source_dir, const fs::path& archive_path) {
  constexpr std::array<std::string_view, 2> k7ZipPaths = {
    "c:\\Program Files (x86)\\7-Zip\\7z.exe",
    "c:\\Program Files\\7-Zip\\7z.exe",
  };

  for (const auto path : k7ZipPaths) {
    if (fs::exists(fs::path(path))) {
      return shell_quote(fs::path(path)) + " a " + shell_quote(archive_path) + " " + shell_quote(source_dir / "*");
    }
  }
  return {};
}
#else
[[nodiscard]] std::string archive_command(const fs::path& source_dir, const fs::path& archive_path) {
  return "cd " + shell_quote(source_dir.parent_path()) + " && zip -9 -r " + shell_quote(archive_path) + " " +
         shell_quote(source_dir.filename());
}
#endif

} // namespace

ksj::base::Status archive_directory_to_zip(const std::filesystem::path& source_dir,
                                           const std::filesystem::path& archive_path) {
  if (source_dir.empty()) {
    return ksj::base::Status::InvalidArgument("archive source directory must not be empty");
  }
  if (archive_path.empty()) {
    return ksj::base::Status::InvalidArgument("archive path must not be empty");
  }

  std::error_code error;
  if (!fs::exists(source_dir, error)) {
    return ksj::base::Status::NotFound("archive source directory does not exist: " + source_dir.string());
  }
  if (error) {
    return ksj::base::Status::IoError("failed to inspect archive source directory '" + source_dir.string() +
                                      "': " + error.message());
  }

  const std::string command = archive_command(source_dir, archive_path);
  if (command.empty()) {
    return ksj::base::Status::NotFound("no zip archive tool is available");
  }

  const int result = std::system(command.c_str());
  if (result != 0) {
    return ksj::base::Status::IoError("zip archive command failed with result " + std::to_string(result) + ": " +
                                      command);
  }
  return ksj::base::Status::Ok();
}

} // namespace ksj::platform
