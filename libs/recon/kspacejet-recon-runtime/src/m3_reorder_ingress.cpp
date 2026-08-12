#include "kspacejet/recon/runtime/m3_reorder_ingress.hpp"

#include <atomic>
#include <limits>
#include <string>
#include <utility>

namespace ksj::recon::runtime {
namespace {

std::atomic<std::uint64_t> g_next_m3_reorder_ingress_identity{1U};

[[nodiscard]] ksj::base::Result<std::uint64_t> allocate_ingress_identity() {
  auto identity = g_next_m3_reorder_ingress_identity.load(std::memory_order_relaxed);
  for (;;) {
    if (identity == 0U || identity == std::numeric_limits<std::uint64_t>::max()) {
      return ksj::base::Status::Unavailable("M3ReorderIngress process-local identity space is exhausted");
    }
    if (g_next_m3_reorder_ingress_identity.compare_exchange_weak(identity, identity + 1U, std::memory_order_relaxed,
                                                                 std::memory_order_relaxed)) {
      return identity;
    }
  }
}

[[nodiscard]] const ksj::recon::ReorderPlan* find_reorder_plan(const ksj::recon::ExecutionPlan& execution_plan,
                                                               const std::string_view node_id) noexcept {
  const ksj::recon::ReorderPlan* result = nullptr;
  for (const auto& candidate : execution_plan.reorder_plans()) {
    if (candidate.node_id() != node_id) {
      continue;
    }
    if (result != nullptr) {
      return nullptr;
    }
    result = &candidate;
  }
  return result;
}

} // namespace

M3PublishLease::M3PublishLease(PublishLease publish, CompletedFrameLease completed_frame) noexcept
    : publish_(std::move(publish)), completed_frame_(std::move(completed_frame)) {}

M3PublishLease::~M3PublishLease() {
  release_noexcept();
}

M3PublishLease::M3PublishLease(M3PublishLease&& other) noexcept
    : publish_(std::move(other.publish_)), completed_frame_(std::move(other.completed_frame_)) {}

M3PublishLease& M3PublishLease::operator=(M3PublishLease&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    publish_ = std::move(other.publish_);
    completed_frame_ = std::move(other.completed_frame_);
  }
  return *this;
}

bool M3PublishLease::valid() const noexcept {
  return publish_.valid();
}

const FixedReorderOutput& M3PublishLease::output() const noexcept {
  return publish_.output();
}

ksj::base::Status M3PublishLease::acknowledge_published() {
  if (!publish_.valid()) {
    return ksj::base::Status::StateError("acknowledge_published requires a live M3PublishLease");
  }
  if (completed_frame_.host_failed_noexcept()) {
    // The output may already have been observed while this publish lease was
    // live, but it cannot be settled as a normal scan result after any other
    // source lease has failed. Dropping the raw lease closes the paired
    // reorder state and releases its credit through the normal failed drain.
    fail_closed_noexcept();
    return {ksj::base::StatusCode::state_error};
  }
  ksj::base::Status acknowledged = ksj::base::Status::Ok();
  try {
    acknowledged = publish_.acknowledge_published();
  } catch (...) {
    fail_closed_noexcept();
    return {ksj::base::StatusCode::internal_error};
  }
  if (!acknowledged.ok()) {
    fail_closed_noexcept();
    return acknowledged;
  }
  completed_frame_.release_terminal_authority_noexcept();
  return ksj::base::Status::Ok();
}

void M3PublishLease::fail_closed_noexcept() noexcept {
  // The host capability uses shared state rather than a raw HostFrameAssembler
  // pointer, so this remains safe even if the outer host object was destroyed
  // after its source slot was recycled.
  completed_frame_.emergency_abandon_noexcept();
  publish_ = PublishLease{};
}

void M3PublishLease::release_noexcept() noexcept {
  if (publish_.valid()) {
    fail_closed_noexcept();
    return;
  }
  // Normal acknowledgement already disarmed this retained capability. This
  // fallback also releases a moved/default object's empty shared state.
  completed_frame_.release_terminal_authority_noexcept();
}

FrameDispatch::FrameDispatch(FixedReorderBuffer* const buffer, const std::uint64_t ingress_identity,
                             CompletedFrameLease completed_frame, DispatchPermit permit) noexcept
    : buffer_(buffer), ingress_identity_(ingress_identity), completed_frame_(std::move(completed_frame)),
      permit_(std::move(permit)), phase_(Phase::prepared) {}

FrameDispatch::~FrameDispatch() {
  release_noexcept();
}

FrameDispatch::FrameDispatch(FrameDispatch&& other) noexcept
    : buffer_(other.buffer_), ingress_identity_(other.ingress_identity_),
      completed_frame_(std::move(other.completed_frame_)), permit_(std::move(other.permit_)), phase_(other.phase_) {
  other.disarm_after_move();
}

FrameDispatch& FrameDispatch::operator=(FrameDispatch&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    buffer_ = other.buffer_;
    ingress_identity_ = other.ingress_identity_;
    completed_frame_ = std::move(other.completed_frame_);
    permit_ = std::move(other.permit_);
    phase_ = other.phase_;
    other.disarm_after_move();
  }
  return *this;
}

bool FrameDispatch::has_live_authority() const noexcept {
  return buffer_ != nullptr && ingress_identity_ != 0U;
}

bool FrameDispatch::has_active_dispatch() const noexcept {
  return has_live_authority() && phase_ != Phase::invalid && phase_ != Phase::published && phase_ != Phase::settled &&
         (permit_.valid() || completed_frame_.valid());
}

bool FrameDispatch::valid() const noexcept {
  return has_active_dispatch();
}

ksj::recon::Quantity FrameDispatch::ordinal() const noexcept {
  return permit_.valid() ? permit_.ordinal() : 0U;
}

ksj::base::Status FrameDispatch::require_phase(const Phase expected, const std::string_view operation) {
  if (!has_live_authority()) {
    return ksj::base::Status::StateError(std::string(operation) + " requires a live FrameDispatch");
  }
  if (!buffer_->has_m3_reorder_ingress(ingress_identity_)) {
    return ksj::base::Status::StateError(std::string(operation) + " received a foreign or moved FrameDispatch");
  }
  if (phase_ == Phase::published || phase_ == Phase::settled || phase_ == Phase::invalid) {
    // After publish transfer, M3PublishLease is the sole owner of both the
    // raw publish permit and its post-ack host terminal authority. An old
    // FrameDispatch is merely a moved/transferred observer and cannot mutate
    // either component.
    return ksj::base::Status::StateError(std::string(operation) + " targets a transferred or settled FrameDispatch");
  }
  if (phase_ != expected || !permit_.valid() || !completed_frame_.valid()) {
    // A moved-from dispatch has no authority above and is rejected without
    // touching an unrelated scan. Reaching this branch means the exact bound
    // dispatch was double-consumed or otherwise violated its local state.
    static_cast<void>(fail_closed_and_settle());
    return ksj::base::Status::StateError(std::string(operation) + " violates the FrameDispatch lifecycle");
  }
  return ksj::base::Status::Ok();
}

ksj::base::Result<ksj::base::ConstByteSpan> FrameDispatch::input_bytes() {
  const auto phase_status = require_phase(Phase::in_flight, "input_bytes");
  if (!phase_status.ok()) {
    return phase_status;
  }
  const auto bytes = completed_frame_.bytes();
  if (!bytes.ok()) {
    static_cast<void>(fail_closed_and_settle());
    return bytes.status();
  }
  return bytes.value();
}

ksj::base::Result<FrameSlotContext> FrameDispatch::input_context() {
  const auto phase_status = require_phase(Phase::in_flight, "input_context");
  if (!phase_status.ok()) {
    return phase_status;
  }
  const auto context = completed_frame_.context();
  if (!context.ok()) {
    static_cast<void>(fail_closed_and_settle());
    return context.status();
  }
  return context.value();
}

ksj::base::Status FrameDispatch::commit() {
  const auto phase_status = require_phase(Phase::prepared, "commit");
  if (!phase_status.ok()) {
    return phase_status;
  }
  const auto committed = permit_.commit();
  if (!committed.ok()) {
    static_cast<void>(fail_closed_and_settle());
    return committed;
  }
  phase_ = Phase::in_flight;
  return ksj::base::Status::Ok();
}

ksj::base::Status FrameDispatch::complete(const OpaqueReorderPayloadHandle payload) {
  const auto phase_status = require_phase(Phase::in_flight, "complete");
  if (!phase_status.ok()) {
    return phase_status;
  }
  const auto completed = buffer_->complete(permit_, payload);
  if (!completed.ok()) {
    static_cast<void>(fail_closed_and_settle());
    return completed;
  }
  const auto acknowledged = completed_frame_.acknowledge_consumed_from_m3_reorder_ingress(ingress_identity_);
  if (!acknowledged.ok()) {
    static_cast<void>(fail_closed_and_settle());
    return acknowledged;
  }
  phase_ = Phase::completed;
  return ksj::base::Status::Ok();
}

ksj::base::Result<M3PublishLease> FrameDispatch::try_acquire_publish() {
  if (!has_live_authority()) {
    return ksj::base::Status::StateError("try_acquire_publish requires a live FrameDispatch");
  }
  if (!buffer_->has_m3_reorder_ingress(ingress_identity_)) {
    return ksj::base::Status::StateError("try_acquire_publish received a foreign or moved FrameDispatch");
  }
  if (phase_ == Phase::published || phase_ == Phase::settled || phase_ == Phase::invalid) {
    return ksj::base::Status::StateError("try_acquire_publish targets a transferred or settled FrameDispatch");
  }
  if (phase_ != Phase::completed || !permit_.valid() || completed_frame_.valid()) {
    static_cast<void>(fail_closed_and_settle());
    return ksj::base::Status::StateError("try_acquire_publish violates the FrameDispatch lifecycle");
  }
  if (completed_frame_.host_failed_noexcept()) {
    static_cast<void>(fail_closed_and_settle());
    return {ksj::base::StatusCode::state_error};
  }
  auto publish = buffer_->try_acquire_publish(permit_);
  if (!publish.ok()) {
    if (publish.status().code() != ksj::base::StatusCode::unavailable) {
      static_cast<void>(fail_closed_and_settle());
    }
    return publish.status();
  }
  phase_ = Phase::published;
  return M3PublishLease{std::move(publish).value(), std::move(completed_frame_)};
}

ksj::base::Status FrameDispatch::abort() {
  if (!has_live_authority()) {
    return ksj::base::Status::StateError("abort requires a live FrameDispatch");
  }
  if (!buffer_->has_m3_reorder_ingress(ingress_identity_)) {
    return ksj::base::Status::StateError("abort received a foreign or moved FrameDispatch");
  }
  if (phase_ == Phase::invalid || phase_ == Phase::published || phase_ == Phase::settled) {
    return ksj::base::Status::StateError("abort targets a transferred or settled FrameDispatch");
  }
  return fail_closed_and_settle();
}

ksj::base::Status FrameDispatch::fail_closed_and_settle() {
  if (!has_live_authority()) {
    return ksj::base::Status::StateError("FrameDispatch has no live owning ingress");
  }
  ksj::base::Status result = ksj::base::Status::Ok();
  bool cleanup_threw = false;
  try {
    result = buffer_->abort_from_m3_reorder_ingress(ingress_identity_);
  } catch (...) {
    cleanup_threw = true;
    buffer_->emergency_abort_noexcept();
  }
  if (permit_.valid()) {
    ksj::base::Status permit_settlement = ksj::base::Status::Ok();
    try {
      permit_settlement = permit_.abort();
    } catch (...) {
      cleanup_threw = true;
      permit_.release_noexcept();
    }
    if (result.ok() && !permit_settlement.ok()) {
      result = permit_settlement;
    }
  }
  if (completed_frame_.valid()) {
    ksj::base::Status frame_settlement = ksj::base::Status::Ok();
    try {
      frame_settlement = completed_frame_.abandon_from_m3_reorder_ingress(ingress_identity_);
    } catch (...) {
      cleanup_threw = true;
      completed_frame_.emergency_abandon_noexcept();
    }
    if (result.ok() && !frame_settlement.ok()) {
      result = frame_settlement;
    }
  }
  // acknowledge_consumed() deliberately recycles the source slot before the
  // reorder permit is published, while retaining a private shared-state
  // terminal capability.  Every downstream failure must still fail that
  // source scan, including a completed dispatch dropped before publish.
  completed_frame_.emergency_abandon_noexcept();
  phase_ = Phase::settled;
  if (result.ok() && cleanup_threw) {
    return {ksj::base::StatusCode::internal_error};
  }
  return result;
}

void FrameDispatch::release_noexcept() noexcept {
  if (!has_active_dispatch()) {
    return;
  }
  try {
    static_cast<void>(fail_closed_and_settle());
  } catch (...) {
    // Destruction cannot expose an exception. The owning Buffer/Host paths
    // already transition toward terminal failure before any dynamic message
    // construction in their cleanup helpers.
  }
}

void FrameDispatch::disarm_after_move() noexcept {
  buffer_ = nullptr;
  ingress_identity_ = 0U;
  phase_ = Phase::invalid;
}

M3ReorderIngress::M3ReorderIngress(ksj::recon::ExecutionPlan execution_plan,
                                   ksj::recon::VerificationRecord verification_record, std::string node_id,
                                   std::string completed_frame_input_port, HostFrameAssembler* const assembler,
                                   FixedReorderBuffer* const reorder_buffer,
                                   const std::uint64_t ingress_identity) noexcept
    : execution_plan_(std::move(execution_plan)), verification_record_(std::move(verification_record)),
      node_id_(std::move(node_id)), completed_frame_input_port_(std::move(completed_frame_input_port)),
      assembler_(assembler), reorder_buffer_(reorder_buffer), ingress_identity_(ingress_identity) {}

M3ReorderIngress::M3ReorderIngress(M3ReorderIngress&& other) noexcept
    : execution_plan_(std::move(other.execution_plan_)), verification_record_(std::move(other.verification_record_)),
      node_id_(std::move(other.node_id_)), completed_frame_input_port_(std::move(other.completed_frame_input_port_)),
      assembler_(other.assembler_), reorder_buffer_(other.reorder_buffer_), ingress_identity_(other.ingress_identity_),
      bound_(other.bound_), input_closed_(other.input_closed_), terminal_(other.terminal_) {
  other.disarm_after_move();
}

M3ReorderIngress::~M3ReorderIngress() {
  release_noexcept();
}

ksj::base::Result<M3ReorderIngress> M3ReorderIngress::create(const ksj::recon::ExecutionPlan& execution_plan,
                                                             const ksj::recon::VerificationRecord& verification_record,
                                                             const std::string_view node_id,
                                                             HostFrameAssembler& assembler,
                                                             FixedReorderBuffer& reorder_buffer) {
  if (node_id.empty()) {
    return ksj::base::Status::InvalidArgument("M3ReorderIngress requires a non-empty reorder node id");
  }
  if (verification_record.execution_plan_digest() != execution_plan.digest() ||
      verification_record.execution_profile() != execution_plan.execution_profile()) {
    return ksj::base::Status::ValidationError(
      "M3ReorderIngress VerificationRecord does not bind the supplied ExecutionPlan/profile");
  }
  const auto* const reorder_plan = find_reorder_plan(execution_plan, node_id);
  if (reorder_plan == nullptr || reorder_plan->completed_frame_input_port().empty()) {
    return ksj::base::Status::ValidationError(
      "M3ReorderIngress requires exactly one ReorderPlan with a completed-frame input port");
  }
  if (reorder_buffer.plan_ == nullptr || reorder_buffer.execution_plan_digest_ != execution_plan.digest().value() ||
      reorder_buffer.verification_record_digest_ != verification_record.digest().value() ||
      reorder_buffer.plan_->node_id() != node_id ||
      reorder_buffer.plan_->completed_frame_input_port() != reorder_plan->completed_frame_input_port()) {
    return ksj::base::Status::ValidationError(
      "M3ReorderIngress artifacts/node/input port do not match the FixedReorderBuffer immutable binding");
  }
  if (!assembler.matches_m3_reorder_ingress_binding(execution_plan, verification_record, node_id,
                                                    reorder_plan->completed_frame_input_port())) {
    return ksj::base::Status::ValidationError(
      "M3ReorderIngress requires a pristine HostFrameAssembler with matching artifacts/node/input port");
  }
  // Construct every potentially allocating/copying control-plane value before
  // permanently binding the scan-local buffer. A failed construction therefore
  // leaves the buffer available and unmodified.
  const auto identity = allocate_ingress_identity();
  if (!identity.ok()) {
    return identity.status();
  }
  M3ReorderIngress candidate{
    execution_plan, verification_record, std::string(node_id), reorder_plan->completed_frame_input_port(),
    &assembler,     &reorder_buffer,     identity.value()};
  const auto buffer_bound = reorder_buffer.bind_m3_reorder_ingress(identity.value());
  if (!buffer_bound.ok()) {
    return buffer_bound;
  }
  const auto rollback_buffer_binding = [&reorder_buffer, ingress_identity = identity.value()]() noexcept {
    try {
      const auto rollback = reorder_buffer.unbind_m3_reorder_ingress_on_create_failure(ingress_identity);
      if (!rollback.ok()) {
        reorder_buffer.emergency_abort_noexcept();
      }
    } catch (...) {
      reorder_buffer.emergency_abort_noexcept();
    }
  };
  try {
    const auto host_bound = assembler.bind_m3_reorder_ingress(identity.value(), reorder_buffer);
    if (!host_bound.ok()) {
      rollback_buffer_binding();
      return host_bound;
    }
  } catch (...) {
    rollback_buffer_binding();
    return {ksj::base::StatusCode::internal_error};
  }
  candidate.bound_ = true;
  return std::move(candidate);
}

bool M3ReorderIngress::has_live_authority() const noexcept {
  return bound_ && !terminal_ && assembler_ != nullptr && reorder_buffer_ != nullptr && ingress_identity_ != 0U;
}

ksj::base::Result<FrameDispatch> M3ReorderIngress::try_prepare(CompletedFrameLease& lease) {
  if (!has_live_authority() || !reorder_buffer_->has_m3_reorder_ingress(ingress_identity_) ||
      !assembler_->has_m3_reorder_ingress(ingress_identity_)) {
    return ksj::base::Status::StateError("M3ReorderIngress is invalid, moved from, or no longer buffer-bound");
  }
  if (input_closed_) {
    return ksj::base::Status::StateError("M3ReorderIngress no longer accepts a CompletedFrameLease after EndOfInput");
  }
  if (!assembler_->has_same_issuer_state(lease)) {
    return ksj::base::Status::StateError("M3ReorderIngress received a foreign or moved CompletedFrameLease issuer");
  }
  if (!lease.valid() || !assembler_->owns(lease)) {
    return fail_closed_for_same_scan(lease, {ksj::base::StatusCode::state_error});
  }
  switch (lease.binding_status(execution_plan_, verification_record_, node_id_, completed_frame_input_port_)) {
    case CompletedFrameLeaseBindingStatus::match:
      break;
    case CompletedFrameLeaseBindingStatus::foreign:
      return ksj::base::Status::StateError("M3ReorderIngress received a foreign CompletedFrameLease binding");
    case CompletedFrameLeaseBindingStatus::stale_or_consumed:
      return fail_closed_for_same_scan(lease, {ksj::base::StatusCode::state_error});
  }

  const auto context = lease.context();
  if (!context.ok()) {
    return fail_closed_for_same_scan(lease, context.status());
  }
  auto prepared =
    reorder_buffer_->try_prepare_dispatch_from_trusted_completed_frame(ingress_identity_, context.value());
  if (!prepared.ok()) {
    if (prepared.status().code() == ksj::base::StatusCode::unavailable) {
      return prepared.status();
    }
    return fail_closed_for_same_scan(lease, prepared.status());
  }
  ksj::base::Status dispatch_started = ksj::base::Status::Ok();
  try {
    dispatch_started = lease.begin_dispatch_from_m3_reorder_ingress(ingress_identity_);
  } catch (...) {
    auto permit = std::move(prepared).value();
    try {
      static_cast<void>(reorder_buffer_->abort_from_m3_reorder_ingress(ingress_identity_));
    } catch (...) {
      reorder_buffer_->emergency_abort_noexcept();
    }
    try {
      static_cast<void>(permit.abort());
    } catch (...) {
      permit.release_noexcept();
    }
    lease.emergency_abandon_noexcept();
    terminate_components_noexcept();
    return {ksj::base::StatusCode::internal_error};
  }
  if (!dispatch_started.ok()) {
    auto permit = std::move(prepared).value();
    try {
      static_cast<void>(reorder_buffer_->abort_from_m3_reorder_ingress(ingress_identity_));
    } catch (...) {
      reorder_buffer_->emergency_abort_noexcept();
    }
    try {
      static_cast<void>(permit.abort());
    } catch (...) {
      permit.release_noexcept();
    }
    try {
      static_cast<void>(lease.abandon_from_m3_reorder_ingress(ingress_identity_));
    } catch (...) {
      lease.emergency_abandon_noexcept();
    }
    terminate_components_noexcept();
    return dispatch_started;
  }
  return FrameDispatch{reorder_buffer_, ingress_identity_, std::move(lease), std::move(prepared).value()};
}

ksj::base::Status M3ReorderIngress::end_of_input() {
  if (!has_live_authority() || !reorder_buffer_->has_m3_reorder_ingress(ingress_identity_) ||
      !assembler_->has_m3_reorder_ingress(ingress_identity_)) {
    return ksj::base::Status::StateError("M3ReorderIngress is invalid, moved from, or no longer buffer-bound");
  }
  if (input_closed_) {
    return ksj::base::Status::StateError("M3ReorderIngress EndOfInput was already accepted");
  }
  ksj::base::Status source_status = ksj::base::Status::Ok();
  try {
    source_status = assembler_->end_of_input_from_m3_reorder_ingress(ingress_identity_);
  } catch (...) {
    terminate_components_noexcept();
    return {ksj::base::StatusCode::internal_error};
  }
  if (!source_status.ok()) {
    if (source_status.code() != ksj::base::StatusCode::unavailable) {
      terminate_components_noexcept();
    }
    return source_status;
  }
  ksj::base::Status reorder_status = ksj::base::Status::Ok();
  try {
    reorder_status = reorder_buffer_->end_of_input_from_m3_reorder_ingress(ingress_identity_);
  } catch (...) {
    terminate_components_noexcept();
    return {ksj::base::StatusCode::internal_error};
  }
  if (!reorder_status.ok()) {
    if (reorder_status.code() != ksj::base::StatusCode::unavailable) {
      terminate_components_noexcept();
    }
    return reorder_status;
  }
  input_closed_ = true;
  FixedReorderBufferState state = FixedReorderBufferState::failed;
  try {
    state = reorder_buffer_->snapshot().state;
  } catch (...) {
    terminate_components_noexcept();
    return {ksj::base::StatusCode::internal_error};
  }
  terminal_ = state == FixedReorderBufferState::completed || state == FixedReorderBufferState::failed;
  if (terminal_) {
    detach_host_failure_notifier_noexcept();
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status M3ReorderIngress::abort() {
  if (!bound_ || terminal_ || assembler_ == nullptr || reorder_buffer_ == nullptr || ingress_identity_ == 0U ||
      !reorder_buffer_->has_m3_reorder_ingress(ingress_identity_) ||
      !assembler_->has_m3_reorder_ingress(ingress_identity_)) {
    return ksj::base::Status::StateError("M3ReorderIngress is invalid, moved from, or no longer buffer-bound");
  }
  const auto state = reorder_buffer_->snapshot().state;
  if (state == FixedReorderBufferState::completed) {
    terminal_ = true;
    detach_host_failure_notifier_noexcept();
    return ksj::base::Status::StateError("M3ReorderIngress cannot abort an already-resolved reorder buffer");
  }
  if (state == FixedReorderBufferState::failed) {
    terminate_components_noexcept();
    return ksj::base::Status::StateError("M3ReorderIngress cannot abort an already-failed reorder buffer");
  }
  ksj::base::Status buffer_status = ksj::base::Status::Ok();
  ksj::base::Status assembler_status = ksj::base::Status::Ok();
  bool cleanup_threw = false;
  try {
    buffer_status = reorder_buffer_->abort_from_m3_reorder_ingress(ingress_identity_);
  } catch (...) {
    cleanup_threw = true;
    reorder_buffer_->emergency_abort_noexcept();
  }
  try {
    assembler_status = assembler_->abort_from_m3_reorder_ingress(ingress_identity_);
  } catch (...) {
    cleanup_threw = true;
    assembler_->emergency_abort_noexcept();
  }
  terminal_ = true;
  detach_host_failure_notifier_noexcept();
  if (cleanup_threw) {
    return ksj::base::Status::InternalError("M3ReorderIngress abort cleanup threw");
  }
  return buffer_status.ok() ? assembler_status : buffer_status;
}

FixedReorderBufferSnapshot M3ReorderIngress::snapshot() const {
  return reorder_buffer_ == nullptr ? FixedReorderBufferSnapshot{} : reorder_buffer_->snapshot();
}

ksj::base::Status M3ReorderIngress::fail_closed_for_same_scan(CompletedFrameLease& lease,
                                                              ksj::base::Status cause) noexcept {
  terminate_components_noexcept();
  try {
    if (lease.valid()) {
      static_cast<void>(lease.abandon_from_m3_reorder_ingress(ingress_identity_));
    }
  } catch (...) {
    // The coupled components are already terminal.  Fall through to the
    // state-based emergency close below even if the ordinary lease settlement
    // could not construct a diagnostic.
  }
  lease.emergency_abandon_noexcept();
  return cause;
}

void M3ReorderIngress::disarm_after_move() noexcept {
  assembler_ = nullptr;
  reorder_buffer_ = nullptr;
  ingress_identity_ = 0U;
  bound_ = false;
  input_closed_ = true;
  terminal_ = true;
  node_id_.clear();
  completed_frame_input_port_.clear();
}

void M3ReorderIngress::detach_host_failure_notifier_noexcept() noexcept {
  if (assembler_ != nullptr && ingress_identity_ != 0U) {
    assembler_->detach_m3_reorder_failure_notifier_after_terminal(ingress_identity_);
  }
}

void M3ReorderIngress::terminate_components_noexcept() noexcept {
  if (reorder_buffer_ != nullptr) {
    try {
      static_cast<void>(reorder_buffer_->abort_from_m3_reorder_ingress(ingress_identity_));
    } catch (...) {
      reorder_buffer_->emergency_abort_noexcept();
    }
  }
  if (assembler_ != nullptr) {
    try {
      static_cast<void>(assembler_->abort_from_m3_reorder_ingress(ingress_identity_));
    } catch (...) {
      assembler_->emergency_abort_noexcept();
    }
  }
  terminal_ = true;
  detach_host_failure_notifier_noexcept();
}

void M3ReorderIngress::release_noexcept() noexcept {
  if (!bound_ || terminal_ || ingress_identity_ == 0U) {
    return;
  }
  if (reorder_buffer_ != nullptr) {
    try {
      const auto state = reorder_buffer_->snapshot().state;
      if (state == FixedReorderBufferState::completed) {
        terminal_ = true;
        detach_host_failure_notifier_noexcept();
        return;
      }
    } catch (...) {
      // Fall through to coupled emergency cancellation below.
    }
  }
  terminate_components_noexcept();
}

} // namespace ksj::recon::runtime
