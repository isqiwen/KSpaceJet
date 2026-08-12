#include "kspacejet/process_runtime/crash.hpp"

namespace ksj::process_runtime::crash {

ksj::crash::InstallOptions load_current_executable_crash_install_options() {
  return {};
}

ksj::base::Status install_current_executable_crash_handler(int argc, char* argv[]) {
  return ksj::crash::InstallCrashHandler(argc, argv, load_current_executable_crash_install_options());
}

} // namespace ksj::process_runtime::crash
