#include "kspacejet/recon/type_descriptor.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ksj::recon {
namespace {

using Json = nlohmann::json;

inline constexpr std::string_view kTypeIdentityDomain = "kspacejet.type-identity";

// recon-model cannot depend on recon-graph without reversing the dependency
// direction, so this deliberately local SHA-256 implementation derives the
// model-owned TypeDescriptor identity preimage.

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

[[nodiscard]] bool is_valid_type_ref(const std::string_view value) noexcept {
  return is_identifier(value);
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

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants{
  0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
  0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
  0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
  0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
  0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
  0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
  0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
  0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] constexpr std::uint32_t rotr(const std::uint32_t value, const unsigned int shift) noexcept {
  return std::rotr(value, shift);
}

void sha256_transform(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t index = 0U; index < 16U; ++index) {
    const auto offset = index * 4U;
    words[index] =
      (static_cast<std::uint32_t>(block[offset]) << 24U) | (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
      (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) | static_cast<std::uint32_t>(block[offset + 3U]);
  }
  for (std::size_t index = 16U; index < words.size(); ++index) {
    const auto sigma0 = rotr(words[index - 15U], 7U) ^ rotr(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
    const auto sigma1 = rotr(words[index - 2U], 17U) ^ rotr(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + sigma0 + words[index - 7U] + sigma1;
  }

  auto a = state[0];
  auto b = state[1];
  auto c = state[2];
  auto d = state[3];
  auto e = state[4];
  auto f = state[5];
  auto g = state[6];
  auto h = state[7];
  for (std::size_t index = 0U; index < words.size(); ++index) {
    const auto sigma1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
    const auto choose = (e & f) ^ (~e & g);
    const auto temp1 = h + sigma1 + choose + kSha256RoundConstants[index] + words[index];
    const auto sigma0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    const auto temp2 = sigma0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

[[nodiscard]] std::array<std::uint8_t, 32> sha256_bytes(const std::string_view input) {
  std::array<std::uint32_t, 8> state{
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  };
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(input.data());
  std::size_t offset = 0U;
  while (input.size() - offset >= 64U) {
    sha256_transform(state, bytes + offset);
    offset += 64U;
  }
  std::array<std::uint8_t, 128> tail{};
  const auto remaining = input.size() - offset;
  for (std::size_t index = 0U; index < remaining; ++index) {
    tail[index] = bytes[offset + index];
  }
  tail[remaining] = 0x80U;
  const auto padding_block_count = remaining >= 56U ? 2U : 1U;
  const auto bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
  const auto length_offset = (padding_block_count * 64U) - 8U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    tail[length_offset + index] = static_cast<std::uint8_t>(bit_length >> ((7U - index) * 8U));
  }
  sha256_transform(state, tail.data());
  if (padding_block_count == 2U) {
    sha256_transform(state, tail.data() + 64U);
  }

  std::array<std::uint8_t, 32> digest{};
  for (std::size_t index = 0U; index < state.size(); ++index) {
    const auto word = state[index];
    digest[index * 4U] = static_cast<std::uint8_t>(word >> 24U);
    digest[index * 4U + 1U] = static_cast<std::uint8_t>(word >> 16U);
    digest[index * 4U + 2U] = static_cast<std::uint8_t>(word >> 8U);
    digest[index * 4U + 3U] = static_cast<std::uint8_t>(word);
  }
  return digest;
}

[[nodiscard]] std::string hex_digest(const std::array<std::uint8_t, 32>& bytes) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto byte : bytes) {
    stream << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return stream.str();
}

[[nodiscard]] Result<ArtifactDigest> derive_type_identity_digest(
  const TypeRef& type_ref, const PayloadKind payload_kind, const ElementType element_type, const Quantity rank,
  const std::vector<std::string>& dimensions, const LayoutKind layout, const StrideKind strides,
  const std::vector<Quantity>& explicit_byte_strides, const std::vector<TypeMemoryDomain>& allowed_memory_domains,
  const Quantity min_alignment_bytes, const PayloadMutability mutability, const std::string_view field_name) {
  Json domains = Json::array();
  for (const auto domain : allowed_memory_domains) {
    domains.push_back(to_string(domain));
  }
  Json byte_strides = Json::array();
  for (const auto stride : explicit_byte_strides) {
    byte_strides.push_back(stride);
  }
  const Json structural{
    {"allowed_memory_domains", std::move(domains)},
    {"dimensions", dimensions},
    {"element_type", to_string(element_type)},
    {"explicit_byte_strides", std::move(byte_strides)},
    {"layout", to_string(layout)},
    {"min_alignment_bytes", min_alignment_bytes},
    {"mutability", to_string(mutability)},
    {"payload_kind", to_string(payload_kind)},
    {"rank", rank},
    {"strides", to_string(strides)},
    {"type_ref", type_ref.value()},
  };
  return derive_domain_separated_sha256_digest(
    kTypeIdentityDomain, structural.dump(-1, ' ', false, Json::error_handler_t::strict), field_name);
}

} // namespace

Result<ArtifactDigest> derive_domain_separated_sha256_digest(const std::string_view domain,
                                                             const std::string_view canonical_document,
                                                             const std::string_view field_name) {
  if (domain.empty()) {
    return Status::InvalidArgument("digest domain must not be empty");
  }
  std::string preimage;
  preimage.reserve(domain.size() + 1U + canonical_document.size());
  preimage.append(domain);
  preimage.push_back('\0');
  preimage.append(canonical_document);
  return ArtifactDigest::parse("sha256:" + hex_digest(sha256_bytes(preimage)), field_name);
}

Result<ArtifactDigest> derive_canonical_config_digest(const std::string_view canonical_config,
                                                      const std::string_view field_name) {
  if (canonical_config.empty()) {
    return validation(std::string(field_name) + " must contain canonical JSON bytes.");
  }
  return derive_domain_separated_sha256_digest(kOperatorConfigDigestDomain, canonical_config, field_name);
}

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

Result<TypeRef> TypeRef::parse(const std::string_view value, const std::string_view field_name) {
  if (!is_valid_type_ref(value)) {
    return validation(std::string(field_name) + " must be a canonical lower-case TypeRef such as 'ksj.image-frame'.");
  }
  return TypeRef(std::string(value));
}

Result<TypeDescriptor> TypeDescriptor::create(const TypeDescriptorSpec& specification,
                                              const std::string_view field_name) {
  auto type_ref = TypeRef::parse(specification.type_ref, field(field_name, "type_ref"));
  if (!type_ref.ok()) {
    return type_ref.status();
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

  auto identity = derive_type_identity_digest(type_ref.value(), specification.payload_kind, specification.element_type,
                                              rank.value().value(), specification.dimensions, specification.layout,
                                              specification.strides, specification.explicit_byte_strides,
                                              allowed_memory_domains, min_alignment.value().value(),
                                              specification.mutability, field(field_name, "type_identity_digest"));
  if (!identity.ok()) {
    return identity.status();
  }

  return TypeDescriptor{std::move(type_ref).value(),
                        std::move(identity).value(),
                        specification.payload_kind,
                        specification.element_type,
                        std::move(rank).value(),
                        specification.dimensions,
                        specification.layout,
                        specification.strides,
                        specification.explicit_byte_strides,
                        std::move(allowed_memory_domains),
                        std::move(min_alignment).value(),
                        specification.mutability};
}

} // namespace ksj::recon
