#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using ksj::recon::ResourceVector;
using ksj::recon::ResourceVectorCapacity;
using ksj::recon::ResourceVectorCapacitySpec;
using ksj::recon::ResourceVectorSpec;
using ksj::recon::runtime::ResourceVectorLedger;

[[nodiscard]] ResourceVector resource_vector(const ResourceVectorSpec& specification) {
  auto created = ResourceVector::create(specification);
  if (!created.ok()) {
    throw std::runtime_error(created.status().message());
  }
  return std::move(created).value();
}

[[nodiscard]] ResourceVectorCapacity resource_capacity(const ResourceVectorCapacitySpec& specification) {
  auto created = ResourceVectorCapacity::create(specification);
  if (!created.ok()) {
    throw std::runtime_error(created.status().message());
  }
  return std::move(created).value();
}

[[nodiscard]] ResourceVectorCapacity host_capacity(const std::uint64_t host_normal_bytes,
                                                   const std::uint64_t descriptor_count = 0U) {
  return resource_capacity({
    .domains =
      {
        .host_normal_bytes = host_normal_bytes,
        .descriptor_count = descriptor_count,
      },
    .host_total_cap_bytes = host_normal_bytes,
  });
}

TEST(KSpaceJetReconRuntimeResourceVectorLedger, FailedReserveDoesNotMutateAndRollbackReturnsEveryDomain) {
  ResourceVectorLedger ledger(host_capacity(8U, 2U));
  const auto first_amount = resource_vector({.host_normal_bytes = 6U, .descriptor_count = 1U});

  auto first = ledger.try_reserve(first_amount);
  ASSERT_TRUE(first.ok()) << first.status();
  const auto before_failure = ledger.snapshot();
  EXPECT_EQ(6U, before_failure.reserved.host_normal_bytes);
  EXPECT_EQ(1U, before_failure.reserved.descriptor_count);

  const auto failure = ledger.try_reserve(resource_vector({.host_normal_bytes = 3U, .descriptor_count = 1U}));
  EXPECT_FALSE(failure.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, failure.status().code());
  const auto after_failure = ledger.snapshot();
  EXPECT_EQ(before_failure.reserved, after_failure.reserved);
  EXPECT_EQ(before_failure.used, after_failure.used);

  ASSERT_TRUE(first.value().rollback().ok());
  EXPECT_FALSE(first.value().valid());
  const auto after_rollback = ledger.snapshot();
  EXPECT_TRUE(after_rollback.reserved.empty());
  EXPECT_TRUE(after_rollback.used.empty());
  EXPECT_EQ(6U, after_rollback.high_water.host_normal_bytes);
  EXPECT_EQ(1U, after_rollback.high_water.descriptor_count);
}

TEST(KSpaceJetReconRuntimeResourceVectorLedger, CommitMovesReservationToUsedAndReleaseReturnsIt) {
  ResourceVectorLedger ledger(host_capacity(16U, 4U));
  auto reservation = ledger.try_reserve(resource_vector({.host_normal_bytes = 8U, .descriptor_count = 2U}));
  ASSERT_TRUE(reservation.ok()) << reservation.status();

  ASSERT_TRUE(reservation.value().commit().ok());
  EXPECT_TRUE(reservation.value().committed());
  const auto committed = ledger.snapshot();
  EXPECT_TRUE(committed.reserved.empty());
  EXPECT_EQ(8U, committed.used.host_normal_bytes);
  EXPECT_EQ(2U, committed.used.descriptor_count);

  ASSERT_TRUE(reservation.value().release().ok());
  EXPECT_FALSE(reservation.value().valid());
  const auto released = ledger.snapshot();
  EXPECT_TRUE(released.reserved.empty());
  EXPECT_TRUE(released.used.empty());
  EXPECT_EQ(8U, released.high_water.host_normal_bytes);
  EXPECT_EQ(2U, released.high_water.descriptor_count);
}

TEST(KSpaceJetReconRuntimeResourceVectorLedger, RejectsDoubleAndMismatchedReservationUse) {
  const auto capacity = host_capacity(8U);
  ResourceVectorLedger first_ledger(capacity);
  ResourceVectorLedger second_ledger(capacity);
  auto reservation = first_ledger.try_reserve(resource_vector({.host_normal_bytes = 4U}));
  ASSERT_TRUE(reservation.ok()) << reservation.status();

  const auto mismatched_commit = second_ledger.commit(reservation.value());
  EXPECT_EQ(ksj::base::StatusCode::state_error, mismatched_commit.code());
  EXPECT_TRUE(reservation.value().valid());

  ASSERT_TRUE(reservation.value().commit().ok());
  EXPECT_EQ(ksj::base::StatusCode::state_error, reservation.value().commit().code());
  EXPECT_EQ(ksj::base::StatusCode::state_error, reservation.value().rollback().code());
  EXPECT_EQ(ksj::base::StatusCode::state_error, second_ledger.release(reservation.value()).code());

  ASSERT_TRUE(first_ledger.release(reservation.value()).ok());
  EXPECT_EQ(ksj::base::StatusCode::state_error, reservation.value().release().code());
  EXPECT_TRUE(first_ledger.snapshot().used.empty());
}

TEST(KSpaceJetReconRuntimeResourceVectorLedger, TracksPerDomainHighWaterAcrossReservedAndUsedAccounts) {
  const auto capacity = resource_capacity({
    .domains =
      {
        .host_normal_bytes = 10U,
        .descriptor_count = 4U,
        .devices = {{.device_id = "cuda:0", .device_bytes = 20U, .gpu_stream_slots = 2U, .copy_engine_slots = 1U}},
      },
    .host_total_cap_bytes = 10U,
  });
  ResourceVectorLedger ledger(capacity);

  auto first = ledger.try_reserve(resource_vector({
    .host_normal_bytes = 4U,
    .descriptor_count = 1U,
    .devices = {{.device_id = "cuda:0", .device_bytes = 8U, .gpu_stream_slots = 1U}},
  }));
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(first.value().commit().ok());

  auto second = ledger.try_reserve(resource_vector({
    .host_normal_bytes = 3U,
    .descriptor_count = 2U,
    .devices = {{.device_id = "cuda:0", .device_bytes = 5U}},
  }));
  ASSERT_TRUE(second.ok()) << second.status();
  const auto peak = ledger.snapshot();
  ASSERT_NE(nullptr, peak.high_water.find_device("cuda:0"));
  EXPECT_EQ(7U, peak.high_water.host_normal_bytes);
  EXPECT_EQ(3U, peak.high_water.descriptor_count);
  EXPECT_EQ(13U, peak.high_water.find_device("cuda:0")->device_bytes);
  EXPECT_EQ(1U, peak.high_water.find_device("cuda:0")->gpu_stream_slots);

  ASSERT_TRUE(second.value().commit().ok());
  ASSERT_TRUE(first.value().release().ok());
  ASSERT_TRUE(second.value().release().ok());
  const auto released = ledger.snapshot();
  EXPECT_TRUE(released.reserved.empty());
  EXPECT_TRUE(released.used.empty());
  EXPECT_EQ(7U, released.high_water.host_normal_bytes);
  EXPECT_EQ(13U, released.high_water.find_device("cuda:0")->device_bytes);
}

TEST(KSpaceJetReconRuntimeResourceVectorLedger, TwoClientsCannotExceedOneSharedBudget) {
  ResourceVectorLedger ledger(host_capacity(10U));
  auto first_client = ledger.try_reserve(resource_vector({.host_normal_bytes = 5U}));
  auto second_client = ledger.try_reserve(resource_vector({.host_normal_bytes = 5U}));
  ASSERT_TRUE(first_client.ok()) << first_client.status();
  ASSERT_TRUE(second_client.ok()) << second_client.status();

  const auto denied_client = ledger.try_reserve(resource_vector({.host_normal_bytes = 1U}));
  EXPECT_FALSE(denied_client.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, denied_client.status().code());
  EXPECT_EQ(10U, ledger.snapshot().reserved.host_normal_bytes);

  ASSERT_TRUE(first_client.value().release().ok());
  auto admitted_after_release = ledger.try_reserve(resource_vector({.host_normal_bytes = 1U}));
  ASSERT_TRUE(admitted_after_release.ok()) << admitted_after_release.status();
  ASSERT_TRUE(admitted_after_release.value().release().ok());
  ASSERT_TRUE(second_client.value().release().ok());
  EXPECT_TRUE(ledger.snapshot().reserved.empty());
}

TEST(KSpaceJetReconRuntimeResourceVectorLedger, EnforcesHostAndExactDeviceDomainsAndRejectsZeroDemand) {
  const auto capacity = resource_capacity({
    .domains =
      {
        .host_normal_bytes = 16U,
        .host_pinned_bytes = 8U,
        .host_hugepage_bytes = 4U,
        .shared_host_bytes = 4U,
        .devices =
          {
            {.device_id = "cuda:0", .device_bytes = 64U, .gpu_stream_slots = 2U, .copy_engine_slots = 1U},
            {.device_id = "cuda:1", .device_bytes = 32U, .gpu_stream_slots = 1U, .copy_engine_slots = 1U},
          },
      },
    .host_total_cap_bytes = 32U,
  });
  ResourceVectorLedger ledger(capacity);

  const auto zero = ledger.try_reserve(resource_vector({}));
  EXPECT_FALSE(zero.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, zero.status().code());

  auto host_full = ledger.try_reserve(resource_vector({
    .host_normal_bytes = 16U,
    .host_pinned_bytes = 8U,
    .host_hugepage_bytes = 4U,
    .shared_host_bytes = 4U,
  }));
  ASSERT_TRUE(host_full.ok()) << host_full.status();
  EXPECT_EQ(32U, ledger.snapshot().reserved.host_total_bytes);
  const auto host_over = ledger.try_reserve(resource_vector({.host_normal_bytes = 1U}));
  EXPECT_FALSE(host_over.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, host_over.status().code());
  ASSERT_TRUE(host_full.value().release().ok());

  auto cuda_zero = ledger.try_reserve(resource_vector({
    .devices = {{.device_id = "cuda:0", .device_bytes = 64U, .gpu_stream_slots = 2U, .copy_engine_slots = 1U}},
  }));
  ASSERT_TRUE(cuda_zero.ok()) << cuda_zero.status();
  const auto cuda_over =
    ledger.try_reserve(resource_vector({.devices = {{.device_id = "cuda:0", .device_bytes = 1U}}}));
  EXPECT_FALSE(cuda_over.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, cuda_over.status().code());
  auto cuda_one = ledger.try_reserve(resource_vector({.devices = {{.device_id = "cuda:1", .device_bytes = 1U}}}));
  ASSERT_TRUE(cuda_one.ok()) << cuda_one.status();

  const auto unknown_device =
    ledger.try_reserve(resource_vector({.devices = {{.device_id = "cuda:9", .device_bytes = 1U}}}));
  EXPECT_FALSE(unknown_device.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, unknown_device.status().code());

  const auto snapshot = ledger.snapshot();
  ASSERT_EQ(2U, snapshot.reserved.devices.size());
  ASSERT_NE(nullptr, snapshot.reserved.find_device("cuda:0"));
  ASSERT_NE(nullptr, snapshot.reserved.find_device("cuda:1"));
  EXPECT_EQ(64U, snapshot.reserved.find_device("cuda:0")->device_bytes);
  EXPECT_EQ(1U, snapshot.reserved.find_device("cuda:1")->device_bytes);
  ASSERT_TRUE(cuda_zero.value().release().ok());
  ASSERT_TRUE(cuda_one.value().release().ok());
}

TEST(KSpaceJetReconRuntimeResourceVectorLedger, DestructorRollsBackAnUncommittedReservation) {
  ResourceVectorLedger ledger(host_capacity(4U));
  {
    auto reservation = ledger.try_reserve(resource_vector({.host_normal_bytes = 4U}));
    ASSERT_TRUE(reservation.ok()) << reservation.status();
    EXPECT_EQ(4U, ledger.snapshot().reserved.host_normal_bytes);
  }
  EXPECT_TRUE(ledger.snapshot().reserved.empty());
  EXPECT_TRUE(ledger.snapshot().used.empty());
}

} // namespace
