// SPDX-License-Identifier: Apache-2.0

#include "kspacejet/provider/loader/provider_loader.hpp"
#include "kspacejet/provider/type_registry.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <utility>

namespace {

using ksj::provider::loader::ProviderLease;
using ksj::provider::loader::ProviderModule;

constexpr char kProviderId[] = "org.kspacejet.kspace-conditioning";
constexpr char kProviderBundleDigestHex[] = "6ff48e549c4ff3ad41cfebb766e6d298360bf686dc7882f5df9e6401b7dc7325";
constexpr char kNoiseOperatorId[] = "noise_prewhiten";
constexpr char kNoiseConfig[] = "{\"channel_count\":2,\"cols\":2,\"rows\":2}";
constexpr char kPhaseOperatorId[] = "phase_correct";
constexpr char kPhaseConfig[] = "{\"channel_count\":2,\"cols\":2,\"rows\":2}";
constexpr char kCoilOperatorId[] = "coil_compress";
constexpr char kCoilConfig[] = "{\"cols\":2,\"physical_channel_count\":2,\"rows\":2,\"virtual_channel_count\":1}";
constexpr char kCropOperatorId[] = "readout_oversampling_remove";
constexpr char kCropConfig[] =
  "{\"channel_count\":2,\"input_cols\":4,\"output_cols\":2,\"readout_offset\":1,\"rows\":2}";
constexpr char kNoncartesianNoiseOperatorId[] = "noncartesian_noise_prewhiten";
constexpr char kNoncartesianNoiseConfig[] = "{\"channel_count\":2,\"sample_count\":3}";
constexpr char kNoncartesianCoilOperatorId[] = "noncartesian_coil_compress";
constexpr char kNoncartesianCoilConfig[] =
  "{\"physical_channel_count\":2,\"sample_count\":3,\"virtual_channel_count\":1}";
constexpr std::uint64_t kComplexBytes = 2U * sizeof(float);

struct OperatorCase {
  const char* id;
  std::uint64_t id_size;
  const char* canonical_config;
  std::uint64_t canonical_config_size;
};

constexpr OperatorCase kNoiseOperator{
  .id = kNoiseOperatorId,
  .id_size = sizeof(kNoiseOperatorId) - 1U,
  .canonical_config = kNoiseConfig,
  .canonical_config_size = sizeof(kNoiseConfig) - 1U,
};
constexpr OperatorCase kPhaseOperator{
  .id = kPhaseOperatorId,
  .id_size = sizeof(kPhaseOperatorId) - 1U,
  .canonical_config = kPhaseConfig,
  .canonical_config_size = sizeof(kPhaseConfig) - 1U,
};
constexpr OperatorCase kCoilOperator{
  .id = kCoilOperatorId,
  .id_size = sizeof(kCoilOperatorId) - 1U,
  .canonical_config = kCoilConfig,
  .canonical_config_size = sizeof(kCoilConfig) - 1U,
};
constexpr OperatorCase kCropOperator{
  .id = kCropOperatorId,
  .id_size = sizeof(kCropOperatorId) - 1U,
  .canonical_config = kCropConfig,
  .canonical_config_size = sizeof(kCropConfig) - 1U,
};
constexpr OperatorCase kNoncartesianNoiseOperator{
  .id = kNoncartesianNoiseOperatorId,
  .id_size = sizeof(kNoncartesianNoiseOperatorId) - 1U,
  .canonical_config = kNoncartesianNoiseConfig,
  .canonical_config_size = sizeof(kNoncartesianNoiseConfig) - 1U,
};
constexpr OperatorCase kNoncartesianCoilOperator{
  .id = kNoncartesianCoilOperatorId,
  .id_size = sizeof(kNoncartesianCoilOperatorId) - 1U,
  .canonical_config = kNoncartesianCoilConfig,
  .canonical_config_size = sizeof(kNoncartesianCoilConfig) - 1U,
};

[[nodiscard]] ksj_provider_abi_header make_header(const std::uint32_t struct_size,
                                                  const std::uint64_t capability_bits = 0U) noexcept {
  return ksj_provider_abi_header_make(struct_size, capability_bits);
}

[[nodiscard]] ksj_utf8_view text(const char* const data, const std::uint64_t size) noexcept {
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

[[nodiscard]] ksj_digest256 digest_from_hex(const char* const hex) noexcept {
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

void write_complex(std::byte* const destination, const std::size_t element_index,
                   const std::complex<float> value) noexcept {
  const auto offset = element_index * static_cast<std::size_t>(kComplexBytes);
  const float real = value.real();
  const float imaginary = value.imag();
  std::memcpy(destination + offset, &real, sizeof(real));
  std::memcpy(destination + offset + sizeof(real), &imaginary, sizeof(imaginary));
}

[[nodiscard]] std::complex<float> read_complex(const std::byte* const source,
                                               const std::size_t element_index) noexcept {
  const auto offset = element_index * static_cast<std::size_t>(kComplexBytes);
  float real = 0.0F;
  float imaginary = 0.0F;
  std::memcpy(&real, source + offset, sizeof(real));
  std::memcpy(&imaginary, source + offset + sizeof(real), sizeof(imaginary));
  return {real, imaginary};
}

void initialize_item(ksj_input_item_view& item, std::byte* const data, const std::uint64_t byte_count,
                     const ksj_type_descriptor_view type, const std::uint64_t semantic_key_hash,
                     const std::uint64_t order_key) noexcept {
  item = {};
  item.abi = make_header(sizeof(item));
  item.payload.abi = make_header(sizeof(item.payload));
  item.payload.data = data;
  item.payload.byte_count = byte_count;
  item.payload.memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
  item.payload.alignment = 64U;
  item.payload.type = type;
  item.metadata.abi = make_header(sizeof(item.metadata));
  item.semantic_key_hash = semantic_key_hash;
  item.order_key = order_key;
  item.item_ordinal = 23U;
}

[[nodiscard]] bool matches_registered_output_type(const ksj_type_descriptor_view& expected,
                                                  const ksj_type_descriptor_view& actual) noexcept {
  if (ksj_type_registry_matches_kspace_frame(&expected) != 0) {
    return ksj_type_registry_matches_kspace_frame(&actual) != 0;
  }
  if (ksj_type_registry_matches_noncartesian_kspace_frame(&expected) != 0) {
    return ksj_type_registry_matches_noncartesian_kspace_frame(&actual) != 0;
  }
  return false;
}

struct HostState {
  HostState(const ksj_type_descriptor_view dynamic_type_value, const std::uint64_t dynamic_size,
            const ksj_type_descriptor_view calibration_type_value, const std::uint64_t calibration_size,
            const bool has_calibration, const ksj_type_descriptor_view output_type_value,
            const std::uint64_t output_size)
      : dynamic_type(dynamic_type_value), calibration_type(calibration_type_value), output_type(output_type_value),
        dynamic_byte_count(dynamic_size), calibration_byte_count(calibration_size), output_byte_count(output_size),
        input_batch_count(has_calibration ? 2U : 1U) {
    initialize_item(dynamic_item, dynamic.data(), dynamic_byte_count, dynamic_type, 11U, 17U);
    initialize_item(calibration_item, calibration.data(), calibration_byte_count, calibration_type, 0U, 0U);
    initialize_batch(batches[0U], &dynamic_item, 0U, 5U);
    if (has_calibration) {
      initialize_batch(batches[1U], &calibration_item, 1U, 6U);
    }
  }

  [[nodiscard]] ksj_output_grant* output_grant() noexcept {
    return reinterpret_cast<ksj_output_grant*>(&output_grant_storage);
  }

  static void initialize_batch(ksj_input_batch_view& batch, const ksj_input_item_view* const item,
                               const std::uint32_t port, const std::uint64_t batch_id) noexcept {
    batch = {};
    batch.abi = make_header(sizeof(batch));
    batch.items = item;
    batch.item_count = 1U;
    batch.input_port = port;
    batch.batch_id = batch_id;
    batch.order_domain = 7U;
  }

  ksj_type_descriptor_view dynamic_type{};
  ksj_type_descriptor_view calibration_type{};
  ksj_type_descriptor_view output_type{};
  alignas(64) std::array<std::byte, 512U> dynamic{};
  alignas(64) std::array<std::byte, 512U> calibration{};
  alignas(64) std::array<std::byte, 512U> output{};
  std::uint64_t dynamic_byte_count{0U};
  std::uint64_t calibration_byte_count{0U};
  std::uint64_t output_byte_count{0U};
  std::uint32_t input_batch_count{0U};
  alignas(std::max_align_t) std::byte output_grant_storage{};
  ksj_input_item_view dynamic_item{};
  ksj_input_item_view calibration_item{};
  std::array<ksj_input_batch_view, 2U> batches{};
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

ksj_status KSJ_PROVIDER_CALL get_info(void* const host_context, const ksj_firing_lease*,
                                      ksj_firing_lease_info* const out_info, ksj_error_view*) noexcept {
  const auto* const state = static_cast<const HostState*>(host_context);
  if (state == nullptr || out_info == nullptr) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_info = {};
  out_info->abi = make_header(sizeof(*out_info));
  out_info->resource_occurrence_id = 3U;
  out_info->slot_generation = 5U;
  out_info->terminal_epoch = state->terminal_epoch;
  out_info->input_batch_count = state->input_batch_count;
  out_info->output_grant_count = 1U;
  out_info->reserved_output_bytes = state->output_byte_count;
  out_info->reserved_scratch_bytes = 0U;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL get_input_batch(void* const host_context, const ksj_firing_lease*,
                                             const std::uint32_t index, ksj_input_batch_view* const out_batch,
                                             ksj_error_view*) noexcept {
  const auto* const state = static_cast<const HostState*>(host_context);
  if (state == nullptr || out_batch == nullptr || index >= state->input_batch_count) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_batch = state->batches[index];
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL acquire_output_grant(void* const host_context, ksj_firing_lease*, const std::uint32_t slot,
                                                  ksj_output_grant** const out_grant, ksj_error_view*) noexcept {
  auto* const state = static_cast<HostState*>(host_context);
  if (state == nullptr || out_grant == nullptr || slot != 0U || state->output_acquired) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ++state->acquire_calls;
  state->output_acquired = true;
  *out_grant = state->output_grant();
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL map_output(void* const host_context, ksj_output_grant* const grant,
                                        ksj_mutable_payload_view* const out_payload, ksj_error_view*) noexcept {
  auto* const state = static_cast<HostState*>(host_context);
  if (state == nullptr || out_payload == nullptr || grant != state->output_grant() || !state->output_acquired ||
      state->output_mapped) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ++state->map_calls;
  state->output_mapped = true;
  *out_payload = {};
  out_payload->abi = make_header(sizeof(*out_payload));
  out_payload->data = state->output.data();
  out_payload->capacity_bytes = state->output_byte_count;
  out_payload->memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
  out_payload->alignment = 64U;
  out_payload->type = state->output_type;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL seal_output(void* const host_context, ksj_output_grant* const grant,
                                         const ksj_output_seal_descriptor* const descriptor, ksj_error_view*) noexcept {
  auto* const state = static_cast<HostState*>(host_context);
  if (state == nullptr || descriptor == nullptr || grant != state->output_grant() || !state->output_mapped ||
      state->output_sealed || descriptor->output_port != 0U || descriptor->produced_item_count != 1U ||
      descriptor->produced_byte_count != state->output_byte_count || descriptor->metadata.size != 0U ||
      descriptor->semantic_key_hash != state->dynamic_item.semantic_key_hash ||
      descriptor->order_key != state->dynamic_item.order_key ||
      !matches_registered_output_type(state->output_type, descriptor->type)) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ++state->seal_calls;
  state->output_sealed = true;
  state->sealed_descriptor = *descriptor;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL release_output(void* const host_context, ksj_output_grant* const grant,
                                            ksj_error_view*) noexcept {
  auto* const state = static_cast<HostState*>(host_context);
  if (state == nullptr || grant != state->output_grant() || state->output_sealed) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ++state->release_calls;
  return KSJ_STATUS_OK;
}

struct CallbackTables {
  explicit CallbackTables(HostState* const state) {
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
  return std::filesystem::path(KSJ_KSPACE_CONDITIONING_PROVIDER_MODULE);
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
    const auto* const api = lease.api();
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

[[nodiscard]] ProviderInstance make_instance(const OperatorCase& operator_case) {
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
  const auto* const api = instance.lease.api();

  ksj_operator_create_request create{};
  create.abi = make_header(sizeof(create));
  create.operator_id = text(operator_case.id, operator_case.id_size);
  create.canonical_config.abi = make_header(sizeof(create.canonical_config));
  create.canonical_config.data = operator_case.canonical_config;
  create.canonical_config.size = operator_case.canonical_config_size;
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

[[nodiscard]] ksj_status process_status(ProviderInstance& instance, HostState& state) {
  EXPECT_TRUE(instance.lease.valid());
  EXPECT_NE(instance.lease.api(), nullptr);
  EXPECT_NE(instance.operator_handle, nullptr);
  EXPECT_NE(instance.context, nullptr);
  EXPECT_NE(instance.key_state, nullptr);
  CallbackTables callbacks{&state};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  return instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                      opaque_lease(), &callbacks.lease, &result, &error);
}

void process_one(ProviderInstance& instance, HostState& state, const std::uint64_t expected_consumed_inputs) {
  ASSERT_TRUE(instance.lease.valid());
  ASSERT_NE(instance.lease.api(), nullptr);
  ASSERT_NE(instance.operator_handle, nullptr);
  ASSERT_NE(instance.context, nullptr);
  ASSERT_NE(instance.key_state, nullptr);
  CallbackTables callbacks{&state};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  ASSERT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.sealed_output_count, 1U);
  EXPECT_EQ(result.consumed_input_item_count, expected_consumed_inputs);
  EXPECT_EQ(result.terminal_epoch, state.terminal_epoch);
  EXPECT_TRUE(state.output_sealed);
  EXPECT_EQ(state.acquire_calls, 1U);
  EXPECT_EQ(state.map_calls, 1U);
  EXPECT_EQ(state.seal_calls, 1U);
  EXPECT_EQ(state.release_calls, 0U);
}

TEST(KSpaceConditioningProvider, LoadsSixOperatorsWithTheirContractIdentities) {
  auto loaded = ProviderModule::load(provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  const auto* const descriptor = loaded.value().descriptor();
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->provider_id, kProviderId);
  ASSERT_EQ(descriptor->operators.size(), 6U);
  EXPECT_EQ(descriptor->operators[0U].operator_id, kNoiseOperatorId);
  EXPECT_EQ(descriptor->operators[1U].operator_id, kPhaseOperatorId);
  EXPECT_EQ(descriptor->operators[2U].operator_id, kCoilOperatorId);
  EXPECT_EQ(descriptor->operators[3U].operator_id, kCropOperatorId);
  EXPECT_EQ(descriptor->operators[4U].operator_id, kNoncartesianNoiseOperatorId);
  EXPECT_EQ(descriptor->operators[5U].operator_id, kNoncartesianCoilOperatorId);

  const auto bundle = digest_from_hex(kProviderBundleDigestHex);
  EXPECT_EQ(std::memcmp(descriptor->bundle_digest.data(), bundle.bytes, KSJ_PROVIDER_DIGEST256_SIZE), 0);
}

TEST(KSpaceConditioningProvider, RejectsMalformedCanonicalConfigurations) {
  auto loaded = ProviderModule::load(provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  auto lease = loaded.value().acquire();
  ASSERT_TRUE(lease.valid());
  ASSERT_NE(lease.api(), nullptr);

  struct InvalidConfiguration {
    const OperatorCase* operator_case;
    const char* config;
  };
  constexpr std::array<InvalidConfiguration, 6U> invalids{{
    {&kNoiseOperator, "{\"channel_count\":0,\"cols\":2,\"rows\":2}"},
    {&kPhaseOperator, "{\"channel_count\":2,\"rows\":2,\"cols\":2}"},
    {&kCoilOperator, "{\"cols\":2,\"physical_channel_count\":1,\"rows\":2,\"virtual_channel_count\":2}"},
    {&kCropOperator, "{\"channel_count\":2,\"input_cols\":4,\"output_cols\":3,\"readout_offset\":2,\"rows\":2}"},
    {&kNoncartesianNoiseOperator, "{\"channel_count\":2,\"sample_count\":0}"},
    {&kNoncartesianCoilOperator, "{\"physical_channel_count\":1,\"sample_count\":3,\"virtual_channel_count\":2}"},
  }};
  for (const auto& invalid : invalids) {
    ksj_operator_create_request create{};
    create.abi = make_header(sizeof(create));
    create.operator_id = text(invalid.operator_case->id, invalid.operator_case->id_size);
    create.canonical_config.abi = make_header(sizeof(create.canonical_config));
    create.canonical_config.data = invalid.config;
    create.canonical_config.size = std::strlen(invalid.config);
    ksj_provider_operator* operator_handle = nullptr;
    auto error = empty_error();
    EXPECT_EQ(lease.api()->operator_create(&create, &operator_handle, &error), KSJ_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(operator_handle, nullptr);
  }
}

TEST(KSpaceConditioningProvider, PrewhitensChannelMajorKspaceWithExplicitNoiseModel) {
  auto instance = make_instance(kNoiseOperator);
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState state{
    ksj_type_registry_kspace_frame(), 8U * kComplexBytes, ksj_type_registry_noise_model(), 4U * kComplexBytes, true,
    ksj_type_registry_kspace_frame(), 8U * kComplexBytes};
  const std::array<std::complex<float>, 8U> kspace{
    std::complex<float>{1.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}, std::complex<float>{3.0F, 0.0F},
    std::complex<float>{4.0F, 0.0F}, std::complex<float>{5.0F, 0.0F}, std::complex<float>{6.0F, 0.0F},
    std::complex<float>{7.0F, 0.0F}, std::complex<float>{8.0F, 0.0F},
  };
  const std::array<std::complex<float>, 4U> whitening{
    std::complex<float>{2.0F, 0.0F},
    std::complex<float>{0.0F, 0.0F},
    std::complex<float>{0.0F, 0.0F},
    std::complex<float>{3.0F, 0.0F},
  };
  for (std::size_t index = 0U; index < kspace.size(); ++index) {
    write_complex(state.dynamic.data(), index, kspace[index]);
  }
  for (std::size_t index = 0U; index < whitening.size(); ++index) {
    write_complex(state.calibration.data(), index, whitening[index]);
  }

  process_one(instance, state, 2U);
  const std::array<float, 8U> expected{2.0F, 4.0F, 6.0F, 8.0F, 15.0F, 18.0F, 21.0F, 24.0F};
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    EXPECT_NEAR(read_complex(state.output.data(), index).real(), expected[index], 1.0e-5F);
    EXPECT_NEAR(read_complex(state.output.data(), index).imag(), 0.0F, 1.0e-5F);
  }
}

TEST(KSpaceConditioningProvider, AppliesPerChannelPerReadoutPhaseModel) {
  auto instance = make_instance(kPhaseOperator);
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState state{
    ksj_type_registry_kspace_frame(), 8U * kComplexBytes, ksj_type_registry_phase_model(), 4U * kComplexBytes, true,
    ksj_type_registry_kspace_frame(), 8U * kComplexBytes};
  const std::array<std::complex<float>, 8U> kspace{
    std::complex<float>{1.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}, std::complex<float>{3.0F, 0.0F},
    std::complex<float>{4.0F, 0.0F}, std::complex<float>{1.0F, 1.0F}, std::complex<float>{2.0F, 1.0F},
    std::complex<float>{3.0F, 1.0F}, std::complex<float>{4.0F, 1.0F},
  };
  const std::array<std::complex<float>, 4U> phase{
    std::complex<float>{0.0F, 1.0F},
    std::complex<float>{0.0F, -1.0F},
    std::complex<float>{1.0F, 0.0F},
    std::complex<float>{-1.0F, 0.0F},
  };
  for (std::size_t index = 0U; index < kspace.size(); ++index) {
    write_complex(state.dynamic.data(), index, kspace[index]);
  }
  for (std::size_t index = 0U; index < phase.size(); ++index) {
    write_complex(state.calibration.data(), index, phase[index]);
  }

  process_one(instance, state, 2U);
  const std::array<std::complex<float>, 8U> expected{
    std::complex<float>{0.0F, 1.0F},  std::complex<float>{0.0F, -2.0F},  std::complex<float>{0.0F, 3.0F},
    std::complex<float>{0.0F, -4.0F}, std::complex<float>{1.0F, 1.0F},   std::complex<float>{-2.0F, -1.0F},
    std::complex<float>{3.0F, 1.0F},  std::complex<float>{-4.0F, -1.0F},
  };
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    const auto actual = read_complex(state.output.data(), index);
    EXPECT_NEAR(actual.real(), expected[index].real(), 1.0e-5F);
    EXPECT_NEAR(actual.imag(), expected[index].imag(), 1.0e-5F);
  }
}

TEST(KSpaceConditioningProvider, CompressesPhysicalChannelsWithExplicitBasis) {
  auto instance = make_instance(kCoilOperator);
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState state{ksj_type_registry_kspace_frame(),
                  8U * kComplexBytes,
                  ksj_type_registry_coil_compression_basis(),
                  2U * kComplexBytes,
                  true,
                  ksj_type_registry_kspace_frame(),
                  4U * kComplexBytes};
  const std::array<std::complex<float>, 8U> kspace{
    std::complex<float>{1.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}, std::complex<float>{3.0F, 0.0F},
    std::complex<float>{4.0F, 0.0F}, std::complex<float>{5.0F, 0.0F}, std::complex<float>{6.0F, 0.0F},
    std::complex<float>{7.0F, 0.0F}, std::complex<float>{8.0F, 0.0F},
  };
  const std::array<std::complex<float>, 2U> basis{
    std::complex<float>{1.0F, 0.0F},
    std::complex<float>{2.0F, 0.0F},
  };
  for (std::size_t index = 0U; index < kspace.size(); ++index) {
    write_complex(state.dynamic.data(), index, kspace[index]);
  }
  for (std::size_t index = 0U; index < basis.size(); ++index) {
    write_complex(state.calibration.data(), index, basis[index]);
  }

  process_one(instance, state, 2U);
  const std::array<float, 4U> expected{11.0F, 14.0F, 17.0F, 20.0F};
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    EXPECT_NEAR(read_complex(state.output.data(), index).real(), expected[index], 1.0e-5F);
    EXPECT_NEAR(read_complex(state.output.data(), index).imag(), 0.0F, 1.0e-5F);
  }
}

TEST(KSpaceConditioningProvider, PrewhitensChannelMajorNoncartesianSamplesWithExplicitNoiseModel) {
  auto instance = make_instance(kNoncartesianNoiseOperator);
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState state{ksj_type_registry_noncartesian_kspace_frame(),
                  6U * kComplexBytes,
                  ksj_type_registry_noise_model(),
                  4U * kComplexBytes,
                  true,
                  ksj_type_registry_noncartesian_kspace_frame(),
                  6U * kComplexBytes};
  const std::array<std::complex<float>, 6U> kspace{
    std::complex<float>{1.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}, std::complex<float>{3.0F, 0.0F},
    std::complex<float>{4.0F, 0.0F}, std::complex<float>{5.0F, 0.0F}, std::complex<float>{6.0F, 0.0F},
  };
  const std::array<std::complex<float>, 4U> whitening{
    std::complex<float>{2.0F, 0.0F},
    std::complex<float>{0.0F, 0.0F},
    std::complex<float>{0.0F, 0.0F},
    std::complex<float>{3.0F, 0.0F},
  };
  for (std::size_t index = 0U; index < kspace.size(); ++index) {
    write_complex(state.dynamic.data(), index, kspace[index]);
  }
  for (std::size_t index = 0U; index < whitening.size(); ++index) {
    write_complex(state.calibration.data(), index, whitening[index]);
  }

  process_one(instance, state, 2U);
  const std::array<float, 6U> expected{2.0F, 4.0F, 6.0F, 12.0F, 15.0F, 18.0F};
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    EXPECT_NEAR(read_complex(state.output.data(), index).real(), expected[index], 1.0e-5F);
    EXPECT_NEAR(read_complex(state.output.data(), index).imag(), 0.0F, 1.0e-5F);
  }
}

TEST(KSpaceConditioningProvider, CompressesChannelMajorNoncartesianSamplesWithExplicitBasis) {
  auto instance = make_instance(kNoncartesianCoilOperator);
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState state{ksj_type_registry_noncartesian_kspace_frame(),
                  6U * kComplexBytes,
                  ksj_type_registry_coil_compression_basis(),
                  2U * kComplexBytes,
                  true,
                  ksj_type_registry_noncartesian_kspace_frame(),
                  3U * kComplexBytes};
  const std::array<std::complex<float>, 6U> kspace{
    std::complex<float>{1.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}, std::complex<float>{3.0F, 0.0F},
    std::complex<float>{4.0F, 0.0F}, std::complex<float>{5.0F, 0.0F}, std::complex<float>{6.0F, 0.0F},
  };
  const std::array<std::complex<float>, 2U> basis{
    std::complex<float>{1.0F, 0.0F},
    std::complex<float>{2.0F, 0.0F},
  };
  for (std::size_t index = 0U; index < kspace.size(); ++index) {
    write_complex(state.dynamic.data(), index, kspace[index]);
  }
  for (std::size_t index = 0U; index < basis.size(); ++index) {
    write_complex(state.calibration.data(), index, basis[index]);
  }

  process_one(instance, state, 2U);
  const std::array<float, 3U> expected{9.0F, 12.0F, 15.0F};
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    EXPECT_NEAR(read_complex(state.output.data(), index).real(), expected[index], 1.0e-5F);
    EXPECT_NEAR(read_complex(state.output.data(), index).imag(), 0.0F, 1.0e-5F);
  }
}

TEST(KSpaceConditioningProvider, RemovesOnlyTheConfiguredReadoutRange) {
  auto instance = make_instance(kCropOperator);
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState state{ksj_type_registry_kspace_frame(), 16U * kComplexBytes, {}, 0U, false,
                  ksj_type_registry_kspace_frame(), 8U * kComplexBytes};
  for (std::size_t channel = 0U; channel < 2U; ++channel) {
    for (std::size_t row = 0U; row < 2U; ++row) {
      for (std::size_t col = 0U; col < 4U; ++col) {
        const float value = static_cast<float>(100U * channel + 10U * row + col);
        write_complex(state.dynamic.data(), channel * 8U + row * 4U + col, {value, 0.0F});
      }
    }
  }

  process_one(instance, state, 1U);
  const std::array<float, 8U> expected{1.0F, 2.0F, 11.0F, 12.0F, 101.0F, 102.0F, 111.0F, 112.0F};
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    EXPECT_NEAR(read_complex(state.output.data(), index).real(), expected[index], 1.0e-5F);
  }
}

TEST(KSpaceConditioningProvider, RejectsWrongTypeAndByteShapesBeforeAnyOutputGrant) {
  auto noise = make_instance(kNoiseOperator);
  ASSERT_NE(noise.operator_handle, nullptr);
  HostState wrong_type{
    ksj_type_registry_image_frame(),  8U * kComplexBytes, ksj_type_registry_noise_model(), 4U * kComplexBytes, true,
    ksj_type_registry_kspace_frame(), 8U * kComplexBytes};
  EXPECT_EQ(process_status(noise, wrong_type), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(wrong_type.acquire_calls, 0U);

  auto phase = make_instance(kPhaseOperator);
  ASSERT_NE(phase.operator_handle, nullptr);
  HostState wrong_bytes{
    ksj_type_registry_kspace_frame(), 7U * kComplexBytes, ksj_type_registry_phase_model(), 4U * kComplexBytes, true,
    ksj_type_registry_kspace_frame(), 8U * kComplexBytes};
  EXPECT_EQ(process_status(phase, wrong_bytes), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(wrong_bytes.acquire_calls, 0U);

  auto noncartesian_noise = make_instance(kNoncartesianNoiseOperator);
  ASSERT_NE(noncartesian_noise.operator_handle, nullptr);
  HostState wrong_noncartesian_type{ksj_type_registry_kspace_frame(),
                                    6U * kComplexBytes,
                                    ksj_type_registry_noise_model(),
                                    4U * kComplexBytes,
                                    true,
                                    ksj_type_registry_noncartesian_kspace_frame(),
                                    6U * kComplexBytes};
  EXPECT_EQ(process_status(noncartesian_noise, wrong_noncartesian_type), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(wrong_noncartesian_type.acquire_calls, 0U);

  auto noncartesian_coil = make_instance(kNoncartesianCoilOperator);
  ASSERT_NE(noncartesian_coil.operator_handle, nullptr);
  HostState wrong_noncartesian_basis_bytes{ksj_type_registry_noncartesian_kspace_frame(),
                                           6U * kComplexBytes,
                                           ksj_type_registry_coil_compression_basis(),
                                           1U * kComplexBytes,
                                           true,
                                           ksj_type_registry_noncartesian_kspace_frame(),
                                           3U * kComplexBytes};
  EXPECT_EQ(process_status(noncartesian_coil, wrong_noncartesian_basis_bytes), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(wrong_noncartesian_basis_bytes.acquire_calls, 0U);

  auto noncartesian_output = make_instance(kNoncartesianNoiseOperator);
  ASSERT_NE(noncartesian_output.operator_handle, nullptr);
  HostState wrong_noncartesian_output_type{ksj_type_registry_noncartesian_kspace_frame(),
                                           6U * kComplexBytes,
                                           ksj_type_registry_noise_model(),
                                           4U * kComplexBytes,
                                           true,
                                           ksj_type_registry_kspace_frame(),
                                           6U * kComplexBytes};
  EXPECT_EQ(process_status(noncartesian_output, wrong_noncartesian_output_type), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(wrong_noncartesian_output_type.acquire_calls, 1U);
  EXPECT_EQ(wrong_noncartesian_output_type.release_calls, 1U);
}

TEST(KSpaceConditioningProvider, RejectsDuplicateCalibrationPortAndMismatchedInheritedIdentity) {
  auto duplicate = make_instance(kCoilOperator);
  ASSERT_NE(duplicate.operator_handle, nullptr);
  HostState duplicate_port{ksj_type_registry_kspace_frame(),
                           8U * kComplexBytes,
                           ksj_type_registry_coil_compression_basis(),
                           2U * kComplexBytes,
                           true,
                           ksj_type_registry_kspace_frame(),
                           4U * kComplexBytes};
  duplicate_port.batches[1U].input_port = 0U;
  EXPECT_EQ(process_status(duplicate, duplicate_port), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(duplicate_port.acquire_calls, 0U);

  auto keyed = make_instance(kCoilOperator);
  ASSERT_NE(keyed.operator_handle, nullptr);
  HostState mismatched_identity{ksj_type_registry_kspace_frame(),
                                8U * kComplexBytes,
                                ksj_type_registry_coil_compression_basis(),
                                2U * kComplexBytes,
                                true,
                                ksj_type_registry_kspace_frame(),
                                4U * kComplexBytes};
  mismatched_identity.calibration_item.semantic_key_hash = 12U;
  EXPECT_EQ(process_status(keyed, mismatched_identity), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(mismatched_identity.acquire_calls, 0U);

  auto noncartesian_keyed = make_instance(kNoncartesianNoiseOperator);
  ASSERT_NE(noncartesian_keyed.operator_handle, nullptr);
  HostState noncartesian_mismatched_order{ksj_type_registry_noncartesian_kspace_frame(),
                                          6U * kComplexBytes,
                                          ksj_type_registry_noise_model(),
                                          4U * kComplexBytes,
                                          true,
                                          ksj_type_registry_noncartesian_kspace_frame(),
                                          6U * kComplexBytes};
  noncartesian_mismatched_order.calibration_item.order_key = 18U;
  EXPECT_EQ(process_status(noncartesian_keyed, noncartesian_mismatched_order), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(noncartesian_mismatched_order.acquire_calls, 0U);
}

} // namespace
