#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/runtime/buffer_pool.hpp"
#include "kspacejet/recon/runtime/calibration_artifact_store.hpp"
#include "kspacejet/recon/runtime/fixed_buffer_edge.hpp"
#include "kspacejet/recon/runtime/synchronous_firing_lease.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::recon::runtime {

// Every graph-owned Provider scratch slab has this minimum alignment. A
// frozen Provider ABI currently has no stronger scratch-alignment declaration;
// any future stronger requirement must be made explicit in the plan before it
// can be admitted.
inline constexpr std::size_t kSynchronousGraphScratchMinimumAlignment = 64U;

class CompletedFrameLease;
class HostFrameAssembler;

namespace detail {
struct SynchronousGraphExecutorState;
struct SynchronousGraphNodeRuntime;
} // namespace detail

// The raw slabs remain owned by the caller. Every id is an exact frozen plan
// id: create() rejects duplicates, omissions, and surplus slabs rather than
// inferring a storage mapping from allocation order.
struct SynchronousGraphBufferPoolStorage {
  std::string pool_id;
  FixedBufferPoolStorage storage{};
};

struct SynchronousGraphDataEdgeStorage {
  std::string edge_id;
  FixedBufferEdgeStorage storage{};
};

// One fixed, caller-owned scratch slab for a frozen node. It is persistent
// graph storage, not a per-call convenience span: create() requires exactly
// one entry per node and charges its bytes for the executor lifetime.
struct SynchronousGraphNodeScratchStorage {
  std::string node_id;
  ksj::base::ByteSpan storage{};
};

struct SynchronousGraphExecutorStorage {
  std::vector<SynchronousGraphBufferPoolStorage> buffer_pools;
  std::vector<SynchronousGraphDataEdgeStorage> data_edges;
  std::vector<SynchronousGraphNodeScratchStorage> node_scratch;
};

// These values identify one runtime invocation occurrence. They are supplied
// by the scan driver and are passed unmodified to the Provider ABI.
struct SynchronousGraphNodeInvocation {
  SynchronousProviderInvocation provider_invocation{};
  std::uint64_t resource_occurrence_id{0U};
  std::uint64_t slot_generation{0U};
  std::uint64_t terminal_epoch{0U};
};

enum class SynchronousGraphExecutorLifecycle : std::uint8_t {
  accepting,
  completed,
  failed,
};

struct SynchronousGraphExecutorSnapshot {
  SynchronousGraphExecutorLifecycle lifecycle{SynchronousGraphExecutorLifecycle::accepting};
  CalibrationArtifactStoreLifecycle calibration_artifact_lifecycle{CalibrationArtifactStoreLifecycle::accepting};
  Quantity configured_nodes{0U};
  Quantity completed_nodes{0U};
  Quantity configured_ingresses{0U};
  Quantity closed_ingresses{0U};
  Quantity published_calibration_artifacts{0U};
  Quantity missing_calibration_artifacts{0U};
  ksj::base::Status last_error{};
};

class SynchronousGraphExecutor;

// One pre-reserved graph ingress output. The lease retains both the fixed pool
// slot and the edge credit until seal_and_commit succeeds. A rejected seal
// leaves the mutable pool lease valid, so the caller retains its writable
// output capability and can correct/retry. A successful seal followed by an
// edge failure is terminal: the executor fail-closes and the sealed handle is
// released instead of being published partially.
class IngressOutputLease final {
public:
  IngressOutputLease() = default;
  ~IngressOutputLease();

  IngressOutputLease(const IngressOutputLease&) = delete;
  IngressOutputLease& operator=(const IngressOutputLease&) = delete;
  IngressOutputLease(IngressOutputLease&& other) noexcept;
  IngressOutputLease& operator=(IngressOutputLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] ksj::base::Result<ksj::base::ByteSpan> writable_payload();
  [[nodiscard]] ksj::base::Result<ksj::base::ByteSpan> writable_metadata();

  // Uses caller-owned metadata bytes. Failed sealing retains this lease.
  [[nodiscard]] ksj::base::Status seal_and_commit(Quantity payload_bytes, ksj::base::ConstByteSpan metadata,
                                                  DataItemIdentity identity);
  // Uses the prefix already written through writable_metadata().
  [[nodiscard]] ksj::base::Status seal_and_commit(Quantity payload_bytes, Quantity metadata_bytes,
                                                  DataItemIdentity identity);

  // Explicitly discards the unpublished output. Destruction does the same.
  void abort() noexcept;

private:
  friend class SynchronousGraphExecutor;
  friend class CompletedFrameIngressBridge;

  IngressOutputLease(std::shared_ptr<detail::SynchronousGraphExecutorState> state, std::string ingress_id,
                     const TypeDescriptor* type_descriptor, MutableBufferLease mutable_buffer,
                     FixedBufferEdgeProducerReservation edge_reservation) noexcept;

  void disarm() noexcept;

  std::shared_ptr<detail::SynchronousGraphExecutorState> state_{};
  std::string ingress_id_;
  const TypeDescriptor* type_descriptor_{nullptr};
  MutableBufferLease mutable_buffer_{};
  FixedBufferEdgeProducerReservation edge_reservation_{};
};

// A read-only egress item. The edge/pool capacity returns only after explicit
// acknowledgement, so a sink cannot accidentally lose a received graph item.
class EgressInputLease final {
public:
  EgressInputLease() = default;
  ~EgressInputLease();

  EgressInputLease(const EgressInputLease&) = delete;
  EgressInputLease& operator=(const EgressInputLease&) = delete;
  EgressInputLease(EgressInputLease&& other) noexcept;
  EgressInputLease& operator=(EgressInputLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> payload() const;
  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> metadata() const;
  [[nodiscard]] const TypeDescriptor* type_descriptor() const noexcept;
  [[nodiscard]] const DataItemIdentity& item_identity() const noexcept;
  [[nodiscard]] ksj::base::Status acknowledge_consumed();

private:
  friend class SynchronousGraphExecutor;

  explicit EgressInputLease(FixedBufferEdgeConsumerLease edge_lease) noexcept;

  FixedBufferEdgeConsumerLease edge_lease_{};
};

// A generic bridge from a completed host frame to a frozen graph ingress.
// It is parameterized by the named endpoint, rather than hard-coding imaging
// data, so the same capability handles imaging, noise calibration, and phase
// reference frames. It copies the canonical bytes into the ingress pool and
// acknowledges the source lease only after the graph edge commit succeeds.
class CompletedFrameIngressBridge final {
public:
  [[nodiscard]] static ksj::base::Result<CompletedFrameIngressBridge>
  create(SynchronousGraphExecutor& executor, std::string_view ingress_id, HostFrameAssembler& source_assembler);

  CompletedFrameIngressBridge(const CompletedFrameIngressBridge&) = delete;
  CompletedFrameIngressBridge& operator=(const CompletedFrameIngressBridge&) = delete;
  CompletedFrameIngressBridge(CompletedFrameIngressBridge&& other) noexcept;
  CompletedFrameIngressBridge& operator=(CompletedFrameIngressBridge&& other) noexcept;
  ~CompletedFrameIngressBridge() = default;

  [[nodiscard]] ksj::base::Status publish(CompletedFrameLease&& completed_frame, DataItemIdentity identity);
  [[nodiscard]] ksj::base::Status end_of_input();
  [[nodiscard]] ksj::base::Status abort();

private:
  CompletedFrameIngressBridge(SynchronousGraphExecutor* executor, std::string ingress_id,
                              HostFrameAssembler* source_assembler) noexcept;

  SynchronousGraphExecutor* executor_{nullptr};
  std::string ingress_id_;
  HostFrameAssembler* source_assembler_{nullptr};
};

// The synchronous graph owner has no global registry. It binds one verified
// immutable plan to explicitly supplied caller slabs and one shared resource
// ledger for a scan. `execution_plan` and `verification_record` must outlive
// this object because their immutable descriptors are borrowed by Provider ABI
// calls.
class SynchronousGraphExecutor final {
public:
  [[nodiscard]] static ksj::base::Result<std::unique_ptr<SynchronousGraphExecutor>>
  create(const ExecutionPlan& execution_plan, const VerificationRecord& verification_record,
         SynchronousGraphExecutorStorage storage, std::shared_ptr<ResourceVectorLedger> resource_ledger);

  SynchronousGraphExecutor(const SynchronousGraphExecutor&) = delete;
  SynchronousGraphExecutor& operator=(const SynchronousGraphExecutor&) = delete;
  SynchronousGraphExecutor(SynchronousGraphExecutor&&) = delete;
  SynchronousGraphExecutor& operator=(SynchronousGraphExecutor&&) = delete;
  ~SynchronousGraphExecutor();

  // Reserves the ingress edge before giving the caller a writable pool slot.
  [[nodiscard]] ksj::base::Result<IngressOutputLease> try_acquire_ingress(std::string_view ingress_id);
  [[nodiscard]] ksj::base::Status end_ingress(std::string_view ingress_id);

  // Runs one normal firing. It reserves every downstream output first, then
  // claims all dynamic input heads transactionally. Missing inputs roll every
  // claim/reservation back unchanged; identity mismatch or callback/commit
  // failure aborts the graph.
  [[nodiscard]] ksj::base::Result<SynchronousFiringResult> try_fire(std::string_view node_id,
                                                                    const SynchronousGraphNodeInvocation& invocation);

  // A terminal callback is admitted only after all dynamic input edges for
  // the node reached EndOfInput and drained. Calibration artifacts are
  // ordinary-firing outputs only; terminal processing may flush data edges
  // but merely closes/polices unfulfilled artifact bindings.
  [[nodiscard]] ksj::base::Result<SynchronousFiringResult>
  try_finish_node(std::string_view node_id, const SynchronousGraphNodeInvocation& invocation);

  [[nodiscard]] FixedBufferEdgePollKind egress_poll_kind(std::string_view egress_id) const;
  [[nodiscard]] ksj::base::Result<EgressInputLease> try_acquire_egress(std::string_view egress_id);

  // This is primarily a diagnostic/read boundary. Node firings acquire their
  // declared static calibration inputs internally.
  [[nodiscard]] ksj::base::Result<CalibrationArtifactReadLease>
  try_acquire_calibration_artifact(std::string_view binding_id);

  [[nodiscard]] ksj::base::Status abort();
  [[nodiscard]] SynchronousGraphExecutorSnapshot snapshot() const;

private:
  friend class IngressOutputLease;
  friend class CompletedFrameIngressBridge;

  explicit SynchronousGraphExecutor(std::shared_ptr<detail::SynchronousGraphExecutorState> state) noexcept;
  [[nodiscard]] bool has_ingress(std::string_view ingress_id) const noexcept;
  [[nodiscard]] static ksj::base::Status commit_sealed_outputs_locked(
    const std::shared_ptr<detail::SynchronousGraphExecutorState>& state, detail::SynchronousGraphNodeRuntime& node,
    std::span<const SynchronousSealedOutput> sealed_outputs, std::optional<DataItemIdentity> expected_identity,
    std::uint64_t terminal_epoch, bool terminal);

  std::shared_ptr<detail::SynchronousGraphExecutorState> state_{};
};

} // namespace ksj::recon::runtime
