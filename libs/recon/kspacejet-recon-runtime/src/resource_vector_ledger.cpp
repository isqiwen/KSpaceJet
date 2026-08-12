#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace ksj::recon::runtime {

namespace detail {

struct ResourceVectorLedgerState {
  explicit ResourceVectorLedgerState(ResourceVectorCapacity resource_capacity)
      : capacity(std::move(resource_capacity)) {}

  const ResourceVectorCapacity capacity;
  mutable std::mutex mutex;
  ResourceVectorLedgerUsage reserved{};
  ResourceVectorLedgerUsage used{};
  ResourceVectorLedgerUsage high_water{};
};

} // namespace detail

namespace {

using LedgerState = detail::ResourceVectorLedgerState;

[[nodiscard]] ResourceVectorLedgerUsage zero_usage_for(const ResourceVectorCapacity& capacity) {
  ResourceVectorLedgerUsage usage;
  usage.devices.reserve(capacity.domains().devices().size());
  for (const auto& device : capacity.domains().devices()) {
    usage.devices.push_back({.device_id = device.device_id()});
  }
  return usage;
}

[[nodiscard]] ResourceVectorLedgerDeviceUsage* find_device(ResourceVectorLedgerUsage& usage,
                                                           const std::string_view device_id) noexcept {
  const auto found = std::lower_bound(usage.devices.begin(), usage.devices.end(), device_id,
                                      [](const ResourceVectorLedgerDeviceUsage& device, const std::string_view value) {
                                        return device.device_id < value;
                                      });
  if (found == usage.devices.end() || found->device_id != device_id) {
    return nullptr;
  }
  return &*found;
}

[[nodiscard]] bool fits(const Quantity reserved, const Quantity used, const Quantity requested,
                        const Quantity capacity) noexcept {
  return reserved <= capacity && used <= capacity - reserved && requested <= capacity - reserved - used;
}

[[nodiscard]] bool contains(const Quantity total, const Quantity amount) noexcept {
  return amount <= total;
}

[[nodiscard]] bool supports_devices(const LedgerState& state, const ResourceVector& amount) noexcept {
  for (const auto& requested : amount.devices()) {
    if (state.capacity.domains().find_device(requested.device_id()) == nullptr) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool can_reserve(const LedgerState& state, const ResourceVector& amount) noexcept {
  const auto& capacity = state.capacity;
  const auto& domains = capacity.domains();
  const auto& reserved = state.reserved;
  const auto& used = state.used;

  if (!fits(reserved.host_normal_bytes, used.host_normal_bytes, amount.host_normal_bytes(),
            domains.host_normal_bytes()) ||
      !fits(reserved.host_pinned_bytes, used.host_pinned_bytes, amount.host_pinned_bytes(),
            domains.host_pinned_bytes()) ||
      !fits(reserved.host_hugepage_bytes, used.host_hugepage_bytes, amount.host_hugepage_bytes(),
            domains.host_hugepage_bytes()) ||
      !fits(reserved.shared_host_bytes, used.shared_host_bytes, amount.shared_host_bytes(),
            domains.shared_host_bytes()) ||
      !fits(reserved.host_total_bytes, used.host_total_bytes, amount.host_total_bytes(),
            capacity.host_total_cap_bytes()) ||
      !fits(reserved.spool_bytes, used.spool_bytes, amount.spool_bytes(), domains.spool_bytes()) ||
      !fits(reserved.transport_bytes, used.transport_bytes, amount.transport_bytes(), domains.transport_bytes()) ||
      !fits(reserved.descriptor_count, used.descriptor_count, amount.descriptor_count(), domains.descriptor_count()) ||
      !fits(reserved.async_token_count, used.async_token_count, amount.async_token_count(),
            domains.async_token_count()) ||
      !fits(reserved.cpu_leaf_permits, used.cpu_leaf_permits, amount.cpu_leaf_permits(), domains.cpu_leaf_permits()) ||
      !fits(reserved.backend_gang_permits, used.backend_gang_permits, amount.backend_gang_permits(),
            domains.backend_gang_permits()) ||
      !fits(reserved.provider_private_permits, used.provider_private_permits, amount.provider_private_permits(),
            domains.provider_private_permits()) ||
      !fits(reserved.io_slots, used.io_slots, amount.io_slots(), domains.io_slots())) {
    return false;
  }

  for (const auto& requested : amount.devices()) {
    const auto* device_capacity = domains.find_device(requested.device_id());
    const auto* reserved_device = reserved.find_device(requested.device_id());
    const auto* used_device = used.find_device(requested.device_id());
    if (device_capacity == nullptr || reserved_device == nullptr || used_device == nullptr ||
        !fits(reserved_device->device_bytes, used_device->device_bytes, requested.device_bytes(),
              device_capacity->device_bytes()) ||
        !fits(reserved_device->gpu_stream_slots, used_device->gpu_stream_slots, requested.gpu_stream_slots(),
              device_capacity->gpu_stream_slots()) ||
        !fits(reserved_device->copy_engine_slots, used_device->copy_engine_slots, requested.copy_engine_slots(),
              device_capacity->copy_engine_slots())) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool contains_usage(const ResourceVectorLedgerUsage& usage, const ResourceVector& amount) noexcept {
  if (!contains(usage.host_normal_bytes, amount.host_normal_bytes()) ||
      !contains(usage.host_pinned_bytes, amount.host_pinned_bytes()) ||
      !contains(usage.host_hugepage_bytes, amount.host_hugepage_bytes()) ||
      !contains(usage.shared_host_bytes, amount.shared_host_bytes()) ||
      !contains(usage.host_total_bytes, amount.host_total_bytes()) ||
      !contains(usage.spool_bytes, amount.spool_bytes()) ||
      !contains(usage.transport_bytes, amount.transport_bytes()) ||
      !contains(usage.descriptor_count, amount.descriptor_count()) ||
      !contains(usage.async_token_count, amount.async_token_count()) ||
      !contains(usage.cpu_leaf_permits, amount.cpu_leaf_permits()) ||
      !contains(usage.backend_gang_permits, amount.backend_gang_permits()) ||
      !contains(usage.provider_private_permits, amount.provider_private_permits()) ||
      !contains(usage.io_slots, amount.io_slots())) {
    return false;
  }
  for (const auto& requested : amount.devices()) {
    const auto* current = usage.find_device(requested.device_id());
    if (current == nullptr || !contains(current->device_bytes, requested.device_bytes()) ||
        !contains(current->gpu_stream_slots, requested.gpu_stream_slots()) ||
        !contains(current->copy_engine_slots, requested.copy_engine_slots())) {
      return false;
    }
  }
  return true;
}

void add_usage(ResourceVectorLedgerUsage& target, const ResourceVector& amount) noexcept {
  target.host_normal_bytes += amount.host_normal_bytes();
  target.host_pinned_bytes += amount.host_pinned_bytes();
  target.host_hugepage_bytes += amount.host_hugepage_bytes();
  target.shared_host_bytes += amount.shared_host_bytes();
  target.host_total_bytes += amount.host_total_bytes();
  target.spool_bytes += amount.spool_bytes();
  target.transport_bytes += amount.transport_bytes();
  target.descriptor_count += amount.descriptor_count();
  target.async_token_count += amount.async_token_count();
  target.cpu_leaf_permits += amount.cpu_leaf_permits();
  target.backend_gang_permits += amount.backend_gang_permits();
  target.provider_private_permits += amount.provider_private_permits();
  target.io_slots += amount.io_slots();
  for (const auto& requested : amount.devices()) {
    auto* current = find_device(target, requested.device_id());
    current->device_bytes += requested.device_bytes();
    current->gpu_stream_slots += requested.gpu_stream_slots();
    current->copy_engine_slots += requested.copy_engine_slots();
  }
}

void subtract_usage(ResourceVectorLedgerUsage& target, const ResourceVector& amount) noexcept {
  target.host_normal_bytes -= amount.host_normal_bytes();
  target.host_pinned_bytes -= amount.host_pinned_bytes();
  target.host_hugepage_bytes -= amount.host_hugepage_bytes();
  target.shared_host_bytes -= amount.shared_host_bytes();
  target.host_total_bytes -= amount.host_total_bytes();
  target.spool_bytes -= amount.spool_bytes();
  target.transport_bytes -= amount.transport_bytes();
  target.descriptor_count -= amount.descriptor_count();
  target.async_token_count -= amount.async_token_count();
  target.cpu_leaf_permits -= amount.cpu_leaf_permits();
  target.backend_gang_permits -= amount.backend_gang_permits();
  target.provider_private_permits -= amount.provider_private_permits();
  target.io_slots -= amount.io_slots();
  for (const auto& requested : amount.devices()) {
    auto* current = find_device(target, requested.device_id());
    current->device_bytes -= requested.device_bytes();
    current->gpu_stream_slots -= requested.gpu_stream_slots();
    current->copy_engine_slots -= requested.copy_engine_slots();
  }
}

void update_high_water(ResourceVectorLedgerUsage& high_water, const ResourceVectorLedgerUsage& reserved,
                       const ResourceVectorLedgerUsage& used) noexcept {
  high_water.host_normal_bytes =
    std::max(high_water.host_normal_bytes, reserved.host_normal_bytes + used.host_normal_bytes);
  high_water.host_pinned_bytes =
    std::max(high_water.host_pinned_bytes, reserved.host_pinned_bytes + used.host_pinned_bytes);
  high_water.host_hugepage_bytes =
    std::max(high_water.host_hugepage_bytes, reserved.host_hugepage_bytes + used.host_hugepage_bytes);
  high_water.shared_host_bytes =
    std::max(high_water.shared_host_bytes, reserved.shared_host_bytes + used.shared_host_bytes);
  high_water.host_total_bytes =
    std::max(high_water.host_total_bytes, reserved.host_total_bytes + used.host_total_bytes);
  high_water.spool_bytes = std::max(high_water.spool_bytes, reserved.spool_bytes + used.spool_bytes);
  high_water.transport_bytes = std::max(high_water.transport_bytes, reserved.transport_bytes + used.transport_bytes);
  high_water.descriptor_count =
    std::max(high_water.descriptor_count, reserved.descriptor_count + used.descriptor_count);
  high_water.async_token_count =
    std::max(high_water.async_token_count, reserved.async_token_count + used.async_token_count);
  high_water.cpu_leaf_permits =
    std::max(high_water.cpu_leaf_permits, reserved.cpu_leaf_permits + used.cpu_leaf_permits);
  high_water.backend_gang_permits =
    std::max(high_water.backend_gang_permits, reserved.backend_gang_permits + used.backend_gang_permits);
  high_water.provider_private_permits =
    std::max(high_water.provider_private_permits, reserved.provider_private_permits + used.provider_private_permits);
  high_water.io_slots = std::max(high_water.io_slots, reserved.io_slots + used.io_slots);

  for (std::size_t index = 0; index < high_water.devices.size(); ++index) {
    auto& high_water_device = high_water.devices[index];
    const auto& reserved_device = reserved.devices[index];
    const auto& used_device = used.devices[index];
    high_water_device.device_bytes =
      std::max(high_water_device.device_bytes, reserved_device.device_bytes + used_device.device_bytes);
    high_water_device.gpu_stream_slots =
      std::max(high_water_device.gpu_stream_slots, reserved_device.gpu_stream_slots + used_device.gpu_stream_slots);
    high_water_device.copy_engine_slots =
      std::max(high_water_device.copy_engine_slots, reserved_device.copy_engine_slots + used_device.copy_engine_slots);
  }
}

} // namespace

bool ResourceVectorLedgerUsage::empty() const noexcept {
  if (host_normal_bytes != 0U || host_pinned_bytes != 0U || host_hugepage_bytes != 0U || shared_host_bytes != 0U ||
      host_total_bytes != 0U || spool_bytes != 0U || transport_bytes != 0U || descriptor_count != 0U ||
      async_token_count != 0U || cpu_leaf_permits != 0U || backend_gang_permits != 0U ||
      provider_private_permits != 0U || io_slots != 0U) {
    return false;
  }
  return std::all_of(devices.begin(), devices.end(), [](const ResourceVectorLedgerDeviceUsage& device) {
    return device.device_bytes == 0U && device.gpu_stream_slots == 0U && device.copy_engine_slots == 0U;
  });
}

const ResourceVectorLedgerDeviceUsage*
ResourceVectorLedgerUsage::find_device(const std::string_view device_id) const noexcept {
  const auto found = std::lower_bound(devices.begin(), devices.end(), device_id,
                                      [](const ResourceVectorLedgerDeviceUsage& device, const std::string_view value) {
                                        return device.device_id < value;
                                      });
  if (found == devices.end() || found->device_id != device_id) {
    return nullptr;
  }
  return &*found;
}

ResourceVectorLedgerReservation::ResourceVectorLedgerReservation(
  std::shared_ptr<detail::ResourceVectorLedgerState> state, ResourceVector amount)
    : state_(std::move(state)), amount_(std::move(amount)) {}

ResourceVectorLedgerReservation::~ResourceVectorLedgerReservation() {
  release_noexcept();
}

ResourceVectorLedgerReservation::ResourceVectorLedgerReservation(ResourceVectorLedgerReservation&& other) noexcept
    : state_(std::move(other.state_)), amount_(std::move(other.amount_)),
      committed_(std::exchange(other.committed_, false)) {
  other.amount_.reset();
}

ResourceVectorLedgerReservation&
ResourceVectorLedgerReservation::operator=(ResourceVectorLedgerReservation&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    state_ = std::move(other.state_);
    amount_ = std::move(other.amount_);
    committed_ = std::exchange(other.committed_, false);
    other.amount_.reset();
  }
  return *this;
}

bool ResourceVectorLedgerReservation::valid() const noexcept {
  return state_ != nullptr && amount_.has_value();
}

bool ResourceVectorLedgerReservation::committed() const noexcept {
  return valid() && committed_;
}

const ResourceVector* ResourceVectorLedgerReservation::amount() const noexcept {
  return amount_.has_value() ? &*amount_ : nullptr;
}

ksj::base::Status ResourceVectorLedgerReservation::commit() {
  return ResourceVectorLedger::commit_impl(*this, nullptr);
}

ksj::base::Status ResourceVectorLedgerReservation::rollback() {
  return ResourceVectorLedger::rollback_impl(*this, nullptr);
}

ksj::base::Status ResourceVectorLedgerReservation::release() {
  return ResourceVectorLedger::release_impl(*this, nullptr);
}

void ResourceVectorLedgerReservation::release_noexcept() noexcept {
  if (!valid()) {
    return;
  }
  try {
    static_cast<void>(ResourceVectorLedger::release_impl(*this, nullptr));
  } catch (...) {
    // A destructor cannot report a mutex/system exception. This exceptional
    // path is not recoverable here and must not escape a destructor.
  }
}

ResourceVectorLedger::ResourceVectorLedger(ResourceVectorCapacity capacity)
    : state_(std::make_shared<detail::ResourceVectorLedgerState>(std::move(capacity))) {
  state_->reserved = zero_usage_for(state_->capacity);
  state_->used = zero_usage_for(state_->capacity);
  state_->high_water = zero_usage_for(state_->capacity);
}

const ResourceVectorCapacity& ResourceVectorLedger::capacity() const noexcept {
  return state_->capacity;
}

ksj::base::Result<ResourceVectorLedgerReservation> ResourceVectorLedger::try_reserve(const ResourceVector& amount) {
  if (amount.empty()) {
    return ksj::base::Status::InvalidArgument("ResourceVectorLedger rejects an empty reservation");
  }

  // Copy before taking the shared accounting lock. The copy is the token's
  // immutable amount; all resource state changes below remain non-allocating.
  ResourceVector reservation_amount = amount;

  std::lock_guard lock(state_->mutex);
  if (!supports_devices(*state_, amount)) {
    return ksj::base::Status::InvalidArgument(
      "ResourceVectorLedger reservation names a device absent from its fixed capacity");
  }
  if (!can_reserve(*state_, amount)) {
    return ksj::base::Status::Unavailable("ResourceVectorLedger capacity is exhausted");
  }

  ResourceVectorLedgerReservation reservation{state_, std::move(reservation_amount)};
  add_usage(state_->reserved, amount);
  update_high_water(state_->high_water, state_->reserved, state_->used);
  return std::move(reservation);
}

ResourceVectorLedgerSnapshot ResourceVectorLedger::snapshot() const {
  std::lock_guard lock(state_->mutex);
  return {.reserved = state_->reserved, .used = state_->used, .high_water = state_->high_water};
}

ksj::base::Status ResourceVectorLedger::commit(ResourceVectorLedgerReservation& reservation) {
  return commit_impl(reservation, &state_);
}

ksj::base::Status ResourceVectorLedger::rollback(ResourceVectorLedgerReservation& reservation) {
  return rollback_impl(reservation, &state_);
}

ksj::base::Status ResourceVectorLedger::release(ResourceVectorLedgerReservation& reservation) {
  return release_impl(reservation, &state_);
}

ksj::base::Status
ResourceVectorLedger::commit_impl(ResourceVectorLedgerReservation& reservation,
                                  const std::shared_ptr<detail::ResourceVectorLedgerState>* expected) {
  if (!reservation.valid()) {
    return ksj::base::Status::StateError("cannot commit an empty ResourceVectorLedger reservation");
  }
  if (expected != nullptr && reservation.state_ != *expected) {
    return ksj::base::Status::StateError("ResourceVectorLedger reservation belongs to another ledger");
  }

  const auto state = reservation.state_;
  std::lock_guard lock(state->mutex);
  if (reservation.committed_) {
    return ksj::base::Status::StateError("ResourceVectorLedger reservation is already committed");
  }
  if (!contains_usage(state->reserved, *reservation.amount_)) {
    return ksj::base::Status::InternalError("ResourceVectorLedger reserved accounting underflow");
  }

  subtract_usage(state->reserved, *reservation.amount_);
  add_usage(state->used, *reservation.amount_);
  reservation.committed_ = true;
  return ksj::base::Status::Ok();
}

ksj::base::Status
ResourceVectorLedger::rollback_impl(ResourceVectorLedgerReservation& reservation,
                                    const std::shared_ptr<detail::ResourceVectorLedgerState>* expected) {
  if (!reservation.valid()) {
    return ksj::base::Status::StateError("cannot roll back an empty ResourceVectorLedger reservation");
  }
  if (expected != nullptr && reservation.state_ != *expected) {
    return ksj::base::Status::StateError("ResourceVectorLedger reservation belongs to another ledger");
  }

  const auto state = reservation.state_;
  std::lock_guard lock(state->mutex);
  if (reservation.committed_) {
    return ksj::base::Status::StateError("cannot roll back a committed ResourceVectorLedger reservation");
  }
  if (!contains_usage(state->reserved, *reservation.amount_)) {
    return ksj::base::Status::InternalError("ResourceVectorLedger reserved accounting underflow");
  }

  subtract_usage(state->reserved, *reservation.amount_);
  reservation.state_.reset();
  reservation.amount_.reset();
  reservation.committed_ = false;
  return ksj::base::Status::Ok();
}

ksj::base::Status
ResourceVectorLedger::release_impl(ResourceVectorLedgerReservation& reservation,
                                   const std::shared_ptr<detail::ResourceVectorLedgerState>* expected) {
  if (!reservation.valid()) {
    return ksj::base::Status::StateError("cannot release an empty ResourceVectorLedger reservation");
  }
  if (expected != nullptr && reservation.state_ != *expected) {
    return ksj::base::Status::StateError("ResourceVectorLedger reservation belongs to another ledger");
  }

  const auto state = reservation.state_;
  std::lock_guard lock(state->mutex);
  auto& account = reservation.committed_ ? state->used : state->reserved;
  if (!contains_usage(account, *reservation.amount_)) {
    reservation.state_.reset();
    reservation.amount_.reset();
    reservation.committed_ = false;
    return ksj::base::Status::InternalError("ResourceVectorLedger release accounting underflow");
  }

  subtract_usage(account, *reservation.amount_);
  reservation.state_.reset();
  reservation.amount_.reset();
  reservation.committed_ = false;
  return ksj::base::Status::Ok();
}

} // namespace ksj::recon::runtime
