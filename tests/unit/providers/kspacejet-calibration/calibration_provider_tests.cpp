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

constexpr char kProviderId[] = "org.kspacejet.calibration";
constexpr char kProviderBundleDigestHex[] = "abd611a1428074691393e7057e21266246647acd9c8ec7d1d4ad65a971d40f25";
constexpr char kNoiseOperatorId[] = "noise_model_estimate";
constexpr char kNoiseConfig[] = "{\"channel_count\":2}";
constexpr char kPhaseOperatorId[] = "phase_correction_estimate";
constexpr char kPhaseConfig[] = "{\"channel_count\":2,\"readout_sample_count\":2}";
constexpr char kCoilOperatorId[] = "coil_compression_basis_estimate";
constexpr char kCoilConfig[] = "{\"physical_channel_count\":2,\"virtual_channel_count\":1}";
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

struct HostState {
  HostState(const ksj_type_descriptor_view input_type_value, const ksj_type_descriptor_view output_type_value,
            const std::uint64_t input_size, const std::uint64_t output_size, const std::uint64_t scratch_size = 0U)
      : input_type(input_type_value), output_type(output_type_value), input_byte_count(input_size),
        output_byte_count(output_size), scratch_byte_count(scratch_size) {
    item.abi = make_header(sizeof(item));
    item.payload.abi = make_header(sizeof(item.payload));
    item.payload.data = input.data();
    item.payload.byte_count = input_byte_count;
    item.payload.memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
    item.payload.alignment = 64U;
    item.payload.type = input_type;
    item.metadata.abi = make_header(sizeof(item.metadata));
    item.metadata.data = nullptr;
    item.metadata.size = 0U;
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

  ksj_type_descriptor_view input_type{};
  ksj_type_descriptor_view output_type{};
  alignas(64) std::array<std::byte, 512U> input{};
  alignas(64) std::array<std::byte, 512U> output{};
  alignas(64) std::array<std::byte, 512U> scratch{};
  std::uint64_t input_byte_count{0U};
  std::uint64_t output_byte_count{0U};
  std::uint64_t scratch_byte_count{0U};
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
  out_info->input_batch_count = 1U;
  out_info->output_grant_count = 1U;
  out_info->reserved_output_bytes = state->output_byte_count;
  out_info->reserved_scratch_bytes = state->scratch_byte_count;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL get_input_batch(void* const host_context, const ksj_firing_lease*,
                                             const std::uint32_t index, ksj_input_batch_view* const out_batch,
                                             ksj_error_view*) noexcept {
  const auto* const state = static_cast<const HostState*>(host_context);
  if (state == nullptr || out_batch == nullptr || index != 0U) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_batch = state->batch;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL get_scratch(void* const host_context, const ksj_firing_lease*,
                                         ksj_scratch_view* const out_scratch, ksj_error_view*) noexcept {
  const auto* const state = static_cast<const HostState*>(host_context);
  if (state == nullptr || out_scratch == nullptr || state->scratch_byte_count > state->scratch.size()) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_scratch = {};
  out_scratch->abi = make_header(sizeof(*out_scratch));
  out_scratch->data = const_cast<std::byte*>(state->scratch.data());
  out_scratch->byte_count = state->scratch_byte_count;
  out_scratch->memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
  out_scratch->alignment = 64U;
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
      descriptor->produced_byte_count != state->output_byte_count || descriptor->metadata.size != 0U) {
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

    lease.abi =
      make_header(sizeof(lease), KSJ_LEASE_CAP_INPUT_BATCHES | KSJ_LEASE_CAP_OUTPUT_GRANTS | KSJ_LEASE_CAP_SCRATCH);
    lease.host_context = state;
    lease.output_grants = &output;
    lease.get_info = &get_info;
    lease.get_input_batch = &get_input_batch;
    lease.get_scratch = &get_scratch;
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
  return std::filesystem::path(KSJ_CALIBRATION_PROVIDER_MODULE);
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

void process_one(ProviderInstance& instance, HostState& state) {
  ASSERT_TRUE(instance.lease.valid());
  ASSERT_NE(instance.lease.api(), nullptr);
  ASSERT_NE(instance.operator_handle, nullptr);
  ASSERT_NE(instance.context, nullptr);
  ASSERT_NE(instance.key_state, nullptr);
  CallbackTables callbacks{&state};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  EXPECT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.sealed_output_count, 1U);
  EXPECT_EQ(result.consumed_input_item_count, 1U);
  EXPECT_EQ(result.terminal_epoch, state.terminal_epoch);
  EXPECT_TRUE(state.output_sealed);
  EXPECT_EQ(state.acquire_calls, 1U);
  EXPECT_EQ(state.map_calls, 1U);
  EXPECT_EQ(state.seal_calls, 1U);
  EXPECT_EQ(state.release_calls, 0U);
}

TEST(CalibrationProvider, LoadsThreeOperatorsWithTheirContractIdentities) {
  auto loaded = ProviderModule::load(provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  const auto* const descriptor = loaded.value().descriptor();
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->provider_id, kProviderId);
  ASSERT_EQ(descriptor->operators.size(), 3U);
  EXPECT_EQ(descriptor->operators[0U].operator_id, kNoiseOperatorId);
  EXPECT_EQ(descriptor->operators[1U].operator_id, kPhaseOperatorId);
  EXPECT_EQ(descriptor->operators[2U].operator_id, kCoilOperatorId);

  const auto bundle = digest_from_hex(kProviderBundleDigestHex);
  EXPECT_EQ(std::memcmp(descriptor->bundle_digest.data(), bundle.bytes, KSJ_PROVIDER_DIGEST256_SIZE), 0);
}

TEST(CalibrationProvider, RejectsMalformedCanonicalConfiguration) {
  auto loaded = ProviderModule::load(provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  auto lease = loaded.value().acquire();
  ASSERT_TRUE(lease.valid());
  ASSERT_NE(lease.api(), nullptr);

  constexpr char kInvalidConfig[] = "{\"channel_count\":0}";
  ksj_operator_create_request create{};
  create.abi = make_header(sizeof(create));
  create.operator_id = text(kNoiseOperatorId, sizeof(kNoiseOperatorId) - 1U);
  create.canonical_config.abi = make_header(sizeof(create.canonical_config));
  create.canonical_config.data = kInvalidConfig;
  create.canonical_config.size = sizeof(kInvalidConfig) - 1U;
  ksj_provider_operator* operator_handle = nullptr;
  auto error = empty_error();
  EXPECT_EQ(lease.api()->operator_create(&create, &operator_handle, &error), KSJ_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(operator_handle, nullptr);
}

TEST(CalibrationProvider, EstimatesWhiteningModelFromChannelMajorNoiseSamples) {
  auto instance = make_instance(kNoiseOperator);
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState state{ksj_type_registry_noise_calibration_frame(), ksj_type_registry_noise_model(), 8U * kComplexBytes,
                  4U * kComplexBytes, 120U};
  const std::array<std::complex<float>, 8U> input{
    std::complex<float>{-1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},  std::complex<float>{-1.0F, 0.0F},
    std::complex<float>{1.0F, 0.0F},  std::complex<float>{-2.0F, 0.0F}, std::complex<float>{-2.0F, 0.0F},
    std::complex<float>{2.0F, 0.0F},  std::complex<float>{2.0F, 0.0F},
  };
  for (std::size_t index = 0U; index < input.size(); ++index) {
    write_complex(state.input.data(), index, input[index]);
  }

  process_one(instance, state);
  EXPECT_NE(ksj_type_registry_matches_noise_model(&state.sealed_descriptor.type), 0);
  EXPECT_NEAR(read_complex(state.output.data(), 0U).real(), 1.0F, 2.0e-4F);
  EXPECT_NEAR(read_complex(state.output.data(), 3U).real(), 0.5F, 2.0e-4F);
  EXPECT_NEAR(std::abs(read_complex(state.output.data(), 1U)), 0.0F, 2.0e-4F);
  EXPECT_NEAR(std::abs(read_complex(state.output.data(), 2U)), 0.0F, 2.0e-4F);
}

TEST(CalibrationProvider, EstimatesPerChannelReadoutPhasePhasors) {
  auto instance = make_instance(kPhaseOperator);
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState state{ksj_type_registry_phase_reference_frame(), ksj_type_registry_phase_model(), 8U * kComplexBytes,
                  4U * kComplexBytes};
  const std::array<std::complex<float>, 8U> input{
    std::complex<float>{0.0F, 1.0F},  std::complex<float>{-1.0F, 0.0F}, std::complex<float>{0.0F, 1.0F},
    std::complex<float>{-1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},  std::complex<float>{0.0F, 0.0F},
    std::complex<float>{1.0F, 0.0F},  std::complex<float>{0.0F, 0.0F},
  };
  for (std::size_t index = 0U; index < input.size(); ++index) {
    write_complex(state.input.data(), index, input[index]);
  }

  process_one(instance, state);
  EXPECT_NE(ksj_type_registry_matches_phase_model(&state.sealed_descriptor.type), 0);
  EXPECT_NEAR(read_complex(state.output.data(), 0U).real(), 0.0F, 1.0e-5F);
  EXPECT_NEAR(read_complex(state.output.data(), 0U).imag(), -1.0F, 1.0e-5F);
  EXPECT_NEAR(read_complex(state.output.data(), 1U).real(), -1.0F, 1.0e-5F);
  EXPECT_NEAR(read_complex(state.output.data(), 2U).real(), 1.0F, 1.0e-5F);
  EXPECT_NEAR(read_complex(state.output.data(), 3U).real(), 1.0F, 1.0e-5F);
  EXPECT_NEAR(read_complex(state.output.data(), 3U).imag(), 0.0F, 1.0e-5F);
}

TEST(CalibrationProvider, EmitsLeadingHermitianEigenmodeAsCompressionBasis) {
  auto instance = make_instance(kCoilOperator);
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState state{ksj_type_registry_kspace_frame(), ksj_type_registry_coil_compression_basis(), 8U * kComplexBytes,
                  2U * kComplexBytes, 104U};
  const std::array<std::complex<float>, 8U> input{
    std::complex<float>{2.0F, 0.0F},  std::complex<float>{-2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F},
    std::complex<float>{-2.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},  std::complex<float>{1.0F, 0.0F},
    std::complex<float>{-1.0F, 0.0F}, std::complex<float>{-1.0F, 0.0F},
  };
  for (std::size_t index = 0U; index < input.size(); ++index) {
    write_complex(state.input.data(), index, input[index]);
  }

  process_one(instance, state);
  EXPECT_NE(ksj_type_registry_matches_coil_compression_basis(&state.sealed_descriptor.type), 0);
  const auto first = read_complex(state.output.data(), 0U);
  const auto second = read_complex(state.output.data(), 1U);
  EXPECT_NEAR(std::norm(first) + std::norm(second), 1.0F, 1.0e-4F);
  EXPECT_GT(std::abs(first), 0.999F);
  EXPECT_LT(std::abs(second), 1.0e-3F);
}

} // namespace
