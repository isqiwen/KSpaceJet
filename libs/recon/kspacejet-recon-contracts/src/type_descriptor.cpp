#include "kspacejet/recon/type_descriptor.hpp"

#include <algorithm>
#include <bit>
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

[[nodiscard]] bool is_identifier(const std::string_view value) noexcept {
  if (value.empty() || value.front() < 'a' || value.front() > 'z') {
    return false;
  }
  for (const char character : value) {
    const bool lower_alpha = character >= 'a' && character <= 'z';
    const bool digit = character >= '0' && character <= '9';
    if (!lower_alpha && !digit && character != '.' && character != '_' && character != '-') {
      return false;
    }
  }
  return value.back() != '.' && value.back() != '_' && value.back() != '-';
}

template <typename T> [[nodiscard]] bool contains_only_unique_values(const std::vector<T>& values) {
  for (std::size_t first = 0; first < values.size(); ++first) {
    for (std::size_t second = first + 1U; second < values.size(); ++second) {
      if (values[first] == values[second]) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool is_valid(const PayloadKind value) noexcept {
  switch (value) {
    case PayloadKind::buffer_handle:
    case PayloadKind::message_handle:
    case PayloadKind::control_token:
    case PayloadKind::opaque_handle:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_valid(const ElementType value) noexcept {
  switch (value) {
    case ElementType::none:
    case ElementType::uint8:
    case ElementType::int16:
    case ElementType::uint16:
    case ElementType::int32:
    case ElementType::uint32:
    case ElementType::float32:
    case ElementType::float64:
    case ElementType::complex_int16:
    case ElementType::complex_float32:
    case ElementType::complex_float64:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_valid(const LayoutKind value) noexcept {
  switch (value) {
    case LayoutKind::canonical_contiguous:
    case LayoutKind::channel_major_contiguous:
    case LayoutKind::row_major_contiguous:
    case LayoutKind::column_major_contiguous:
    case LayoutKind::opaque:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_valid(const StrideKind value) noexcept {
  return value == StrideKind::canonical || value == StrideKind::explicit_byte_strides;
}

[[nodiscard]] bool is_valid(const TypeMemoryDomain value) noexcept {
  switch (value) {
    case TypeMemoryDomain::host_normal:
    case TypeMemoryDomain::host_pinned:
    case TypeMemoryDomain::host_hugepage:
    case TypeMemoryDomain::shared_host:
    case TypeMemoryDomain::cuda_device:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_valid(const PayloadMutability value) noexcept {
  return value == PayloadMutability::immutable_after_publish || value == PayloadMutability::mutable_exclusive;
}

[[nodiscard]] Result<CanonicalQuantity> required_quantity(const Quantity value, const std::string_view field_name) {
  if (value == 0) {
    return validation(std::string(field_name) + " must be greater than zero.");
  }
  return CanonicalQuantity::create(value, field_name);
}

} // namespace

std::string_view to_string(const PayloadKind value) noexcept {
  switch (value) {
    case PayloadKind::buffer_handle:
      return "buffer_handle";
    case PayloadKind::message_handle:
      return "message_handle";
    case PayloadKind::control_token:
      return "control_token";
    case PayloadKind::opaque_handle:
      return "opaque_handle";
  }
  return "unknown";
}

std::string_view to_string(const ElementType value) noexcept {
  switch (value) {
    case ElementType::none:
      return "none";
    case ElementType::uint8:
      return "uint8";
    case ElementType::int16:
      return "int16";
    case ElementType::uint16:
      return "uint16";
    case ElementType::int32:
      return "int32";
    case ElementType::uint32:
      return "uint32";
    case ElementType::float32:
      return "float32";
    case ElementType::float64:
      return "float64";
    case ElementType::complex_int16:
      return "complex_int16";
    case ElementType::complex_float32:
      return "complex_float32";
    case ElementType::complex_float64:
      return "complex_float64";
  }
  return "unknown";
}

std::string_view to_string(const LayoutKind value) noexcept {
  switch (value) {
    case LayoutKind::canonical_contiguous:
      return "canonical_contiguous";
    case LayoutKind::channel_major_contiguous:
      return "channel_major_contiguous";
    case LayoutKind::row_major_contiguous:
      return "row_major_contiguous";
    case LayoutKind::column_major_contiguous:
      return "column_major_contiguous";
    case LayoutKind::opaque:
      return "opaque";
  }
  return "unknown";
}

std::string_view to_string(const StrideKind value) noexcept {
  switch (value) {
    case StrideKind::canonical:
      return "canonical";
    case StrideKind::explicit_byte_strides:
      return "explicit_byte_strides";
  }
  return "unknown";
}

std::string_view to_string(const TypeMemoryDomain value) noexcept {
  switch (value) {
    case TypeMemoryDomain::host_normal:
      return "host_normal";
    case TypeMemoryDomain::host_pinned:
      return "host_pinned";
    case TypeMemoryDomain::host_hugepage:
      return "host_hugepage";
    case TypeMemoryDomain::shared_host:
      return "shared_host";
    case TypeMemoryDomain::cuda_device:
      return "cuda_device";
  }
  return "unknown";
}

std::string_view to_string(const PayloadMutability value) noexcept {
  switch (value) {
    case PayloadMutability::immutable_after_publish:
      return "immutable_after_publish";
    case PayloadMutability::mutable_exclusive:
      return "mutable_exclusive";
  }
  return "unknown";
}

Result<TypeDescriptor> TypeDescriptor::create(const TypeDescriptorSpec& specification,
                                              const std::string_view field_name) {
  if (!is_identifier(specification.type_id)) {
    return validation(field(field_name, "type_id") + " must be a lower-case identifier containing only [a-z0-9._-].");
  }
  auto revision = required_quantity(specification.revision, field(field_name, "revision"));
  if (!revision.ok()) {
    return revision.status();
  }
  auto abi_descriptor_digest =
    ArtifactDigest::parse(specification.abi_descriptor_digest, field(field_name, "abi_descriptor_digest"));
  if (!abi_descriptor_digest.ok()) {
    return abi_descriptor_digest.status();
  }
  auto payload_schema_digest =
    ArtifactDigest::parse(specification.payload_schema_digest, field(field_name, "payload_schema_digest"));
  if (!payload_schema_digest.ok()) {
    return payload_schema_digest.status();
  }
  auto metadata_schema_digest =
    ArtifactDigest::parse(specification.metadata_schema_digest, field(field_name, "metadata_schema_digest"));
  if (!metadata_schema_digest.ok()) {
    return metadata_schema_digest.status();
  }
  if (!is_valid(specification.payload_kind) || !is_valid(specification.element_type) ||
      !is_valid(specification.layout) || !is_valid(specification.strides) || !is_valid(specification.mutability)) {
    return validation(std::string(field_name) +
                      " contains an invalid payload, element, layout, stride, or mutability enum.");
  }
  if (specification.payload_kind == PayloadKind::buffer_handle && specification.element_type == ElementType::none) {
    return validation(field(field_name, "element_type") + " must not be none for a buffer_handle payload.");
  }
  if (specification.payload_kind == PayloadKind::control_token && specification.element_type != ElementType::none) {
    return validation(field(field_name, "element_type") + " must be none for a control_token payload.");
  }
  if (specification.rank != specification.dimensions.size()) {
    return validation(field(field_name, "dimensions") + " count must equal rank.");
  }
  auto rank = CanonicalQuantity::create(specification.rank, field(field_name, "rank"));
  if (!rank.ok()) {
    return rank.status();
  }
  if (!contains_only_unique_values(specification.dimensions)) {
    return validation(field(field_name, "dimensions") + " must contain unique names.");
  }
  for (std::size_t index = 0; index < specification.dimensions.size(); ++index) {
    if (!is_identifier(specification.dimensions[index])) {
      return validation(field(field_name, "dimensions[" + std::to_string(index) + "]") +
                        " must be a lower-case identifier containing only [a-z0-9._-].");
    }
  }
  if (specification.strides == StrideKind::canonical && !specification.explicit_byte_strides.empty()) {
    return validation(field(field_name, "explicit_byte_strides") + " must be empty when strides is canonical.");
  }
  if (specification.strides == StrideKind::explicit_byte_strides) {
    if (specification.rank == 0 || specification.explicit_byte_strides.size() != specification.rank) {
      return validation(field(field_name, "explicit_byte_strides") +
                        " must contain one non-zero byte stride per dimension.");
    }
    for (std::size_t index = 0; index < specification.explicit_byte_strides.size(); ++index) {
      auto stride = required_quantity(specification.explicit_byte_strides[index],
                                      field(field_name, "explicit_byte_strides[" + std::to_string(index) + "]"));
      if (!stride.ok()) {
        return stride.status();
      }
    }
  }
  if (specification.allowed_memory_domains.empty() ||
      !contains_only_unique_values(specification.allowed_memory_domains)) {
    return validation(field(field_name, "allowed_memory_domains") + " must contain unique domains.");
  }
  std::vector<TypeMemoryDomain> allowed_memory_domains = specification.allowed_memory_domains;
  for (const auto domain : allowed_memory_domains) {
    if (!is_valid(domain)) {
      return validation(field(field_name, "allowed_memory_domains") + " contains an invalid memory domain.");
    }
  }
  std::sort(allowed_memory_domains.begin(), allowed_memory_domains.end(),
            [](const TypeMemoryDomain left, const TypeMemoryDomain right) {
              return static_cast<unsigned int>(left) < static_cast<unsigned int>(right);
            });
  auto min_alignment = required_quantity(specification.min_alignment_bytes, field(field_name, "min_alignment_bytes"));
  if (!min_alignment.ok()) {
    return min_alignment.status();
  }
  if (!std::has_single_bit(min_alignment.value().value())) {
    return validation(field(field_name, "min_alignment_bytes") + " must be a power of two.");
  }

  return TypeDescriptor{specification.type_id,
                        std::move(revision).value(),
                        std::move(abi_descriptor_digest).value(),
                        std::move(payload_schema_digest).value(),
                        specification.payload_kind,
                        specification.element_type,
                        std::move(rank).value(),
                        specification.dimensions,
                        specification.layout,
                        specification.strides,
                        specification.explicit_byte_strides,
                        std::move(allowed_memory_domains),
                        std::move(min_alignment).value(),
                        specification.mutability,
                        std::move(metadata_schema_digest).value()};
}

} // namespace ksj::recon
