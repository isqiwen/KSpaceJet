#include "kspacejet/recon/runtime/fixed_reorder_buffer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <string_view>
#include <utility>

namespace ksj::recon::runtime {
namespace {

constexpr std::size_t kOrdinalRecordBytes = 16U;
constexpr std::size_t kPhysicalSlotRecordBytes = 16U;
constexpr std::size_t kFirstWordOffset = 0U;
constexpr std::size_t kSecondWordOffset = sizeof(std::uint64_t);
constexpr std::uint64_t kOrdinalStateShift = 61U;
constexpr std::uint64_t kOrdinalSlotMask = (std::uint64_t{1} << kOrdinalStateShift) - 1U;
constexpr std::uint64_t kPhysicalFreeFlag = std::uint64_t{1} << 63U;
constexpr ksj::recon::Quantity kNoSlot = std::numeric_limits<ksj::recon::Quantity>::max();
// Ordinal records reserve the all-ones 61-bit value for the direct current
// head. Physical reorder slots are always below the frozen plan capacity,
// whose canonical bound is strictly smaller than this sentinel.
constexpr ksj::recon::Quantity kDirectHeadSlot = kOrdinalSlotMask;

static_assert(ksj::recon::kMaxCanonicalJsonInteger <= kOrdinalSlotMask);
static_assert(ksj::recon::kMaxCanonicalJsonInteger < kPhysicalFreeFlag);
static_assert(ksj::recon::kMaxCanonicalJsonInteger < kDirectHeadSlot);

[[nodiscard]] constexpr bool is_direct_head_slot(const ksj::recon::Quantity slot_id) noexcept {
  return slot_id == kDirectHeadSlot;
}

std::atomic<std::uint64_t> g_next_reorder_buffer_identity{1U};

[[nodiscard]] std::uint64_t read_u64(const ksj::base::byte* const address) noexcept {
  std::uint64_t value = 0U;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

void write_u64(ksj::base::byte* const address, const std::uint64_t value) noexcept {
  std::memcpy(address, &value, sizeof(value));
}

[[nodiscard]] bool checked_add(const ksj::recon::Quantity lhs, const ksj::recon::Quantity rhs,
                               ksj::recon::Quantity& result) noexcept {
  if (rhs > std::numeric_limits<ksj::recon::Quantity>::max() - lhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

[[nodiscard]] bool checked_multiply(const ksj::recon::Quantity lhs, const ksj::recon::Quantity rhs,
                                    ksj::recon::Quantity& result) noexcept {
  if (lhs != 0U && rhs > std::numeric_limits<ksj::recon::Quantity>::max() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

[[nodiscard]] ksj::base::Result<std::uint64_t> allocate_buffer_identity() {
  auto identity = g_next_reorder_buffer_identity.load(std::memory_order_relaxed);
  for (;;) {
    if (identity == 0U || identity == std::numeric_limits<std::uint64_t>::max()) {
      return ksj::base::Status::Unavailable("FixedReorderBuffer process-local identity space is exhausted");
    }
    if (g_next_reorder_buffer_identity.compare_exchange_weak(identity, identity + 1U, std::memory_order_relaxed,
                                                             std::memory_order_relaxed)) {
      return identity;
    }
  }
}

[[nodiscard]] ksj::base::Result<DenseCartesianOrdinalMapper::Axis> axis_for_field(const std::string_view field) {
  if (field == "encoding") {
    return DenseCartesianOrdinalMapper::Axis::encoding;
  }
  if (field == "average") {
    return DenseCartesianOrdinalMapper::Axis::average;
  }
  if (field == "slice") {
    return DenseCartesianOrdinalMapper::Axis::slice;
  }
  if (field == "contrast") {
    return DenseCartesianOrdinalMapper::Axis::contrast;
  }
  if (field == "phase") {
    return DenseCartesianOrdinalMapper::Axis::phase;
  }
  if (field == "repetition") {
    return DenseCartesianOrdinalMapper::Axis::repetition;
  }
  if (field == "set") {
    return DenseCartesianOrdinalMapper::Axis::set;
  }
  return ksj::base::Status::ValidationError("ReorderPlan ordinal dimension is not representable by FrameSemanticKey");
}

[[nodiscard]] ksj::recon::Quantity axis_value(const FrameSlotContext& context,
                                              const DenseCartesianOrdinalMapper::Axis axis) noexcept {
  switch (axis) {
    case DenseCartesianOrdinalMapper::Axis::encoding:
      return context.semantic_key.encoding_space;
    case DenseCartesianOrdinalMapper::Axis::average:
      return context.semantic_key.average;
    case DenseCartesianOrdinalMapper::Axis::slice:
      return context.semantic_key.slice;
    case DenseCartesianOrdinalMapper::Axis::contrast:
      return context.semantic_key.contrast;
    case DenseCartesianOrdinalMapper::Axis::phase:
      return context.semantic_key.phase;
    case DenseCartesianOrdinalMapper::Axis::repetition:
      return context.semantic_key.repetition;
    case DenseCartesianOrdinalMapper::Axis::set:
      return context.semantic_key.set;
  }
  return 0U;
}

[[nodiscard]] ksj::base::Status validate_frozen_plan_identity(const ksj::recon::ReorderPlan& plan) {
  if (plan.node_id().empty() || plan.order_domain_id().empty() || plan.completed_frame_input_port().empty() ||
      plan.ordered_output_port().empty()) {
    return ksj::base::Status::ValidationError(
      "ReorderPlan requires non-empty node, order-domain, completed-frame-port, and output identities");
  }
  if (plan.ordinal_binding_id() != ksj::recon::kCompletedFrameSlotContextSemanticKeyOrdinalBindingId) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer requires the completed FrameSlotContext semantic-key ordinal binding");
  }
  if (plan.order_domain_id() != plan.node_id()) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer requires the M3 node-local order-domain identity");
  }
  if (plan.mapping_algorithm_id() != ksj::recon::kDenseCartesianReorderMappingAlgorithmId) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer requires the dense-cartesian-ordinal/v1 plan mapping");
  }
  if (plan.storage_accounting_id() != ksj::recon::kDenseCartesianReorderStorageAccountingId) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer requires dense-cartesian-v1 storage accounting");
  }
  if (plan.outputs_per_ordinal() != 1U || plan.charged_bytes_per_ordinal() == 0U) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer requires one positive-byte output per ordinal");
  }
  if (plan.publish_policy() != ksj::recon::kNextExpectedOnlyReorderPublishPolicy ||
      plan.end_of_input_policy() != ksj::recon::kFailReorderEndOfInputPolicy ||
      !plan.certified_skipped_ordinals().empty()) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer requires M3 next-expected-only fail-without-skips policy");
  }
  if (plan.occurrence_policy() != ksj::recon::kStrictDenseAllTuplesReorderOccurrencePolicy) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer requires strict dense all-tuples EndOfInput occurrence policy");
  }
  if (plan.ordinal_domain_bound() == 0U || plan.first_expected_ordinal() != ksj::recon::kFirstExpectedReorderOrdinal ||
      plan.last_expected_ordinal() != plan.ordinal_domain_bound() - 1U ||
      plan.max_gap_ordinals() != plan.ordinal_domain_bound() - 1U) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer received an invalid closed ordinal domain");
  }
  if (plan.max_ahead_items() == 0U || plan.max_ahead_items() > plan.ordinal_domain_bound() ||
      plan.descriptor_charged_count() != plan.max_ahead_items()) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer received an invalid ahead-item capacity");
  }
  ksj::recon::Quantity expected_handle_storage = 0U;
  if (!checked_multiply(plan.max_ahead_items(), ksj::recon::kDenseCartesianReorderHandleSidecarChargedBytes,
                        expected_handle_storage) ||
      plan.handle_storage_charged_bytes() != expected_handle_storage) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer handle-sidecar charge does not match the frozen ahead capacity");
  }
  ksj::recon::Quantity minimum_ahead_bytes = 0U;
  if (!checked_multiply(plan.max_ahead_items(), plan.charged_bytes_per_ordinal(), minimum_ahead_bytes) ||
      plan.max_ahead_charged_bytes() < minimum_ahead_bytes) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer ahead-byte capacity is smaller than full envelopes");
  }
  const auto expected_metadata = ksj::recon::dense_cartesian_reorder_host_metadata_charged_bytes(
    plan.ordinal_domain_bound(), plan.max_ahead_items(), "FixedReorderBuffer storage");
  if (!expected_metadata.ok()) {
    return expected_metadata.status();
  }
  if (expected_metadata.value() != plan.host_metadata_charged_bytes()) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer storage does not match ReorderPlan metadata charge");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] bool contains_identifier(const std::vector<std::string>& identifiers,
                                       const std::string_view expected) noexcept {
  return std::find(identifiers.begin(), identifiers.end(), expected) != identifiers.end();
}

// This boundary does not authenticate artifact-store provenance; it verifies
// the immutable artifacts' internal relation before extracting the one plan
// slice this primitive is allowed to consume. Admission and durable artifact
// provenance remain outer runtime responsibilities.
[[nodiscard]] ksj::base::Result<const ksj::recon::ReorderPlan*>
resolve_verified_reorder_plan(const ksj::recon::ExecutionPlan& execution_plan,
                              const ksj::recon::VerificationRecord& verification_record,
                              const std::string_view node_id) {
  if (node_id.empty()) {
    return ksj::base::Status::InvalidArgument("FixedReorderBuffer requires a non-empty reorder node identity");
  }
  if (verification_record.execution_plan_digest() != execution_plan.digest()) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer VerificationRecord does not bind this ExecutionPlan digest");
  }
  if (verification_record.execution_profile() != execution_plan.execution_profile()) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer VerificationRecord execution profile does not match the ExecutionPlan");
  }
  if (!ksj::recon::is_currently_supported_in_process(execution_plan.execution_profile())) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer supports only in-process ExecutionPlan profiles");
  }
  if (!verification_record.verified_resource_vector().exactly_matches(execution_plan.resources())) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer VerificationRecord resource vector does not match the ExecutionPlan");
  }
  if (verification_record.verified_terminal_occurrences() != execution_plan.terminal_occurrences()) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer VerificationRecord terminal occurrences do not match the ExecutionPlan");
  }
  if (!contains_identifier(verification_record.verified_obligations(),
                           ksj::recon::kM3CompletedFrameSlotBindingVerificationObligation)) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer VerificationRecord lacks the M3 completed FrameSlot binding verdict");
  }
  if (!contains_identifier(verification_record.verified_obligations(),
                           ksj::recon::kM3StrictDenseAllTuplesEoiVerificationObligation)) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer VerificationRecord lacks the M3 strict dense all-tuples EndOfInput verdict");
  }
  if (!contains_identifier(execution_plan.proof_obligations(),
                           ksj::recon::kM3CompletedFrameSlotBindingProofObligation)) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer ExecutionPlan lacks the M3 completed FrameSlot binding proof obligation");
  }
  if (!contains_identifier(execution_plan.proof_obligations(),
                           ksj::recon::kM3StrictDenseAllTuplesEoiRuntimeAssumption)) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer ExecutionPlan lacks the M3 strict dense all-tuples EndOfInput assumption");
  }

  const ksj::recon::ReorderPlan* matching_plan = nullptr;
  for (const auto& candidate : execution_plan.reorder_plans()) {
    if (candidate.node_id() != node_id) {
      continue;
    }
    if (matching_plan != nullptr) {
      return ksj::base::Status::ValidationError(
        "FixedReorderBuffer ExecutionPlan has multiple ReorderPlans for one node identity");
    }
    matching_plan = &candidate;
  }
  if (matching_plan == nullptr) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer node identity does not select a ReorderPlan in this ExecutionPlan");
  }
  return matching_plan;
}

} // namespace

ksj::base::Result<std::size_t> required_storage_bytes(const ksj::recon::ReorderPlan& plan) {
  const auto identity = validate_frozen_plan_identity(plan);
  if (!identity.ok()) {
    return identity;
  }
  const auto expected = ksj::recon::dense_cartesian_reorder_host_metadata_charged_bytes(
    plan.ordinal_domain_bound(), plan.max_ahead_items(), "FixedReorderBuffer storage");
  if (!expected.ok()) {
    return expected.status();
  }
  if (expected.value() > std::numeric_limits<std::size_t>::max()) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer storage exceeds this host's ByteSpan size");
  }
  return static_cast<std::size_t>(expected.value());
}

ksj::base::Result<std::size_t>
fixed_reorder_buffer_required_bookkeeping_storage_bytes(const ksj::recon::ReorderPlan& plan) {
  const auto identity = validate_frozen_plan_identity(plan);
  if (!identity.ok()) {
    return identity;
  }
  ksj::recon::Quantity ordinal_bytes = 0U;
  if (!checked_multiply(plan.ordinal_domain_bound(), kOrdinalRecordBytes, ordinal_bytes)) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer ordinal bookkeeping exceeds this host's ByteSpan size");
  }
  ksj::recon::Quantity slot_bytes = 0U;
  if (!checked_multiply(plan.max_ahead_items(), kPhysicalSlotRecordBytes, slot_bytes)) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer slot bookkeeping exceeds this host's ByteSpan size");
  }
  ksj::recon::Quantity total = 0U;
  if (!checked_add(ordinal_bytes, slot_bytes, total) || total > std::numeric_limits<std::size_t>::max()) {
    return ksj::base::Status::ValidationError("FixedReorderBuffer bookkeeping exceeds this host's ByteSpan size");
  }
  return static_cast<std::size_t>(total);
}

ksj::base::Result<DenseCartesianOrdinalMapper>
DenseCartesianOrdinalMapper::create(const ksj::recon::ReorderPlan& plan) {
  const auto identity = validate_frozen_plan_identity(plan);
  if (!identity.ok()) {
    return identity;
  }
  if (plan.ordinal_dimensions().empty() || plan.ordinal_dimensions().size() > kMaximumDimensions) {
    return ksj::base::Status::ValidationError("DenseCartesianOrdinalMapper requires one to seven ordinal dimensions");
  }

  std::array<Dimension, kMaximumDimensions> dimensions{};
  std::array<bool, kMaximumDimensions> axis_seen{};
  ksj::recon::Quantity cardinality_product = 1U;
  for (std::size_t index = 0U; index < plan.ordinal_dimensions().size(); ++index) {
    const auto& dimension = plan.ordinal_dimensions()[index];
    const auto axis = axis_for_field(dimension.field());
    if (!axis.ok()) {
      return axis.status();
    }
    const auto axis_index = static_cast<std::size_t>(axis.value());
    if (axis_seen[axis_index]) {
      return ksj::base::Status::ValidationError("DenseCartesianOrdinalMapper received a duplicate ordinal axis");
    }
    if (dimension.cardinality() == 0U) {
      return ksj::base::Status::ValidationError("DenseCartesianOrdinalMapper received a zero-cardinality axis");
    }
    ksj::recon::Quantity next_product = 0U;
    if (!checked_multiply(cardinality_product, dimension.cardinality(), next_product)) {
      return ksj::base::Status::ValidationError("DenseCartesianOrdinalMapper ordinal cardinality overflows");
    }
    axis_seen[axis_index] = true;
    cardinality_product = next_product;
    dimensions[index] = {.axis = axis.value(), .minimum = dimension.minimum(), .cardinality = dimension.cardinality()};
  }
  if (cardinality_product != plan.ordinal_domain_bound()) {
    return ksj::base::Status::ValidationError("DenseCartesianOrdinalMapper dimensions do not match ordinal domain");
  }
  return DenseCartesianOrdinalMapper{&plan, dimensions, plan.ordinal_dimensions().size()};
}

ksj::base::Result<ksj::recon::Quantity> DenseCartesianOrdinalMapper::ordinal(const FrameSlotContext& context) const {
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("DenseCartesianOrdinalMapper was moved from or not initialized");
  }
  ksj::recon::Quantity result = 0U;
  for (std::size_t index = 0U; index < dimension_count_; ++index) {
    const auto& dimension = dimensions_[index];
    const auto value = axis_value(context, dimension.axis);
    if (value < dimension.minimum || value - dimension.minimum >= dimension.cardinality) {
      return ksj::base::Status::ValidationError("FrameSlotContext is outside the frozen Cartesian ordinal domain");
    }
    ksj::recon::Quantity expanded = 0U;
    if (!checked_multiply(result, dimension.cardinality, expanded) ||
        !checked_add(expanded, value - dimension.minimum, result)) {
      return ksj::base::Status::InternalError("DenseCartesianOrdinalMapper ordinal calculation overflowed");
    }
  }
  if (result >= plan_->ordinal_domain_bound()) {
    return ksj::base::Status::InternalError("DenseCartesianOrdinalMapper produced an ordinal outside its plan");
  }
  return result;
}

DispatchPermit::~DispatchPermit() {
  release_noexcept();
}

DispatchPermit::DispatchPermit(DispatchPermit&& other) noexcept
    : owner_(other.owner_), plan_(other.plan_), buffer_identity_(other.buffer_identity_), ordinal_(other.ordinal_),
      slot_id_(other.slot_id_), direct_head_handle_(std::move(other.direct_head_handle_)), phase_(other.phase_) {
  other.disarm();
}

DispatchPermit& DispatchPermit::operator=(DispatchPermit&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    owner_ = other.owner_;
    plan_ = other.plan_;
    buffer_identity_ = other.buffer_identity_;
    ordinal_ = other.ordinal_;
    slot_id_ = other.slot_id_;
    direct_head_handle_ = std::move(other.direct_head_handle_);
    phase_ = other.phase_;
    other.disarm();
  }
  return *this;
}

bool DispatchPermit::valid() const noexcept {
  return owner_ != nullptr && plan_ != nullptr && buffer_identity_ != 0U && phase_ != Phase::invalid;
}

ksj::base::Status DispatchPermit::commit() {
  if (owner_ == nullptr) {
    return ksj::base::Status::StateError("DispatchPermit is invalid or was moved from");
  }
  return owner_->commit_dispatch(*this);
}

ksj::base::Status DispatchPermit::abort() {
  if (owner_ == nullptr) {
    return ksj::base::Status::StateError("DispatchPermit is invalid or was moved from");
  }
  return owner_->abort_dispatch(*this);
}

void DispatchPermit::release_noexcept() noexcept {
  if (owner_ != nullptr) {
    owner_->abandon_dispatch_noexcept(*this);
  }
  disarm();
}

void DispatchPermit::disarm() noexcept {
  owner_ = nullptr;
  plan_ = nullptr;
  buffer_identity_ = 0U;
  ordinal_ = 0U;
  slot_id_ = 0U;
  direct_head_handle_.reset();
  phase_ = Phase::invalid;
}

PublishLease::~PublishLease() {
  release_noexcept();
}

PublishLease::PublishLease(PublishLease&& other) noexcept
    : owner_(other.owner_), buffer_identity_(other.buffer_identity_), permit_(std::move(other.permit_)),
      output_(other.output_), handle_sidecar_(other.handle_sidecar_) {
  other.disarm();
}

PublishLease& PublishLease::operator=(PublishLease&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    owner_ = other.owner_;
    buffer_identity_ = other.buffer_identity_;
    permit_ = std::move(other.permit_);
    output_ = other.output_;
    handle_sidecar_ = other.handle_sidecar_;
    other.disarm();
  }
  return *this;
}

bool PublishLease::valid() const noexcept {
  return owner_ != nullptr && buffer_identity_ != 0U && permit_.valid();
}

ksj::base::Result<ImmutableBufferHandle> PublishLease::take_buffer_handle_for_ordered_edge_commit() {
  if (owner_ == nullptr) {
    return ksj::base::Status::StateError("PublishLease is invalid or was moved from");
  }
  return owner_->take_published_buffer_handle(*this);
}

bool PublishLease::has_buffer_handle() const {
  return owner_ != nullptr && owner_->publish_lease_has_buffer_handle(*this);
}

ksj::base::Status PublishLease::acknowledge_published() {
  if (owner_ == nullptr) {
    return ksj::base::Status::StateError("PublishLease is invalid or was moved from");
  }
  return owner_->acknowledge_published(*this);
}

void PublishLease::release_noexcept() noexcept {
  if (owner_ != nullptr) {
    owner_->abandon_publish_noexcept(*this);
  }
  permit_.disarm();
  disarm();
}

void PublishLease::disarm() noexcept {
  owner_ = nullptr;
  buffer_identity_ = 0U;
  handle_sidecar_ = nullptr;
}

ksj::base::Result<FixedReorderBuffer>
FixedReorderBuffer::create(const ksj::recon::ExecutionPlan& execution_plan,
                           const ksj::recon::VerificationRecord& verification_record, const std::string_view node_id,
                           const ksj::base::ByteSpan storage, FixedReorderBufferConfig config) {
  if (config.resource_ledger == nullptr) {
    return ksj::base::Status::InvalidArgument("FixedReorderBuffer requires an explicit shared ResourceVectorLedger");
  }
  const auto resolved_plan = resolve_verified_reorder_plan(execution_plan, verification_record, node_id);
  if (!resolved_plan.ok()) {
    return resolved_plan.status();
  }
  const auto& plan = *resolved_plan.value();
  const auto plan_status = validate_plan(plan);
  if (!plan_status.ok()) {
    return plan_status;
  }
  const auto required = required_storage_bytes(plan);
  if (!required.ok()) {
    return required.status();
  }
  if (storage.size() < required.value()) {
    return ksj::base::Status::InvalidArgument(
      "FixedReorderBuffer caller storage is smaller than the frozen plan bound");
  }
  if (required.value() != 0U && storage.data() == nullptr) {
    return ksj::base::Status::InvalidArgument("FixedReorderBuffer caller storage must not be null");
  }
  const auto storage_address = reinterpret_cast<std::uintptr_t>(storage.data());
  if (storage_address % fixed_reorder_buffer_storage_alignment() != 0U) {
    return ksj::base::Status::InvalidArgument(
      "FixedReorderBuffer caller storage has insufficient byte-layout alignment");
  }
  const std::array<ksj::base::ByteSpan, 1U> slabs{storage};
  auto slab_claim = detail::claim_exclusive_slab_ranges(std::span<const ksj::base::ByteSpan>{slabs});
  if (!slab_claim.ok()) {
    return slab_claim.status();
  }
  const auto identity = allocate_buffer_identity();
  if (!identity.ok()) {
    return identity.status();
  }
  const auto pool = credit_pool_reservation(plan);
  if (!pool.ok()) {
    return pool.status();
  }
  if (!config.resource_ledger->capacity().can_admit(pool.value())) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer metadata and descriptor pool exceeds ResourceVectorLedger capacity");
  }
  auto reservation = config.resource_ledger->try_reserve(pool.value());
  if (!reservation.ok()) {
    return reservation.status();
  }

  FixedReorderBuffer buffer{plan,
                            execution_plan.digest().value(),
                            verification_record.digest().value(),
                            std::move(config.resource_ledger),
                            std::move(slab_claim).value(),
                            storage.data(),
                            required.value(),
                            nullptr,
                            0U,
                            identity.value()};
  const auto mapper = DenseCartesianOrdinalMapper::create(*buffer.plan_);
  if (!mapper.ok()) {
    return mapper.status();
  }
  buffer.mapper_ = mapper.value();
  buffer.credit_pool_reservation_.emplace(std::move(reservation).value());
  {
    std::lock_guard lock(buffer.mutex_);
    buffer.initialize_storage_unlocked();
  }
  const auto committed = buffer.credit_pool_reservation_->commit();
  if (!committed.ok()) {
    return committed;
  }
  return std::move(buffer);
}

ksj::base::Result<FixedReorderBuffer>
FixedReorderBuffer::create(const ksj::recon::ExecutionPlan& execution_plan,
                           const ksj::recon::VerificationRecord& verification_record, const std::string_view node_id,
                           FixedReorderBufferStorage storage, FixedReorderBufferConfig config) {
  if (config.resource_ledger == nullptr) {
    return ksj::base::Status::InvalidArgument("FixedReorderBuffer requires an explicit shared ResourceVectorLedger");
  }
  const auto resolved_plan = resolve_verified_reorder_plan(execution_plan, verification_record, node_id);
  if (!resolved_plan.ok()) {
    return resolved_plan.status();
  }
  const auto& plan = *resolved_plan.value();
  const auto plan_status = validate_plan(plan);
  if (!plan_status.ok()) {
    return plan_status;
  }
  const auto charged_storage = required_storage_bytes(plan);
  if (!charged_storage.ok()) {
    return charged_storage.status();
  }
  const auto bookkeeping_storage = fixed_reorder_buffer_required_bookkeeping_storage_bytes(plan);
  if (!bookkeeping_storage.ok()) {
    return bookkeeping_storage.status();
  }
  if (storage.bookkeeping.size() != bookkeeping_storage.value() ||
      (bookkeeping_storage.value() != 0U && storage.bookkeeping.data() == nullptr)) {
    return ksj::base::Status::InvalidArgument(
      "FixedReorderBuffer bookkeeping storage must exactly match the frozen plan bound");
  }
  const auto bookkeeping_address = reinterpret_cast<std::uintptr_t>(storage.bookkeeping.data());
  if (bookkeeping_address % fixed_reorder_buffer_storage_alignment() != 0U) {
    return ksj::base::Status::InvalidArgument(
      "FixedReorderBuffer bookkeeping storage has insufficient byte-layout alignment");
  }
  if (storage.handle_sidecars.size() != plan.max_ahead_items() ||
      (plan.max_ahead_items() != 0U && storage.handle_sidecars.data() == nullptr)) {
    return ksj::base::Status::InvalidArgument(
      "FixedReorderBuffer typed handle sidecars must exactly match the frozen ahead-item capacity");
  }
  if (reinterpret_cast<std::uintptr_t>(storage.handle_sidecars.data()) % alignof(FixedReorderBufferHandleSidecar) !=
      0U) {
    return ksj::base::Status::InvalidArgument(
      "FixedReorderBuffer typed handle sidecars have insufficient object alignment");
  }
  if (storage.handle_sidecars.size() >
      std::numeric_limits<std::size_t>::max() / sizeof(FixedReorderBufferHandleSidecar)) {
    return ksj::base::Status::InvalidArgument("FixedReorderBuffer typed handle sidecar range exceeds host size");
  }
  for (const auto& sidecar : storage.handle_sidecars) {
    if (sidecar.handle.has_value()) {
      return ksj::base::Status::StateError("FixedReorderBuffer typed handle sidecars must be empty at creation");
    }
  }
  const auto sidecar_bytes = storage.handle_sidecars.size() * sizeof(FixedReorderBufferHandleSidecar);
  const std::array<ksj::base::ByteSpan, 2U> slabs{
    storage.bookkeeping,
    {reinterpret_cast<ksj::base::byte*>(storage.handle_sidecars.data()), sidecar_bytes},
  };
  auto slab_claim = detail::claim_exclusive_slab_ranges(std::span<const ksj::base::ByteSpan>{slabs});
  if (!slab_claim.ok()) {
    return slab_claim.status();
  }
  const auto identity = allocate_buffer_identity();
  if (!identity.ok()) {
    return identity.status();
  }
  const auto pool = credit_pool_reservation(plan);
  if (!pool.ok()) {
    return pool.status();
  }
  if (!config.resource_ledger->capacity().can_admit(pool.value())) {
    return ksj::base::Status::ValidationError(
      "FixedReorderBuffer metadata and descriptor pool exceeds ResourceVectorLedger capacity");
  }
  auto reservation = config.resource_ledger->try_reserve(pool.value());
  if (!reservation.ok()) {
    return reservation.status();
  }

  FixedReorderBuffer buffer{plan,
                            execution_plan.digest().value(),
                            verification_record.digest().value(),
                            std::move(config.resource_ledger),
                            std::move(slab_claim).value(),
                            storage.bookkeeping.data(),
                            charged_storage.value(),
                            storage.handle_sidecars.data(),
                            static_cast<Quantity>(storage.handle_sidecars.size()),
                            identity.value()};
  const auto mapper = DenseCartesianOrdinalMapper::create(*buffer.plan_);
  if (!mapper.ok()) {
    return mapper.status();
  }
  buffer.mapper_ = mapper.value();
  buffer.credit_pool_reservation_.emplace(std::move(reservation).value());
  {
    std::lock_guard lock(buffer.mutex_);
    buffer.initialize_storage_unlocked();
  }
  const auto committed = buffer.credit_pool_reservation_->commit();
  if (!committed.ok()) {
    return committed;
  }
  return std::move(buffer);
}

FixedReorderBuffer::FixedReorderBuffer(ksj::recon::ReorderPlan plan, std::string execution_plan_digest,
                                       std::string verification_record_digest,
                                       std::shared_ptr<ResourceVectorLedger> resource_ledger,
                                       detail::SlabRangeClaim slab_claim, ksj::base::byte* const bookkeeping_storage,
                                       const std::size_t charged_storage_bytes,
                                       FixedReorderBufferHandleSidecar* const handle_sidecars,
                                       const Quantity handle_sidecar_count,
                                       const std::uint64_t buffer_identity) noexcept
    : owned_plan_(std::move(plan)), plan_(&*owned_plan_), execution_plan_digest_(std::move(execution_plan_digest)),
      verification_record_digest_(std::move(verification_record_digest)), resource_ledger_(std::move(resource_ledger)),
      slab_claim_(std::move(slab_claim)), storage_(bookkeeping_storage), storage_bytes_(charged_storage_bytes),
      handle_sidecars_(handle_sidecars), handle_sidecar_count_(handle_sidecar_count), buffer_identity_(buffer_identity),
      free_head_(0U), next_expected_(plan_->first_expected_ordinal()) {}

FixedReorderBuffer::~FixedReorderBuffer() {
  try {
    std::lock_guard lock(mutex_);
    clear_all_handle_sidecars_unlocked();
  } catch (...) {
    // Destruction must not throw. The caller-owned sidecars will still be
    // destroyed by their owner after the buffer/leases have settled.
  }
}

FixedReorderBuffer::FixedReorderBuffer(FixedReorderBuffer&& other) noexcept {
  std::lock_guard lock(other.mutex_);
  move_from_unlocked(other);
}

FixedReorderBuffer& FixedReorderBuffer::operator=(FixedReorderBuffer&& other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(mutex_, other.mutex_);
    move_from_unlocked(other);
  }
  return *this;
}

ksj::base::Status FixedReorderBuffer::bind_m3_reorder_ingress(const std::uint64_t ingress_identity) {
  if (ingress_identity == 0U) {
    return ksj::base::Status::InvalidArgument("M3ReorderIngress identity must be non-zero");
  }
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("FixedReorderBuffer was moved from");
  }
  if (state_ != FixedReorderBufferState::accepting || !credit_pool_reservation_.has_value() ||
      !credit_pool_reservation_->committed()) {
    return ksj::base::Status::StateError("FixedReorderBuffer cannot bind an M3ReorderIngress after terminal control");
  }
  if (m3_reorder_ingress_identity_ != 0U) {
    return ksj::base::Status::StateError("FixedReorderBuffer is already bound to an M3ReorderIngress");
  }
  m3_reorder_ingress_identity_ = ingress_identity;
  return ksj::base::Status::Ok();
}

ksj::base::Status
FixedReorderBuffer::unbind_m3_reorder_ingress_on_create_failure(const std::uint64_t ingress_identity) {
  std::lock_guard lock(mutex_);
  // No M3 ingress capability has been returned on this path, so no trusted
  // dispatch can exist. Do not permit an arbitrary later unbind/rebind.
  if (plan_ == nullptr || ingress_identity == 0U || m3_reorder_ingress_identity_ != ingress_identity ||
      state_ != FixedReorderBufferState::accepting || prepared_dispatches_ != 0U || in_flight_dispatches_ != 0U ||
      publishing_outputs_ != 0U || direct_head_items_ != 0U || retained_items_ != 0U) {
    return {ksj::base::StatusCode::state_error};
  }
  m3_reorder_ingress_identity_ = 0U;
  return ksj::base::Status::Ok();
}

bool FixedReorderBuffer::has_m3_reorder_ingress(const std::uint64_t ingress_identity) const {
  std::lock_guard lock(mutex_);
  return plan_ != nullptr && ingress_identity != 0U && m3_reorder_ingress_identity_ == ingress_identity;
}

ksj::base::Status FixedReorderBuffer::end_of_input_from_m3_reorder_ingress(const std::uint64_t ingress_identity) {
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("FixedReorderBuffer was moved from");
  }
  if (ingress_identity == 0U || m3_reorder_ingress_identity_ != ingress_identity) {
    return ksj::base::Status::StateError("FixedReorderBuffer rejected terminal control from a foreign ingress");
  }
  return end_of_input_unlocked();
}

ksj::base::Status FixedReorderBuffer::abort_from_m3_reorder_ingress(const std::uint64_t ingress_identity) {
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("FixedReorderBuffer was moved from");
  }
  if (ingress_identity == 0U || m3_reorder_ingress_identity_ != ingress_identity) {
    return ksj::base::Status::StateError("FixedReorderBuffer rejected terminal control from a foreign ingress");
  }
  return abort_unlocked();
}

void FixedReorderBuffer::emergency_abort_noexcept() noexcept {
  try {
    std::lock_guard lock(mutex_);
    if (plan_ == nullptr || state_ == FixedReorderBufferState::completed || state_ == FixedReorderBufferState::failed ||
        state_ == FixedReorderBufferState::failed_draining) {
      return;
    }
    // Do not release the committed pool here. This is a no-throw emergency
    // path, so retaining the reservation is safer than attempting a ledger
    // operation that could itself fail while a permit or lease is unwinding.
    state_ = FixedReorderBufferState::failed_draining;
  } catch (...) {
    // No recovery is safe if the mutex/runtime is already exceptional.
  }
}

void FixedReorderBuffer::fail_from_bound_host_noexcept(const std::uint64_t ingress_identity) noexcept {
  try {
    std::lock_guard lock(mutex_);
    if (plan_ == nullptr || ingress_identity == 0U || m3_reorder_ingress_identity_ != ingress_identity ||
        state_ == FixedReorderBufferState::failed || state_ == FixedReorderBufferState::completed) {
      return;
    }
    // fail_unlocked() releases the precommitted pool only when no permit or
    // publish owner remains. Otherwise it preserves failed_draining until the
    // exact live owner settles, which is the same accounting rule as an
    // explicit terminal abort.
    static_cast<void>(fail_unlocked());
  } catch (...) {
    // Host failure has no recovery path; never let a notification throw while
    // a lease destructor or emergency terminal path is unwinding.
  }
}

ksj::base::Result<DispatchPermit>
FixedReorderBuffer::try_prepare_dispatch_from_trusted_completed_frame(const std::uint64_t ingress_identity,
                                                                      const FrameSlotContext& completed_frame_context) {
  std::lock_guard lock(mutex_);
  if (ingress_identity == 0U || m3_reorder_ingress_identity_ != ingress_identity) {
    return ksj::base::Status::StateError("FixedReorderBuffer rejected an unbound M3ReorderIngress");
  }
  const auto accepting = require_accepting_unlocked();
  if (!accepting.ok()) {
    return accepting;
  }
  const auto ordinal = mapper_.ordinal(completed_frame_context);
  if (!ordinal.ok()) {
    // This is not ordinary pressure: the supplied completed-frame semantic
    // key cannot belong to the plan's closed strict-dense domain. Freeze the
    // scan before surfacing the validation cause. fail_unlocked() preserves
    // the precommitted pool until any extant permits/leases settle.
    return fail_closed_unlocked(ordinal.status());
  }
  return try_prepare_dispatch_ordinal_unlocked(ordinal.value());
}

ksj::base::Status FixedReorderBuffer::complete(DispatchPermit& permit, const OpaqueReorderPayloadHandle payload) {
  std::lock_guard lock(mutex_);
  if (!permit_matches_unlocked(permit) || permit.phase_ != DispatchPermit::Phase::in_flight) {
    return ksj::base::Status::StateError("FixedReorderBuffer completion requires one active in-flight DispatchPermit");
  }
  if (!can_run_or_complete_unlocked()) {
    return ksj::base::Status::StateError("FixedReorderBuffer cannot complete after terminal failure or drain");
  }
  if (handle_sidecars_ != nullptr) {
    return fail_closed_unlocked(ksj::base::Status::ValidationError(
      "FixedReorderBuffer typed-sidecar binding requires ImmutableBufferHandle completion"));
  }
  const auto record = read_ordinal(permit.ordinal_);
  if (record.state != OrdinalState::in_flight || record.slot_id != permit.slot_id_) {
    return fail_closed_unlocked(
      ksj::base::Status::StateError("DispatchPermit no longer owns its in-flight reorder ordinal"));
  }
  if (in_flight_dispatches_ == 0U) {
    return fail_closed_unlocked(ksj::base::Status::InternalError("FixedReorderBuffer in-flight accounting underflow"));
  }
  write_ordinal(permit.ordinal_, {.state = OrdinalState::completed, .slot_id = permit.slot_id_, .payload = payload});
  --in_flight_dispatches_;
  ++completed_ordinals_;
  permit.phase_ = DispatchPermit::Phase::completed;
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedReorderBuffer::complete(DispatchPermit& permit, ImmutableBufferHandle& payload) {
  std::lock_guard lock(mutex_);
  if (!permit_matches_unlocked(permit) || permit.phase_ != DispatchPermit::Phase::in_flight) {
    return ksj::base::Status::StateError("FixedReorderBuffer completion requires one active in-flight DispatchPermit");
  }
  if (!can_run_or_complete_unlocked()) {
    return ksj::base::Status::StateError("FixedReorderBuffer cannot complete after terminal failure or drain");
  }
  if (handle_sidecars_ == nullptr || handle_sidecar_count_ != plan_->max_ahead_items()) {
    return fail_closed_unlocked(
      ksj::base::Status::StateError("FixedReorderBuffer has no plan-bound typed handle sidecars"));
  }
  if (!payload.valid()) {
    return fail_closed_unlocked(
      ksj::base::Status::ValidationError("FixedReorderBuffer cannot retain an invalid ImmutableBufferHandle"));
  }
  ksj::recon::Quantity handle_logical_bytes = 0U;
  if (!checked_add(payload.payload_bytes(), payload.metadata_bytes(), handle_logical_bytes) ||
      handle_logical_bytes > plan_->charged_bytes_per_ordinal()) {
    return fail_closed_unlocked(
      ksj::base::Status::ValidationError("FixedReorderBuffer handle exceeds its frozen logical-byte envelope"));
  }
  const auto record = read_ordinal(permit.ordinal_);
  if (record.state != OrdinalState::in_flight || record.slot_id != permit.slot_id_) {
    return fail_closed_unlocked(
      ksj::base::Status::StateError("DispatchPermit no longer owns its in-flight reorder ordinal"));
  }
  if (in_flight_dispatches_ == 0U) {
    return fail_closed_unlocked(ksj::base::Status::InternalError("FixedReorderBuffer in-flight accounting underflow"));
  }
  if (is_direct_head_slot(permit.slot_id_)) {
    if (permit.direct_head_handle_.has_value()) {
      return fail_closed_unlocked(ksj::base::Status::InternalError(
        "FixedReorderBuffer direct current head already owns an ImmutableBufferHandle"));
    }
    permit.direct_head_handle_.emplace(std::move(payload));
  } else {
    auto* const sidecar = handle_sidecar_unlocked(permit.slot_id_);
    if (sidecar == nullptr || sidecar->handle.has_value()) {
      return fail_closed_unlocked(
        ksj::base::Status::InternalError("FixedReorderBuffer typed handle sidecar is unavailable for its active slot"));
    }
    sidecar->handle.emplace(std::move(payload));
  }
  write_ordinal(permit.ordinal_, {.state = OrdinalState::completed,
                                  .slot_id = permit.slot_id_,
                                  .payload = OpaqueReorderPayloadHandle::from_opaque_id(0U)});
  --in_flight_dispatches_;
  ++completed_ordinals_;
  permit.phase_ = DispatchPermit::Phase::completed;
  return ksj::base::Status::Ok();
}

ksj::base::Result<PublishLease> FixedReorderBuffer::try_acquire_publish(DispatchPermit& completed_permit) {
  std::lock_guard lock(mutex_);
  if (!permit_matches_unlocked(completed_permit) || completed_permit.phase_ != DispatchPermit::Phase::completed) {
    return ksj::base::Status::StateError("publish acquisition requires one completed DispatchPermit for this buffer");
  }
  if (state_ != FixedReorderBufferState::accepting && state_ != FixedReorderBufferState::close_pending &&
      state_ != FixedReorderBufferState::draining) {
    return ksj::base::Status::StateError("FixedReorderBuffer cannot publish after terminal failure or completion");
  }
  if (completed_permit.ordinal_ != next_expected_) {
    return ksj::base::Status::Unavailable("completed reorder ordinal is not the current next expected output");
  }
  const auto record = read_ordinal(completed_permit.ordinal_);
  if (record.state != OrdinalState::completed || record.slot_id != completed_permit.slot_id_) {
    return fail_closed_unlocked(
      ksj::base::Status::StateError("completed DispatchPermit no longer owns its reorder ordinal"));
  }
  const auto direct_head = is_direct_head_slot(completed_permit.slot_id_);
  auto* const handle_sidecar = direct_head ? nullptr : handle_sidecar_unlocked(completed_permit.slot_id_);
  if (direct_head) {
    if ((handle_sidecars_ != nullptr && !completed_permit.direct_head_handle_.has_value()) ||
        (handle_sidecars_ == nullptr && completed_permit.direct_head_handle_.has_value())) {
      return fail_closed_unlocked(ksj::base::Status::InternalError(
        "FixedReorderBuffer completed direct head has inconsistent ImmutableBufferHandle ownership"));
    }
  } else if ((handle_sidecars_ != nullptr && (handle_sidecar == nullptr || !handle_sidecar->handle.has_value())) ||
             (handle_sidecars_ == nullptr && handle_sidecar != nullptr) ||
             completed_permit.direct_head_handle_.has_value()) {
    return fail_closed_unlocked(
      ksj::base::Status::InternalError("FixedReorderBuffer completed slot has inconsistent handle-sidecar ownership"));
  }
  write_ordinal(completed_permit.ordinal_,
                {.state = OrdinalState::publishing, .slot_id = completed_permit.slot_id_, .payload = record.payload});
  ++publishing_outputs_;
  completed_permit.phase_ = DispatchPermit::Phase::publishing;
  return PublishLease{this,
                      buffer_identity_,
                      std::move(completed_permit),
                      {.ordinal = next_expected_, .payload = record.payload},
                      handle_sidecar};
}

ksj::base::Result<ImmutableBufferHandle> FixedReorderBuffer::take_published_buffer_handle(PublishLease& lease) {
  std::lock_guard lock(mutex_);
  if (lease.owner_ != this || lease.buffer_identity_ != buffer_identity_ || !permit_matches_unlocked(lease.permit_) ||
      lease.permit_.phase_ != DispatchPermit::Phase::publishing) {
    return ksj::base::Status::StateError("PublishLease does not own an active publishing output for this buffer");
  }
  if (is_direct_head_slot(lease.permit_.slot_id_)) {
    if (handle_sidecars_ == nullptr || lease.handle_sidecar_ != nullptr ||
        !lease.permit_.direct_head_handle_.has_value()) {
      return ksj::base::Status::StateError("PublishLease does not retain a direct current-head ImmutableBufferHandle");
    }
    auto handle = std::move(*lease.permit_.direct_head_handle_);
    lease.permit_.direct_head_handle_.reset();
    return handle;
  }
  if (handle_sidecars_ == nullptr || lease.handle_sidecar_ == nullptr ||
      lease.handle_sidecar_ != handle_sidecar_unlocked(lease.permit_.slot_id_) ||
      !lease.handle_sidecar_->handle.has_value() || lease.permit_.direct_head_handle_.has_value()) {
    return ksj::base::Status::StateError("PublishLease does not retain a plan-bound ImmutableBufferHandle");
  }
  auto handle = std::move(*lease.handle_sidecar_->handle);
  lease.handle_sidecar_->handle.reset();
  return handle;
}

bool FixedReorderBuffer::publish_lease_has_buffer_handle(const PublishLease& lease) const {
  std::lock_guard lock(mutex_);
  if (lease.owner_ != this || lease.buffer_identity_ != buffer_identity_ || !permit_matches_unlocked(lease.permit_) ||
      lease.permit_.phase_ != DispatchPermit::Phase::publishing || handle_sidecars_ == nullptr) {
    return false;
  }
  if (is_direct_head_slot(lease.permit_.slot_id_)) {
    return lease.handle_sidecar_ == nullptr && lease.permit_.direct_head_handle_.has_value();
  }
  return lease.handle_sidecar_ != nullptr && lease.handle_sidecar_ == handle_sidecar_unlocked(lease.permit_.slot_id_) &&
         lease.handle_sidecar_->handle.has_value() && !lease.permit_.direct_head_handle_.has_value();
}

ksj::base::Status FixedReorderBuffer::end_of_input() {
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("FixedReorderBuffer was moved from");
  }
  if (m3_reorder_ingress_identity_ != 0U) {
    return ksj::base::Status::StateError("FixedReorderBuffer terminal control is owned by its bound M3ReorderIngress");
  }
  return end_of_input_unlocked();
}

ksj::base::Status FixedReorderBuffer::end_of_input_unlocked() {
  if (state_ == FixedReorderBufferState::accepting) {
    state_ = FixedReorderBufferState::close_pending;
  } else if (state_ != FixedReorderBufferState::close_pending) {
    return ksj::base::Status::StateError("EndOfInput was already resolved or the reorder buffer is terminal");
  }
  if (prepared_dispatches_ != 0U || in_flight_dispatches_ != 0U) {
    return ksj::base::Status::Unavailable("EndOfInput waits for prepared and in-flight reorder dispatches to quiesce");
  }
  if (completed_ordinals_ != plan_->ordinal_domain_bound()) {
    (void)fail_unlocked();
    return ksj::base::Status::ValidationError("REORDER_GAP_AT_EOI");
  }
  if (next_expected_ == plan_->ordinal_domain_bound()) {
    state_ = FixedReorderBufferState::completed;
    return release_credit_pool_unlocked();
  }
  state_ = FixedReorderBufferState::draining;
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedReorderBuffer::abort() {
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("FixedReorderBuffer was moved from");
  }
  if (m3_reorder_ingress_identity_ != 0U) {
    return ksj::base::Status::StateError("FixedReorderBuffer terminal control is owned by its bound M3ReorderIngress");
  }
  return abort_unlocked();
}

ksj::base::Status FixedReorderBuffer::abort_unlocked() {
  if (state_ == FixedReorderBufferState::failed) {
    return ksj::base::Status::Ok();
  }
  if (state_ == FixedReorderBufferState::completed) {
    return ksj::base::Status::StateError("cannot abort a completed FixedReorderBuffer");
  }
  return fail_unlocked();
}

FixedReorderBufferSnapshot FixedReorderBuffer::snapshot() const {
  std::lock_guard lock(mutex_);
  return {.state = state_,
          .next_expected_ordinal = next_expected_,
          .completed_ordinals = completed_ordinals_,
          .prepared_dispatches = prepared_dispatches_,
          .in_flight_dispatches = in_flight_dispatches_,
          .publishing_outputs = publishing_outputs_,
          .direct_head_items = direct_head_items_,
          .retained_items = retained_items_,
          .retained_charged_bytes = retained_charged_bytes_,
          .free_slots = plan_ == nullptr ? 0U : plan_->max_ahead_items() - retained_items_,
          .storage_bytes = storage_bytes_,
          .credit_pool_committed = credit_pool_reservation_.has_value() && credit_pool_reservation_->committed()};
}

ksj::base::Status FixedReorderBuffer::validate_plan(const ksj::recon::ReorderPlan& plan) {
  const auto identity = validate_frozen_plan_identity(plan);
  if (!identity.ok()) {
    return identity;
  }
  const auto mapper = DenseCartesianOrdinalMapper::create(plan);
  if (!mapper.ok()) {
    return mapper.status();
  }
  return ksj::base::Status::Ok();
}

ksj::base::Result<ksj::recon::ResourceVector>
FixedReorderBuffer::credit_pool_reservation(const ksj::recon::ReorderPlan& plan) {
  return ksj::recon::ResourceVector::create(
    {.host_normal_bytes = plan.host_metadata_charged_bytes(), .descriptor_count = plan.descriptor_charged_count()},
    "FixedReorderBuffer precommitted metadata and descriptor pool");
}

ksj::base::Result<DispatchPermit>
FixedReorderBuffer::try_prepare_dispatch_ordinal_unlocked(const ksj::recon::Quantity ordinal) {
  const auto ordinal_status = validate_ordinal_unlocked(ordinal);
  if (!ordinal_status.ok()) {
    // Defensive backstop for the private mapper path: an ordinal outside the
    // closed domain is the same strict-dense semantic violation as an
    // unmappable FrameSlotContext, not a retryable admission condition.
    return fail_closed_unlocked(ordinal_status);
  }
  if (ordinal < next_expected_) {
    // An already published / observed tuple is a duplicate occurrence. M3
    // has no retry or overwrite semantics for strict dense ordinals.
    return fail_closed_unlocked(
      ksj::base::Status::ValidationError("completed FrameSlot ordinal is behind next_expected"));
  }
  const auto local_credit_status = validate_local_credit_invariants_unlocked();
  if (!local_credit_status.ok()) {
    return fail_closed_unlocked(local_credit_status);
  }
  const auto direct_head = ordinal == next_expected_;
  // `max_ahead_items` is exactly the number of retained outputs with
  // ordinal > next_expected. The head itself has a separate direct
  // capability, so an ahead distance equal to the bound is legal.
  if (!direct_head && ordinal - next_expected_ > plan_->max_ahead_items()) {
    return ksj::base::Status::Unavailable("completed FrameSlot ordinal exceeds the frozen ahead window");
  }
  if (!direct_head && retained_items_ == plan_->max_ahead_items()) {
    return ksj::base::Status::Unavailable("FixedReorderBuffer ahead item/descriptor credits are exhausted");
  }

  const auto ordinal_record = read_ordinal(ordinal);
  if (ordinal_record.state != OrdinalState::never_seen) {
    return fail_closed_unlocked(
      ksj::base::Status::ValidationError("duplicate completed FrameSlot ordinal for ReorderPlan output domain"));
  }
  if (direct_head) {
    if (direct_head_items_ != 0U) {
      return fail_closed_unlocked(
        ksj::base::Status::InternalError("FixedReorderBuffer has more than one direct current-head dispatch"));
    }
    write_ordinal(ordinal, {.state = OrdinalState::prepared,
                            .slot_id = kDirectHeadSlot,
                            .payload = OpaqueReorderPayloadHandle::from_opaque_id(0U)});
    ++prepared_dispatches_;
    ++direct_head_items_;
    return DispatchPermit{this, plan_, buffer_identity_, ordinal, kDirectHeadSlot};
  }

  auto slot = read_slot(free_head_);
  if (!slot.free || slot.stable_slot_id != free_head_ ||
      (slot.next_free_or_owner != kNoSlot && slot.next_free_or_owner >= plan_->max_ahead_items())) {
    return fail_closed_unlocked(
      ksj::base::Status::InternalError("FixedReorderBuffer physical free-list record is invalid"));
  }
  const auto* const sidecar = handle_sidecar_unlocked(free_head_);
  if (sidecar != nullptr && sidecar->handle.has_value()) {
    return fail_closed_unlocked(
      ksj::base::Status::InternalError("FixedReorderBuffer free slot retains an ImmutableBufferHandle"));
  }
  ksj::recon::Quantity next_retained_bytes = 0U;
  if (!checked_add(retained_charged_bytes_, plan_->charged_bytes_per_ordinal(), next_retained_bytes) ||
      next_retained_bytes > plan_->max_ahead_charged_bytes()) {
    return fail_closed_unlocked(
      ksj::base::Status::InternalError("FixedReorderBuffer ahead-byte credit accounting is inconsistent"));
  }

  const auto slot_id = free_head_;
  free_head_ = slot.next_free_or_owner;
  slot.free = false;
  slot.next_free_or_owner = ordinal;
  write_slot(slot_id, slot);
  write_ordinal(
    ordinal,
    {.state = OrdinalState::prepared, .slot_id = slot_id, .payload = OpaqueReorderPayloadHandle::from_opaque_id(0U)});
  ++prepared_dispatches_;
  ++retained_items_;
  retained_charged_bytes_ = next_retained_bytes;
  return DispatchPermit{this, plan_, buffer_identity_, ordinal, slot_id};
}

ksj::base::Status FixedReorderBuffer::commit_dispatch(DispatchPermit& permit) {
  std::lock_guard lock(mutex_);
  if (!permit_matches_unlocked(permit) || permit.phase_ != DispatchPermit::Phase::prepared) {
    return ksj::base::Status::StateError("DispatchPermit is not a prepared permit for this reorder buffer");
  }
  if (!can_run_or_complete_unlocked()) {
    return ksj::base::Status::StateError("FixedReorderBuffer cannot start dispatch after terminal failure or drain");
  }
  const auto record = read_ordinal(permit.ordinal_);
  if (record.state != OrdinalState::prepared || record.slot_id != permit.slot_id_) {
    return fail_closed_unlocked(
      ksj::base::Status::StateError("DispatchPermit no longer owns its prepared reorder ordinal"));
  }
  if (prepared_dispatches_ == 0U) {
    return fail_closed_unlocked(
      ksj::base::Status::InternalError("FixedReorderBuffer prepared-dispatch accounting underflow"));
  }
  write_ordinal(permit.ordinal_, {.state = OrdinalState::in_flight,
                                  .slot_id = permit.slot_id_,
                                  .payload = OpaqueReorderPayloadHandle::from_opaque_id(0U)});
  --prepared_dispatches_;
  ++in_flight_dispatches_;
  permit.phase_ = DispatchPermit::Phase::in_flight;
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedReorderBuffer::abort_dispatch(DispatchPermit& permit) {
  std::lock_guard lock(mutex_);
  if (!permit_matches_unlocked(permit)) {
    return ksj::base::Status::StateError("DispatchPermit does not belong to this reorder buffer");
  }
  const auto failed = fail_unlocked();
  if (!failed.ok()) {
    return failed;
  }
  return discard_failed_dispatch_unlocked(permit);
}

ksj::base::Status FixedReorderBuffer::acknowledge_published(PublishLease& lease) {
  std::lock_guard lock(mutex_);
  if (lease.owner_ != this || lease.buffer_identity_ != buffer_identity_ || !permit_matches_unlocked(lease.permit_) ||
      lease.permit_.phase_ != DispatchPermit::Phase::publishing) {
    return ksj::base::Status::StateError("PublishLease does not own an active publishing output for this buffer");
  }
  if (state_ != FixedReorderBufferState::accepting && state_ != FixedReorderBufferState::close_pending &&
      state_ != FixedReorderBufferState::draining && state_ != FixedReorderBufferState::failed_draining) {
    return ksj::base::Status::StateError("FixedReorderBuffer cannot acknowledge an already-settled output");
  }
  if (lease.permit_.ordinal_ != next_expected_) {
    return fail_closed_unlocked(
      ksj::base::Status::InternalError("publishing ordinal is not the current next expected output"));
  }
  const auto record = read_ordinal(lease.permit_.ordinal_);
  if (record.state != OrdinalState::publishing || record.slot_id != lease.permit_.slot_id_) {
    return fail_closed_unlocked(
      ksj::base::Status::StateError("PublishLease no longer owns its publishing reorder ordinal"));
  }
  if (publishing_outputs_ == 0U) {
    return fail_closed_unlocked(
      ksj::base::Status::InternalError("FixedReorderBuffer publishing-output accounting underflow"));
  }
  const auto direct_head = is_direct_head_slot(lease.permit_.slot_id_);
  if ((direct_head && lease.handle_sidecar_ != nullptr) ||
      (!direct_head &&
       ((handle_sidecars_ == nullptr && lease.handle_sidecar_ != nullptr) ||
        (handle_sidecars_ != nullptr && lease.handle_sidecar_ != handle_sidecar_unlocked(lease.permit_.slot_id_))))) {
    return fail_closed_unlocked(
      ksj::base::Status::InternalError("PublishLease handle-sidecar binding is inconsistent"));
  }
  if (lease.permit_.direct_head_handle_.has_value() ||
      (lease.handle_sidecar_ != nullptr && lease.handle_sidecar_->handle.has_value())) {
    // A typed M3.7 payload is inseparable from its pre-reserved downstream
    // DataEdgePlan credit. Raw PublishLease acknowledgement cannot discard
    // it: only M3PublishLease::commit_to_edge() may first transfer the handle
    // out of this sidecar. Treat a bypass attempt as terminal so no payload
    // becomes silently unaccounted.
    return fail_closed_unlocked(
      ksj::base::Status::StateError("typed PublishLease requires ordered edge handoff before acknowledgement"));
  }
  if (direct_head) {
    if (direct_head_items_ == 0U) {
      return fail_closed_unlocked(
        ksj::base::Status::InternalError("FixedReorderBuffer direct current-head accounting underflow"));
    }
    --direct_head_items_;
  } else {
    clear_handle_sidecar_unlocked(lease.permit_.slot_id_);
    const auto slot_status = release_slot_unlocked(lease.permit_.slot_id_, lease.permit_.ordinal_);
    if (!slot_status.ok()) {
      return fail_closed_unlocked(slot_status);
    }
  }
  write_ordinal(
    lease.permit_.ordinal_,
    {.state = OrdinalState::acknowledged, .slot_id = 0U, .payload = OpaqueReorderPayloadHandle::from_opaque_id(0U)});
  --publishing_outputs_;
  ++next_expected_;
  if (state_ == FixedReorderBufferState::draining && next_expected_ == plan_->ordinal_domain_bound()) {
    state_ = FixedReorderBufferState::completed;
    const auto released = release_credit_pool_unlocked();
    if (!released.ok()) {
      return released;
    }
  }
  lease.permit_.disarm();
  lease.disarm();
  return finalize_failed_if_quiescent_unlocked();
}

void FixedReorderBuffer::abandon_dispatch_noexcept(DispatchPermit& permit) noexcept {
  try {
    std::lock_guard lock(mutex_);
    if (!permit_matches_unlocked(permit)) {
      return;
    }
    if (permit.phase_ == DispatchPermit::Phase::prepared) {
      const auto record = read_ordinal(permit.ordinal_);
      if (record.state == OrdinalState::prepared && record.slot_id == permit.slot_id_ &&
          (state_ == FixedReorderBufferState::accepting || state_ == FixedReorderBufferState::close_pending)) {
        if (prepared_dispatches_ == 0U) {
          (void)fail_closed_unlocked(
            ksj::base::Status::InternalError("FixedReorderBuffer prepared-dispatch accounting underflow"));
          return;
        }
        if (is_direct_head_slot(permit.slot_id_)) {
          if (direct_head_items_ == 0U) {
            (void)fail_closed_unlocked(
              ksj::base::Status::InternalError("FixedReorderBuffer direct current-head accounting underflow"));
            return;
          }
          --direct_head_items_;
        } else {
          clear_handle_sidecar_unlocked(permit.slot_id_);
          const auto released = release_slot_unlocked(permit.slot_id_, permit.ordinal_);
          if (!released.ok()) {
            (void)fail_closed_unlocked(released);
            return;
          }
        }
        write_ordinal(permit.ordinal_, {.state = OrdinalState::never_seen,
                                        .slot_id = 0U,
                                        .payload = OpaqueReorderPayloadHandle::from_opaque_id(0U)});
        --prepared_dispatches_;
      } else {
        (void)fail_unlocked();
        (void)discard_failed_dispatch_unlocked(permit);
      }
    } else if (permit.phase_ != DispatchPermit::Phase::invalid) {
      (void)fail_unlocked();
      (void)discard_failed_dispatch_unlocked(permit);
    }
  } catch (...) {
    // Destruction cannot surface an exception. The containing runtime treats
    // a failed permit cleanup as terminal rather than attempting a retry.
  }
}

void FixedReorderBuffer::abandon_publish_noexcept(PublishLease& lease) noexcept {
  try {
    std::lock_guard lock(mutex_);
    if (lease.owner_ == this && lease.buffer_identity_ == buffer_identity_ && permit_matches_unlocked(lease.permit_) &&
        lease.permit_.phase_ == DispatchPermit::Phase::publishing) {
      (void)fail_unlocked();
      (void)discard_failed_publish_unlocked(lease);
    }
  } catch (...) {
    // See DispatchPermit::release_noexcept().
  }
}

bool FixedReorderBuffer::permit_matches_unlocked(const DispatchPermit& permit) const noexcept {
  return plan_ != nullptr && permit.owner_ == this && permit.plan_ == plan_ &&
         permit.buffer_identity_ == buffer_identity_ && permit.phase_ != DispatchPermit::Phase::invalid;
}

bool FixedReorderBuffer::can_run_or_complete_unlocked() const noexcept {
  return state_ == FixedReorderBufferState::accepting || state_ == FixedReorderBufferState::close_pending;
}

ksj::base::Status FixedReorderBuffer::require_accepting_unlocked() const {
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("FixedReorderBuffer was moved from");
  }
  if (state_ != FixedReorderBufferState::accepting) {
    return ksj::base::Status::StateError("FixedReorderBuffer no longer accepts dispatch after EndOfInput");
  }
  if (!credit_pool_reservation_.has_value() || !credit_pool_reservation_->committed()) {
    return ksj::base::Status::StateError("FixedReorderBuffer precommitted credit pool is not live");
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedReorderBuffer::validate_ordinal_unlocked(const ksj::recon::Quantity ordinal) const {
  if (ordinal >= plan_->ordinal_domain_bound()) {
    return ksj::base::Status::ValidationError("completed FrameSlot ordinal is outside the closed dense domain");
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedReorderBuffer::validate_local_credit_invariants_unlocked() const {
  if (direct_head_items_ > 1U) {
    return ksj::base::Status::InternalError("FixedReorderBuffer has more than one direct current-head dispatch");
  }
  if (retained_items_ > plan_->max_ahead_items()) {
    return ksj::base::Status::InternalError("FixedReorderBuffer retained-item count exceeds its frozen capacity");
  }
  ksj::recon::Quantity expected_retained_bytes = 0U;
  if (!checked_multiply(retained_items_, plan_->charged_bytes_per_ordinal(), expected_retained_bytes) ||
      retained_charged_bytes_ != expected_retained_bytes) {
    return ksj::base::Status::InternalError("FixedReorderBuffer local byte-credit accounting is inconsistent");
  }
  const auto full = retained_items_ == plan_->max_ahead_items();
  if ((free_head_ == kNoSlot) != full) {
    return ksj::base::Status::InternalError("FixedReorderBuffer free-list availability disagrees with local credits");
  }
  if (!full && free_head_ >= plan_->max_ahead_items()) {
    return ksj::base::Status::InternalError("FixedReorderBuffer free-list head is outside the physical slot range");
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedReorderBuffer::release_slot_unlocked(const ksj::recon::Quantity slot_id,
                                                            const ksj::recon::Quantity expected_ordinal) {
  const auto local_credit_status = validate_local_credit_invariants_unlocked();
  if (!local_credit_status.ok()) {
    return local_credit_status;
  }
  if (slot_id >= plan_->max_ahead_items()) {
    return ksj::base::Status::InternalError("FixedReorderBuffer release slot is outside the physical slot range");
  }
  auto slot = read_slot(slot_id);
  if (slot.free || slot.stable_slot_id != slot_id || slot.next_free_or_owner != expected_ordinal ||
      (free_head_ != kNoSlot && free_head_ >= plan_->max_ahead_items())) {
    return ksj::base::Status::InternalError("FixedReorderBuffer physical slot release is invalid");
  }
  if (retained_items_ == 0U || retained_charged_bytes_ < plan_->charged_bytes_per_ordinal()) {
    return ksj::base::Status::InternalError("FixedReorderBuffer local credit accounting underflow");
  }
  const auto* const sidecar = handle_sidecar_unlocked(slot_id);
  if (sidecar != nullptr && sidecar->handle.has_value()) {
    return ksj::base::Status::InternalError(
      "FixedReorderBuffer attempted to recycle a slot retaining an ImmutableBufferHandle");
  }
  slot.free = true;
  slot.next_free_or_owner = free_head_;
  write_slot(slot_id, slot);
  free_head_ = slot_id;
  --retained_items_;
  retained_charged_bytes_ -= plan_->charged_bytes_per_ordinal();
  return ksj::base::Status::Ok();
}

FixedReorderBufferHandleSidecar*
FixedReorderBuffer::handle_sidecar_unlocked(const ksj::recon::Quantity slot_id) noexcept {
  if (handle_sidecars_ == nullptr || slot_id >= handle_sidecar_count_) {
    return nullptr;
  }
  return handle_sidecars_ + static_cast<std::size_t>(slot_id);
}

const FixedReorderBufferHandleSidecar*
FixedReorderBuffer::handle_sidecar_unlocked(const ksj::recon::Quantity slot_id) const noexcept {
  if (handle_sidecars_ == nullptr || slot_id >= handle_sidecar_count_) {
    return nullptr;
  }
  return handle_sidecars_ + static_cast<std::size_t>(slot_id);
}

void FixedReorderBuffer::clear_handle_sidecar_unlocked(const ksj::recon::Quantity slot_id) noexcept {
  auto* const sidecar = handle_sidecar_unlocked(slot_id);
  if (sidecar != nullptr) {
    sidecar->handle.reset();
  }
}

void FixedReorderBuffer::clear_all_handle_sidecars_unlocked() noexcept {
  for (Quantity slot_id = 0U; slot_id < handle_sidecar_count_; ++slot_id) {
    clear_handle_sidecar_unlocked(slot_id);
  }
}

ksj::base::Status FixedReorderBuffer::fail_closed_unlocked(ksj::base::Status cause) {
  const auto failed = fail_unlocked();
  return failed.ok() ? cause : failed;
}

ksj::base::Status FixedReorderBuffer::fail_unlocked() {
  if (state_ == FixedReorderBufferState::failed) {
    return ksj::base::Status::Ok();
  }
  if (state_ != FixedReorderBufferState::failed_draining) {
    state_ = FixedReorderBufferState::failed_draining;
  }
  return finalize_failed_if_quiescent_unlocked();
}

ksj::base::Status FixedReorderBuffer::finalize_failed_if_quiescent_unlocked() {
  if (state_ != FixedReorderBufferState::failed_draining) {
    return ksj::base::Status::Ok();
  }
  if (prepared_dispatches_ != 0U || in_flight_dispatches_ != 0U || publishing_outputs_ != 0U ||
      direct_head_items_ != 0U || retained_items_ != 0U) {
    return ksj::base::Status::Ok();
  }
  const auto released = release_credit_pool_unlocked();
  if (!released.ok()) {
    return released;
  }
  state_ = FixedReorderBufferState::failed;
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedReorderBuffer::discard_failed_dispatch_unlocked(DispatchPermit& permit) {
  if (!permit_matches_unlocked(permit)) {
    return ksj::base::Status::StateError("DispatchPermit does not belong to this failed reorder buffer");
  }
  if (state_ != FixedReorderBufferState::failed_draining) {
    return ksj::base::Status::StateError("DispatchPermit cannot settle outside failed-draining state");
  }

  const auto record = read_ordinal(permit.ordinal_);
  if (record.slot_id != permit.slot_id_) {
    return ksj::base::Status::StateError("DispatchPermit no longer owns its failed reorder ordinal");
  }
  switch (permit.phase_) {
    case DispatchPermit::Phase::prepared:
      if (record.state != OrdinalState::prepared || prepared_dispatches_ == 0U) {
        return ksj::base::Status::StateError("prepared DispatchPermit cannot settle failed reorder state");
      }
      break;
    case DispatchPermit::Phase::in_flight:
      if (record.state != OrdinalState::in_flight || in_flight_dispatches_ == 0U) {
        return ksj::base::Status::StateError("in-flight DispatchPermit cannot settle failed reorder state");
      }
      break;
    case DispatchPermit::Phase::completed:
      if (record.state != OrdinalState::completed) {
        return ksj::base::Status::StateError("completed DispatchPermit cannot settle failed reorder state");
      }
      break;
    case DispatchPermit::Phase::publishing:
      if (record.state != OrdinalState::publishing || publishing_outputs_ == 0U) {
        return ksj::base::Status::StateError("publishing DispatchPermit cannot settle failed reorder state");
      }
      break;
    case DispatchPermit::Phase::invalid:
      return ksj::base::Status::StateError("invalid DispatchPermit cannot settle failed reorder state");
  }
  if (is_direct_head_slot(permit.slot_id_)) {
    if (direct_head_items_ == 0U) {
      return ksj::base::Status::InternalError("FixedReorderBuffer direct current-head accounting underflow");
    }
    permit.direct_head_handle_.reset();
    --direct_head_items_;
  } else {
    clear_handle_sidecar_unlocked(permit.slot_id_);
    const auto released_slot = release_slot_unlocked(permit.slot_id_, permit.ordinal_);
    if (!released_slot.ok()) {
      return released_slot;
    }
  }
  switch (permit.phase_) {
    case DispatchPermit::Phase::prepared:
      --prepared_dispatches_;
      break;
    case DispatchPermit::Phase::in_flight:
      --in_flight_dispatches_;
      break;
    case DispatchPermit::Phase::completed:
      break;
    case DispatchPermit::Phase::publishing:
      --publishing_outputs_;
      break;
    case DispatchPermit::Phase::invalid:
      return ksj::base::Status::InternalError("invalid DispatchPermit passed failed-reorder settlement validation");
  }
  write_ordinal(
    permit.ordinal_,
    {.state = OrdinalState::discarded, .slot_id = 0U, .payload = OpaqueReorderPayloadHandle::from_opaque_id(0U)});
  permit.disarm();
  return finalize_failed_if_quiescent_unlocked();
}

ksj::base::Status FixedReorderBuffer::discard_failed_publish_unlocked(PublishLease& lease) {
  if (lease.owner_ != this || lease.buffer_identity_ != buffer_identity_ || !permit_matches_unlocked(lease.permit_) ||
      lease.permit_.phase_ != DispatchPermit::Phase::publishing) {
    return ksj::base::Status::StateError("PublishLease does not belong to this failed reorder buffer");
  }
  return discard_failed_dispatch_unlocked(lease.permit_);
}

ksj::base::Status FixedReorderBuffer::release_credit_pool_unlocked() {
  if (!credit_pool_reservation_.has_value()) {
    return ksj::base::Status::Ok();
  }
  const auto released = credit_pool_reservation_->release();
  if (!released.ok()) {
    return released;
  }
  credit_pool_reservation_.reset();
  return ksj::base::Status::Ok();
}

FixedReorderBuffer::OrdinalRecord FixedReorderBuffer::read_ordinal(const ksj::recon::Quantity ordinal) const noexcept {
  const auto* const record = storage_ + static_cast<std::size_t>(ordinal) * kOrdinalRecordBytes;
  const auto packed = read_u64(record + kFirstWordOffset);
  const auto payload = read_u64(record + kSecondWordOffset);
  return {.state = static_cast<OrdinalState>(packed >> kOrdinalStateShift),
          .slot_id = packed & kOrdinalSlotMask,
          .payload = OpaqueReorderPayloadHandle::from_opaque_id(payload)};
}

void FixedReorderBuffer::write_ordinal(const ksj::recon::Quantity ordinal, const OrdinalRecord record) noexcept {
  const auto packed =
    (static_cast<std::uint64_t>(record.state) << kOrdinalStateShift) | (record.slot_id & kOrdinalSlotMask);
  auto* const destination = storage_ + static_cast<std::size_t>(ordinal) * kOrdinalRecordBytes;
  write_u64(destination + kFirstWordOffset, packed);
  write_u64(destination + kSecondWordOffset, record.payload.opaque_id());
}

FixedReorderBuffer::PhysicalSlotRecord
FixedReorderBuffer::read_slot(const ksj::recon::Quantity slot_id) const noexcept {
  const auto ordinal_bytes = static_cast<std::size_t>(plan_->ordinal_domain_bound()) * kOrdinalRecordBytes;
  const auto* const record = storage_ + ordinal_bytes + static_cast<std::size_t>(slot_id) * kPhysicalSlotRecordBytes;
  const auto packed = read_u64(record + kFirstWordOffset);
  if ((packed & kPhysicalFreeFlag) == 0U) {
    return {.free = false, .next_free_or_owner = packed, .stable_slot_id = read_u64(record + kSecondWordOffset)};
  }
  return {.free = true,
          .next_free_or_owner = packed == kNoSlot ? kNoSlot : packed & ~kPhysicalFreeFlag,
          .stable_slot_id = read_u64(record + kSecondWordOffset)};
}

void FixedReorderBuffer::write_slot(const ksj::recon::Quantity slot_id, const PhysicalSlotRecord record) noexcept {
  const auto ordinal_bytes = static_cast<std::size_t>(plan_->ordinal_domain_bound()) * kOrdinalRecordBytes;
  auto* const destination = storage_ + ordinal_bytes + static_cast<std::size_t>(slot_id) * kPhysicalSlotRecordBytes;
  const auto packed =
    record.free ? (record.next_free_or_owner == kNoSlot ? kNoSlot : kPhysicalFreeFlag | record.next_free_or_owner)
                : record.next_free_or_owner;
  write_u64(destination + kFirstWordOffset, packed);
  write_u64(destination + kSecondWordOffset, record.stable_slot_id);
}

void FixedReorderBuffer::initialize_storage_unlocked() noexcept {
  for (ksj::recon::Quantity ordinal = 0U; ordinal < plan_->ordinal_domain_bound(); ++ordinal) {
    write_ordinal(
      ordinal,
      {.state = OrdinalState::never_seen, .slot_id = 0U, .payload = OpaqueReorderPayloadHandle::from_opaque_id(0U)});
  }
  for (ksj::recon::Quantity slot_id = 0U; slot_id < plan_->max_ahead_items(); ++slot_id) {
    clear_handle_sidecar_unlocked(slot_id);
    const auto next = slot_id + 1U == plan_->max_ahead_items() ? kNoSlot : slot_id + 1U;
    write_slot(slot_id, {.free = true, .next_free_or_owner = next, .stable_slot_id = slot_id});
  }
  free_head_ = 0U;
  next_expected_ = plan_->first_expected_ordinal();
  completed_ordinals_ = 0U;
  prepared_dispatches_ = 0U;
  in_flight_dispatches_ = 0U;
  publishing_outputs_ = 0U;
  direct_head_items_ = 0U;
  retained_items_ = 0U;
  retained_charged_bytes_ = 0U;
  state_ = FixedReorderBufferState::accepting;
}

void FixedReorderBuffer::move_from_unlocked(FixedReorderBuffer& other) noexcept {
  owned_plan_ = std::move(other.owned_plan_);
  plan_ = owned_plan_.has_value() ? &*owned_plan_ : nullptr;
  execution_plan_digest_ = std::move(other.execution_plan_digest_);
  verification_record_digest_ = std::move(other.verification_record_digest_);
  mapper_ = other.mapper_;
  mapper_.plan_ = plan_;
  resource_ledger_ = std::move(other.resource_ledger_);
  credit_pool_reservation_ = std::move(other.credit_pool_reservation_);
  slab_claim_ = std::move(other.slab_claim_);
  storage_ = other.storage_;
  storage_bytes_ = other.storage_bytes_;
  handle_sidecars_ = other.handle_sidecars_;
  handle_sidecar_count_ = other.handle_sidecar_count_;
  buffer_identity_ = other.buffer_identity_;
  m3_reorder_ingress_identity_ = other.m3_reorder_ingress_identity_;
  free_head_ = other.free_head_;
  next_expected_ = other.next_expected_;
  completed_ordinals_ = other.completed_ordinals_;
  prepared_dispatches_ = other.prepared_dispatches_;
  in_flight_dispatches_ = other.in_flight_dispatches_;
  publishing_outputs_ = other.publishing_outputs_;
  direct_head_items_ = other.direct_head_items_;
  retained_items_ = other.retained_items_;
  retained_charged_bytes_ = other.retained_charged_bytes_;
  state_ = other.state_;

  other.owned_plan_.reset();
  other.plan_ = nullptr;
  other.execution_plan_digest_.clear();
  other.verification_record_digest_.clear();
  other.mapper_ = DenseCartesianOrdinalMapper{nullptr, {}, 0U};
  other.storage_ = nullptr;
  other.storage_bytes_ = 0U;
  other.handle_sidecars_ = nullptr;
  other.handle_sidecar_count_ = 0U;
  other.buffer_identity_ = 0U;
  other.m3_reorder_ingress_identity_ = 0U;
  other.free_head_ = kNoSlot;
  other.next_expected_ = 0U;
  other.completed_ordinals_ = 0U;
  other.prepared_dispatches_ = 0U;
  other.in_flight_dispatches_ = 0U;
  other.publishing_outputs_ = 0U;
  other.direct_head_items_ = 0U;
  other.retained_items_ = 0U;
  other.retained_charged_bytes_ = 0U;
  other.state_ = FixedReorderBufferState::failed;
}

} // namespace ksj::recon::runtime
