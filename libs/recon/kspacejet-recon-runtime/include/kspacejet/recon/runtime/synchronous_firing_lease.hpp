#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/provider/loader/provider_loader.hpp"
#include "kspacejet/recon/resource_vector.hpp"
#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace ksj::recon::runtime {

class PlanBoundSynchronousOutputBridge;
class AdmittedPlanBoundDataPlane;

/**
 * The intentionally small M3-A host boundary for one synchronous Provider
 * callback.  It is not a scheduler, BufferHandle implementation, edge, or
 * worker fault boundary.  In particular, it supports only host-pageable
 * payloads and rejects retention and asynchronous registration.
 *
 * A caller owns the Provider lifecycle handles.  `provider` pins the shared
 * library while the host calls its ABI table; it is copied into this value so
 * that a registry cannot unload the module while a firing is in progress.
 */
struct SynchronousProviderInvocation {
  // The plan-bound M3.7 bridge verifies this loaded module's frozen provider
  // bundle/operator/contract identity before entering Provider code. The
  // opaque lifecycle handles must have been created with the resolved
  // canonical node configuration and remain trusted in-process preconditions
  // established by the caller: M3.7 does not attempt to reconstruct or attest
  // them from raw ABI pointers.
  ksj::provider::loader::ProviderLease provider{};
  std::string operator_id;
  ksj_provider_operator* operator_handle{nullptr};
  ksj_execution_context* execution_context{nullptr};
  ksj_key_state* key_state{nullptr};
};

// All backing spans are borrowed only for the enclosing process/on_scan_end
// callback.  The host never retains an input payload after the callback
// returns.  ABI type descriptors are likewise borrowed plan-owned values and
// must remain valid through that callback.
struct SynchronousInputItem {
  ksj::base::ConstByteSpan payload{};
  ksj::base::ConstByteSpan metadata{};
  ksj_type_descriptor_view type{};
  std::uint64_t semantic_key_hash{0U};
  std::uint64_t order_key{0U};
  std::uint64_t item_ordinal{0U};
};

struct SynchronousInputBatch {
  std::span<const SynchronousInputItem> items{};
  std::uint32_t input_port{0U};
  std::uint64_t batch_id{0U};
  std::uint64_t order_domain{0U};
};

// The output storage is supplied and owned by the host/runtime before the
// Provider callback starts.  The Provider can access it only after acquiring
// the matching output slot and mapping the resulting grant.  `required_type`
// is the exact frozen ABI descriptor that seal() must return.
struct SynchronousOutputGrantSpec {
  ksj::base::ByteSpan storage{};
  // Plan-owned, bounded storage into which the host copies metadata from a
  // Provider's borrowed seal descriptor before that Provider callback returns.
  // Its bytes are charged together with `storage` in the firing reservation.
  ksj::base::ByteSpan metadata_storage{};
  std::uint32_t output_port{0U};
  std::uint32_t maximum_item_count{0U};
  ksj_type_descriptor_view required_type{};
};

// This view is valid only inside SynchronousOutputCommitCallback.  A future
// BufferHandle/edge implementation will take ownership before the callback
// returns; this first slice deliberately does not invent a second output
// ownership protocol.
struct SynchronousSealedOutput {
  std::uint32_t output_slot{0U};
  ksj::base::ConstByteSpan payload{};
  ksj_output_seal_descriptor descriptor{};
};

using SynchronousOutputCommitCallback =
  std::function<ksj::base::Status(std::span<const SynchronousSealedOutput> outputs)>;

struct SynchronousFiringRequest {
  std::uint64_t resource_occurrence_id{0U};
  std::uint64_t slot_generation{0U};
  std::uint64_t terminal_epoch{0U};
  std::span<const SynchronousInputBatch> input_batches{};
  std::span<const SynchronousOutputGrantSpec> output_grants{};
  ksj::base::ByteSpan scratch{};
  SynchronousOutputCommitCallback commit_outputs{};
};

// These are fixed plan bounds. The host preallocates its ABI view arrays at
// create() and does not allocate them while a Provider callback is live.
// create() separately commits its persistent ABI staging to the shared
// ledger for the host lifetime. Each firing acquires only the dynamic
// `firing_reservation` before entering the Provider ABI, and releases that
// reservation on every callback outcome, including an exception or
// output-commit failure.
struct SynchronousFiringLeaseConfig {
  SynchronousFiringLeaseConfig(std::shared_ptr<ResourceVectorLedger> resource_ledger,
                               ksj::recon::ResourceVector firing_reservation)
      : resource_ledger(std::move(resource_ledger)), firing_reservation(std::move(firing_reservation)) {}

  // The ledger is process/scan-owned and may be shared by several hosts. A
  // null ledger is rejected by create(); there is intentionally no per-host
  // capacity fallback that could silently bypass shared accounting.
  std::shared_ptr<ResourceVectorLedger> resource_ledger;
  // Dynamic callback resources only: CPU execution, externally supplied
  // output/scratch storage, and later per-firing domains. It must not include
  // the bounded ABI staging that the host reserves for its full lifetime.
  ksj::recon::ResourceVector firing_reservation;
  std::uint32_t maximum_input_batches{0U};
  std::uint32_t maximum_input_items{0U};
  std::uint32_t maximum_output_grants{0U};
  std::uint64_t maximum_input_payload_bytes{0U};
  std::uint64_t maximum_scratch_bytes{0U};
  std::uint64_t maximum_metadata_bytes{64U * 1024U};
};

enum class SynchronousFiringOutcome : std::uint8_t {
  done,
  yielded,
  structured_failure,
  contract_violation,
};

[[nodiscard]] std::string_view to_string(SynchronousFiringOutcome outcome) noexcept;

// A successful Result means that the Provider callback was entered.  The
// outcome then tells the caller whether the claimed occurrence is terminally
// successful, failed, or a Provider/host-contract violation. `yielded` is
// reserved for a later transactional-key-state slice; M3-A treats a raw ABI
// YIELD as a contract violation. A failing Result is exclusively a
// pre-callback error, so its input claim may safely remain available to a
// future scheduler.
struct SynchronousFiringResult {
  SynchronousFiringOutcome outcome{SynchronousFiringOutcome::structured_failure};
  ksj_status provider_status{KSJ_STATUS_OK};
  std::uint64_t consumed_input_item_count{0U};
  std::uint32_t sealed_output_count{0U};
  std::uint32_t committed_output_count{0U};
  std::uint64_t sealed_output_bytes{0U};
  std::uint64_t terminal_epoch{0U};
};

struct SynchronousFiringLeaseSnapshot {
  bool callback_active{false};
  std::optional<ksj::recon::ResourceVector> active_reservation{};
  std::optional<ksj::recon::ResourceVector> high_water_reservation{};
  std::uint64_t callback_count{0U};
};

/**
 * Synchronous, host-pageable FiringLease executor.
 *
 * The host acquires the complete fixed ResourceVector from a shared ledger
 * before a callback is entered, exposes only `INPUT_BATCHES`,
 * `OUTPUT_GRANTS`, optional `SCRATCH`, and `CANCELLATION` capabilities, and
 * releases that bundle on every outcome. It does not replace a future
 * process/scan scheduler or resource controller.
 * Retention and async callbacks are present only to return UNSUPPORTED; an
 * AsyncPending or YIELD result without the required later runtime feature is
 * a contract violation. This class intentionally has one active callback at
 * a time.
 */
class SynchronousFiringLeaseHost final {
public:
  [[nodiscard]] static ksj::base::Result<SynchronousFiringLeaseHost> create(SynchronousFiringLeaseConfig config);

  SynchronousFiringLeaseHost(const SynchronousFiringLeaseHost&) = delete;
  SynchronousFiringLeaseHost& operator=(const SynchronousFiringLeaseHost&) = delete;
  SynchronousFiringLeaseHost(SynchronousFiringLeaseHost&&) noexcept;
  SynchronousFiringLeaseHost& operator=(SynchronousFiringLeaseHost&&) noexcept;
  ~SynchronousFiringLeaseHost();

  // Invokes operator_process_batch.  The full request is validated and the
  // resource bundle is reserved before the Provider ABI call.
  [[nodiscard]] ksj::base::Result<SynchronousFiringResult> process(const SynchronousProviderInvocation& invocation,
                                                                   const SynchronousFiringRequest& request);

  // Invokes the normal, output-bearing terminal callback.  Terminal input is
  // deliberately empty in this first slice; bounded normal-flush output still
  // uses the same OutputGrant/seal/commit path as ordinary process().
  [[nodiscard]] ksj::base::Result<SynchronousFiringResult> on_scan_end(const SynchronousProviderInvocation& invocation,
                                                                       const SynchronousFiringRequest& request,
                                                                       std::uint64_t completed_input_item_count);

  [[nodiscard]] SynchronousFiringLeaseSnapshot snapshot() const;

private:
  friend class PlanBoundSynchronousOutputBridge;
  friend class AdmittedPlanBoundDataPlane;

  enum class StagingAccounting : std::uint8_t {
    self_reserved,
    preaccounted_by_plan,
  };

  // This is intentionally private to the plan-bound bridge.  It preserves
  // the public raw-span process() ABI while allowing an already-admitted
  // BufferPool slot to serve as an output grant without adding those same
  // payload bytes to the dynamic firing ResourceVector a second time.
  [[nodiscard]] ksj::base::Result<SynchronousFiringResult>
  process_preaccounted_output(const SynchronousProviderInvocation& invocation, const SynchronousFiringRequest& request);

  // The plan-bound data plane has already reserved and committed the full
  // frozen firing-lease staging allowance from its local ledger. This factory
  // constructs the same fixed ABI workspace without taking a second
  // actual-size staging reservation. It is deliberately unavailable to the
  // public raw-span host API.
  [[nodiscard]] static ksj::base::Result<SynchronousFiringLeaseHost>
  create_preaccounted_staging(SynchronousFiringLeaseConfig config);

  [[nodiscard]] static ksj::base::Result<SynchronousFiringLeaseHost> create_impl(SynchronousFiringLeaseConfig config,
                                                                                 StagingAccounting staging_accounting);

  struct Impl;

  explicit SynchronousFiringLeaseHost(Impl* implementation) noexcept;

  Impl* implementation_{nullptr};
};

} // namespace ksj::recon::runtime
