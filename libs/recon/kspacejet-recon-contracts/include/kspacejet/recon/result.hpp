#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/status.hpp"

namespace ksj::recon {

// Reconstruction contracts use the framework-wide structured status model.
// Keeping this alias in the public contract namespace makes API signatures
// stable without inventing a second error transport for the pipeline layer.
template <typename T> using Result = ksj::base::Result<T>;

using Status = ksj::base::Status;
using StatusCode = ksj::base::StatusCode;

} // namespace ksj::recon
