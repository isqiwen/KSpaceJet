#pragma once

#include <string>
#include <string_view>

namespace ksj::config {

inline constexpr char kSiteConfigFileName[] = "config.txt";
inline constexpr char kRuntimeConfigFileExtension[] = ".conf";

[[nodiscard]] std::string primary_site_config_text_path();

[[nodiscard]] std::string runtime_config_file_name(std::string_view executable_config_key);
[[nodiscard]] std::string primary_runtime_config_path(std::string_view executable_config_key);

} // namespace ksj::config
