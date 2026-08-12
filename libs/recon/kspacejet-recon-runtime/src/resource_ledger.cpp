#include "kspacejet/recon/runtime/resource_ledger.hpp"

#include <utility>

namespace ksj::recon::runtime {

ResourceReservation::ResourceReservation(ResourceLedger* ledger, const ResourceAmount amount) noexcept
    : ledger_(ledger), amount_(amount) {}

ResourceReservation::~ResourceReservation() {
  release();
}

ResourceReservation::ResourceReservation(ResourceReservation&& other) noexcept
    : ledger_(std::exchange(other.ledger_, nullptr)), amount_(other.amount_), committed_(other.committed_) {
  other.amount_ = {};
  other.committed_ = false;
}

ResourceReservation& ResourceReservation::operator=(ResourceReservation&& other) noexcept {
  if (this != &other) {
    release();
    ledger_ = std::exchange(other.ledger_, nullptr);
    amount_ = other.amount_;
    committed_ = other.committed_;
    other.amount_ = {};
    other.committed_ = false;
  }
  return *this;
}

bool ResourceReservation::valid() const noexcept {
  return ledger_ != nullptr;
}

bool ResourceReservation::committed() const noexcept {
  return committed_;
}

ResourceAmount ResourceReservation::amount() const noexcept {
  return amount_;
}

ksj::base::Status ResourceReservation::commit() {
  if (ledger_ == nullptr) {
    return ksj::base::Status::StateError("cannot commit an empty resource reservation");
  }
  return ledger_->commit(*this);
}

void ResourceReservation::release() noexcept {
  if (ledger_ != nullptr) {
    ledger_->release(*this);
  }
}

ResourceLedger::ResourceLedger(const ResourceCapacity capacity) : capacity_(capacity) {}

ksj::base::Result<ResourceReservation> ResourceLedger::try_reserve(const ResourceAmount amount) {
  std::lock_guard lock(mutex_);
  if (!fits(reserved_, amount, capacity_) || !fits(used_, amount, capacity_)) {
    return ksj::base::Status::Unavailable("resource ledger capacity is exhausted");
  }
  if (amount.items > capacity_.items - reserved_.items - used_.items ||
      amount.bytes > capacity_.bytes - reserved_.bytes - used_.bytes) {
    return ksj::base::Status::Unavailable("resource ledger capacity is exhausted");
  }
  reserved_.items += amount.items;
  reserved_.bytes += amount.bytes;
  return ResourceReservation{this, amount};
}

ResourceLedgerSnapshot ResourceLedger::snapshot() const {
  std::lock_guard lock(mutex_);
  return {.capacity = capacity_, .reserved = reserved_, .used = used_};
}

ksj::base::Status ResourceLedger::commit(ResourceReservation& reservation) {
  std::lock_guard lock(mutex_);
  if (reservation.ledger_ != this) {
    return ksj::base::Status::StateError("resource reservation belongs to another ledger");
  }
  if (reservation.committed_) {
    return ksj::base::Status::StateError("resource reservation is already committed");
  }
  if (reservation.amount_.items > reserved_.items || reservation.amount_.bytes > reserved_.bytes) {
    return ksj::base::Status::InternalError("resource ledger reservation accounting underflow");
  }
  subtract(reserved_, reservation.amount_);
  used_.items += reservation.amount_.items;
  used_.bytes += reservation.amount_.bytes;
  reservation.committed_ = true;
  return ksj::base::Status::Ok();
}

void ResourceLedger::release(ResourceReservation& reservation) noexcept {
  std::lock_guard lock(mutex_);
  if (reservation.ledger_ != this) {
    return;
  }
  auto& account = reservation.committed_ ? used_ : reserved_;
  if (reservation.amount_.items > account.items || reservation.amount_.bytes > account.bytes) {
    // A corrupted ledger must not wrap unsigned accounting.  The runtime
    // invariant monitor records this condition at a higher layer.
    reservation.ledger_ = nullptr;
    reservation.amount_ = {};
    reservation.committed_ = false;
    return;
  }
  subtract(account, reservation.amount_);
  reservation.ledger_ = nullptr;
  reservation.amount_ = {};
  reservation.committed_ = false;
}

bool ResourceLedger::fits(const ResourceAmount current, const ResourceAmount requested,
                          const ResourceCapacity capacity) noexcept {
  return current.items <= capacity.items && requested.items <= capacity.items - current.items &&
         current.bytes <= capacity.bytes && requested.bytes <= capacity.bytes - current.bytes;
}

void ResourceLedger::subtract(ResourceAmount& total, const ResourceAmount value) noexcept {
  total.items -= value.items;
  total.bytes -= value.bytes;
}

} // namespace ksj::recon::runtime
