#include "kspacejet/recon/runtime/ismrmrd_semantic_ingress.hpp"

#include "kspacejet/ismrmrd/dataset_reader.hpp"

#include <ismrmrd/ismrmrd.h>

#include <gtest/gtest.h>

#include <array>
#include <complex>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace {

using ksj::recon::runtime::AcquisitionClassifier;
using ksj::recon::runtime::AcquisitionLane;
using ksj::recon::runtime::IsmrmrdFrameSlotContextBinding;

[[nodiscard]] constexpr std::uint64_t flag_bit(const ISMRMRD::ISMRMRD_AcquisitionFlags flag) noexcept {
  return UINT64_C(1) << (static_cast<std::uint64_t>(flag) - 1U);
}

[[nodiscard]] ksj::ismrmrd::AcquisitionHeader make_header(const std::uint64_t flags = 0U) {
  return {
    .flags = flags,
    .number_of_samples = 2U,
    .available_channels = 1U,
    .active_channels = 1U,
    .center_sample = 0U,
    .encoding_space_ref = 3U,
    .trajectory_dimensions = 0U,
    .sample_time_us = 2.5F,
    .index =
      {
        .kspace_encode_step_1 = 5U,
        .kspace_encode_step_2 = 0U,
        .average = 7U,
        .slice = 11U,
        .contrast = 13U,
        .phase = 17U,
        .repetition = 19U,
        .set = 23U,
        .segment = 29U,
      },
  };
}

[[nodiscard]] AcquisitionClassifier classifier() {
  auto result = AcquisitionClassifier::create({});
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

TEST(IsmrmrdSemanticIngress, AcceptsControlFlagsAndPreservesEveryFrameKeyDimension) {
  std::array<std::complex<float>, 2U> samples{std::complex<float>{1.0F, 2.0F}, std::complex<float>{3.0F, 4.0F}};
  const auto flags = flag_bit(ISMRMRD::ISMRMRD_ACQ_FIRST_IN_ENCODE_STEP1) |
                     flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_ENCODE_STEP1) |
                     flag_bit(ISMRMRD::ISMRMRD_ACQ_LAST_IN_MEASUREMENT);
  const ksj::ismrmrd::AcquisitionView acquisition{
    .header = make_header(flags),
    .samples = samples,
    .trajectory = {},
  };

  const auto normalized = ksj::recon::runtime::normalize_ismrmrd_acquisition(acquisition, classifier());
  ASSERT_TRUE(normalized.ok()) << normalized.status();
  EXPECT_EQ(AcquisitionLane::imaging, normalized.value().classification.lane);
  EXPECT_TRUE(normalized.value().control_flags.first_in_encode_step_1);
  EXPECT_TRUE(normalized.value().control_flags.last_in_encode_step_1);
  EXPECT_TRUE(normalized.value().control_flags.last_in_measurement);
  EXPECT_EQ(3U, normalized.value().frame_key.encoding_space);
  EXPECT_EQ(11U, normalized.value().frame_key.slice);
  EXPECT_EQ(13U, normalized.value().frame_key.contrast);
  EXPECT_EQ(17U, normalized.value().frame_key.phase);
  EXPECT_EQ(19U, normalized.value().frame_key.repetition);
  EXPECT_EQ(23U, normalized.value().frame_key.set);
  EXPECT_EQ(7U, normalized.value().frame_key.average);
  EXPECT_EQ(29U, normalized.value().frame_key.segment);
  EXPECT_EQ(5U, normalized.value().cartesian_coordinate.phase_encode_1);
  EXPECT_EQ(2U * sizeof(std::complex<float>), normalized.value().sample_bytes.size());

  const auto context =
    ksj::recon::runtime::make_ismrmrd_frame_slot_context(normalized.value(), {.order_key = 31U, .placement_key = 37U});
  EXPECT_EQ(normalized.value().frame_key, context.semantic_key);
  EXPECT_EQ(31U, context.order_key);
  EXPECT_EQ(37U, context.placement_key);
}

TEST(IsmrmrdSemanticIngress, RejectsConflictingSemanticLanesBeforeFrameAssembly) {
  std::array<std::complex<float>, 2U> samples{};
  const auto flags =
    flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT) | flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_PHASECORR_DATA);
  const ksj::ismrmrd::AcquisitionView acquisition{
    .header = make_header(flags),
    .samples = samples,
    .trajectory = {},
  };

  const auto normalized = ksj::recon::runtime::normalize_ismrmrd_acquisition(acquisition, classifier());
  EXPECT_FALSE(normalized.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, normalized.status().code());
}

TEST(IsmrmrdSemanticIngress, RejectsInvalidLayoutAndNonFiniteValues) {
  std::array<std::complex<float>, 2U> samples{};
  auto layout_header = make_header();
  layout_header.number_of_samples = 3U;
  const ksj::ismrmrd::AcquisitionView malformed{
    .header = layout_header,
    .samples = samples,
    .trajectory = {},
  };
  const auto malformed_result = ksj::recon::runtime::normalize_ismrmrd_acquisition(malformed, classifier());
  EXPECT_FALSE(malformed_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, malformed_result.status().code());

  samples.front() = {std::numeric_limits<float>::quiet_NaN(), 0.0F};
  const ksj::ismrmrd::AcquisitionView non_finite{
    .header = make_header(),
    .samples = samples,
    .trajectory = {},
  };
  const auto non_finite_result = ksj::recon::runtime::normalize_ismrmrd_acquisition(non_finite, classifier());
  EXPECT_FALSE(non_finite_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, non_finite_result.status().code());
}

TEST(IsmrmrdSemanticIngress, RejectsUnknownFlagsRatherThanGuessingAnImagingLane) {
  std::array<std::complex<float>, 2U> samples{};
  const ksj::ismrmrd::AcquisitionView acquisition{
    .header = make_header(UINT64_C(1) << 31U),
    .samples = samples,
    .trajectory = {},
  };

  const auto normalized = ksj::recon::runtime::normalize_ismrmrd_acquisition(acquisition, classifier());
  EXPECT_FALSE(normalized.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, normalized.status().code());
}

TEST(IsmrmrdSemanticIngress, RejectsReverseReadoutUntilARouteImplementsItsSampleOrderSemantics) {
  std::array<std::complex<float>, 2U> samples{};
  const ksj::ismrmrd::AcquisitionView acquisition{
    .header = make_header(flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_REVERSE)),
    .samples = samples,
    .trajectory = {},
  };

  const auto normalized = ksj::recon::runtime::normalize_ismrmrd_acquisition(acquisition, classifier());
  EXPECT_FALSE(normalized.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, normalized.status().code());
}

TEST(IsmrmrdSemanticIngress, RejectsKnownButUnhandledCompressionFlagsWithARoutePolicyDiagnostic) {
  std::array<std::complex<float>, 2U> samples{};
  const ksj::ismrmrd::AcquisitionView acquisition{
    .header = make_header(flag_bit(ISMRMRD::ISMRMRD_ACQ_COMPRESSION1)),
    .samples = samples,
    .trajectory = {},
  };

  const auto normalized = ksj::recon::runtime::normalize_ismrmrd_acquisition(acquisition, classifier());
  EXPECT_FALSE(normalized.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, normalized.status().code());
  EXPECT_NE(std::string::npos, normalized.status().message().find("explicit route policy"));
}

TEST(IsmrmrdSemanticIngress, RejectsInvalidDiscardTrajectoryAndHeaderFacts) {
  std::array<std::complex<float>, 2U> samples{};

  auto discard_header = make_header();
  discard_header.discard_pre = 2U;
  discard_header.discard_post = 1U;
  const ksj::ismrmrd::AcquisitionView invalid_discard{
    .header = discard_header,
    .samples = samples,
    .trajectory = {},
  };
  const auto discard_result = ksj::recon::runtime::normalize_ismrmrd_acquisition(invalid_discard, classifier());
  EXPECT_FALSE(discard_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, discard_result.status().code());

  auto trajectory_header = make_header();
  trajectory_header.trajectory_dimensions = 2U;
  std::array<float, 3U> malformed_trajectory{};
  const ksj::ismrmrd::AcquisitionView invalid_trajectory{
    .header = trajectory_header,
    .samples = samples,
    .trajectory = malformed_trajectory,
  };
  const auto trajectory_result = ksj::recon::runtime::normalize_ismrmrd_acquisition(invalid_trajectory, classifier());
  EXPECT_FALSE(trajectory_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, trajectory_result.status().code());

  auto header_nan = make_header();
  header_nan.sample_time_us = std::numeric_limits<float>::quiet_NaN();
  const ksj::ismrmrd::AcquisitionView invalid_header{
    .header = header_nan,
    .samples = samples,
    .trajectory = {},
  };
  const auto header_result = ksj::recon::runtime::normalize_ismrmrd_acquisition(invalid_header, classifier());
  EXPECT_FALSE(header_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, header_result.status().code());
}

} // namespace
