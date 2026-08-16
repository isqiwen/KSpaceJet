// SPDX-License-Identifier: Apache-2.0
//
// C++-only, Provider-private convenience helpers for the public C ABI.
//
// This header does not add an ABI surface or a runtime dependency.  It keeps
// the repetitive defensive checks that a C++ Provider performs at its ABI
// boundary in one place.  Provider algorithms and Provider-specific parsing
// deliberately do not belong here.

#pragma once

#include "kspacejet/provider/provider.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <system_error>

namespace ksj::provider::detail {

[[nodiscard]] inline ksj_provider_abi_header make_header(const std::uint32_t struct_size,
                                                         const std::uint64_t capability_bits = 0U) noexcept {
  return ksj_provider_abi_header_make(struct_size, capability_bits);
}

[[nodiscard]] inline bool has_compatible_header(const ksj_provider_abi_header* const header,
                                                const std::size_t required_size) noexcept {
  return header != nullptr && header->struct_size >= required_size && header->reserved0 == 0U &&
         header->reserved[0U] == 0U && header->reserved[1U] == 0U;
}

template <typename T> [[nodiscard]] inline bool has_full_compatible_header(const T* const value) noexcept {
  return value != nullptr && has_compatible_header(&value->abi, sizeof(T));
}

[[nodiscard]] inline ksj_utf8_view make_utf8_view(const char* const data, const std::uint64_t size) noexcept {
  ksj_utf8_view view{};
  view.abi = make_header(sizeof(view));
  view.data = data;
  view.size = size;
  return view;
}

template <std::size_t N> [[nodiscard]] inline ksj_utf8_view make_utf8_view(const char (&text)[N]) noexcept {
  static_assert(N > 0U, "A string literal includes its terminating null byte.");
  return make_utf8_view(text, static_cast<std::uint64_t>(N - 1U));
}

[[nodiscard]] inline bool text_equals(const ksj_utf8_view& view, const char* const expected,
                                      const std::size_t expected_size) noexcept {
  return has_full_compatible_header(&view) && view.data != nullptr && expected != nullptr &&
         view.size == expected_size && (expected_size == 0U || std::memcmp(view.data, expected, expected_size) == 0);
}

template <std::size_t N>
[[nodiscard]] inline bool text_equals(const ksj_utf8_view& view, const char (&expected)[N]) noexcept {
  static_assert(N > 0U, "A string literal includes its terminating null byte.");
  return text_equals(view, expected, N - 1U);
}

[[nodiscard]] constexpr std::uint8_t hex_nibble(const char value) noexcept {
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<std::uint8_t>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<std::uint8_t>(value - 'A' + 10);
  }
  return 0U;
}

[[nodiscard]] constexpr bool is_hexadecimal(const char value) noexcept {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

// A malformed literal yields a zero-initialized digest with an invalid ABI
// header.  Providers only construct this from their own fixed bundle literal;
// callers can reject a malformed result with has_full_compatible_header().
[[nodiscard]] inline ksj_digest256 make_bundle_digest_from_hex(const std::string_view hex) noexcept {
  ksj_digest256 digest{};
  if (hex.size() != KSJ_PROVIDER_DIGEST256_SIZE * 2U) {
    return digest;
  }

  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    const std::size_t offset = static_cast<std::size_t>(index) * 2U;
    if (!is_hexadecimal(hex[offset]) || !is_hexadecimal(hex[offset + 1U])) {
      return {};
    }
    digest.bytes[index] = static_cast<std::uint8_t>((hex_nibble(hex[offset]) << 4U) | hex_nibble(hex[offset + 1U]));
  }

  digest.abi = make_header(sizeof(digest));
  return digest;
}

template <std::size_t N> [[nodiscard]] inline ksj_digest256 make_bundle_digest_from_hex(const char (&hex)[N]) noexcept {
  static_assert(N == KSJ_PROVIDER_DIGEST256_SIZE * 2U + 1U,
                "A SHA-256 bundle digest literal must contain 64 hexadecimal characters.");
  return make_bundle_digest_from_hex(std::string_view{hex, N - 1U});
}

[[nodiscard]] inline bool has_valid_digest(const ksj_digest256& digest) noexcept {
  return has_full_compatible_header(&digest);
}

inline void set_error(ksj_error_view* const out_error, const ksj_status status, const char* const message,
                      const std::uint64_t message_size) noexcept {
  if (!has_full_compatible_header(out_error)) {
    return;
  }

  *out_error = {};
  out_error->abi = make_header(sizeof(*out_error));
  out_error->status = status;
  out_error->category = 0U;
  out_error->provider_error_code = 0U;
  out_error->message = make_utf8_view(message, message_size);
}

template <std::size_t N>
inline void set_error(ksj_error_view* const out_error, const ksj_status status, const char (&message)[N]) noexcept {
  static_assert(N > 0U, "A string literal includes its terminating null byte.");
  set_error(out_error, status, message, static_cast<std::uint64_t>(N - 1U));
}

[[nodiscard]] inline ksj_status reject(ksj_error_view* const out_error, const ksj_status status,
                                       const char* const message, const std::uint64_t message_size) noexcept {
  set_error(out_error, status, message, message_size);
  return status;
}

template <std::size_t N>
[[nodiscard]] inline ksj_status reject(ksj_error_view* const out_error, const ksj_status status,
                                       const char (&message)[N]) noexcept {
  static_assert(N > 0U, "A string literal includes its terminating null byte.");
  set_error(out_error, status, message);
  return status;
}

[[nodiscard]] inline bool valid_borrowed_bytes(const ksj_byte_view& bytes) noexcept {
  return has_full_compatible_header(&bytes) && (bytes.size == 0U || bytes.data != nullptr);
}

[[nodiscard]] inline bool has_valid_type_descriptor(const ksj_type_descriptor_view& type) noexcept {
  if (!has_full_compatible_header(&type) || !has_full_compatible_header(&type.type_ref) ||
      type.type_ref.data == nullptr || type.type_ref.size == 0U || !has_valid_digest(type.type_identity_digest) ||
      type.rank > 8U || type.minimum_alignment == 0U || type.reserved0 != 0U ||
      (type.rank != 0U && type.dimension_names == nullptr)) {
    return false;
  }

  if ((type.minimum_alignment & (type.minimum_alignment - 1U)) != 0U) {
    return false;
  }

  for (std::uint32_t index = 0U; index < type.rank; ++index) {
    const ksj_utf8_view& dimension_name = type.dimension_names[index];
    if (!has_full_compatible_header(&dimension_name) || dimension_name.data == nullptr || dimension_name.size == 0U) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr bool is_power_of_two(const std::uint32_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] inline bool is_aligned(const void* const pointer, const std::uint32_t alignment) noexcept {
  return pointer != nullptr && is_power_of_two(alignment) &&
         (reinterpret_cast<std::uintptr_t>(pointer) & (static_cast<std::uintptr_t>(alignment) - 1U)) == 0U;
}

[[nodiscard]] inline bool has_usable_host_memory(const void* const pointer, const std::uint32_t declared_alignment,
                                                 const std::uint32_t required_alignment) noexcept {
  return is_power_of_two(required_alignment) && declared_alignment >= required_alignment &&
         is_aligned(pointer, declared_alignment);
}

[[nodiscard]] inline bool parse_canonical_positive_u32(const char* const first, const char* const last,
                                                       std::uint32_t& output) noexcept {
  if (first == nullptr || last == nullptr || first == last || *first == '0') {
    return false;
  }

  std::uint32_t value = 0U;
  const auto parsed = std::from_chars(first, last, value);
  if (parsed.ec != std::errc{} || parsed.ptr != last || value == 0U) {
    return false;
  }
  output = value;
  return true;
}

[[nodiscard]] inline bool parse_canonical_positive_u32(const std::string_view text, std::uint32_t& output) noexcept {
  if (text.empty()) {
    return false;
  }
  return parse_canonical_positive_u32(text.data(), text.data() + text.size(), output);
}

[[nodiscard]] inline bool checked_multiply(const std::uint64_t lhs, const std::uint64_t rhs,
                                           std::uint64_t& output) noexcept {
  if (lhs != 0U && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
    return false;
  }
  output = lhs * rhs;
  return true;
}

[[nodiscard]] inline bool has_usable_firing_callbacks(
  const ksj_firing_lease_callbacks* const callbacks,
  const std::uint64_t required_capabilities = KSJ_LEASE_CAP_INPUT_BATCHES | KSJ_LEASE_CAP_OUTPUT_GRANTS) noexcept {
  if (!has_full_compatible_header(callbacks) || callbacks->get_info == nullptr ||
      (callbacks->abi.capability_bits & required_capabilities) != required_capabilities) {
    return false;
  }

  const std::uint64_t capabilities = callbacks->abi.capability_bits;
  if ((capabilities & KSJ_LEASE_CAP_INPUT_BATCHES) != 0U && callbacks->get_input_batch == nullptr) {
    return false;
  }
  if ((capabilities & KSJ_LEASE_CAP_OUTPUT_GRANTS) != 0U) {
    if (callbacks->acquire_output_grant == nullptr || callbacks->output_grants == nullptr ||
        !has_full_compatible_header(callbacks->output_grants) ||
        callbacks->output_grants->map_mutable_payload == nullptr || callbacks->output_grants->seal == nullptr ||
        callbacks->output_grants->release == nullptr) {
      return false;
    }
  }
  if ((capabilities & KSJ_LEASE_CAP_SCRATCH) != 0U && callbacks->get_scratch == nullptr) {
    return false;
  }
  if ((capabilities & KSJ_LEASE_CAP_KEY_STATE) != 0U && callbacks->get_key_state == nullptr) {
    return false;
  }
  if ((capabilities & KSJ_LEASE_CAP_RETENTION) != 0U &&
      (callbacks->retain_input == nullptr || callbacks->release_retention == nullptr)) {
    return false;
  }
  if ((capabilities & KSJ_LEASE_CAP_ASYNC) != 0U &&
      (callbacks->register_async == nullptr || callbacks->complete_async == nullptr ||
       callbacks->release_async == nullptr)) {
    return false;
  }
  return (capabilities & KSJ_LEASE_CAP_CANCELLATION) == 0U || callbacks->get_cancellation != nullptr;
}

inline void write_done(ksj_process_result* const out_result, const std::uint32_t sealed_output_count,
                       const std::uint64_t consumed_input_item_count, const std::uint64_t terminal_epoch) noexcept {
  if (!has_full_compatible_header(out_result)) {
    return;
  }

  *out_result = {};
  out_result->abi = make_header(sizeof(*out_result));
  out_result->outcome = KSJ_PROVIDER_PROCESS_DONE;
  out_result->sealed_output_count = sealed_output_count;
  out_result->consumed_input_item_count = consumed_input_item_count;
  out_result->terminal_epoch = terminal_epoch;
  out_result->async_token = nullptr;
}

struct OutputGrantGuard {
  const ksj_output_grant_callbacks* callbacks{nullptr};
  ksj_output_grant* grant{nullptr};
  bool settled{false};

  ~OutputGrantGuard() noexcept {
    if (callbacks == nullptr || grant == nullptr || settled || callbacks->release == nullptr) {
      return;
    }

    ksj_error_view error{};
    error.abi = make_header(sizeof(error));
    error.message.abi = make_header(sizeof(error.message));
    (void)callbacks->release(callbacks->host_context, grant, &error);
  }
};

} // namespace ksj::provider::detail
