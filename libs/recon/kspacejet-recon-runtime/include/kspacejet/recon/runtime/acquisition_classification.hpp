#pragma once

#include "kspacejet/base/result.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ksj::recon::runtime {

// The classifier consumes a normalized, public-MRD view.  Adapters may decode
// an ISMRMRD header into this type, but public runtime headers deliberately do
// not expose an ISMRMRD C++ type or a transport-specific message wrapper.
inline constexpr std::string_view kAcquisitionClassificationRuleVersion = "kspacejet.acquisition-classification/v1";

enum class PublicMrdMessageKind : std::uint8_t {
  acquisition,
  waveform,
  image,
  other,
};

enum class AcquisitionLane : std::uint8_t {
  noise,
  calibration,
  calibration_and_imaging,
  imaging,
  phase_correction,
  navigator,
  ignored_explicitly,
};

// The reason remains attached to an ignored acquisition so an adapter can
// emit an auditable record rather than silently dropping data in edge policy.
enum class AcquisitionClassificationReason : std::uint8_t {
  noise_measurement,
  parallel_calibration,
  parallel_calibration_and_imaging,
  imaging_default,
  phase_correction,
  navigation,
  dummy_scan_rule,
  explicit_rule,
};

[[nodiscard]] std::string_view to_string(PublicMrdMessageKind kind) noexcept;
[[nodiscard]] std::string_view to_string(AcquisitionLane lane) noexcept;
[[nodiscard]] std::string_view to_string(AcquisitionClassificationReason reason) noexcept;

// These are semantic flag facts after decoding, not a copy of any vendor or
// transport bit layout.  `explicitly_ignored` is only valid when an immutable,
// auditable classifier predicate selected the acquisition for exclusion.
struct NormalizedAcquisitionFlags {
  bool noise_measurement{false};
  bool parallel_calibration{false};
  bool parallel_calibration_and_imaging{false};
  bool phase_correction{false};
  bool navigation{false};
  bool dummy_scan{false};
  bool explicitly_ignored{false};
};

// Public ISMRMRD acquisition indices normalized to fixed-width values.  The
// FrameSlot consumes its Cartesian coordinates separately; retaining the full
// semantic index here keeps classification records self-contained.
struct NormalizedAcquisitionIndex {
  std::uint16_t encoding_space{0};
  std::uint16_t kspace_encode_step_1{0};
  std::uint16_t kspace_encode_step_2{0};
  std::uint16_t average{0};
  std::uint16_t slice{0};
  std::uint16_t contrast{0};
  std::uint16_t phase{0};
  std::uint16_t repetition{0};
  std::uint16_t set{0};
  std::uint16_t segment{0};

  [[nodiscard]] friend constexpr bool operator==(const NormalizedAcquisitionIndex&,
                                                 const NormalizedAcquisitionIndex&) noexcept = default;
};

struct AcquisitionClassificationInput {
  PublicMrdMessageKind message_kind{PublicMrdMessageKind::acquisition};
  NormalizedAcquisitionFlags flags{};
  NormalizedAcquisitionIndex index{};
};

struct AcquisitionClassification {
  AcquisitionLane lane{AcquisitionLane::imaging};
  AcquisitionClassificationReason reason{AcquisitionClassificationReason::imaging_default};
  NormalizedAcquisitionIndex index{};
};

struct AcquisitionClassifierConfig {
  std::string rule_version{std::string{kAcquisitionClassificationRuleVersion}};

  // This is an explicit frozen predicate, not an implicit source-edge drop.
  bool ignore_dummy_scans{true};
};

// A stateless classifier whose construction validates the rule revision.  The
// source router must route waveform/image/other messages before calling it;
// classify() rejects non-acquisition inputs to make that boundary enforceable.
class AcquisitionClassifier final {
public:
  [[nodiscard]] static ksj::base::Result<AcquisitionClassifier> create(AcquisitionClassifierConfig config);

  [[nodiscard]] ksj::base::Result<AcquisitionClassification>
  classify(const AcquisitionClassificationInput& input) const;

  [[nodiscard]] const AcquisitionClassifierConfig& config() const noexcept;

private:
  explicit AcquisitionClassifier(AcquisitionClassifierConfig config) noexcept;

  AcquisitionClassifierConfig config_;
};

[[nodiscard]] constexpr bool is_imaging_lane(const AcquisitionLane lane) noexcept {
  return lane == AcquisitionLane::imaging || lane == AcquisitionLane::calibration_and_imaging;
}

} // namespace ksj::recon::runtime
