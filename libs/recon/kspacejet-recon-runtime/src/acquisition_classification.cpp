#include "kspacejet/recon/runtime/acquisition_classification.hpp"

#include <array>
#include <utility>

namespace ksj::recon::runtime {

std::string_view to_string(const PublicMrdMessageKind kind) noexcept {
  static constexpr std::array names{"acquisition", "waveform", "image", "other"};
  return names.at(static_cast<std::size_t>(kind));
}

std::string_view to_string(const AcquisitionLane lane) noexcept {
  static constexpr std::array names{
    "noise", "calibration", "calibration_and_imaging", "imaging", "phase_correction", "navigator", "ignored_explicitly",
  };
  return names.at(static_cast<std::size_t>(lane));
}

std::string_view to_string(const AcquisitionClassificationReason reason) noexcept {
  static constexpr std::array names{
    "noise_measurement", "parallel_calibration", "parallel_calibration_and_imaging",
    "imaging_default",   "phase_correction",     "navigation",
    "dummy_scan_rule",   "explicit_rule",
  };
  return names.at(static_cast<std::size_t>(reason));
}

AcquisitionClassifier::AcquisitionClassifier(AcquisitionClassifierConfig config) noexcept
    : config_(std::move(config)) {}

ksj::base::Result<AcquisitionClassifier> AcquisitionClassifier::create(AcquisitionClassifierConfig config) {
  if (config.rule_version != kAcquisitionClassificationRuleVersion) {
    return ksj::base::Status::ValidationError("unsupported acquisition classification rule version: " +
                                              config.rule_version);
  }
  return AcquisitionClassifier{std::move(config)};
}

ksj::base::Result<AcquisitionClassification>
AcquisitionClassifier::classify(const AcquisitionClassificationInput& input) const {
  if (input.message_kind != PublicMrdMessageKind::acquisition) {
    return ksj::base::Status::InvalidArgument(
      "acquisition classifier accepts only acquisition messages; route public MRD message kinds first");
  }

  const auto& flags = input.flags;
  if (flags.explicitly_ignored) {
    return AcquisitionClassification{
      .lane = AcquisitionLane::ignored_explicitly,
      .reason = AcquisitionClassificationReason::explicit_rule,
      .index = input.index,
    };
  }
  if (flags.dummy_scan && config_.ignore_dummy_scans) {
    return AcquisitionClassification{
      .lane = AcquisitionLane::ignored_explicitly,
      .reason = AcquisitionClassificationReason::dummy_scan_rule,
      .index = input.index,
    };
  }

  // `parallel_calibration_and_imaging` intentionally dominates the more
  // general calibration flag: a decoder may surface both facts, but it still
  // represents one combined lane.  All other simultaneous lane flags are
  // ambiguous and must be rejected before they reach a FrameSlot.
  const bool calibration = flags.parallel_calibration || flags.parallel_calibration_and_imaging;
  const std::uint32_t semantic_lane_count =
    static_cast<std::uint32_t>(flags.noise_measurement) + static_cast<std::uint32_t>(calibration) +
    static_cast<std::uint32_t>(flags.phase_correction) + static_cast<std::uint32_t>(flags.navigation);
  if (semantic_lane_count > 1U) {
    return ksj::base::Status::ValidationError(
      "acquisition flags select more than one mutually exclusive semantic lane");
  }

  if (flags.noise_measurement) {
    return AcquisitionClassification{
      .lane = AcquisitionLane::noise,
      .reason = AcquisitionClassificationReason::noise_measurement,
      .index = input.index,
    };
  }
  if (flags.parallel_calibration_and_imaging) {
    return AcquisitionClassification{
      .lane = AcquisitionLane::calibration_and_imaging,
      .reason = AcquisitionClassificationReason::parallel_calibration_and_imaging,
      .index = input.index,
    };
  }
  if (flags.parallel_calibration) {
    return AcquisitionClassification{
      .lane = AcquisitionLane::calibration,
      .reason = AcquisitionClassificationReason::parallel_calibration,
      .index = input.index,
    };
  }
  if (flags.phase_correction) {
    return AcquisitionClassification{
      .lane = AcquisitionLane::phase_correction,
      .reason = AcquisitionClassificationReason::phase_correction,
      .index = input.index,
    };
  }
  if (flags.navigation) {
    return AcquisitionClassification{
      .lane = AcquisitionLane::navigator,
      .reason = AcquisitionClassificationReason::navigation,
      .index = input.index,
    };
  }
  return AcquisitionClassification{
    .lane = AcquisitionLane::imaging,
    .reason = AcquisitionClassificationReason::imaging_default,
    .index = input.index,
  };
}

const AcquisitionClassifierConfig& AcquisitionClassifier::config() const noexcept {
  return config_;
}

} // namespace ksj::recon::runtime
