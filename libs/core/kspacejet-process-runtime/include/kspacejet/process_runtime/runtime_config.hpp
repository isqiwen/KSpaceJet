#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "kspacejet/base/status.hpp"
#include "kspacejet/base/result.hpp"
#include "kspacejet/config/runtime_config.hpp"

namespace ksj::process_runtime::runtime_config {

namespace fs = std::filesystem;

struct CachedExecutableConfig {
  fs::path source_path{};
  ksj::base::Status status{ksj::base::Status::Ok()};
};

struct CachedRuntimeConfig {
  fs::path source_path{};
  std::optional<ksj::config::RuntimeConfig> config{};
  ksj::base::Status status{ksj::base::Status::Ok()};
};

[[nodiscard]] std::string current_executable_name();
[[nodiscard]] std::string executable_config_key();

[[nodiscard]] const CachedExecutableConfig& current_executable_config();
[[nodiscard]] const ksj::base::Status& current_executable_config_status();
[[nodiscard]] std::string find_site_config_text_path();
[[nodiscard]] std::string find_runtime_config_path();

[[nodiscard]] fs::path current_executable_runtime_config_path();
[[nodiscard]] const CachedRuntimeConfig& current_runtime_config();
[[nodiscard]] const ksj::base::Status& current_runtime_config_status();
[[nodiscard]] std::string find_runtime_conf_path();

} // namespace ksj::process_runtime::runtime_config
