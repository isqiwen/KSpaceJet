#include "kspacejet/config/site_config.hpp"

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

namespace ksj::config {
namespace {

std::string join_native_path(std::initializer_list<std::string_view> parts) {
  std::filesystem::path path;
  for (const auto part : parts) {
    path /= std::string(part);
  }
  path.make_preferred();
  return path.string();
}

} // namespace

std::string primary_site_config_text_path() {
  return join_native_path({"..", "..", "Site_config", kSiteConfigFileName});
}

std::string runtime_config_file_name(std::string_view executable_config_key) {
  std::string filename(executable_config_key);
  filename += kRuntimeConfigFileExtension;
  return filename;
}

std::string primary_runtime_config_path(std::string_view executable_config_key) {
  const std::string file_name = runtime_config_file_name(executable_config_key);
  return join_native_path({"..", "config", file_name});
}

} // namespace ksj::config
