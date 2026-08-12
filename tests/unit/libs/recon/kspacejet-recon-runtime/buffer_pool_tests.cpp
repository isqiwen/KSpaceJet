#include "kspacejet/recon/runtime/buffer_pool.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ksj::base::byte;
using ksj::recon::PayloadKind;
using ksj::recon::PayloadMutability;
using ksj::recon::Quantity;
using ksj::recon::ResourceVectorCapacity;
using ksj::recon::ResourceVectorCapacitySpec;
using ksj::recon::ResourceVectorSpec;
using ksj::recon::TypeDescriptor;
using ksj::recon::TypeDescriptorSpec;
using ksj::recon::TypeMemoryDomain;
using ksj::recon::runtime::FixedBufferPool;
using ksj::recon::runtime::FixedBufferPoolConfig;
using ksj::recon::runtime::FixedBufferPoolStorage;
using ksj::recon::runtime::ImmutableBufferHandle;
using ksj::recon::runtime::MutableBufferLease;
using ksj::recon::runtime::ResourceVectorLedger;

constexpr auto kPayloadDigest = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr auto kMetadataDigest = "sha256:cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34";

static_assert(!std::is_copy_constructible_v<MutableBufferLease>);
static_assert(!std::is_copy_assignable_v<MutableBufferLease>);
static_assert(!std::is_copy_constructible_v<ImmutableBufferHandle>);
static_assert(!std::is_copy_assignable_v<ImmutableBufferHandle>);

[[nodiscard]] TypeDescriptor make_type(const Quantity revision = 1U) {
  const auto created = TypeDescriptor::create({
    .type_id = "ksj.fixed-buffer-pool-test",
    .revision = revision,
    .payload_schema_digest = kPayloadDigest,
    .payload_kind = PayloadKind::buffer_handle,
    .element_type = ksj::recon::ElementType::uint8,
    .rank = 1U,
    .dimensions = {"sample"},
    .layout = ksj::recon::LayoutKind::canonical_contiguous,
    .strides = ksj::recon::StrideKind::canonical,
    .explicit_byte_strides = {},
    .allowed_memory_domains = {TypeMemoryDomain::host_normal},
    .min_alignment_bytes = 1U,
    .mutability = PayloadMutability::immutable_after_publish,
    .metadata_schema_digest = kMetadataDigest,
  });
  EXPECT_TRUE(created.ok()) << created.status();
  return std::move(created).value();
}

[[nodiscard]] std::shared_ptr<ResourceVectorLedger> make_ledger(const Quantity host_normal_bytes,
                                                                const Quantity descriptor_count) {
  const auto capacity = ResourceVectorCapacity::create(ResourceVectorCapacitySpec{
    .domains =
      ResourceVectorSpec{
        .host_normal_bytes = host_normal_bytes,
        .host_pinned_bytes = 0U,
        .host_hugepage_bytes = 0U,
        .shared_host_bytes = 0U,
        .spool_bytes = 0U,
        .transport_bytes = 0U,
        .descriptor_count = descriptor_count,
        .async_token_count = 0U,
        .cpu_leaf_permits = 0U,
        .backend_gang_permits = 0U,
        .provider_private_permits = 0U,
        .io_slots = 0U,
        .devices = {},
      },
    .host_total_cap_bytes = host_normal_bytes,
  });
  EXPECT_TRUE(capacity.ok()) << capacity.status();
  return std::make_shared<ResourceVectorLedger>(capacity.value());
}

struct Slabs {
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

[[nodiscard]] Slabs make_slabs(const Quantity slot_count, const Quantity payload_capacity,
                               const Quantity metadata_capacity) {
  const auto control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(slot_count);
  EXPECT_TRUE(control.ok()) << control.status();
  return {
    .payload = std::vector<byte>(static_cast<std::size_t>(slot_count * payload_capacity)),
    .metadata = std::vector<byte>(static_cast<std::size_t>(slot_count * metadata_capacity)),
    .control = std::vector<byte>(control.value()),
  };
}

[[nodiscard]] Quantity occupancy_bytes(const Quantity slot_count, const Quantity payload_capacity,
                                       const Quantity metadata_capacity) {
  const auto control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(slot_count);
  EXPECT_TRUE(control.ok()) << control.status();
  return slot_count * payload_capacity + slot_count * metadata_capacity + control.value();
}

[[nodiscard]] ksj::base::Result<std::unique_ptr<FixedBufferPool>>
create_pool_with_storage(const TypeDescriptor& type_descriptor, const std::shared_ptr<ResourceVectorLedger>& ledger,
                         const FixedBufferPoolStorage storage, const Quantity slot_count,
                         const Quantity payload_capacity, const Quantity metadata_capacity) {
  return FixedBufferPool::create({.occupancy_ledger = ledger,
                                  .type_descriptor = type_descriptor,
                                  .slot_count = slot_count,
                                  .payload_capacity_bytes = payload_capacity,
                                  .metadata_capacity_bytes = metadata_capacity},
                                 storage);
}

[[nodiscard]] ksj::base::Result<std::unique_ptr<FixedBufferPool>>
create_pool(const TypeDescriptor& type_descriptor, const std::shared_ptr<ResourceVectorLedger>& ledger, Slabs& slabs,
            const Quantity slot_count, const Quantity payload_capacity, const Quantity metadata_capacity) {
  return create_pool_with_storage(type_descriptor, ledger, slabs.view(), slot_count, payload_capacity,
                                  metadata_capacity);
}

TEST(KSpaceJetFixedBufferPool, HoldsExactExternalSlabOccupancyCreditForItsLifetime) {
  constexpr Quantity kSlots = 2U;
  constexpr Quantity kPayloadCapacity = 16U;
  constexpr Quantity kMetadataCapacity = 8U;
  const auto type_descriptor = make_type();
  auto slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  const auto expected_occupancy_bytes = occupancy_bytes(kSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(expected_occupancy_bytes, kSlots);

  auto pool_result = create_pool(type_descriptor, ledger, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();

  const auto usage = ledger->snapshot();
  EXPECT_TRUE(usage.reserved.empty());
  EXPECT_EQ(expected_occupancy_bytes, usage.used.host_normal_bytes);
  EXPECT_EQ(kSlots, usage.used.descriptor_count);
  EXPECT_NE(0U, pool->pool_identity());
  ASSERT_NE(nullptr, pool->type_descriptor());
  EXPECT_TRUE(pool->type_descriptor()->exactly_matches(type_descriptor));

  const auto snapshot = pool->snapshot();
  EXPECT_EQ(kSlots, snapshot.slot_count);
  EXPECT_EQ(kSlots, snapshot.free_slots);
  EXPECT_EQ(0U, snapshot.writable_slots);
  EXPECT_EQ(0U, snapshot.sealed_slots);
  EXPECT_EQ(kPayloadCapacity, snapshot.payload_capacity_bytes);
  EXPECT_EQ(kMetadataCapacity, snapshot.metadata_capacity_bytes);
  EXPECT_EQ(slabs.payload.size(), snapshot.payload_storage_bytes);
  EXPECT_EQ(slabs.metadata.size(), snapshot.metadata_storage_bytes);
  EXPECT_EQ(slabs.control.size(), snapshot.control_storage_bytes);
  EXPECT_TRUE(snapshot.accepting);
  EXPECT_TRUE(snapshot.occupancy_credit_enabled);
  EXPECT_TRUE(snapshot.occupancy_credit_committed);

  pool.reset();
  const auto released = ledger->snapshot();
  EXPECT_TRUE(released.reserved.empty());
  EXPECT_TRUE(released.used.empty());
}

TEST(KSpaceJetFixedBufferPool, PermitsNoOccupancyCreditWhenOuterAdmissionOwnsTheExternalSlabs) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 4U;
  const auto type_descriptor = make_type();
  auto slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  const std::shared_ptr<ResourceVectorLedger> already_accounted_by_outer_runtime;

  auto pool_result = create_pool(type_descriptor, already_accounted_by_outer_runtime, slabs, kSlots, kPayloadCapacity,
                                 kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  const auto snapshot = pool->snapshot();
  EXPECT_FALSE(snapshot.occupancy_credit_enabled);
  EXPECT_FALSE(snapshot.occupancy_credit_committed);
}

TEST(KSpaceJetFixedBufferPool, RejectsUnrepresentableSlabCombinationWithoutOccupancyCredit) {
  const auto type_descriptor = make_type();
  const std::shared_ptr<ResourceVectorLedger> already_accounted_by_outer_runtime;

  const auto result = FixedBufferPool::create({.occupancy_ledger = already_accounted_by_outer_runtime,
                                               .type_descriptor = type_descriptor,
                                               .slot_count = 1U,
                                               .payload_capacity_bytes = std::numeric_limits<Quantity>::max(),
                                               .metadata_capacity_bytes = 1U},
                                              {});

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, result.status().code());
}

TEST(KSpaceJetFixedBufferPool, RejectsIncorrectSlabsAndInsufficientOccupancyCreditWithoutResidualAccounting) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 4U;
  const auto type_descriptor = make_type();
  const auto expected_occupancy_bytes = occupancy_bytes(kSlots, kPayloadCapacity, kMetadataCapacity);

  auto undersized_slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  undersized_slabs.payload.pop_back();
  const auto adequate_ledger = make_ledger(expected_occupancy_bytes, kSlots);
  const auto bad_storage =
    create_pool(type_descriptor, adequate_ledger, undersized_slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  EXPECT_FALSE(bad_storage.ok());
  EXPECT_TRUE(adequate_ledger->snapshot().reserved.empty());
  EXPECT_TRUE(adequate_ledger->snapshot().used.empty());

  auto exact_slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  const auto insufficient_ledger = make_ledger(expected_occupancy_bytes - 1U, kSlots);
  const auto denied =
    create_pool(type_descriptor, insufficient_ledger, exact_slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  EXPECT_FALSE(denied.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, denied.status().code());
  EXPECT_TRUE(insufficient_ledger->snapshot().reserved.empty());
  EXPECT_TRUE(insufficient_ledger->snapshot().used.empty());
}

TEST(KSpaceJetFixedBufferPool, RejectsLocalOverlapAcrossPayloadMetadataAndControlSlabs) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 4U;
  const auto type_descriptor = make_type();
  const auto control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(kSlots);
  ASSERT_TRUE(control.ok()) << control.status();
  std::vector<byte> overlapping_storage(kPayloadCapacity + control.value());
  const FixedBufferPoolStorage overlapping{
    .payload = {overlapping_storage.data(), static_cast<std::size_t>(kPayloadCapacity)},
    .metadata = {overlapping_storage.data() + 4U, static_cast<std::size_t>(kMetadataCapacity)},
    .control = {overlapping_storage.data() + 6U, control.value()},
  };
  const auto ledger = make_ledger(occupancy_bytes(kSlots, kPayloadCapacity, kMetadataCapacity), kSlots);

  const auto rejected =
    create_pool_with_storage(type_descriptor, ledger, overlapping, kSlots, kPayloadCapacity, kMetadataCapacity);
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, rejected.status().code());
  EXPECT_TRUE(ledger->snapshot().reserved.empty());
  EXPECT_TRUE(ledger->snapshot().used.empty());
}

TEST(KSpaceJetFixedBufferPool, RejectsAnotherActivePoolForPartiallyOverlappingCallerSlabs) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 4U;
  const auto type_descriptor = make_type();
  const auto control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(kSlots);
  ASSERT_TRUE(control.ok()) << control.status();
  std::vector<byte> payload_storage(12U);
  std::vector<byte> metadata_storage(8U);
  std::vector<byte> control_storage(control.value() * 2U);
  const FixedBufferPoolStorage first{
    .payload = {payload_storage.data(), static_cast<std::size_t>(kPayloadCapacity)},
    .metadata = {metadata_storage.data(), static_cast<std::size_t>(kMetadataCapacity)},
    .control = {control_storage.data(), control.value()},
  };
  const FixedBufferPoolStorage second{
    .payload = {payload_storage.data() + 4U, static_cast<std::size_t>(kPayloadCapacity)},
    .metadata = {metadata_storage.data() + 4U, static_cast<std::size_t>(kMetadataCapacity)},
    .control = {control_storage.data() + control.value(), control.value()},
  };
  const auto first_ledger = make_ledger(occupancy_bytes(kSlots, kPayloadCapacity, kMetadataCapacity), kSlots);
  const auto second_ledger = make_ledger(occupancy_bytes(kSlots, kPayloadCapacity, kMetadataCapacity), kSlots);

  auto first_pool_result =
    create_pool_with_storage(type_descriptor, first_ledger, first, kSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(first_pool_result.ok()) << first_pool_result.status();
  auto first_pool = std::move(first_pool_result).value();

  const auto rejected =
    create_pool_with_storage(type_descriptor, second_ledger, second, kSlots, kPayloadCapacity, kMetadataCapacity);
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, rejected.status().code());
  EXPECT_TRUE(second_ledger->snapshot().reserved.empty());
  EXPECT_TRUE(second_ledger->snapshot().used.empty());

  first_pool.reset();
  auto rebound =
    create_pool_with_storage(type_descriptor, second_ledger, second, kSlots, kPayloadCapacity, kMetadataCapacity);
  EXPECT_TRUE(rebound.ok()) << rebound.status();
}

TEST(KSpaceJetFixedBufferPool, SealsExactTypeAndCopiesBoundedMetadataIntoTheFixedSlab) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 16U;
  constexpr Quantity kMetadataCapacity = 8U;
  const auto type_descriptor = make_type();
  auto slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(occupancy_bytes(kSlots, kPayloadCapacity, kMetadataCapacity), kSlots);
  auto pool_result = create_pool(type_descriptor, ledger, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();

  auto lease_result = pool->try_acquire();
  ASSERT_TRUE(lease_result.ok()) << lease_result.status();
  auto lease = std::move(lease_result).value();
  const auto writable = lease.writable_payload();
  ASSERT_TRUE(writable.ok()) << writable.status();
  ASSERT_EQ(kPayloadCapacity, writable.value().size());
  writable.value()[0] = byte{0x31U};
  writable.value()[1] = byte{0x32U};
  writable.value()[2] = byte{0x33U};
  writable.value()[3] = byte{0x34U};
  writable.value()[4] = byte{0x35U};

  std::array<byte, 3U> metadata{byte{0xA1U}, byte{0xA2U}, byte{0xA3U}};
  const auto incompatible_type = make_type(2U);
  EXPECT_FALSE(lease.seal(incompatible_type, 5U, {metadata.data(), metadata.size()}).ok());
  EXPECT_TRUE(lease.valid());
  EXPECT_FALSE(lease.seal(type_descriptor, kPayloadCapacity + 1U, {metadata.data(), metadata.size()}).ok());
  std::array<byte, 9U> oversized_metadata{};
  EXPECT_FALSE(lease.seal(type_descriptor, 5U, {oversized_metadata.data(), oversized_metadata.size()}).ok());

  auto sealed = lease.seal(type_descriptor, 5U, {metadata.data(), metadata.size()});
  ASSERT_TRUE(sealed.ok()) << sealed.status();
  auto handle = std::move(sealed).value();
  EXPECT_FALSE(lease.valid());
  EXPECT_EQ(5U, handle.payload_bytes());
  EXPECT_EQ(metadata.size(), handle.metadata_bytes());
  EXPECT_EQ(5U + metadata.size(), handle.logical_bytes());
  ASSERT_NE(nullptr, handle.type_descriptor());
  EXPECT_TRUE(handle.type_descriptor()->exactly_matches(type_descriptor));

  metadata[0] = byte{0xFFU};
  const auto immutable_payload = handle.payload();
  ASSERT_TRUE(immutable_payload.ok()) << immutable_payload.status();
  ASSERT_EQ(5U, immutable_payload.value().size());
  EXPECT_EQ(byte{0x31U}, immutable_payload.value()[0]);
  EXPECT_EQ(byte{0x35U}, immutable_payload.value()[4]);
  const auto immutable_metadata = handle.metadata();
  ASSERT_TRUE(immutable_metadata.ok()) << immutable_metadata.status();
  ASSERT_EQ(3U, immutable_metadata.value().size());
  EXPECT_EQ(byte{0xA1U}, immutable_metadata.value()[0]);
  EXPECT_EQ(byte{0xA3U}, immutable_metadata.value()[2]);

  const auto snapshot = pool->snapshot();
  EXPECT_EQ(0U, snapshot.free_slots);
  EXPECT_EQ(0U, snapshot.writable_slots);
  EXPECT_EQ(1U, snapshot.sealed_slots);
}

TEST(KSpaceJetFixedBufferPool, RecyclesOnlySoleOwnersAndChangesGenerationBeforeReuse) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  const auto type_descriptor = make_type();
  auto slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(occupancy_bytes(kSlots, kPayloadCapacity, kMetadataCapacity), kSlots);
  auto pool_result = create_pool(type_descriptor, ledger, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();

  Quantity first_slot = 0U;
  std::uint64_t first_generation = 0U;
  {
    auto lease_result = pool->try_acquire();
    ASSERT_TRUE(lease_result.ok()) << lease_result.status();
    auto lease = std::move(lease_result).value();
    first_slot = lease.slot_index();
    first_generation = lease.generation();
    auto sealed = lease.seal(type_descriptor, 0U, {});
    ASSERT_TRUE(sealed.ok()) << sealed.status();
    auto handle = std::move(sealed).value();
    EXPECT_EQ(pool->pool_identity(), handle.pool_identity());
    EXPECT_EQ(first_slot, handle.slot_index());
    EXPECT_EQ(first_generation, handle.generation());

    auto moved_handle = std::move(handle);
    EXPECT_FALSE(handle.valid());
    EXPECT_FALSE(handle.payload().ok());
    EXPECT_TRUE(moved_handle.valid());
  }

  EXPECT_EQ(1U, pool->snapshot().free_slots);
  auto replacement_result = pool->try_acquire();
  ASSERT_TRUE(replacement_result.ok()) << replacement_result.status();
  auto replacement = std::move(replacement_result).value();
  EXPECT_EQ(first_slot, replacement.slot_index());
  EXPECT_NE(first_generation, replacement.generation());

  const auto held = pool->try_acquire();
  EXPECT_FALSE(held.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, held.status().code());
}

TEST(KSpaceJetFixedBufferPool, OutstandingHandleSettlesAfterOwnerDestructionBeforeOccupancyCreditIsReleased) {
  constexpr Quantity kSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 2U;
  const auto type_descriptor = make_type();
  auto slabs = make_slabs(kSlots, kPayloadCapacity, kMetadataCapacity);
  const auto expected_occupancy_bytes = occupancy_bytes(kSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(expected_occupancy_bytes, kSlots);
  const auto competing_ledger = make_ledger(expected_occupancy_bytes, kSlots);
  ImmutableBufferHandle handle;

  {
    auto pool_result = create_pool(type_descriptor, ledger, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
    ASSERT_TRUE(pool_result.ok()) << pool_result.status();
    auto pool = std::move(pool_result).value();
    auto lease_result = pool->try_acquire();
    ASSERT_TRUE(lease_result.ok()) << lease_result.status();
    auto lease = std::move(lease_result).value();
    const auto writable = lease.writable_payload();
    ASSERT_TRUE(writable.ok()) << writable.status();
    writable.value()[0] = byte{0x7FU};
    auto sealed = lease.seal(type_descriptor, 1U, {});
    ASSERT_TRUE(sealed.ok()) << sealed.status();
    handle = std::move(sealed).value();

    pool.reset();
    EXPECT_TRUE(handle.valid());
    const auto payload = handle.payload();
    ASSERT_TRUE(payload.ok()) << payload.status();
    ASSERT_EQ(1U, payload.value().size());
    EXPECT_EQ(byte{0x7FU}, payload.value()[0]);
    EXPECT_EQ(expected_occupancy_bytes, ledger->snapshot().used.host_normal_bytes);

    const auto competing =
      create_pool(type_descriptor, competing_ledger, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
    EXPECT_FALSE(competing.ok());
    EXPECT_EQ(ksj::base::StatusCode::unavailable, competing.status().code());
    EXPECT_TRUE(competing_ledger->snapshot().reserved.empty());
    EXPECT_TRUE(competing_ledger->snapshot().used.empty());
  }

  handle = ImmutableBufferHandle{};
  EXPECT_TRUE(ledger->snapshot().reserved.empty());
  EXPECT_TRUE(ledger->snapshot().used.empty());
  auto rebound = create_pool(type_descriptor, competing_ledger, slabs, kSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(rebound.ok()) << rebound.status();
}

} // namespace
