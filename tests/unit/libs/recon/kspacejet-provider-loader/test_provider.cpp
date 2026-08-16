#include "kspacejet/provider/provider.h"

#include <cstdint>

namespace {

constexpr char kProviderId[] = "org.kspacejet.tests.provider-loader";
constexpr char kOperatorId[] = "test_operator";

[[nodiscard]] ksj_utf8_view utf8_view(const char* data, const std::uint64_t size) {
  ksj_utf8_view view{};
  view.abi = ksj_provider_abi_header_make(sizeof(view), 0U);
  view.data = data;
  view.size = size;
  return view;
}

[[nodiscard]] ksj_digest256 digest(const std::uint8_t seed) {
  ksj_digest256 value{};
  value.abi = ksj_provider_abi_header_make(sizeof(value), 0U);
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    value.bytes[index] = static_cast<std::uint8_t>(seed + index);
  }
  return value;
}

ksj_status KSJ_PROVIDER_CALL operator_create(const ksj_operator_create_request*, ksj_provider_operator**,
                                             ksj_error_view*) {
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL execution_context_create(ksj_provider_operator*, const ksj_execution_context_descriptor*,
                                                      ksj_execution_context**, ksj_error_view*) {
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL key_state_init(ksj_provider_operator*, ksj_execution_context*,
                                            const ksj_key_state_descriptor*, ksj_key_state**, ksj_error_view*) {
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_start(ksj_provider_operator*, ksj_execution_context*, ksj_key_state*,
                                               const ksj_scan_start_descriptor*, ksj_error_view*) {
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_process_batch(ksj_provider_operator*, ksj_execution_context*, ksj_key_state*,
                                                    ksj_firing_lease*, const ksj_firing_lease_callbacks*,
                                                    ksj_process_result*, ksj_error_view*) {
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_scan_end(ksj_provider_operator*, ksj_execution_context*, ksj_key_state*,
                                                  const ksj_scan_end_descriptor*, ksj_firing_lease*,
                                                  const ksj_firing_lease_callbacks*, ksj_process_result*,
                                                  ksj_error_view*) {
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_cancel(ksj_provider_operator*, ksj_execution_context*, ksj_key_state*,
                                                const ksj_cancel_context*, ksj_error_view*) {
  return KSJ_STATUS_OK;
}

void KSJ_PROVIDER_CALL key_state_reset(ksj_provider_operator*, ksj_execution_context*, ksj_key_state*) {}

void KSJ_PROVIDER_CALL execution_context_destroy(ksj_provider_operator*, ksj_execution_context*) {}

void KSJ_PROVIDER_CALL operator_destroy(ksj_provider_operator*) {}

} // namespace

KSJ_PROVIDER_ENTRY ksj_status KSJ_PROVIDER_CALL ksj_provider_query(const ksj_provider_query_request*,
                                                                   ksj_provider_descriptor* out_descriptor,
                                                                   ksj_provider_api* out_api, ksj_error_view*) {
  if (out_descriptor == nullptr || out_api == nullptr) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }

  static ksj_operator_descriptor operator_descriptor{};
  operator_descriptor.abi = ksj_provider_abi_header_make(sizeof(operator_descriptor), 0U);
  operator_descriptor.operator_id = utf8_view(kOperatorId, sizeof(kOperatorId) - 1U);
  operator_descriptor.max_in_flight = 1U;
  operator_descriptor.thread_safety = KSJ_PROVIDER_SERIAL_INSTANCE;
  operator_descriptor.max_private_threads = 0U;
  operator_descriptor.max_input_items_per_firing = 1U;
  operator_descriptor.max_output_items_per_firing = 1U;
  operator_descriptor.max_output_bytes_per_firing = 1024U;
  operator_descriptor.max_scratch_bytes_per_firing = 0U;
  operator_descriptor.max_retained_input_bytes = 0U;
  operator_descriptor.max_async_tail_bytes = 0U;

  out_descriptor->abi = ksj_provider_abi_header_make(sizeof(*out_descriptor), KSJ_PROVIDER_CAP_SYNC_PROCESS);
  out_descriptor->provider_id = utf8_view(kProviderId, sizeof(kProviderId) - 1U);
#if defined(KSJ_PROVIDER_LOADER_TEST_BAD_HEADER)
  out_descriptor->abi.reserved0 = 99U;
#endif
  out_descriptor->bundle_digest = digest(0x80U);
  out_descriptor->operator_count = 1U;
  out_descriptor->reserved0 = 0U;
  out_descriptor->operators = &operator_descriptor;

  out_api->abi = ksj_provider_abi_header_make(sizeof(*out_api), 0U);
  out_api->operator_create = &operator_create;
  out_api->execution_context_create = &execution_context_create;
  out_api->key_state_init = &key_state_init;
  out_api->operator_on_start = &operator_on_start;
  out_api->operator_process_batch = &operator_process_batch;
  out_api->operator_on_scan_end = &operator_on_scan_end;
  out_api->operator_on_cancel = &operator_on_cancel;
  out_api->key_state_reset = &key_state_reset;
  out_api->execution_context_destroy = &execution_context_destroy;
  out_api->operator_destroy = &operator_destroy;
  return KSJ_STATUS_OK;
}
