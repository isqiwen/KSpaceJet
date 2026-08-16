#include "kspacejet/recon/runtime/calibration_artifact_store.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ksj::base::byte;
using ksj::recon::ElementType;
using ksj::recon::LayoutKind;
using ksj::recon::PayloadKind;
using ksj::recon::PayloadMutability;
using ksj::recon::Quantity;
using ksj::recon::StrideKind;
using ksj::recon::TypeDescriptor;
using ksj::recon::TypeMemoryDomain;
using ksj::recon::runtime::CalibrationArtifactBindingConfig;
using ksj::recon::runtime::CalibrationArtifactReadLease;
using ksj::recon::runtime::CalibrationArtifactStore;
using ksj::recon::runtime::CalibrationArtifactStoreConfig;
using ksj::recon::runtime::CalibrationArtifactStoreLifecycle;
using ksj::recon::runtime::FixedBufferPool;
using ksj::recon::runtime::FixedBufferPoolConfig;
using ksj::recon::runtime::FixedBufferPoolStorage;
using ksj::recon::runtime::ImmutableBufferHandle;

static_assert(!std::is_copy_constructible_v<CalibrationArtifactReadLease>);
static_assert(!std::is_copy_assignable_v<CalibrationArtifactReadLease>);
static_assert(!std::is_copy_constructible_v<CalibrationArtifactStore>);
static_assert(!std::is_copy_assignable_v<CalibrationArtifactStore>);

[[nodiscard]] TypeDescriptor make_type(const Quantity variant = 1U) {
  auto result = TypeDescriptor::create({
    .type_ref = variant == 1U ? "ksj.calibration-artifact-store-test" : "ksj.calibration-artifact-store-test-alternate",
    .payload_kind = PayloadKind::buffer_handle,
    .element_type = ElementType::uint8,
    .rank = 1U,
    .dimensions = {"coefficient"},
    .layout = LayoutKind::canonical_contiguous,
    .strides = StrideKind::canonical,
    .explicit_byte_strides = {},
    .allowed_memory_domains = {TypeMemoryDomain::host_normal},
    .min_alignment_bytes = 1U,
    .mutability = PayloadMutability::immutable_after_publish,
  });
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

[[nodiscard]] CalibrationArtifactBindingConfig make_binding(std::string binding_id, const FixedBufferPool& pool,
                                                            const TypeDescriptor& type_descriptor) {
  return {
    .binding_id = std::move(binding_id),
    .source_pool_identity = pool.pool_identity(),
    .type_descriptor = type_descriptor,
  };
}

struct PoolSlabs {
  std::vector<byte> payload;
  std::vector<byte> metadata;
  std::vector<byte> control;

  [[nodiscard]] FixedBufferPoolStorage view() {
    return {
      .payload = {payload.data(), payload.size()},
      .metadata = {metadata.data(), metadata.size()},
      .control = {control.data(), control.size()},
    };
  }
};

[[nodiscard]] PoolSlabs make_slabs(const Quantity slot_count, const Quantity payload_capacity,
                                   const Quantity metadata_capacity) {
  const auto control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(slot_count);
  EXPECT_TRUE(control.ok()) << control.status();
  return {
    .payload = std::vector<byte>(static_cast<std::size_t>(slot_count * payload_capacity)),
    .metadata = std::vector<byte>(static_cast<std::size_t>(slot_count * metadata_capacity)),
    .control = std::vector<byte>(control.value()),
  };
}

[[nodiscard]] std::unique_ptr<FixedBufferPool> create_pool(const TypeDescriptor& type_descriptor, PoolSlabs& slabs,
                                                           const Quantity slot_count, const Quantity payload_capacity,
                                                           const Quantity metadata_capacity) {
  auto result = FixedBufferPool::create({.occupancy_ledger = nullptr,
                                         .type_descriptor = type_descriptor,
                                         .slot_count = slot_count,
                                         .payload_capacity_bytes = payload_capacity,
                                         .metadata_capacity_bytes = metadata_capacity},
                                        slabs.view());
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

[[nodiscard]] ksj::base::Result<ImmutableBufferHandle> seal_artifact(FixedBufferPool& pool,
                                                                     const TypeDescriptor& type_descriptor,
                                                                     const ksj::base::ConstByteSpan payload,
                                                                     const ksj::base::ConstByteSpan metadata) {
  auto lease_result = pool.try_acquire();
  if (!lease_result.ok()) {
    return lease_result.status();
  }
  auto lease = std::move(lease_result).value();
  auto writable_result = lease.writable_payload();
  if (!writable_result.ok()) {
    return writable_result.status();
  }
  if (payload.size() > writable_result.value().size()) {
    return ksj::base::Status::InvalidArgument("test payload exceeds the fixed pool slot");
  }
  for (std::size_t index = 0U; index < payload.size(); ++index) {
    writable_result.value()[index] = payload[index];
  }
  return lease.seal(type_descriptor, static_cast<Quantity>(payload.size()), metadata);
}

TEST(KSpaceJetCalibrationArtifactStore, PublishesOneImmutableArtifactToMultipleConcurrentReaders) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 16U;
  constexpr Quantity kMetadataCapacity = 8U;
  const auto type_descriptor = make_type();
  auto slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  auto pool = create_pool(type_descriptor, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  const std::array<byte, 5U> expected_payload{byte{0x11U}, byte{0x12U}, byte{0x13U}, byte{0x14U}, byte{0x15U}};
  const std::array<byte, 3U> expected_metadata{byte{0xA1U}, byte{0xA2U}, byte{0xA3U}};
  auto artifact_result = seal_artifact(*pool, type_descriptor, {expected_payload.data(), expected_payload.size()},
                                       {expected_metadata.data(), expected_metadata.size()});
  ASSERT_TRUE(artifact_result.ok()) << artifact_result.status();
  auto artifact = std::move(artifact_result).value();

  auto store_result =
    CalibrationArtifactStore::create({.bindings = {make_binding("noise-model", *pool, type_descriptor)}});
  ASSERT_TRUE(store_result.ok()) << store_result.status();
  auto store = std::move(store_result).value();

  const auto pending = store->try_acquire("noise-model");
  EXPECT_FALSE(pending.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, pending.status().code());

  ASSERT_TRUE(store->publish("noise-model", artifact).ok());
  EXPECT_FALSE(artifact.valid());
  EXPECT_EQ(1U, store->snapshot().published_bindings);
  EXPECT_EQ(1U, store->snapshot().retained_artifacts);
  EXPECT_EQ(1U, pool->snapshot().sealed_slots);

  auto first_result = store->try_acquire("noise-model");
  ASSERT_TRUE(first_result.ok()) << first_result.status();
  auto first = std::move(first_result).value();
  auto second_result = store->try_acquire("noise-model");
  ASSERT_TRUE(second_result.ok()) << second_result.status();
  auto second = std::move(second_result).value();

  EXPECT_EQ("noise-model", first.binding_id());
  EXPECT_EQ("noise-model", second.binding_id());
  ASSERT_NE(nullptr, first.type_descriptor());
  EXPECT_TRUE(first.type_descriptor()->exactly_matches(type_descriptor));
  EXPECT_EQ(expected_payload.size(), first.payload_bytes());
  EXPECT_EQ(expected_metadata.size(), first.metadata_bytes());

  const auto first_payload = first.payload();
  const auto second_payload = second.payload();
  ASSERT_TRUE(first_payload.ok()) << first_payload.status();
  ASSERT_TRUE(second_payload.ok()) << second_payload.status();
  EXPECT_EQ(first_payload.value().data(), second_payload.value().data());
  EXPECT_EQ(expected_payload.size(), first_payload.value().size());
  EXPECT_EQ(expected_payload[0], first_payload.value()[0]);
  EXPECT_EQ(expected_payload[4], second_payload.value()[4]);
  const auto second_metadata = second.metadata();
  ASSERT_TRUE(second_metadata.ok()) << second_metadata.status();
  EXPECT_EQ(expected_metadata.size(), second_metadata.value().size());
  EXPECT_EQ(expected_metadata[0], second_metadata.value()[0]);

  first.release();
  EXPECT_FALSE(first.valid());
  EXPECT_EQ(1U, store->snapshot().active_read_leases);
  EXPECT_EQ(1U, pool->snapshot().sealed_slots);
  second.release();
  EXPECT_EQ(0U, store->snapshot().active_read_leases);
  // The store retains the sole owner until explicitly retired by abort or
  // destruction, so future consumers can still acquire this calibration.
  EXPECT_EQ(1U, pool->snapshot().sealed_slots);

  ASSERT_TRUE(store->abort().ok());
  EXPECT_EQ(CalibrationArtifactStoreLifecycle::failed, store->snapshot().lifecycle);
  EXPECT_EQ(0U, store->snapshot().retained_artifacts);
  EXPECT_EQ(kSlots, pool->snapshot().free_slots);
}

TEST(KSpaceJetCalibrationArtifactStore, RejectsDuplicateOrUnknownPublicationWithoutStealingTheCallerHandle) {
  constexpr Quantity kSlots = 2U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  const auto type_descriptor = make_type();
  auto slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  auto pool = create_pool(type_descriptor, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  const std::array<byte, 2U> first_payload{byte{0x21U}, byte{0x22U}};
  const std::array<byte, 2U> second_payload{byte{0x31U}, byte{0x32U}};
  auto first_result = seal_artifact(*pool, type_descriptor, {first_payload.data(), first_payload.size()}, {});
  auto second_result = seal_artifact(*pool, type_descriptor, {second_payload.data(), second_payload.size()}, {});
  ASSERT_TRUE(first_result.ok()) << first_result.status();
  ASSERT_TRUE(second_result.ok()) << second_result.status();
  auto first = std::move(first_result).value();
  auto second = std::move(second_result).value();

  auto store_result =
    CalibrationArtifactStore::create({.bindings = {make_binding("noise-model", *pool, type_descriptor)}});
  ASSERT_TRUE(store_result.ok()) << store_result.status();
  auto store = std::move(store_result).value();
  ASSERT_TRUE(store->publish("noise-model", first).ok());
  EXPECT_FALSE(first.valid());

  const auto duplicate = store->publish("noise-model", second);
  EXPECT_FALSE(duplicate.ok());
  EXPECT_EQ(ksj::base::StatusCode::already_exists, duplicate.code());
  EXPECT_TRUE(second.valid());
  const auto unknown = store->publish("phase-model", second);
  EXPECT_FALSE(unknown.ok());
  EXPECT_EQ(ksj::base::StatusCode::not_found, unknown.code());
  EXPECT_TRUE(second.valid());

  ASSERT_TRUE(store->abort().ok());
  EXPECT_EQ(1U, pool->snapshot().free_slots);
  second = ImmutableBufferHandle{};
  EXPECT_EQ(kSlots, pool->snapshot().free_slots);
}

TEST(KSpaceJetCalibrationArtifactStore, FreezesEachBindingsPoolIdentityAndExactType) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  const auto expected_type = make_type();
  const auto alternate_type = make_type(2U);
  auto source_slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  auto foreign_slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  auto source_pool = create_pool(expected_type, source_slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  auto foreign_pool = create_pool(expected_type, foreign_slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  const std::array<byte, 2U> payload{byte{0x35U}, byte{0x36U}};

  auto foreign_artifact_result = seal_artifact(*foreign_pool, expected_type, {payload.data(), payload.size()}, {});
  ASSERT_TRUE(foreign_artifact_result.ok()) << foreign_artifact_result.status();
  auto foreign_artifact = std::move(foreign_artifact_result).value();
  auto pool_bound_store_result =
    CalibrationArtifactStore::create({.bindings = {make_binding("noise-model", *source_pool, expected_type)}});
  ASSERT_TRUE(pool_bound_store_result.ok()) << pool_bound_store_result.status();
  auto pool_bound_store = std::move(pool_bound_store_result).value();
  const auto foreign_status = pool_bound_store->publish("noise-model", foreign_artifact);
  EXPECT_FALSE(foreign_status.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, foreign_status.code());
  EXPECT_TRUE(foreign_artifact.valid());

  auto expected_pool_artifact_result = seal_artifact(*source_pool, expected_type, {payload.data(), payload.size()}, {});
  ASSERT_TRUE(expected_pool_artifact_result.ok()) << expected_pool_artifact_result.status();
  auto expected_pool_artifact = std::move(expected_pool_artifact_result).value();
  auto type_bound_store_result =
    CalibrationArtifactStore::create({.bindings = {make_binding("noise-model", *source_pool, alternate_type)}});
  ASSERT_TRUE(type_bound_store_result.ok()) << type_bound_store_result.status();
  auto type_bound_store = std::move(type_bound_store_result).value();
  const auto type_status = type_bound_store->publish("noise-model", expected_pool_artifact);
  EXPECT_FALSE(type_status.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, type_status.code());
  EXPECT_TRUE(expected_pool_artifact.valid());

  foreign_artifact = ImmutableBufferHandle{};
  expected_pool_artifact = ImmutableBufferHandle{};
  EXPECT_EQ(kSlots, foreign_pool->snapshot().free_slots);
  EXPECT_EQ(kSlots, source_pool->snapshot().free_slots);
}

TEST(KSpaceJetCalibrationArtifactStore, EndOfInputLeavesPublishedArtifactsReadableAndMakesMissingBindingsTerminal) {
  constexpr Quantity kSlots = 2U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  const auto type_descriptor = make_type();
  auto slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  auto pool = create_pool(type_descriptor, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  const std::array<byte, 3U> payload{byte{0x41U}, byte{0x42U}, byte{0x43U}};
  auto artifact_result = seal_artifact(*pool, type_descriptor, {payload.data(), payload.size()}, {});
  ASSERT_TRUE(artifact_result.ok()) << artifact_result.status();
  auto artifact = std::move(artifact_result).value();

  auto store_result =
    CalibrationArtifactStore::create({.bindings = {make_binding("noise-model", *pool, type_descriptor),
                                                   make_binding("phase-model", *pool, type_descriptor)}});
  ASSERT_TRUE(store_result.ok()) << store_result.status();
  auto store = std::move(store_result).value();
  ASSERT_TRUE(store->publish("noise-model", artifact).ok());
  ASSERT_TRUE(store->end_of_input().ok());
  EXPECT_EQ(CalibrationArtifactStoreLifecycle::end_of_input, store->snapshot().lifecycle);
  EXPECT_EQ(1U, store->snapshot().missing_bindings);

  auto ready_result = store->try_acquire("noise-model");
  ASSERT_TRUE(ready_result.ok()) << ready_result.status();
  auto ready = std::move(ready_result).value();
  const auto ready_payload = ready.payload();
  ASSERT_TRUE(ready_payload.ok()) << ready_payload.status();
  EXPECT_EQ(payload[2], ready_payload.value()[2]);
  ready.release();

  const auto missing = store->try_acquire("phase-model");
  EXPECT_FALSE(missing.ok());
  EXPECT_EQ(ksj::base::StatusCode::state_error, missing.status().code());

  auto late_result = seal_artifact(*pool, type_descriptor, {payload.data(), payload.size()}, {});
  ASSERT_TRUE(late_result.ok()) << late_result.status();
  auto late = std::move(late_result).value();
  const auto late_publish = store->publish("phase-model", late);
  EXPECT_FALSE(late_publish.ok());
  EXPECT_EQ(ksj::base::StatusCode::state_error, late_publish.code());
  EXPECT_TRUE(late.valid());

  ASSERT_TRUE(store->abort().ok());
  late = ImmutableBufferHandle{};
  EXPECT_EQ(kSlots, pool->snapshot().free_slots);
}

TEST(KSpaceJetCalibrationArtifactStore, AbortAndOwnerDestructionKeepExistingReadLeasesSafeUntilTheySettle) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  const auto type_descriptor = make_type();
  auto slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  auto pool = create_pool(type_descriptor, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  const std::array<byte, 2U> payload{byte{0x51U}, byte{0x52U}};
  auto artifact_result = seal_artifact(*pool, type_descriptor, {payload.data(), payload.size()}, {});
  ASSERT_TRUE(artifact_result.ok()) << artifact_result.status();
  auto artifact = std::move(artifact_result).value();

  auto store_result =
    CalibrationArtifactStore::create({.bindings = {make_binding("coil-basis", *pool, type_descriptor)}});
  ASSERT_TRUE(store_result.ok()) << store_result.status();
  auto store = std::move(store_result).value();
  ASSERT_TRUE(store->publish("coil-basis", artifact).ok());
  auto lease_result = store->try_acquire("coil-basis");
  ASSERT_TRUE(lease_result.ok()) << lease_result.status();
  auto lease = std::move(lease_result).value();

  ASSERT_TRUE(store->abort().ok());
  const auto rejected = store->try_acquire("coil-basis");
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(ksj::base::StatusCode::state_error, rejected.status().code());
  const auto still_readable = lease.payload();
  ASSERT_TRUE(still_readable.ok()) << still_readable.status();
  EXPECT_EQ(payload[0], still_readable.value()[0]);
  EXPECT_EQ(1U, pool->snapshot().sealed_slots);

  // The owner can go away while a consumer is still reading. The lease keeps
  // the store state and its sole ImmutableBufferHandle alive until release.
  store.reset();
  const auto after_owner_destroyed = lease.payload();
  ASSERT_TRUE(after_owner_destroyed.ok()) << after_owner_destroyed.status();
  EXPECT_EQ(payload[1], after_owner_destroyed.value()[1]);
  EXPECT_EQ(1U, pool->snapshot().sealed_slots);
  lease.release();
  EXPECT_EQ(kSlots, pool->snapshot().free_slots);
}

TEST(KSpaceJetCalibrationArtifactStore, RejectsInvalidBindingConfiguration) {
  const auto type_descriptor = make_type();
  const auto empty = CalibrationArtifactStore::create({.bindings = {}});
  EXPECT_FALSE(empty.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, empty.status().code());

  const auto blank = CalibrationArtifactStore::create(
    {.bindings = {{.binding_id = "", .source_pool_identity = 1U, .type_descriptor = type_descriptor}}});
  EXPECT_FALSE(blank.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, blank.status().code());

  const auto duplicate = CalibrationArtifactStore::create(
    {.bindings = {{.binding_id = "noise-model", .source_pool_identity = 1U, .type_descriptor = type_descriptor},
                  {.binding_id = "noise-model", .source_pool_identity = 2U, .type_descriptor = type_descriptor}}});
  EXPECT_FALSE(duplicate.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, duplicate.status().code());

  const auto no_pool = CalibrationArtifactStore::create(
    {.bindings = {{.binding_id = "noise-model", .source_pool_identity = 0U, .type_descriptor = type_descriptor}}});
  EXPECT_FALSE(no_pool.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, no_pool.status().code());
}

} // namespace
