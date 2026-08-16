#include "kspacejet/array/views.hpp"
#include "kspacejet/nufft/direct_nudft.hpp"
#include "kspacejet/provider/loader/provider_loader.hpp"
#include "kspacejet/provider/type_registry.h"

#include <gtest/gtest.h>

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <string_view>
#include <utility>

namespace {

using ksj::provider::loader::ProviderLease;
using ksj::provider::loader::ProviderModule;

constexpr char kProviderId[] = "org.kspacejet.noncartesian-recon";
constexpr char kProviderBundleDigestHex[] = "6ca4098a56512026f49ee9c023d92e20b063369dcfccb610e12f5a1132db94a0";
constexpr char kOperatorId[] = "noncartesian_adjoint_reconstruct";
constexpr char kTwoChannelConfig[] = "{\"channels\":2,\"image_cols\":2,\"image_rows\":2,\"sample_count\":2}";
constexpr char kOneChannelConfig[] = "{\"channels\":1,\"image_cols\":2,\"image_rows\":2,\"sample_count\":2}";

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
  return value >= '0' && value <= '9' ? static_cast<std::uint8_t>(value - '0')
                                      : static_cast<std::uint8_t>(value - 'a' + 10);
}

template <std::size_t N> [[nodiscard]] ksj_digest256 digest_from_hex(const char (&hex)[N]) noexcept {
  static_assert(N == KSJ_PROVIDER_DIGEST256_SIZE * 2U + 1U);
  ksj_digest256 value{};
  value.abi = make_header(sizeof(value));
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    const auto offset = static_cast<std::size_t>(index) * 2U;
    value.bytes[index] = static_cast<std::uint8_t>((hex_nibble(hex[offset]) << 4U) | hex_nibble(hex[offset + 1U]));
  }
  return value;
}

[[nodiscard]] ksj_error_view empty_error() noexcept {
  ksj_error_view error{};
  error.abi = make_header(sizeof(error));
  error.message.abi = make_header(sizeof(error.message));
  return error;
}

struct HostState {
  using Complex = std::complex<float>;

  HostState(const std::uint32_t channels = 2U, const std::uint32_t samples = 2U)
      : channels(channels), samples(samples),
        kspace_byte_count(static_cast<std::uint64_t>(channels) * samples * sizeof(Complex)),
        trajectory_byte_count(static_cast<std::uint64_t>(samples) * 2U * sizeof(float)),
        coil_image_byte_count(static_cast<std::uint64_t>(channels) * 4U * sizeof(Complex)),
        scratch_byte_count(4U * sizeof(Complex)) {
    kspace = {{{1.0F, 2.0F}, {3.0F, -1.0F}, {10.0F, -4.0F}, {-2.0F, 7.0F}}};
    trajectory = {{0.0F, 0.0F, std::numbers::pi_v<float>, -0.5F * std::numbers::pi_v<float>}};
    kspace_item.abi = make_header(sizeof(kspace_item));
    kspace_item.payload.abi = make_header(sizeof(kspace_item.payload));
    kspace_item.payload.data = kspace.data();
    kspace_item.payload.byte_count = kspace_byte_count;
    kspace_item.payload.memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
    kspace_item.payload.alignment = 64U;
    kspace_item.payload.type = kspace_type;
    kspace_item.metadata.abi = make_header(sizeof(kspace_item.metadata));
    kspace_item.metadata.data = kspace_metadata.data();
    kspace_item.metadata.size = kspace_metadata.size();
    kspace_item.semantic_key_hash = 71U;
    kspace_item.order_key = 73U;
    kspace_item.item_ordinal = 79U;

    trajectory_item.abi = make_header(sizeof(trajectory_item));
    trajectory_item.payload.abi = make_header(sizeof(trajectory_item.payload));
    trajectory_item.payload.data = trajectory.data();
    trajectory_item.payload.byte_count = trajectory_byte_count;
    trajectory_item.payload.memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
    trajectory_item.payload.alignment = 64U;
    trajectory_item.payload.type = trajectory_type;
    trajectory_item.metadata.abi = make_header(sizeof(trajectory_item.metadata));
    trajectory_item.semantic_key_hash = kspace_item.semantic_key_hash;
    trajectory_item.order_key = kspace_item.order_key;
    trajectory_item.item_ordinal = 83U;

    kspace_batch.abi = make_header(sizeof(kspace_batch));
    kspace_batch.items = &kspace_item;
    kspace_batch.item_count = 1U;
    kspace_batch.input_port = 0U;
    kspace_batch.batch_id = 5U;
    kspace_batch.order_domain = 7U;

    trajectory_batch.abi = make_header(sizeof(trajectory_batch));
    trajectory_batch.items = &trajectory_item;
    trajectory_batch.item_count = 1U;
    trajectory_batch.input_port = 1U;
    trajectory_batch.batch_id = 11U;
    trajectory_batch.order_domain = 13U;
  }

  [[nodiscard]] ksj_output_grant* output_grant() noexcept {
    return reinterpret_cast<ksj_output_grant*>(&output_grant_storage);
  }

  std::uint32_t channels{0U};
  std::uint32_t samples{0U};
  std::uint64_t kspace_byte_count{0U};
  std::uint64_t trajectory_byte_count{0U};
  std::uint64_t coil_image_byte_count{0U};
  std::uint64_t scratch_byte_count{0U};
  ksj_type_descriptor_view kspace_type{ksj_type_registry_noncartesian_kspace_frame()};
  ksj_type_descriptor_view trajectory_type{ksj_type_registry_trajectory_frame()};
  ksj_type_descriptor_view coil_image_type{ksj_type_registry_coil_image_frame()};
  alignas(64) std::array<Complex, 4U> kspace{};
  alignas(64) std::array<float, 4U> trajectory{};
  alignas(64) std::array<Complex, 8U> output{};
  alignas(64) std::array<std::byte, 4U * sizeof(Complex)> scratch{};
  std::array<std::byte, 3U> kspace_metadata{{std::byte{0x31}, std::byte{0x32}, std::byte{0x33}}};
  alignas(std::max_align_t) std::byte output_grant_storage{};
  ksj_input_item_view kspace_item{};
  ksj_input_item_view trajectory_item{};
  ksj_input_batch_view kspace_batch{};
  ksj_input_batch_view trajectory_batch{};
  std::uint32_t input_batch_count{2U};
  std::uint32_t input_batch_requests{0U};
  std::uint32_t acquire_calls{0U};
  std::uint32_t map_calls{0U};
  std::uint32_t seal_calls{0U};
  std::uint32_t release_calls{0U};
  bool output_acquired{false};
  bool output_mapped{false};
  bool output_sealed{false};
  bool reverse_batch_order{false};
  std::uint64_t terminal_epoch{89U};
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
  out_info->terminal_epoch = state->terminal_epoch;
  out_info->input_batch_count = state->input_batch_count;
  out_info->output_grant_count = 1U;
  out_info->reserved_output_bytes = state->coil_image_byte_count;
  out_info->reserved_scratch_bytes = state->scratch_byte_count;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL get_input_batch(void* host_context, const ksj_firing_lease*, const std::uint32_t index,
                                             ksj_input_batch_view* out_batch, ksj_error_view*) noexcept {
  auto* state = static_cast<HostState*>(host_context);
  if (state == nullptr || out_batch == nullptr || index > 1U) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  ++state->input_batch_requests;
  if (state->reverse_batch_order) {
    *out_batch = index == 0U ? state->trajectory_batch : state->kspace_batch;
  } else {
    *out_batch = index == 0U ? state->kspace_batch : state->trajectory_batch;
  }
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL get_scratch(void* host_context, const ksj_firing_lease*, ksj_scratch_view* out_scratch,
                                         ksj_error_view*) noexcept {
  auto* state = static_cast<HostState*>(host_context);
  if (state == nullptr || out_scratch == nullptr) {
    return KSJ_STATUS_INVALID_ARGUMENT;
  }
  *out_scratch = {};
  out_scratch->abi = make_header(sizeof(*out_scratch));
  out_scratch->data = state->scratch.data();
  out_scratch->byte_count = state->scratch_byte_count;
  out_scratch->memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
  out_scratch->alignment = 64U;
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
  out_payload->capacity_bytes = state->coil_image_byte_count;
  out_payload->memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
  out_payload->alignment = 64U;
  out_payload->type = state->coil_image_type;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL seal_output(void* host_context, ksj_output_grant* grant,
                                         const ksj_output_seal_descriptor* descriptor, ksj_error_view*) noexcept {
  auto* state = static_cast<HostState*>(host_context);
  if (state == nullptr || descriptor == nullptr || grant != state->output_grant() || !state->output_mapped ||
      state->output_sealed || descriptor->output_port != 0U || descriptor->produced_item_count != 1U ||
      descriptor->produced_byte_count != state->coil_image_byte_count) {
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

struct CallbackTables {
  explicit CallbackTables(HostState* state) {
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
  return std::filesystem::path(KSJ_NONCARTESIAN_RECON_PROVIDER_MODULE);
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

[[nodiscard]] ProviderInstance make_instance(const char* config, const std::uint64_t config_size) {
  ProviderInstance instance;
  auto module = ProviderModule::load(provider_path());
  EXPECT_TRUE(module.ok()) << module.status();
  if (!module.ok()) {
    return instance;
  }
  instance.module = std::move(module).value();
  instance.lease = instance.module.acquire();
  EXPECT_TRUE(instance.lease.valid());
  if (!instance.lease.valid()) {
    return instance;
  }
  const auto* api = instance.lease.api();
  ksj_operator_create_request create{};
  create.abi = make_header(sizeof(create));
  create.operator_id = text(kOperatorId, sizeof(kOperatorId) - 1U);
  set_config(create, config, config_size);
  auto error = empty_error();
  EXPECT_EQ(api->operator_create(&create, &instance.operator_handle, &error), KSJ_STATUS_OK);
  if (instance.operator_handle == nullptr) {
    return instance;
  }
  ksj_execution_context_descriptor context_descriptor{};
  context_descriptor.abi = make_header(sizeof(context_descriptor));
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
  error = empty_error();
  EXPECT_EQ(
    api->key_state_init(instance.operator_handle, instance.context, &key_descriptor, &instance.key_state, &error),
    KSJ_STATUS_OK);
  if (instance.key_state == nullptr) {
    return instance;
  }
  ksj_scan_start_descriptor start{};
  start.abi = make_header(sizeof(start));
  error = empty_error();
  EXPECT_EQ(api->operator_on_start(instance.operator_handle, instance.context, instance.key_state, &start, &error),
            KSJ_STATUS_OK);
  return instance;
}

[[nodiscard]] ProviderInstance make_one_channel_instance() {
  return make_instance(kOneChannelConfig, sizeof(kOneChannelConfig) - 1U);
}

[[nodiscard]] ProviderInstance make_two_channel_instance() {
  return make_instance(kTwoChannelConfig, sizeof(kTwoChannelConfig) - 1U);
}

[[nodiscard]] ksj_status process(ProviderInstance& instance, HostState& host, ksj_process_result& result,
                                 ksj_error_view& error) {
  CallbackTables callbacks{&host};
  return instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                      opaque_lease(), &callbacks.lease, &result, &error);
}

[[nodiscard]] ksj::nufft::Grid2D grid() {
  return {
    .rows = 2U,
    .cols = 2U,
    .row_origin = 0.5,
    .col_origin = 0.5,
  };
}

TEST(NonCartesianReconProvider, PublishesTheTwoInputAdjointOperatorIdentityAndBounds) {
  auto module = ProviderModule::load(provider_path());
  ASSERT_TRUE(module.ok()) << module.status();
  const auto* descriptor = module.value().descriptor();
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->provider_id, kProviderId);
  ASSERT_EQ(descriptor->operators.size(), 1U);
  EXPECT_EQ(descriptor->operators.front().operator_id, kOperatorId);
  EXPECT_EQ(descriptor->operators.front().max_input_items_per_firing, 2U);
  EXPECT_EQ(descriptor->operators.front().max_output_bytes_per_firing, 128U * 1024U * 1024U);
  EXPECT_EQ(descriptor->operators.front().max_scratch_bytes_per_firing, 2U * 1024U * 1024U);
  const auto expected = digest_from_hex(kProviderBundleDigestHex);
  EXPECT_EQ(std::memcmp(descriptor->bundle_digest.data(), expected.bytes, KSJ_PROVIDER_DIGEST256_SIZE), 0);
}

TEST(NonCartesianReconProvider, ReconstructsOneTinyChannelThroughThePublicDirectAdjoint) {
  auto instance = make_one_channel_instance();
  ASSERT_TRUE(instance.lease.valid());
  ASSERT_NE(instance.operator_handle, nullptr);
  HostState host{1U, 2U};

  std::array<HostState::Complex, 4U> expected{};
  const auto samples = ksj::array::VectorView<const HostState::Complex>{host.kspace.data(), 2U};
  const auto trajectory = ksj::array::MatrixView<const float>{host.trajectory.data(), 2U, 2U};
  const auto image = ksj::array::MatrixView<HostState::Complex>{expected.data(), 2U, 2U};
  ksj::nufft::direct_nudft2_adjoint(grid(), samples, trajectory, image);

  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  ASSERT_EQ(process(instance, host, result, error), KSJ_STATUS_OK);
  EXPECT_EQ(result.outcome, KSJ_PROVIDER_PROCESS_DONE);
  EXPECT_EQ(result.consumed_input_item_count, 2U);
  EXPECT_EQ(result.sealed_output_count, 1U);
  EXPECT_EQ(host.seal_calls, 1U);
  EXPECT_EQ(host.sealed_descriptor.semantic_key_hash, 71U);
  EXPECT_EQ(host.sealed_descriptor.order_key, 73U);
  EXPECT_EQ(host.sealed_descriptor.metadata.data, host.kspace_metadata.data());
  EXPECT_EQ(host.sealed_descriptor.metadata.size, host.kspace_metadata.size());
  for (std::size_t pixel = 0U; pixel < expected.size(); ++pixel) {
    EXPECT_NEAR(host.output[pixel].real(), expected[pixel].real(), 1.0e-5F);
    EXPECT_NEAR(host.output[pixel].imag(), expected[pixel].imag(), 1.0e-5F);
  }
}

TEST(NonCartesianReconProvider, EmitsEveryChannelInCoilLastOrder) {
  auto instance = make_two_channel_instance();
  ASSERT_TRUE(instance.lease.valid());
  HostState host{2U, 2U};

  std::array<HostState::Complex, 4U> expected_first{};
  std::array<HostState::Complex, 4U> expected_second{};
  const auto trajectory = ksj::array::MatrixView<const float>{host.trajectory.data(), 2U, 2U};
  ksj::nufft::direct_nudft2_adjoint(grid(), ksj::array::VectorView<const HostState::Complex>{host.kspace.data(), 2U},
                                    trajectory,
                                    ksj::array::MatrixView<HostState::Complex>{expected_first.data(), 2U, 2U});
  ksj::nufft::direct_nudft2_adjoint(
    grid(), ksj::array::VectorView<const HostState::Complex>{host.kspace.data() + 2U, 2U}, trajectory,
    ksj::array::MatrixView<HostState::Complex>{expected_second.data(), 2U, 2U});

  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  ASSERT_EQ(process(instance, host, result, error), KSJ_STATUS_OK);
  ASSERT_EQ(host.seal_calls, 1U);
  for (std::size_t pixel = 0U; pixel < expected_first.size(); ++pixel) {
    EXPECT_NEAR(host.output[pixel * 2U].real(), expected_first[pixel].real(), 1.0e-5F);
    EXPECT_NEAR(host.output[pixel * 2U].imag(), expected_first[pixel].imag(), 1.0e-5F);
    EXPECT_NEAR(host.output[pixel * 2U + 1U].real(), expected_second[pixel].real(), 1.0e-5F);
    EXPECT_NEAR(host.output[pixel * 2U + 1U].imag(), expected_second[pixel].imag(), 1.0e-5F);
  }
}

TEST(NonCartesianReconProvider, ResolvesTheTwoDynamicInputsByFixedPortRatherThanBatchIndex) {
  auto instance = make_two_channel_instance();
  ASSERT_TRUE(instance.lease.valid());
  HostState host{2U, 2U};
  host.reverse_batch_order = true;
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  ASSERT_EQ(process(instance, host, result, error), KSJ_STATUS_OK);
  EXPECT_EQ(result.consumed_input_item_count, 2U);
  EXPECT_EQ(host.seal_calls, 1U);
  EXPECT_EQ(host.sealed_descriptor.semantic_key_hash, host.kspace_item.semantic_key_hash);
  EXPECT_EQ(host.sealed_descriptor.order_key, host.kspace_item.order_key);
}

TEST(NonCartesianReconProvider, RejectsWrongTrajectoryTypeWithoutAcquiringOrSealingOutput) {
  auto instance = make_two_channel_instance();
  ASSERT_TRUE(instance.lease.valid());
  HostState host{2U, 2U};
  host.trajectory_item.payload.type.type_identity_digest = ksj_type_registry_coil_image_frame().type_identity_digest;
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  EXPECT_EQ(process(instance, host, result, error), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(host.acquire_calls, 0U);
  EXPECT_EQ(host.seal_calls, 0U);
  EXPECT_EQ(host.release_calls, 0U);
  EXPECT_EQ(result.sealed_output_count, 0U);
}

TEST(NonCartesianReconProvider, RejectsAnIncorrectTrajectoryPayloadSizeWithoutOutput) {
  auto instance = make_two_channel_instance();
  ASSERT_TRUE(instance.lease.valid());
  HostState host{2U, 2U};
  host.trajectory_item.payload.byte_count -= sizeof(float);
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  EXPECT_EQ(process(instance, host, result, error), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(host.acquire_calls, 0U);
  EXPECT_EQ(host.seal_calls, 0U);
  EXPECT_EQ(host.release_calls, 0U);
}

TEST(NonCartesianReconProvider, RejectsMismatchedKspaceAndTrajectoryIdentityWithoutOutput) {
  auto instance = make_two_channel_instance();
  ASSERT_TRUE(instance.lease.valid());
  HostState host{2U, 2U};
  host.trajectory_item.order_key = host.kspace_item.order_key + 1U;
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  EXPECT_EQ(process(instance, host, result, error), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(host.acquire_calls, 0U);
  EXPECT_EQ(host.seal_calls, 0U);
  EXPECT_EQ(host.release_calls, 0U);
}

TEST(NonCartesianReconProvider, RejectsMissingOrDuplicateSecondInputWithoutOutput) {
  auto instance = make_two_channel_instance();
  ASSERT_TRUE(instance.lease.valid());
  {
    HostState host{2U, 2U};
    host.input_batch_count = 1U;
    ksj_process_result result{};
    result.abi = make_header(sizeof(result));
    auto error = empty_error();
    EXPECT_EQ(process(instance, host, result, error), KSJ_STATUS_CONTRACT_VIOLATION);
    EXPECT_EQ(host.input_batch_requests, 0U);
    EXPECT_EQ(host.acquire_calls, 0U);
    EXPECT_EQ(host.seal_calls, 0U);
  }
  {
    HostState host{2U, 2U};
    host.trajectory_batch.input_port = 0U;
    ksj_process_result result{};
    result.abi = make_header(sizeof(result));
    auto error = empty_error();
    EXPECT_EQ(process(instance, host, result, error), KSJ_STATUS_CONTRACT_VIOLATION);
    EXPECT_EQ(host.acquire_calls, 0U);
    EXPECT_EQ(host.seal_calls, 0U);
  }
}

TEST(NonCartesianReconProvider, RejectsNonCanonicalAndOverBudgetConfiguration) {
  auto module = ProviderModule::load(provider_path());
  ASSERT_TRUE(module.ok()) << module.status();
  auto lease = module.value().acquire();
  ASSERT_TRUE(lease.valid());
  const auto* api = lease.api();

  constexpr char kWrongOrder[] = "{\"channels\":1,\"image_rows\":2,\"image_cols\":2,\"sample_count\":2}";
  constexpr char kOverBudget[] = "{\"channels\":64,\"image_cols\":512,\"image_rows\":512,\"sample_count\":65536}";
  for (const auto config : {std::string_view{kWrongOrder}, std::string_view{kOverBudget}}) {
    ksj_operator_create_request create{};
    create.abi = make_header(sizeof(create));
    create.operator_id = text(kOperatorId, sizeof(kOperatorId) - 1U);
    set_config(create, config.data(), config.size());
    ksj_provider_operator* operator_handle = nullptr;
    auto error = empty_error();
    EXPECT_EQ(api->operator_create(&create, &operator_handle, &error), KSJ_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(operator_handle, nullptr);
  }
}

TEST(NonCartesianReconProvider, ReleasesAnInvalidOutputGrantWithoutSealingAnOutput) {
  auto instance = make_two_channel_instance();
  ASSERT_TRUE(instance.lease.valid());
  HostState host{2U, 2U};
  host.coil_image_type.type_identity_digest = ksj_type_registry_noncartesian_kspace_frame().type_identity_digest;
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  EXPECT_EQ(process(instance, host, result, error), KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(host.acquire_calls, 1U);
  EXPECT_EQ(host.map_calls, 1U);
  EXPECT_EQ(host.seal_calls, 0U);
  EXPECT_EQ(host.release_calls, 1U);
  EXPECT_EQ(result.sealed_output_count, 0U);
}

} // namespace
