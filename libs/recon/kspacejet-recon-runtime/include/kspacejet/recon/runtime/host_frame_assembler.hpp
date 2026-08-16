#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ksj::recon::runtime {

namespace detail {
struct HostFrameAssemblerState;
} // namespace detail

class CompletedFrameLease;

// A writable capability for one FrameSlot while it assembles exactly one
// Cartesian frame. It cannot be copied or constructed by an adapter. Dropping
// an unsettled lease fails the scan rather than silently recycling incomplete
// source data.
class FrameAssemblyLease final {
public:
  FrameAssemblyLease() = default;
  ~FrameAssemblyLease();

  FrameAssemblyLease(const FrameAssemblyLease&) = delete;
  FrameAssemblyLease& operator=(const FrameAssemblyLease&) = delete;
  FrameAssemblyLease(FrameAssemblyLease&& other) noexcept;
  FrameAssemblyLease& operator=(FrameAssemblyLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] ksj::base::Status scatter(CartesianLineCoordinate coordinate, ksj::base::ConstByteSpan payload);

  // Succeeds only after the exact configured coverage set is complete. It
  // consumes this writable lease and produces the sole read-only capability
  // for the sealed slot generation.
  [[nodiscard]] ksj::base::Result<CompletedFrameLease> seal_complete();

private:
  friend class HostFrameAssembler;

  FrameAssemblyLease(std::shared_ptr<detail::HostFrameAssemblerState> state, std::size_t slot_index,
                     FrameSlotToken token, std::uint64_t lease_id) noexcept;
  void abandon_noexcept() noexcept;

  std::shared_ptr<detail::HostFrameAssemblerState> state_{};
  std::size_t slot_index_{0U};
  FrameSlotToken token_{};
  std::uint64_t lease_id_{0U};
};

// A move-only host capability for a fully sealed Cartesian FrameSlot. The
// slot data and semantic context are valid only while this lease remains
// valid. It is intentionally not a BufferHandle: it has one synchronous
// consumer, no retain/fan-out/async operation, and no cross-device transfer.
class CompletedFrameLease final {
public:
  CompletedFrameLease() = default;
  ~CompletedFrameLease();

  CompletedFrameLease(const CompletedFrameLease&) = delete;
  CompletedFrameLease& operator=(const CompletedFrameLease&) = delete;
  CompletedFrameLease(CompletedFrameLease&& other) noexcept;
  CompletedFrameLease& operator=(CompletedFrameLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;

  // The returned span is an immutable borrow of host-owned FrameSlot storage.
  // It becomes invalid when this lease is acknowledged, abandoned, or the
  // assembler is aborted.
  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> bytes() const;
  [[nodiscard]] ksj::base::Result<FrameSlotContext> context() const;
  [[nodiscard]] ksj::base::Result<FrameSlotToken> token() const;

  // Marks the sealed input as in use by its single synchronous consumer. The
  // consumer must then call acknowledge_consumed() exactly once after it has
  // stopped reading bytes, or abandon() to fail closed.
  [[nodiscard]] ksj::base::Status begin_dispatch();
  [[nodiscard]] ksj::base::Status acknowledge_consumed();
  [[nodiscard]] ksj::base::Status abandon();

private:
  friend class HostFrameAssembler;
  friend class FrameAssemblyLease;

  CompletedFrameLease(std::shared_ptr<detail::HostFrameAssemblerState> state, std::size_t slot_index,
                      FrameSlotToken token, std::uint64_t lease_id) noexcept;
  void abandon_noexcept() noexcept;

  std::shared_ptr<detail::HostFrameAssemblerState> state_{};
  std::size_t slot_index_{0U};
  FrameSlotToken token_{};
  std::uint64_t lease_id_{0U};
};

struct HostFrameAssemblerConfig {
  // Opaque scan/run identity supplied by the outer runtime. A graph ingress
  // bridge checks that a completed frame came from the assembler it owns.
  std::string scan_instance_id;

  // The caller supplies the finite, preallocated Cartesian FrameSlot pool.
  std::vector<CartesianFrameSlotConfig> frame_slots;
};

struct HostFrameAssemblerSnapshot {
  bool ingress_closed{false};
  bool failed{false};
  std::size_t free_slots{0U};
  std::size_t filling_slots{0U};
  std::size_t ready_slots{0U};
  std::size_t dispatched_slots{0U};
  std::size_t quarantined_slots{0U};
  ksj::base::Status last_error{};
};

// A scan-owned completion authority for Cartesian frames. A generic graph
// ingress bridge supplies the frozen downstream endpoint/type binding when a
// completed frame is published.
class HostFrameAssembler final {
public:
  [[nodiscard]] static ksj::base::Result<std::unique_ptr<HostFrameAssembler>> create(HostFrameAssemblerConfig config);

  HostFrameAssembler(const HostFrameAssembler&) = delete;
  HostFrameAssembler& operator=(const HostFrameAssembler&) = delete;
  HostFrameAssembler(HostFrameAssembler&&) = delete;
  HostFrameAssembler& operator=(HostFrameAssembler&&) = delete;
  ~HostFrameAssembler();

  // Acquires one free physical slot and begins exactly one semantic frame.
  // Two live slots may not carry the same complete FrameSlotContext.
  [[nodiscard]] ksj::base::Result<FrameAssemblyLease> try_begin_frame(FrameSlotContext context);

  // Stops accepting new frames. It returns Unavailable while already sealed
  // leases are still being consumed, and fails if any slot is still filling,
  // because this assembler accepts complete frames only.
  [[nodiscard]] ksj::base::Status end_of_input();

  // Exceptional terminal path. It invalidates every outstanding lease and
  // keeps its shared state alive until those move-only capabilities disappear.
  [[nodiscard]] ksj::base::Status abort();

  [[nodiscard]] HostFrameAssemblerSnapshot snapshot() const;

  // Lets a graph ingress bridge reject a completed lease issued by a different
  // scan-local assembler instance.
  [[nodiscard]] bool owns(const CompletedFrameLease& lease) const noexcept;

private:
  explicit HostFrameAssembler(std::shared_ptr<detail::HostFrameAssemblerState> state) noexcept;
  void emergency_abort_noexcept() noexcept;

  std::shared_ptr<detail::HostFrameAssemblerState> state_{};
};

} // namespace ksj::recon::runtime
