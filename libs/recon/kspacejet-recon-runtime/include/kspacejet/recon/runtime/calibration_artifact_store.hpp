#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/recon/runtime/buffer_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::recon::runtime {

namespace detail {
struct CalibrationArtifactStoreState;
} // namespace detail

// The store has no process-global registry. A caller creates one explicit
// instance per scan/compiled calibration scope and freezes the exact source
// pool and payload ABI expected for each binding. This keeps binding lookup
// deterministic and prevents a same-shaped but foreign pool from injecting an
// artifact into a calibration path.
struct CalibrationArtifactBindingConfig {
  std::string binding_id;
  std::uint64_t source_pool_identity{0U};
  TypeDescriptor type_descriptor;
};

struct CalibrationArtifactStoreConfig {
  std::vector<CalibrationArtifactBindingConfig> bindings;
};

enum class CalibrationArtifactStoreLifecycle : std::uint8_t {
  accepting,
  end_of_input,
  failed,
};

// A compact, observable ownership summary. `published_bindings` is historical
// (a binding can be published at most once); `retained_artifacts` describes
// handles still owned by the store while consumers hold read leases.
struct CalibrationArtifactStoreSnapshot {
  CalibrationArtifactStoreLifecycle lifecycle{CalibrationArtifactStoreLifecycle::accepting};
  Quantity configured_bindings{0U};
  Quantity published_bindings{0U};
  Quantity missing_bindings{0U};
  Quantity retained_artifacts{0U};
  Quantity active_read_leases{0U};
  ksj::base::Status last_error{};
};

class CalibrationArtifactReadLease final {
public:
  CalibrationArtifactReadLease() = default;
  ~CalibrationArtifactReadLease();

  CalibrationArtifactReadLease(const CalibrationArtifactReadLease&) = delete;
  CalibrationArtifactReadLease& operator=(const CalibrationArtifactReadLease&) = delete;
  CalibrationArtifactReadLease(CalibrationArtifactReadLease&& other) noexcept;
  CalibrationArtifactReadLease& operator=(CalibrationArtifactReadLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;

  // The returned view is owned by the store configuration and remains valid
  // only while this read lease remains valid.
  [[nodiscard]] std::string_view binding_id() const noexcept;

  // These are immutable borrows. They remain valid until this lease is
  // released, even if the store is aborted or its owner is destroyed in the
  // meantime. An aborted store refuses new leases but never recycles an
  // artifact slot while an existing lease can still observe it.
  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> payload() const;
  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> metadata() const;
  [[nodiscard]] const TypeDescriptor* type_descriptor() const noexcept;
  [[nodiscard]] Quantity payload_bytes() const noexcept { return payload_bytes_; }
  [[nodiscard]] Quantity metadata_bytes() const noexcept { return metadata_bytes_; }
  [[nodiscard]] Quantity logical_bytes() const noexcept { return payload_bytes_ + metadata_bytes_; }

  // Optional early RAII settlement. It is idempotent for an already moved or
  // released lease and is otherwise equivalent to destruction.
  void release() noexcept;

private:
  friend struct detail::CalibrationArtifactStoreState;

  CalibrationArtifactReadLease(std::shared_ptr<detail::CalibrationArtifactStoreState> state, std::size_t binding_index,
                               const TypeDescriptor* type_descriptor, Quantity payload_bytes,
                               Quantity metadata_bytes) noexcept;

  void disarm() noexcept;

  std::shared_ptr<detail::CalibrationArtifactStoreState> state_{};
  std::size_t binding_index_{0U};
  const TypeDescriptor* type_descriptor_{nullptr};
  Quantity payload_bytes_{0U};
  Quantity metadata_bytes_{0U};
};

// Owns exactly one ImmutableBufferHandle for each successfully published
// calibration binding. A handle is never copied: the store retains the sole
// pool-slot owner and hands any number of consumers independent, move-only
// read leases. The store owns no payload slab and adds no hidden global state.
class CalibrationArtifactStore final {
public:
  [[nodiscard]] static ksj::base::Result<std::unique_ptr<CalibrationArtifactStore>>
  create(CalibrationArtifactStoreConfig config);

  CalibrationArtifactStore(const CalibrationArtifactStore&) = delete;
  CalibrationArtifactStore& operator=(const CalibrationArtifactStore&) = delete;
  CalibrationArtifactStore(CalibrationArtifactStore&&) = delete;
  CalibrationArtifactStore& operator=(CalibrationArtifactStore&&) = delete;
  ~CalibrationArtifactStore();

  // Publishes one sealed artifact for this explicit binding. On success the
  // supplied handle is moved into the store. On every failure it remains owned
  // by the caller, including duplicate publication; a published artifact can
  // never be overwritten.
  [[nodiscard]] ksj::base::Status publish(std::string_view binding_id, ImmutableBufferHandle& artifact);

  // Returns Unavailable while an accepting binding has not yet been published.
  // After end_of_input(), a missing binding is a terminal StateError; already
  // published bindings remain readable. After abort(), no new lease can be
  // acquired, while pre-existing leases remain safe until their RAII release.
  [[nodiscard]] ksj::base::Result<CalibrationArtifactReadLease> try_acquire(std::string_view binding_id);

  // Stops new publication. Every still-unpublished configured binding becomes
  // terminally missing. Published artifacts remain available to new readers.
  [[nodiscard]] ksj::base::Status end_of_input();

  // Fail-closes the store: it rejects all future publication and acquisition,
  // immediately releases unleased artifacts, and defers release of artifacts
  // observed by existing read leases until the last lease settles.
  [[nodiscard]] ksj::base::Status abort();

  [[nodiscard]] CalibrationArtifactStoreSnapshot snapshot() const;

private:
  explicit CalibrationArtifactStore(std::shared_ptr<detail::CalibrationArtifactStoreState> state) noexcept;

  std::shared_ptr<detail::CalibrationArtifactStoreState> state_{};
};

} // namespace ksj::recon::runtime
