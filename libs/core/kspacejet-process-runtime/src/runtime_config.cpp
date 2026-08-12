#include "kspacejet/process_runtime/runtime_config.hpp"

#include <string>

#include "kspacejet/base/path.hpp"
#include "kspacejet/config/site_config.hpp"
#include "kspacejet/process_runtime/executable_layout.hpp"
#include "kspacejet/platform/process.hpp"

namespace ksj::process_runtime::runtime_config {
namespace {

[[nodiscard]] fs::path resolve_executable_relative_layout_path(const fs::path& path) {
  return ksj::base::path::is_absolute_path_like(path) ? path : executable_layout::executable_dir() / path;
}

[[nodiscard]] CachedRuntimeConfig load_current_runtime_config_impl() {
  CachedRuntimeConfig cached;
  cached.source_path = current_executable_runtime_config_path();

  auto loaded = ksj::config::load_runtime_config_file(ksj::base::path::normalize(cached.source_path));
  if (!loaded.ok()) {
    cached.status = loaded.status();
    return cached;
  }

  cached.config = std::move(loaded).value();
  cached.status = ksj::base::Status::Ok();
  return cached;
}

[[nodiscard]] CachedExecutableConfig load_current_executable_config_impl() {
  CachedExecutableConfig cached;
  cached.source_path = resolve_executable_relative_layout_path(ksj::config::primary_site_config_text_path());
  cached.status = ksj::base::Status::Ok();
  return cached;
}

} // namespace

std::string current_executable_name() {
  const fs::path path = ksj::platform::executable_path();
  return path.empty() ? std::string() : path.stem().string();
}

std::string executable_config_key() {
  return current_executable_name();
}

const CachedExecutableConfig& current_executable_config() {
  static const CachedExecutableConfig cached = load_current_executable_config_impl();
  return cached;
}

const ksj::base::Status& current_executable_config_status() {
  return current_executable_config().status;
}

std::string find_site_config_text_path() {
  const fs::path source_path = current_executable_config().source_path;
  if (!source_path.empty()) {
    return ksj::base::path::normalize(source_path);
  }
  return ksj::base::path::normalize(fs::path(ksj::config::primary_site_config_text_path()));
}

std::string find_runtime_config_path() {
  return find_site_config_text_path();
}

fs::path current_executable_runtime_config_path() {
  return resolve_executable_relative_layout_path(ksj::config::primary_runtime_config_path(executable_config_key()));
}

const CachedRuntimeConfig& current_runtime_config() {
  static const CachedRuntimeConfig cached = load_current_runtime_config_impl();
  return cached;
}

const ksj::base::Status& current_runtime_config_status() {
  return current_runtime_config().status;
}

std::string find_runtime_conf_path() {
  const fs::path source_path = current_runtime_config().source_path;
  if (!source_path.empty()) {
    return ksj::base::path::normalize(source_path);
  }

  return ksj::base::path::normalize(current_executable_runtime_config_path());
}

} // namespace ksj::process_runtime::runtime_config
