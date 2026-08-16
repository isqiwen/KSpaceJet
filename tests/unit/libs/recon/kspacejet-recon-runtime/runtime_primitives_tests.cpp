#include "kspacejet/recon/runtime/calibration_gate.hpp"
#include "kspacejet/recon/runtime/bounded_edge.hpp"
#include "kspacejet/recon/runtime/key_shard.hpp"
#include "kspacejet/recon/runtime/resource_ledger.hpp"
#include "kspacejet/recon/runtime/scan_lifecycle.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

TEST(KSpaceJetReconRuntimeResourceLedger, ReservesCommitsAndReleasesBothBudgets) {
  ksj::recon::runtime::ResourceLedger ledger({.items = 2U, .bytes = 16U});

  auto reservation = ledger.try_reserve({.items = 1U, .bytes = 8U});
  ASSERT_TRUE(reservation.ok()) << reservation.status();
  EXPECT_EQ(1U, ledger.snapshot().reserved.items);
  EXPECT_EQ(8U, ledger.snapshot().reserved.bytes);

  ASSERT_TRUE(reservation.value().commit().ok());
  EXPECT_EQ(0U, ledger.snapshot().reserved.items);
  EXPECT_EQ(1U, ledger.snapshot().used.items);
  EXPECT_EQ(8U, ledger.snapshot().used.bytes);

  const auto exhausted = ledger.try_reserve({.items = 2U, .bytes = 1U});
  EXPECT_FALSE(exhausted.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, exhausted.status().code());

  reservation.value().release();
  const auto snapshot = ledger.snapshot();
  EXPECT_EQ(0U, snapshot.reserved.items);
  EXPECT_EQ(0U, snapshot.used.items);
  EXPECT_EQ(0U, snapshot.used.bytes);
}

TEST(KSpaceJetReconRuntimeLifecycle, DrainsNormalEndOfInputPathBeforeCompletion) {
  ksj::recon::runtime::ScanLifecycle lifecycle;
  ASSERT_TRUE(lifecycle.begin_describing().ok());
  ASSERT_TRUE(lifecycle.begin_planning().ok());
  ASSERT_TRUE(lifecycle.begin_verifying().ok());
  ASSERT_TRUE(lifecycle.begin_admitting().ok());
  ASSERT_TRUE(lifecycle.admit().ok());
  ASSERT_TRUE(lifecycle.start().ok());
  ASSERT_TRUE(lifecycle.close_ingress().ok());
  ASSERT_TRUE(lifecycle.begin_draining().ok());
  ASSERT_TRUE(lifecycle.begin_finalizing().ok());
  ASSERT_TRUE(lifecycle.begin_sink_flush().ok());
  ASSERT_TRUE(lifecycle.complete().ok());
  EXPECT_TRUE(lifecycle.admitted());
  EXPECT_TRUE(lifecycle.terminal());
  EXPECT_EQ(ksj::recon::runtime::ScanState::completed, lifecycle.state());
}

TEST(KSpaceJetReconRuntimeLifecycle, FailureWinsOverCancellationDuringCleanup) {
  ksj::recon::runtime::ScanLifecycle lifecycle;
  ASSERT_TRUE(lifecycle.begin_describing().ok());
  ASSERT_TRUE(lifecycle.begin_planning().ok());
  ASSERT_TRUE(lifecycle.begin_verifying().ok());
  ASSERT_TRUE(lifecycle.begin_admitting().ok());
  ASSERT_TRUE(lifecycle.admit().ok());
  ASSERT_TRUE(lifecycle.start().ok());

  ASSERT_TRUE(lifecycle.request_cancel().ok());
  EXPECT_EQ(ksj::recon::runtime::ScanState::cancelling, lifecycle.state());
  ASSERT_TRUE(lifecycle.fail().ok());
  EXPECT_EQ(ksj::recon::runtime::ScanState::failing, lifecycle.state());
  ASSERT_TRUE(lifecycle.begin_terminal_cleanup().ok());
  ASSERT_TRUE(lifecycle.finish_terminal_cleanup().ok());
  EXPECT_EQ(ksj::recon::runtime::ScanState::failed, lifecycle.state());
  EXPECT_EQ(ksj::recon::runtime::TerminalCause::failure, lifecycle.terminal_cause());
}

TEST(KSpaceJetReconRuntimeLifecycle, CleanupDoesNotRegressWhenCancellationArrivesLate) {
  ksj::recon::runtime::ScanLifecycle lifecycle;
  ASSERT_TRUE(lifecycle.begin_describing().ok());
  ASSERT_TRUE(lifecycle.begin_planning().ok());
  ASSERT_TRUE(lifecycle.begin_verifying().ok());
  ASSERT_TRUE(lifecycle.begin_admitting().ok());
  ASSERT_TRUE(lifecycle.admit().ok());
  ASSERT_TRUE(lifecycle.start().ok());
  ASSERT_TRUE(lifecycle.fail().ok());
  ASSERT_TRUE(lifecycle.begin_terminal_cleanup().ok());

  ASSERT_TRUE(lifecycle.request_cancel().ok());
  EXPECT_EQ(ksj::recon::runtime::ScanState::terminal_cleanup, lifecycle.state());
  EXPECT_EQ(ksj::recon::runtime::TerminalCause::failure, lifecycle.terminal_cause());

  ASSERT_TRUE(lifecycle.finish_terminal_cleanup().ok());
  EXPECT_EQ(ksj::recon::runtime::ScanState::failed, lifecycle.state());
}

TEST(KSpaceJetReconRuntimeCalibrationGate, ReleasesOnlyMatchingKeyAfterCalibrationReady) {
  ksj::recon::runtime::CalibrationGate gate({
    .max_active_keys = 2U,
    .max_waiting_items_per_key = 2U,
    .max_waiting_bytes_per_key = 16U,
    .max_waiting_items_total = 3U,
    .max_waiting_bytes_total = 24U,
  });

  ASSERT_TRUE(gate.await_or_pass("slice-0", {.sequence = 7U, .charged_bytes = 8U}).ok());
  ASSERT_TRUE(gate.await_or_pass("slice-1", {.sequence = 8U, .charged_bytes = 8U}).ok());
  EXPECT_EQ(2U, gate.waiting_items());

  auto released = gate.publish_ready("slice-0", {.digest = "abc", .epoch = 0U});
  ASSERT_TRUE(released.ok()) << released.status();
  ASSERT_EQ(1U, released.value().size());
  EXPECT_EQ(7U, released.value().front().sequence);
  EXPECT_EQ(1U, gate.waiting_items());
  EXPECT_EQ(8U, gate.waiting_bytes());

  const auto token = gate.token_for("slice-0");
  ASSERT_TRUE(token.ok()) << token.status();
  EXPECT_EQ("abc", token.value().digest);
  EXPECT_EQ((std::vector<std::string>{"slice-1"}), gate.close_missing());
}

TEST(KSpaceJetReconRuntimeBoundedEdge, EndOfInputClosesOnlyAfterQueuedDataDrains) {
  ksj::recon::runtime::BoundedEdge<std::string> edge({.items = 2U, .charged_bytes = 16U});
  ASSERT_TRUE(edge.try_push("first", 8U).ok());
  ASSERT_TRUE(edge.close_input().ok());
  EXPECT_EQ(ksj::recon::runtime::EdgeState::close_pending, edge.state());
  EXPECT_FALSE(edge.try_push("late", 1U).ok());

  const auto first = edge.try_pop();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ("first", *first);
  EXPECT_EQ(ksj::recon::runtime::EdgeState::closed, edge.state());
  EXPECT_FALSE(edge.try_pop().has_value());
}

TEST(KSpaceJetReconRuntimeKeyShard, CoalescesReadinessAndPreservesSerialActivation) {
  ksj::recon::runtime::KeyShard shard;

  EXPECT_TRUE(shard.notify_ready());
  EXPECT_FALSE(shard.notify_ready());
  EXPECT_EQ(ksj::recon::runtime::KeyShardState::scheduled, shard.state());
  ASSERT_TRUE(shard.begin_activation().ok());
  EXPECT_EQ(ksj::recon::runtime::KeyShardState::running, shard.state());

  // An event racing with a callback is remembered rather than enqueued as a
  // second activation.  Completion is the sole point that requeues it.
  EXPECT_FALSE(shard.notify_ready());
  auto requeue = shard.complete_activation(ksj::recon::runtime::ActivationOutcome::blocked_input);
  ASSERT_TRUE(requeue.ok()) << requeue.status();
  EXPECT_TRUE(requeue.value());
  EXPECT_EQ(ksj::recon::runtime::KeyShardState::scheduled, shard.state());

  ASSERT_TRUE(shard.begin_activation().ok());
  shard.request_cancel();
  auto cancelled = shard.complete_activation(ksj::recon::runtime::ActivationOutcome::idle);
  ASSERT_TRUE(cancelled.ok()) << cancelled.status();
  EXPECT_FALSE(cancelled.value());
  EXPECT_EQ(ksj::recon::runtime::KeyShardState::cancelling, shard.state());
  EXPECT_TRUE(shard.cancellation_requested());
  EXPECT_FALSE(shard.notify_ready());
}

} // namespace
