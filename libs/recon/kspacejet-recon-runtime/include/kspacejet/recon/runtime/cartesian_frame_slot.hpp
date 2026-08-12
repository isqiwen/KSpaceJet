#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ksj::recon::runtime {

// A Cartesian FrameSlot is deliberately a serial M1 primitive.  A future
// KeyShard owns its serialization; this class itself neither schedules work
// nor exposes a transport or Provider ABI.
enum class DuplicateAcquisitionPolicy : std::uint8_t {
  reject,
  ignore_identical,
  replace_before_seal,
};

enum class IncompleteFramePolicy : std::uint8_t {
  fail,
  emit_partial,
  certified_skip,
};

enum class FrameSlotState : std::uint8_t {
  free,
  filling,
  ready,
  computing,
  emitting,
  quarantined,
  skipped,
  recycled,
};

enum class FrameCompletion : std::uint8_t {
  not_sealed,
  complete,
  partial,
  certified_skip,
};

enum class FrameSealDisposition : std::uint8_t {
  complete,
  partial,
  certified_skip,
};

[[nodiscard]] std::string_view to_string(DuplicateAcquisitionPolicy policy) noexcept;
[[nodiscard]] std::string_view to_string(IncompleteFramePolicy policy) noexcept;
[[nodiscard]] std::string_view to_string(FrameSlotState state) noexcept;
[[nodiscard]] std::string_view to_string(FrameCompletion completion) noexcept;
[[nodiscard]] std::string_view to_string(FrameSealDisposition disposition) noexcept;

// One imaging acquisition maps to one phase-encode line/plane.  Readout and
// channel dimensions belong to the payload layout, not this placement key.
struct CartesianLineCoordinate {
  std::uint32_t phase_encode_1{0};
  std::uint32_t phase_encode_2{0};

  [[nodiscard]] friend constexpr bool operator==(const CartesianLineCoordinate&,
                                                 const CartesianLineCoordinate&) noexcept = default;
};

struct CartesianFrameDimensions {
  std::uint32_t readout_samples{0};
  std::uint32_t phase_encode_1{0};
  std::uint32_t phase_encode_2{0};
  std::uint32_t channels{0};
  std::uint32_t bytes_per_sample{0};
};

// Completion is an exact, explicit set of required coordinates.  It is not
// inferred from arrival count or from a dimension/envelope maximum, so it can
// represent undersampling, partial Fourier and ACS patterns faithfully.
struct CartesianCoverageSpec {
  std::vector<CartesianLineCoordinate> required_indices;
};

// Resource bounds remain independent from the completion predicate.  Payload
// bytes are per arrival; total/duplicate arrival limits bound malformed or
// repeated ingress even if the exact coverage set is already finite.
struct CartesianResourceUpperBound {
  std::uint64_t max_total_arrivals{0};
  std::uint64_t max_duplicate_arrivals{0};
  std::uint64_t max_payload_bytes{0};
};

struct FrameSemanticKey {
  std::uint16_t encoding_space{0};
  std::uint16_t slice{0};
  std::uint16_t contrast{0};
  std::uint16_t repetition{0};
  std::uint16_t set{0};
  std::uint16_t phase{0};
  std::uint16_t average{0};

  [[nodiscard]] friend constexpr bool operator==(const FrameSemanticKey&, const FrameSemanticKey&) noexcept = default;
};

struct FrameSlotContext {
  FrameSemanticKey semantic_key{};
  std::uint64_t order_key{0};
  std::uint64_t placement_key{0};
};

struct FrameSlotToken {
  std::uint32_t slot_id{0};
  std::uint64_t generation{0};

  [[nodiscard]] friend constexpr bool operator==(const FrameSlotToken&, const FrameSlotToken&) noexcept = default;
};

struct CartesianFrameSlotConfig {
  std::uint32_t slot_id{0};
  CartesianFrameDimensions dimensions{};
  CartesianCoverageSpec completion{};
  CartesianResourceUpperBound resource_upper_bound{};
  DuplicateAcquisitionPolicy duplicate_policy{DuplicateAcquisitionPolicy::reject};
  IncompleteFramePolicy incomplete_policy{IncompleteFramePolicy::fail};
};

struct CartesianFrameSlotSnapshot {
  FrameSlotToken token{};
  FrameSlotState state{FrameSlotState::free};
  FrameCompletion completion{FrameCompletion::not_sealed};
  FrameSlotContext context{};
  std::uint64_t required_indices{0};
  std::uint64_t covered_indices{0};
  std::uint64_t total_arrivals{0};
  std::uint64_t duplicate_arrivals{0};
  std::size_t line_bytes{0};
  std::size_t storage_bytes{0};
};

// The backing vector and both bitmaps are allocated exactly once by create().
// scatter() performs a checked, direct copy into the final physical line
// location and never accumulates heap-owned acquisition objects.
class CartesianFrameSlot final {
public:
  [[nodiscard]] static ksj::base::Result<CartesianFrameSlot> create(CartesianFrameSlotConfig config);

  CartesianFrameSlot(const CartesianFrameSlot&) = delete;
  CartesianFrameSlot& operator=(const CartesianFrameSlot&) = delete;
  CartesianFrameSlot(CartesianFrameSlot&&) noexcept = default;
  CartesianFrameSlot& operator=(CartesianFrameSlot&&) noexcept = default;
  ~CartesianFrameSlot() = default;

  // Begins a new active frame in a reusable slot and advances generation.  A
  // token from an older frame is rejected by every mutation/access operation.
  [[nodiscard]] ksj::base::Result<FrameSlotToken> begin_frame(FrameSlotContext context);

  [[nodiscard]] ksj::base::Status scatter(FrameSlotToken token, CartesianLineCoordinate coordinate,
                                          ksj::base::ConstByteSpan payload);

  // Closes this frame's ingress.  A complete slot is sealed eagerly after its
  // final required coordinate; EndOfInput handles only the explicit missing
  // data policy.  `fail` moves the slot to quarantined and returns an error.
  [[nodiscard]] ksj::base::Result<FrameSealDisposition> end_of_input(FrameSlotToken token);

  [[nodiscard]] ksj::base::Status begin_compute(FrameSlotToken token);
  [[nodiscard]] ksj::base::Status begin_emit(FrameSlotToken token);
  [[nodiscard]] ksj::base::Status quarantine(FrameSlotToken token);
  [[nodiscard]] ksj::base::Status recycle(FrameSlotToken token);

  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> frame_bytes(FrameSlotToken token) const;
  [[nodiscard]] ksj::base::Result<std::vector<CartesianLineCoordinate>> missing_indices(FrameSlotToken token) const;

  [[nodiscard]] CartesianFrameSlotSnapshot snapshot() const noexcept;
  [[nodiscard]] const CartesianFrameSlotConfig& config() const noexcept;

private:
  CartesianFrameSlot(CartesianFrameSlotConfig config, std::size_t line_bytes, std::size_t physical_line_count,
                     std::vector<ksj::base::byte> storage, std::vector<std::uint64_t> expected_bitmap,
                     std::vector<std::uint64_t> coverage_bitmap) noexcept;

  [[nodiscard]] ksj::base::Status validate_active_token(FrameSlotToken token, std::string_view operation) const;
  [[nodiscard]] ksj::base::Status validate_coordinate(CartesianLineCoordinate coordinate) const;
  [[nodiscard]] std::size_t physical_line_index(CartesianLineCoordinate coordinate) const noexcept;
  [[nodiscard]] bool is_expected(std::size_t physical_index) const noexcept;
  [[nodiscard]] bool is_covered(std::size_t physical_index) const noexcept;
  void mark_covered(std::size_t physical_index) noexcept;
  void clear_for_next_frame() noexcept;
  void seal_complete() noexcept;

  CartesianFrameSlotConfig config_;
  std::size_t line_bytes_{0};
  std::size_t physical_line_count_{0};
  std::vector<ksj::base::byte> storage_;
  std::vector<std::uint64_t> expected_bitmap_;
  std::vector<std::uint64_t> coverage_bitmap_;
  FrameSlotState state_{FrameSlotState::free};
  FrameCompletion completion_{FrameCompletion::not_sealed};
  FrameSlotContext context_{};
  std::uint64_t generation_{0};
  std::uint64_t covered_indices_{0};
  std::uint64_t total_arrivals_{0};
  std::uint64_t duplicate_arrivals_{0};
};

} // namespace ksj::recon::runtime
