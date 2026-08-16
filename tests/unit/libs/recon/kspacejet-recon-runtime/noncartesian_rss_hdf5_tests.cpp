#include "kspacejet/recon/runtime/noncartesian_rss_hdf5.hpp"

#include <ismrmrd/dataset.h>

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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
  std::filesystem::path metadata;

  ~TestFiles() {
    std::error_code error;
    std::filesystem::remove(input, error);
    std::filesystem::remove(image, error);
    std::filesystem::remove(metadata, error);
  }
};

void append_two_coil_sample(ISMRMRD::Dataset& dataset, const float coil_zero, const float coil_one,
                            const float row_coordinate, const float column_coordinate,
                            const std::uint16_t trajectory_dimensions = 2U,
                            const std::optional<ISMRMRD::ISMRMRD_AcquisitionFlags> flag = std::nullopt) {
  ISMRMRD::Acquisition acquisition(1U, 2U, trajectory_dimensions);
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

void write_flagged_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilRadialXml));
  append_two_coil_sample(dataset, 1.0F, 3.0F, 0.0F, 0.0F, 2U, ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT);
}

void write_nonfinite_trajectory_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilRadialXml));
  append_two_coil_sample(dataset, 1.0F, 3.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F);
}

[[nodiscard]] std::vector<float> read_float32_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  EXPECT_TRUE(input.is_open());
  const auto byte_count = input.tellg();
  EXPECT_EQ(static_cast<std::streamoff>(4U * sizeof(float)), byte_count);
  input.seekg(0U);
  std::vector<float> result(4U);
  input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size() * sizeof(float)));
  EXPECT_TRUE(static_cast<bool>(input));
  return result;
}

[[nodiscard]] ksj::recon::runtime::NoncartesianRssHdf5ReconstructionConfig make_config(const TestFiles& files) {
  return {
    .input_file = files.input,
    .output_image_file = files.image,
    .output_metadata_file = files.metadata,
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
    .image = temporary_path("two_by_two_two_coil_radial.f32"),
    .metadata = temporary_path("two_by_two_two_coil_radial.f32.json"),
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

  const auto image = read_float32_file(files.image);
  ASSERT_EQ(4U, image.size());
  // The two HDF5 trajectory coordinates are (0, 0) and (pi, 0).
  // Each coil's direct adjoint is respectively 1 +/- 2i and 3 +/- 4i,
  // so RSS is sqrt(5 + 25) at every 2x2 pixel.
  for (const auto value : image) {
    EXPECT_NEAR(std::sqrt(30.0F), value, 1.0e-5F);
  }
  std::ifstream metadata(files.metadata, std::ios::binary);
  ASSERT_TRUE(metadata.is_open());
  const std::string document{std::istreambuf_iterator<char>{metadata}, std::istreambuf_iterator<char>{}};
  EXPECT_NE(std::string::npos, document.find("\"samples_read\":2"));
  EXPECT_NE(std::string::npos, document.find("\"noncartesian_adjoint_reconstruct\""));
  EXPECT_NE(std::string::npos, document.find("\"coil_combine_rss\""));
}

TEST(NoncartesianRssHdf5Reconstruction, RejectsMissingTwoDimensionalTrajectoryBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("missing_trajectory.h5"),
    .image = temporary_path("missing_trajectory.f32"),
    .metadata = temporary_path("missing_trajectory.f32.json"),
  };
  write_missing_trajectory_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
  EXPECT_FALSE(std::filesystem::exists(files.metadata));
}

TEST(NoncartesianRssHdf5Reconstruction, RejectsFlaggedAcquisitionBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("flagged_acquisition.h5"),
    .image = temporary_path("flagged_acquisition.f32"),
    .metadata = temporary_path("flagged_acquisition.f32.json"),
  };
  write_flagged_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
  EXPECT_FALSE(std::filesystem::exists(files.metadata));
}

TEST(NoncartesianRssHdf5Reconstruction, RejectsNonfiniteTrajectoryBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("nonfinite_trajectory.h5"),
    .image = temporary_path("nonfinite_trajectory.f32"),
    .metadata = temporary_path("nonfinite_trajectory.f32.json"),
  };
  write_nonfinite_trajectory_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
  EXPECT_FALSE(std::filesystem::exists(files.metadata));
}

} // namespace
