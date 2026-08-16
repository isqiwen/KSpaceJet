#include "kspacejet/recon/runtime/fixed_buffer_edge.hpp"

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
using ksj::recon::PayloadKind;
using ksj::recon::PayloadMutability;
using ksj::recon::Quantity;
using ksj::recon::ResourceVectorCapacity;
using ksj::recon::ResourceVectorCapacitySpec;
using ksj::recon::ResourceVectorSpec;
using ksj::recon::TypeDescriptor;
using ksj::recon::TypeMemoryDomain;
using ksj::recon::runtime::DataItemIdentity;
using ksj::recon::runtime::FixedBufferEdge;
using ksj::recon::runtime::FixedBufferEdgeConfig;
using ksj::recon::runtime::FixedBufferEdgeConsumerLease;
using ksj::recon::runtime::FixedBufferEdgeLifecycle;
using ksj::recon::runtime::FixedBufferEdgePollKind;
using ksj::recon::runtime::FixedBufferEdgeProducerReservation;
using ksj::recon::runtime::FixedBufferEdgeStorage;
using ksj::recon::runtime::FixedBufferPool;
using ksj::recon::runtime::FixedBufferPoolConfig;
using ksj::recon::runtime::FixedBufferPoolStorage;
using ksj::recon::runtime::ImmutableBufferHandle;
using ksj::recon::runtime::ResourceVectorLedger;

static_assert(!std::is_copy_constructible_v<FixedBufferEdgeProducerReservation>);
static_assert(!std::is_copy_assignable_v<FixedBufferEdgeProducerReservation>);
static_assert(!std::is_copy_constructible_v<FixedBufferEdgeConsumerLease>);
static_assert(!std::is_copy_assignable_v<FixedBufferEdgeConsumerLease>);
static_assert(sizeof(DataItemIdentity) == 3U * sizeof(std::uint64_t));

[[nodiscard]] TypeDescriptor make_type(const Quantity variant = 1U) {
  const auto created = TypeDescriptor::create({
    .type_ref = variant == 1U ? "ksj.fixed-buffer-edge-test" : "ksj.fixed-buffer-edge-test-alternate",
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

struct EdgeControlSlab {
  std::size_t bytes{0U};
  std::vector<std::max_align_t> words;

  [[nodiscard]] FixedBufferEdgeStorage view() { return {.control = {reinterpret_cast<byte*>(words.data()), bytes}}; }
};

[[nodiscard]] PoolSlabs make_pool_slabs(const Quantity slot_count, const Quantity payload_capacity,
                                        const Quantity metadata_capacity) {
  const auto control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(slot_count);
  EXPECT_TRUE(control.ok()) << control.status();
  return {
    .payload = std::vector<byte>(static_cast<std::size_t>(slot_count * payload_capacity)),
    .metadata = std::vector<byte>(static_cast<std::size_t>(slot_count * metadata_capacity)),
    .control = std::vector<byte>(control.value()),
  };
}

[[nodiscard]] EdgeControlSlab make_edge_control_slab(const Quantity max_items) {
  const auto required = ksj::recon::runtime::fixed_buffer_edge_required_control_storage_bytes(max_items);
  EXPECT_TRUE(required.ok()) << required.status();
  const auto words = (required.value() + sizeof(std::max_align_t) - 1U) / sizeof(std::max_align_t);
  return {.bytes = required.value(), .words = std::vector<std::max_align_t>(words)};
}

[[nodiscard]] Quantity pool_external_slab_bytes(const Quantity slot_count, const Quantity payload_capacity,
                                                const Quantity metadata_capacity) {
  const auto control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(slot_count);
  EXPECT_TRUE(control.ok()) << control.status();
  return slot_count * payload_capacity + slot_count * metadata_capacity + control.value();
}

[[nodiscard]] ksj::base::Result<std::unique_ptr<FixedBufferPool>>
create_pool(const TypeDescriptor& type_descriptor, const std::shared_ptr<ResourceVectorLedger>& ledger,
            PoolSlabs& slabs, const Quantity slot_count, const Quantity payload_capacity,
            const Quantity metadata_capacity) {
  return FixedBufferPool::create({.occupancy_ledger = ledger,
                                  .type_descriptor = type_descriptor,
                                  .slot_count = slot_count,
                                  .payload_capacity_bytes = payload_capacity,
                                  .metadata_capacity_bytes = metadata_capacity},
                                 slabs.view());
}

[[nodiscard]] ksj::base::Result<ImmutableBufferHandle> seal_handle(FixedBufferPool& pool,
                                                                   const TypeDescriptor& type_descriptor,
                                                                   const Quantity payload_bytes,
                                                                   const ksj::base::ConstByteSpan metadata) {
  auto lease = pool.try_acquire();
  if (!lease.ok()) {
    return lease.status();
  }
  auto writable = lease.value().writable_payload();
  if (!writable.ok()) {
    return writable.status();
  }
  for (std::size_t index = 0U; index < static_cast<std::size_t>(payload_bytes); ++index) {
    writable.value()[index] = byte{static_cast<unsigned char>(index + 1U)};
  }
  return lease.value().seal(type_descriptor, payload_bytes, metadata);
}

[[nodiscard]] ksj::base::Result<std::unique_ptr<FixedBufferEdge>>
create_edge(const std::shared_ptr<ResourceVectorLedger>& ledger, const FixedBufferPool& pool, EdgeControlSlab& control,
            const Quantity max_items, const Quantity max_logical_bytes) {
  return FixedBufferEdge::create(
    {.occupancy_ledger = ledger, .source_pool = &pool, .max_items = max_items, .max_logical_bytes = max_logical_bytes},
    control.view());
}

TEST(KSpaceJetFixedBufferEdge, HoldsSeparateExternalSlabOccupancyAndNeverRechargesPoolPayload) {
  constexpr Quantity kPoolSlots = 2U;
  constexpr Quantity kPayloadCapacity = 16U;
  constexpr Quantity kMetadataCapacity = 8U;
  constexpr Quantity kEdgeItems = 2U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto pool_bytes = pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(pool_bytes + edge_control.bytes, kPoolSlots + kEdgeItems);

  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 1024U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  const auto usage = ledger->snapshot();
  EXPECT_TRUE(usage.reserved.empty());
  EXPECT_EQ(pool_bytes + edge_control.bytes, usage.used.host_normal_bytes);
  EXPECT_EQ(kPoolSlots + kEdgeItems, usage.used.descriptor_count);
  const auto snapshot = edge->snapshot();
  EXPECT_EQ(kEdgeItems, snapshot.free_slots);
  EXPECT_EQ(0U, snapshot.occupied_items);
  EXPECT_EQ(0U, snapshot.occupied_logical_bytes);
  EXPECT_TRUE(snapshot.occupancy_credit_enabled);
  EXPECT_TRUE(snapshot.occupancy_credit_committed);

  ASSERT_TRUE(edge->end_of_input().ok());
  EXPECT_EQ(FixedBufferEdgeLifecycle::completed, edge->snapshot().lifecycle);
  const auto after_close = ledger->snapshot();
  EXPECT_EQ(pool_bytes + edge_control.bytes, after_close.used.host_normal_bytes);
  EXPECT_EQ(kPoolSlots + kEdgeItems, after_close.used.descriptor_count);

  edge.reset();
  const auto after_edge_destroyed = ledger->snapshot();
  EXPECT_EQ(pool_bytes, after_edge_destroyed.used.host_normal_bytes);
  EXPECT_EQ(kPoolSlots, after_edge_destroyed.used.descriptor_count);
  pool.reset();
  EXPECT_TRUE(ledger->snapshot().used.empty());
}

TEST(KSpaceJetFixedBufferEdge, CommitTransfersMoveOnlyHandleAndAckReturnsBothCreditsAndPoolSlot) {
  constexpr Quantity kPoolSlots = 2U;
  constexpr Quantity kPayloadCapacity = 16U;
  constexpr Quantity kMetadataCapacity = 8U;
  constexpr Quantity kEdgeItems = 2U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto ledger =
    make_ledger(pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity) + edge_control.bytes,
                kPoolSlots + kEdgeItems);
  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 20U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  std::array<byte, 2U> metadata{byte{0xC1U}, byte{0xC2U}};
  auto handle_result = seal_handle(*pool, type_descriptor, 5U, {metadata.data(), metadata.size()});
  ASSERT_TRUE(handle_result.ok()) << handle_result.status();
  auto handle = std::move(handle_result).value();
  auto reservation_result = edge->try_reserve(7U);
  ASSERT_TRUE(reservation_result.ok()) << reservation_result.status();
  auto reservation = std::move(reservation_result).value();
  ASSERT_TRUE(reservation.commit_from(handle).ok());
  EXPECT_FALSE(handle.valid());
  EXPECT_FALSE(reservation.valid());
  EXPECT_EQ(1U, edge->snapshot().queued_items);
  EXPECT_EQ(7U, edge->snapshot().occupied_logical_bytes);
  EXPECT_EQ(1U, pool->snapshot().sealed_slots);

  auto poll = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, poll.kind);
  ASSERT_TRUE(poll.lease.has_value());
  auto consumer = std::move(*poll.lease);
  ASSERT_TRUE(consumer.valid());
  const auto payload = consumer.buffer().payload();
  ASSERT_TRUE(payload.ok()) << payload.status();
  ASSERT_EQ(5U, payload.value().size());
  EXPECT_EQ(byte{1U}, payload.value()[0]);
  EXPECT_EQ(byte{5U}, payload.value()[4]);
  const auto observed_metadata = consumer.buffer().metadata();
  ASSERT_TRUE(observed_metadata.ok()) << observed_metadata.status();
  ASSERT_EQ(2U, observed_metadata.value().size());
  EXPECT_EQ(byte{0xC1U}, observed_metadata.value()[0]);
  ASSERT_TRUE(consumer.acknowledge_consumed().ok());

  const auto snapshot = edge->snapshot();
  EXPECT_EQ(0U, snapshot.occupied_items);
  EXPECT_EQ(0U, snapshot.occupied_logical_bytes);
  EXPECT_EQ(kEdgeItems, snapshot.free_slots);
  EXPECT_EQ(kPoolSlots, pool->snapshot().free_slots);
}

TEST(KSpaceJetFixedBufferEdge, StandaloneCommitExposesOnlyTheDefaultDataItemIdentity) {
  constexpr Quantity kPoolSlots = 1U;
  constexpr Quantity kPayloadCapacity = 16U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 1U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto ledger =
    make_ledger(pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity) + edge_control.bytes,
                kPoolSlots + kEdgeItems);
  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 16U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  auto handle_result = seal_handle(*pool, type_descriptor, 4U, {});
  ASSERT_TRUE(handle_result.ok()) << handle_result.status();
  auto handle = std::move(handle_result).value();
  auto reservation_result = edge->try_reserve(4U);
  ASSERT_TRUE(reservation_result.ok()) << reservation_result.status();
  ASSERT_TRUE(reservation_result.value().commit_from(handle).ok());

  auto poll = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, poll.kind);
  ASSERT_TRUE(poll.lease.has_value());
  auto consumer = std::move(*poll.lease);
  EXPECT_EQ(0U, consumer.item_identity().semantic_key_hash);
  EXPECT_EQ(0U, consumer.item_identity().order_key);
  EXPECT_EQ(0U, consumer.item_identity().item_ordinal);

  auto moved_consumer = std::move(consumer);
  ASSERT_TRUE(moved_consumer.valid());
  EXPECT_EQ(0U, moved_consumer.item_identity().semantic_key_hash);
  EXPECT_EQ(0U, moved_consumer.item_identity().order_key);
  EXPECT_EQ(0U, moved_consumer.item_identity().item_ordinal);
  ASSERT_TRUE(moved_consumer.acknowledge_consumed().ok());
  EXPECT_EQ(kPoolSlots, pool->snapshot().free_slots);
}

TEST(KSpaceJetFixedBufferEdge, ConsumerAcknowledgeAdvancesTheFifoHeadExactlyOneSlot) {
  constexpr Quantity kPoolSlots = 2U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 2U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto ledger =
    make_ledger(pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity) + edge_control.bytes,
                kPoolSlots + kEdgeItems);
  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 16U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  auto first_handle_result = seal_handle(*pool, type_descriptor, 1U, {});
  ASSERT_TRUE(first_handle_result.ok()) << first_handle_result.status();
  auto first_handle = std::move(first_handle_result).value();
  auto first_reservation_result = edge->try_reserve(1U);
  ASSERT_TRUE(first_reservation_result.ok()) << first_reservation_result.status();
  ASSERT_TRUE(first_reservation_result.value().commit_from(first_handle).ok());

  auto second_handle_result = seal_handle(*pool, type_descriptor, 2U, {});
  ASSERT_TRUE(second_handle_result.ok()) << second_handle_result.status();
  auto second_handle = std::move(second_handle_result).value();
  auto second_reservation_result = edge->try_reserve(2U);
  ASSERT_TRUE(second_reservation_result.ok()) << second_reservation_result.status();
  ASSERT_TRUE(second_reservation_result.value().commit_from(second_handle).ok());

  auto first_poll = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, first_poll.kind);
  ASSERT_TRUE(first_poll.lease.has_value());
  auto first_consumer = std::move(*first_poll.lease);
  ASSERT_TRUE(first_consumer.acknowledge_consumed().ok());

  auto second_poll = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, second_poll.kind);
  ASSERT_TRUE(second_poll.lease.has_value());
  auto second_consumer = std::move(*second_poll.lease);
  const auto payload = second_consumer.buffer().payload();
  ASSERT_TRUE(payload.ok()) << payload.status();
  ASSERT_EQ(2U, payload.value().size());
  ASSERT_TRUE(second_consumer.acknowledge_consumed().ok());
  EXPECT_EQ(kEdgeItems, edge->snapshot().free_slots);
}

TEST(KSpaceJetFixedBufferEdge, HoldsCapacityUntilConsumerAcknowledgesAndRollsBackUnpublishedReservation) {
  constexpr Quantity kPoolSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 1U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto ledger =
    make_ledger(pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity) + edge_control.bytes,
                kPoolSlots + kEdgeItems);
  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 8U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  auto speculative_result = edge->try_reserve(4U);
  ASSERT_TRUE(speculative_result.ok()) << speculative_result.status();
  auto speculative = std::move(speculative_result).value();
  EXPECT_EQ(ksj::base::StatusCode::unavailable, edge->try_reserve(1U).status().code());
  ASSERT_TRUE(speculative.rollback().ok());
  EXPECT_EQ(0U, edge->snapshot().occupied_items);

  auto handle_result = seal_handle(*pool, type_descriptor, 4U, {});
  ASSERT_TRUE(handle_result.ok()) << handle_result.status();
  auto handle = std::move(handle_result).value();
  auto reservation_result = edge->try_reserve(4U);
  ASSERT_TRUE(reservation_result.ok()) << reservation_result.status();
  auto reservation = std::move(reservation_result).value();
  ASSERT_TRUE(reservation.commit_from(handle).ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, edge->try_reserve(1U).status().code());

  auto poll = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, poll.kind);
  auto consumer = std::move(*poll.lease);
  EXPECT_EQ(ksj::base::StatusCode::unavailable, edge->try_reserve(1U).status().code());
  ASSERT_TRUE(consumer.acknowledge_consumed().ok());
  auto after_ack = edge->try_reserve(1U);
  ASSERT_TRUE(after_ack.ok()) << after_ack.status();
  EXPECT_TRUE(std::move(after_ack).value().rollback().ok());
}

TEST(KSpaceJetFixedBufferEdge, DetachedPreReservedCreditsEnterFifoInCommitOrder) {
  constexpr Quantity kPoolSlots = 2U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 2U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto ledger =
    make_ledger(pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity) + edge_control.bytes,
                kPoolSlots + kEdgeItems);
  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 16U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  // These credits are acquired before either Provider callback. They consume
  // capacity but intentionally have no FIFO position yet.
  auto first_credit_result = edge->try_reserve(1U);
  ASSERT_TRUE(first_credit_result.ok()) << first_credit_result.status();
  auto first_credit = std::move(first_credit_result).value();
  auto second_credit_result = edge->try_reserve(3U);
  ASSERT_TRUE(second_credit_result.ok()) << second_credit_result.status();
  auto second_credit = std::move(second_credit_result).value();
  const auto pending = edge->snapshot();
  EXPECT_EQ(2U, pending.reserved_items);
  EXPECT_EQ(0U, pending.queued_items);
  EXPECT_EQ(2U, pending.occupied_items);
  EXPECT_EQ(FixedBufferEdgePollKind::empty, edge->try_acquire().kind);

  auto first_handle_result = seal_handle(*pool, type_descriptor, 1U, {});
  ASSERT_TRUE(first_handle_result.ok()) << first_handle_result.status();
  auto first_handle = std::move(first_handle_result).value();
  auto second_handle_result = seal_handle(*pool, type_descriptor, 3U, {});
  ASSERT_TRUE(second_handle_result.ok()) << second_handle_result.status();
  auto second_handle = std::move(second_handle_result).value();

  // A reorder stage can publish the later-acquired credit first. FIFO order
  // follows this ordered commit sequence, not the earlier callback/credit
  // acquisition order.
  ASSERT_TRUE(second_credit.commit_from(second_handle).ok());
  ASSERT_TRUE(first_credit.commit_from(first_handle).ok());
  const auto queued = edge->snapshot();
  EXPECT_EQ(0U, queued.reserved_items);
  EXPECT_EQ(2U, queued.queued_items);
  EXPECT_EQ(2U, queued.occupied_items);

  auto first_poll = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, first_poll.kind);
  auto first_consumer = std::move(*first_poll.lease);
  const auto first_payload = first_consumer.buffer().payload();
  ASSERT_TRUE(first_payload.ok()) << first_payload.status();
  EXPECT_EQ(3U, first_payload.value().size());
  ASSERT_TRUE(first_consumer.acknowledge_consumed().ok());

  auto second_poll = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, second_poll.kind);
  auto second_consumer = std::move(*second_poll.lease);
  const auto second_payload = second_consumer.buffer().payload();
  ASSERT_TRUE(second_payload.ok()) << second_payload.status();
  EXPECT_EQ(1U, second_payload.value().size());
  ASSERT_TRUE(second_consumer.acknowledge_consumed().ok());
  EXPECT_EQ(kEdgeItems, edge->snapshot().free_slots);
}

TEST(KSpaceJetFixedBufferEdge, EndOfInputDrainsPreexistingReservationWhileExternalOccupancyPersists) {
  constexpr Quantity kPoolSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 1U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto pool_bytes = pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(pool_bytes + edge_control.bytes, kPoolSlots + kEdgeItems);
  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 8U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  auto reservation_result = edge->try_reserve(3U);
  ASSERT_TRUE(reservation_result.ok()) << reservation_result.status();
  auto reservation = std::move(reservation_result).value();
  ASSERT_TRUE(edge->end_of_input().ok());
  EXPECT_EQ(FixedBufferEdgeLifecycle::close_pending, edge->snapshot().lifecycle);
  EXPECT_EQ(ksj::base::StatusCode::state_error, edge->try_reserve(1U).status().code());

  auto handle_result = seal_handle(*pool, type_descriptor, 3U, {});
  ASSERT_TRUE(handle_result.ok()) << handle_result.status();
  auto handle = std::move(handle_result).value();
  ASSERT_TRUE(reservation.commit_from(handle).ok());
  auto poll = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, poll.kind);
  auto consumer = std::move(*poll.lease);
  ASSERT_TRUE(consumer.acknowledge_consumed().ok());
  EXPECT_EQ(FixedBufferEdgeLifecycle::completed, edge->snapshot().lifecycle);
  EXPECT_EQ(FixedBufferEdgePollKind::completed, edge->try_acquire().kind);
  const auto usage = ledger->snapshot();
  EXPECT_EQ(pool_bytes + edge_control.bytes, usage.used.host_normal_bytes);
  EXPECT_EQ(kPoolSlots + kEdgeItems, usage.used.descriptor_count);
}

TEST(KSpaceJetFixedBufferEdge, DroppedConsumerFailsClosedAndReturnsTheSolePoolHandle) {
  constexpr Quantity kPoolSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 1U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto pool_bytes = pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(pool_bytes + edge_control.bytes, kPoolSlots + kEdgeItems);
  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 8U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  auto handle_result = seal_handle(*pool, type_descriptor, 3U, {});
  ASSERT_TRUE(handle_result.ok()) << handle_result.status();
  auto handle = std::move(handle_result).value();
  auto reservation_result = edge->try_reserve(3U);
  ASSERT_TRUE(reservation_result.ok()) << reservation_result.status();
  ASSERT_TRUE(reservation_result.value().commit_from(handle).ok());
  {
    auto poll = edge->try_acquire();
    ASSERT_EQ(FixedBufferEdgePollKind::item, poll.kind);
    ASSERT_TRUE(poll.lease.has_value());
  }

  EXPECT_EQ(FixedBufferEdgeLifecycle::failed, edge->snapshot().lifecycle);
  EXPECT_EQ(kPoolSlots, pool->snapshot().free_slots);
  const auto usage = ledger->snapshot();
  EXPECT_EQ(pool_bytes + edge_control.bytes, usage.used.host_normal_bytes);
  EXPECT_EQ(kPoolSlots + kEdgeItems, usage.used.descriptor_count);
  EXPECT_FALSE(edge->try_reserve(1U).ok());
}

TEST(KSpaceJetFixedBufferEdge, ForeignPoolHandleFailsClosedWithoutStealingCallerOwnership) {
  constexpr Quantity kPoolSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 1U;
  const auto type_descriptor = make_type();
  auto first_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto second_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto pool_bytes = pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(pool_bytes * 2U + edge_control.bytes, kPoolSlots * 2U + kEdgeItems);
  auto first_result =
    create_pool(type_descriptor, ledger, first_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(first_result.ok()) << first_result.status();
  auto first_pool = std::move(first_result).value();
  auto second_result =
    create_pool(type_descriptor, ledger, second_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(second_result.ok()) << second_result.status();
  auto second_pool = std::move(second_result).value();
  auto edge_result = create_edge(ledger, *first_pool, edge_control, kEdgeItems, 8U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  auto foreign_result = seal_handle(*second_pool, type_descriptor, 3U, {});
  ASSERT_TRUE(foreign_result.ok()) << foreign_result.status();
  auto foreign = std::move(foreign_result).value();
  auto reservation_result = edge->try_reserve(3U);
  ASSERT_TRUE(reservation_result.ok()) << reservation_result.status();
  auto reservation = std::move(reservation_result).value();
  EXPECT_FALSE(reservation.commit_from(foreign).ok());
  EXPECT_TRUE(foreign.valid());
  ASSERT_TRUE(reservation.rollback().ok());
  EXPECT_EQ(FixedBufferEdgeLifecycle::failed, edge->snapshot().lifecycle);
  foreign = ImmutableBufferHandle{};
  EXPECT_EQ(kPoolSlots, second_pool->snapshot().free_slots);
}

TEST(KSpaceJetFixedBufferEdge, AbortDestroysUnexposedQueuedHandleAndDrainsToFailed) {
  constexpr Quantity kPoolSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 1U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto pool_bytes = pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(pool_bytes + edge_control.bytes, kPoolSlots + kEdgeItems);
  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 8U);
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  auto handle_result = seal_handle(*pool, type_descriptor, 3U, {});
  ASSERT_TRUE(handle_result.ok()) << handle_result.status();
  auto handle = std::move(handle_result).value();
  auto reservation_result = edge->try_reserve(3U);
  ASSERT_TRUE(reservation_result.ok()) << reservation_result.status();
  ASSERT_TRUE(reservation_result.value().commit_from(handle).ok());
  ASSERT_TRUE(edge->abort().ok());
  EXPECT_EQ(FixedBufferEdgeLifecycle::failed, edge->snapshot().lifecycle);
  EXPECT_EQ(kPoolSlots, pool->snapshot().free_slots);
  const auto usage = ledger->snapshot();
  EXPECT_EQ(pool_bytes + edge_control.bytes, usage.used.host_normal_bytes);
  EXPECT_EQ(kPoolSlots + kEdgeItems, usage.used.descriptor_count);
}

TEST(KSpaceJetFixedBufferEdge, SlabClaimPersistsPastCompletedUntilTheEdgeOwnerIsReleased) {
  constexpr Quantity kPoolSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 1U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  const auto pool_bytes = pool_external_slab_bytes(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  const auto ledger = make_ledger(pool_bytes + edge_control.bytes, kPoolSlots + kEdgeItems);
  auto pool_result = create_pool(type_descriptor, ledger, pool_slabs, kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto first_edge_result = create_edge(ledger, *pool, edge_control, kEdgeItems, 8U);
  ASSERT_TRUE(first_edge_result.ok()) << first_edge_result.status();
  auto first_edge = std::move(first_edge_result).value();

  ASSERT_TRUE(first_edge->end_of_input().ok());
  auto overlapping = create_edge(ledger, *pool, edge_control, kEdgeItems, 8U);
  ASSERT_FALSE(overlapping.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, overlapping.status().code());

  first_edge.reset();
  auto replacement = create_edge(ledger, *pool, edge_control, kEdgeItems, 8U);
  ASSERT_TRUE(replacement.ok()) << replacement.status();
  ASSERT_TRUE(replacement.value()->end_of_input().ok());
}

TEST(KSpaceJetFixedBufferEdge, AllowsUnledgeredExternalSlabWhenOuterRuntimeOwnsAccounting) {
  constexpr Quantity kPoolSlots = 1U;
  constexpr Quantity kPayloadCapacity = 8U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kEdgeItems = 1U;
  const auto type_descriptor = make_type();
  auto pool_slabs = make_pool_slabs(kPoolSlots, kPayloadCapacity, kMetadataCapacity);
  auto edge_control = make_edge_control_slab(kEdgeItems);
  auto pool_result = FixedBufferPool::create({.occupancy_ledger = nullptr,
                                              .type_descriptor = type_descriptor,
                                              .slot_count = kPoolSlots,
                                              .payload_capacity_bytes = kPayloadCapacity,
                                              .metadata_capacity_bytes = kMetadataCapacity},
                                             pool_slabs.view());
  ASSERT_TRUE(pool_result.ok()) << pool_result.status();
  auto pool = std::move(pool_result).value();
  auto edge_result = FixedBufferEdge::create(
    {.occupancy_ledger = nullptr, .source_pool = pool.get(), .max_items = kEdgeItems, .max_logical_bytes = 8U},
    edge_control.view());
  ASSERT_TRUE(edge_result.ok()) << edge_result.status();
  auto edge = std::move(edge_result).value();

  const auto snapshot = edge->snapshot();
  EXPECT_FALSE(snapshot.occupancy_credit_enabled);
  EXPECT_FALSE(snapshot.occupancy_credit_committed);
  ASSERT_TRUE(edge->end_of_input().ok());
}

} // namespace
