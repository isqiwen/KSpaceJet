#include "kspacejet/platform/filesystem.hpp"

#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ksj::platform {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string quoted_path(const fs::path& path) {
  return "'" + path.string() + "'";
}

[[nodiscard]] bool is_not_found(const std::error_code& error) noexcept {
  return error == std::errc::no_such_file_or_directory;
}

[[nodiscard]] ksj::base::Status inspect_source_directory(const fs::path& source_directory) {
  if (source_directory.empty()) {
    return ksj::base::Status::InvalidArgument("source directory must not be empty");
  }

  std::error_code error;
  const fs::file_status status = fs::symlink_status(source_directory, error);
  if (error) {
    if (is_not_found(error)) {
      return ksj::base::Status::NotFound("source directory does not exist: " + quoted_path(source_directory));
    }
    return ksj::base::Status::IoError("cannot inspect source directory " + quoted_path(source_directory) + ": " +
                                      error.message());
  }
  if (status.type() == fs::file_type::not_found) {
    return ksj::base::Status::NotFound("source directory does not exist: " + quoted_path(source_directory));
  }
  if (!fs::is_directory(status)) {
    return ksj::base::Status::InvalidArgument("source must be a directory: " + quoted_path(source_directory));
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status inspect_destination(const fs::path& destination_directory) {
  if (destination_directory.empty() || destination_directory.filename().empty() ||
      destination_directory.filename() == "." || destination_directory.filename() == "..") {
    return ksj::base::Status::InvalidArgument("destination must name a new directory path");
  }

  std::error_code error;
  fs::path destination_parent = destination_directory.parent_path();
  if (destination_parent.empty()) {
    destination_parent = fs::current_path(error);
    if (error) {
      return ksj::base::Status::IoError("cannot resolve the destination parent directory: " + error.message());
    }
  }

  const fs::file_status parent_status = fs::status(destination_parent, error);
  if (error) {
    if (is_not_found(error)) {
      return ksj::base::Status::NotFound("destination parent directory does not exist: " +
                                         quoted_path(destination_parent));
    }
    return ksj::base::Status::IoError("cannot inspect destination parent directory " + quoted_path(destination_parent) +
                                      ": " + error.message());
  }
  if (parent_status.type() == fs::file_type::not_found) {
    return ksj::base::Status::NotFound("destination parent directory does not exist: " +
                                       quoted_path(destination_parent));
  }
  if (!fs::is_directory(parent_status)) {
    return ksj::base::Status::InvalidArgument("destination parent must be a directory: " +
                                              quoted_path(destination_parent));
  }

  const fs::file_status destination_status = fs::symlink_status(destination_directory, error);
  if (error && !is_not_found(error)) {
    return ksj::base::Status::IoError("cannot inspect destination directory " + quoted_path(destination_directory) +
                                      ": " + error.message());
  }
  if (!error && destination_status.type() != fs::file_type::not_found) {
    return ksj::base::Status::AlreadyExists("destination directory already exists: " +
                                            quoted_path(destination_directory));
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status publish_failure(const fs::path& source_directory, const fs::path& destination_directory,
                                                const std::string& message) {
  return ksj::base::Status::IoError("cannot atomically publish directory " + quoted_path(source_directory) + " as " +
                                    quoted_path(destination_directory) + ": " + message);
}

#if defined(_WIN32)
[[nodiscard]] ksj::base::Status publish_windows(const fs::path& source_directory,
                                                const fs::path& destination_directory) {
  if (::MoveFileExW(source_directory.c_str(), destination_directory.c_str(), MOVEFILE_WRITE_THROUGH) != 0) {
    return ksj::base::Status::Ok();
  }

  const DWORD error = ::GetLastError();
  if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
    return ksj::base::Status::AlreadyExists("destination directory already exists: " +
                                            quoted_path(destination_directory));
  }
  if (error == ERROR_NOT_SAME_DEVICE) {
    return ksj::base::Status::InvalidArgument("source and destination must be on the same filesystem");
  }
  return publish_failure(source_directory, destination_directory,
                         std::error_code(static_cast<int>(error), std::system_category()).message());
}
#elif defined(__linux__)
[[nodiscard]] ksj::base::Status publish_linux(const fs::path& source_directory, const fs::path& destination_directory) {
#if defined(SYS_renameat2)
  constexpr unsigned int kRenameNoReplace = 1U;
  if (::syscall(SYS_renameat2, AT_FDCWD, source_directory.c_str(), AT_FDCWD, destination_directory.c_str(),
                kRenameNoReplace) == 0) {
    return ksj::base::Status::Ok();
  }

  const int error = errno;
  if (error == EEXIST) {
    return ksj::base::Status::AlreadyExists("destination directory already exists: " +
                                            quoted_path(destination_directory));
  }
  if (error == EXDEV) {
    return ksj::base::Status::InvalidArgument("source and destination must be on the same filesystem");
  }
  if (error == ENOSYS || error == EOPNOTSUPP || error == EINVAL) {
    return ksj::base::Status::Unavailable("atomic non-replacing directory publication is unavailable on this Linux "
                                          "kernel or filesystem");
  }
  return publish_failure(source_directory, destination_directory,
                         std::error_code(error, std::generic_category()).message());
#else
  (void)source_directory;
  (void)destination_directory;
  return ksj::base::Status::Unavailable(
    "atomic non-replacing directory publication is unavailable with this Linux libc");
#endif
}
#endif

} // namespace

ksj::base::Status publish_directory_no_replace(const std::filesystem::path& source_directory,
                                               const std::filesystem::path& destination_directory) {
  if (const auto source_status = inspect_source_directory(source_directory); !source_status.ok()) {
    return source_status;
  }
  if (const auto destination_status = inspect_destination(destination_directory); !destination_status.ok()) {
    return destination_status;
  }

#if defined(_WIN32)
  return publish_windows(source_directory, destination_directory);
#elif defined(__linux__)
  return publish_linux(source_directory, destination_directory);
#else
  (void)source_directory;
  (void)destination_directory;
  return ksj::base::Status::Unimplemented("atomic non-replacing directory publication is unsupported on this platform");
#endif
}

} // namespace ksj::platform
