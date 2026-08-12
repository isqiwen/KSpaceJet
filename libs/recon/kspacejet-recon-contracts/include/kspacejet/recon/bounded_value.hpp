#pragma once

#include "kspacejet/recon/result.hpp"

#include <cstdint>
#include <string_view>

namespace ksj::recon {

using Quantity = std::uint64_t;

// RFC 8785 canonical JSON numbers must be exactly representable by an IEEE
// 754 binary64 implementation.  Every value that can reach a v1 artifact is
// therefore constrained to this inclusive bound, even when the in-memory
// arithmetic implementation uses uint64_t.
inline constexpr Quantity kMaxCanonicalJsonInteger = 9'007'199'254'740'991ULL;

class CanonicalQuantity final {
public:
  [[nodiscard]] static Result<CanonicalQuantity> create(Quantity value, std::string_view field_name);

  [[nodiscard]] constexpr Quantity value() const noexcept { return value_; }

  friend constexpr bool operator==(CanonicalQuantity, CanonicalQuantity) noexcept = default;

private:
  explicit constexpr CanonicalQuantity(const Quantity value) noexcept : value_(value) {}

  Quantity value_ = 0;
};

// Checked operations intentionally return a raw Quantity.  The caller must
// subsequently name the receiving artifact field through CanonicalQuantity::create,
// which keeps diagnostics precise while maintaining the canonical bound.
[[nodiscard]] Result<Quantity> checked_add(Quantity lhs, Quantity rhs, std::string_view expression_name);
[[nodiscard]] Result<Quantity> checked_multiply(Quantity lhs, Quantity rhs, std::string_view expression_name);
[[nodiscard]] Result<Quantity> checked_ceil_divide(Quantity numerator, Quantity denominator,
                                                   std::string_view expression_name);

class Capacity final {
public:
  [[nodiscard]] static Result<Capacity> create(Quantity max_items, Quantity max_charged_bytes,
                                               std::string_view field_name);

  [[nodiscard]] constexpr Quantity max_items() const noexcept { return max_items_.value(); }
  [[nodiscard]] constexpr Quantity max_charged_bytes() const noexcept { return max_charged_bytes_.value(); }

  friend constexpr bool operator==(const Capacity&, const Capacity&) noexcept = default;

private:
  Capacity(CanonicalQuantity max_items, CanonicalQuantity max_charged_bytes) noexcept
      : max_items_(max_items), max_charged_bytes_(max_charged_bytes) {}

  CanonicalQuantity max_items_;
  CanonicalQuantity max_charged_bytes_;
};

} // namespace ksj::recon
