#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/recon/resource_vector.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::recon::runtime {

namespace detail {
struct ResourceVectorLedgerState;
} // namespace detail

/**
 * A non-canonical, observable usage counter for one fixed device identity.
 *
 * Unlike DeviceResourceSlot, every field is permitted to be zero: snapshots
 * retain the complete fixed device table so a zero current usage cannot hide a
 * configured device domain.
 */
struct ResourceVectorLedgerDeviceUsage {
  std::string device_id;
  Quantity device_bytes{0U};
  Quantity gpu_stream_slots{0U};
  Quantity copy_engine_slots{0U};

  friend bool operator==(const ResourceVectorLedgerDeviceUsage&,
                         const ResourceVectorLedgerDeviceUsage&) noexcept = default;
};

/**
 * Per-domain accounting snapshot for ResourceVectorLedger.
 *
 * For Reserved and Used snapshots, `host_total_bytes` is the exact sum of
 * normal, pinned, hugepage, and shared host charges. It is included explicitly
 * so callers can inspect the host hierarchy limit without recreating arithmetic
 * from a concurrent snapshot. For HighWater, each field is an independent
 * maximum; consequently its host-total peak need not equal the sum of its
 * independently observed component peaks.
 */
struct ResourceVectorLedgerUsage {
  Quantity host_normal_bytes{0U};
  Quantity host_pinned_bytes{0U};
  Quantity host_hugepage_bytes{0U};
  Quantity shared_host_bytes{0U};
  Quantity host_total_bytes{0U};
  Quantity spool_bytes{0U};
  Quantity transport_bytes{0U};
  Quantity descriptor_count{0U};
  Quantity async_token_count{0U};
  Quantity cpu_leaf_permits{0U};
  Quantity backend_gang_permits{0U};
  Quantity provider_private_permits{0U};
  Quantity io_slots{0U};
  std::vector<ResourceVectorLedgerDeviceUsage> devices;

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const ResourceVectorLedgerDeviceUsage* find_device(std::string_view device_id) const noexcept;

  friend bool operator==(const ResourceVectorLedgerUsage&, const ResourceVectorLedgerUsage&) noexcept = default;
};

/**
 * A stable observation of one fixed-capacity ledger.
 *
 * `high_water` is the greatest combined (`reserved + used`) occupancy seen in
 * each domain since ledger construction.  It is not a second reservation
 * account and does not fall when capacity is released.
 */
struct ResourceVectorLedgerSnapshot {
  ResourceVectorLedgerUsage reserved{};
  ResourceVectorLedgerUsage used{};
  ResourceVectorLedgerUsage high_water{};
};

class ResourceVectorLedger;

/**
 * Move-only all-domain reservation acquired from ResourceVectorLedger.
 *
 * A reservation starts in the Reserved account. `commit()` atomically moves
 * it to Used; `rollback()` is valid only before commit and returns it to the
 * ledger; `release()` returns either account. Destruction calls `release()`,
 * so error paths cannot leak a successful resource acquisition.
 *
 * The object itself has single-owner semantics and is not safe for concurrent
 * operations. The shared ledger is safe for independent reservations from
 * many clients.
 */
class ResourceVectorLedgerReservation final {
public:
  ResourceVectorLedgerReservation() = default;
  ~ResourceVectorLedgerReservation();

  ResourceVectorLedgerReservation(const ResourceVectorLedgerReservation&) = delete;
  ResourceVectorLedgerReservation& operator=(const ResourceVectorLedgerReservation&) = delete;
  ResourceVectorLedgerReservation(ResourceVectorLedgerReservation&& other) noexcept;
  ResourceVectorLedgerReservation& operator=(ResourceVectorLedgerReservation&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool committed() const noexcept;
  [[nodiscard]] const ResourceVector* amount() const noexcept;

  [[nodiscard]] ksj::base::Status commit();
  [[nodiscard]] ksj::base::Status rollback();
  [[nodiscard]] ksj::base::Status release();

private:
  friend class ResourceVectorLedger;

  ResourceVectorLedgerReservation(std::shared_ptr<detail::ResourceVectorLedgerState> state, ResourceVector amount);

  void release_noexcept() noexcept;

  std::shared_ptr<detail::ResourceVectorLedgerState> state_{};
  std::optional<ResourceVector> amount_{};
  bool committed_{false};
};

/**
 * Shared, fixed-capacity multi-domain ResourceVector ledger.
 *
 * A ledger is constructed from one already validated ResourceVectorCapacity.
 * It never grows its capacity, never substitutes one resource domain for
 * another, and accepts only exact device identities declared by that capacity.
 * Every reserve/commit/rollback/release state transition is linearized under
 * one mutex after checking all hierarchy caps, including host total. This is
 * deliberately one atomic bundle acquisition, not a sequence of per-domain
 * locks that can leak a partial reservation on failure.
 *
 * An empty demand is rejected with invalid_argument because it would carry no
 * accounting claim. A demand for an unknown device identity is likewise
 * invalid_argument. A supported, non-empty demand that exceeds fixed capacity
 * or remaining capacity is rejected with unavailable and leaves all accounts
 * unchanged. A zero-capacity ledger is valid as an explicit no-work ledger;
 * it therefore rejects every non-empty demand with unavailable.
 */
class ResourceVectorLedger final {
public:
  explicit ResourceVectorLedger(ResourceVectorCapacity capacity);
  ~ResourceVectorLedger() = default;

  ResourceVectorLedger(const ResourceVectorLedger&) = delete;
  ResourceVectorLedger& operator=(const ResourceVectorLedger&) = delete;
  ResourceVectorLedger(ResourceVectorLedger&&) = delete;
  ResourceVectorLedger& operator=(ResourceVectorLedger&&) = delete;

  [[nodiscard]] const ResourceVectorCapacity& capacity() const noexcept;

  // Acquires the complete ResourceVector or no resource at all. The returned
  // move-only token retains a ResourceVector value and can therefore copy its
  // device table before locking; the accounting transition itself is
  // non-allocating and performs no backend operation.
  [[nodiscard]] ksj::base::Result<ResourceVectorLedgerReservation> try_reserve(const ResourceVector& amount);
  [[nodiscard]] ResourceVectorLedgerSnapshot snapshot() const;

  // These explicit ledger entry points reject a reservation from a different
  // ledger. The corresponding reservation methods use the reservation's own
  // state and are convenient for ordinary single-owner use.
  [[nodiscard]] ksj::base::Status commit(ResourceVectorLedgerReservation& reservation);
  [[nodiscard]] ksj::base::Status rollback(ResourceVectorLedgerReservation& reservation);
  [[nodiscard]] ksj::base::Status release(ResourceVectorLedgerReservation& reservation);

private:
  friend class ResourceVectorLedgerReservation;

  [[nodiscard]] static ksj::base::Status
  commit_impl(ResourceVectorLedgerReservation& reservation,
              const std::shared_ptr<detail::ResourceVectorLedgerState>* expected);
  [[nodiscard]] static ksj::base::Status
  rollback_impl(ResourceVectorLedgerReservation& reservation,
                const std::shared_ptr<detail::ResourceVectorLedgerState>* expected);
  [[nodiscard]] static ksj::base::Status
  release_impl(ResourceVectorLedgerReservation& reservation,
               const std::shared_ptr<detail::ResourceVectorLedgerState>* expected);

  std::shared_ptr<detail::ResourceVectorLedgerState> state_;
};

} // namespace ksj::recon::runtime
