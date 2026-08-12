#pragma once

#include "kspacejet/recon/execution_plan.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {

// A frozen description of the payload carried by a resolved operator port.
//
// A type id alone is deliberately not compatible: layout, shape meaning,
// payload schema, memory-domain eligibility, and mutability are all part of
// the type.  The compiler must therefore use exactly_matches() rather than a
// prefix, revision-range, or "convert if convenient" test.  Any conversion is
// an explicit adapter/transfer occurrence in a later ExecutionPlan.
enum class PayloadKind {
  buffer_handle,
  message_handle,
  control_token,
  opaque_handle,
};

enum class ElementType {
  none,
  uint8,
  int16,
  uint16,
  int32,
  uint32,
  float32,
  float64,
  complex_int16,
  complex_float32,
  complex_float64,
};

enum class LayoutKind {
  canonical_contiguous,
  channel_major_contiguous,
  row_major_contiguous,
  column_major_contiguous,
  opaque,
};

enum class StrideKind {
  canonical,
  explicit_byte_strides,
};

// This enum belongs to the payload ABI, not the legacy MachinePolicy memory
// preference enum.  In particular, cuda_device is an exact payload-location
// constraint; it does not imply that an arbitrary "device" allocation is
// compatible.
enum class TypeMemoryDomain {
  host_normal,
  host_pinned,
  host_hugepage,
  shared_host,
  cuda_device,
};

enum class PayloadMutability {
  immutable_after_publish,
  mutable_exclusive,
};

[[nodiscard]] std::string_view to_string(PayloadKind value) noexcept;
[[nodiscard]] std::string_view to_string(ElementType value) noexcept;
[[nodiscard]] std::string_view to_string(LayoutKind value) noexcept;
[[nodiscard]] std::string_view to_string(StrideKind value) noexcept;
[[nodiscard]] std::string_view to_string(TypeMemoryDomain value) noexcept;
[[nodiscard]] std::string_view to_string(PayloadMutability value) noexcept;

struct TypeDescriptorSpec {
  std::string type_id;
  Quantity revision = 0;
  std::string payload_schema_digest;
  PayloadKind payload_kind = PayloadKind::buffer_handle;
  ElementType element_type = ElementType::none;
  Quantity rank = 0;
  std::vector<std::string> dimensions;
  LayoutKind layout = LayoutKind::canonical_contiguous;
  StrideKind strides = StrideKind::canonical;
  std::vector<Quantity> explicit_byte_strides;
  std::vector<TypeMemoryDomain> allowed_memory_domains;
  Quantity min_alignment_bytes = 0;
  PayloadMutability mutability = PayloadMutability::immutable_after_publish;
  std::string metadata_schema_digest;
};

class TypeDescriptor final {
public:
  [[nodiscard]] static Result<TypeDescriptor> create(const TypeDescriptorSpec& specification,
                                                     std::string_view field_name = "type_descriptor");

  [[nodiscard]] const std::string& type_id() const noexcept { return type_id_; }
  [[nodiscard]] constexpr Quantity revision() const noexcept { return revision_.value(); }
  [[nodiscard]] const ArtifactDigest& payload_schema_digest() const noexcept { return payload_schema_digest_; }
  [[nodiscard]] constexpr PayloadKind payload_kind() const noexcept { return payload_kind_; }
  [[nodiscard]] constexpr ElementType element_type() const noexcept { return element_type_; }
  [[nodiscard]] constexpr Quantity rank() const noexcept { return rank_.value(); }
  [[nodiscard]] const std::vector<std::string>& dimensions() const noexcept { return dimensions_; }
  [[nodiscard]] constexpr LayoutKind layout() const noexcept { return layout_; }
  [[nodiscard]] constexpr StrideKind strides() const noexcept { return strides_; }
  [[nodiscard]] const std::vector<Quantity>& explicit_byte_strides() const noexcept { return explicit_byte_strides_; }
  [[nodiscard]] const std::vector<TypeMemoryDomain>& allowed_memory_domains() const noexcept {
    return allowed_memory_domains_;
  }
  [[nodiscard]] constexpr Quantity min_alignment_bytes() const noexcept { return min_alignment_bytes_.value(); }
  [[nodiscard]] constexpr PayloadMutability mutability() const noexcept { return mutability_; }
  [[nodiscard]] const ArtifactDigest& metadata_schema_digest() const noexcept { return metadata_schema_digest_; }

  // This is intentionally the only compatibility predicate in v1.  It is
  // exact structural equality after canonical validation/sorting of the
  // memory-domain set; no implicit layout or memory-domain conversion exists.
  [[nodiscard]] bool exactly_matches(const TypeDescriptor& other) const noexcept { return *this == other; }

  friend bool operator==(const TypeDescriptor&, const TypeDescriptor&) noexcept = default;

private:
  TypeDescriptor(std::string type_id, CanonicalQuantity revision, ArtifactDigest payload_schema_digest,
                 PayloadKind payload_kind, ElementType element_type, CanonicalQuantity rank,
                 std::vector<std::string> dimensions, LayoutKind layout, StrideKind strides,
                 std::vector<Quantity> explicit_byte_strides, std::vector<TypeMemoryDomain> allowed_memory_domains,
                 CanonicalQuantity min_alignment_bytes, PayloadMutability mutability,
                 ArtifactDigest metadata_schema_digest) noexcept
      : type_id_(std::move(type_id)), revision_(revision), payload_schema_digest_(std::move(payload_schema_digest)),
        payload_kind_(payload_kind), element_type_(element_type), rank_(rank), dimensions_(std::move(dimensions)),
        layout_(layout), strides_(strides), explicit_byte_strides_(std::move(explicit_byte_strides)),
        allowed_memory_domains_(std::move(allowed_memory_domains)), min_alignment_bytes_(min_alignment_bytes),
        mutability_(mutability), metadata_schema_digest_(std::move(metadata_schema_digest)) {}

  std::string type_id_;
  CanonicalQuantity revision_;
  ArtifactDigest payload_schema_digest_;
  PayloadKind payload_kind_;
  ElementType element_type_;
  CanonicalQuantity rank_;
  std::vector<std::string> dimensions_;
  LayoutKind layout_;
  StrideKind strides_;
  std::vector<Quantity> explicit_byte_strides_;
  std::vector<TypeMemoryDomain> allowed_memory_domains_;
  CanonicalQuantity min_alignment_bytes_;
  PayloadMutability mutability_;
  ArtifactDigest metadata_schema_digest_;
};

} // namespace ksj::recon
