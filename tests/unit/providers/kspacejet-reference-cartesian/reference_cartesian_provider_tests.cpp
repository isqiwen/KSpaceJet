#include "kspacejet/provider/loader/provider_loader.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace {

using ksj::provider::loader::ProviderModule;

constexpr char kProviderId[] = "org.kspacejet.reference.cartesian";
constexpr char kOperatorId[] = "cartesian_reference_sink";
constexpr char kCanonicalEmptyConfig[] = "{}";

struct HostLeaseState {
  std::array<std::uint32_t, 2U> batch_item_counts{2U, 3U};
  std::uint32_t output_grant_requests{0U};
  std::uint64_t terminal_epoch{41U};
};

[[nodiscard]] ksj_provider_abi_header make_header(const std::uint32_t struct_size,
                                                  const std::uint64_t capability_bits = 0U) {
  return ksj_provider_abi_header_make(struct_size, capability_bits);
}

[[nodiscard]] ksj_utf8_view utf8_view(const char* data, const std::uint64_t size) {
  ksj_utf8_view result{};
  result.abi = make_header(sizeof(result));
  result.data = data;
  result.size = size;
  return result;
}

[[nodiscard]] ksj_digest256 digest(const std::uint8_t seed) {
  ksj_digest256 result{};
  result.abi = make_header(sizeof(result));
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    result.bytes[index] = static_cast<std::uint8_t>(seed + index);
  }
  return result;
}

ksj_status KSJ_PROVIDER_CALL get_info(void* host_context, const ksj_firing_lease*, ksj_firing_lease_info* out_info,
                                      ksj_error_view*) {
  const auto* state = static_cast<const HostLeaseState*>(host_context);
  if (state == nullptr || out_info == nullptr) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }

  *out_info = {};
  out_info->abi = make_header(sizeof(*out_info));
  out_info->input_batch_count = static_cast<std::uint32_t>(state->batch_item_counts.size());
  out_info->output_grant_count = 0U;
  out_info->terminal_epoch = state->terminal_epoch;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL get_input_batch(void* host_context, const ksj_firing_lease*,
                                             const std::uint32_t batch_index, ksj_input_batch_view* out_batch,
                                             ksj_error_view*) {
  const auto* state = static_cast<const HostLeaseState*>(host_context);
  if (state == nullptr || out_batch == nullptr || batch_index >= state->batch_item_counts.size()) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }

  *out_batch = {};
  out_batch->abi = make_header(sizeof(*out_batch));
  out_batch->item_count = state->batch_item_counts[batch_index];
  out_batch->input_port = 0U;
  out_batch->batch_id = batch_index;
  out_batch->order_domain = 1U;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL acquire_output_grant(void* host_context, ksj_firing_lease*, std::uint32_t,
                                                  ksj_output_grant**, ksj_error_view*) {
  auto* state = static_cast<HostLeaseState*>(host_context);
  if (state != nullptr) {
    ++state->output_grant_requests;
  }
  return KSJ_STATUS_CONTRACT_VIOLATION;
}

[[nodiscard]] ksj_firing_lease_callbacks_v1 make_firing_callbacks(HostLeaseState* state) {
  ksj_firing_lease_callbacks_v1 callbacks{};
  callbacks.abi = make_header(sizeof(callbacks));
  callbacks.host_context = state;
  callbacks.get_info = &get_info;
  callbacks.get_input_batch = &get_input_batch;
  callbacks.acquire_output_grant = &acquire_output_grant;
  return callbacks;
}

[[nodiscard]] std::filesystem::path provider_path() {
  return std::filesystem::path(KSJ_REFERENCE_CARTESIAN_PROVIDER_MODULE);
}

[[nodiscard]] ksj_firing_lease* opaque_lease() {
  alignas(std::max_align_t) static std::byte token{};
  return reinterpret_cast<ksj_firing_lease*>(&token);
}

[[nodiscard]] ksj_error_view empty_error() {
  ksj_error_view error{};
  error.abi = make_header(sizeof(error));
  error.message.abi = make_header(sizeof(error.message));
  return error;
}

void set_canonical_empty_config(ksj_operator_create_request& request) {
  request.canonical_config.abi = make_header(sizeof(request.canonical_config));
  request.canonical_config.data = kCanonicalEmptyConfig;
  request.canonical_config.size = sizeof(kCanonicalEmptyConfig) - 1U;
}

TEST(ReferenceCartesianProvider, LoadsThroughTheTrustedProviderLoader) {
  auto loaded = ProviderModule::load(provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();

  const auto* descriptor = loaded.value().descriptor();
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->provider_id, kProviderId);
  ASSERT_EQ(descriptor->operators.size(), 1U);
  const auto& operator_descriptor = descriptor->operators.front();
  EXPECT_EQ(operator_descriptor.operator_id, kOperatorId);
  EXPECT_EQ(operator_descriptor.max_input_items_per_firing, 256U);
  EXPECT_EQ(operator_descriptor.max_output_items_per_firing, 0U);
  EXPECT_EQ(operator_descriptor.max_output_bytes_per_firing, 0U);
  EXPECT_EQ(operator_descriptor.max_private_threads, 0U);
  EXPECT_EQ(operator_descriptor.max_async_tail_bytes, 0U);
  EXPECT_EQ(operator_descriptor.thread_safety, KSJ_PROVIDER_FULLY_REENTRANT);
}

TEST(ReferenceCartesianProvider, AttestsRequiredContractAtOperatorCreation) {
  auto loaded = ProviderModule::load(provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  auto lease = loaded.value().acquire();
  ASSERT_TRUE(lease.valid());
  ASSERT_NE(lease.api(), nullptr);

  ksj_operator_create_request create_request{};
  create_request.abi = make_header(sizeof(create_request));
  create_request.operator_id = utf8_view(kOperatorId, sizeof(kOperatorId) - 1U);
  create_request.required_contract_digest = digest(0x61U);
  set_canonical_empty_config(create_request);

  ksj_error_view error = empty_error();
  ksj_provider_operator* operator_handle = nullptr;
  ASSERT_EQ(lease.api()->operator_create(&create_request, &operator_handle, &error), KSJ_STATUS_OK);
  ASSERT_NE(operator_handle, nullptr);
  lease.api()->operator_destroy(operator_handle);

  create_request.required_contract_digest = digest(0x62U);
  error = empty_error();
  operator_handle = reinterpret_cast<ksj_provider_operator*>(static_cast<std::uintptr_t>(1U));
  EXPECT_EQ(lease.api()->operator_create(&create_request, &operator_handle, &error), KSJ_STATUS_FAILED_PRECONDITION);
  EXPECT_EQ(operator_handle, nullptr);

  create_request.required_contract_digest = digest(0x61U);
  create_request.canonical_config.data = nullptr;
  create_request.canonical_config.size = 0U;
  error = empty_error();
  EXPECT_EQ(lease.api()->operator_create(&create_request, &operator_handle, &error), KSJ_STATUS_INVALID_ARGUMENT);
}

TEST(ReferenceCartesianProvider, RunsBoundedNormalLifecycleWithoutMRIOutput) {
  auto loaded = ProviderModule::load(provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  auto lease = loaded.value().acquire();
  ASSERT_TRUE(lease.valid());
  ASSERT_NE(lease.api(), nullptr);
  const auto* api = lease.api();

  ksj_operator_create_request create_request{};
  create_request.abi = make_header(sizeof(create_request));
  create_request.operator_id = utf8_view(kOperatorId, sizeof(kOperatorId) - 1U);
  create_request.required_contract_digest = digest(0x61U);
  set_canonical_empty_config(create_request);

  ksj_error_view error = empty_error();
  ksj_provider_operator* operator_handle = nullptr;
  ASSERT_EQ(api->operator_create(&create_request, &operator_handle, &error), KSJ_STATUS_OK);
  ASSERT_NE(operator_handle, nullptr);

  ksj_execution_context_descriptor context_descriptor{};
  context_descriptor.abi = make_header(sizeof(context_descriptor));
  context_descriptor.numa_node = 0U;
  context_descriptor.device_ordinal = 0U;
  context_descriptor.execution_context_id = 17U;
  context_descriptor.resource_domain_id = 3U;
  context_descriptor.max_backend_concurrency = 1U;
  ksj_execution_context* context = nullptr;
  error = empty_error();
  ASSERT_EQ(api->execution_context_create(operator_handle, &context_descriptor, &context, &error), KSJ_STATUS_OK);
  ASSERT_NE(context, nullptr);

  ksj_key_state_descriptor key_descriptor{};
  key_descriptor.abi = make_header(sizeof(key_descriptor));
  key_descriptor.semantic_key.abi = make_header(sizeof(key_descriptor.semantic_key));
  key_descriptor.semantic_key.data = nullptr;
  key_descriptor.semantic_key.size = 0U;
  key_descriptor.placement_key = 7U;
  key_descriptor.key_state_generation = 1U;
  key_descriptor.home_shard = 0U;
  ksj_key_state* key_state = nullptr;
  error = empty_error();
  ASSERT_EQ(api->key_state_init(operator_handle, context, &key_descriptor, &key_state, &error), KSJ_STATUS_OK);
  ASSERT_NE(key_state, nullptr);

  ksj_scan_start_descriptor start_descriptor{};
  start_descriptor.abi = make_header(sizeof(start_descriptor));
  start_descriptor.run_id = utf8_view("run-1", 5U);
  start_descriptor.scan_id = utf8_view("scan-1", 6U);
  start_descriptor.normalized_scan_facts_digest = digest(0x11U);
  start_descriptor.execution_plan_digest = digest(0x21U);
  start_descriptor.terminal_epoch = 40U;
  error = empty_error();
  ASSERT_EQ(api->operator_on_start(operator_handle, context, key_state, &start_descriptor, &error), KSJ_STATUS_OK);

  HostLeaseState host_state;
  const auto callbacks = make_firing_callbacks(&host_state);
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  error = empty_error();
  ASSERT_EQ(
    api->operator_process_batch(operator_handle, context, key_state, opaque_lease(), &callbacks, &result, &error),
    KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.consumed_input_item_count, 5U);
  EXPECT_EQ(result.sealed_output_count, 0U);
  EXPECT_EQ(result.terminal_epoch, host_state.terminal_epoch);
  EXPECT_EQ(host_state.output_grant_requests, 0U);

  ksj_scan_end_descriptor normal_end{};
  normal_end.abi = make_header(sizeof(normal_end));
  normal_end.kind = KSJ_PROVIDER_SCAN_END_NORMAL;
  normal_end.terminal_epoch = 42U;
  normal_end.completed_input_item_count = 5U;
  result = {};
  result.abi = make_header(sizeof(result));
  error = empty_error();
  ASSERT_EQ(api->operator_on_scan_end(operator_handle, context, key_state, &normal_end, opaque_lease(), &callbacks,
                                      &result, &error),
            KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.consumed_input_item_count, 0U);
  EXPECT_EQ(result.sealed_output_count, 0U);
  EXPECT_EQ(result.terminal_epoch, normal_end.terminal_epoch);
  EXPECT_EQ(host_state.output_grant_requests, 0U);

  api->key_state_reset(operator_handle, context, key_state);
  api->execution_context_destroy(operator_handle, context);
  api->operator_destroy(operator_handle);
}

TEST(ReferenceCartesianProvider, CancellationLifecycleHasNoOutputGrantRoute) {
  auto loaded = ProviderModule::load(provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  auto lease = loaded.value().acquire();
  ASSERT_TRUE(lease.valid());
  ASSERT_NE(lease.api(), nullptr);
  const auto* api = lease.api();

  ksj_operator_create_request create_request{};
  create_request.abi = make_header(sizeof(create_request));
  create_request.operator_id = utf8_view(kOperatorId, sizeof(kOperatorId) - 1U);
  create_request.required_contract_digest = digest(0x61U);
  set_canonical_empty_config(create_request);

  ksj_error_view error = empty_error();
  ksj_provider_operator* operator_handle = nullptr;
  ASSERT_EQ(api->operator_create(&create_request, &operator_handle, &error), KSJ_STATUS_OK);
  ASSERT_NE(operator_handle, nullptr);

  ksj_execution_context_descriptor context_descriptor{};
  context_descriptor.abi = make_header(sizeof(context_descriptor));
  context_descriptor.execution_context_id = 19U;
  context_descriptor.resource_domain_id = 3U;
  context_descriptor.max_backend_concurrency = 1U;
  ksj_execution_context* context = nullptr;
  error = empty_error();
  ASSERT_EQ(api->execution_context_create(operator_handle, &context_descriptor, &context, &error), KSJ_STATUS_OK);

  ksj_key_state_descriptor key_descriptor{};
  key_descriptor.abi = make_header(sizeof(key_descriptor));
  key_descriptor.semantic_key.abi = make_header(sizeof(key_descriptor.semantic_key));
  key_descriptor.key_state_generation = 1U;
  ksj_key_state* key_state = nullptr;
  error = empty_error();
  ASSERT_EQ(api->key_state_init(operator_handle, context, &key_descriptor, &key_state, &error), KSJ_STATUS_OK);

  ksj_scan_start_descriptor start_descriptor{};
  start_descriptor.abi = make_header(sizeof(start_descriptor));
  start_descriptor.run_id = utf8_view("run-2", 5U);
  start_descriptor.scan_id = utf8_view("scan-2", 6U);
  start_descriptor.normalized_scan_facts_digest = digest(0x12U);
  start_descriptor.execution_plan_digest = digest(0x22U);
  start_descriptor.terminal_epoch = 50U;
  error = empty_error();
  ASSERT_EQ(api->operator_on_start(operator_handle, context, key_state, &start_descriptor, &error), KSJ_STATUS_OK);

  ksj_cancel_context cancel{};
  cancel.abi = make_header(sizeof(cancel));
  cancel.kind = KSJ_PROVIDER_SCAN_END_CANCELLED;
  cancel.terminal_epoch = 51U;
  cancel.cancellation_generation = 1U;
  cancel.reason = utf8_view("test cancellation", 17U);
  error = empty_error();
  ASSERT_EQ(api->operator_on_cancel(operator_handle, context, key_state, &cancel, &error), KSJ_STATUS_OK);

  // on_cancel has neither a firing lease nor a callback table.  This test is
  // therefore an ABI-level proof that the cancellation path cannot acquire an
  // OutputGrant or publish ordinary MRI data.
  api->key_state_reset(operator_handle, context, key_state);
  api->execution_context_destroy(operator_handle, context);
  api->operator_destroy(operator_handle);
}

} // namespace
