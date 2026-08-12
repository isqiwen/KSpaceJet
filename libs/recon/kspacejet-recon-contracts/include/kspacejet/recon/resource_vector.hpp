#pragma once

#include "kspacejet/recon/bounded_value.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {

// This is the common v1 JSON-schema bound used by every artifact that embeds
// a ResourceVector.  Keeping it in the value model prevents a valid in-memory
// reservation from becoming impossible to publish as a public artifact.
inline constexpr std::size_t kMaxDeviceResourceIdLength = 255U;

// All device-specific resources for one stable device identity.  The identity
// is deployment/plan data (for example "cuda:0"), never an implicit vector
// index: this keeps per-device capacity comparisons deterministic.
struct DeviceResourceSlotSpec {
  std::string device_id;
  Quantity device_bytes = 0;
  Quantity gpu_stream_slots = 0;
  Quantity copy_engine_slots = 0;
};

class DeviceResourceSlot final {
public:
  [[nodiscard]] static Result<DeviceResourceSlot> create(const DeviceResourceSlotSpec& specification,
                                                         std::string_view field_name);

  [[nodiscard]] const std::string& device_id() const noexcept { return device_id_; }
  [[nodiscard]] constexpr Quantity device_bytes() const noexcept { return device_bytes_.value(); }
  [[nodiscard]] constexpr Quantity gpu_stream_slots() const noexcept { return gpu_stream_slots_.value(); }
  [[nodiscard]] constexpr Quantity copy_engine_slots() const noexcept { return copy_engine_slots_.value(); }

  [[nodiscard]] bool exactly_matches(const DeviceResourceSlot& other) const noexcept { return *this == other; }

  friend bool operator==(const DeviceResourceSlot&, const DeviceResourceSlot&) noexcept = default;

private:
  DeviceResourceSlot(std::string device_id, CanonicalQuantity device_bytes, CanonicalQuantity gpu_stream_slots,
                     CanonicalQuantity copy_engine_slots) noexcept
      : device_id_(std::move(device_id)), device_bytes_(device_bytes), gpu_stream_slots_(gpu_stream_slots),
        copy_engine_slots_(copy_engine_slots) {}

  std::string device_id_;
  CanonicalQuantity device_bytes_;
  CanonicalQuantity gpu_stream_slots_;
  CanonicalQuantity copy_engine_slots_;
};

// A plan/resource demand expressed in independently-accounted domains.  It
// is intentionally a value model only: enforcing the four ledgers described
// by the architecture is runtime work.  A scalar field cannot be borrowed by
// another domain (for example normal host bytes cannot satisfy pinned bytes).
struct ResourceVectorSpec {
  Quantity host_normal_bytes = 0;
  Quantity host_pinned_bytes = 0;
  Quantity host_hugepage_bytes = 0;
  Quantity shared_host_bytes = 0;
  Quantity spool_bytes = 0;
  Quantity transport_bytes = 0;
  Quantity descriptor_count = 0;
  Quantity async_token_count = 0;
  Quantity cpu_leaf_permits = 0;
  Quantity backend_gang_permits = 0;
  Quantity provider_private_permits = 0;
  Quantity io_slots = 0;
  std::vector<DeviceResourceSlotSpec> devices;
};

class ResourceVector final {
public:
  [[nodiscard]] static Result<ResourceVector> create(const ResourceVectorSpec& specification,
                                                     std::string_view field_name = "resource_vector");

  [[nodiscard]] constexpr Quantity host_normal_bytes() const noexcept { return host_normal_bytes_.value(); }
  [[nodiscard]] constexpr Quantity host_pinned_bytes() const noexcept { return host_pinned_bytes_.value(); }
  [[nodiscard]] constexpr Quantity host_hugepage_bytes() const noexcept { return host_hugepage_bytes_.value(); }
  [[nodiscard]] constexpr Quantity shared_host_bytes() const noexcept { return shared_host_bytes_.value(); }
  [[nodiscard]] constexpr Quantity host_total_bytes() const noexcept { return host_total_bytes_.value(); }
  [[nodiscard]] constexpr Quantity spool_bytes() const noexcept { return spool_bytes_.value(); }
  [[nodiscard]] constexpr Quantity transport_bytes() const noexcept { return transport_bytes_.value(); }
  [[nodiscard]] constexpr Quantity descriptor_count() const noexcept { return descriptor_count_.value(); }
  [[nodiscard]] constexpr Quantity async_token_count() const noexcept { return async_token_count_.value(); }
  [[nodiscard]] constexpr Quantity cpu_leaf_permits() const noexcept { return cpu_leaf_permits_.value(); }
  [[nodiscard]] constexpr Quantity backend_gang_permits() const noexcept { return backend_gang_permits_.value(); }
  [[nodiscard]] constexpr Quantity provider_private_permits() const noexcept {
    return provider_private_permits_.value();
  }
  [[nodiscard]] constexpr Quantity io_slots() const noexcept { return io_slots_.value(); }
  [[nodiscard]] const std::vector<DeviceResourceSlot>& devices() const noexcept { return devices_; }

  [[nodiscard]] const DeviceResourceSlot* find_device(std::string_view device_id) const noexcept;

  // A rejected admission must not hold a process reservation.  Keep this
  // predicate on the canonical multi-domain representation rather than
  // reintroducing a scalar "zero reservation" side channel.
  [[nodiscard]] bool empty() const noexcept;

  // Component-wise comparison only.  Use ResourceVectorCapacity::can_admit()
  // when the deployment's host-total hierarchical cap also applies.
  [[nodiscard]] bool fits_within(const ResourceVector& capacity) const noexcept;
  [[nodiscard]] bool exactly_matches(const ResourceVector& other) const noexcept { return *this == other; }

  friend bool operator==(const ResourceVector&, const ResourceVector&) noexcept = default;

private:
  ResourceVector(CanonicalQuantity host_normal_bytes, CanonicalQuantity host_pinned_bytes,
                 CanonicalQuantity host_hugepage_bytes, CanonicalQuantity shared_host_bytes,
                 CanonicalQuantity host_total_bytes, CanonicalQuantity spool_bytes, CanonicalQuantity transport_bytes,
                 CanonicalQuantity descriptor_count, CanonicalQuantity async_token_count,
                 CanonicalQuantity cpu_leaf_permits, CanonicalQuantity backend_gang_permits,
                 CanonicalQuantity provider_private_permits, CanonicalQuantity io_slots,
                 std::vector<DeviceResourceSlot> devices) noexcept
      : host_normal_bytes_(host_normal_bytes), host_pinned_bytes_(host_pinned_bytes),
        host_hugepage_bytes_(host_hugepage_bytes), shared_host_bytes_(shared_host_bytes),
        host_total_bytes_(host_total_bytes), spool_bytes_(spool_bytes), transport_bytes_(transport_bytes),
        descriptor_count_(descriptor_count), async_token_count_(async_token_count), cpu_leaf_permits_(cpu_leaf_permits),
        backend_gang_permits_(backend_gang_permits), provider_private_permits_(provider_private_permits),
        io_slots_(io_slots), devices_(std::move(devices)) {}

  CanonicalQuantity host_normal_bytes_;
  CanonicalQuantity host_pinned_bytes_;
  CanonicalQuantity host_hugepage_bytes_;
  CanonicalQuantity shared_host_bytes_;
  CanonicalQuantity host_total_bytes_;
  CanonicalQuantity spool_bytes_;
  CanonicalQuantity transport_bytes_;
  CanonicalQuantity descriptor_count_;
  CanonicalQuantity async_token_count_;
  CanonicalQuantity cpu_leaf_permits_;
  CanonicalQuantity backend_gang_permits_;
  CanonicalQuantity provider_private_permits_;
  CanonicalQuantity io_slots_;
  std::vector<DeviceResourceSlot> devices_;
};

// Deployment capacity wraps a vector with the host hierarchy that cannot be
// represented by independent component limits alone:
// normal + pinned + hugepage + shared <= host_total_cap.
struct ResourceVectorCapacitySpec {
  ResourceVectorSpec domains;
  Quantity host_total_cap_bytes = 0;
};

class ResourceVectorCapacity final {
public:
  [[nodiscard]] static Result<ResourceVectorCapacity> create(const ResourceVectorCapacitySpec& specification,
                                                             std::string_view field_name = "resource_vector_capacity");

  [[nodiscard]] const ResourceVector& domains() const noexcept { return domains_; }
  [[nodiscard]] constexpr Quantity host_total_cap_bytes() const noexcept { return host_total_cap_bytes_.value(); }

  // No cross-domain substitution is permitted.  In particular, a demand for
  // pinned/device memory is rejected even when normal host capacity is idle.
  [[nodiscard]] bool can_admit(const ResourceVector& demand) const noexcept;

private:
  ResourceVectorCapacity(ResourceVector domains, CanonicalQuantity host_total_cap_bytes) noexcept
      : domains_(std::move(domains)), host_total_cap_bytes_(host_total_cap_bytes) {}

  ResourceVector domains_;
  CanonicalQuantity host_total_cap_bytes_;
};

} // namespace ksj::recon
