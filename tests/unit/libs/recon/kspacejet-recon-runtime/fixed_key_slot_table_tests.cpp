#include "kspacejet/recon/runtime/fixed_key_slot_table.hpp"

#include <gtest/gtest.h>

#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using ksj::base::byte;
using ksj::recon::CanonicalQuantity;
using ksj::recon::DenseKeySlotDimension;
using ksj::recon::KeySlotTablePlan;
using ksj::recon::Quantity;
using ksj::recon::runtime::FixedKeySlotTable;
using ksj::recon::runtime::KeySlotToken;

struct DenseDimensionSpec {
  std::string_view field;
  Quantity minimum;
  Quantity cardinality;
};

[[nodiscard]] CanonicalQuantity canonical(const Quantity value, const std::string_view field_name) {
  auto parsed = CanonicalQuantity::create(value, field_name);
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] KeySlotTablePlan make_plan(const std::initializer_list<DenseDimensionSpec> dimension_specs,
                                         const Quantity slot_count) {
  Quantity domain = 1U;
  std::vector<DenseKeySlotDimension> dimensions;
  dimensions.reserve(dimension_specs.size());
  for (const auto& specification : dimension_specs) {
    EXPECT_NE(0U, specification.cardinality);
    domain *= specification.cardinality;
    dimensions.push_back(DenseKeySlotDimension::from_validated(std::string(specification.field),
                                                               canonical(specification.minimum, "minimum"),
                                                               canonical(specification.cardinality, "cardinality")));
  }
  EXPECT_LE(slot_count, domain);
  const auto metadata = ksj::recon::dense_key_slot_host_metadata_charged_bytes(domain, slot_count, "test storage");
  EXPECT_TRUE(metadata.ok()) << metadata.status();

  return KeySlotTablePlan::from_validated(
    "test-node", std::move(dimensions), std::string(ksj::recon::kDenseMixedRadixKeySlotMappingAlgorithmId),
    std::string(ksj::recon::kDenseKeySlotStorageAccountingId), canonical(domain, "key_domain_bound"),
    canonical(domain, "max_distinct_keys"), canonical(slot_count, "max_live_keys"), canonical(slot_count, "slot_count"),
    std::string(ksj::recon::kMonotonicU64KeySlotGenerationPolicy),
    canonical(ksj::recon::kInitialKeySlotGeneration, "initial_generation"), true,
    std::string(ksj::recon::kCompletedOnlyKeySlotEvictionPolicy), std::string(ksj::recon::kFailKeySlotLateEventPolicy),
    canonical(metadata.value(), "host_metadata_charged_bytes"), canonical(1U, "max_items_per_activation"),
    canonical(1U, "max_charged_bytes_per_activation"));
}

[[nodiscard]] ksj::base::ByteSpan bytes(std::vector<byte>& storage) {
  return {storage.data(), storage.size()};
}

TEST(KSpaceJetFixedKeySlotTable, UsesExactPlanStorageAndDenseMixedRadixIndices) {
  const auto plan = make_plan({{"encoding", 1U, 2U}, {"slice", 10U, 3U}}, 6U);
  const auto required = ksj::recon::runtime::required_storage_bytes(plan);
  ASSERT_TRUE(required.ok()) << required.status();
  EXPECT_EQ(plan.host_metadata_charged_bytes(), required.value());
  EXPECT_EQ(alignof(byte), ksj::recon::runtime::fixed_key_slot_table_storage_alignment());

  // The memcpy byte layout deliberately supports a subspan with byte-only
  // alignment. No typed object is materialised in caller-owned storage.
  std::vector<byte> storage(required.value() + 1U);
  auto table = FixedKeySlotTable::create(plan, {storage.data() + 1U, required.value()});
  ASSERT_TRUE(table.ok()) << table.status();

  const std::array<Quantity, 2U> first{1U, 10U};
  const std::array<Quantity, 2U> second{1U, 12U};
  const std::array<Quantity, 2U> third{2U, 10U};
  const std::array<Quantity, 2U> last{2U, 12U};
  EXPECT_EQ(0U, table.value().dense_index(first).value());
  EXPECT_EQ(2U, table.value().dense_index(second).value());
  EXPECT_EQ(3U, table.value().dense_index(third).value());
  EXPECT_EQ(5U, table.value().dense_index(last).value());

  const std::array<Quantity, 2U> outside{0U, 10U};
  EXPECT_FALSE(table.value().dense_index(outside).ok());
  const std::array<Quantity, 1U> wrong_arity{1U};
  EXPECT_FALSE(table.value().dense_index(wrong_arity).ok());

  auto first_token = table.value().bind_or_find(first);
  auto first_again = table.value().bind_or_find(first);
  auto second_token = table.value().bind_or_find(second);
  auto last_token = table.value().bind_or_find(last);
  ASSERT_TRUE(first_token.ok()) << first_token.status();
  ASSERT_TRUE(first_again.ok()) << first_again.status();
  ASSERT_TRUE(second_token.ok()) << second_token.status();
  ASSERT_TRUE(last_token.ok()) << last_token.status();
  EXPECT_EQ(first_token.value(), first_again.value());
  EXPECT_NE(first_token.value(), second_token.value());
  EXPECT_NE(second_token.value(), last_token.value());

  const auto snapshot = table.value().snapshot();
  EXPECT_EQ(6U, snapshot.key_domain_bound);
  EXPECT_EQ(3U, snapshot.ever_bound_keys);
  EXPECT_EQ(3U, snapshot.live_keys);
  EXPECT_EQ(3U, snapshot.free_slots);
  EXPECT_EQ(required.value(), snapshot.storage_bytes);

  std::vector<byte> too_small(required.value() - 1U);
  EXPECT_FALSE(FixedKeySlotTable::create(plan, bytes(too_small)).ok());
}

TEST(KSpaceJetFixedKeySlotTable, EnforcesCompletedOnlyEvictionAndPreservesLateEventTombstones) {
  const auto plan = make_plan({{"slice", 0U, 3U}}, 2U);
  const auto required = ksj::recon::runtime::required_storage_bytes(plan);
  ASSERT_TRUE(required.ok()) << required.status();
  std::vector<byte> storage(required.value());
  auto table = FixedKeySlotTable::create(plan, bytes(storage));
  ASSERT_TRUE(table.ok()) << table.status();

  const std::array<Quantity, 1U> key_zero{0U};
  const std::array<Quantity, 1U> key_one{1U};
  const std::array<Quantity, 1U> key_two{2U};
  auto first = table.value().bind_or_find(key_zero);
  auto second = table.value().bind_or_find(key_one);
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  const auto full = table.value().bind_or_find(key_two);
  EXPECT_FALSE(full.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, full.status().code());

  EXPECT_FALSE(table.value().evict_completed(first.value()).ok());
  ASSERT_TRUE(table.value().seal_completed(first.value()).ok());
  EXPECT_FALSE(table.value().validate_active(first.value()).ok());
  EXPECT_FALSE(table.value().bind_or_find(key_zero).ok());
  ASSERT_TRUE(table.value().evict_completed(first.value()).ok());

  // The freed physical slot is reused at a strictly newer generation. The
  // old token cannot address the new key, and the completed semantic key
  // remains a tombstone rather than silently binding again.
  auto replacement = table.value().bind_or_find(key_two);
  ASSERT_TRUE(replacement.ok()) << replacement.status();
  EXPECT_NE(first.value(), replacement.value());
  EXPECT_FALSE(table.value().validate_active(first.value()).ok());
  EXPECT_FALSE(table.value().seal_completed(first.value()).ok());
  EXPECT_FALSE(table.value().bind_or_find(key_zero).ok());
  EXPECT_TRUE(table.value().validate_active(replacement.value()).ok());

  const auto snapshot = table.value().snapshot();
  EXPECT_EQ(3U, snapshot.ever_bound_keys);
  EXPECT_EQ(2U, snapshot.live_keys);
  EXPECT_EQ(1U, snapshot.completed_tombstones);
  EXPECT_EQ(0U, snapshot.free_slots);
}

TEST(KSpaceJetFixedKeySlotTable, RejectsTokenFromAnotherTableEvenWhenSlotAndGenerationMatch) {
  const auto plan = make_plan({{"slice", 0U, 1U}}, 1U);
  const auto required = ksj::recon::runtime::required_storage_bytes(plan);
  ASSERT_TRUE(required.ok()) << required.status();
  std::vector<byte> first_storage(required.value());
  std::vector<byte> second_storage(required.value());
  auto first_table = FixedKeySlotTable::create(plan, bytes(first_storage));
  auto second_table = FixedKeySlotTable::create(plan, bytes(second_storage));
  ASSERT_TRUE(first_table.ok()) << first_table.status();
  ASSERT_TRUE(second_table.ok()) << second_table.status();

  const std::array<Quantity, 1U> key{0U};
  auto first_token = first_table.value().bind_or_find(key);
  auto second_token = second_table.value().bind_or_find(key);
  ASSERT_TRUE(first_token.ok()) << first_token.status();
  ASSERT_TRUE(second_token.ok()) << second_token.status();
  EXPECT_NE(first_token.value(), second_token.value());

  // Both tables deliberately start from physical slot zero/generation one.
  // The process-local table identity prevents table B from accepting table
  // A's opaque token as one of its own.
  EXPECT_FALSE(second_table.value().validate_active(first_token.value()).ok());
  EXPECT_FALSE(second_table.value().seal_completed(first_token.value()).ok());
  EXPECT_FALSE(second_table.value().evict_completed(first_token.value()).ok());
  EXPECT_TRUE(first_table.value().validate_active(first_token.value()).ok());
  EXPECT_TRUE(second_table.value().validate_active(second_token.value()).ok());
}

TEST(KSpaceJetFixedKeySlotTable, PreservesTokenTableIdentityAcrossMove) {
  const auto plan = make_plan({{"slice", 0U, 1U}}, 1U);
  const auto required = ksj::recon::runtime::required_storage_bytes(plan);
  ASSERT_TRUE(required.ok()) << required.status();
  std::vector<byte> storage(required.value());
  auto source = FixedKeySlotTable::create(plan, bytes(storage));
  ASSERT_TRUE(source.ok()) << source.status();

  const std::array<Quantity, 1U> key{0U};
  auto token = source.value().bind_or_find(key);
  ASSERT_TRUE(token.ok()) << token.status();
  auto destination = std::move(source).value();

  EXPECT_TRUE(destination.validate_active(token.value()).ok());
  EXPECT_FALSE(source.value().validate_active(token.value()).ok());
}

TEST(KSpaceJetFixedKeySlotTable, ClosesOnlyNewKeysAndAbortInvalidatesEveryLiveToken) {
  const auto plan = make_plan({{"slice", 0U, 2U}}, 2U);
  const auto required = ksj::recon::runtime::required_storage_bytes(plan);
  ASSERT_TRUE(required.ok()) << required.status();
  std::vector<byte> storage(required.value());
  auto table = FixedKeySlotTable::create(plan, bytes(storage));
  ASSERT_TRUE(table.ok()) << table.status();

  const std::array<Quantity, 1U> existing_key{0U};
  const std::array<Quantity, 1U> new_key{1U};
  auto existing = table.value().bind_or_find(existing_key);
  ASSERT_TRUE(existing.ok()) << existing.status();
  ASSERT_TRUE(table.value().close_new_keys().ok());
  auto existing_again = table.value().bind_or_find(existing_key);
  ASSERT_TRUE(existing_again.ok()) << existing_again.status();
  EXPECT_EQ(existing.value(), existing_again.value());
  EXPECT_FALSE(table.value().bind_or_find(new_key).ok());
  ASSERT_TRUE(table.value().seal_completed(existing.value()).ok());
  ASSERT_TRUE(table.value().evict_completed(existing.value()).ok());

  ASSERT_TRUE(table.value().abort().ok());
  EXPECT_TRUE(table.value().abort().ok());
  EXPECT_FALSE(table.value().bind_or_find(existing_key).ok());
  EXPECT_FALSE(table.value().validate_active(existing.value()).ok());
  EXPECT_FALSE(table.value().close_new_keys().ok());
  const auto snapshot = table.value().snapshot();
  EXPECT_TRUE(snapshot.new_keys_closed);
  EXPECT_TRUE(snapshot.aborted);
}

TEST(KSpaceJetFixedKeySlotTable, SerializesConcurrentBindOrFindForTheSameDenseKey) {
  constexpr std::size_t kThreadCount = 8U;
  constexpr std::size_t kIterationsPerThread = 256U;
  const auto plan = make_plan({{"encoding", 0U, 2U}, {"slice", 10U, 4U}}, 8U);
  const auto required = ksj::recon::runtime::required_storage_bytes(plan);
  ASSERT_TRUE(required.ok()) << required.status();
  std::vector<byte> storage(required.value());
  auto table = FixedKeySlotTable::create(plan, bytes(storage));
  ASSERT_TRUE(table.ok()) << table.status();

  const std::array<Quantity, 2U> shared_key{1U, 12U};
  std::array<KeySlotToken, kThreadCount> tokens{};
  std::array<bool, kThreadCount> succeeded{};
  std::barrier<> start_line(static_cast<std::ptrdiff_t>(kThreadCount + 1U));
  std::array<std::thread, kThreadCount> workers;
  for (std::size_t thread_index = 0U; thread_index < kThreadCount; ++thread_index) {
    workers[thread_index] = std::thread([&, thread_index] {
      start_line.arrive_and_wait();
      for (std::size_t iteration = 0U; iteration < kIterationsPerThread; ++iteration) {
        auto token = table.value().bind_or_find(shared_key);
        if (!token.ok()) {
          return;
        }
        tokens[thread_index] = token.value();
      }
      succeeded[thread_index] = true;
    });
  }
  start_line.arrive_and_wait();
  for (auto& worker : workers) {
    worker.join();
  }

  for (std::size_t thread_index = 0U; thread_index < kThreadCount; ++thread_index) {
    EXPECT_TRUE(succeeded[thread_index]);
    EXPECT_EQ(tokens[0], tokens[thread_index]);
  }
  EXPECT_TRUE(table.value().validate_active(tokens[0]).ok());
  const auto snapshot = table.value().snapshot();
  EXPECT_EQ(1U, snapshot.ever_bound_keys);
  EXPECT_EQ(1U, snapshot.live_keys);
  EXPECT_EQ(7U, snapshot.free_slots);
}

} // namespace
