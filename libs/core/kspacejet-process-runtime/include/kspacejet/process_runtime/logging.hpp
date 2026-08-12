#pragma once

#include "kspacejet/base/status.hpp"

namespace ksj::process_runtime::logging {

[[nodiscard]] ksj::base::Status configure_current_executable_logging();

} // namespace ksj::process_runtime::logging
