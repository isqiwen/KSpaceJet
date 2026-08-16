#pragma once

#include <filesystem>
#include <string>

namespace ksj::cli {

enum class ProviderInitOutcome {
  success,
  invalid_request,
  unavailable,
  io_error,
};

struct ProviderInitRequest {
  std::string provider_slug;
  std::string operator_id;
  std::filesystem::path output_parent;
};

struct ProviderInitResult {
  ProviderInitOutcome outcome{ProviderInitOutcome::io_error};
  std::filesystem::path provider_directory;
  std::string message;
};

[[nodiscard]] ProviderInitResult initialize_provider(const ProviderInitRequest& request);

} // namespace ksj::cli
