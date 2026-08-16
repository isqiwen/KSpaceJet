#include "kspacejet/recon/runtime/calibration_artifact_store.hpp"

#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ksj::recon::runtime {
namespace {

enum class BindingState : std::uint8_t {
  pending,
  published,
  missing_at_end_of_input,
  released_after_abort,
};

struct BindingRecord {
  std::string id;
  std::uint64_t source_pool_identity{0U};
  TypeDescriptor type_descriptor;
  BindingState state{BindingState::pending};
  std::optional<ImmutableBufferHandle> artifact{};
  Quantity active_readers{0U};
  bool release_when_idle{false};
};

[[nodiscard]] bool has_invalid_binding_config(const std::vector<CalibrationArtifactBindingConfig>& bindings) {
  for (std::size_t index = 0U; index < bindings.size(); ++index) {
    if (bindings[index].binding_id.empty() || bindings[index].source_pool_identity == 0U) {
      return true;
    }
    for (std::size_t other = index + 1U; other < bindings.size(); ++other) {
      if (bindings[index].binding_id == bindings[other].binding_id) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

namespace detail {

struct CalibrationArtifactStoreState final : std::enable_shared_from_this<CalibrationArtifactStoreState> {
  explicit CalibrationArtifactStoreState(std::vector<BindingRecord> bindings_value) noexcept
      : bindings(std::move(bindings_value)) {}

  [[nodiscard]] ksj::base::Status publish(const std::string_view binding_id, ImmutableBufferHandle& artifact) {
    if (binding_id.empty()) {
      return ksj::base::Status::InvalidArgument("CalibrationArtifactStore binding_id must not be empty");
    }
    if (!artifact.valid() || artifact.type_descriptor() == nullptr) {
      return ksj::base::Status::InvalidArgument(
        "CalibrationArtifactStore requires a valid sealed ImmutableBufferHandle");
    }

    std::lock_guard lock(mutex);
    if (lifecycle == CalibrationArtifactStoreLifecycle::failed) {
      return failure_status_locked();
    }
    if (lifecycle != CalibrationArtifactStoreLifecycle::accepting) {
      return ksj::base::Status::StateError("CalibrationArtifactStore cannot publish after end_of_input");
    }
    const auto index = find_binding_locked(binding_id);
    if (!index.has_value()) {
      return ksj::base::Status::NotFound("CalibrationArtifactStore binding_id is not configured");
    }
    auto& binding = bindings[*index];
    if (binding.state != BindingState::pending || binding.artifact.has_value()) {
      return ksj::base::Status::AlreadyExists("CalibrationArtifactStore binding was already published");
    }
    if (artifact.pool_identity() != binding.source_pool_identity) {
      return ksj::base::Status::ValidationError(
        "CalibrationArtifactStore artifact source pool does not match the frozen binding identity");
    }
    if (!artifact.type_descriptor()->exactly_matches(binding.type_descriptor)) {
      return ksj::base::Status::ValidationError(
        "CalibrationArtifactStore artifact TypeDescriptor does not exactly match the frozen binding type");
    }

    binding.artifact.emplace(std::move(artifact));
    binding.state = BindingState::published;
    ++published_bindings;
    return ksj::base::Status::Ok();
  }

  [[nodiscard]] ksj::base::Result<CalibrationArtifactReadLease> try_acquire(const std::string_view binding_id) {
    if (binding_id.empty()) {
      return ksj::base::Status::InvalidArgument("CalibrationArtifactStore binding_id must not be empty");
    }

    std::lock_guard lock(mutex);
    if (lifecycle == CalibrationArtifactStoreLifecycle::failed) {
      return failure_status_locked();
    }
    const auto index = find_binding_locked(binding_id);
    if (!index.has_value()) {
      return ksj::base::Status::NotFound("CalibrationArtifactStore binding_id is not configured");
    }
    auto& binding = bindings[*index];
    if (binding.state == BindingState::pending) {
      return ksj::base::Status::Unavailable("CalibrationArtifactStore artifact is not published yet");
    }
    if (binding.state == BindingState::missing_at_end_of_input) {
      return ksj::base::Status::StateError("CalibrationArtifactStore binding is missing at end_of_input");
    }
    if (binding.state != BindingState::published || !binding.artifact.has_value() || binding.release_when_idle) {
      return ksj::base::Status::StateError("CalibrationArtifactStore artifact is no longer available");
    }
    if (binding.active_readers == std::numeric_limits<Quantity>::max() ||
        active_read_leases == std::numeric_limits<Quantity>::max()) {
      return ksj::base::Status::Unavailable("CalibrationArtifactStore read-lease count is exhausted");
    }
    const auto* const type_descriptor = binding.artifact->type_descriptor();
    if (type_descriptor == nullptr) {
      return ksj::base::Status::InternalError("CalibrationArtifactStore retained an invalid immutable artifact");
    }

    ++binding.active_readers;
    ++active_read_leases;
    return CalibrationArtifactReadLease{shared_from_this(), *index, type_descriptor, binding.artifact->payload_bytes(),
                                        binding.artifact->metadata_bytes()};
  }

  [[nodiscard]] ksj::base::Status end_of_input() {
    std::lock_guard lock(mutex);
    if (lifecycle == CalibrationArtifactStoreLifecycle::failed) {
      return failure_status_locked();
    }
    if (lifecycle != CalibrationArtifactStoreLifecycle::accepting) {
      return ksj::base::Status::StateError("CalibrationArtifactStore end_of_input was already applied");
    }
    for (auto& binding : bindings) {
      if (binding.state == BindingState::pending) {
        binding.state = BindingState::missing_at_end_of_input;
        ++missing_bindings;
      }
    }
    lifecycle = CalibrationArtifactStoreLifecycle::end_of_input;
    return ksj::base::Status::Ok();
  }

  [[nodiscard]] ksj::base::Status abort() {
    std::lock_guard lock(mutex);
    if (lifecycle == CalibrationArtifactStoreLifecycle::failed) {
      return ksj::base::Status::StateError("CalibrationArtifactStore is already failed closed");
    }
    abort_locked();
    return ksj::base::Status::Ok();
  }

  void close_owner_noexcept() noexcept {
    try {
      std::lock_guard lock(mutex);
      if (lifecycle != CalibrationArtifactStoreLifecycle::failed) {
        abort_locked();
      }
    } catch (...) {
      // An owning store can never safely reopen during destruction. Any shared
      // state retained by read leases still owns its immutable handles and is
      // released by those leases when possible.
    }
  }

  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> payload(const std::size_t binding_index) const {
    std::lock_guard lock(mutex);
    const auto status = validate_live_reader_locked(binding_index, "payload");
    if (!status.ok()) {
      return status;
    }
    return bindings[binding_index].artifact->payload();
  }

  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> metadata(const std::size_t binding_index) const {
    std::lock_guard lock(mutex);
    const auto status = validate_live_reader_locked(binding_index, "metadata");
    if (!status.ok()) {
      return status;
    }
    return bindings[binding_index].artifact->metadata();
  }

  [[nodiscard]] std::string_view binding_id(const std::size_t binding_index) const noexcept {
    if (binding_index >= bindings.size()) {
      return {};
    }
    return bindings[binding_index].id;
  }

  void release_reader_noexcept(const std::size_t binding_index) noexcept {
    try {
      std::lock_guard lock(mutex);
      if (binding_index >= bindings.size()) {
        emergency_fail_closed_locked();
        return;
      }
      auto& binding = bindings[binding_index];
      if (binding.active_readers == 0U || active_read_leases == 0U) {
        emergency_fail_closed_locked();
        return;
      }
      --binding.active_readers;
      --active_read_leases;
      if (binding.release_when_idle && binding.active_readers == 0U && binding.artifact.has_value()) {
        // The handle is the sole pool-slot owner. Release it only after all
        // outstanding borrows are gone; ImmutableBufferHandle's destructor is
        // noexcept and can safely settle the underlying FixedBufferPool here.
        binding.artifact.reset();
        binding.state = BindingState::released_after_abort;
      }
    } catch (...) {
      // A lease destructor has no error channel. Leaving the store failed
      // closed is safer than allowing another consumer to acquire data after
      // an inconsistent read-count transition.
    }
  }

  [[nodiscard]] CalibrationArtifactStoreSnapshot snapshot() const {
    std::lock_guard lock(mutex);
    Quantity retained_artifacts{0U};
    for (const auto& binding : bindings) {
      if (binding.artifact.has_value()) {
        ++retained_artifacts;
      }
    }
    return {
      .lifecycle = lifecycle,
      .configured_bindings = static_cast<Quantity>(bindings.size()),
      .published_bindings = published_bindings,
      .missing_bindings = missing_bindings,
      .retained_artifacts = retained_artifacts,
      .active_read_leases = active_read_leases,
      .last_error = last_error,
    };
  }

private:
  [[nodiscard]] std::optional<std::size_t> find_binding_locked(const std::string_view binding_id) const noexcept {
    for (std::size_t index = 0U; index < bindings.size(); ++index) {
      if (bindings[index].id == binding_id) {
        return index;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] ksj::base::Status failure_status_locked() const {
    if (!last_error.ok()) {
      return last_error;
    }
    return ksj::base::Status::StateError("CalibrationArtifactStore is failed closed");
  }

  [[nodiscard]] ksj::base::Status validate_live_reader_locked(const std::size_t binding_index,
                                                              const char* const operation) const {
    if (binding_index >= bindings.size()) {
      return ksj::base::Status::StateError(std::string("CalibrationArtifactReadLease ") + operation +
                                           " binding is invalid");
    }
    const auto& binding = bindings[binding_index];
    if (binding.active_readers == 0U || !binding.artifact.has_value()) {
      return ksj::base::Status::StateError(std::string("CalibrationArtifactReadLease ") + operation +
                                           " is stale or released");
    }
    return ksj::base::Status::Ok();
  }

  void abort_locked() {
    lifecycle = CalibrationArtifactStoreLifecycle::failed;
    last_error = ksj::base::Status::StateError("CalibrationArtifactStore was aborted");
    for (auto& binding : bindings) {
      binding.release_when_idle = true;
      if (binding.active_readers == 0U && binding.artifact.has_value()) {
        binding.artifact.reset();
        binding.state = BindingState::released_after_abort;
      }
    }
  }

  void emergency_fail_closed_locked() noexcept {
    lifecycle = CalibrationArtifactStoreLifecycle::failed;
    // This is reached from noexcept lease cleanup. Do not allocate a diagnostic
    // string here: failure_status_locked() supplies a stable generic error when
    // no earlier failure status exists.
    for (auto& binding : bindings) {
      binding.release_when_idle = true;
      if (binding.active_readers == 0U && binding.artifact.has_value()) {
        binding.artifact.reset();
        binding.state = BindingState::released_after_abort;
      }
    }
  }

  mutable std::mutex mutex;
  std::vector<BindingRecord> bindings;
  CalibrationArtifactStoreLifecycle lifecycle{CalibrationArtifactStoreLifecycle::accepting};
  Quantity published_bindings{0U};
  Quantity missing_bindings{0U};
  Quantity active_read_leases{0U};
  ksj::base::Status last_error{};
};

} // namespace detail

CalibrationArtifactReadLease::CalibrationArtifactReadLease(std::shared_ptr<detail::CalibrationArtifactStoreState> state,
                                                           const std::size_t binding_index,
                                                           const TypeDescriptor* const type_descriptor,
                                                           const Quantity payload_bytes,
                                                           const Quantity metadata_bytes) noexcept
    : state_(std::move(state)), binding_index_(binding_index), type_descriptor_(type_descriptor),
      payload_bytes_(payload_bytes), metadata_bytes_(metadata_bytes) {}

CalibrationArtifactReadLease::~CalibrationArtifactReadLease() {
  release();
}

CalibrationArtifactReadLease::CalibrationArtifactReadLease(CalibrationArtifactReadLease&& other) noexcept
    : state_(std::move(other.state_)), binding_index_(std::exchange(other.binding_index_, 0U)),
      type_descriptor_(std::exchange(other.type_descriptor_, nullptr)),
      payload_bytes_(std::exchange(other.payload_bytes_, 0U)),
      metadata_bytes_(std::exchange(other.metadata_bytes_, 0U)) {}

CalibrationArtifactReadLease& CalibrationArtifactReadLease::operator=(CalibrationArtifactReadLease&& other) noexcept {
  if (this != &other) {
    release();
    state_ = std::move(other.state_);
    binding_index_ = std::exchange(other.binding_index_, 0U);
    type_descriptor_ = std::exchange(other.type_descriptor_, nullptr);
    payload_bytes_ = std::exchange(other.payload_bytes_, 0U);
    metadata_bytes_ = std::exchange(other.metadata_bytes_, 0U);
  }
  return *this;
}

bool CalibrationArtifactReadLease::valid() const noexcept {
  return state_ != nullptr && type_descriptor_ != nullptr;
}

std::string_view CalibrationArtifactReadLease::binding_id() const noexcept {
  if (!valid()) {
    return {};
  }
  return state_->binding_id(binding_index_);
}

ksj::base::Result<ksj::base::ConstByteSpan> CalibrationArtifactReadLease::payload() const {
  if (!valid()) {
    return ksj::base::Status::StateError("CalibrationArtifactReadLease is invalid or moved from");
  }
  return state_->payload(binding_index_);
}

ksj::base::Result<ksj::base::ConstByteSpan> CalibrationArtifactReadLease::metadata() const {
  if (!valid()) {
    return ksj::base::Status::StateError("CalibrationArtifactReadLease is invalid or moved from");
  }
  return state_->metadata(binding_index_);
}

const TypeDescriptor* CalibrationArtifactReadLease::type_descriptor() const noexcept {
  return valid() ? type_descriptor_ : nullptr;
}

void CalibrationArtifactReadLease::release() noexcept {
  auto state = std::move(state_);
  const auto binding_index = std::exchange(binding_index_, 0U);
  type_descriptor_ = nullptr;
  payload_bytes_ = 0U;
  metadata_bytes_ = 0U;
  if (state != nullptr) {
    state->release_reader_noexcept(binding_index);
  }
}

void CalibrationArtifactReadLease::disarm() noexcept {
  state_.reset();
  binding_index_ = 0U;
  type_descriptor_ = nullptr;
  payload_bytes_ = 0U;
  metadata_bytes_ = 0U;
}

CalibrationArtifactStore::CalibrationArtifactStore(
  std::shared_ptr<detail::CalibrationArtifactStoreState> state) noexcept
    : state_(std::move(state)) {}

CalibrationArtifactStore::~CalibrationArtifactStore() {
  if (state_ != nullptr) {
    state_->close_owner_noexcept();
  }
}

ksj::base::Result<std::unique_ptr<CalibrationArtifactStore>>
CalibrationArtifactStore::create(CalibrationArtifactStoreConfig config) {
  if (config.bindings.empty()) {
    return ksj::base::Status::InvalidArgument("CalibrationArtifactStore requires at least one configured binding");
  }
  if (config.bindings.size() > std::numeric_limits<Quantity>::max()) {
    return ksj::base::Status::InvalidArgument("CalibrationArtifactStore binding count exceeds Quantity accounting");
  }
  if (has_invalid_binding_config(config.bindings)) {
    return ksj::base::Status::InvalidArgument(
      "CalibrationArtifactStore bindings require non-empty unique IDs and non-zero source pool identities");
  }

  try {
    std::vector<BindingRecord> bindings;
    bindings.reserve(config.bindings.size());
    for (auto& binding : config.bindings) {
      bindings.push_back({.id = std::move(binding.binding_id),
                          .source_pool_identity = binding.source_pool_identity,
                          .type_descriptor = std::move(binding.type_descriptor)});
    }
    auto state = std::make_shared<detail::CalibrationArtifactStoreState>(std::move(bindings));
    return std::unique_ptr<CalibrationArtifactStore>(new CalibrationArtifactStore(std::move(state)));
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("CalibrationArtifactStore could not allocate binding state");
  }
}

ksj::base::Status CalibrationArtifactStore::publish(const std::string_view binding_id,
                                                    ImmutableBufferHandle& artifact) {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("CalibrationArtifactStore is invalid");
  }
  return state_->publish(binding_id, artifact);
}

ksj::base::Result<CalibrationArtifactReadLease>
CalibrationArtifactStore::try_acquire(const std::string_view binding_id) {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("CalibrationArtifactStore is invalid");
  }
  return state_->try_acquire(binding_id);
}

ksj::base::Status CalibrationArtifactStore::end_of_input() {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("CalibrationArtifactStore is invalid");
  }
  return state_->end_of_input();
}

ksj::base::Status CalibrationArtifactStore::abort() {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("CalibrationArtifactStore is invalid");
  }
  return state_->abort();
}

CalibrationArtifactStoreSnapshot CalibrationArtifactStore::snapshot() const {
  if (state_ == nullptr) {
    return {.lifecycle = CalibrationArtifactStoreLifecycle::failed,
            .last_error = ksj::base::Status::StateError("CalibrationArtifactStore is invalid")};
  }
  return state_->snapshot();
}

} // namespace ksj::recon::runtime
