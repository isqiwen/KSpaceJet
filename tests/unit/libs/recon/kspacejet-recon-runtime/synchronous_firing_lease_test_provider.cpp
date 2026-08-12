#include "kspacejet/provider/v1/provider.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

enum class TestProviderMode : std::uint32_t {
  done_output,
  unsettled_output,
  yield_consumed,
  yield_sealed,
  yield_clean,
  retain_input,
  async_pending,
  metadata_output,
  undersized_info,
  throws_across_abi,
  direct_contract_violation,
};

struct ksj_provider_operator {
  TestProviderMode mode;
};
struct ksj_execution_context {};
struct ksj_key_state {};

namespace {

constexpr char kProviderId[] = "org.kspacejet.tests.synchronous-firing-lease";
constexpr char kOperatorId[] = "synchronous_firing_lease_test_operator";
constexpr char kError[] = "test Provider rejected ABI input";

ksj_provider_operator kDoneOutput{TestProviderMode::done_output};
ksj_provider_operator kUnsettledOutput{TestProviderMode::unsettled_output};
ksj_provider_operator kYieldConsumed{TestProviderMode::yield_consumed};
ksj_provider_operator kYieldSealed{TestProviderMode::yield_sealed};
ksj_provider_operator kYieldClean{TestProviderMode::yield_clean};
ksj_provider_operator kRetainInput{TestProviderMode::retain_input};
ksj_provider_operator kAsyncPending{TestProviderMode::async_pending};
ksj_provider_operator kMetadataOutput{TestProviderMode::metadata_output};
ksj_provider_operator kUndersizedInfo{TestProviderMode::undersized_info};
ksj_provider_operator kThrowsAcrossAbi{TestProviderMode::throws_across_abi};
ksj_provider_operator kDirectContractViolation{TestProviderMode::direct_contract_violation};
ksj_execution_context kContext;
ksj_key_state kKeyState;

[[nodiscard]] ksj_provider_abi_header header(const std::uint32_t size, const std::uint64_t capabilities = 0U) {
  return ksj_provider_abi_header_make(size, capabilities);
}

[[nodiscard]] bool full_header(const ksj_provider_abi_header* value, const std::size_t size) {
  return value != nullptr && value->struct_size >= size && value->abi_major == KSJ_PROVIDER_ABI_MAJOR &&
         value->abi_minor <= KSJ_PROVIDER_ABI_MINOR;
}

template <typename T> [[nodiscard]] bool full_header(const T* value) {
  return value != nullptr && full_header(&value->abi, sizeof(T));
}

[[nodiscard]] ksj_utf8_view text(const char* data, const std::uint64_t size) {
  ksj_utf8_view value{};
  value.abi = header(sizeof(value));
  value.data = data;
  value.size = size;
  return value;
}

[[nodiscard]] ksj_digest256 digest(const std::uint8_t seed) {
  ksj_digest256 value{};
  value.abi = header(sizeof(value));
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    value.bytes[index] = static_cast<std::uint8_t>(seed + index);
  }
  return value;
}

void fail(ksj_error_view* out_error, const ksj_status status) {
  if (!full_header(out_error)) {
    return;
  }
  *out_error = {};
  out_error->abi = header(sizeof(*out_error));
  out_error->status = status;
  out_error->message = text(kError, sizeof(kError) - 1U);
}

[[nodiscard]] bool same(const ksj_utf8_view& value, const char* expected, const std::size_t expected_size) {
  return full_header(&value) && value.data != nullptr && value.size == expected_size &&
         std::memcmp(value.data, expected, expected_size) == 0;
}

[[nodiscard]] bool config_is(const ksj_byte_view& value, const char* expected, const std::size_t expected_size) {
  return full_header(&value) && value.data != nullptr && value.size == expected_size &&
         std::memcmp(value.data, expected, expected_size) == 0;
}

[[nodiscard]] ksj_provider_operator* operator_for_config(const ksj_byte_view& config) {
  if (config_is(config, "{\"mode\":\"done-output\"}", sizeof("{\"mode\":\"done-output\"}") - 1U)) {
    return &kDoneOutput;
  }
  if (config_is(config, "{\"mode\":\"unsettled-output\"}", sizeof("{\"mode\":\"unsettled-output\"}") - 1U)) {
    return &kUnsettledOutput;
  }
  if (config_is(config, "{\"mode\":\"yield-consumed\"}", sizeof("{\"mode\":\"yield-consumed\"}") - 1U)) {
    return &kYieldConsumed;
  }
  if (config_is(config, "{\"mode\":\"yield-sealed\"}", sizeof("{\"mode\":\"yield-sealed\"}") - 1U)) {
    return &kYieldSealed;
  }
  if (config_is(config, "{\"mode\":\"yield-clean\"}", sizeof("{\"mode\":\"yield-clean\"}") - 1U)) {
    return &kYieldClean;
  }
  if (config_is(config, "{\"mode\":\"retain-input\"}", sizeof("{\"mode\":\"retain-input\"}") - 1U)) {
    return &kRetainInput;
  }
  if (config_is(config, "{\"mode\":\"async-pending\"}", sizeof("{\"mode\":\"async-pending\"}") - 1U)) {
    return &kAsyncPending;
  }
  if (config_is(config, "{\"mode\":\"metadata-output\"}", sizeof("{\"mode\":\"metadata-output\"}") - 1U)) {
    return &kMetadataOutput;
  }
  if (config_is(config, "{\"mode\":\"undersized-info\"}", sizeof("{\"mode\":\"undersized-info\"}") - 1U)) {
    return &kUndersizedInfo;
  }
  if (config_is(config, "{\"mode\":\"throws-across-abi\"}", sizeof("{\"mode\":\"throws-across-abi\"}") - 1U)) {
    return &kThrowsAcrossAbi;
  }
  if (config_is(config, "{\"mode\":\"direct-contract-violation\"}",
                sizeof("{\"mode\":\"direct-contract-violation\"}") - 1U)) {
    return &kDirectContractViolation;
  }
  return nullptr;
}

void write_result(ksj_process_result* out_result, const ksj_provider_process_outcome outcome,
                  const std::uint32_t sealed_output_count, const std::uint64_t consumed_input_item_count,
                  const std::uint64_t terminal_epoch) {
  *out_result = {};
  out_result->abi = header(sizeof(*out_result));
  out_result->outcome = outcome;
  out_result->sealed_output_count = sealed_output_count;
  out_result->consumed_input_item_count = consumed_input_item_count;
  out_result->terminal_epoch = terminal_epoch;
}

[[nodiscard]] ksj_status get_lease_info(const ksj_firing_lease_callbacks_v1* callbacks, ksj_firing_lease* lease,
                                        ksj_firing_lease_info* out_info, ksj_error_view* out_error) {
  if (!full_header(callbacks) || callbacks->get_info == nullptr) {
    return KSJ_STATUS_BAD_ABI;
  }
  out_info->abi = header(sizeof(*out_info));
  return callbacks->get_info(callbacks->host_context, lease, out_info, out_error);
}

[[nodiscard]] ksj_status produce_one(const ksj_firing_lease_callbacks_v1* callbacks, ksj_firing_lease* lease,
                                     const bool settle, const bool include_metadata, std::uint32_t& sealed_count,
                                     ksj_error_view* out_error) {
  if (!full_header(callbacks) || callbacks->acquire_output_grant == nullptr || callbacks->output_grants == nullptr ||
      !full_header(callbacks->output_grants) || callbacks->output_grants->map_mutable_payload == nullptr ||
      callbacks->output_grants->seal == nullptr) {
    return KSJ_STATUS_BAD_ABI;
  }
  ksj_output_grant* grant = nullptr;
  const auto acquire = callbacks->acquire_output_grant(callbacks->host_context, lease, 0U, &grant, out_error);
  if (acquire != KSJ_STATUS_OK) {
    return acquire;
  }
  ksj_mutable_payload_view payload{};
  payload.abi = header(sizeof(payload));
  const auto map =
    callbacks->output_grants->map_mutable_payload(callbacks->output_grants->host_context, grant, &payload, out_error);
  if (map != KSJ_STATUS_OK || !full_header(&payload) || payload.capacity_bytes < 4U || payload.data == nullptr) {
    return map == KSJ_STATUS_OK ? KSJ_STATUS_CONTRACT_VIOLATION : map;
  }
  std::memset(payload.data, 0x5AU, 4U);
  if (!settle) {
    return KSJ_STATUS_OK;
  }
  ksj_output_seal_descriptor seal{};
  seal.abi = header(sizeof(seal));
  seal.output_port = 0U;
  seal.produced_item_count = 1U;
  seal.produced_byte_count = 4U;
  seal.type = payload.type;
  seal.metadata.abi = header(sizeof(seal.metadata));
  std::array<char, 9U> transient_metadata{{'t', 'e', 's', 't', '-', 'm', 'e', 't', 'a'}};
  if (include_metadata) {
    seal.metadata.data = transient_metadata.data();
    seal.metadata.size = transient_metadata.size();
  }
  const auto sealed = callbacks->output_grants->seal(callbacks->output_grants->host_context, grant, &seal, out_error);
  if (sealed == KSJ_STATUS_OK) {
    ++sealed_count;
  }
  return sealed;
}

ksj_status KSJ_PROVIDER_CALL operator_create(const ksj_operator_create_request* request,
                                             ksj_provider_operator** out_operator, ksj_error_view* out_error) {
  if (!full_header(request) || out_operator == nullptr ||
      !same(request->operator_id, kOperatorId, sizeof(kOperatorId) - 1U)) {
    fail(out_error, KSJ_STATUS_INVALID_ARGUMENT);
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  auto* result = operator_for_config(request->canonical_config);
  if (result == nullptr) {
    fail(out_error, KSJ_STATUS_INVALID_ARGUMENT);
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_operator = result;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL execution_context_create(ksj_provider_operator* operator_handle,
                                                      const ksj_execution_context_descriptor* descriptor,
                                                      ksj_execution_context** out_context, ksj_error_view* out_error) {
  if (operator_handle == nullptr || !full_header(descriptor) || out_context == nullptr) {
    fail(out_error, KSJ_STATUS_INVALID_ARGUMENT);
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_context = &kContext;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL key_state_init(ksj_provider_operator* operator_handle, ksj_execution_context* context,
                                            const ksj_key_state_descriptor* descriptor, ksj_key_state** out_key_state,
                                            ksj_error_view* out_error) {
  if (operator_handle == nullptr || context != &kContext || !full_header(descriptor) || out_key_state == nullptr) {
    fail(out_error, KSJ_STATUS_INVALID_ARGUMENT);
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_key_state = &kKeyState;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_start(ksj_provider_operator*, ksj_execution_context*, ksj_key_state*,
                                               const ksj_scan_start_descriptor*, ksj_error_view*) {
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_process_batch(ksj_provider_operator* operator_handle,
                                                    ksj_execution_context* context, ksj_key_state* key_state,
                                                    ksj_firing_lease* lease,
                                                    const ksj_firing_lease_callbacks_v1* callbacks,
                                                    ksj_process_result* out_result, ksj_error_view* out_error) {
  if (operator_handle == nullptr || context != &kContext || key_state != &kKeyState || lease == nullptr ||
      !full_header(out_result)) {
    fail(out_error, KSJ_STATUS_INVALID_ARGUMENT);
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  if (operator_handle->mode == TestProviderMode::throws_across_abi) {
    throw 7;
  }
  if (operator_handle->mode == TestProviderMode::direct_contract_violation) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ksj_firing_lease_info info{};
  if (operator_handle->mode == TestProviderMode::undersized_info) {
    info.abi = header(static_cast<std::uint32_t>(offsetof(ksj_firing_lease_info, terminal_epoch)));
    return callbacks->get_info(callbacks->host_context, lease, &info, out_error);
  }
  const auto info_status = get_lease_info(callbacks, lease, &info, out_error);
  if (info_status != KSJ_STATUS_OK) {
    return info_status;
  }

  if (operator_handle->mode == TestProviderMode::retain_input) {
    ksj_retention_handle* retention = nullptr;
    return callbacks->retain_input(callbacks->host_context, lease, 0U, 0U, nullptr, &retention, out_error);
  }
  if (operator_handle->mode == TestProviderMode::async_pending) {
    ksj_async_token* token = nullptr;
    const auto registration = callbacks->register_async(callbacks->host_context, lease, nullptr, &token, out_error);
    if (registration != KSJ_STATUS_OK) {
      write_result(out_result, KSJ_PROVIDER_PROCESS_ASYNC_PENDING, 0U, 0U, info.terminal_epoch);
      return KSJ_STATUS_OK;
    }
    write_result(out_result, KSJ_PROVIDER_PROCESS_ASYNC_PENDING, 0U, 0U, info.terminal_epoch);
    return KSJ_STATUS_OK;
  }
  if (operator_handle->mode == TestProviderMode::yield_consumed) {
    write_result(out_result, KSJ_PROVIDER_PROCESS_YIELD, 0U, 1U, info.terminal_epoch);
    return KSJ_STATUS_OK;
  }
  if (operator_handle->mode == TestProviderMode::yield_clean) {
    write_result(out_result, KSJ_PROVIDER_PROCESS_YIELD, 0U, 0U, info.terminal_epoch);
    return KSJ_STATUS_OK;
  }

  std::uint32_t sealed_count = 0U;
  if (operator_handle->mode == TestProviderMode::unsettled_output) {
    const auto status = produce_one(callbacks, lease, false, false, sealed_count, out_error);
    if (status != KSJ_STATUS_OK) {
      return status;
    }
    write_result(out_result, KSJ_PROVIDER_PROCESS_DONE, 0U, info.input_batch_count, info.terminal_epoch);
    return KSJ_STATUS_OK;
  }
  const auto output_status = produce_one(
    callbacks, lease, true, operator_handle->mode == TestProviderMode::metadata_output, sealed_count, out_error);
  if (output_status != KSJ_STATUS_OK) {
    return output_status;
  }
  if (operator_handle->mode == TestProviderMode::yield_sealed) {
    write_result(out_result, KSJ_PROVIDER_PROCESS_YIELD, sealed_count, 0U, info.terminal_epoch);
    return KSJ_STATUS_OK;
  }
  write_result(out_result, KSJ_PROVIDER_PROCESS_DONE, sealed_count, info.input_batch_count, info.terminal_epoch);
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_scan_end(ksj_provider_operator* operator_handle,
                                                  ksj_execution_context* context, ksj_key_state* key_state,
                                                  const ksj_scan_end_descriptor* descriptor,
                                                  ksj_firing_lease* terminal_lease,
                                                  const ksj_firing_lease_callbacks_v1* callbacks,
                                                  ksj_process_result* out_result, ksj_error_view* out_error) {
  if (operator_handle == nullptr || context != &kContext || key_state != &kKeyState || !full_header(descriptor) ||
      descriptor->kind != KSJ_PROVIDER_SCAN_END_NORMAL || terminal_lease == nullptr || !full_header(out_result)) {
    fail(out_error, KSJ_STATUS_INVALID_ARGUMENT);
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  ksj_firing_lease_info info{};
  const auto info_status = get_lease_info(callbacks, terminal_lease, &info, out_error);
  if (info_status != KSJ_STATUS_OK) {
    return info_status;
  }
  std::uint32_t sealed_count = 0U;
  const auto output_status = produce_one(callbacks, terminal_lease, true, false, sealed_count, out_error);
  if (output_status != KSJ_STATUS_OK) {
    return output_status;
  }
  write_result(out_result, KSJ_PROVIDER_PROCESS_DONE, sealed_count, 0U, descriptor->terminal_epoch);
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_cancel(ksj_provider_operator*, ksj_execution_context*, ksj_key_state*,
                                                const ksj_cancel_context*, ksj_error_view*) {
  return KSJ_STATUS_OK;
}

void KSJ_PROVIDER_CALL key_state_reset(ksj_provider_operator*, ksj_execution_context*, ksj_key_state*) {}
void KSJ_PROVIDER_CALL execution_context_destroy(ksj_provider_operator*, ksj_execution_context*) {}
void KSJ_PROVIDER_CALL operator_destroy(ksj_provider_operator*) {}

[[nodiscard]] ksj_operator_descriptor operator_descriptor() {
  ksj_operator_descriptor value{};
  value.abi = header(sizeof(value), KSJ_OPERATOR_CAP_MAY_EMIT_TERMINAL_OUTPUT);
  value.operator_id = text(kOperatorId, sizeof(kOperatorId) - 1U);
  value.interface_revision = 1U;
  value.max_in_flight = 1U;
  value.interface_digest = digest(0x10U);
  value.contract_digest = digest(0x40U);
  value.thread_safety = KSJ_PROVIDER_SERIAL_INSTANCE;
  value.max_input_items_per_firing = 4U;
  value.max_output_items_per_firing = 2U;
  value.max_output_bytes_per_firing = 64U;
  value.max_scratch_bytes_per_firing = 16U;
  return value;
}

[[nodiscard]] ksj_provider_api_v1 api() {
  ksj_provider_api_v1 value{};
  value.abi = header(sizeof(value));
  value.operator_create = &operator_create;
  value.execution_context_create = &execution_context_create;
  value.key_state_init = &key_state_init;
  value.operator_on_start = &operator_on_start;
  value.operator_process_batch = &operator_process_batch;
  value.operator_on_scan_end = &operator_on_scan_end;
  value.operator_on_cancel = &operator_on_cancel;
  value.key_state_reset = &key_state_reset;
  value.execution_context_destroy = &execution_context_destroy;
  value.operator_destroy = &operator_destroy;
  return value;
}

} // namespace

KSJ_PROVIDER_ENTRY ksj_status KSJ_PROVIDER_CALL ksj_provider_query(const ksj_provider_query_request*,
                                                                   ksj_provider_descriptor* out_descriptor,
                                                                   ksj_provider_api_v1* out_api,
                                                                   ksj_error_view* out_error) {
  if (!full_header(out_descriptor) || !full_header(out_api)) {
    fail(out_error, KSJ_STATUS_BAD_ABI);
    return KSJ_STATUS_BAD_ABI;
  }
  static const ksj_operator_descriptor kDescriptor = operator_descriptor();
  *out_descriptor = {};
  out_descriptor->abi = header(sizeof(*out_descriptor), KSJ_PROVIDER_CAP_SYNC_PROCESS);
  out_descriptor->provider_id = text(kProviderId, sizeof(kProviderId) - 1U);
  out_descriptor->version.abi = header(sizeof(out_descriptor->version));
  out_descriptor->version.major = 1U;
  out_descriptor->provider_abi_major = KSJ_PROVIDER_ABI_MAJOR;
  out_descriptor->provider_abi_minor = KSJ_PROVIDER_ABI_MINOR;
  out_descriptor->bundle_digest = digest(0x80U);
  out_descriptor->operator_count = 1U;
  out_descriptor->operators = &kDescriptor;
  *out_api = api();
  return KSJ_STATUS_OK;
}
