#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace ksj::recon::runtime {
namespace {

constexpr std::size_t kBitmapWordBits = 64U;

[[nodiscard]] bool coordinate_less(const CartesianLineCoordinate& lhs, const CartesianLineCoordinate& rhs) noexcept {
  return lhs.phase_encode_2 < rhs.phase_encode_2 ||
         (lhs.phase_encode_2 == rhs.phase_encode_2 && lhs.phase_encode_1 < rhs.phase_encode_1);
}

[[nodiscard]] bool checked_multiply(const std::size_t lhs, const std::size_t rhs, std::size_t& result) noexcept {
  if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

[[nodiscard]] bool bitmap_is_set(const std::vector<std::uint64_t>& bitmap, const std::size_t index) noexcept {
  const auto word = index / kBitmapWordBits;
  const auto bit = index % kBitmapWordBits;
  return (bitmap[word] & (std::uint64_t{1} << bit)) != 0U;
}

void bitmap_set(std::vector<std::uint64_t>& bitmap, const std::size_t index) noexcept {
  const auto word = index / kBitmapWordBits;
  const auto bit = index % kBitmapWordBits;
  bitmap[word] |= std::uint64_t{1} << bit;
}

[[nodiscard]] bool frame_data_is_available(const FrameSlotState state) noexcept {
  return state == FrameSlotState::ready || state == FrameSlotState::computing || state == FrameSlotState::emitting;
}

} // namespace

std::string_view to_string(const DuplicateAcquisitionPolicy policy) noexcept {
  static constexpr std::array names{"reject", "ignore_identical", "replace_before_seal"};
  return names.at(static_cast<std::size_t>(policy));
}

std::string_view to_string(const IncompleteFramePolicy policy) noexcept {
  static constexpr std::array names{"fail", "emit_partial", "certified_skip"};
  return names.at(static_cast<std::size_t>(policy));
}

std::string_view to_string(const FrameSlotState state) noexcept {
  static constexpr std::array names{
    "free", "filling", "ready", "computing", "emitting", "quarantined", "skipped", "recycled",
  };
  return names.at(static_cast<std::size_t>(state));
}

std::string_view to_string(const FrameCompletion completion) noexcept {
  static constexpr std::array names{"not_sealed", "complete", "partial", "certified_skip"};
  return names.at(static_cast<std::size_t>(completion));
}

std::string_view to_string(const FrameSealDisposition disposition) noexcept {
  static constexpr std::array names{"complete", "partial", "certified_skip"};
  return names.at(static_cast<std::size_t>(disposition));
}

ksj::base::Result<CartesianFrameSlot> CartesianFrameSlot::create(CartesianFrameSlotConfig config) {
  const auto& dimensions = config.dimensions;
  if (dimensions.readout_samples == 0U || dimensions.phase_encode_1 == 0U || dimensions.phase_encode_2 == 0U ||
      dimensions.channels == 0U || dimensions.bytes_per_sample == 0U) {
    return ksj::base::Status::InvalidArgument("Cartesian FrameSlot dimensions must all be finite and non-zero");
  }
  if (config.completion.required_indices.empty()) {
    return ksj::base::Status::InvalidArgument("Cartesian FrameSlot requires an explicit exact coverage set");
  }
  if (config.resource_upper_bound.max_total_arrivals == 0U || config.resource_upper_bound.max_payload_bytes == 0U) {
    return ksj::base::Status::InvalidArgument("Cartesian FrameSlot resource upper bounds must be finite and non-zero");
  }

  std::size_t physical_line_count = 0;
  if (!checked_multiply(static_cast<std::size_t>(dimensions.phase_encode_1),
                        static_cast<std::size_t>(dimensions.phase_encode_2), physical_line_count)) {
    return ksj::base::Status::ValidationError("Cartesian FrameSlot phase-encode dimensions overflow host size");
  }
  std::size_t line_bytes = 0;
  if (!checked_multiply(static_cast<std::size_t>(dimensions.readout_samples),
                        static_cast<std::size_t>(dimensions.channels), line_bytes) ||
      !checked_multiply(line_bytes, static_cast<std::size_t>(dimensions.bytes_per_sample), line_bytes)) {
    return ksj::base::Status::ValidationError("Cartesian FrameSlot line payload size overflows host size");
  }
  std::size_t storage_bytes = 0;
  if (!checked_multiply(physical_line_count, line_bytes, storage_bytes)) {
    return ksj::base::Status::ValidationError("Cartesian FrameSlot backing storage size overflows host size");
  }
  if (line_bytes > std::numeric_limits<std::uint64_t>::max() ||
      config.resource_upper_bound.max_payload_bytes < static_cast<std::uint64_t>(line_bytes)) {
    return ksj::base::Status::ValidationError(
      "Cartesian FrameSlot max_payload_bytes is smaller than one configured acquisition payload");
  }

  auto& required = config.completion.required_indices;
  std::sort(required.begin(), required.end(), coordinate_less);
  for (const auto coordinate : required) {
    if (coordinate.phase_encode_1 >= dimensions.phase_encode_1 ||
        coordinate.phase_encode_2 >= dimensions.phase_encode_2) {
      return ksj::base::Status::ValidationError(
        "Cartesian FrameSlot coverage coordinate is outside configured dimensions");
    }
  }
  if (std::adjacent_find(required.begin(), required.end()) != required.end()) {
    return ksj::base::Status::ValidationError("Cartesian FrameSlot coverage set contains a duplicate coordinate");
  }
  if (required.size() > config.resource_upper_bound.max_total_arrivals) {
    return ksj::base::Status::ValidationError(
      "Cartesian FrameSlot max_total_arrivals is smaller than exact coverage cardinality");
  }
  const auto maximum_possible_duplicates = config.resource_upper_bound.max_total_arrivals - required.size();
  if (config.resource_upper_bound.max_duplicate_arrivals > maximum_possible_duplicates) {
    return ksj::base::Status::ValidationError(
      "Cartesian FrameSlot max_duplicate_arrivals exceeds remaining total-arrival budget");
  }
  if (config.duplicate_policy == DuplicateAcquisitionPolicy::reject &&
      config.resource_upper_bound.max_duplicate_arrivals != 0U) {
    return ksj::base::Status::ValidationError("reject duplicate policy requires max_duplicate_arrivals to be zero");
  }

  if (physical_line_count > std::numeric_limits<std::size_t>::max() - (kBitmapWordBits - 1U)) {
    return ksj::base::Status::ValidationError("Cartesian FrameSlot bitmap size overflows host size");
  }
  const auto bitmap_words = (physical_line_count + (kBitmapWordBits - 1U)) / kBitmapWordBits;

  try {
    std::vector<ksj::base::byte> storage(storage_bytes);
    std::vector<std::uint64_t> expected_bitmap(bitmap_words, 0U);
    std::vector<std::uint64_t> coverage_bitmap(bitmap_words, 0U);
    for (const auto coordinate : required) {
      const auto physical_index =
        static_cast<std::size_t>(coordinate.phase_encode_2) * dimensions.phase_encode_1 + coordinate.phase_encode_1;
      bitmap_set(expected_bitmap, physical_index);
    }
    return CartesianFrameSlot{std::move(config),          line_bytes,
                              physical_line_count,        std::move(storage),
                              std::move(expected_bitmap), std::move(coverage_bitmap)};
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate bounded Cartesian FrameSlot storage");
  } catch (const std::length_error&) {
    return ksj::base::Status::OutOfMemory("configured Cartesian FrameSlot exceeds vector capacity");
  }
}

CartesianFrameSlot::CartesianFrameSlot(CartesianFrameSlotConfig config, const std::size_t line_bytes,
                                       const std::size_t physical_line_count, std::vector<ksj::base::byte> storage,
                                       std::vector<std::uint64_t> expected_bitmap,
                                       std::vector<std::uint64_t> coverage_bitmap) noexcept
    : config_(std::move(config)), line_bytes_(line_bytes), physical_line_count_(physical_line_count),
      storage_(std::move(storage)), expected_bitmap_(std::move(expected_bitmap)),
      coverage_bitmap_(std::move(coverage_bitmap)) {}

ksj::base::Result<FrameSlotToken> CartesianFrameSlot::begin_frame(const FrameSlotContext context) {
  if (state_ != FrameSlotState::free && state_ != FrameSlotState::recycled) {
    return ksj::base::Status::StateError("Cartesian FrameSlot can begin only from free or recycled state");
  }
  if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
    return ksj::base::Status::Unavailable("Cartesian FrameSlot generation is exhausted");
  }
  ++generation_;
  context_ = context;
  clear_for_next_frame();
  state_ = FrameSlotState::filling;
  return FrameSlotToken{.slot_id = config_.slot_id, .generation = generation_};
}

ksj::base::Status CartesianFrameSlot::scatter(const FrameSlotToken token, const CartesianLineCoordinate coordinate,
                                              const ksj::base::ConstByteSpan payload) {
  const auto token_status = validate_active_token(token, "scatter");
  if (!token_status.ok()) {
    return token_status;
  }
  if (state_ != FrameSlotState::filling) {
    return ksj::base::Status::StateError("late acquisition: Cartesian FrameSlot is already sealed or terminal");
  }
  const auto coordinate_status = validate_coordinate(coordinate);
  if (!coordinate_status.ok()) {
    return coordinate_status;
  }
  if (payload.size() != line_bytes_) {
    return ksj::base::Status::ValidationError("Cartesian acquisition payload does not match configured line bytes");
  }
  if (payload.size() > config_.resource_upper_bound.max_payload_bytes) {
    return ksj::base::Status::ValidationError("Cartesian acquisition payload exceeds max_payload_bytes");
  }
  if (total_arrivals_ == config_.resource_upper_bound.max_total_arrivals) {
    return ksj::base::Status::Unavailable("Cartesian FrameSlot max_total_arrivals is exhausted");
  }

  const auto physical_index = physical_line_index(coordinate);
  if (!is_expected(physical_index)) {
    return ksj::base::Status::ValidationError("Cartesian acquisition coordinate is not part of the exact coverage set");
  }
  ++total_arrivals_;

  auto* const destination = storage_.data() + physical_index * line_bytes_;
  if (is_covered(physical_index)) {
    if (config_.duplicate_policy == DuplicateAcquisitionPolicy::reject) {
      return ksj::base::Status::AlreadyExists("duplicate Cartesian acquisition is forbidden by contract");
    }
    if (duplicate_arrivals_ == config_.resource_upper_bound.max_duplicate_arrivals) {
      return ksj::base::Status::Unavailable("Cartesian FrameSlot max_duplicate_arrivals is exhausted");
    }
    ++duplicate_arrivals_;
    switch (config_.duplicate_policy) {
      case DuplicateAcquisitionPolicy::reject:
        return ksj::base::Status::InternalError("reject duplicate policy was not handled before dispatch");
      case DuplicateAcquisitionPolicy::ignore_identical:
        if (std::memcmp(destination, payload.data(), line_bytes_) != 0) {
          return ksj::base::Status::ValidationError(
            "duplicate Cartesian acquisition differs under ignore-identical policy");
        }
        return ksj::base::Status::Ok();
      case DuplicateAcquisitionPolicy::replace_before_seal:
        std::memcpy(destination, payload.data(), line_bytes_);
        return ksj::base::Status::Ok();
    }
  }

  std::memcpy(destination, payload.data(), line_bytes_);
  mark_covered(physical_index);
  ++covered_indices_;
  if (covered_indices_ == config_.completion.required_indices.size()) {
    seal_complete();
  }
  return ksj::base::Status::Ok();
}

ksj::base::Result<FrameSealDisposition> CartesianFrameSlot::end_of_input(const FrameSlotToken token) {
  const auto token_status = validate_active_token(token, "end_of_input");
  if (!token_status.ok()) {
    return token_status;
  }
  if (state_ == FrameSlotState::ready && completion_ == FrameCompletion::complete) {
    return FrameSealDisposition::complete;
  }
  if (state_ != FrameSlotState::filling) {
    return ksj::base::Status::StateError("EndOfInput is valid only for a filling Cartesian FrameSlot");
  }
  if (covered_indices_ == config_.completion.required_indices.size()) {
    seal_complete();
    return FrameSealDisposition::complete;
  }

  switch (config_.incomplete_policy) {
    case IncompleteFramePolicy::fail:
      state_ = FrameSlotState::quarantined;
      return ksj::base::Status::ValidationError("Cartesian FrameSlot has missing required coordinates at EndOfInput");
    case IncompleteFramePolicy::emit_partial:
      completion_ = FrameCompletion::partial;
      state_ = FrameSlotState::ready;
      return FrameSealDisposition::partial;
    case IncompleteFramePolicy::certified_skip:
      completion_ = FrameCompletion::certified_skip;
      state_ = FrameSlotState::skipped;
      return FrameSealDisposition::certified_skip;
  }
  return ksj::base::Status::InternalError("unknown incomplete Cartesian FrameSlot policy");
}

ksj::base::Status CartesianFrameSlot::begin_compute(const FrameSlotToken token) {
  const auto token_status = validate_active_token(token, "begin_compute");
  if (!token_status.ok()) {
    return token_status;
  }
  if (state_ != FrameSlotState::ready) {
    return ksj::base::Status::StateError("Cartesian FrameSlot compute may begin only from ready state");
  }
  state_ = FrameSlotState::computing;
  return ksj::base::Status::Ok();
}

ksj::base::Status CartesianFrameSlot::begin_emit(const FrameSlotToken token) {
  const auto token_status = validate_active_token(token, "begin_emit");
  if (!token_status.ok()) {
    return token_status;
  }
  if (state_ != FrameSlotState::computing) {
    return ksj::base::Status::StateError("Cartesian FrameSlot emit may begin only from computing state");
  }
  state_ = FrameSlotState::emitting;
  return ksj::base::Status::Ok();
}

ksj::base::Status CartesianFrameSlot::quarantine(const FrameSlotToken token) {
  const auto token_status = validate_active_token(token, "quarantine");
  if (!token_status.ok()) {
    return token_status;
  }
  if (state_ == FrameSlotState::free || state_ == FrameSlotState::recycled || state_ == FrameSlotState::skipped ||
      state_ == FrameSlotState::quarantined) {
    return ksj::base::Status::StateError("Cartesian FrameSlot cannot be quarantined from its current state");
  }
  state_ = FrameSlotState::quarantined;
  return ksj::base::Status::Ok();
}

ksj::base::Status CartesianFrameSlot::recycle(const FrameSlotToken token) {
  const auto token_status = validate_active_token(token, "recycle");
  if (!token_status.ok()) {
    return token_status;
  }
  if (state_ != FrameSlotState::emitting && state_ != FrameSlotState::quarantined &&
      state_ != FrameSlotState::skipped) {
    return ksj::base::Status::StateError("Cartesian FrameSlot may recycle only after emission, skip, or quarantine");
  }
  state_ = FrameSlotState::recycled;
  return ksj::base::Status::Ok();
}

ksj::base::Result<ksj::base::ConstByteSpan> CartesianFrameSlot::frame_bytes(const FrameSlotToken token) const {
  const auto token_status = validate_active_token(token, "frame_bytes");
  if (!token_status.ok()) {
    return token_status;
  }
  if (!frame_data_is_available(state_)) {
    return ksj::base::Status::StateError(
      "Cartesian FrameSlot payload is available only while ready, computing, or emitting");
  }
  return ksj::base::ConstByteSpan{storage_.data(), storage_.size()};
}

ksj::base::Result<std::vector<CartesianLineCoordinate>>
CartesianFrameSlot::missing_indices(const FrameSlotToken token) const {
  const auto token_status = validate_active_token(token, "missing_indices");
  if (!token_status.ok()) {
    return token_status;
  }
  if (state_ == FrameSlotState::free || state_ == FrameSlotState::recycled) {
    return ksj::base::Status::StateError("Cartesian FrameSlot has no active frame");
  }
  std::vector<CartesianLineCoordinate> missing;
  missing.reserve(config_.completion.required_indices.size() - static_cast<std::size_t>(covered_indices_));
  for (const auto coordinate : config_.completion.required_indices) {
    if (!is_covered(physical_line_index(coordinate))) {
      missing.push_back(coordinate);
    }
  }
  return missing;
}

CartesianFrameSlotSnapshot CartesianFrameSlot::snapshot() const noexcept {
  return {
    .token = {.slot_id = config_.slot_id, .generation = generation_},
    .state = state_,
    .completion = completion_,
    .context = context_,
    .required_indices = config_.completion.required_indices.size(),
    .covered_indices = covered_indices_,
    .total_arrivals = total_arrivals_,
    .duplicate_arrivals = duplicate_arrivals_,
    .line_bytes = line_bytes_,
    .storage_bytes = storage_.size(),
  };
}

const CartesianFrameSlotConfig& CartesianFrameSlot::config() const noexcept {
  return config_;
}

ksj::base::Status CartesianFrameSlot::validate_active_token(const FrameSlotToken token,
                                                            const std::string_view operation) const {
  if (token.slot_id != config_.slot_id || token.generation != generation_ || generation_ == 0U) {
    return ksj::base::Status::StateError(std::string(operation) + " received a stale Cartesian FrameSlot token");
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status CartesianFrameSlot::validate_coordinate(const CartesianLineCoordinate coordinate) const {
  if (coordinate.phase_encode_1 >= config_.dimensions.phase_encode_1 ||
      coordinate.phase_encode_2 >= config_.dimensions.phase_encode_2) {
    return ksj::base::Status::ValidationError("Cartesian acquisition coordinate is outside configured dimensions");
  }
  return ksj::base::Status::Ok();
}

std::size_t CartesianFrameSlot::physical_line_index(const CartesianLineCoordinate coordinate) const noexcept {
  return static_cast<std::size_t>(coordinate.phase_encode_2) * config_.dimensions.phase_encode_1 +
         coordinate.phase_encode_1;
}

bool CartesianFrameSlot::is_expected(const std::size_t physical_index) const noexcept {
  return bitmap_is_set(expected_bitmap_, physical_index);
}

bool CartesianFrameSlot::is_covered(const std::size_t physical_index) const noexcept {
  return bitmap_is_set(coverage_bitmap_, physical_index);
}

void CartesianFrameSlot::mark_covered(const std::size_t physical_index) noexcept {
  bitmap_set(coverage_bitmap_, physical_index);
}

void CartesianFrameSlot::clear_for_next_frame() noexcept {
  std::fill(storage_.begin(), storage_.end(), ksj::base::byte{0});
  std::fill(coverage_bitmap_.begin(), coverage_bitmap_.end(), 0U);
  completion_ = FrameCompletion::not_sealed;
  covered_indices_ = 0U;
  total_arrivals_ = 0U;
  duplicate_arrivals_ = 0U;
}

void CartesianFrameSlot::seal_complete() noexcept {
  completion_ = FrameCompletion::complete;
  state_ = FrameSlotState::ready;
}

} // namespace ksj::recon::runtime
