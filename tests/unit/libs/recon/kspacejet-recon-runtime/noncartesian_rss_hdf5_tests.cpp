#include "kspacejet/recon/runtime/noncartesian_rss_hdf5.hpp"

#include <ismrmrd/dataset.h>
#include <ismrmrd/meta.h>

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr std::string_view kTwoByTwoTwoCoilRadialXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>2</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>radial</trajectory>
    <encodedSpace>
      <matrixSize><x>2</x><y>2</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>200</x><y>200</y><z>5</z></fieldOfView_mm>
    </encodedSpace>
    <reconSpace>
      <matrixSize><x>2</x><y>2</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>200</x><y>200</y><z>5</z></fieldOfView_mm>
    </reconSpace>
    <encodingLimits>
      <kspace_encoding_step_1><minimum>0</minimum><maximum>1</maximum><center>0</center></kspace_encoding_step_1>
    </encodingLimits>
  </encoding>
</ismrmrdHeader>
)xml";

[[nodiscard]] std::filesystem::path temporary_path(const std::string_view filename) {
  const auto directory = std::filesystem::temp_directory_path() / "ksj_noncartesian_rss_hdf5_tests";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  const auto path = directory / filename;
  std::filesystem::remove(path, error);
  return path;
}

struct TestFiles final {
  std::filesystem::path input;
  std::filesystem::path image;

  ~TestFiles() {
    std::error_code error;
    std::filesystem::remove(input, error);
    std::filesystem::remove(image, error);
  }
};

void append_two_coil_sample(ISMRMRD::Dataset& dataset, const float coil_zero, const float coil_one,
                            const float row_coordinate, const float column_coordinate,
                            const std::uint16_t trajectory_dimensions = 2U,
                            const std::optional<ISMRMRD::ISMRMRD_AcquisitionFlags> flag = std::nullopt,
                            const std::uint16_t slice = 0U, const std::uint16_t segment = 0U) {
  ISMRMRD::Acquisition acquisition(1U, 2U, trajectory_dimensions);
  acquisition.idx().slice = slice;
  acquisition.idx().segment = segment;
  if (flag.has_value()) {
    acquisition.setFlag(*flag);
  }
  acquisition.data(0U, 0U) = {coil_zero, 0.0F};
  acquisition.data(0U, 1U) = {coil_one, 0.0F};
  if (trajectory_dimensions == 2U) {
    // ISMRMRD trajectory storage is [coordinate][sample]. DatasetReader
    // normalizes it into its public [sample][coordinate] span.
    acquisition.traj(0U, 0U) = row_coordinate;
    acquisition.traj(1U, 0U) = column_coordinate;
  }
  dataset.appendAcquisition(acquisition);
}

void write_two_acquisition_nonuniform_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilRadialXml));
  append_two_coil_sample(dataset, 1.0F, 3.0F, 0.0F, 0.0F);
  append_two_coil_sample(dataset, 2.0F, 4.0F, std::numbers::pi_v<float>, 0.0F);
}

void write_missing_trajectory_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilRadialXml));
  append_two_coil_sample(dataset, 1.0F, 3.0F, 0.0F, 0.0F, 0U);
}

void write_noise_semantic_lane_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilRadialXml));
  append_two_coil_sample(dataset, 1.0F, 3.0F, 0.0F, 0.0F, 2U, ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT);
}

void write_control_flagged_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilRadialXml));
  append_two_coil_sample(dataset, 1.0F, 3.0F, 0.0F, 0.0F, 2U, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_ENCODE_STEP1, 3U);
  append_two_coil_sample(dataset, 2.0F, 4.0F, std::numbers::pi_v<float>, 0.0F, 2U,
                         ISMRMRD::ISMRMRD_ACQ_LAST_IN_MEASUREMENT, 3U);
}

void write_mixed_semantic_context_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilRadialXml));
  append_two_coil_sample(dataset, 1.0F, 3.0F, 0.0F, 0.0F, 2U, std::nullopt, 0U, 0U);
  append_two_coil_sample(dataset, 2.0F, 4.0F, std::numbers::pi_v<float>, 0.0F, 2U, std::nullopt, 0U, 1U);
}

void write_nonfinite_trajectory_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilRadialXml));
  append_two_coil_sample(dataset, 1.0F, 3.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F);
}

[[nodiscard]] ksj::recon::runtime::NoncartesianRssHdf5ReconstructionConfig make_config(const TestFiles& files) {
  return {
    .input_file = files.input,
    .output_image_file = files.image,
    .noncartesian_provider_module = KSJ_NONCARTESIAN_RECON_PROVIDER_MODULE,
    .coil_combine_provider_module = KSJ_COIL_COMBINE_PROVIDER_MODULE,
    .noncartesian_operator_contract = KSJ_NONCARTESIAN_RECON_OPERATOR_CONTRACT,
    .coil_combine_operator_contract = KSJ_COIL_COMBINE_OPERATOR_CONTRACT,
    .dataset_group = "dataset",
  };
}

TEST(NoncartesianRssHdf5Reconstruction, ReconstructsMultiCoilRssFromActualTrajectoryThroughTheGenericGraph) {
  TestFiles files{
    .input = temporary_path("two_by_two_two_coil_radial.h5"),
    .image = temporary_path("two_by_two_two_coil_radial.mrd"),
  };
  write_two_acquisition_nonuniform_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(make_config(files));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(2U, result.value().rows);
  EXPECT_EQ(2U, result.value().cols);
  EXPECT_EQ(2U, result.value().channels);
  EXPECT_EQ(2U, result.value().acquisitions_read);
  EXPECT_EQ(2U, result.value().samples_read);
  EXPECT_EQ(4U * sizeof(float), result.value().image_payload_bytes);
  EXPECT_FALSE(result.value().execution_plan_digest.empty());
  EXPECT_FALSE(result.value().verification_record_digest.empty());

  const auto output_filename = files.image.string();
  ISMRMRD::Dataset output(output_filename.c_str(), "dataset", false);
  std::string output_xml;
  output.readHeader(output_xml);
  EXPECT_EQ(std::string(kTwoByTwoTwoCoilRadialXml), output_xml);
  ASSERT_EQ(1U, output.getNumberOfImages("image_0"));

  ISMRMRD::Image<float> image;
  output.readImage("image_0", 0U, image);
  EXPECT_EQ(ISMRMRD::ISMRMRD_FLOAT, image.getDataType());
  EXPECT_EQ(2U, image.getMatrixSizeX());
  EXPECT_EQ(2U, image.getMatrixSizeY());
  EXPECT_EQ(1U, image.getMatrixSizeZ());
  EXPECT_EQ(1U, image.getNumberOfChannels());
  EXPECT_FLOAT_EQ(200.0F, image.getFieldOfViewX());
  EXPECT_FLOAT_EQ(200.0F, image.getFieldOfViewY());
  EXPECT_FLOAT_EQ(5.0F, image.getFieldOfViewZ());
  EXPECT_EQ(ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE, image.getImageType());
  EXPECT_EQ(1U, image.getImageIndex());
  EXPECT_EQ(0U, image.getImageSeriesIndex());
  EXPECT_EQ(4U, image.getNumberOfDataElements());

  // The two HDF5 trajectory coordinates are (0, 0) and (pi, 0).
  // Each coil's direct adjoint is respectively 1 +/- 2i and 3 +/- 4i,
  // so RSS is sqrt(5 + 25) at every 2x2 pixel.
  for (std::uint16_t y = 0U; y < image.getMatrixSizeY(); ++y) {
    for (std::uint16_t x = 0U; x < image.getMatrixSizeX(); ++x) {
      EXPECT_NEAR(std::sqrt(30.0F), image(x, y), 1.0e-5F);
    }
  }

  std::string serialized_metadata;
  image.getAttributeString(serialized_metadata);
  ISMRMRD::MetaContainer metadata;
  ISMRMRD::deserialize(serialized_metadata.c_str(), metadata);
  ASSERT_EQ(1U, metadata.length("DataRole"));
  EXPECT_STREQ("Image", metadata.as_str("DataRole"));
  ASSERT_EQ(1U, metadata.length("ImageNumber"));
  EXPECT_STREQ("1", metadata.as_str("ImageNumber"));
  ASSERT_EQ(1U, metadata.length("KSpaceJet.Route"));
  EXPECT_STREQ("noncartesian-rss", metadata.as_str("KSpaceJet.Route"));
  ASSERT_EQ(1U, metadata.length("KSpaceJet.AcquisitionsRead"));
  EXPECT_STREQ("2", metadata.as_str("KSpaceJet.AcquisitionsRead"));
  ASSERT_EQ(1U, metadata.length("KSpaceJet.SamplesRead"));
  EXPECT_STREQ("2", metadata.as_str("KSpaceJet.SamplesRead"));
  ASSERT_EQ(1U, metadata.length("KSpaceJet.InputChannels"));
  EXPECT_STREQ("2", metadata.as_str("KSpaceJet.InputChannels"));
  ASSERT_EQ(1U, metadata.length("KSpaceJet.ExecutionPlanDigest"));
  EXPECT_EQ(result.value().execution_plan_digest, metadata.as_str("KSpaceJet.ExecutionPlanDigest"));
  ASSERT_EQ(1U, metadata.length("KSpaceJet.VerificationRecordDigest"));
  EXPECT_EQ(result.value().verification_record_digest, metadata.as_str("KSpaceJet.VerificationRecordDigest"));
  ASSERT_EQ(2U, metadata.length("KSpaceJet.Operator"));
  EXPECT_STREQ("noncartesian_adjoint_reconstruct", metadata.as_str("KSpaceJet.Operator", 0U));
  EXPECT_STREQ("coil_combine_rss", metadata.as_str("KSpaceJet.Operator", 1U));
}

TEST(NoncartesianRssHdf5Reconstruction, RejectsMissingTwoDimensionalTrajectoryBeforeWritingOutput) {
  TestFiles files{
    .input = temporary_path("missing_trajectory.h5"),
    .image = temporary_path("missing_trajectory.mrd"),
  };
  write_missing_trajectory_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
}

TEST(NoncartesianRssHdf5Reconstruction, RejectsUnsupportedNoiseSemanticLaneBeforeWritingOutput) {
  TestFiles files{
    .input = temporary_path("flagged_acquisition.h5"),
    .image = temporary_path("flagged_acquisition.mrd"),
  };
  write_noise_semantic_lane_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_NE(std::string::npos, result.status().message().find("imaging semantic lane"));
  EXPECT_FALSE(std::filesystem::exists(files.image));
}

TEST(NoncartesianRssHdf5Reconstruction, AllowsControlFlagsForOneNonzeroFrameSlotSemanticContext) {
  TestFiles files{
    .input = temporary_path("control_flagged_acquisitions.h5"),
    .image = temporary_path("control_flagged_acquisitions.mrd"),
  };
  write_control_flagged_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(make_config(files));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(2U, result.value().acquisitions_read);
  EXPECT_EQ(2U, result.value().samples_read);
  EXPECT_TRUE(std::filesystem::exists(files.image));
}

TEST(NoncartesianRssHdf5Reconstruction, RejectsMixedFrameSlotSemanticContextsBeforeWritingOutput) {
  TestFiles files{
    .input = temporary_path("mixed_semantic_contexts.h5"),
    .image = temporary_path("mixed_semantic_contexts.mrd"),
  };
  write_mixed_semantic_context_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_NE(std::string::npos, result.status().message().find("mixed FrameSlot semantic contexts"));
  EXPECT_FALSE(std::filesystem::exists(files.image));
}

TEST(NoncartesianRssHdf5Reconstruction, RejectsNonfiniteTrajectoryBeforeWritingOutput) {
  TestFiles files{
    .input = temporary_path("nonfinite_trajectory.h5"),
    .image = temporary_path("nonfinite_trajectory.mrd"),
  };
  write_nonfinite_trajectory_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
}

} // namespace
