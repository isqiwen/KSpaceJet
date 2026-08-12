#include "kspacejet/provider/loader/provider_loader.hpp"

#include "kspacejet/platform/dynamic_library.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace ksj::provider::loader::detail {

struct ProviderModuleState {
  ksj::platform::DynamicLibrary library;
  std::filesystem::path loaded_path;
  ProviderDescriptor descriptor;
  ksj_provider_api_v1 api{};
};

} // namespace ksj::provider::loader::detail

namespace ksj::provider::loader {
namespace {

using ksj::base::Result;
using ksj::base::Status;

// A newer host remains able to read an older compatible Provider v1
// descriptor, provided all fields required below fit in its declared size.
constexpr std::uint16_t kMinimumSupportedAbiMinor = 0U;
constexpr std::uint16_t kMaximumSupportedAbiMinor = KSJ_PROVIDER_ABI_MINOR;

[[nodiscard]] std::string bracketed(const std::string_view value) {
  return "[" + std::string(value) + "]";
}

[[nodiscard]] Status validation_error(const std::string_view path, const std::string_view detail) {
  return Status::ValidationError("Provider module " + bracketed(path) + " is invalid: " + std::string(detail));
}

[[nodiscard]] bool is_zero_digest(const Digest256& digest) noexcept {
  return std::all_of(digest.begin(), digest.end(), [](const std::uint8_t byte) {
    return byte == 0U;
  });
}

[[nodiscard]] Status validate_abi_header(const ksj_provider_abi_header& header, const std::size_t required_size,
                                         const std::string_view path, const std::string_view subject) {
  if (static_cast<std::uint64_t>(header.struct_size) < required_size) {
    return validation_error(path, std::string(subject) + " has struct_size " + std::to_string(header.struct_size) +
                                    ", below required " + std::to_string(required_size));
  }
  if (header.abi_major != KSJ_PROVIDER_ABI_MAJOR) {
    return validation_error(path, std::string(subject) + " has ABI major " + std::to_string(header.abi_major) +
                                    ", expected " + std::to_string(KSJ_PROVIDER_ABI_MAJOR));
  }
  if (header.abi_minor < kMinimumSupportedAbiMinor || header.abi_minor > kMaximumSupportedAbiMinor) {
    return validation_error(path,
                            std::string(subject) + " has unsupported ABI minor " + std::to_string(header.abi_minor));
  }
  if (header.reserved[0] != 0U || header.reserved[1] != 0U) {
    return validation_error(path, std::string(subject) + " sets reserved ABI header fields");
  }
  return Status::Ok();
}

[[nodiscard]] bool is_valid_utf8(const std::string_view text) noexcept {
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  std::size_t index = 0U;
  while (index < text.size()) {
    const unsigned char lead = bytes[index++];
    if (lead <= 0x7FU) {
      continue;
    }

    std::size_t continuation_count = 0U;
    std::uint32_t codepoint = 0U;
    if (lead >= 0xC2U && lead <= 0xDFU) {
      continuation_count = 1U;
      codepoint = lead & 0x1FU;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
      continuation_count = 2U;
      codepoint = lead & 0x0FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
      continuation_count = 3U;
      codepoint = lead & 0x07U;
    } else {
      return false;
    }
    if (text.size() - index < continuation_count) {
      return false;
    }
    for (std::size_t continuation = 0U; continuation < continuation_count; ++continuation) {
      const unsigned char byte = bytes[index++];
      if ((byte & 0xC0U) != 0x80U) {
        return false;
      }
      codepoint = (codepoint << 6U) | (byte & 0x3FU);
    }
    if ((continuation_count == 1U && codepoint < 0x80U) || (continuation_count == 2U && codepoint < 0x800U) ||
        (continuation_count == 3U && codepoint < 0x10000U) || (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
        codepoint > 0x10FFFFU) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Result<std::string> copy_utf8_view(const ksj_utf8_view& view, const ProviderLoadOptions& options,
                                                 const std::string_view path, const std::string_view subject,
                                                 const bool require_non_empty) {
  const std::size_t required_size = offsetof(ksj_utf8_view, size) + sizeof(view.size);
  const Status header_status = validate_abi_header(view.abi, required_size, path, subject);
  if (!header_status.ok()) {
    return header_status;
  }
  if (view.size > options.maximum_utf8_bytes || view.size > std::numeric_limits<std::size_t>::max()) {
    return validation_error(path, std::string(subject) + " exceeds configured UTF-8 byte limit");
  }
  if (view.size > 0U && view.data == nullptr) {
    return validation_error(path, std::string(subject) + " has a null data pointer with nonzero length");
  }

  const std::string value(view.data == nullptr ? "" : view.data, static_cast<std::size_t>(view.size));
  if ((require_non_empty && value.empty()) || value.find('\0') != std::string::npos) {
    return validation_error(path, std::string(subject) + " must be non-empty UTF-8 without NUL");
  }
  if (!is_valid_utf8(value)) {
    return validation_error(path, std::string(subject) + " is not valid UTF-8");
  }
  return value;
}

[[nodiscard]] Result<Digest256> copy_digest(const ksj_digest256& digest, const std::string_view path,
                                            const std::string_view subject) {
  const Status header_status = validate_abi_header(digest.abi, sizeof(ksj_digest256), path, subject);
  if (!header_status.ok()) {
    return header_status;
  }
  Digest256 copied{};
  std::copy(std::begin(digest.bytes), std::end(digest.bytes), copied.begin());
  if (is_zero_digest(copied)) {
    return validation_error(path, std::string(subject) + " must not be all zero");
  }
  return copied;
}

[[nodiscard]] bool valid_thread_safety(const ksj_provider_thread_safety value) noexcept {
  return value == KSJ_PROVIDER_SERIAL_INSTANCE || value == KSJ_PROVIDER_SERIAL_PER_KEY_REENTRANT_ACROSS_KEYS ||
         value == KSJ_PROVIDER_FULLY_REENTRANT;
}

[[nodiscard]] Result<OperatorDescriptor> copy_operator_descriptor(const ksj_operator_descriptor& raw,
                                                                  const ProviderLoadOptions& options,
                                                                  const std::string_view path,
                                                                  const std::size_t index) {
  const std::string label = "operator descriptor " + std::to_string(index);
  const std::size_t required_size =
    offsetof(ksj_operator_descriptor, max_async_tail_bytes) + sizeof(raw.max_async_tail_bytes);
  const Status header_status = validate_abi_header(raw.abi, required_size, path, label);
  if (!header_status.ok()) {
    return header_status;
  }
  if (raw.interface_revision == 0U) {
    return validation_error(path, label + " has interface_revision 0");
  }
  if (raw.max_in_flight == 0U) {
    return validation_error(path, label + " has max_in_flight 0");
  }
  if (!valid_thread_safety(raw.thread_safety)) {
    return validation_error(path, label + " has an unknown thread_safety value");
  }

  auto operator_id = copy_utf8_view(raw.operator_id, options, path, label + ".operator_id", true);
  if (!operator_id.ok()) {
    return operator_id.status();
  }
  auto interface_digest = copy_digest(raw.interface_digest, path, label + ".interface_digest");
  if (!interface_digest.ok()) {
    return interface_digest.status();
  }
  auto contract_digest = copy_digest(raw.contract_digest, path, label + ".contract_digest");
  if (!contract_digest.ok()) {
    return contract_digest.status();
  }

  return OperatorDescriptor{
    .capability_bits = raw.abi.capability_bits,
    .operator_id = std::move(operator_id).value(),
    .interface_revision = raw.interface_revision,
    .max_in_flight = raw.max_in_flight,
    .interface_digest = std::move(interface_digest).value(),
    .contract_digest = std::move(contract_digest).value(),
    .thread_safety = raw.thread_safety,
    .max_private_threads = raw.max_private_threads,
    .max_input_items_per_firing = raw.max_input_items_per_firing,
    .max_output_items_per_firing = raw.max_output_items_per_firing,
    .max_output_bytes_per_firing = raw.max_output_bytes_per_firing,
    .max_scratch_bytes_per_firing = raw.max_scratch_bytes_per_firing,
    .max_retained_input_bytes = raw.max_retained_input_bytes,
    .max_async_tail_bytes = raw.max_async_tail_bytes,
  };
}

[[nodiscard]] Result<ProviderDescriptor> copy_provider_descriptor(const ksj_provider_descriptor& raw,
                                                                  const ProviderLoadOptions& options,
                                                                  const std::string_view path) {
  const std::size_t required_size = offsetof(ksj_provider_descriptor, operators) + sizeof(raw.operators);
  const Status header_status = validate_abi_header(raw.abi, required_size, path, "provider descriptor");
  if (!header_status.ok()) {
    return header_status;
  }
  if (raw.provider_abi_major != KSJ_PROVIDER_ABI_MAJOR) {
    return validation_error(path, "provider descriptor has provider ABI major " +
                                    std::to_string(raw.provider_abi_major) + ", expected " +
                                    std::to_string(KSJ_PROVIDER_ABI_MAJOR));
  }
  if (raw.provider_abi_minor < kMinimumSupportedAbiMinor || raw.provider_abi_minor > kMaximumSupportedAbiMinor) {
    return validation_error(path, "provider descriptor has unsupported provider ABI minor " +
                                    std::to_string(raw.provider_abi_minor));
  }
  if (raw.operator_count == 0U || raw.operator_count > options.maximum_operator_count) {
    return validation_error(path, "provider descriptor operator_count is zero or exceeds configured limit");
  }
  if (raw.operators == nullptr) {
    return validation_error(path, "provider descriptor has a null operators array");
  }
  if (raw.reserved0 != 0U) {
    return validation_error(path, "provider descriptor sets its reserved0 field");
  }

  const std::size_t version_size = offsetof(ksj_provider_version, prerelease) + sizeof(raw.version.prerelease);
  const Status version_status = validate_abi_header(raw.version.abi, version_size, path, "provider version");
  if (!version_status.ok()) {
    return version_status;
  }

  auto provider_id = copy_utf8_view(raw.provider_id, options, path, "provider_id", true);
  if (!provider_id.ok()) {
    return provider_id.status();
  }
  auto bundle_digest = copy_digest(raw.bundle_digest, path, "bundle_digest");
  if (!bundle_digest.ok()) {
    return bundle_digest.status();
  }
  if (options.required_bundle_digest.has_value() && bundle_digest.value() != *options.required_bundle_digest) {
    return validation_error(path, "provider descriptor bundle_digest does not match the frozen trusted digest");
  }

  ProviderDescriptor descriptor{
    .capability_bits = raw.abi.capability_bits,
    .provider_id = std::move(provider_id).value(),
    .version = {.major = raw.version.major,
                .minor = raw.version.minor,
                .patch = raw.version.patch,
                .prerelease = raw.version.prerelease},
    .provider_abi_major = raw.provider_abi_major,
    .provider_abi_minor = raw.provider_abi_minor,
    .bundle_digest = std::move(bundle_digest).value(),
    .operators = {},
  };
  descriptor.operators.reserve(raw.operator_count);
  std::set<std::string, std::less<>> unique_operator_ids;
  for (std::uint32_t index = 0U; index < raw.operator_count; ++index) {
    auto operator_descriptor = copy_operator_descriptor(raw.operators[index], options, path, index);
    if (!operator_descriptor.ok()) {
      return operator_descriptor.status();
    }
    if (!unique_operator_ids.insert(operator_descriptor.value().operator_id).second) {
      return validation_error(path, "provider descriptor contains duplicate operator_id " +
                                      bracketed(operator_descriptor.value().operator_id));
    }
    descriptor.operators.push_back(std::move(operator_descriptor).value());
  }
  return descriptor;
}

[[nodiscard]] Status validate_required_operator_contracts(const ProviderDescriptor& descriptor,
                                                          const ProviderLoadOptions& options,
                                                          const std::string_view path) {
  for (const auto& requirement : options.required_operator_contracts) {
    const auto operator_descriptor = std::find_if(descriptor.operators.begin(), descriptor.operators.end(),
                                                  [&requirement](const OperatorDescriptor& value) {
                                                    return value.operator_id == requirement.operator_id;
                                                  });
    if (operator_descriptor == descriptor.operators.end()) {
      return validation_error(path, "provider descriptor does not expose required operator_id " +
                                      bracketed(requirement.operator_id));
    }
    if (operator_descriptor->contract_digest != requirement.contract_digest) {
      return validation_error(path, "operator " + bracketed(requirement.operator_id) +
                                      " contract_digest does not match the frozen required digest");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_api(const ksj_provider_api_v1& api, const std::string_view path) {
  const std::size_t required_size = offsetof(ksj_provider_api_v1, operator_destroy) + sizeof(api.operator_destroy);
  const Status header_status = validate_abi_header(api.abi, required_size, path, "provider API table");
  if (!header_status.ok()) {
    return header_status;
  }

  if (api.operator_create == nullptr || api.execution_context_create == nullptr || api.key_state_init == nullptr ||
      api.operator_on_start == nullptr || api.operator_process_batch == nullptr ||
      api.operator_on_scan_end == nullptr || api.operator_on_cancel == nullptr || api.key_state_reset == nullptr ||
      api.execution_context_destroy == nullptr || api.operator_destroy == nullptr) {
    return validation_error(path, "provider API table omits a mandatory lifecycle callback");
  }
  return Status::Ok();
}

[[nodiscard]] std::string describe_query_error(const ksj_error_view& error, const ProviderLoadOptions& options) {
  constexpr std::size_t kErrorRequiredSize = offsetof(ksj_error_view, message) + sizeof(error.message);
  if (error.abi.struct_size < kErrorRequiredSize || error.abi.abi_major != KSJ_PROVIDER_ABI_MAJOR ||
      error.abi.abi_minor > kMaximumSupportedAbiMinor) {
    return "Provider returned status " + std::to_string(error.status) + " without a valid error view";
  }
  const auto message = copy_utf8_view(error.message, options, "<query-error>", "provider error message", false);
  if (!message.ok() || message.value().empty()) {
    return "Provider returned status " + std::to_string(error.status);
  }
  return "Provider returned status " + std::to_string(error.status) + ": " + message.value();
}

[[nodiscard]] Result<std::filesystem::path> canonical_trusted_path(const std::filesystem::path& path,
                                                                   const ProviderLoadOptions& options) {
  if (path.empty()) {
    return Status::InvalidArgument("Provider module path must not be empty");
  }
  if (!path.is_absolute()) {
    return Status::InvalidArgument("Provider module path must be an explicit absolute trusted file path");
  }

  std::error_code error;
  const std::filesystem::path canonical = std::filesystem::canonical(path, error);
  if (error) {
    return Status::Unavailable("Unable to canonicalize provider module path [" + path.string() +
                               "]: " + error.message());
  }
  if (!std::filesystem::is_regular_file(canonical, error) || error) {
    return Status::InvalidArgument("Provider module path is not a regular file [" + canonical.string() + "]");
  }

  if (!options.trusted_root.empty()) {
    const std::filesystem::path canonical_root = std::filesystem::canonical(options.trusted_root, error);
    if (error || !std::filesystem::is_directory(canonical_root, error) || error) {
      return Status::InvalidArgument("Configured provider trusted_root is not a canonical directory [" +
                                     options.trusted_root.string() + "]");
    }
    const std::filesystem::path relative = canonical.lexically_relative(canonical_root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
      return Status::InvalidArgument("Provider module path [" + canonical.string() +
                                     "] is outside the configured trusted_root [" + canonical_root.string() + "]");
    }
  }
  return canonical;
}

[[nodiscard]] Status validate_options(const ProviderLoadOptions& options) {
  if (options.maximum_operator_count == 0U) {
    return Status::InvalidArgument("Provider loader maximum_operator_count must be nonzero");
  }
  if (options.maximum_utf8_bytes == 0U) {
    return Status::InvalidArgument("Provider loader maximum_utf8_bytes must be nonzero");
  }
  if (options.host_build_id.empty() || options.host_build_id.find('\0') != std::string::npos ||
      !is_valid_utf8(options.host_build_id)) {
    return Status::InvalidArgument("Provider loader host_build_id must be non-empty valid UTF-8 without NUL");
  }
  if (options.host_build_id.size() > options.maximum_utf8_bytes) {
    return Status::InvalidArgument("Provider loader host_build_id exceeds maximum_utf8_bytes");
  }
  if (options.required_operator_contracts.size() > options.maximum_operator_count) {
    return Status::InvalidArgument("Provider loader required_operator_contracts exceeds maximum_operator_count");
  }

  std::set<std::string, std::less<>> required_operator_ids;
  for (std::size_t index = 0U; index < options.required_operator_contracts.size(); ++index) {
    const auto& requirement = options.required_operator_contracts[index];
    const std::string label = "Provider loader required_operator_contracts[" + std::to_string(index) + "]";
    if (requirement.operator_id.empty() || requirement.operator_id.find('\0') != std::string::npos ||
        !is_valid_utf8(requirement.operator_id)) {
      return Status::InvalidArgument(label + ".operator_id must be non-empty valid UTF-8 without NUL");
    }
    if (requirement.operator_id.size() > options.maximum_utf8_bytes) {
      return Status::InvalidArgument(label + ".operator_id exceeds maximum_utf8_bytes");
    }
    if (is_zero_digest(requirement.contract_digest)) {
      return Status::InvalidArgument(label + ".contract_digest must not be all zero");
    }
    if (!required_operator_ids.insert(requirement.operator_id).second) {
      return Status::InvalidArgument(label + ".operator_id duplicates an earlier contract requirement");
    }
  }
  return Status::Ok();
}

[[nodiscard]] ksj_provider_abi_header make_header(const std::uint32_t size) {
  return ksj_provider_abi_header_make(size, 0U);
}

} // namespace

ProviderLease::ProviderLease(std::shared_ptr<const detail::ProviderModuleState> state) noexcept
    : state_(std::move(state)) {}

bool ProviderLease::valid() const noexcept {
  return state_ != nullptr;
}

const std::filesystem::path* ProviderLease::loaded_path() const noexcept {
  return state_ == nullptr ? nullptr : &state_->loaded_path;
}

const ProviderDescriptor* ProviderLease::descriptor() const noexcept {
  return state_ == nullptr ? nullptr : &state_->descriptor;
}

const ksj_provider_api_v1* ProviderLease::api() const noexcept {
  return state_ == nullptr ? nullptr : &state_->api;
}

ProviderModule::ProviderModule(std::shared_ptr<const detail::ProviderModuleState> state) noexcept
    : state_(std::move(state)) {}

Result<ProviderModule> ProviderModule::load(const std::filesystem::path& immutable_trusted_path,
                                            ProviderLoadOptions options) {
  const Status option_status = validate_options(options);
  if (!option_status.ok()) {
    return option_status;
  }
  auto canonical_path = canonical_trusted_path(immutable_trusted_path, options);
  if (!canonical_path.ok()) {
    return canonical_path.status();
  }

  auto state = std::make_shared<detail::ProviderModuleState>();
  const Status open_status =
    state->library.open(canonical_path.value(), ksj::platform::LoadMode::now | ksj::platform::LoadMode::local);
  if (!open_status.ok()) {
    return Status::Unavailable("Unable to load trusted Provider module [" + canonical_path.value().string() +
                               "]: " + open_status.message());
  }
  state->loaded_path = canonical_path.value();

  auto query_symbol = state->library.symbol_as<ksj_provider_query_fn>("ksj_provider_query");
  if (!query_symbol.ok()) {
    return Status::NotFound(
      "Trusted Provider module [" + state->loaded_path.string() +
      "] does not export required symbol [ksj_provider_query]: " + query_symbol.status().message());
  }

  ksj_utf8_view host_build_id{};
  host_build_id.abi = make_header(sizeof(host_build_id));
  host_build_id.data = options.host_build_id.data();
  host_build_id.size = static_cast<std::uint64_t>(options.host_build_id.size());

  ksj_provider_query_request request{};
  request.abi = make_header(sizeof(request));
  request.minimum_abi_minor = kMinimumSupportedAbiMinor;
  request.maximum_abi_minor = kMaximumSupportedAbiMinor;
  request.host_capability_bits = options.host_capability_bits;
  request.host_build_id = host_build_id;

  ksj_provider_descriptor raw_descriptor{};
  raw_descriptor.abi = make_header(sizeof(raw_descriptor));
  ksj_provider_api_v1 raw_api{};
  raw_api.abi = make_header(sizeof(raw_api));
  ksj_error_view raw_error{};
  raw_error.abi = make_header(sizeof(raw_error));
  raw_error.message.abi = make_header(sizeof(raw_error.message));

  ksj_status query_status = KSJ_STATUS_INTERNAL_ERROR;
  try {
    query_status = query_symbol.value()(&request, &raw_descriptor, &raw_api, &raw_error);
  } catch (...) {
    return Status::InternalError("Trusted Provider module [" + state->loaded_path.string() +
                                 "] allowed an exception to cross ksj_provider_query");
  }
  if (query_status != KSJ_STATUS_OK) {
    return Status::ValidationError("Trusted Provider module [" + state->loaded_path.string() +
                                   "] rejected ABI query: " + describe_query_error(raw_error, options));
  }

  auto descriptor = copy_provider_descriptor(raw_descriptor, options, state->loaded_path.string());
  if (!descriptor.ok()) {
    return descriptor.status();
  }
  const Status contract_status =
    validate_required_operator_contracts(descriptor.value(), options, state->loaded_path.string());
  if (!contract_status.ok()) {
    return contract_status;
  }
  const Status api_status = validate_api(raw_api, state->loaded_path.string());
  if (!api_status.ok()) {
    return api_status;
  }

  state->descriptor = std::move(descriptor).value();
  state->api = raw_api;
  return ProviderModule(std::move(state));
}

bool ProviderModule::loaded() const noexcept {
  return state_ != nullptr;
}

const std::filesystem::path* ProviderModule::loaded_path() const noexcept {
  return state_ == nullptr ? nullptr : &state_->loaded_path;
}

const ProviderDescriptor* ProviderModule::descriptor() const noexcept {
  return state_ == nullptr ? nullptr : &state_->descriptor;
}

ProviderLease ProviderModule::acquire() const noexcept {
  return ProviderLease(state_);
}

} // namespace ksj::provider::loader
