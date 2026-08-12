#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace ksj::base {

template <typename Tag> class StrongId {
public:
  using value_type = std::uint64_t;

  constexpr StrongId() noexcept = default;
  explicit constexpr StrongId(value_type value) noexcept : value_(value) {}

  [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_ != 0; }

  friend constexpr bool operator==(StrongId lhs, StrongId rhs) noexcept = default;
  friend constexpr auto operator<=>(StrongId lhs, StrongId rhs) noexcept = default;

private:
  value_type value_{};
};

struct SessionIdTag {};
struct WorkerIdTag {};
struct MeasurementIdTag {};
struct FrameIdTag {};
struct TraceIdTag {};
struct PluginIdTag {};

using SessionId = StrongId<SessionIdTag>;
using WorkerId = StrongId<WorkerIdTag>;
using MeasurementId = StrongId<MeasurementIdTag>;
using FrameId = StrongId<FrameIdTag>;
using TraceId = StrongId<TraceIdTag>;
using PluginId = StrongId<PluginIdTag>;

} // namespace ksj::base

namespace std {
template <typename Tag> struct hash<ksj::base::StrongId<Tag>> {
  size_t operator()(const ksj::base::StrongId<Tag>& id) const noexcept {
    return std::hash<typename ksj::base::StrongId<Tag>::value_type>{}(id.value());
  }
};
} // namespace std
