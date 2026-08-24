#include "kspacejet/recon/runtime/ismrmrd_semantic_ingress.hpp"

#include "kspacejet/ismrmrd/dataset_reader.hpp"

#include <ismrmrd/ismrmrd.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ksj::recon::runtime {
namespace {

using AcquisitionFlag = ISMRMRD::ISMRMRD_AcquisitionFlags;

[[nodiscard]] constexpr std::uint64_t flag_bit(const AcquisitionFlag flag) noexcept {
  return UINT64_C(1) << (static_cast<std::uint64_t>(flag) - 1U);
}

[[nodiscard]] bool is_set(const std::uint64_t flags, const AcquisitionFlag flag) noexcept {
  return (flags & flag_bit(flag)) != 0U;
}

constexpr std::uint64_t kKnownIsmrmrdAcquisitionFlags =
  flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_ENCODE_STEP1) | flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_ENCODE_STEP1) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_ENCODE_STEP2) | flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_ENCODE_STEP2) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_AVERAGE) | flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_AVERAGE) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_SLICE) | flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_SLICE) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_CONTRAST) | flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_CONTRAST) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_PHASE) | flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_PHASE) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_REPETITION) | flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_REPETITION) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_SET) | flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_SET) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_SEGMENT) | flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_SEGMENT) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT) | flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION_AND_IMAGING) | flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_REVERSE) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_NAVIGATION_DATA) | flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_PHASECORR_DATA) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_MEASUREMENT) | flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_HPFEEDBACK_DATA) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_DUMMYSCAN_DATA) | flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_RTFEEDBACK_DATA) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_SURFACECOILCORRECTIONSCAN_DATA) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_PHASE_STABILIZATION_REFERENCE) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_PHASE_STABILIZATION) | flag_bit(ISMRMRD::ISMRMRD_ACQ_COMPRESSION1) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_COMPRESSION2) | flag_bit(ISMRMRD::ISMRMRD_ACQ_COMPRESSION3) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_COMPRESSION4) | flag_bit(ISMRMRD::ISMRMRD_ACQ_USER1) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_USER2) | flag_bit(ISMRMRD::ISMRMRD_ACQ_USER3) | flag_bit(ISMRMRD::ISMRMRD_ACQ_USER4) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_USER5) | flag_bit(ISMRMRD::ISMRMRD_ACQ_USER6) | flag_bit(ISMRMRD::ISMRMRD_ACQ_USER7) |
  flag_bit(ISMRMRD::ISMRMRD_ACQ_USER8);

[[nodiscard]] bool product_matches_size(const std::uint64_t lhs, const std::uint64_t rhs,
                                        const std::size_t observed_size) noexcept {
  if (lhs != 0U && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
    return false;
  }
  const auto expected = lhs * rhs;
  if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
    if (expected > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      return false;
    }
  }
  return static_cast<std::size_t>(expected) == observed_size;
}

template <std::size_t N> [[nodiscard]] bool all_finite(const std::array<float, N>& values) noexcept {
  for (const auto value : values) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool header_floats_are_finite(const ksj::ismrmrd::AcquisitionHeader& header) noexcept {
  return std::isfinite(header.sample_time_us) && all_finite(header.position) && all_finite(header.read_dir) &&
         all_finite(header.phase_dir) && all_finite(header.slice_dir) && all_finite(header.patient_table_position) &&
         all_finite(header.user_float);
}

[[nodiscard]] bool payload_values_are_finite(const ksj::ismrmrd::AcquisitionView& acquisition) noexcept {
  for (const auto value : acquisition.samples) {
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
      return false;
    }
  }
  for (const auto value : acquisition.trajectory) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] ksj::base::Status validate_layout(const ksj::ismrmrd::AcquisitionView& acquisition) {
  const auto& header = acquisition.header;
  if (!product_matches_size(header.number_of_samples, header.active_channels, acquisition.samples.size())) {
    return ksj::base::Status::ValidationError(
      "ISMRMRD acquisition samples do not match header number_of_samples * active_channels");
  }
  if (!product_matches_size(header.number_of_samples, header.trajectory_dimensions, acquisition.trajectory.size())) {
    return ksj::base::Status::ValidationError(
      "ISMRMRD acquisition trajectory does not match header number_of_samples * trajectory_dimensions");
  }
  if (header.number_of_samples != 0U && header.active_channels == 0U) {
    return ksj::base::Status::ValidationError("ISMRMRD acquisition has samples but no active channels");
  }
  if (header.available_channels < header.active_channels) {
    return ksj::base::Status::ValidationError("ISMRMRD acquisition available_channels is smaller than active_channels");
  }
  if (header.discard_pre > header.number_of_samples ||
      header.discard_post > header.number_of_samples - header.discard_pre) {
    return ksj::base::Status::ValidationError("ISMRMRD acquisition discard range exceeds number_of_samples");
  }
  if (header.number_of_samples == 0U) {
    if (header.center_sample != 0U || header.discard_pre != 0U || header.discard_post != 0U) {
      return ksj::base::Status::ValidationError("zero-sample ISMRMRD acquisition has a nonzero sample-range field");
    }
  } else if (header.center_sample >= header.number_of_samples) {
    return ksj::base::Status::ValidationError("ISMRMRD acquisition center_sample is outside number_of_samples");
  }
  if (!header_floats_are_finite(header) || !payload_values_are_finite(acquisition)) {
    return ksj::base::Status::ValidationError("ISMRMRD acquisition contains a non-finite header or payload value");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status reject_unhandled_semantic_flags(const std::uint64_t flags) {
  if ((flags & ~kKnownIsmrmrdAcquisitionFlags) != 0U) {
    return ksj::base::Status::ValidationError("ISMRMRD acquisition contains unknown semantic flag bits");
  }
  const auto unsupported =
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_IS_HPFEEDBACK_DATA) || is_set(flags, ISMRMRD::ISMRMRD_ACQ_IS_RTFEEDBACK_DATA) ||
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_IS_SURFACECOILCORRECTIONSCAN_DATA) ||
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_IS_PHASE_STABILIZATION_REFERENCE) ||
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_IS_PHASE_STABILIZATION) || is_set(flags, ISMRMRD::ISMRMRD_ACQ_IS_REVERSE) ||
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_COMPRESSION1) || is_set(flags, ISMRMRD::ISMRMRD_ACQ_COMPRESSION2) ||
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_COMPRESSION3) || is_set(flags, ISMRMRD::ISMRMRD_ACQ_COMPRESSION4) ||
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_USER1) || is_set(flags, ISMRMRD::ISMRMRD_ACQ_USER2) ||
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_USER3) || is_set(flags, ISMRMRD::ISMRMRD_ACQ_USER4) ||
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_USER5) || is_set(flags, ISMRMRD::ISMRMRD_ACQ_USER6) ||
    is_set(flags, ISMRMRD::ISMRMRD_ACQ_USER7) || is_set(flags, ISMRMRD::ISMRMRD_ACQ_USER8);
  if (unsupported) {
    return ksj::base::Status::ValidationError(
      "ISMRMRD acquisition auxiliary semantic flags require an explicit route policy");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] AcquisitionClassificationInput
make_classification_input(const ksj::ismrmrd::AcquisitionHeader& header) noexcept {
  return {
    .message_kind = IsmrmrdMessageKind::acquisition,
    .flags =
      {
        .noise_measurement = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT),
        .parallel_calibration = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION),
        .parallel_calibration_and_imaging =
          is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION_AND_IMAGING),
        .phase_correction = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_PHASECORR_DATA),
        .navigation = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_NAVIGATION_DATA),
        .dummy_scan = is_set(header.flags, ISMRMRD::ISMRMRD_ACQ_IS_DUMMYSCAN_DATA),
        .explicitly_ignored = false,
      },
    .index =
      {
        .encoding_space = header.encoding_space_ref,
        .kspace_encode_step_1 = header.index.kspace_encode_step_1,
        .kspace_encode_step_2 = header.index.kspace_encode_step_2,
        .average = header.index.average,
        .slice = header.index.slice,
        .contrast = header.index.contrast,
        .phase = header.index.phase,
        .repetition = header.index.repetition,
        .set = header.index.set,
        .segment = header.index.segment,
      },
  };
}

[[nodiscard]] IsmrmrdAcquisitionControlFlags make_control_flags(const std::uint64_t flags) noexcept {
  return {
    .first_in_encode_step_1 = is_set(flags, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_ENCODE_STEP1),
    .last_in_encode_step_1 = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_ENCODE_STEP1),
    .first_in_encode_step_2 = is_set(flags, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_ENCODE_STEP2),
    .last_in_encode_step_2 = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_ENCODE_STEP2),
    .first_in_average = is_set(flags, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_AVERAGE),
    .last_in_average = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_AVERAGE),
    .first_in_slice = is_set(flags, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_SLICE),
    .last_in_slice = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_SLICE),
    .first_in_contrast = is_set(flags, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_CONTRAST),
    .last_in_contrast = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_CONTRAST),
    .first_in_phase = is_set(flags, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_PHASE),
    .last_in_phase = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_PHASE),
    .first_in_repetition = is_set(flags, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_REPETITION),
    .last_in_repetition = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_REPETITION),
    .first_in_set = is_set(flags, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_SET),
    .last_in_set = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_SET),
    .first_in_segment = is_set(flags, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_SEGMENT),
    .last_in_segment = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_SEGMENT),
    .last_in_measurement = is_set(flags, ISMRMRD::ISMRMRD_ACQ_LAST_IN_MEASUREMENT),
  };
}

[[nodiscard]] FrameSemanticKey make_frame_key(const NormalizedAcquisitionIndex& index) noexcept {
  return {
    .encoding_space = index.encoding_space,
    .slice = index.slice,
    .contrast = index.contrast,
    .repetition = index.repetition,
    .set = index.set,
    .phase = index.phase,
    .average = index.average,
    .segment = index.segment,
  };
}

} // namespace

ksj::base::Result<NormalizedIsmrmrdAcquisition>
normalize_ismrmrd_acquisition(const ksj::ismrmrd::AcquisitionView& acquisition,
                              const AcquisitionClassifier& classifier) {
  const auto layout = validate_layout(acquisition);
  if (!layout.ok()) {
    return layout;
  }
  const auto semantic_flags = reject_unhandled_semantic_flags(acquisition.header.flags);
  if (!semantic_flags.ok()) {
    return semantic_flags;
  }

  const auto classification_input = make_classification_input(acquisition.header);
  auto classification = classifier.classify(classification_input);
  if (!classification.ok()) {
    return classification.status();
  }
  const auto sample_bytes = std::as_bytes(acquisition.samples);
  return NormalizedIsmrmrdAcquisition{
    .classification_input = classification_input,
    .classification = std::move(classification).value(),
    .frame_key = make_frame_key(classification_input.index),
    .cartesian_coordinate =
      {
        .phase_encode_1 = classification_input.index.kspace_encode_step_1,
        .phase_encode_2 = classification_input.index.kspace_encode_step_2,
      },
    .control_flags = make_control_flags(acquisition.header.flags),
    .ingress_facts =
      {
        .samples_per_acquisition = acquisition.header.number_of_samples,
        .active_channels = acquisition.header.active_channels,
        .trajectory_dimensions = acquisition.header.trajectory_dimensions,
        .complete = true,
      },
    .sample_bytes = ksj::base::ConstByteSpan{sample_bytes.data(), sample_bytes.size()},
    .trajectory = acquisition.trajectory,
  };
}

FrameSlotContext make_ismrmrd_frame_slot_context(const NormalizedIsmrmrdAcquisition& acquisition,
                                                 const IsmrmrdFrameSlotContextBinding binding) noexcept {
  return {
    .semantic_key = acquisition.frame_key,
    .order_key = binding.order_key,
    .placement_key = binding.placement_key,
  };
}

} // namespace ksj::recon::runtime
