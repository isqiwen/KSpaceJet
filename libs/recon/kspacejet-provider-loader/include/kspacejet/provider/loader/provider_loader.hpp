#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kspacejet/base/result.hpp"
#include "kspacejet/provider/v1/provider.h"

namespace ksj::provider::loader {

/**
 * A copied digest advertised by a Provider v1 descriptor.
 *
 * The loader copies descriptor data out of the provider's borrowed C views.
 * The provider module is nevertheless kept loaded by ProviderLease while its
 * lifecycle function pointers can be used.
 */
using Digest256 = std::array<std::uint8_t, KSJ_PROVIDER_DIGEST256_SIZE>;

struct ProviderVersion {
  std::uint32_t major{0U};
  std::uint32_t minor{0U};
  std::uint32_t patch{0U};
  std::uint32_t prerelease{0U};

  [[nodiscard]] bool operator==(const ProviderVersion&) const = default;
};

struct OperatorDescriptor {
  std::uint64_t capability_bits{0U};
  std::string operator_id;
  std::uint32_t interface_revision{0U};
  std::uint32_t max_in_flight{0U};
  Digest256 interface_digest{};
  Digest256 contract_digest{};
  ksj_provider_thread_safety thread_safety{KSJ_PROVIDER_SERIAL_INSTANCE};
  std::uint32_t max_private_threads{0U};
  std::uint32_t max_input_items_per_firing{0U};
  std::uint32_t max_output_items_per_firing{0U};
  std::uint64_t max_output_bytes_per_firing{0U};
  std::uint64_t max_scratch_bytes_per_firing{0U};
  std::uint64_t max_retained_input_bytes{0U};
  std::uint64_t max_async_tail_bytes{0U};

  [[nodiscard]] bool operator==(const OperatorDescriptor&) const = default;
};

struct ProviderDescriptor {
  std::uint64_t capability_bits{0U};
  std::string provider_id;
  ProviderVersion version{};
  std::uint32_t provider_abi_major{0U};
  std::uint32_t provider_abi_minor{0U};
  Digest256 bundle_digest{};
  std::vector<OperatorDescriptor> operators;

  [[nodiscard]] bool operator==(const ProviderDescriptor&) const = default;
};

/**
 * A frozen contract attestation required for one Provider-exposed operator.
 *
 * A load that supplies this requirement succeeds only when the returned
 * descriptor exposes operator_id with this exact contract digest.  It is a
 * descriptor attestation boundary; it is not a bundle-signature or
 * verify-to-load protection mechanism.
 */
struct OperatorContractRequirement {
  std::string operator_id;
  Digest256 contract_digest{};

  [[nodiscard]] bool operator==(const OperatorContractRequirement&) const = default;
};

/**
 * Explicit policy for one trusted Provider module load.
 *
 * No directory search, environment lookup, or plugin discovery is performed.
 * The caller supplies a canonicalizable absolute file path whose provenance and
 * immutability have already been established by installation/admission code.
 * When trusted_root is set, the canonical module must remain underneath it.
 */
struct ProviderLoadOptions {
  std::filesystem::path trusted_root;
  std::string host_build_id{"KSpaceJet ProviderLoader/v1"};
  std::uint64_t host_capability_bits{0U};
  std::uint32_t maximum_operator_count{1024U};
  std::uint64_t maximum_utf8_bytes{1024U * 1024U};
  std::optional<Digest256> required_bundle_digest;
  std::vector<OperatorContractRequirement> required_operator_contracts;
};

namespace detail {
struct ProviderModuleState;
} // namespace detail

/**
 * A lease pins a loaded dynamic module while its API function pointers are
 * accessible.  Retain this object for every Provider-owned operator, context,
 * key state, callback, or asynchronous token that may call provider code.
 */
class ProviderLease {
public:
  ProviderLease() = default;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return valid(); }
  [[nodiscard]] const std::filesystem::path* loaded_path() const noexcept;
  [[nodiscard]] const ProviderDescriptor* descriptor() const noexcept;
  [[nodiscard]] const ksj_provider_api_v1* api() const noexcept;

private:
  friend class ProviderModule;
  explicit ProviderLease(std::shared_ptr<const detail::ProviderModuleState> state) noexcept;

  std::shared_ptr<const detail::ProviderModuleState> state_;
};

/**
 * Move-only value wrapper around one validated, in-process Provider v1 module.
 *
 * This loader is a trusted in-process ABI boundary only.  It does not provide
 * process isolation, a private transport protocol, or permission for a
 * Provider to bypass the host-owned firing lease/output grant interfaces.
 */
class ProviderModule {
public:
  ProviderModule() = default;
  ~ProviderModule() = default;

  ProviderModule(const ProviderModule&) = delete;
  ProviderModule& operator=(const ProviderModule&) = delete;
  ProviderModule(ProviderModule&&) noexcept = default;
  ProviderModule& operator=(ProviderModule&&) noexcept = default;

  /**
   * Loads exactly immutable_trusted_path, resolves only ksj_provider_query,
   * negotiates Provider ABI v1, and copies/validates the returned descriptor.
   */
  [[nodiscard]] static ksj::base::Result<ProviderModule> load(const std::filesystem::path& immutable_trusted_path,
                                                              ProviderLoadOptions options = {});

  [[nodiscard]] bool loaded() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return loaded(); }
  [[nodiscard]] const std::filesystem::path* loaded_path() const noexcept;
  [[nodiscard]] const ProviderDescriptor* descriptor() const noexcept;

  /**
   * Returns a module-pinning view.  The raw API table is available from the
   * lease rather than this wrapper so downstream Provider handles can retain
   * the lease independently of a registry replacing or destroying this value.
   */
  [[nodiscard]] ProviderLease acquire() const noexcept;

private:
  explicit ProviderModule(std::shared_ptr<const detail::ProviderModuleState> state) noexcept;

  std::shared_ptr<const detail::ProviderModuleState> state_;
};

} // namespace ksj::provider::loader
