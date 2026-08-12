#include "kspacejet/platform/system.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/sysinfo.h>
#endif

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

namespace ksj::platform {
namespace {

constexpr std::uint64_t kBytesPerKib = 1024;
constexpr std::uint64_t kBytesPerMib = 1024 * 1024;

[[nodiscard]] std::uint64_t kib_from_bytes(const std::uint64_t bytes) noexcept {
  return bytes / kBytesPerKib;
}

[[nodiscard]] std::uint64_t mib_from_bytes(const std::uint64_t bytes) noexcept {
  return bytes / kBytesPerMib;
}

#if !defined(_WIN32)
[[nodiscard]] std::optional<std::uint64_t> read_meminfo_bytes(const std::string_view key) {
  std::ifstream meminfo("/proc/meminfo");
  if (!meminfo.is_open()) {
    return std::nullopt;
  }

  std::string line;
  while (std::getline(meminfo, line)) {
    std::string_view view(line);
    if (!view.starts_with(key)) {
      continue;
    }

    view.remove_prefix(key.size());
    while (!view.empty() && std::isspace(static_cast<unsigned char>(view.front())) != 0) {
      view.remove_prefix(1);
    }

    std::uint64_t value_kib = 0;
    const auto parse_result = std::from_chars(view.data(), view.data() + view.size(), value_kib);
    if (parse_result.ec != std::errc{}) {
      return std::nullopt;
    }
    return value_kib * kBytesPerKib;
  }

  return std::nullopt;
}
#endif

} // namespace

bool is_windows() noexcept {
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

unsigned available_cpu_cores() {
#if defined(_WIN32)
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  return si.dwNumberOfProcessors;
#else
  return static_cast<unsigned>(get_nprocs());
#endif
}

void set_console_title(const char* title) {
#if defined(_WIN32)
  if (title != nullptr) {
    SetConsoleTitleA(title);
  }
#else
  (void)title;
#endif
}

void set_environment_variable_if_unset(const char* name, const char* value) {
  if (name == nullptr || name[0] == '\0' || value == nullptr || value[0] == '\0') {
    return;
  }

#if defined(_WIN32)
  SetLastError(ERROR_SUCCESS);
  const DWORD required_size = GetEnvironmentVariableA(name, nullptr, 0);
  if (required_size != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
    return;
  }
  SetEnvironmentVariableA(name, value);
#else
  if (std::getenv(name) == nullptr) {
    setenv(name, value, 0);
  }
#endif
}

SystemMemoryStatus system_memory_status() {
  SystemMemoryStatus status{};

#if defined(_WIN32)
  MEMORYSTATUSEX state{};
  state.dwLength = sizeof(state);
  if (!GlobalMemoryStatusEx(&state)) {
    return status;
  }

  status.used_percent = static_cast<double>(state.dwMemoryLoad);
  status.memory_unit_bytes = 1;
  status.total_physical_kib = kib_from_bytes(state.ullTotalPhys);
  status.available_physical_kib = kib_from_bytes(state.ullAvailPhys);
  status.total_page_file_kib = kib_from_bytes(state.ullTotalPageFile);
  status.available_page_file_kib = kib_from_bytes(state.ullAvailPageFile);
  status.total_virtual_kib = kib_from_bytes(state.ullTotalVirtual);
  status.available_virtual_kib = kib_from_bytes(state.ullAvailVirtual);

  std::uint64_t recommended = mib_from_bytes(state.ullTotalPhys);
  const std::uint64_t virtual_mib = mib_from_bytes(state.ullTotalVirtual);
  if (virtual_mib > 1024 && recommended > virtual_mib - 1024) {
    recommended = virtual_mib - 1024;
  }
#if !defined(_WIN64)
  if (recommended > 2047) {
    recommended = 2047;
  }
#endif
  status.recommended_aux_memory_mib = recommended;
#else
  struct sysinfo info{};
  if (sysinfo(&info) != 0) {
    return status;
  }

  const auto memory_unit = static_cast<std::uint64_t>(info.mem_unit);
  const std::uint64_t total_physical_bytes = static_cast<std::uint64_t>(info.totalram) * memory_unit;
  const std::uint64_t available_physical_bytes = static_cast<std::uint64_t>(info.freeram) * memory_unit;
  const std::uint64_t total_virtual_bytes = static_cast<std::uint64_t>(info.totalswap) * memory_unit;
  const std::uint64_t available_virtual_bytes = static_cast<std::uint64_t>(info.freeswap) * memory_unit;

  status.memory_unit_bytes = memory_unit;
  status.total_physical_kib = kib_from_bytes(total_physical_bytes);
  status.available_physical_kib = kib_from_bytes(available_physical_bytes);
  status.total_virtual_kib = kib_from_bytes(total_virtual_bytes);
  status.available_virtual_kib = kib_from_bytes(available_virtual_bytes);
  status.recommended_aux_memory_mib = mib_from_bytes(total_physical_bytes);
  if (info.totalram != 0) {
    status.used_percent = (1.0 - static_cast<double>(info.freeram) / static_cast<double>(info.totalram)) * 100.0;
  }
#endif

  return status;
}

std::optional<ProcessMemoryStatus> process_memory_status() {
#if defined(_WIN32)
  PERFORMANCE_INFORMATION info{};
  if (GetPerformanceInfo(&info, static_cast<DWORD>(sizeof(info))) == FALSE) {
    return std::nullopt;
  }

  const auto page_size = static_cast<std::uint64_t>(info.PageSize);
  return ProcessMemoryStatus{
    .current_bytes = static_cast<std::uint64_t>(info.CommitTotal) * page_size,
    .physical_bytes = static_cast<std::uint64_t>(info.PhysicalTotal) * page_size,
    .current_bytes_label = "current committed size",
  };
#else
  const auto total_bytes = read_meminfo_bytes("MemTotal:");
  const auto available_bytes = read_meminfo_bytes("MemAvailable:");
  if (total_bytes.has_value() && available_bytes.has_value()) {
    const auto clamped_available_bytes = std::min(*available_bytes, *total_bytes);
    return ProcessMemoryStatus{
      .current_bytes = *total_bytes - clamped_available_bytes,
      .physical_bytes = *total_bytes,
      .current_bytes_label = "estimated used physical memory",
    };
  }

  struct sysinfo info{};
  if (sysinfo(&info) != 0) {
    return std::nullopt;
  }

  const auto mem_unit = static_cast<std::uint64_t>(info.mem_unit);
  const auto physical_bytes = static_cast<std::uint64_t>(info.totalram) * mem_unit;
  const auto free_bytes = static_cast<std::uint64_t>(info.freeram) * mem_unit;
  return ProcessMemoryStatus{
    .current_bytes = physical_bytes - std::min(free_bytes, physical_bytes),
    .physical_bytes = physical_bytes,
    .current_bytes_label = "used physical memory",
  };
#endif
}

} // namespace ksj::platform
