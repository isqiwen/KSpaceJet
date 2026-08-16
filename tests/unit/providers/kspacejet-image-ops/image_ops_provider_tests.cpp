#include "kspacejet/provider/loader/provider_loader.hpp"
#include "kspacejet/provider/type_registry.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <utility>

namespace {

using ksj::provider::loader::ProviderLease;
using ksj::provider::loader::ProviderModule;

constexpr char kProviderId[] = "org.kspacejet.image-ops";
constexpr char kProviderBundleDigestHex[] = "8db8232be7b7166a6e6aa2ddd896d1a8ef9aa6c1811ba3530b3796d60f5e6b53";
constexpr char kImageScaleOperatorId[] = "image_scale_float32";
constexpr char kImageScaleCanonicalConfig[] = "{\"factor\":2}";
constexpr char kImageOffsetOperatorId[] = "image_offset_float32";
constexpr char kImageOffsetCanonicalConfig[] = "{\"offset\":1}";
constexpr char kImageClampOperatorId[] = "image_clamp_float32";
constexpr char kImageClampCanonicalConfig[] = "{\"maximum\":1,\"minimum\":0}";

struct OperatorCase {
  const char* id;
  std::uint64_t id_size;
  const char* canonical_config;
  std::uint64_t canonical_config_size;
};

constexpr OperatorCase kImageScaleOperator{
  .id = kImageScaleOperatorId,
  .id_size = sizeof(kImageScaleOperatorId) - 1U,
  .canonical_config = kImageScaleCanonicalConfig,
  .canonical_config_size = sizeof(kImageScaleCanonicalConfig) - 1U,
};
constexpr char kImageScaleFractionalCanonicalConfig[] = "{\"factor\":1.5}";
constexpr OperatorCase kImageScaleFractionalOperator{
  .id = kImageScaleOperatorId,
  .id_size = sizeof(kImageScaleOperatorId) - 1U,
  .canonical_config = kImageScaleFractionalCanonicalConfig,
  .canonical_config_size = sizeof(kImageScaleFractionalCanonicalConfig) - 1U,
};
constexpr OperatorCase kImageOffsetOperator{
  .id = kImageOffsetOperatorId,
  .id_size = sizeof(kImageOffsetOperatorId) - 1U,
  .canonical_config = kImageOffsetCanonicalConfig,
  .canonical_config_size = sizeof(kImageOffsetCanonicalConfig) - 1U,
};
constexpr OperatorCase kImageClampOperator{
  .id = kImageClampOperatorId,
  .id_size = sizeof(kImageClampOperatorId) - 1U,
  .canonical_config = kImageClampCanonicalConfig,
  .canonical_config_size = sizeof(kImageClampCanonicalConfig) - 1U,
};
constexpr char kImageOffsetNegativeCanonicalConfig[] = "{\"offset\":-0.25}";
constexpr OperatorCase kImageOffsetNegativeOperator{
  .id = kImageOffsetOperatorId,
  .id_size = sizeof(kImageOffsetOperatorId) - 1U,
  .canonical_config = kImageOffsetNegativeCanonicalConfig,
  .canonical_config_size = sizeof(kImageOffsetNegativeCanonicalConfig) - 1U,
};
constexpr char kImageClampNarrowCanonicalConfig[] = "{\"maximum\":0.75,\"minimum\":-0.5}";
constexpr OperatorCase kImageClampNarrowOperator{
  .id = kImageClampOperatorId,
  .id_size = sizeof(kImageClampOperatorId) - 1U,
  .canonical_config = kImageClampNarrowCanonicalConfig,
  .canonical_config_size = sizeof(kImageClampNarrowCanonicalConfig) - 1U,
};
constexpr std::array<OperatorCase, 3U> kImageOperators{
  kImageScaleOperator,
  kImageOffsetOperator,
  kImageClampOperator,
};

[[nodiscard]] ksj_provider_abi_header make_header(const std::uint32_t struct_size,
                                                  const std::uint64_t capability_bits = 0U) noexcept {
  return ksj_provider_abi_header_make(struct_size, capability_bits);
}

[[nodiscard]] ksj_utf8_view text(const char* data, const std::uint64_t size) noexcept {
  ksj_utf8_view view{};
  view.abi = make_header(sizeof(view));
  view.data = data;
  view.size = size;
  return view;
}

[[nodiscard]] constexpr std::uint8_t hex_nibble(const char value) noexcept {
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<std::uint8_t>(value - 'a' + 10);
  }
  return 0U;
}

template <std::size_t N> [[nodiscard]] ksj_digest256 digest_from_hex(const char (&hex)[N]) noexcept {
  static_assert(N == KSJ_PROVIDER_DIGEST256_SIZE * 2U + 1U, "A SHA-256 literal must contain 64 hexadecimal bytes.");
  ksj_digest256 value{};
  value.abi = make_header(sizeof(value));
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    const std::size_t offset = static_cast<std::size_t>(index) * 2U;
    value.bytes[index] = static_cast<std::uint8_t>((hex_nibble(hex[offset]) << 4U) | hex_nibble(hex[offset + 1U]));
  }
  return value;
}

[[nodiscard]] ksj_digest256 neutral_digest(const std::uint8_t tag) noexcept {
  ksj_digest256 value{};
  value.abi = make_header(sizeof(value));
  value.bytes[0U] = tag;
  return value;
}

[[nodiscard]] ksj_error_view empty_error() noexcept {
  ksj_error_view error{};
  error.abi = make_header(sizeof(error));
  error.message.abi = make_header(sizeof(error.message));
  return error;
}

struct HostState {
  HostState() {
    input = {-1.0F, 0.5F, 1.25F, -3.0F};

    item.abi = make_header(sizeof(item));
    item.payload.abi = make_header(sizeof(item.payload));
    item.payload.data = input.data();
    item.payload.byte_count = input.size() * sizeof(float);
    item.payload.memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
    item.payload.alignment = 64U;
    item.payload.type = image_type;
    item.metadata.abi = make_header(sizeof(item.metadata));
    item.metadata.data = metadata.data();
    item.metadata.size = metadata.size();
    item.semantic_key_hash = 11U;
    item.order_key = 17U;
    item.item_ordinal = 23U;

    batch.abi = make_header(sizeof(batch));
    batch.items = &item;
    batch.item_count = 1U;
    batch.input_port = 0U;
    batch.batch_id = 5U;
    batch.order_domain = 7U;
  }

  [[nodiscard]] ksj_output_grant* output_grant() noexcept {
    return reinterpret_cast<ksj_output_grant*>(&output_grant_storage);
  }

  ksj_type_descriptor_view image_type{ksj_type_registry_image_frame()};
  alignas(64) std::array<float, 4U> input{};
  alignas(64) std::array<float, 4U> output{};
  std::array<std::byte, 3U> metadata{{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};
  alignas(std::max_align_t) std::byte output_grant_storage{};
  ksj_input_item_view item{};
  ksj_input_batch_view batch{};
  std::uint32_t acquire_calls{0U};
  std::uint32_t map_calls{0U};
  std::uint32_t seal_calls{0U};
  std::uint32_t release_calls{0U};
  bool output_acquired{false};
  bool output_mapped{false};
  bool output_sealed{false};
  std::uint64_t terminal_epoch{41U};
  ksj_output_seal_descriptor sealed_descriptor{};
};

ksj_status KSJ_PROVIDER_CALL get_info(void* host_context, const ksj_firing_lease*, ksj_firing_lease_info* out_info,
                                      ksj_error_view*) noexcept {
  const auto* state = static_cast<const HostState*>(host_context);
  if (state == nullptr || out_info == nullptr) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_info = {};
  out_info->abi = make_header(sizeof(*out_info));
  out_info->resource_occurrence_id = 3U;
  out_info->slot_generation = 5U;
  out_info->terminal_epoch = state->terminal_epoch;
  out_info->input_batch_count = 1U;
  out_info->output_grant_count = 1U;
  out_info->reserved_output_bytes = state->output.size() * sizeof(float);
  out_info->reserved_scratch_bytes = 0U;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL get_input_batch(void* host_context, const ksj_firing_lease*, const std::uint32_t index,
                                             ksj_input_batch_view* out_batch, ksj_error_view*) noexcept {
  const auto* state = static_cast<const HostState*>(host_context);
  if (state == nullptr || out_batch == nullptr || index != 0U) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_batch = state->batch;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL acquire_output_grant(void* host_context, ksj_firing_lease*, const std::uint32_t slot,
                                                  ksj_output_grant** out_grant, ksj_error_view*) noexcept {
  auto* state = static_cast<HostState*>(host_context);
  if (state == nullptr || out_grant == nullptr || slot != 0U || state->output_acquired) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ++state->acquire_calls;
  state->output_acquired = true;
  *out_grant = state->output_grant();
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL map_output(void* host_context, ksj_output_grant* grant,
                                        ksj_mutable_payload_view* out_payload, ksj_error_view*) noexcept {
  auto* state = static_cast<HostState*>(host_context);
  if (state == nullptr || out_payload == nullptr || grant != state->output_grant() || !state->output_acquired ||
      state->output_mapped) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ++state->map_calls;
  state->output_mapped = true;
  *out_payload = {};
  out_payload->abi = make_header(sizeof(*out_payload));
  out_payload->data = state->output.data();
  out_payload->capacity_bytes = state->output.size() * sizeof(float);
  out_payload->memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
  out_payload->alignment = 64U;
  out_payload->type = state->image_type;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL seal_output(void* host_context, ksj_output_grant* grant,
                                         const ksj_output_seal_descriptor* descriptor, ksj_error_view*) noexcept {
  auto* state = static_cast<HostState*>(host_context);
  if (state == nullptr || descriptor == nullptr || grant != state->output_grant() || !state->output_mapped ||
      state->output_sealed || descriptor->output_port != 0U || descriptor->produced_item_count != 1U ||
      descriptor->produced_byte_count != state->output.size() * sizeof(float)) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ++state->seal_calls;
  state->output_sealed = true;
  state->sealed_descriptor = *descriptor;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL release_output(void* host_context, ksj_output_grant* grant, ksj_error_view*) noexcept {
  auto* state = static_cast<HostState*>(host_context);
  if (state == nullptr || grant != state->output_grant() || state->output_sealed) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ++state->release_calls;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL seal_then_fail(void*, ksj_output_grant*, const ksj_output_seal_descriptor*,
                                            ksj_error_view*) noexcept {
  return KSJ_STATUS_CONTRACT_VIOLATION;
}

struct CallbackTables {
  explicit CallbackTables(HostState* state) {
    output.abi = make_header(sizeof(output));
    output.host_context = state;
    output.map_mutable_payload = &map_output;
    output.seal = &seal_output;
    output.release = &release_output;

    lease.abi = make_header(sizeof(lease), KSJ_LEASE_CAP_INPUT_BATCHES | KSJ_LEASE_CAP_OUTPUT_GRANTS);
    lease.host_context = state;
    lease.output_grants = &output;
    lease.get_info = &get_info;
    lease.get_input_batch = &get_input_batch;
    lease.acquire_output_grant = &acquire_output_grant;
  }

  ksj_output_grant_callbacks output{};
  ksj_firing_lease_callbacks lease{};
};

[[nodiscard]] ksj_firing_lease* opaque_lease() noexcept {
  alignas(std::max_align_t) static std::byte storage{};
  return reinterpret_cast<ksj_firing_lease*>(&storage);
}

[[nodiscard]] std::filesystem::path provider_path() {
  return std::filesystem::path(KSJ_IMAGE_OPS_PROVIDER_MODULE);
}

void set_config(ksj_operator_create_request& request, const char* config, const std::uint64_t size) {
  request.canonical_config.abi = make_header(sizeof(request.canonical_config));
  request.canonical_config.data = config;
  request.canonical_config.size = size;
}

struct ProviderInstance {
  ProviderModule module{};
  ProviderLease lease{};
  ksj_provider_operator* operator_handle{nullptr};
  ksj_execution_context* context{nullptr};
  ksj_key_state* key_state{nullptr};

  ProviderInstance() = default;
  ProviderInstance(const ProviderInstance&) = delete;
  ProviderInstance& operator=(const ProviderInstance&) = delete;
  ProviderInstance(ProviderInstance&& other) noexcept
      : module(std::move(other.module)), lease(std::move(other.lease)), operator_handle(other.operator_handle),
        context(other.context), key_state(other.key_state) {
    other.operator_handle = nullptr;
    other.context = nullptr;
    other.key_state = nullptr;
  }
  ProviderInstance& operator=(ProviderInstance&&) = delete;

  ~ProviderInstance() {
    if (!lease.valid() || lease.api() == nullptr) {
      return;
    }
    const auto* api = lease.api();
    if (key_state != nullptr) {
      api->key_state_reset(operator_handle, context, key_state);
    }
    if (context != nullptr) {
      api->execution_context_destroy(operator_handle, context);
    }
    if (operator_handle != nullptr) {
      api->operator_destroy(operator_handle);
    }
  }
};

[[nodiscard]] ProviderInstance make_instance(const OperatorCase& operator_case = kImageScaleOperator) {
  ProviderInstance instance;
  auto module = ProviderModule::load(provider_path());
  EXPECT_TRUE(module.ok()) << module.status();
  if (!module.ok()) {
    return instance;
  }
  instance.module = std::move(module).value();
  instance.lease = instance.module.acquire();
  EXPECT_TRUE(instance.lease.valid());
  if (!instance.lease.valid() || instance.lease.api() == nullptr) {
    return instance;
  }
  const auto* api = instance.lease.api();

  ksj_operator_create_request create{};
  create.abi = make_header(sizeof(create));
  create.operator_id = text(operator_case.id, operator_case.id_size);
  set_config(create, operator_case.canonical_config, operator_case.canonical_config_size);
  auto error = empty_error();
  EXPECT_EQ(api->operator_create(&create, &instance.operator_handle, &error), KSJ_STATUS_OK);
  if (instance.operator_handle == nullptr) {
    return instance;
  }

  ksj_execution_context_descriptor context_descriptor{};
  context_descriptor.abi = make_header(sizeof(context_descriptor));
  context_descriptor.execution_context_id = 9U;
  context_descriptor.resource_domain_id = 3U;
  context_descriptor.max_backend_concurrency = 1U;
  error = empty_error();
  EXPECT_EQ(api->execution_context_create(instance.operator_handle, &context_descriptor, &instance.context, &error),
            KSJ_STATUS_OK);
  if (instance.context == nullptr) {
    return instance;
  }

  ksj_key_state_descriptor key_descriptor{};
  key_descriptor.abi = make_header(sizeof(key_descriptor));
  key_descriptor.semantic_key.abi = make_header(sizeof(key_descriptor.semantic_key));
  key_descriptor.key_state_generation = 1U;
  error = empty_error();
  EXPECT_EQ(
    api->key_state_init(instance.operator_handle, instance.context, &key_descriptor, &instance.key_state, &error),
    KSJ_STATUS_OK);
  if (instance.key_state == nullptr) {
    return instance;
  }

  ksj_scan_start_descriptor start{};
  start.abi = make_header(sizeof(start));
  start.run_id = text("run", 3U);
  start.scan_id = text("scan", 4U);
  start.normalized_scan_facts_digest = neutral_digest(1U);
  start.execution_plan_digest = neutral_digest(2U);
  start.terminal_epoch = 40U;
  error = empty_error();
  EXPECT_EQ(api->operator_on_start(instance.operator_handle, instance.context, instance.key_state, &start, &error),
            KSJ_STATUS_OK);
  return instance;
}

TEST(ImageOpsProvider, LoadsThreeStatelessImageOperatorsAndRejectsNoncanonicalConfigurations) {
  auto loaded = ProviderModule::load(provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  const auto* descriptor = loaded.value().descriptor();
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->provider_id, kProviderId);
  const auto expected_bundle_digest = digest_from_hex(kProviderBundleDigestHex);
  EXPECT_EQ(std::memcmp(descriptor->bundle_digest.data(), expected_bundle_digest.bytes, KSJ_PROVIDER_DIGEST256_SIZE),
            0);
  ASSERT_EQ(descriptor->operators.size(), kImageOperators.size());
  for (std::size_t index = 0U; index < kImageOperators.size(); ++index) {
    const auto& operator_descriptor = descriptor->operators[index];
    EXPECT_EQ(operator_descriptor.operator_id, kImageOperators[index].id);
    EXPECT_EQ(operator_descriptor.max_in_flight, 1U);
    EXPECT_EQ(operator_descriptor.max_input_items_per_firing, 1U);
    EXPECT_EQ(operator_descriptor.max_output_items_per_firing, 1U);
    EXPECT_EQ(operator_descriptor.max_output_bytes_per_firing, 1024U * 1024U);
    EXPECT_EQ(operator_descriptor.max_scratch_bytes_per_firing, 0U);
    EXPECT_EQ(operator_descriptor.max_private_threads, 0U);
    EXPECT_EQ(operator_descriptor.max_retained_input_bytes, 0U);
    EXPECT_EQ(operator_descriptor.max_async_tail_bytes, 0U);
    EXPECT_EQ(operator_descriptor.thread_safety, KSJ_PROVIDER_SERIAL_INSTANCE);
  }

  auto lease = loaded.value().acquire();
  ASSERT_TRUE(lease.valid());
  ksj_operator_create_request request{};
  request.abi = make_header(sizeof(request));
  request.operator_id = text(kImageScaleOperator.id, kImageScaleOperator.id_size);
  set_config(request, "{\"factor\":3.0}", sizeof("{\"factor\":3.0}") - 1U);
  auto error = empty_error();
  ksj_provider_operator* operator_handle = nullptr;
  EXPECT_EQ(lease.api()->operator_create(&request, &operator_handle, &error), KSJ_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(operator_handle, nullptr);

  request.operator_id = text(kImageOffsetOperator.id, kImageOffsetOperator.id_size);
  set_config(request, "{\"offset\":1.0}", sizeof("{\"offset\":1.0}") - 1U);
  error = empty_error();
  EXPECT_EQ(lease.api()->operator_create(&request, &operator_handle, &error), KSJ_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(operator_handle, nullptr);

  request.operator_id = text(kImageClampOperator.id, kImageClampOperator.id_size);
  set_config(request, "{\"minimum\":0,\"maximum\":1}", sizeof("{\"minimum\":0,\"maximum\":1}") - 1U);
  error = empty_error();
  EXPECT_EQ(lease.api()->operator_create(&request, &operator_handle, &error), KSJ_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(operator_handle, nullptr);
}

TEST(ImageOpsProvider, ScalesASealedImageAndPreservesTheInputIdentity) {
  auto instance = make_instance();
  ASSERT_TRUE(instance.lease.valid());
  ASSERT_NE(instance.operator_handle, nullptr);
  ASSERT_NE(instance.context, nullptr);
  ASSERT_NE(instance.key_state, nullptr);

  HostState host;
  CallbackTables callbacks{&host};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  ASSERT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.consumed_input_item_count, 1U);
  EXPECT_EQ(result.sealed_output_count, 1U);
  EXPECT_EQ(result.terminal_epoch, host.terminal_epoch);
  EXPECT_EQ(host.acquire_calls, 1U);
  EXPECT_EQ(host.map_calls, 1U);
  EXPECT_EQ(host.seal_calls, 1U);
  EXPECT_EQ(host.release_calls, 0U);
  EXPECT_TRUE(host.output_sealed);
  EXPECT_EQ(host.sealed_descriptor.semantic_key_hash, host.item.semantic_key_hash);
  EXPECT_EQ(host.sealed_descriptor.order_key, host.item.order_key);
  EXPECT_EQ(host.sealed_descriptor.produced_byte_count, host.output.size() * sizeof(float));
  EXPECT_EQ(host.sealed_descriptor.metadata.data, host.metadata.data());
  EXPECT_EQ(host.sealed_descriptor.metadata.size, host.metadata.size());
  EXPECT_EQ(host.output[0U], -2.0F);
  EXPECT_EQ(host.output[1U], 1.0F);
  EXPECT_EQ(host.output[2U], 2.5F);
  EXPECT_EQ(host.output[3U], -6.0F);

  ksj_scan_end_descriptor end{};
  end.abi = make_header(sizeof(end));
  end.kind = KSJ_PROVIDER_SCAN_END_NORMAL;
  end.terminal_epoch = host.terminal_epoch + 1U;
  end.completed_input_item_count = 1U;
  result = {};
  result.abi = make_header(sizeof(result));
  error = empty_error();
  ASSERT_EQ(instance.lease.api()->operator_on_scan_end(instance.operator_handle, instance.context, instance.key_state,
                                                       &end, opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.sealed_output_count, 0U);
  EXPECT_EQ(result.consumed_input_item_count, 0U);
  EXPECT_EQ(result.terminal_epoch, end.terminal_epoch);
  EXPECT_EQ(host.acquire_calls, 1U);

  ksj_cancel_context cancel{};
  cancel.abi = make_header(sizeof(cancel));
  cancel.kind = KSJ_PROVIDER_SCAN_END_CANCELLED;
  cancel.terminal_epoch = end.terminal_epoch + 1U;
  cancel.reason = text("test", 4U);
  error = empty_error();
  EXPECT_EQ(instance.lease.api()->operator_on_cancel(instance.operator_handle, instance.context, instance.key_state,
                                                     &cancel, &error),
            KSJ_STATUS_OK);
  EXPECT_EQ(host.acquire_calls, 1U);
}

TEST(ImageOpsProvider, ScalesByAConfiguredFiniteDecimalAndPreservesTheInputIdentity) {
  auto instance = make_instance(kImageScaleFractionalOperator);
  ASSERT_TRUE(instance.lease.valid());
  ASSERT_NE(instance.operator_handle, nullptr);
  ASSERT_NE(instance.context, nullptr);
  ASSERT_NE(instance.key_state, nullptr);

  HostState host;
  CallbackTables callbacks{&host};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  ASSERT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.consumed_input_item_count, 1U);
  EXPECT_EQ(result.sealed_output_count, 1U);
  EXPECT_EQ(result.terminal_epoch, host.terminal_epoch);
  EXPECT_TRUE(host.output_sealed);
  EXPECT_EQ(host.sealed_descriptor.semantic_key_hash, host.item.semantic_key_hash);
  EXPECT_EQ(host.sealed_descriptor.order_key, host.item.order_key);
  EXPECT_EQ(host.sealed_descriptor.produced_byte_count, host.output.size() * sizeof(float));
  EXPECT_EQ(host.sealed_descriptor.metadata.data, host.metadata.data());
  EXPECT_EQ(host.sealed_descriptor.metadata.size, host.metadata.size());
  EXPECT_EQ(host.output[0U], -1.5F);
  EXPECT_EQ(host.output[1U], 0.75F);
  EXPECT_EQ(host.output[2U], 1.875F);
  EXPECT_EQ(host.output[3U], -4.5F);
}

TEST(ImageOpsProvider, OffsetsASealedImageAndPreservesTheInputIdentity) {
  auto instance = make_instance(kImageOffsetNegativeOperator);
  ASSERT_TRUE(instance.lease.valid());
  ASSERT_NE(instance.operator_handle, nullptr);
  ASSERT_NE(instance.context, nullptr);
  ASSERT_NE(instance.key_state, nullptr);

  HostState host;
  CallbackTables callbacks{&host};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  ASSERT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.consumed_input_item_count, 1U);
  EXPECT_EQ(result.sealed_output_count, 1U);
  EXPECT_EQ(result.terminal_epoch, host.terminal_epoch);
  EXPECT_TRUE(host.output_sealed);
  EXPECT_EQ(host.sealed_descriptor.semantic_key_hash, host.item.semantic_key_hash);
  EXPECT_EQ(host.sealed_descriptor.order_key, host.item.order_key);
  EXPECT_EQ(host.sealed_descriptor.produced_byte_count, host.output.size() * sizeof(float));
  EXPECT_EQ(host.sealed_descriptor.metadata.data, host.metadata.data());
  EXPECT_EQ(host.sealed_descriptor.metadata.size, host.metadata.size());
  EXPECT_EQ(host.output[0U], -1.25F);
  EXPECT_EQ(host.output[1U], 0.25F);
  EXPECT_EQ(host.output[2U], 1.0F);
  EXPECT_EQ(host.output[3U], -3.25F);
}

TEST(ImageOpsProvider, ClampsASealedImageAndPreservesTheInputIdentity) {
  auto instance = make_instance(kImageClampNarrowOperator);
  ASSERT_TRUE(instance.lease.valid());
  ASSERT_NE(instance.operator_handle, nullptr);
  ASSERT_NE(instance.context, nullptr);
  ASSERT_NE(instance.key_state, nullptr);

  HostState host;
  CallbackTables callbacks{&host};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  ASSERT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.consumed_input_item_count, 1U);
  EXPECT_EQ(result.sealed_output_count, 1U);
  EXPECT_EQ(result.terminal_epoch, host.terminal_epoch);
  EXPECT_TRUE(host.output_sealed);
  EXPECT_EQ(host.sealed_descriptor.semantic_key_hash, host.item.semantic_key_hash);
  EXPECT_EQ(host.sealed_descriptor.order_key, host.item.order_key);
  EXPECT_EQ(host.sealed_descriptor.produced_byte_count, host.output.size() * sizeof(float));
  EXPECT_EQ(host.sealed_descriptor.metadata.data, host.metadata.data());
  EXPECT_EQ(host.sealed_descriptor.metadata.size, host.metadata.size());
  EXPECT_EQ(host.output[0U], -0.5F);
  EXPECT_EQ(host.output[1U], 0.5F);
  EXPECT_EQ(host.output[2U], 0.75F);
  EXPECT_EQ(host.output[3U], -0.5F);
}

TEST(ImageOpsProvider, RejectsAnInputWhoseDescriptorDoesNotMatchTheFrozenImageType) {
  auto instance = make_instance();
  ASSERT_TRUE(instance.lease.valid());
  ASSERT_NE(instance.operator_handle, nullptr);
  ASSERT_NE(instance.context, nullptr);
  ASSERT_NE(instance.key_state, nullptr);

  HostState host;
  host.item.payload.type.element_type = KSJ_ELEMENT_TYPE_FLOAT64;
  CallbackTables callbacks{&host};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();

  EXPECT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(host.acquire_calls, 0U);
  EXPECT_EQ(host.map_calls, 0U);
  EXPECT_EQ(host.seal_calls, 0U);
}

TEST(ImageOpsProvider, ReleasesThePregrantedOutputWhenItsDescriptorOrSealIsRejected) {
  auto instance = make_instance();
  ASSERT_TRUE(instance.lease.valid());
  ASSERT_NE(instance.operator_handle, nullptr);
  ASSERT_NE(instance.context, nullptr);
  ASSERT_NE(instance.key_state, nullptr);

  HostState invalid_output_host;
  invalid_output_host.image_type.layout_flags = KSJ_TYPE_LAYOUT_CHANNEL_MAJOR_CONTIGUOUS | KSJ_TYPE_STRIDES_CANONICAL;
  CallbackTables invalid_output_callbacks{&invalid_output_host};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  EXPECT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &invalid_output_callbacks.lease, &result,
                                                         &error),
            KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(invalid_output_host.acquire_calls, 1U);
  EXPECT_EQ(invalid_output_host.map_calls, 1U);
  EXPECT_EQ(invalid_output_host.seal_calls, 0U);
  EXPECT_EQ(invalid_output_host.release_calls, 1U);

  HostState rejected_seal_host;
  CallbackTables rejected_seal_callbacks{&rejected_seal_host};
  rejected_seal_callbacks.output.seal = &seal_then_fail;
  result = {};
  result.abi = make_header(sizeof(result));
  error = empty_error();
  EXPECT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &rejected_seal_callbacks.lease, &result,
                                                         &error),
            KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(rejected_seal_host.acquire_calls, 1U);
  EXPECT_EQ(rejected_seal_host.map_calls, 1U);
  EXPECT_EQ(rejected_seal_host.seal_calls, 0U);
  EXPECT_EQ(rejected_seal_host.release_calls, 1U);
}

} // namespace
