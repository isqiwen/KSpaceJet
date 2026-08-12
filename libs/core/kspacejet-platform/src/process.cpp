#include "kspacejet/platform/process.hpp"

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace ksj::platform {

std::filesystem::path current_working_directory() {
  std::error_code error;
  std::filesystem::path cwd = std::filesystem::current_path(error);
  return error ? std::filesystem::path(".") : cwd;
}

std::filesystem::path executable_path() {
#ifdef _WIN32
  std::vector<char> buffer(MAX_PATH, '\0');
  DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0) {
    return {};
  }
  while (length >= buffer.size() - 1) {
    buffer.resize(buffer.size() * 2, '\0');
    length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
  }
  return std::filesystem::path(std::string(buffer.data(), length));
#else
  std::vector<char> buffer(PATH_MAX, '\0');
  const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length <= 0) {
    return {};
  }
  buffer[static_cast<std::size_t>(length)] = '\0';
  return std::filesystem::path(buffer.data());
#endif
}

} // namespace ksj::platform
