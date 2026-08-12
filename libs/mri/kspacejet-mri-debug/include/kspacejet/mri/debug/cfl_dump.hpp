#pragma once

#include "kspacejet/base/file.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/process_runtime/debug_dump.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ksj::mri::debug {

inline void write_cfl(const char* file_name, const ksj::base::cf32* cpx_data, std::size_t* dims, std::size_t n_dims) {
  if (!ksj::process_runtime::debug_dump::IsDebugDumpEnabledForOutput(file_name == nullptr ? "" : file_name)) {
    return;
  }
  std::size_t n_size = 0;

  std::string name = ksj::process_runtime::debug_dump::ResolveDebugFile(file_name);
  (void)ksj::process_runtime::debug_dump::PrepareDebugFilePath(name);

  std::string header = "# Dimensions\n";
  for (std::size_t i = 0; i < n_dims; i++) {
    header += std::to_string(*(dims + i));
    header += " ";
    if (0 == i) {
      n_size = *(dims + i);
    } else {
      n_size *= *(dims + i);
    }
  }

  const auto header_result = ksj::base::file::write_text_file_atomically(name + ".hdr", header);
  if (!header_result.complete()) {
    KSJ_LOG_ERROR("Failed to write CFL header [{}]: {}", header_result.path, header_result.error);
    return;
  }

  const auto data_result =
    ksj::base::file::write_binary_file_atomically(name + ".cfl", cpx_data, sizeof(ksj::base::cf32), n_size);
  if (!data_result.complete()) {
    KSJ_LOG_ERROR("Failed to write CFL data [{}]: {}", data_result.path, data_result.error);
  }
}

inline void write_cfl(const char* file_base, const float* values, std::size_t* dims, std::size_t n_dims) {
  if (!ksj::process_runtime::debug_dump::IsDebugDumpEnabledForOutput(file_base == nullptr ? "" : file_base)) {
    return;
  }
  std::size_t n_size = 0;
  std::string name = ksj::process_runtime::debug_dump::ResolveDebugFile(file_base);
  (void)ksj::process_runtime::debug_dump::PrepareDebugFilePath(name);

  std::string header = "# Dimensions\n";
  for (std::size_t i = 0; i < n_dims; i++) {
    header += std::to_string(*(dims + i));
    header += " ";
    if (0 == i) {
      n_size = *(dims + i);
    } else {
      n_size *= *(dims + i);
    }
  }
  std::vector<ksj::base::cf32> cpx_values(n_size);
  for (std::size_t i = 0; i < cpx_values.size(); i++) {
    cpx_values[i] = ksj::base::cf32(*(values + i), 0);
  }

  const auto header_result = ksj::base::file::write_text_file_atomically(name + ".hdr", header);
  if (!header_result.complete()) {
    KSJ_LOG_ERROR("Failed to write CFL header [{}]: {}", header_result.path, header_result.error);
    return;
  }

  const auto data_result = ksj::base::file::write_binary_file_atomically(name + ".cfl", cpx_values.data(),
                                                                         sizeof(ksj::base::cf32), cpx_values.size());
  if (!data_result.complete()) {
    KSJ_LOG_ERROR("Failed to write CFL data [{}]: {}", data_result.path, data_result.error);
  }
}

} // namespace ksj::mri::debug
