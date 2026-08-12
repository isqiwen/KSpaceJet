#pragma once

#include "kspacejet/base/status.hpp"
#include "kspacejet/crash/handler.hpp"

namespace ksj::process_runtime::crash {

[[nodiscard]] ksj::crash::InstallOptions load_current_executable_crash_install_options();
[[nodiscard]] ksj::base::Status install_current_executable_crash_handler(int argc, char* argv[]);

} // namespace ksj::process_runtime::crash
