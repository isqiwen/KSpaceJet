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

constexpr char kProviderId[] = "org.kspacejet.coil-combine";
constexpr char kProviderBundleDigestHex[] = "ba4f619047391419c8ffc42220ace7d888f047c9e2fd64aaeb78d9093191aed5";
constexpr char kOperatorId[] = "coil_combine_rss";
constexpr char kCanonicalConfig[] = "{\"channels\":2,\"cols\":2,\"rows\":2}";

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
  HostState() {
    input = {{{3.0F, 4.0F},
              {0.0F, 0.0F},
              {0.0F, 0.0F},
              {0.0F, 12.0F},
              {5.0F, 0.0F},
              {0.0F, 12.0F},
              {1.0F, 1.0F},
              {0.0F, 2.0F}}};
    item.abi = make_header(sizeof(item));
    item.payload.abi = make_header(sizeof(item.payload));
    item.payload.data = input.data();
    item.payload.byte_count = sizeof(input);
    item.payload.memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
    item.payload.alignment = 64U;
    item.payload.type = coil_image_type;
    item.metadata.abi = make_header(sizeof(item.metadata));
    item.semantic_key_hash = 7U;
    item.order_key = 9U;
    batch.abi = make_header(sizeof(batch));
    batch.items = &item;
    batch.item_count = 1U;
    batch.input_port = 0U;
  }

  [[nodiscard]] ksj_output_grant* output_grant() const noexcept {
    return reinterpret_cast<ksj_output_grant*>(const_cast<std::byte*>(&output_grant_storage));
  }

  ksj_type_descriptor_view coil_image_type{ksj_type_registry_coil_image_frame()};
  ksj_type_descriptor_view image_type{ksj_type_registry_image_frame()};
  alignas(64) std::array<std::complex<float>, 8U> input{};
  alignas(64) std::array<float, 4U> output{};
  alignas(std::max_align_t) std::byte output_grant_storage{};
  ksj_input_item_view item{};
  ksj_input_batch_view batch{};
  bool output_acquired{false};
  bool output_mapped{false};
  bool output_sealed{false};
  std::uint32_t acquire_calls{0U};
  std::uint32_t seal_calls{0U};
  std::uint64_t terminal_epoch{17U};
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
  out_info->input_batch_count = 1U;
  out_info->output_grant_count = 1U;
  out_info->reserved_output_bytes = sizeof(state->output);
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
  state->output_mapped = true;
  *out_payload = {};
  out_payload->abi = make_header(sizeof(*out_payload));
  out_payload->data = state->output.data();
  out_payload->capacity_bytes = sizeof(state->output);
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
      descriptor->produced_byte_count != sizeof(state->output)) {
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  ++state->seal_calls;
  state->output_sealed = true;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL release_output(void* host_context, ksj_output_grant* grant, ksj_error_view*) noexcept {
  const auto* state = static_cast<const HostState*>(host_context);
  return state != nullptr && grant == state->output_grant() && !state->output_sealed ? KSJ_STATUS_OK
                                                                                     : KSJ_STATUS_CONTRACT_VIOLATION;
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
  return std::filesystem::path(KSJ_COIL_COMBINE_PROVIDER_MODULE);
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

[[nodiscard]] ProviderInstance make_instance() {
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
  create.canonical_config.abi = make_header(sizeof(create.canonical_config));
  create.canonical_config.data = kCanonicalConfig;
  create.canonical_config.size = sizeof(kCanonicalConfig) - 1U;
  auto error = empty_error();
  EXPECT_EQ(api->operator_create(&create, &instance.operator_handle, &error), KSJ_STATUS_OK);
  if (instance.operator_handle == nullptr) {
    return instance;
  }
  ksj_execution_context_descriptor context_descriptor{};
  context_descriptor.abi = make_header(sizeof(context_descriptor));
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
  return instance;
}

TEST(CoilCombineProvider, PublishesTheRssOperatorAndBundleIdentity) {
  auto module = ProviderModule::load(provider_path());
  ASSERT_TRUE(module.ok()) << module.status();
  const auto* descriptor = module.value().descriptor();
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->provider_id, kProviderId);
  ASSERT_EQ(descriptor->operators.size(), 1U);
  EXPECT_EQ(descriptor->operators.front().operator_id, kOperatorId);
  EXPECT_EQ(descriptor->operators.front().max_output_bytes_per_firing, 1024U * 1024U);
  const auto expected = digest_from_hex(kProviderBundleDigestHex);
  EXPECT_EQ(std::memcmp(descriptor->bundle_digest.data(), expected.bytes, KSJ_PROVIDER_DIGEST256_SIZE), 0);
}

TEST(CoilCombineProvider, ComputesRssAcrossTheCoilLastChannelAxis) {
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
  EXPECT_EQ(result.sealed_output_count, 1U);
  EXPECT_EQ(result.consumed_input_item_count, 1U);
  EXPECT_EQ(host.seal_calls, 1U);
  EXPECT_NEAR(host.output[0U], 5.0F, 1.0e-5F);
  EXPECT_NEAR(host.output[1U], 12.0F, 1.0e-5F);
  EXPECT_NEAR(host.output[2U], 13.0F, 1.0e-5F);
  EXPECT_NEAR(host.output[3U], std::sqrt(6.0F), 1.0e-5F);
}

TEST(CoilCombineProvider, RejectsAnInputWhoseGeneratedCoilImageTypeIdentityDoesNotMatch) {
  auto instance = make_instance();
  ASSERT_TRUE(instance.lease.valid());
  HostState host;
  host.item.payload.type.type_identity_digest = ksj_type_registry_image_frame().type_identity_digest;
  CallbackTables callbacks{&host};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  EXPECT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(host.acquire_calls, 0U);
}

TEST(CoilCombineProvider, RejectsAnOutputGrantWhoseGeneratedImageTypeIdentityDoesNotMatch) {
  auto instance = make_instance();
  ASSERT_TRUE(instance.lease.valid());
  HostState host;
  host.image_type.type_identity_digest = ksj_type_registry_coil_image_frame().type_identity_digest;
  CallbackTables callbacks{&host};
  ksj_process_result result{};
  result.abi = make_header(sizeof(result));
  auto error = empty_error();
  EXPECT_EQ(instance.lease.api()->operator_process_batch(instance.operator_handle, instance.context, instance.key_state,
                                                         opaque_lease(), &callbacks.lease, &result, &error),
            KSJ_STATUS_CONTRACT_VIOLATION);
  EXPECT_EQ(host.acquire_calls, 1U);
  EXPECT_EQ(host.seal_calls, 0U);
}

} // namespace
