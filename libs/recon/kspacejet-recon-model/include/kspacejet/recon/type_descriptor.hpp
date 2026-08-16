#pragma once

#include "kspacejet/recon/artifact_digest.hpp"
#include "kspacejet/recon/bounded_value.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {

// A canonical, human-readable reference to one immutable registry type.
// `ksj.image-frame`, for example, names one exact payload meaning.  A
// semantic change requires a new TypeRef; it must never silently reuse an
// existing name merely because its byte layout still happens to fit.
class TypeRef final {
public:
  [[nodiscard]] static Result<TypeRef> parse(std::string_view value, std::string_view field_name = "type_ref");

  [[nodiscard]] const std::string& value() const noexcept { return value_; }

  friend bool operator==(const TypeRef&, const TypeRef&) noexcept = default;

private:
  explicit TypeRef(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

// A frozen description of the payload carried by a resolved operator port.
//
// A TypeRef alone is deliberately not compatible: its resolved layout, shape
// meaning, memory-domain eligibility, and mutability are all part of the
// type. TypeDescriptor::create derives type_identity_digest automatically
// from this complete structural definition. The compiler must therefore use
// exactly_matches() rather than a prefix or "convert if
// convenient" test. Any conversion is an explicit adapter/transfer
// occurrence in a later ExecutionPlan.
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
  // This readable source value is the only type identity authored by normal
  // callers. The checked-in registry is responsible for deciding which
  // TypeRefs may appear in Provider-owned contracts.
  std::string type_ref;
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
};

class TypeDescriptor final {
public:
  // Deterministically constructs a resolved descriptor and derives its
  // type_identity_digest from the canonical structural preimage. Registry
  // factories call this; normal Provider contracts resolve a TypeRef through
  // `types::resolve` instead of constructing descriptors themselves.
  [[nodiscard]] static Result<TypeDescriptor> create(const TypeDescriptorSpec& specification,
                                                     std::string_view field_name = "type_descriptor");

  [[nodiscard]] const TypeRef& type_ref() const noexcept { return type_ref_; }
  [[nodiscard]] const ArtifactDigest& type_identity_digest() const noexcept { return type_identity_digest_; }
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

  // This is intentionally the only compatibility predicate in this model. It is
  // exact structural equality after canonical validation/sorting of the
  // memory-domain set; no implicit layout or memory-domain conversion exists.
  [[nodiscard]] bool exactly_matches(const TypeDescriptor& other) const noexcept { return *this == other; }

  friend bool operator==(const TypeDescriptor&, const TypeDescriptor&) noexcept = default;

private:
  TypeDescriptor(TypeRef type_ref, ArtifactDigest type_identity_digest, PayloadKind payload_kind,
                 ElementType element_type, CanonicalQuantity rank, std::vector<std::string> dimensions,
                 LayoutKind layout, StrideKind strides, std::vector<Quantity> explicit_byte_strides,
                 std::vector<TypeMemoryDomain> allowed_memory_domains, CanonicalQuantity min_alignment_bytes,
                 PayloadMutability mutability) noexcept
      : type_ref_(std::move(type_ref)), type_identity_digest_(std::move(type_identity_digest)),
        payload_kind_(payload_kind), element_type_(element_type), rank_(rank), dimensions_(std::move(dimensions)),
        layout_(layout), strides_(strides), explicit_byte_strides_(std::move(explicit_byte_strides)),
        allowed_memory_domains_(std::move(allowed_memory_domains)), min_alignment_bytes_(min_alignment_bytes),
        mutability_(mutability) {}

  TypeRef type_ref_;
  ArtifactDigest type_identity_digest_;
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
};

} // namespace ksj::recon
