#include "kspacejet/recon/runtime/synchronous_firing_lease.hpp"

#include "kspacejet/provider/loader/provider_loader.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <string_view>
#include <utility>

namespace ksj::recon::runtime {

// The preaccounted path is deliberately not a public raw-span host API. This
// narrow test friend exercises the same private graph capability that the
// executor uses, including its frozen staging proof.
class SynchronousFiringLeaseHostTestAccess final {
public:
  [[nodiscard]] static ksj::base::Result<SynchronousFiringLeaseHost>
  create_preaccounted(SynchronousFiringLeaseConfig config) {
    return SynchronousFiringLeaseHost::create_preaccounted_staging(std::move(config));
  }

  [[nodiscard]] static ksj::base::Result<SynchronousFiringResult>
  process(SynchronousFiringLeaseHost& host, const SynchronousProviderInvocation& invocation,
          const SynchronousFiringRequest& request) {
    return host.process_preaccounted_output(invocation, request);
  }

  [[nodiscard]] static ksj::base::Result<SynchronousFiringResult>
  on_scan_end(SynchronousFiringLeaseHost& host, const SynchronousProviderInvocation& invocation,
              const SynchronousFiringRequest& request, const std::uint64_t completed_input_item_count) {
    return host.on_scan_end_preaccounted_output(invocation, request, completed_input_item_count);
  }
};

} // namespace ksj::recon::runtime

namespace {

using ksj::base::Status;
using ksj::provider::loader::ProviderLease;
using ksj::provider::loader::ProviderModule;
using ksj::recon::ResourceVector;
using ksj::recon::ResourceVectorCapacity;
using ksj::recon::ResourceVectorCapacitySpec;
using ksj::recon::ResourceVectorSpec;
using ksj::recon::runtime::ResourceVectorLedger;
using ksj::recon::runtime::ResourceVectorLedgerUsage;
using ksj::recon::runtime::SynchronousFiringLeaseConfig;
using ksj::recon::runtime::SynchronousFiringLeaseHost;
using ksj::recon::runtime::SynchronousFiringLeaseHostTestAccess;
using ksj::recon::runtime::SynchronousFiringOutcome;
using ksj::recon::runtime::SynchronousFiringRequest;
using ksj::recon::runtime::SynchronousInputBatch;
using ksj::recon::runtime::SynchronousInputItem;
using ksj::recon::runtime::SynchronousOutputGrantSpec;
using ksj::recon::runtime::SynchronousProviderInvocation;

constexpr char kTestOperatorId[] = "synchronous_firing_lease_test_operator";
constexpr char kNoOutputTestOperatorId[] = "synchronous_firing_lease_no_output_test_operator";

[[nodiscard]] ksj_provider_abi_header header(const std::uint32_t size, const std::uint64_t capabilities = 0U) noexcept {
  return ksj_provider_abi_header_make(size, capabilities);
}

[[nodiscard]] ksj_utf8_view text(const char* data, const std::uint64_t size) noexcept {
  ksj_utf8_view result{};
  result.abi = header(sizeof(result));
  result.data = data;
  result.size = size;
  return result;
}

[[nodiscard]] ksj_digest256 digest(const std::uint8_t seed) noexcept {
  ksj_digest256 result{};
  result.abi = header(sizeof(result));
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    result.bytes[index] = static_cast<std::uint8_t>(seed + index);
  }
  return result;
}

[[nodiscard]] ksj_error_view error_storage() noexcept {
  ksj_error_view result{};
  result.abi = header(sizeof(result));
  result.message.abi = header(sizeof(result.message));
  return result;
}

struct TestPayloadType {
  TestPayloadType() {
    value.abi = header(sizeof(value));
    value.type_ref =
      text("org.kspacejet.tests.host-pageable-bytes", sizeof("org.kspacejet.tests.host-pageable-bytes") - 1U);
    value.type_identity_digest = digest(0x21U);
    value.payload_kind = 1U;
    value.element_type = 1U;
    value.rank = 0U;
    value.dimension_names = nullptr;
    value.allowed_memory_domains = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
    value.minimum_alignment = 1U;
    value.mutability = 0U;
  }

  ksj_type_descriptor_view value{};
};

[[nodiscard]] ResourceVector resource_vector(const std::uint64_t host_normal_bytes,
                                             const std::uint64_t descriptor_count) {
  auto resource = ResourceVector::create({
    .host_normal_bytes = host_normal_bytes,
    .descriptor_count = descriptor_count,
    .cpu_leaf_permits = 1U,
  });
  EXPECT_TRUE(resource.ok()) << resource.status();
  return std::move(resource).value();
}

[[nodiscard]] std::shared_ptr<ResourceVectorLedger> make_ledger(const std::uint64_t host_normal_bytes,
                                                                const std::uint64_t descriptor_count) {
  auto capacity = ResourceVectorCapacity::create({
    .domains =
      {
        .host_normal_bytes = host_normal_bytes,
        .descriptor_count = descriptor_count,
        .cpu_leaf_permits = 1U,
      },
    .host_total_cap_bytes = host_normal_bytes,
  });
  EXPECT_TRUE(capacity.ok()) << capacity.status();
  return std::make_shared<ResourceVectorLedger>(std::move(capacity).value());
}

[[nodiscard]] SynchronousFiringLeaseHost make_host(std::shared_ptr<ResourceVectorLedger> ledger,
                                                   const std::uint64_t host_normal_bytes,
                                                   const std::uint64_t descriptor_count,
                                                   const std::uint32_t maximum_output_grants = 1U) {
  SynchronousFiringLeaseConfig config{std::move(ledger), resource_vector(host_normal_bytes, descriptor_count)};
  config.maximum_input_batches = 1U;
  config.maximum_input_items = 1U;
  config.maximum_output_grants = maximum_output_grants;
  config.maximum_input_payload_bytes = 16U;
  config.maximum_scratch_bytes = 0U;
  auto host = SynchronousFiringLeaseHost::create(std::move(config));
  EXPECT_TRUE(host.ok()) << host.status();
  return std::move(host).value();
}

[[nodiscard]] SynchronousFiringLeaseHost make_host(const std::uint64_t host_normal_bytes,
                                                   const std::uint64_t descriptor_count,
                                                   const std::uint32_t maximum_output_grants = 1U) {
  // The shared ledger must cover both persistent ABI staging and the dynamic
  // firing bundle. Individual behavior tests use a deliberately ample local
  // ledger; constrained shared-ledger behavior is covered separately below.
  return make_host(make_ledger(64U * 1024U, 64U), host_normal_bytes, descriptor_count, maximum_output_grants);
}

void expect_ledger_idle(const std::shared_ptr<ResourceVectorLedger>& ledger) {
  ASSERT_NE(nullptr, ledger);
  const auto snapshot = ledger->snapshot();
  EXPECT_TRUE(snapshot.reserved.empty());
  EXPECT_TRUE(snapshot.used.empty());
}

[[nodiscard]] ResourceVectorLedgerUsage persistent_usage(const std::shared_ptr<ResourceVectorLedger>& ledger) {
  EXPECT_NE(nullptr, ledger);
  return ledger->snapshot().used;
}

void expect_only_persistent_usage(const std::shared_ptr<ResourceVectorLedger>& ledger,
                                  const ResourceVectorLedgerUsage& expected) {
  ASSERT_NE(nullptr, ledger);
  const auto snapshot = ledger->snapshot();
  EXPECT_TRUE(snapshot.reserved.empty());
  EXPECT_EQ(expected, snapshot.used);
}

struct ProviderInstance {
  ProviderModule module{};
  ProviderLease lease{};
  ksj_provider_operator* operator_handle{nullptr};
  ksj_execution_context* execution_context{nullptr};
  ksj_key_state* key_state{nullptr};

  ProviderInstance() = default;
  ProviderInstance(const ProviderInstance&) = delete;
  ProviderInstance& operator=(const ProviderInstance&) = delete;
  ProviderInstance(ProviderInstance&&) = delete;
  ProviderInstance& operator=(ProviderInstance&&) = delete;

  ~ProviderInstance() {
    if (!lease.valid() || lease.api() == nullptr) {
      return;
    }
    const auto* api = lease.api();
    if (key_state != nullptr) {
      api->key_state_reset(operator_handle, execution_context, key_state);
      key_state = nullptr;
    }
    if (execution_context != nullptr) {
      api->execution_context_destroy(operator_handle, execution_context);
      execution_context = nullptr;
    }
    if (operator_handle != nullptr) {
      api->operator_destroy(operator_handle);
      operator_handle = nullptr;
    }
  }

  [[nodiscard]] SynchronousProviderInvocation invocation(std::string_view operator_id) const {
    return {
      .provider = lease,
      .operator_id = std::string(operator_id),
      .operator_handle = operator_handle,
      .execution_context = execution_context,
      .key_state = key_state,
    };
  }
};

[[nodiscard]] Status initialize_provider(ProviderInstance& instance, const std::filesystem::path& module_path,
                                         const std::string_view operator_id, const std::string_view canonical_config) {
  auto loaded = ProviderModule::load(module_path);
  if (!loaded.ok()) {
    return loaded.status();
  }
  instance.module = std::move(loaded).value();
  instance.lease = instance.module.acquire();
  const auto* api = instance.lease.api();
  if (api == nullptr || instance.lease.descriptor() == nullptr) {
    return Status::InternalError("loaded test Provider has no ABI table");
  }
  if (std::find_if(instance.lease.descriptor()->operators.begin(), instance.lease.descriptor()->operators.end(),
                   [operator_id](const auto& candidate) {
                     return candidate.operator_id == operator_id;
                   }) == instance.lease.descriptor()->operators.end()) {
    return Status::NotFound("loaded test Provider does not expose the requested operator");
  }

  ksj_operator_create_request create{};
  create.abi = header(sizeof(create));
  create.operator_id = text(operator_id.data(), operator_id.size());
  create.canonical_config.abi = header(sizeof(create.canonical_config));
  create.canonical_config.data = canonical_config.data();
  create.canonical_config.size = canonical_config.size();
  auto error = error_storage();
  if (api->operator_create(&create, &instance.operator_handle, &error) != KSJ_STATUS_OK ||
      instance.operator_handle == nullptr) {
    return Status::ValidationError("test Provider rejected operator creation");
  }

  ksj_execution_context_descriptor context{};
  context.abi = header(sizeof(context));
  context.execution_context_id = 1U;
  context.max_backend_concurrency = 1U;
  error = error_storage();
  if (api->execution_context_create(instance.operator_handle, &context, &instance.execution_context, &error) !=
        KSJ_STATUS_OK ||
      instance.execution_context == nullptr) {
    return Status::ValidationError("test Provider rejected execution-context creation");
  }

  ksj_key_state_descriptor key{};
  key.abi = header(sizeof(key));
  key.semantic_key.abi = header(sizeof(key.semantic_key));
  key.key_state_generation = 1U;
  error = error_storage();
  if (api->key_state_init(instance.operator_handle, instance.execution_context, &key, &instance.key_state, &error) !=
        KSJ_STATUS_OK ||
      instance.key_state == nullptr) {
    return Status::ValidationError("test Provider rejected key-state creation");
  }

  ksj_scan_start_descriptor start{};
  start.abi = header(sizeof(start));
  start.run_id = text("run", 3U);
  start.scan_id = text("scan", 4U);
  start.normalized_scan_facts_digest = digest(0x41U);
  start.execution_plan_digest = digest(0x51U);
  start.terminal_epoch = 7U;
  error = error_storage();
  if (api->operator_on_start(instance.operator_handle, instance.execution_context, instance.key_state, &start,
                             &error) != KSJ_STATUS_OK) {
    return Status::ValidationError("test Provider rejected scan start");
  }
  return Status::Ok();
}

[[nodiscard]] std::filesystem::path test_provider_path() {
  return std::filesystem::path(KSJ_SYNCHRONOUS_FIRING_LEASE_TEST_PROVIDER_MODULE);
}

struct RequestStorage {
  TestPayloadType type{};
  std::array<std::byte, 4U> input{{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}};
  std::array<std::byte, 16U> output{};
  std::array<std::byte, 16U> output_metadata{};
  SynchronousInputItem input_item{
    .payload = input,
    .type = type.value,
    .semantic_key_hash = 1U,
    .order_key = 2U,
    .item_ordinal = 3U,
  };
  std::array<SynchronousInputItem, 1U> items{{input_item}};
  SynchronousInputBatch input_batch{.items = items, .input_port = 0U, .batch_id = 1U, .order_domain = 1U};
  std::array<SynchronousInputBatch, 1U> batches{{input_batch}};
  SynchronousOutputGrantSpec output_grant{
    .storage = output,
    .output_port = 0U,
    .maximum_item_count = 1U,
    .required_type = type.value,
  };
  std::array<SynchronousOutputGrantSpec, 1U> output_grants{{output_grant}};
};

[[nodiscard]] SynchronousFiringRequest ordinary_request(RequestStorage& storage,
                                                        const bool include_output_grant = true) {
  return {
    .resource_occurrence_id = 5U,
    .slot_generation = 2U,
    .terminal_epoch = 7U,
    .input_batches = storage.batches,
    .output_grants = include_output_grant ? std::span<const SynchronousOutputGrantSpec>{storage.output_grants}
                                          : std::span<const SynchronousOutputGrantSpec>{},
  };
}

[[nodiscard]] SynchronousFiringRequest terminal_request(RequestStorage& storage) {
  return {
    .resource_occurrence_id = 6U,
    .slot_generation = 2U,
    .terminal_epoch = 7U,
    .output_grants = storage.output_grants,
  };
}

TEST(SynchronousFiringLease, RequiresResourceVectorToCoverPersistentBoundedAbiStaging) {
  auto capacity = ResourceVectorCapacity::create({
    .domains =
      {
        .host_normal_bytes = 1U,
        .descriptor_count = 1U,
        .cpu_leaf_permits = 1U,
      },
    .host_total_cap_bytes = 1U,
  });
  ASSERT_TRUE(capacity.ok()) << capacity.status();
  auto ledger = std::make_shared<ResourceVectorLedger>(std::move(capacity).value());
  SynchronousFiringLeaseConfig config{std::move(ledger), resource_vector(1U, 1U)};
  config.maximum_input_batches = 1U;
  config.maximum_input_items = 1U;
  config.maximum_output_grants = 1U;
  config.maximum_input_payload_bytes = 16U;
  auto host = SynchronousFiringLeaseHost::create(std::move(config));
  ASSERT_FALSE(host.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, host.status().code());
}

TEST(SynchronousFiringLease, PersistentStagingConsumesSharedLedgerUntilHostDestruction) {
  // With no vector-backed views, the FiringLease descriptor itself is still
  // persistent staging. The capacity admits exactly one such host plus its
  // one-CPU dynamic firing reservation.
  auto ledger = make_ledger(0U, 1U);
  {
    SynchronousFiringLeaseConfig first_config{ledger, resource_vector(0U, 0U)};
    auto first = SynchronousFiringLeaseHost::create(std::move(first_config));
    ASSERT_TRUE(first.ok()) << first.status();
    const auto first_snapshot = ledger->snapshot();
    EXPECT_TRUE(first_snapshot.reserved.empty());
    EXPECT_EQ(1U, first_snapshot.used.descriptor_count);
    EXPECT_EQ(0U, first_snapshot.used.cpu_leaf_permits);

    SynchronousFiringLeaseConfig second_config{ledger, resource_vector(0U, 0U)};
    auto second = SynchronousFiringLeaseHost::create(std::move(second_config));
    ASSERT_FALSE(second.ok());
    EXPECT_EQ(ksj::base::StatusCode::unavailable, second.status().code());
    const auto after_rejection = ledger->snapshot();
    EXPECT_EQ(first_snapshot.reserved, after_rejection.reserved);
    EXPECT_EQ(first_snapshot.used, after_rejection.used);
  }
  expect_ledger_idle(ledger);

  {
    SynchronousFiringLeaseConfig recreated_config{ledger, resource_vector(0U, 0U)};
    auto recreated = SynchronousFiringLeaseHost::create(std::move(recreated_config));
    ASSERT_TRUE(recreated.ok()) << recreated.status();
  }
  expect_ledger_idle(ledger);
}

TEST(SynchronousFiringLease, PersistentStagingRemainsAfterDynamicFiringIsReleased) {
  ProviderInstance provider;
  ASSERT_TRUE(
    initialize_provider(provider, test_provider_path(), kNoOutputTestOperatorId, "{\"mode\":\"done-no-output\"}").ok());
  auto ledger = make_ledger(64U * 1024U, 64U);
  {
    auto host = make_host(ledger, 0U, 0U, 0U);
    const auto persistent = persistent_usage(ledger);
    EXPECT_GT(persistent.descriptor_count, 0U);
    EXPECT_EQ(0U, persistent.cpu_leaf_permits);

    RequestStorage storage;
    auto result = host.process(provider.invocation(kNoOutputTestOperatorId), ordinary_request(storage, false));
    ASSERT_TRUE(result.ok()) << result.status();
    EXPECT_EQ(SynchronousFiringOutcome::done, result.value().outcome);
    expect_only_persistent_usage(ledger, persistent);
  }
  expect_ledger_idle(ledger);
}

TEST(SynchronousFiringLease, CommitsBoundedOutputAndNormalTerminalThroughOneHostCallback) {
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"done-output\"}").ok());
  auto host = make_host(16U, 3U);
  RequestStorage storage;
  std::uint32_t commits = 0U;
  auto request = ordinary_request(storage);
  request.commit_outputs = [&commits](const std::span<const ksj::recon::runtime::SynchronousSealedOutput> outputs) {
    ++commits;
    return outputs.size() == 1U ? Status::Ok() : Status::InternalError("unexpected output count");
  };

  auto result = host.process(provider.invocation(kTestOperatorId), request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(SynchronousFiringOutcome::done, result.value().outcome);
  EXPECT_EQ(1U, result.value().consumed_input_item_count);
  EXPECT_EQ(1U, result.value().sealed_output_count);
  EXPECT_EQ(1U, result.value().committed_output_count);
  EXPECT_EQ(1U, commits);
  EXPECT_EQ(std::byte{0x5A}, storage.output.front());
  EXPECT_FALSE(host.snapshot().callback_active);
  ASSERT_TRUE(host.snapshot().high_water_reservation.has_value());
  EXPECT_EQ(16U, host.snapshot().high_water_reservation->host_normal_bytes());

  RequestStorage terminal_storage;
  std::uint32_t terminal_commits = 0U;
  auto terminal = terminal_request(terminal_storage);
  terminal.commit_outputs =
    [&terminal_commits](const std::span<const ksj::recon::runtime::SynchronousSealedOutput> outputs) {
      ++terminal_commits;
      return outputs.size() == 1U ? Status::Ok() : Status::InternalError("unexpected terminal output count");
    };
  auto terminal_result = host.on_scan_end(provider.invocation(kTestOperatorId), terminal, 1U);
  ASSERT_TRUE(terminal_result.ok()) << terminal_result.status();
  EXPECT_EQ(SynchronousFiringOutcome::done, terminal_result.value().outcome);
  EXPECT_EQ(1U, terminal_result.value().committed_output_count);
  EXPECT_EQ(1U, terminal_commits);
}

TEST(SynchronousFiringLease, PreaccountedGraphPathSupportsTwoBatchesTwoOutputsAndTerminal) {
  ProviderInstance provider;
  ASSERT_TRUE(
    initialize_provider(provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"done-two-output\"}").ok());

  // The graph has already committed its pool and ABI staging charge. The
  // shared ledger contains only the dynamic CPU firing reservation, proving
  // that the host does not recharge either of the two output pool slots.
  auto ledger = make_ledger(0U, 0U);
  SynchronousFiringLeaseConfig config{ledger, resource_vector(0U, 0U)};
  config.maximum_input_batches = 2U;
  config.maximum_input_items = 2U;
  config.maximum_output_grants = 2U;
  config.maximum_input_payload_bytes = 8U;
  config.maximum_scratch_bytes = 0U;
  config.maximum_metadata_bytes = 64U;
  config.frozen_staging_charged_bytes = 64U * 1024U;
  config.frozen_staging_descriptor_count = 32U;
  auto host = SynchronousFiringLeaseHostTestAccess::create_preaccounted(std::move(config));
  ASSERT_TRUE(host.ok()) << host.status();

  TestPayloadType type;
  std::array<std::byte, 4U> first_input{{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}};
  std::array<std::byte, 4U> second_input{{std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}}};
  std::array<SynchronousInputItem, 1U> first_items{
    {{.payload = first_input, .type = type.value, .semantic_key_hash = 11U, .order_key = 12U, .item_ordinal = 13U}}};
  std::array<SynchronousInputItem, 1U> second_items{
    {{.payload = second_input, .type = type.value, .semantic_key_hash = 11U, .order_key = 12U, .item_ordinal = 13U}}};
  std::array<SynchronousInputBatch, 2U> batches{{
    {.items = first_items, .input_port = 0U, .batch_id = 13U, .order_domain = 0U},
    {.items = second_items, .input_port = 1U, .batch_id = 13U, .order_domain = 0U},
  }};
  std::array<std::byte, 16U> first_output{};
  std::array<std::byte, 16U> second_output{};
  std::array<SynchronousOutputGrantSpec, 2U> outputs{{
    {.storage = first_output, .output_port = 0U, .maximum_item_count = 1U, .required_type = type.value},
    {.storage = second_output, .output_port = 1U, .maximum_item_count = 1U, .required_type = type.value},
  }};
  std::uint32_t commits = 0U;
  SynchronousFiringRequest request{
    .resource_occurrence_id = 1U,
    .slot_generation = 2U,
    .terminal_epoch = 7U,
    .input_batches = batches,
    .output_grants = outputs,
    .commit_outputs =
      [&commits](const std::span<const ksj::recon::runtime::SynchronousSealedOutput> sealed) {
        ++commits;
        return sealed.size() == 2U && sealed[0].output_slot == 0U && sealed[1].output_slot == 1U
                 ? Status::Ok()
                 : Status::InternalError("unexpected graph output set");
      },
  };
  auto ordinary =
    SynchronousFiringLeaseHostTestAccess::process(host.value(), provider.invocation(kTestOperatorId), request);
  ASSERT_TRUE(ordinary.ok()) << ordinary.status();
  EXPECT_EQ(SynchronousFiringOutcome::done, ordinary.value().outcome);
  EXPECT_EQ(2U, ordinary.value().consumed_input_item_count);
  EXPECT_EQ(2U, ordinary.value().committed_output_count);
  EXPECT_EQ(1U, commits);
  EXPECT_EQ(std::byte{0x5A}, first_output.front());
  EXPECT_EQ(std::byte{0x5A}, second_output.front());

  std::uint32_t terminal_commits = 0U;
  auto terminal = request;
  terminal.input_batches = {};
  terminal.commit_outputs =
    [&terminal_commits](const std::span<const ksj::recon::runtime::SynchronousSealedOutput> sealed) {
      ++terminal_commits;
      return sealed.size() == 2U ? Status::Ok() : Status::InternalError("unexpected terminal graph output set");
    };
  auto ended =
    SynchronousFiringLeaseHostTestAccess::on_scan_end(host.value(), provider.invocation(kTestOperatorId), terminal, 2U);
  ASSERT_TRUE(ended.ok()) << ended.status();
  EXPECT_EQ(SynchronousFiringOutcome::done, ended.value().outcome);
  EXPECT_EQ(2U, ended.value().committed_output_count);
  EXPECT_EQ(1U, terminal_commits);
  expect_ledger_idle(ledger);
}

TEST(SynchronousFiringLease, RejectsMappedButUnsettledGrantAndReleasesReservation) {
  ProviderInstance provider;
  ASSERT_TRUE(
    initialize_provider(provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"unsettled-output\"}").ok());
  auto ledger = make_ledger(64U * 1024U, 64U);
  auto host = make_host(ledger, 16U, 3U);
  const auto persistent = persistent_usage(ledger);
  RequestStorage storage;
  std::uint32_t commits = 0U;
  auto request = ordinary_request(storage);
  request.commit_outputs = [&commits](std::span<const ksj::recon::runtime::SynchronousSealedOutput>) {
    ++commits;
    return Status::Ok();
  };

  auto result = host.process(provider.invocation(kTestOperatorId), request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, result.value().outcome);
  EXPECT_EQ(0U, commits);
  EXPECT_FALSE(host.snapshot().callback_active);
  EXPECT_FALSE(host.snapshot().active_reservation.has_value());
  expect_only_persistent_usage(ledger, persistent);
}

TEST(SynchronousFiringLease, RejectsEveryYieldUntilTransactionalStateAndResumeExist) {
  ProviderInstance consumed_provider;
  ASSERT_TRUE(
    initialize_provider(consumed_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"yield-consumed\"}")
      .ok());
  auto host = make_host(16U, 3U);
  RequestStorage consumed_storage;
  auto consumed_request = ordinary_request(consumed_storage, false);
  auto consumed = host.process(consumed_provider.invocation(kTestOperatorId), consumed_request);
  ASSERT_TRUE(consumed.ok()) << consumed.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, consumed.value().outcome);
  EXPECT_FALSE(host.snapshot().callback_active);

  ProviderInstance sealed_provider;
  ASSERT_TRUE(
    initialize_provider(sealed_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"yield-sealed\"}").ok());
  RequestStorage sealed_storage;
  std::uint32_t commits = 0U;
  auto sealed_request = ordinary_request(sealed_storage);
  sealed_request.commit_outputs = [&commits](std::span<const ksj::recon::runtime::SynchronousSealedOutput>) {
    ++commits;
    return Status::Ok();
  };
  auto sealed = host.process(sealed_provider.invocation(kTestOperatorId), sealed_request);
  ASSERT_TRUE(sealed.ok()) << sealed.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, sealed.value().outcome);
  EXPECT_EQ(0U, commits);
  EXPECT_FALSE(host.snapshot().callback_active);

  ProviderInstance clean_provider;
  ASSERT_TRUE(
    initialize_provider(clean_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"yield-clean\"}").ok());
  RequestStorage clean_storage;
  auto clean = host.process(clean_provider.invocation(kTestOperatorId), ordinary_request(clean_storage, false));
  ASSERT_TRUE(clean.ok()) << clean.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, clean.value().outcome);
  EXPECT_FALSE(host.snapshot().callback_active);
}

TEST(SynchronousFiringLease, CommitFailureDoesNotPublishAndReleasesReservation) {
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"done-output\"}").ok());
  auto ledger = make_ledger(64U * 1024U, 64U);
  auto host = make_host(ledger, 16U, 3U);
  const auto persistent = persistent_usage(ledger);
  RequestStorage storage;
  auto request = ordinary_request(storage);
  request.commit_outputs = [](std::span<const ksj::recon::runtime::SynchronousSealedOutput>) {
    return Status::Unavailable("test sink rejected atomic commit");
  };

  auto result = host.process(provider.invocation(kTestOperatorId), request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(SynchronousFiringOutcome::structured_failure, result.value().outcome);
  EXPECT_EQ(1U, result.value().sealed_output_count);
  EXPECT_EQ(0U, result.value().committed_output_count);
  EXPECT_FALSE(host.snapshot().callback_active);
  EXPECT_FALSE(host.snapshot().active_reservation.has_value());
  expect_only_persistent_usage(ledger, persistent);
}

TEST(SynchronousFiringLease, CopiesSealMetadataIntoBoundedHostStorageBeforeCommit) {
  ProviderInstance insufficient_provider;
  ASSERT_TRUE(
    initialize_provider(insufficient_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"metadata-output\"}")
      .ok());
  auto insufficient_host = make_host(16U, 3U);
  RequestStorage insufficient_storage;
  auto insufficient_request = ordinary_request(insufficient_storage);
  insufficient_request.commit_outputs = [](std::span<const ksj::recon::runtime::SynchronousSealedOutput>) {
    return Status::Ok();
  };
  auto insufficient =
    insufficient_host.process(insufficient_provider.invocation(kTestOperatorId), insufficient_request);
  ASSERT_TRUE(insufficient.ok()) << insufficient.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, insufficient.value().outcome);

  ProviderInstance copied_provider;
  ASSERT_TRUE(
    initialize_provider(copied_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"metadata-output\"}").ok());
  auto copied_host = make_host(32U, 3U);
  RequestStorage copied_storage;
  copied_storage.output_grants.front().metadata_storage = copied_storage.output_metadata;
  auto copied_request = ordinary_request(copied_storage);
  bool saw_host_copy = false;
  copied_request.commit_outputs =
    [&copied_storage, &saw_host_copy](const std::span<const ksj::recon::runtime::SynchronousSealedOutput> outputs) {
      if (outputs.size() != 1U || outputs.front().descriptor.metadata.size != 9U ||
          outputs.front().descriptor.metadata.data != copied_storage.output_metadata.data()) {
        return Status::InternalError("metadata was not normalized into host storage");
      }
      const auto* bytes = static_cast<const char*>(outputs.front().descriptor.metadata.data);
      saw_host_copy = std::string_view(bytes, 9U) == "test-meta";
      return saw_host_copy ? Status::Ok() : Status::InternalError("metadata copy has unexpected bytes");
    };
  auto copied = copied_host.process(copied_provider.invocation(kTestOperatorId), copied_request);
  ASSERT_TRUE(copied.ok()) << copied.status();
  EXPECT_EQ(SynchronousFiringOutcome::done, copied.value().outcome);
  EXPECT_TRUE(saw_host_copy);
}

TEST(SynchronousFiringLease, RetentionAndAsyncAreNotAvailableToTheSynchronousHost) {
  ProviderInstance retention_provider;
  ASSERT_TRUE(
    initialize_provider(retention_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"retain-input\"}").ok());
  auto host = make_host(16U, 3U);
  RequestStorage retention_storage;
  auto retention =
    host.process(retention_provider.invocation(kTestOperatorId), ordinary_request(retention_storage, false));
  ASSERT_TRUE(retention.ok()) << retention.status();
  EXPECT_EQ(SynchronousFiringOutcome::structured_failure, retention.value().outcome);
  EXPECT_EQ(KSJ_STATUS_UNSUPPORTED, retention.value().provider_status);

  ProviderInstance async_provider;
  ASSERT_TRUE(
    initialize_provider(async_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"async-pending\"}").ok());
  RequestStorage async_storage;
  auto async = host.process(async_provider.invocation(kTestOperatorId), ordinary_request(async_storage, false));
  ASSERT_TRUE(async.ok()) << async.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, async.value().outcome);
  EXPECT_FALSE(host.snapshot().callback_active);
}

TEST(SynchronousFiringLease, RejectsProviderRequestingAnUndersizedAbiOutputStructure) {
  ProviderInstance provider;
  ASSERT_TRUE(
    initialize_provider(provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"undersized-info\"}").ok());
  auto host = make_host(16U, 3U);
  RequestStorage storage;
  auto result = host.process(provider.invocation(kTestOperatorId), ordinary_request(storage, false));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, result.value().outcome);
  EXPECT_FALSE(host.snapshot().callback_active);
}

TEST(SynchronousFiringLease, MapsThrownOrDirectAbiContractViolationToContractOutcome) {
  ProviderInstance throwing_provider;
  ASSERT_TRUE(
    initialize_provider(throwing_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"throws-across-abi\"}")
      .ok());
  auto ledger = make_ledger(64U * 1024U, 64U);
  auto host = make_host(ledger, 16U, 3U);
  const auto persistent = persistent_usage(ledger);
  RequestStorage throwing_storage;
  auto thrown = host.process(throwing_provider.invocation(kTestOperatorId), ordinary_request(throwing_storage, false));
  ASSERT_TRUE(thrown.ok()) << thrown.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, thrown.value().outcome);
  EXPECT_EQ(KSJ_STATUS_CONTRACT_VIOLATION, thrown.value().provider_status);
  const auto after_throw = host.snapshot();
  EXPECT_FALSE(after_throw.callback_active);
  EXPECT_FALSE(after_throw.active_reservation.has_value());
  expect_only_persistent_usage(ledger, persistent);

  ProviderInstance direct_provider;
  ASSERT_TRUE(initialize_provider(direct_provider, test_provider_path(), kTestOperatorId,
                                  "{\"mode\":\"direct-contract-violation\"}")
                .ok());
  RequestStorage direct_storage;
  auto direct = host.process(direct_provider.invocation(kTestOperatorId), ordinary_request(direct_storage, false));
  ASSERT_TRUE(direct.ok()) << direct.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, direct.value().outcome);
  EXPECT_EQ(KSJ_STATUS_CONTRACT_VIOLATION, direct.value().provider_status);
  EXPECT_FALSE(host.snapshot().callback_active);
  expect_only_persistent_usage(ledger, persistent);
}

TEST(SynchronousFiringLease, PreservesBoundedProviderFailureStatusAndMessage) {
  ProviderInstance provider;
  ASSERT_TRUE(
    initialize_provider(provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"failed-with-error\"}").ok());
  auto host = make_host(16U, 3U);
  RequestStorage storage;
  auto result = host.process(provider.invocation(kTestOperatorId), ordinary_request(storage, false));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(SynchronousFiringOutcome::structured_failure, result.value().outcome);
  EXPECT_EQ(KSJ_STATUS_FAILED_PRECONDITION, result.value().provider_status);
  EXPECT_EQ(KSJ_STATUS_FAILED_PRECONDITION, result.value().provider_failure.status);
  EXPECT_EQ("test Provider rejected ABI input", result.value().provider_failure.message());
  EXPECT_FALSE(result.value().provider_failure.message_truncated);
}

TEST(SynchronousFiringLease, RejectsConcurrentEntryWithoutMutatingTheLiveLease) {
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"done-output\"}").ok());
  auto host = make_host(16U, 3U);
  RequestStorage first_storage;
  RequestStorage second_storage;
  auto first_request = ordinary_request(first_storage);
  auto second_request = ordinary_request(second_storage);
  std::mutex mutex;
  std::condition_variable entered_condition;
  std::condition_variable release_condition;
  bool entered = false;
  bool release = false;
  first_request.commit_outputs = [&mutex, &entered_condition, &release_condition, &entered,
                                  &release](std::span<const ksj::recon::runtime::SynchronousSealedOutput>) {
    std::unique_lock lock(mutex);
    entered = true;
    entered_condition.notify_one();
    release_condition.wait(lock, [&release] {
      return release;
    });
    return Status::Ok();
  };
  second_request.commit_outputs = [](std::span<const ksj::recon::runtime::SynchronousSealedOutput>) {
    return Status::Ok();
  };

  ksj::base::Result<ksj::recon::runtime::SynchronousFiringResult> first_result =
    Status::InternalError("first callback has not run");
  std::thread first_thread([&] {
    first_result = host.process(provider.invocation(kTestOperatorId), first_request);
  });
  {
    std::unique_lock lock(mutex);
    entered_condition.wait(lock, [&entered] {
      return entered;
    });
  }
  auto concurrent = host.process(provider.invocation(kTestOperatorId), second_request);
  ASSERT_FALSE(concurrent.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, concurrent.status().code());
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  release_condition.notify_one();
  first_thread.join();
  ASSERT_TRUE(first_result.ok()) << first_result.status();
  EXPECT_EQ(SynchronousFiringOutcome::done, first_result.value().outcome);
  const auto snapshot = host.snapshot();
  EXPECT_FALSE(snapshot.callback_active);
  EXPECT_FALSE(snapshot.active_reservation.has_value());
  EXPECT_EQ(1U, snapshot.callback_count);
}

TEST(SynchronousFiringLease, SharedLedgerRejectsCompetingHostBundleAndAdmitsAfterRelease) {
  ProviderInstance first_provider;
  ASSERT_TRUE(
    initialize_provider(first_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"done-output\"}").ok());
  ProviderInstance second_provider;
  ASSERT_TRUE(
    initialize_provider(second_provider, test_provider_path(), kTestOperatorId, "{\"mode\":\"done-output\"}").ok());

  // Measure one host's fixed staging first. The target ledger then has room
  // for two persistent hosts plus exactly one dynamic output-bearing firing.
  ResourceVectorLedgerUsage one_host_persistent{};
  {
    auto probe_ledger = make_ledger(64U * 1024U, 64U);
    {
      auto probe_host = make_host(probe_ledger, 16U, 0U);
      one_host_persistent = persistent_usage(probe_ledger);
    }
    expect_ledger_idle(probe_ledger);
  }
  const auto persistent_host_bytes = 2U * one_host_persistent.host_normal_bytes;
  const auto persistent_descriptor_count = 2U * one_host_persistent.descriptor_count;
  auto ledger = make_ledger(persistent_host_bytes + 16U, persistent_descriptor_count);

  {
    auto first_host = make_host(ledger, 16U, 0U);
    auto second_host = make_host(ledger, 16U, 0U);
    const auto persistent = persistent_usage(ledger);
    EXPECT_EQ(persistent_host_bytes, persistent.host_normal_bytes);
    EXPECT_EQ(persistent_descriptor_count, persistent.descriptor_count);
    EXPECT_EQ(0U, persistent.cpu_leaf_permits);

    RequestStorage first_storage;
    RequestStorage second_storage;
    auto first_request = ordinary_request(first_storage);
    auto second_request = ordinary_request(second_storage);

    std::mutex mutex;
    std::condition_variable entered_condition;
    std::condition_variable release_condition;
    bool entered = false;
    bool release = false;
    first_request.commit_outputs = [&mutex, &entered_condition, &release_condition, &entered,
                                    &release](std::span<const ksj::recon::runtime::SynchronousSealedOutput>) {
      std::unique_lock lock(mutex);
      entered = true;
      entered_condition.notify_one();
      release_condition.wait(lock, [&release] {
        return release;
      });
      return Status::Ok();
    };
    second_request.commit_outputs = [](std::span<const ksj::recon::runtime::SynchronousSealedOutput>) {
      return Status::Ok();
    };

    ksj::base::Result<ksj::recon::runtime::SynchronousFiringResult> first_result =
      Status::InternalError("first callback has not run");
    std::thread first_thread([&] {
      first_result = first_host.process(first_provider.invocation(kTestOperatorId), first_request);
    });
    {
      std::unique_lock lock(mutex);
      entered_condition.wait(lock, [&entered] {
        return entered;
      });
    }

    const auto held = ledger->snapshot();
    EXPECT_EQ(16U, held.reserved.host_normal_bytes);
    EXPECT_EQ(0U, held.reserved.descriptor_count);
    EXPECT_EQ(1U, held.reserved.cpu_leaf_permits);
    EXPECT_EQ(persistent, held.used);
    const auto rejected = second_host.process(second_provider.invocation(kTestOperatorId), second_request);
    ASSERT_FALSE(rejected.ok());
    EXPECT_EQ(ksj::base::StatusCode::unavailable, rejected.status().code());
    EXPECT_EQ(std::byte{0U}, second_storage.output.front());
    const auto second_snapshot = second_host.snapshot();
    EXPECT_FALSE(second_snapshot.callback_active);
    EXPECT_FALSE(second_snapshot.active_reservation.has_value());
    EXPECT_FALSE(second_snapshot.high_water_reservation.has_value());
    EXPECT_EQ(0U, second_snapshot.callback_count);

    {
      std::lock_guard lock(mutex);
      release = true;
    }
    release_condition.notify_one();
    first_thread.join();
    ASSERT_TRUE(first_result.ok()) << first_result.status();
    EXPECT_EQ(SynchronousFiringOutcome::done, first_result.value().outcome);
    expect_only_persistent_usage(ledger, persistent);

    const auto accepted = second_host.process(second_provider.invocation(kTestOperatorId), second_request);
    ASSERT_TRUE(accepted.ok()) << accepted.status();
    EXPECT_EQ(SynchronousFiringOutcome::done, accepted.value().outcome);
    expect_only_persistent_usage(ledger, persistent);
    const auto peak = ledger->snapshot().high_water;
    EXPECT_EQ(persistent_host_bytes + 16U, peak.host_normal_bytes);
    EXPECT_EQ(persistent_descriptor_count, peak.descriptor_count);
    EXPECT_EQ(1U, peak.cpu_leaf_permits);
  }
  expect_ledger_idle(ledger);
}

TEST(SynchronousFiringLease, RunsZeroOutputProviderProcessAndNormalEnd) {
  ProviderInstance provider;
  ASSERT_TRUE(
    initialize_provider(provider, test_provider_path(), kNoOutputTestOperatorId, "{\"mode\":\"done-no-output\"}").ok());
  auto host = make_host(0U, 2U, 0U);
  RequestStorage storage;
  auto request = ordinary_request(storage, false);

  auto result = host.process(provider.invocation(kNoOutputTestOperatorId), request);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(SynchronousFiringOutcome::done, result.value().outcome);
  EXPECT_EQ(1U, result.value().consumed_input_item_count);
  EXPECT_EQ(0U, result.value().sealed_output_count);
  EXPECT_EQ(0U, result.value().committed_output_count);

  SynchronousFiringRequest terminal{
    .resource_occurrence_id = 8U,
    .slot_generation = 2U,
    .terminal_epoch = 7U,
  };
  auto terminal_result = host.on_scan_end(provider.invocation(kNoOutputTestOperatorId), terminal, 1U);
  ASSERT_TRUE(terminal_result.ok()) << terminal_result.status();
  EXPECT_EQ(SynchronousFiringOutcome::done, terminal_result.value().outcome);
  EXPECT_EQ(0U, terminal_result.value().sealed_output_count);
  EXPECT_EQ(0U, terminal_result.value().committed_output_count);
  EXPECT_FALSE(host.snapshot().callback_active);

  RequestStorage unsupported_terminal_storage;
  auto unsupported_terminal = terminal_request(unsupported_terminal_storage);
  unsupported_terminal.commit_outputs = [](std::span<const ksj::recon::runtime::SynchronousSealedOutput>) {
    return Status::Ok();
  };
  auto rejected = host.on_scan_end(provider.invocation(kNoOutputTestOperatorId), unsupported_terminal, 1U);
  ASSERT_FALSE(rejected.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, rejected.status().code());
}

} // namespace
