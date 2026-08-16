#include "kspacejet/recon/resource_vector.hpp"

#include "utf8.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ksj::recon {
namespace {

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

[[nodiscard]] std::string field(const std::string_view prefix, const std::string_view suffix) {
  std::string result(prefix);
  if (!result.empty() && !suffix.empty()) {
    result.push_back('.');
  }
  result.append(suffix);
  return result;
}

[[nodiscard]] bool is_device_id(const std::string_view value) noexcept {
  for (const unsigned char character : value) {
    if (character <= 0x7FU && (std::iscntrl(character) != 0 || std::isspace(character) != 0)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Result<CanonicalQuantity> canonical_quantity(const Quantity value, const std::string_view field_name) {
  return CanonicalQuantity::create(value, field_name);
}

[[nodiscard]] Result<CanonicalQuantity> host_total(const CanonicalQuantity host_normal,
                                                   const CanonicalQuantity host_pinned,
                                                   const CanonicalQuantity host_hugepage,
                                                   const CanonicalQuantity shared_host,
                                                   const std::string_view field_name) {
  auto first = checked_add(host_normal.value(), host_pinned.value(), field_name);
  if (!first.ok()) {
    return first.status();
  }
  auto second = checked_add(first.value(), host_hugepage.value(), field_name);
  if (!second.ok()) {
    return second.status();
  }
  auto third = checked_add(second.value(), shared_host.value(), field_name);
  if (!third.ok()) {
    return third.status();
  }
  return CanonicalQuantity::create(third.value(), field_name);
}

} // namespace

Result<DeviceResourceSlot> DeviceResourceSlot::create(const DeviceResourceSlotSpec& specification,
                                                      const std::string_view field_name) {
  auto device_id_length = detail::utf8_code_point_count(specification.device_id, field(field_name, "device_id"));
  if (!device_id_length.ok()) {
    return device_id_length.status();
  }
  if (device_id_length.value() == 0U || device_id_length.value() > kMaxDeviceResourceIdLength) {
    return validation(field(field_name, "device_id") + " must contain 1 to " +
                      std::to_string(kMaxDeviceResourceIdLength) + " Unicode code points.");
  }
  if (!is_device_id(specification.device_id)) {
    return validation(field(field_name, "device_id") + " must contain no ASCII whitespace or control bytes.");
  }
  auto device_bytes = canonical_quantity(specification.device_bytes, field(field_name, "device_bytes"));
  if (!device_bytes.ok()) {
    return device_bytes.status();
  }
  auto streams = canonical_quantity(specification.gpu_stream_slots, field(field_name, "gpu_stream_slots"));
  if (!streams.ok()) {
    return streams.status();
  }
  auto copy_slots = canonical_quantity(specification.copy_engine_slots, field(field_name, "copy_engine_slots"));
  if (!copy_slots.ok()) {
    return copy_slots.status();
  }
  if (device_bytes.value().value() == 0 && streams.value().value() == 0 && copy_slots.value().value() == 0) {
    return validation(std::string(field_name) + " must reserve at least one device resource.");
  }
  return DeviceResourceSlot{specification.device_id, std::move(device_bytes).value(), std::move(streams).value(),
                            std::move(copy_slots).value()};
}

Result<ResourceVector> ResourceVector::create(const ResourceVectorSpec& specification,
                                              const std::string_view field_name) {
  auto host_normal = canonical_quantity(specification.host_normal_bytes, field(field_name, "host_normal_bytes"));
  if (!host_normal.ok()) {
    return host_normal.status();
  }
  auto host_pinned = canonical_quantity(specification.host_pinned_bytes, field(field_name, "host_pinned_bytes"));
  if (!host_pinned.ok()) {
    return host_pinned.status();
  }
  auto host_hugepage = canonical_quantity(specification.host_hugepage_bytes, field(field_name, "host_hugepage_bytes"));
  if (!host_hugepage.ok()) {
    return host_hugepage.status();
  }
  auto shared_host = canonical_quantity(specification.shared_host_bytes, field(field_name, "shared_host_bytes"));
  if (!shared_host.ok()) {
    return shared_host.status();
  }
  auto total = host_total(host_normal.value(), host_pinned.value(), host_hugepage.value(), shared_host.value(),
                          field(field_name, "host_total_bytes"));
  if (!total.ok()) {
    return total.status();
  }
  auto spool = canonical_quantity(specification.spool_bytes, field(field_name, "spool_bytes"));
  if (!spool.ok()) {
    return spool.status();
  }
  auto transport = canonical_quantity(specification.transport_bytes, field(field_name, "transport_bytes"));
  if (!transport.ok()) {
    return transport.status();
  }
  auto descriptors = canonical_quantity(specification.descriptor_count, field(field_name, "descriptor_count"));
  if (!descriptors.ok()) {
    return descriptors.status();
  }
  auto async_tokens = canonical_quantity(specification.async_token_count, field(field_name, "async_token_count"));
  if (!async_tokens.ok()) {
    return async_tokens.status();
  }
  auto cpu = canonical_quantity(specification.cpu_leaf_permits, field(field_name, "cpu_leaf_permits"));
  if (!cpu.ok()) {
    return cpu.status();
  }
  auto backend = canonical_quantity(specification.backend_gang_permits, field(field_name, "backend_gang_permits"));
  if (!backend.ok()) {
    return backend.status();
  }
  auto provider =
    canonical_quantity(specification.provider_private_permits, field(field_name, "provider_private_permits"));
  if (!provider.ok()) {
    return provider.status();
  }
  auto io_slots = canonical_quantity(specification.io_slots, field(field_name, "io_slots"));
  if (!io_slots.ok()) {
    return io_slots.status();
  }

  std::vector<DeviceResourceSlot> devices;
  devices.reserve(specification.devices.size());
  for (std::size_t index = 0; index < specification.devices.size(); ++index) {
    auto device = DeviceResourceSlot::create(specification.devices[index],
                                             field(field_name, "devices[" + std::to_string(index) + "]"));
    if (!device.ok()) {
      return device.status();
    }
    devices.push_back(std::move(device).value());
  }
  std::sort(devices.begin(), devices.end(), [](const DeviceResourceSlot& left, const DeviceResourceSlot& right) {
    return left.device_id() < right.device_id();
  });
  const auto duplicate = std::adjacent_find(devices.begin(), devices.end(),
                                            [](const DeviceResourceSlot& left, const DeviceResourceSlot& right) {
                                              return left.device_id() == right.device_id();
                                            });
  if (duplicate != devices.end()) {
    return validation(field(field_name, "devices") + " must not contain duplicate device_id values.");
  }

  return ResourceVector{std::move(host_normal).value(),   std::move(host_pinned).value(),
                        std::move(host_hugepage).value(), std::move(shared_host).value(),
                        std::move(total).value(),         std::move(spool).value(),
                        std::move(transport).value(),     std::move(descriptors).value(),
                        std::move(async_tokens).value(),  std::move(cpu).value(),
                        std::move(backend).value(),       std::move(provider).value(),
                        std::move(io_slots).value(),      std::move(devices)};
}

const DeviceResourceSlot* ResourceVector::find_device(const std::string_view device_id) const noexcept {
  const auto found = std::lower_bound(devices_.begin(), devices_.end(), device_id,
                                      [](const DeviceResourceSlot& device, const std::string_view value) {
                                        return device.device_id() < value;
                                      });
  if (found == devices_.end() || found->device_id() != device_id) {
    return nullptr;
  }
  return &*found;
}

bool ResourceVector::empty() const noexcept {
  return host_normal_bytes() == 0U && host_pinned_bytes() == 0U && host_hugepage_bytes() == 0U &&
         shared_host_bytes() == 0U && spool_bytes() == 0U && transport_bytes() == 0U && descriptor_count() == 0U &&
         async_token_count() == 0U && cpu_leaf_permits() == 0U && backend_gang_permits() == 0U &&
         provider_private_permits() == 0U && io_slots() == 0U && devices_.empty();
}

bool ResourceVector::fits_within(const ResourceVector& capacity) const noexcept {
  if (host_normal_bytes() > capacity.host_normal_bytes() || host_pinned_bytes() > capacity.host_pinned_bytes() ||
      host_hugepage_bytes() > capacity.host_hugepage_bytes() || shared_host_bytes() > capacity.shared_host_bytes() ||
      spool_bytes() > capacity.spool_bytes() || transport_bytes() > capacity.transport_bytes() ||
      descriptor_count() > capacity.descriptor_count() || async_token_count() > capacity.async_token_count() ||
      cpu_leaf_permits() > capacity.cpu_leaf_permits() || backend_gang_permits() > capacity.backend_gang_permits() ||
      provider_private_permits() > capacity.provider_private_permits() || io_slots() > capacity.io_slots()) {
    return false;
  }
  for (const auto& demand_device : devices_) {
    const auto* capacity_device = capacity.find_device(demand_device.device_id());
    if (capacity_device == nullptr || demand_device.device_bytes() > capacity_device->device_bytes() ||
        demand_device.gpu_stream_slots() > capacity_device->gpu_stream_slots() ||
        demand_device.copy_engine_slots() > capacity_device->copy_engine_slots()) {
      return false;
    }
  }
  return true;
}

Result<ResourceVectorCapacity> ResourceVectorCapacity::create(const ResourceVectorCapacitySpec& specification,
                                                              const std::string_view field_name) {
  auto domains = ResourceVector::create(specification.domains, field(field_name, "domains"));
  if (!domains.ok()) {
    return domains.status();
  }
  auto host_total_cap =
    canonical_quantity(specification.host_total_cap_bytes, field(field_name, "host_total_cap_bytes"));
  if (!host_total_cap.ok()) {
    return host_total_cap.status();
  }
  if (domains.value().host_total_bytes() > host_total_cap.value().value()) {
    return validation(field(field_name, "host_total_cap_bytes") +
                      " must cover host_normal + host_pinned + host_hugepage + shared_host capacity.");
  }
  return ResourceVectorCapacity{std::move(domains).value(), std::move(host_total_cap).value()};
}

bool ResourceVectorCapacity::can_admit(const ResourceVector& demand) const noexcept {
  return demand.host_total_bytes() <= host_total_cap_bytes() && demand.fits_within(domains_);
}

} // namespace ksj::recon
