#pragma once

#include "kspacejet/base/result.hpp"

#include <cstdint>
#include <mutex>

namespace ksj::recon::runtime {

// Resource quantities are charged before a frame is materialised, retained or
// published.  The ledger is intentionally independent of the transport: it is
// an in-process accounting primitive, not a wire-level credit protocol.
struct ResourceAmount {
  std::uint64_t items{0};
  std::uint64_t bytes{0};

  [[nodiscard]] constexpr bool empty() const noexcept { return items == 0U && bytes == 0U; }
};

struct ResourceCapacity {
  std::uint64_t items{0};
  std::uint64_t bytes{0};
};

struct ResourceLedgerSnapshot {
  ResourceCapacity capacity{};
  ResourceAmount reserved{};
  ResourceAmount used{};
};

class ResourceLedger;

// A reservation is move-only.  It is either released, or committed and later
// released.  Destruction releases either accounting state, which makes failure
// paths and early returns ledger-safe by construction.
class ResourceReservation {
public:
  ResourceReservation() = default;
  ~ResourceReservation();

  ResourceReservation(const ResourceReservation&) = delete;
  ResourceReservation& operator=(const ResourceReservation&) = delete;
  ResourceReservation(ResourceReservation&& other) noexcept;
  ResourceReservation& operator=(ResourceReservation&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool committed() const noexcept;
  [[nodiscard]] ResourceAmount amount() const noexcept;

  // Moves the reservation from the reserved account to the used account.  A
  // second commit is an explicit state error rather than silently succeeding.
  [[nodiscard]] ksj::base::Status commit();
  void release() noexcept;

private:
  friend class ResourceLedger;

  ResourceReservation(ResourceLedger* ledger, ResourceAmount amount) noexcept;

  ResourceLedger* ledger_{nullptr};
  ResourceAmount amount_{};
  bool committed_{false};
};

class ResourceLedger {
public:
  explicit ResourceLedger(ResourceCapacity capacity);

  ResourceLedger(const ResourceLedger&) = delete;
  ResourceLedger& operator=(const ResourceLedger&) = delete;

  // `try_reserve` is the only way to acquire managed capacity.  It makes no
  // allocation and never blocks a compute worker.
  [[nodiscard]] ksj::base::Result<ResourceReservation> try_reserve(ResourceAmount amount);
  [[nodiscard]] ResourceLedgerSnapshot snapshot() const;

private:
  friend class ResourceReservation;

  [[nodiscard]] ksj::base::Status commit(ResourceReservation& reservation);
  void release(ResourceReservation& reservation) noexcept;

  static bool fits(ResourceAmount current, ResourceAmount requested, ResourceCapacity capacity) noexcept;
  static void subtract(ResourceAmount& total, ResourceAmount value) noexcept;

  const ResourceCapacity capacity_;
  mutable std::mutex mutex_;
  ResourceAmount reserved_{};
  ResourceAmount used_{};
};

} // namespace ksj::recon::runtime
