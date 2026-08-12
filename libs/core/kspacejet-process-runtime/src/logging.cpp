#include "kspacejet/process_runtime/logging.hpp"

#include <string>

#include "kspacejet/logging/logging.hpp"
#include "kspacejet/process_runtime/runtime_config.hpp"
#include "kspacejet/process_runtime/state_paths.hpp"

namespace ksj::process_runtime::logging {

ksj::base::Status configure_current_executable_logging() {
  const auto& cached_config = runtime_config::current_runtime_config();
  if (!cached_config.status.ok()) {
    return cached_config.status;
  }
  if (!cached_config.config.has_value()) {
    return ksj::base::Status::NotFound("Runtime config was not loaded from " +
                                       runtime_config::find_runtime_conf_path());
  }

  std::string error_message;
  if (!ksj::logging::Configure(cached_config.config->logging, state_paths::logging_base_dir_path().c_str(),
                               state_paths::current_executable_logger_name().c_str(),
                               state_paths::current_executable_relative_log_path().c_str(), &error_message)) {
    return ksj::base::Status::ValidationError(error_message);
  }
  return ksj::base::Status::Ok();
}

} // namespace ksj::process_runtime::logging
