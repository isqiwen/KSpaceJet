#pragma once

#include <cstddef>
#include <string_view>

#include "kspacejet/base/status.hpp"

namespace ksj::crash {

struct ThreadCrashContext {
  std::string_view current_message{};
  std::string_view scan_uid{};
  std::string_view scan_title{};
  std::string_view stage{};
  std::string_view ismrmrd_dataset_path{};
  std::string_view output{};
};

struct InstallOptions {
  bool enabled = true;
  const char* debugger_env_var = "USE_GDB_ON_FAULT";
  bool enable_debugger_from_env = true;
  bool install_altstack = true;
  bool capture_terminate = true;
  bool print_readable_stack = true;
  std::size_t max_frames = 200;
};

[[nodiscard]] base::Status InstallCrashHandler(int argc, char* argv[], InstallOptions options = {});

void DumpCurrentThreadStack(std::size_t max_frames = 32);

[[nodiscard]] base::Status RegisterCurrentThread(std::string_view thread_name = {});
void SetThreadCrashContext(const ThreadCrashContext& context) noexcept;
void ClearThreadCrashContext() noexcept;

class ScopedThreadRegistration {
public:
  explicit ScopedThreadRegistration(std::string_view thread_name = {});
  ScopedThreadRegistration(const ScopedThreadRegistration&) = delete;
  ScopedThreadRegistration& operator=(const ScopedThreadRegistration&) = delete;
  ~ScopedThreadRegistration();

  [[nodiscard]] const base::Status& status() const noexcept { return status_; }

private:
  static constexpr std::size_t kStoredThreadNameCapacity = 32;

  base::Status status_{base::Status::Ok()};
  bool had_previous_name_ = false;
  char previous_name_[kStoredThreadNameCapacity]{};
};

} // namespace ksj::crash
