#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace ksj::platform {

struct SystemMemoryStatus {
  double used_percent{};
  std::uint64_t memory_unit_bytes{};
  std::uint64_t total_physical_kib{};
  std::uint64_t available_physical_kib{};
  std::uint64_t total_virtual_kib{};
  std::uint64_t available_virtual_kib{};
  std::uint64_t total_page_file_kib{};
  std::uint64_t available_page_file_kib{};
  std::uint64_t recommended_aux_memory_mib{};
};

struct ProcessMemoryStatus {
  std::uint64_t current_bytes{};
  std::uint64_t physical_bytes{};
  std::string_view current_bytes_label = "current memory usage";
};

[[nodiscard]] bool is_windows() noexcept;
[[nodiscard]] unsigned available_cpu_cores();
[[nodiscard]] SystemMemoryStatus system_memory_status();
[[nodiscard]] std::optional<ProcessMemoryStatus> process_memory_status();
void set_console_title(const char* title);
void set_environment_variable_if_unset(const char* name, const char* value);

} // namespace ksj::platform
