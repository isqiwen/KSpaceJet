#include "kspacejet/recon/runtime/radial_rss_hdf5.hpp"

#include "kspacejet/array/views.hpp"
#include "kspacejet/nufft/radial_gridding.hpp"

#include <ismrmrd/dataset.h>
#include <ismrmrd/meta.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::string_view kTwoByTwoRadialXml = R"xml(
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

constexpr std::string_view kTwoByTwoSpiralXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>2</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>spiral</trajectory>
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

constexpr std::string_view kTwoByFourRadialXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>2</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>radial</trajectory>
    <encodedSpace>
      <matrixSize><x>4</x><y>2</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>400</x><y>200</y><z>5</z></fieldOfView_mm>
    </encodedSpace>
    <reconSpace>
      <matrixSize><x>4</x><y>2</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>400</x><y>200</y><z>5</z></fieldOfView_mm>
    </reconSpace>
    <encodingLimits>
      <kspace_encoding_step_1><minimum>0</minimum><maximum>1</maximum><center>0</center></kspace_encoding_step_1>
    </encodingLimits>
  </encoding>
</ismrmrdHeader>
)xml";

[[nodiscard]] std::filesystem::path temporary_path(const std::string_view filename) {
  const auto directory = std::filesystem::temp_directory_path() / "ksj_radial_rss_hdf5_tests";
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

void append_two_coil_sample(ISMRMRD::Dataset& dataset, const float coil_zero, const float coil_one, const float raw_kx,
                            const float raw_ky) {
  ISMRMRD::Acquisition acquisition(1U, 2U, 2U);
  acquisition.data(0U, 0U) = {coil_zero, 0.0F};
  acquisition.data(0U, 1U) = {coil_one, 0.0F};
  // ISMRMRD stores raw [kx, ky] as [coordinate][sample]; DatasetReader
  // normalizes only layout to [sample][coordinate]. radial-rss owns the
  // explicit semantic conversion to [row=ky, column=kx].
  acquisition.traj(0U, 0U) = raw_kx;
  acquisition.traj(1U, 0U) = raw_ky;
  dataset.appendAcquisition(acquisition);
}

void write_radial_frame(const std::filesystem::path& path, const std::string_view xml, const float coordinate_scale) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(xml));
  // Two non-central Cartesian-bin samples give the analytic ramp a non-zero
  // contribution while retaining a compact two-channel fixture.
  append_two_coil_sample(dataset, 1.0F, 3.0F, coordinate_scale, 0.0F);
  append_two_coil_sample(dataset, 2.0F, 4.0F, -coordinate_scale, 0.0F);
}

void write_rectangular_radial_frame(const std::filesystem::path& path, const bool encoded_matrix_indices = false) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByFourRadialXml));
  // Both components are deliberately non-zero and asymmetric. The fixture
  // verifies raw [kx,ky] -> canonical [row=ky,column=kx], not merely a
  // unit conversion that a square matrix could hide.
  if (encoded_matrix_indices) {
    // x is encoded width 4 and y is encoded height 2.
    append_two_coil_sample(dataset, 1.0F, 3.0F, 0.5F, 0.5F);
    append_two_coil_sample(dataset, 2.0F, 4.0F, -1.0F, 0.25F);
  } else {
    append_two_coil_sample(dataset, 1.0F, 3.0F, 0.125F, 0.25F);
    append_two_coil_sample(dataset, 2.0F, 4.0F, -0.25F, 0.125F);
  }
}

struct StandardImageArtifact final {
  std::string source_xml;
  ISMRMRD::Image<float> image;
  ISMRMRD::MetaContainer metadata;
};

[[nodiscard]] StandardImageArtifact read_standard_image_artifact(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", false);

  StandardImageArtifact artifact;
  dataset.readHeader(artifact.source_xml);
  EXPECT_EQ(1U, dataset.getNumberOfImages("image_0"));
  dataset.readImage("image_0", 0U, artifact.image);

  std::string attributes;
  artifact.image.getAttributeString(attributes);
  ISMRMRD::deserialize(attributes.c_str(), artifact.metadata);
  return artifact;
}

[[nodiscard]] std::vector<float> image_pixels(const ISMRMRD::Image<float>& image) {
  return {image.getDataPtr(), image.getDataPtr() + image.getNumberOfDataElements()};
}

[[nodiscard]] ksj::recon::runtime::RadialRssHdf5ReconstructionConfig
make_config(const TestFiles& files, const ksj::recon::runtime::RadialHdf5TrajectoryUnits units) {
  return {
    .input_file = files.input,
    .output_image_file = files.image,
    .radial_provider_module = KSJ_NONCARTESIAN_RECON_PROVIDER_MODULE,
    .coil_combine_provider_module = KSJ_COIL_COMBINE_PROVIDER_MODULE,
    .radial_operator_contract = KSJ_RADIAL_GRIDDING_RECON_OPERATOR_CONTRACT,
    .coil_combine_operator_contract = KSJ_COIL_COMBINE_OPERATOR_CONTRACT,
    .dataset_group = "dataset",
    .input_trajectory_units = units,
  };
}

[[nodiscard]] std::vector<float> expected_two_by_four_rss_from_canonical_trajectory() {
  constexpr std::size_t kRows = 2U;
  constexpr std::size_t kCols = 4U;
  constexpr std::size_t kSamples = 2U;
  std::array<ksj::base::cf32, kSamples> first_coil{{{1.0F, 0.0F}, {2.0F, 0.0F}}};
  std::array<ksj::base::cf32, kSamples> second_coil{{{3.0F, 0.0F}, {4.0F, 0.0F}}};
  // Raw fixture points are [kx,ky] = [(1/8,1/4),(-1/4,1/8)] cycles/FOV.
  // The Provider sees [row=ky,column=kx] in radians-per-pixel.
  std::array<float, kSamples * 2U> canonical_trajectory{
    std::numbers::pi_v<float> / 2.0F,
    std::numbers::pi_v<float> / 4.0F,
    std::numbers::pi_v<float> / 4.0F,
    -std::numbers::pi_v<float> / 2.0F,
  };
  std::array<float, kSamples> density{};
  std::array<ksj::base::cf32, kRows * kCols> first_image{};
  std::array<ksj::base::cf32, kRows * kCols> second_image{};
  std::array<ksj::base::cf32, kRows * kCols> intermediate{};
  std::array<ksj::base::cf32, kCols> source{};
  std::array<ksj::base::cf32, kCols> destination{};

  const auto trajectory = ksj::array::MatrixView<const float>{canonical_trajectory.data(), kSamples, 2U};
  const auto density_view = ksj::array::VectorView<float>{density.data(), density.size()};
  const auto workspace = ksj::nufft::RadialGridding2Workspace<float>{
    .fft_intermediate = ksj::array::MatrixView<ksj::base::cf32>{intermediate.data(), kRows, kCols},
    .fft_source = ksj::array::VectorView<ksj::base::cf32>{source.data(), source.size()},
    .fft_destination = ksj::array::VectorView<ksj::base::cf32>{destination.data(), destination.size()},
  };
  const auto grid = ksj::nufft::Grid2D{.rows = kRows, .cols = kCols, .row_origin = 0.5, .col_origin = 1.5};

  ksj::nufft::radial_analytic_ramp_dcf2(trajectory, density_view);
  ksj::nufft::radial_linear_gridding2_adjoint(
    grid, ksj::array::VectorView<const ksj::base::cf32>{first_coil.data(), first_coil.size()}, trajectory,
    ksj::array::as_const_view(density_view), ksj::array::MatrixView<ksj::base::cf32>{first_image.data(), kRows, kCols},
    workspace);
  ksj::nufft::radial_linear_gridding2_adjoint(
    grid, ksj::array::VectorView<const ksj::base::cf32>{second_coil.data(), second_coil.size()}, trajectory,
    ksj::array::as_const_view(density_view), ksj::array::MatrixView<ksj::base::cf32>{second_image.data(), kRows, kCols},
    workspace);

  std::vector<float> expected(kRows * kCols);
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    expected[index] = std::sqrt(std::norm(first_image[index]) + std::norm(second_image[index]));
  }
  return expected;
}

TEST(RadialRssHdf5Reconstruction, RunsTheExplicitAnalyticRampGriddingRouteForCyclesPerFovInput) {
  TestFiles files{
    .input = temporary_path("radial_cycles_per_fov.h5"),
    .image = temporary_path("radial_cycles_per_fov.mrd"),
  };
  write_radial_frame(files.input, kTwoByTwoRadialXml, 0.5F);

  const auto result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(files, ksj::recon::runtime::RadialHdf5TrajectoryUnits::cycles_per_fov));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(2U, result.value().rows);
  EXPECT_EQ(2U, result.value().cols);
  EXPECT_EQ(2U, result.value().channels);
  EXPECT_EQ(2U, result.value().acquisitions_read);
  EXPECT_EQ(2U, result.value().samples_read);
  EXPECT_EQ(4U * sizeof(float), result.value().image_payload_bytes);
  EXPECT_FALSE(result.value().execution_plan_digest.empty());
  EXPECT_FALSE(result.value().verification_record_digest.empty());

  auto artifact = read_standard_image_artifact(files.image);
  EXPECT_EQ(kTwoByTwoRadialXml, artifact.source_xml);
  const auto& header = artifact.image.getHead();
  EXPECT_EQ(ISMRMRD::ISMRMRD_FLOAT, artifact.image.getDataType());
  EXPECT_EQ(2U, header.matrix_size[0]);
  EXPECT_EQ(2U, header.matrix_size[1]);
  EXPECT_EQ(1U, header.matrix_size[2]);
  EXPECT_EQ(1U, header.channels);
  EXPECT_FLOAT_EQ(200.0F, header.field_of_view[0]);
  EXPECT_FLOAT_EQ(200.0F, header.field_of_view[1]);
  EXPECT_FLOAT_EQ(5.0F, header.field_of_view[2]);
  EXPECT_EQ(ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE, header.image_type);
  EXPECT_EQ(1U, header.image_index);
  EXPECT_EQ(0U, header.image_series_index);

  const auto image = image_pixels(artifact.image);
  bool has_nonzero_value{false};
  for (const auto value : image) {
    EXPECT_TRUE(std::isfinite(value));
    has_nonzero_value = has_nonzero_value || value > 0.0F;
  }
  EXPECT_TRUE(has_nonzero_value);
  ASSERT_EQ(1U, artifact.metadata.length("DataRole"));
  EXPECT_STREQ("Image", artifact.metadata.as_str("DataRole"));
  ASSERT_EQ(1U, artifact.metadata.length("ImageNumber"));
  EXPECT_STREQ("1", artifact.metadata.as_str("ImageNumber"));
  ASSERT_EQ(1U, artifact.metadata.length("KSpaceJet.Route"));
  EXPECT_STREQ("radial-rss", artifact.metadata.as_str("KSpaceJet.Route"));
  ASSERT_EQ(2U, artifact.metadata.length("KSpaceJet.Operator"));
  EXPECT_STREQ("radial_gridding_reconstruct", artifact.metadata.as_str("KSpaceJet.Operator", 0U));
  EXPECT_STREQ("coil_combine_rss", artifact.metadata.as_str("KSpaceJet.Operator", 1U));
  ASSERT_EQ(1U, artifact.metadata.length("KSpaceJet.DensityCompensation"));
  EXPECT_STREQ("radial_analytic_ramp", artifact.metadata.as_str("KSpaceJet.DensityCompensation"));
  ASSERT_EQ(1U, artifact.metadata.length("KSpaceJet.InputTrajectoryUnits"));
  EXPECT_STREQ("cycles_per_fov", artifact.metadata.as_str("KSpaceJet.InputTrajectoryUnits"));
  ASSERT_EQ(1U, artifact.metadata.length("KSpaceJet.InputEncodedMatrixColumns"));
  EXPECT_STREQ("2", artifact.metadata.as_str("KSpaceJet.InputEncodedMatrixColumns"));
  ASSERT_EQ(1U, artifact.metadata.length("KSpaceJet.InputEncodedMatrixRows"));
  EXPECT_STREQ("2", artifact.metadata.as_str("KSpaceJet.InputEncodedMatrixRows"));
  ASSERT_EQ(1U, artifact.metadata.length("KSpaceJet.TrajectoryUnits"));
  EXPECT_STREQ("radians_per_pixel", artifact.metadata.as_str("KSpaceJet.TrajectoryUnits"));
  ASSERT_EQ(1U, artifact.metadata.length("KSpaceJet.ExecutionPlanDigest"));
  EXPECT_STREQ(result.value().execution_plan_digest.c_str(), artifact.metadata.as_str("KSpaceJet.ExecutionPlanDigest"));
  ASSERT_EQ(1U, artifact.metadata.length("KSpaceJet.VerificationRecordDigest"));
  EXPECT_STREQ(result.value().verification_record_digest.c_str(),
               artifact.metadata.as_str("KSpaceJet.VerificationRecordDigest"));
}

TEST(RadialRssHdf5Reconstruction, ConvertsRawIsmrmrdKxKyToCanonicalRowColumnForRectangularGeometry) {
  TestFiles files{
    .input = temporary_path("radial_raw_kx_ky_rectangular.h5"),
    .image = temporary_path("radial_raw_kx_ky_rectangular.mrd"),
  };
  write_rectangular_radial_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(files, ksj::recon::runtime::RadialHdf5TrajectoryUnits::cycles_per_fov));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(2U, result.value().rows);
  EXPECT_EQ(4U, result.value().cols);

  auto artifact = read_standard_image_artifact(files.image);
  EXPECT_EQ(4U, artifact.image.getHead().matrix_size[0]);
  EXPECT_EQ(2U, artifact.image.getHead().matrix_size[1]);
  const auto expected = expected_two_by_four_rss_from_canonical_trajectory();
  ASSERT_EQ(expected.size(), artifact.image.getNumberOfDataElements());
  for (std::size_t row = 0U; row < 2U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      EXPECT_NEAR(expected[row * 4U + column], artifact.image(column, row), 1.0e-5F);
    }
  }
}

TEST(RadialRssHdf5Reconstruction, NormalizesRectangularEncodedMatrixIndicesPerRawKxKyAxis) {
  TestFiles cycles{
    .input = temporary_path("radial_rectangular_cycles.h5"),
    .image = temporary_path("radial_rectangular_cycles.mrd"),
  };
  TestFiles encoded_matrix_index{
    .input = temporary_path("radial_rectangular_encoded_index.h5"),
    .image = temporary_path("radial_rectangular_encoded_index.mrd"),
  };
  write_rectangular_radial_frame(cycles.input);
  write_rectangular_radial_frame(encoded_matrix_index.input, true);

  const auto cycles_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(cycles, ksj::recon::runtime::RadialHdf5TrajectoryUnits::cycles_per_fov));
  ASSERT_TRUE(cycles_result.ok()) << cycles_result.status();
  const auto encoded_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(encoded_matrix_index, ksj::recon::runtime::RadialHdf5TrajectoryUnits::encoded_matrix_index));
  ASSERT_TRUE(encoded_result.ok()) << encoded_result.status();

  const auto cycles_image = image_pixels(read_standard_image_artifact(cycles.image).image);
  const auto encoded_image = image_pixels(read_standard_image_artifact(encoded_matrix_index.image).image);
  ASSERT_EQ(cycles_image.size(), encoded_image.size());
  for (std::size_t index = 0U; index < cycles_image.size(); ++index) {
    EXPECT_NEAR(cycles_image[index], encoded_image[index], 1.0e-5F);
  }
}

TEST(RadialRssHdf5Reconstruction, NormalizesEquivalentCyclesRadiansAndEncodedMatrixIndexInputToTheSameImage) {
  TestFiles cycles{
    .input = temporary_path("radial_equivalent_cycles.h5"),
    .image = temporary_path("radial_equivalent_cycles.mrd"),
  };
  TestFiles radians{
    .input = temporary_path("radial_equivalent_radians.h5"),
    .image = temporary_path("radial_equivalent_radians.mrd"),
  };
  TestFiles encoded_matrix_index{
    .input = temporary_path("radial_equivalent_encoded_matrix_index.h5"),
    .image = temporary_path("radial_equivalent_encoded_matrix_index.mrd"),
  };
  write_radial_frame(cycles.input, kTwoByTwoRadialXml, 0.5F);
  write_radial_frame(radians.input, kTwoByTwoRadialXml, std::numbers::pi_v<float>);
  // The XML encoded matrix is 2x2, so one encoded-matrix index is 0.5 cycles/FOV.
  write_radial_frame(encoded_matrix_index.input, kTwoByTwoRadialXml, 1.0F);

  const auto cycles_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(cycles, ksj::recon::runtime::RadialHdf5TrajectoryUnits::cycles_per_fov));
  ASSERT_TRUE(cycles_result.ok()) << cycles_result.status();
  const auto radians_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(radians, ksj::recon::runtime::RadialHdf5TrajectoryUnits::radians_per_pixel));
  ASSERT_TRUE(radians_result.ok()) << radians_result.status();
  const auto encoded_matrix_index_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(encoded_matrix_index, ksj::recon::runtime::RadialHdf5TrajectoryUnits::encoded_matrix_index));
  ASSERT_TRUE(encoded_matrix_index_result.ok()) << encoded_matrix_index_result.status();

  const auto cycles_image = image_pixels(read_standard_image_artifact(cycles.image).image);
  const auto radians_image = image_pixels(read_standard_image_artifact(radians.image).image);
  const auto encoded_matrix_index_image = image_pixels(read_standard_image_artifact(encoded_matrix_index.image).image);
  ASSERT_EQ(cycles_image.size(), radians_image.size());
  ASSERT_EQ(cycles_image.size(), encoded_matrix_index_image.size());
  for (std::size_t index = 0U; index < cycles_image.size(); ++index) {
    EXPECT_NEAR(cycles_image[index], radians_image[index], 1.0e-6F);
    EXPECT_NEAR(cycles_image[index], encoded_matrix_index_image[index], 1.0e-6F);
  }
}

TEST(RadialRssHdf5Reconstruction, RequiresExplicitUnitAndDeclaredRadialGeometryBeforeWritingOutputs) {
  TestFiles missing_unit{
    .input = temporary_path("radial_missing_unit.h5"),
    .image = temporary_path("radial_missing_unit.mrd"),
  };
  write_radial_frame(missing_unit.input, kTwoByTwoRadialXml, 0.5F);
  const auto missing_unit_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(missing_unit, ksj::recon::runtime::RadialHdf5TrajectoryUnits::unspecified));
  EXPECT_FALSE(missing_unit_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, missing_unit_result.status().code());
  EXPECT_FALSE(std::filesystem::exists(missing_unit.image));

  TestFiles nonradial{
    .input = temporary_path("radial_wrong_declared_geometry.h5"),
    .image = temporary_path("radial_wrong_declared_geometry.mrd"),
  };
  write_radial_frame(nonradial.input, kTwoByTwoSpiralXml, 0.5F);
  const auto nonradial_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(nonradial, ksj::recon::runtime::RadialHdf5TrajectoryUnits::cycles_per_fov));
  EXPECT_FALSE(nonradial_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, nonradial_result.status().code());
  EXPECT_FALSE(std::filesystem::exists(nonradial.image));

  TestFiles non_power_of_two{
    .input = temporary_path("radial_non_power_of_two_geometry.h5"),
    .image = temporary_path("radial_non_power_of_two_geometry.mrd"),
  };
  auto non_power_of_two_xml = std::string(kTwoByFourRadialXml);
  for (auto position = non_power_of_two_xml.find("<x>4</x>"); position != std::string::npos;
       position = non_power_of_two_xml.find("<x>4</x>", position + 1U)) {
    non_power_of_two_xml.replace(position, sizeof("<x>4</x>") - 1U, "<x>3</x>");
  }
  write_radial_frame(non_power_of_two.input, non_power_of_two_xml, 0.5F);
  const auto non_power_of_two_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(non_power_of_two, ksj::recon::runtime::RadialHdf5TrajectoryUnits::cycles_per_fov));
  EXPECT_FALSE(non_power_of_two_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, non_power_of_two_result.status().code());
  EXPECT_FALSE(std::filesystem::exists(non_power_of_two.image));
}

TEST(RadialRssHdf5Reconstruction, RejectsOutOfRangeInputCoordinatesAndWrongProviderContractBeforeWritingOutputs) {
  TestFiles outside_unit_range{
    .input = temporary_path("radial_outside_unit_range.h5"),
    .image = temporary_path("radial_outside_unit_range.mrd"),
  };
  write_radial_frame(outside_unit_range.input, kTwoByTwoRadialXml, 0.75F);
  const auto outside_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(
    make_config(outside_unit_range, ksj::recon::runtime::RadialHdf5TrajectoryUnits::cycles_per_fov));
  EXPECT_FALSE(outside_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, outside_result.status().code());
  EXPECT_FALSE(std::filesystem::exists(outside_unit_range.image));

  TestFiles outside_encoded_matrix_index_range{
    .input = temporary_path("radial_outside_encoded_matrix_index_range.h5"),
    .image = temporary_path("radial_outside_encoded_matrix_index_range.mrd"),
  };
  write_radial_frame(outside_encoded_matrix_index_range.input, kTwoByTwoRadialXml, 1.25F);
  const auto outside_encoded_matrix_index_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(make_config(
    outside_encoded_matrix_index_range, ksj::recon::runtime::RadialHdf5TrajectoryUnits::encoded_matrix_index));
  EXPECT_FALSE(outside_encoded_matrix_index_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, outside_encoded_matrix_index_result.status().code());
  EXPECT_FALSE(std::filesystem::exists(outside_encoded_matrix_index_range.image));

  TestFiles wrong_contract{
    .input = temporary_path("radial_wrong_contract.h5"),
    .image = temporary_path("radial_wrong_contract.mrd"),
  };
  write_radial_frame(wrong_contract.input, kTwoByTwoRadialXml, 0.5F);
  auto config = make_config(wrong_contract, ksj::recon::runtime::RadialHdf5TrajectoryUnits::cycles_per_fov);
  config.radial_operator_contract = KSJ_NONCARTESIAN_RECON_OPERATOR_CONTRACT;
  const auto wrong_contract_result = ksj::recon::runtime::reconstruct_radial_rss_hdf5(config);
  EXPECT_FALSE(wrong_contract_result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, wrong_contract_result.status().code());
  EXPECT_FALSE(std::filesystem::exists(wrong_contract.image));
}

} // namespace
