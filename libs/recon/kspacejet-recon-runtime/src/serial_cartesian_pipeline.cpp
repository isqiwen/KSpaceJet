#include "kspacejet/recon/runtime/serial_cartesian_pipeline.hpp"

#include <array>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace ksj::recon::runtime {
namespace {

[[nodiscard]] constexpr bool same_semantic_key(const FrameSemanticKey& lhs, const FrameSemanticKey& rhs) noexcept {
  return lhs == rhs;
}

[[nodiscard]] constexpr bool same_context(const FrameSlotContext& lhs, const FrameSlotContext& rhs) noexcept {
  return same_semantic_key(lhs.semantic_key, rhs.semantic_key) && lhs.order_key == rhs.order_key &&
         lhs.placement_key == rhs.placement_key;
}

[[nodiscard]] constexpr bool context_less(const FrameSlotContext& lhs, const FrameSlotContext& rhs) noexcept {
  if (lhs.order_key != rhs.order_key) {
    return lhs.order_key < rhs.order_key;
  }
  if (lhs.semantic_key.encoding_space != rhs.semantic_key.encoding_space) {
    return lhs.semantic_key.encoding_space < rhs.semantic_key.encoding_space;
  }
  if (lhs.semantic_key.slice != rhs.semantic_key.slice) {
    return lhs.semantic_key.slice < rhs.semantic_key.slice;
  }
  if (lhs.semantic_key.contrast != rhs.semantic_key.contrast) {
    return lhs.semantic_key.contrast < rhs.semantic_key.contrast;
  }
  if (lhs.semantic_key.repetition != rhs.semantic_key.repetition) {
    return lhs.semantic_key.repetition < rhs.semantic_key.repetition;
  }
  if (lhs.semantic_key.set != rhs.semantic_key.set) {
    return lhs.semantic_key.set < rhs.semantic_key.set;
  }
  if (lhs.semantic_key.phase != rhs.semantic_key.phase) {
    return lhs.semantic_key.phase < rhs.semantic_key.phase;
  }
  if (lhs.semantic_key.average != rhs.semantic_key.average) {
    return lhs.semantic_key.average < rhs.semantic_key.average;
  }
  return lhs.placement_key < rhs.placement_key;
}

[[nodiscard]] constexpr std::size_t lane_index(const AcquisitionLane lane) noexcept {
  return static_cast<std::size_t>(lane);
}

[[nodiscard]] bool same_slot_contract(const CartesianFrameSlotConfig& lhs,
                                      const CartesianFrameSlotConfig& rhs) noexcept {
  return lhs.dimensions.readout_samples == rhs.dimensions.readout_samples &&
         lhs.dimensions.phase_encode_1 == rhs.dimensions.phase_encode_1 &&
         lhs.dimensions.phase_encode_2 == rhs.dimensions.phase_encode_2 &&
         lhs.dimensions.channels == rhs.dimensions.channels &&
         lhs.dimensions.bytes_per_sample == rhs.dimensions.bytes_per_sample &&
         lhs.completion.required_indices == rhs.completion.required_indices &&
         lhs.resource_upper_bound.max_total_arrivals == rhs.resource_upper_bound.max_total_arrivals &&
         lhs.resource_upper_bound.max_duplicate_arrivals == rhs.resource_upper_bound.max_duplicate_arrivals &&
         lhs.resource_upper_bound.max_payload_bytes == rhs.resource_upper_bound.max_payload_bytes &&
         lhs.duplicate_policy == rhs.duplicate_policy && lhs.incomplete_policy == rhs.incomplete_policy;
}

[[nodiscard]] bool is_quarantineable(const FrameSlotState state) noexcept {
  return state == FrameSlotState::filling || state == FrameSlotState::ready || state == FrameSlotState::computing ||
         state == FrameSlotState::emitting;
}

[[nodiscard]] bool exceeds_quantity(const std::size_t actual, const ksj::recon::Quantity bound) noexcept {
  if constexpr (sizeof(std::size_t) < sizeof(ksj::recon::Quantity)) {
    if (bound > static_cast<ksj::recon::Quantity>(std::numeric_limits<std::size_t>::max())) {
      return false;
    }
  }
  if constexpr (sizeof(std::size_t) > sizeof(ksj::recon::Quantity)) {
    if (actual > static_cast<std::size_t>(std::numeric_limits<ksj::recon::Quantity>::max())) {
      return true;
    }
  }
  return actual > static_cast<std::size_t>(bound);
}

[[nodiscard]] bool checked_multiply(const std::uint64_t lhs, const std::uint64_t rhs, std::uint64_t& result) noexcept {
  if (lhs != 0U && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

[[nodiscard]] ksj::base::Status validate_frame_slot_envelope(const CartesianFrameSlotConfig& frame_slot,
                                                             const ksj::recon::TargetEnvelope& envelope) {
  const auto& dimensions = frame_slot.dimensions;
  if (dimensions.readout_samples > envelope.max_samples_per_acquisition()) {
    return ksj::base::Status::ValidationError(
      "Cartesian FrameSlot readout_samples exceeds TargetEnvelope.max_samples_per_acquisition");
  }
  if (dimensions.channels > envelope.max_active_channels()) {
    return ksj::base::Status::ValidationError(
      "Cartesian FrameSlot channels exceeds TargetEnvelope.max_active_channels");
  }

  std::uint64_t storage_bytes = static_cast<std::uint64_t>(dimensions.readout_samples);
  for (const auto factor :
       {static_cast<std::uint64_t>(dimensions.phase_encode_1), static_cast<std::uint64_t>(dimensions.phase_encode_2),
        static_cast<std::uint64_t>(dimensions.channels), static_cast<std::uint64_t>(dimensions.bytes_per_sample)}) {
    if (!checked_multiply(storage_bytes, factor, storage_bytes)) {
      return ksj::base::Status::ValidationError(
        "Cartesian FrameSlot storage calculation overflows before TargetEnvelope validation");
    }
  }
  if (storage_bytes > envelope.max_frame_charged_bytes()) {
    return ksj::base::Status::ValidationError(
      "Cartesian FrameSlot storage exceeds TargetEnvelope.max_frame_charged_bytes");
  }
  return ksj::base::Status::Ok();
}

} // namespace

std::string_view to_string(const SerialIngressDisposition disposition) noexcept {
  static constexpr std::array names{
    "routed_to_frame_slot",
    "classified_non_imaging",
    "recorded_explicitly_ignored",
  };
  return names.at(static_cast<std::size_t>(disposition));
}

ksj::base::Result<SerialCartesianPipeline> SerialCartesianPipeline::create(SerialCartesianPipelineConfig config) {
  if (!config.on_sealed_frame) {
    return ksj::base::Status::InvalidArgument("SerialCartesianPipeline requires a synchronous sealed-frame callback");
  }
  if (config.frame_slots.empty()) {
    return ksj::base::Status::InvalidArgument("SerialCartesianPipeline requires at least one Cartesian FrameSlot");
  }
  if (config.max_terminal_frame_records == 0U) {
    return ksj::base::Status::InvalidArgument(
      "SerialCartesianPipeline max_terminal_frame_records must be a finite non-zero plan bound");
  }
  if (config.max_explicitly_ignored_records == 0U) {
    return ksj::base::Status::InvalidArgument(
      "SerialCartesianPipeline max_explicitly_ignored_records must be a finite non-zero plan bound");
  }
  if (config.target_envelope.has_value()) {
    for (const auto& frame_slot : config.frame_slots) {
      const auto envelope_status = validate_frame_slot_envelope(frame_slot, *config.target_envelope);
      if (!envelope_status.ok()) {
        return envelope_status;
      }
    }
  }

  auto classifier = AcquisitionClassifier::create(std::move(config.classifier));
  if (!classifier.ok()) {
    return classifier.status();
  }

  try {
    std::vector<SlotRecord> slots;
    slots.reserve(config.frame_slots.size());
    for (auto& slot_config : config.frame_slots) {
      const auto duplicate_slot = [&slots, slot_id = slot_config.slot_id]() {
        for (const auto& existing : slots) {
          if (existing.frame_slot.config().slot_id == slot_id) {
            return true;
          }
        }
        return false;
      }();
      if (duplicate_slot) {
        return ksj::base::Status::ValidationError("SerialCartesianPipeline FrameSlot ids must be unique");
      }

      auto frame_slot = CartesianFrameSlot::create(std::move(slot_config));
      if (!frame_slot.ok()) {
        return frame_slot.status();
      }
      if (!slots.empty() && !same_slot_contract(slots.front().frame_slot.config(), frame_slot.value().config())) {
        return ksj::base::Status::ValidationError(
          "SerialCartesianPipeline FrameSlots must share one Cartesian assembly contract");
      }
      slots.push_back({.frame_slot = std::move(frame_slot).value()});
    }

    SerialCartesianPipeline pipeline{
      std::move(classifier).value(),         std::move(slots),
      std::move(config.on_sealed_frame),     config.max_terminal_frame_records,
      config.max_explicitly_ignored_records, std::move(config.target_envelope),
    };
    pipeline.terminal_frame_records_.reserve(pipeline.max_terminal_frame_records_);
    pipeline.explicitly_ignored_records_.reserve(pipeline.max_explicitly_ignored_records_);
    return pipeline;
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate SerialCartesianPipeline bounded state");
  } catch (const std::length_error&) {
    return ksj::base::Status::OutOfMemory("SerialCartesianPipeline plan bounds exceed host vector capacity");
  }
}

SerialCartesianPipeline::SerialCartesianPipeline(AcquisitionClassifier classifier, std::vector<SlotRecord> slots,
                                                 SerialFrameCallback on_sealed_frame,
                                                 const std::size_t max_terminal_frame_records,
                                                 const std::size_t max_explicitly_ignored_records,
                                                 std::optional<ksj::recon::TargetEnvelope> target_envelope) noexcept
    : classifier_(std::move(classifier)), slots_(std::move(slots)), on_sealed_frame_(std::move(on_sealed_frame)),
      max_terminal_frame_records_(max_terminal_frame_records),
      max_explicitly_ignored_records_(max_explicitly_ignored_records), target_envelope_(std::move(target_envelope)) {}

ksj::base::Status SerialCartesianPipeline::start() {
  const auto callback_status = require_not_in_callback("start");
  if (!callback_status.ok()) {
    return callback_status;
  }
  if (lifecycle_.state() != ScanState::session_candidate) {
    return ksj::base::Status::StateError("SerialCartesianPipeline start is valid only before admission");
  }

  const std::array transitions{
    &ScanLifecycle::begin_describing, &ScanLifecycle::begin_planning, &ScanLifecycle::begin_verifying,
    &ScanLifecycle::begin_admitting,  &ScanLifecycle::admit,          &ScanLifecycle::start,
  };
  for (const auto transition : transitions) {
    const auto status = (lifecycle_.*transition)();
    if (!status.ok()) {
      return status;
    }
  }
  return ksj::base::Status::Ok();
}

ksj::base::Result<SerialIngressReceipt>
SerialCartesianPipeline::submit(const NormalizedCartesianAcquisitionFrame& frame) {
  const auto running_status = require_running("submit");
  if (!running_status.ok()) {
    return running_status;
  }

  const auto envelope_status = validate_ingress(frame.ingress_facts, frame.payload.size());
  if (!envelope_status.ok()) {
    return fail_internal(envelope_status);
  }

  auto classification = classifier_.classify(frame.classification_input);
  if (!classification.ok()) {
    return fail_internal(classification.status());
  }
  const auto classified = classification.value();
  const auto index = lane_index(classified.lane);
  if (index >= arrivals_by_lane_.size()) {
    return fail_internal(ksj::base::Status::InternalError("acquisition classifier returned an unknown lane"));
  }
  ++arrivals_by_lane_.at(index);

  if (!is_imaging_lane(classified.lane)) {
    if (classified.lane == AcquisitionLane::ignored_explicitly) {
      if (explicitly_ignored_records_.size() == max_explicitly_ignored_records_) {
        return fail_internal(
          ksj::base::Status::Unavailable("explicitly ignored acquisition record bound is exhausted"));
      }
      explicitly_ignored_records_.push_back({
        .classification = classified,
        .frame_context = frame.frame_context,
        .coordinate = frame.coordinate,
        .payload_bytes = frame.payload.size(),
      });
      return SerialIngressReceipt{
        .classification = classified,
        .disposition = SerialIngressDisposition::recorded_explicitly_ignored,
      };
    }
    return SerialIngressReceipt{
      .classification = classified,
      .disposition = SerialIngressDisposition::classified_non_imaging,
    };
  }

  auto* slot = find_active_slot(frame.frame_context);
  if (slot == nullptr) {
    auto acquired = acquire_slot(frame.frame_context);
    if (!acquired.ok()) {
      return fail_internal(acquired.status());
    }
    slot = acquired.value();
  }

  const auto scatter_status = slot->frame_slot.scatter(slot->token, frame.coordinate, frame.payload);
  if (!scatter_status.ok()) {
    return fail_internal(scatter_status);
  }
  if (classified.lane == AcquisitionLane::imaging) {
    ++slot->imaging_arrivals;
  } else {
    ++slot->calibration_and_imaging_arrivals;
  }

  const auto finalize_status = finalize_available_slots();
  if (!finalize_status.ok()) {
    return finalize_status;
  }
  return SerialIngressReceipt{
    .classification = classified,
    .disposition = SerialIngressDisposition::routed_to_frame_slot,
  };
}

ksj::base::Status SerialCartesianPipeline::end_of_input() {
  const auto running_status = require_running("end_of_input");
  if (!running_status.ok()) {
    return running_status;
  }

  auto status = lifecycle_.close_ingress();
  if (!status.ok()) {
    return fail_internal(status);
  }
  status = lifecycle_.begin_draining();
  if (!status.ok()) {
    return fail_internal(status);
  }

  for (auto& slot : slots_) {
    if (!slot.active) {
      continue;
    }
    const auto disposition = slot.frame_slot.end_of_input(slot.token);
    if (!disposition.ok()) {
      return fail_internal(disposition.status());
    }
  }

  status = finalize_available_slots();
  if (!status.ok()) {
    return status;
  }
  status = lifecycle_.begin_finalizing();
  if (!status.ok()) {
    return fail_internal(status);
  }
  status = lifecycle_.begin_sink_flush();
  if (!status.ok()) {
    return fail_internal(status);
  }
  status = lifecycle_.complete();
  if (!status.ok()) {
    return fail_internal(status);
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status SerialCartesianPipeline::cancel() {
  const auto callback_status = require_not_in_callback("cancel");
  if (!callback_status.ok()) {
    return callback_status;
  }
  if (lifecycle_.terminal()) {
    return ksj::base::Status::StateError("cannot cancel a terminal SerialCartesianPipeline");
  }

  const auto cancel_status = lifecycle_.request_cancel();
  if (!cancel_status.ok()) {
    return cancel_status;
  }
  if (lifecycle_.state() == ScanState::cancelled) {
    return ksj::base::Status::Ok();
  }
  quarantine_active_slots();
  return finish_abort_cleanup();
}

ksj::base::Status SerialCartesianPipeline::fail(ksj::base::Status cause) {
  const auto callback_status = require_not_in_callback("fail");
  if (!callback_status.ok()) {
    return callback_status;
  }
  if (cause.ok()) {
    return ksj::base::Status::InvalidArgument("SerialCartesianPipeline failure requires a non-OK cause");
  }
  if (lifecycle_.terminal()) {
    return ksj::base::Status::StateError("cannot fail a terminal SerialCartesianPipeline");
  }
  return fail_internal(std::move(cause));
}

SerialCartesianPipelineSnapshot SerialCartesianPipeline::snapshot() const {
  std::size_t active_frames = 0U;
  for (const auto& slot : slots_) {
    if (slot.active) {
      ++active_frames;
    }
  }
  return {
    .state = lifecycle_.state(),
    .terminal_cause = lifecycle_.terminal_cause(),
    .active_frames = active_frames,
    .terminal_frames = terminal_frame_records_.size(),
    .explicitly_ignored_records = explicitly_ignored_records_.size(),
    .arrivals_by_lane = arrivals_by_lane_,
    .callbacks_completed = callbacks_completed_,
    .certified_skips = certified_skips_,
    .last_error = last_error_,
  };
}

const std::vector<ExplicitlyIgnoredAcquisitionRecord>&
SerialCartesianPipeline::explicitly_ignored_records() const noexcept {
  return explicitly_ignored_records_;
}

const std::vector<SerialFrameTerminalRecord>& SerialCartesianPipeline::terminal_frame_records() const noexcept {
  return terminal_frame_records_;
}

ksj::base::Status SerialCartesianPipeline::validate_ingress(const NormalizedAcquisitionIngressFacts& facts,
                                                            const std::size_t payload_bytes) const {
  if (!target_envelope_.has_value()) {
    return ksj::base::Status::Ok();
  }
  const auto& envelope = *target_envelope_;
  if (!facts.complete) {
    return ksj::base::Status::ValidationError(
      "TargetEnvelope ingress validation requires complete normalized acquisition facts");
  }
  if (facts.samples_per_acquisition > envelope.max_samples_per_acquisition()) {
    return ksj::base::Status::ValidationError(
      "normalized acquisition samples_per_acquisition exceeds TargetEnvelope.max_samples_per_acquisition");
  }
  if (facts.active_channels > envelope.max_active_channels()) {
    return ksj::base::Status::ValidationError(
      "normalized acquisition active_channels exceeds TargetEnvelope.max_active_channels");
  }
  if (facts.trajectory_dimensions > envelope.max_trajectory_dimensions()) {
    return ksj::base::Status::ValidationError(
      "normalized acquisition trajectory_dimensions exceeds TargetEnvelope.max_trajectory_dimensions");
  }
  if (exceeds_quantity(payload_bytes, envelope.max_frame_charged_bytes())) {
    return ksj::base::Status::ValidationError(
      "normalized Cartesian acquisition payload exceeds TargetEnvelope.max_frame_charged_bytes");
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status SerialCartesianPipeline::require_not_in_callback(const std::string_view operation) const {
  if (callback_active_) {
    return ksj::base::Status::StateError(std::string(operation) +
                                         " cannot re-enter SerialCartesianPipeline from its sealed-frame callback");
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status SerialCartesianPipeline::require_running(const std::string_view operation) const {
  const auto callback_status = require_not_in_callback(operation);
  if (!callback_status.ok()) {
    return callback_status;
  }
  if (lifecycle_.state() != ScanState::running) {
    return ksj::base::Status::StateError(std::string(operation) + " is invalid after SerialCartesianPipeline state " +
                                         std::string(to_string(lifecycle_.state())));
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status SerialCartesianPipeline::fail_internal(ksj::base::Status cause) {
  if (cause.ok()) {
    cause = ksj::base::Status::InternalError("SerialCartesianPipeline internal failure was missing a cause");
  }
  if (lifecycle_.terminal()) {
    return ksj::base::Status::StateError("cannot transition a terminal SerialCartesianPipeline to failure");
  }
  last_error_ = std::move(cause);
  const auto fail_status = lifecycle_.fail();
  if (!fail_status.ok()) {
    return fail_status;
  }
  quarantine_active_slots();
  const auto cleanup_status = finish_abort_cleanup();
  if (!cleanup_status.ok()) {
    return cleanup_status;
  }
  return last_error_;
}

ksj::base::Status SerialCartesianPipeline::finish_abort_cleanup() {
  auto status = lifecycle_.begin_terminal_cleanup();
  if (!status.ok()) {
    return status;
  }
  status = lifecycle_.finish_terminal_cleanup();
  if (!status.ok()) {
    return status;
  }
  return ksj::base::Status::Ok();
}

void SerialCartesianPipeline::quarantine_active_slots() {
  for (auto& slot : slots_) {
    if (!slot.active) {
      continue;
    }
    if (is_quarantineable(slot.frame_slot.snapshot().state)) {
      static_cast<void>(slot.frame_slot.quarantine(slot.token));
    }
    slot.active = false;
  }
}

SerialCartesianPipeline::SlotRecord*
SerialCartesianPipeline::find_active_slot(const FrameSlotContext& context) noexcept {
  for (auto& slot : slots_) {
    if (slot.active && same_context(slot.context, context)) {
      return &slot;
    }
  }
  return nullptr;
}

ksj::base::Result<SerialCartesianPipeline::SlotRecord*>
SerialCartesianPipeline::acquire_slot(const FrameSlotContext& context) {
  if (context_is_terminal(context)) {
    return ksj::base::Status::StateError(
      "late acquisition targets a Cartesian frame that has already reached a terminal state");
  }
  if (order_key_is_in_use(context.order_key)) {
    return ksj::base::Status::ValidationError(
      "a distinct Cartesian frame reused an OrderKey that must be unique within a serial scan");
  }
  if (has_started_order_key_ && context.order_key < greatest_started_order_key_) {
    return ksj::base::Status::ValidationError(
      "a new Cartesian frame arrived below the greatest started OrderKey; serial output order is not provable");
  }

  std::size_t active_frames = 0U;
  for (const auto& slot : slots_) {
    if (slot.active) {
      ++active_frames;
    }
  }
  if (terminal_frame_records_.size() + active_frames >= max_terminal_frame_records_) {
    return ksj::base::Status::Unavailable(
      "SerialCartesianPipeline terminal-frame history bound is exhausted; admission bound was too small");
  }

  for (auto& slot : slots_) {
    if (slot.active) {
      continue;
    }
    const auto state = slot.frame_slot.snapshot().state;
    if (state != FrameSlotState::free && state != FrameSlotState::recycled) {
      continue;
    }
    auto token = slot.frame_slot.begin_frame(context);
    if (!token.ok()) {
      return token.status();
    }
    slot.token = token.value();
    slot.context = context;
    slot.active = true;
    slot.imaging_arrivals = 0U;
    slot.calibration_and_imaging_arrivals = 0U;
    greatest_started_order_key_ = context.order_key;
    has_started_order_key_ = true;
    return &slot;
  }
  return ksj::base::Status::Unavailable("all bounded Cartesian FrameSlots are active");
}

bool SerialCartesianPipeline::context_is_terminal(const FrameSlotContext& context) const noexcept {
  for (const auto& terminal : terminal_frame_records_) {
    if (same_context(terminal.context, context)) {
      return true;
    }
  }
  return false;
}

bool SerialCartesianPipeline::order_key_is_in_use(const std::uint64_t order_key) const noexcept {
  for (const auto& slot : slots_) {
    if (slot.active && slot.context.order_key == order_key) {
      return true;
    }
  }
  for (const auto& terminal : terminal_frame_records_) {
    if (terminal.context.order_key == order_key) {
      return true;
    }
  }
  return false;
}

SerialCartesianPipeline::SlotRecord* SerialCartesianPipeline::next_active_slot_in_order() noexcept {
  SlotRecord* result = nullptr;
  for (auto& slot : slots_) {
    if (!slot.active) {
      continue;
    }
    if (result == nullptr || context_less(slot.context, result->context)) {
      result = &slot;
    }
  }
  return result;
}

ksj::base::Status SerialCartesianPipeline::finalize_available_slots() {
  for (;;) {
    auto* const slot = next_active_slot_in_order();
    if (slot == nullptr) {
      return ksj::base::Status::Ok();
    }
    switch (slot->frame_slot.snapshot().state) {
      case FrameSlotState::filling:
        // A lower OrderKey is still assembling, so preserving deterministic
        // output order requires every later ready frame to wait.
        return ksj::base::Status::Ok();
      case FrameSlotState::ready:
        {
          const auto status = dispatch_ready_slot(*slot);
          if (!status.ok()) {
            return status;
          }
          break;
        }
      case FrameSlotState::skipped:
        {
          const auto status = finalize_skipped_slot(*slot);
          if (!status.ok()) {
            return status;
          }
          break;
        }
      case FrameSlotState::free:
      case FrameSlotState::computing:
      case FrameSlotState::emitting:
      case FrameSlotState::quarantined:
      case FrameSlotState::recycled:
        return fail_internal(
          ksj::base::Status::InternalError("active Cartesian FrameSlot reached an invalid serial-finalization state"));
    }
  }
}

ksj::base::Status SerialCartesianPipeline::dispatch_ready_slot(SlotRecord& slot) {
  if (terminal_frame_records_.size() >= max_terminal_frame_records_) {
    return fail_internal(ksj::base::Status::Unavailable(
      "SerialCartesianPipeline terminal-frame history bound is exhausted during callback dispatch"));
  }

  auto status = slot.frame_slot.begin_compute(slot.token);
  if (!status.ok()) {
    return fail_internal(status);
  }
  const auto bytes = slot.frame_slot.frame_bytes(slot.token);
  if (!bytes.ok()) {
    return fail_internal(bytes.status());
  }
  const auto slot_snapshot = slot.frame_slot.snapshot();
  const SealedCartesianFrame frame{
    .token = slot.token,
    .context = slot.context,
    .completion = slot_snapshot.completion,
    .bytes = bytes.value(),
    .imaging_arrivals = slot.imaging_arrivals,
    .calibration_and_imaging_arrivals = slot.calibration_and_imaging_arrivals,
  };

  ksj::base::Status callback_status = ksj::base::Status::Ok();
  callback_active_ = true;
  try {
    callback_status = on_sealed_frame_(frame);
  } catch (const std::exception& exception) {
    callback_status = ksj::base::Status::InternalError(
      std::string("SerialCartesianPipeline sealed-frame callback threw: ") + exception.what());
  } catch (...) {
    callback_status = ksj::base::Status::InternalError("SerialCartesianPipeline sealed-frame callback threw");
  }
  callback_active_ = false;
  if (!callback_status.ok()) {
    static_cast<void>(slot.frame_slot.quarantine(slot.token));
    return fail_internal(std::move(callback_status));
  }

  status = slot.frame_slot.begin_emit(slot.token);
  if (!status.ok()) {
    return fail_internal(status);
  }
  status = slot.frame_slot.recycle(slot.token);
  if (!status.ok()) {
    return fail_internal(status);
  }
  status = append_terminal_record(slot, true);
  if (!status.ok()) {
    return fail_internal(status);
  }
  ++callbacks_completed_;
  return ksj::base::Status::Ok();
}

ksj::base::Status SerialCartesianPipeline::finalize_skipped_slot(SlotRecord& slot) {
  if (terminal_frame_records_.size() >= max_terminal_frame_records_) {
    return fail_internal(ksj::base::Status::Unavailable(
      "SerialCartesianPipeline terminal-frame history bound is exhausted during certified skip"));
  }
  const auto recycle_status = slot.frame_slot.recycle(slot.token);
  if (!recycle_status.ok()) {
    return fail_internal(recycle_status);
  }
  const auto append_status = append_terminal_record(slot, false);
  if (!append_status.ok()) {
    return fail_internal(append_status);
  }
  ++certified_skips_;
  return ksj::base::Status::Ok();
}

ksj::base::Status SerialCartesianPipeline::append_terminal_record(SlotRecord& slot, const bool delivered_to_callback) {
  if (terminal_frame_records_.size() >= max_terminal_frame_records_) {
    return ksj::base::Status::Unavailable("SerialCartesianPipeline terminal-frame record bound is exhausted");
  }
  const auto slot_snapshot = slot.frame_slot.snapshot();
  terminal_frame_records_.push_back({
    .token = slot.token,
    .context = slot.context,
    .completion = slot_snapshot.completion,
    .delivered_to_callback = delivered_to_callback,
  });
  slot.active = false;
  slot.imaging_arrivals = 0U;
  slot.calibration_and_imaging_arrivals = 0U;
  return ksj::base::Status::Ok();
}

} // namespace ksj::recon::runtime
