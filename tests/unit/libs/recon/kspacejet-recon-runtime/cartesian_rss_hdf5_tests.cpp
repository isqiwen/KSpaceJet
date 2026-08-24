#include "kspacejet/recon/runtime/cartesian_rss_hdf5.hpp"

#include <ismrmrd/dataset.h>
#include <ismrmrd/meta.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using FourSamples = std::array<std::complex<float>, 4U>;

constexpr std::string_view kTwoByTwoTwoCoilCartesianXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>2</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>cartesian</trajectory>
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

constexpr std::string_view kFourByTwoTwoCoilCartesianXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>2</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>cartesian</trajectory>
    <encodedSpace>
      <matrixSize><x>4</x><y>2</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>400</x><y>200</y><z>5</z></fieldOfView_mm>
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
  const auto directory = std::filesystem::temp_directory_path() / "ksj_cartesian_rss_hdf5_tests";
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

void set_standard_image_source_header(ISMRMRD::Acquisition& acquisition) {
  acquisition.measurement_uid() = 42U;
  acquisition.position()[0] = 1.0F;
  acquisition.position()[1] = 2.0F;
  acquisition.position()[2] = 3.0F;
  acquisition.read_dir()[0] = 1.0F;
  acquisition.read_dir()[1] = 0.0F;
  acquisition.read_dir()[2] = 0.0F;
  acquisition.phase_dir()[0] = 0.0F;
  acquisition.phase_dir()[1] = 1.0F;
  acquisition.phase_dir()[2] = 0.0F;
  acquisition.slice_dir()[0] = 0.0F;
  acquisition.slice_dir()[1] = 0.0F;
  acquisition.slice_dir()[2] = 1.0F;
  acquisition.patient_table_position()[0] = 4.0F;
  acquisition.patient_table_position()[1] = 5.0F;
  acquisition.patient_table_position()[2] = 6.0F;
  acquisition.acquisition_time_stamp() = 123U;
}

void append_two_coil_line(ISMRMRD::Dataset& dataset, const std::uint16_t ky, const float coil_zero_dc,
                          const float coil_one_dc,
                          const std::optional<ISMRMRD::ISMRMRD_AcquisitionFlags> flag = std::nullopt,
                          const std::uint16_t segment = 0U) {
  ISMRMRD::Acquisition acquisition(2U, 2U, 0U);
  acquisition.idx().kspace_encode_step_1 = ky;
  acquisition.idx().kspace_encode_step_2 = 0U;
  acquisition.idx().segment = segment;
  set_standard_image_source_header(acquisition);
  if (flag.has_value()) {
    acquisition.setFlag(*flag);
  }
  acquisition.data(0U, 0U) = ky == 0U ? std::complex<float>{coil_zero_dc, 0.0F} : std::complex<float>{};
  acquisition.data(1U, 0U) = {};
  acquisition.data(0U, 1U) = ky == 0U ? std::complex<float>{coil_one_dc, 0.0F} : std::complex<float>{};
  acquisition.data(1U, 1U) = {};
  dataset.appendAcquisition(acquisition);
}

void append_two_coil_four_sample_line(ISMRMRD::Dataset& dataset, const std::uint16_t ky, const FourSamples& coil_zero,
                                      const FourSamples& coil_one,
                                      const std::optional<ISMRMRD::ISMRMRD_AcquisitionFlags> flag = std::nullopt) {
  ISMRMRD::Acquisition acquisition(4U, 2U, 0U);
  acquisition.idx().kspace_encode_step_1 = ky;
  acquisition.idx().kspace_encode_step_2 = 0U;
  set_standard_image_source_header(acquisition);
  if (flag.has_value()) {
    acquisition.setFlag(*flag);
  }
  for (std::uint16_t sample = 0U; sample < 4U; ++sample) {
    acquisition.data(sample, 0U) = coil_zero[sample];
    acquisition.data(sample, 1U) = coil_one[sample];
  }
  dataset.appendAcquisition(acquisition);
}

void write_two_by_two_two_coil_dc_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilCartesianXml));
  // The line order is intentionally non-monotonic.  The host assembler must
  // preserve the channel-major source layout while placing each ky line.
  append_two_coil_line(dataset, 1U, 4.0F, 8.0F);
  append_two_coil_line(dataset, 0U, 4.0F, 8.0F);
}

void write_duplicate_ky_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilCartesianXml));
  append_two_coil_line(dataset, 0U, 4.0F, 8.0F);
  append_two_coil_line(dataset, 0U, 4.0F, 8.0F);
}

void write_missing_ky_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilCartesianXml));
  append_two_coil_line(dataset, 0U, 4.0F, 8.0F);
}

void write_flagged_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilCartesianXml));
  append_two_coil_line(dataset, 0U, 4.0F, 8.0F, ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT);
  append_two_coil_line(dataset, 1U, 4.0F, 8.0F);
}

void write_control_flagged_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilCartesianXml));
  append_two_coil_line(dataset, 0U, 4.0F, 8.0F, ISMRMRD::ISMRMRD_ACQ_FIRST_IN_ENCODE_STEP1);
  append_two_coil_line(dataset, 1U, 4.0F, 8.0F, ISMRMRD::ISMRMRD_ACQ_LAST_IN_MEASUREMENT);
}

void write_mixed_semantic_context_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kTwoByTwoTwoCoilCartesianXml));
  append_two_coil_line(dataset, 0U, 4.0F, 8.0F, std::nullopt, 0U);
  append_two_coil_line(dataset, 1U, 4.0F, 8.0F, std::nullopt, 1U);
}

void write_full_conditioning_frame(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kFourByTwoTwoCoilCartesianXml));

  // The two calibration channels have zero means and a diagonal population
  // covariance of 1/2.  The numerics-backed estimate therefore applies a
  // sqrt(2) whitening gain to the one populated imaging channel.
  append_two_coil_four_sample_line(
    dataset, 0U, FourSamples{std::complex<float>{1.0F, 0.0F}, std::complex<float>{-1.0F, 0.0F}, {}, {}},
    FourSamples{std::complex<float>{}, std::complex<float>{}, std::complex<float>{1.0F, 0.0F},
                std::complex<float>{-1.0F, 0.0F}},
    ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT);
  // A positive-real phase reference produces a unit phase-correction model.
  append_two_coil_four_sample_line(dataset, 0U,
                                   FourSamples{std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},
                                               std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F}},
                                   FourSamples{std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},
                                               std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F}},
                                   ISMRMRD::ISMRMRD_ACQ_IS_PHASECORR_DATA);
  // The rank-one calibration chooses the first physical channel as the one
  // virtual channel. Its phase is irrelevant to RSS, while its selection is
  // tested by the nonzero output below.
  append_two_coil_four_sample_line(dataset, 0U, FourSamples{std::complex<float>{1.0F, 0.0F}, {}, {}, {}}, FourSamples{},
                                   ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION);
  // The encoded x=4 data puts DC at x=1, so the explicit offset-one crop
  // yields one 2x2 virtual-coil DC frame with magnitude 4*sqrt(2).
  append_two_coil_four_sample_line(dataset, 1U, FourSamples{}, FourSamples{});
  append_two_coil_four_sample_line(
    dataset, 0U,
    FourSamples{std::complex<float>{}, std::complex<float>{4.0F, 0.0F}, std::complex<float>{}, std::complex<float>{}},
    FourSamples{});
}

[[nodiscard]] ISMRMRD::Image<float> read_standard_image(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", false);
  std::string xml;
  dataset.readHeader(xml);
  EXPECT_NE(std::string::npos, xml.find("<trajectory>cartesian</trajectory>"));
  EXPECT_EQ(1U, dataset.getNumberOfImages("image_0"));
  ISMRMRD::Image<float> image;
  dataset.readImage("image_0", 0U, image);
  return image;
}

void expect_standard_metadata(const ISMRMRD::Image<float>& image, const std::size_t expected_operator_count) {
  std::string serialized;
  image.getAttributeString(serialized);
  ISMRMRD::MetaContainer metadata;
  ISMRMRD::deserialize(serialized.c_str(), metadata);
  EXPECT_EQ(1U, metadata.length("DataRole"));
  EXPECT_STREQ("Image", metadata.as_str("DataRole"));
  EXPECT_EQ(1U, metadata.length("ImageNumber"));
  EXPECT_STREQ("1", metadata.as_str("ImageNumber"));
  EXPECT_EQ(expected_operator_count, metadata.length("KSpaceJet.Operator"));
  EXPECT_EQ(1U, metadata.length("KSpaceJet.ExecutionPlanDigest"));
  EXPECT_EQ(1U, metadata.length("KSpaceJet.VerificationRecordDigest"));
  EXPECT_EQ(1U, metadata.length("KSpaceJet.SourceXmlDigest"));
}

[[nodiscard]] ksj::recon::runtime::CartesianRssHdf5ReconstructionConfig make_config(const TestFiles& files) {
  return {
    .input_file = files.input,
    .output_image_file = files.image,
    .cartesian_provider_module = KSJ_CARTESIAN_RECON_PROVIDER_MODULE,
    .coil_combine_provider_module = KSJ_COIL_COMBINE_PROVIDER_MODULE,
    .cartesian_operator_contract = KSJ_CARTESIAN_RECON_OPERATOR_CONTRACT,
    .coil_combine_operator_contract = KSJ_COIL_COMBINE_OPERATOR_CONTRACT,
    .dataset_group = "dataset",
  };
}

[[nodiscard]] ksj::recon::runtime::CartesianRssHdf5ReconstructionConfig
make_full_conditioning_config(const TestFiles& files) {
  auto config = make_config(files);
  config.noise_prewhiten = {
    .noise_model_estimate = {.provider_module = KSJ_CALIBRATION_PROVIDER_MODULE,
                             .operator_contract = KSJ_NOISE_MODEL_ESTIMATE_OPERATOR_CONTRACT},
    .noise_prewhiten = {.provider_module = KSJ_KSPACE_CONDITIONING_PROVIDER_MODULE,
                        .operator_contract = KSJ_NOISE_PREWHITEN_OPERATOR_CONTRACT},
  };
  config.phase_correction = {
    .phase_correction_estimate = {.provider_module = KSJ_CALIBRATION_PROVIDER_MODULE,
                                  .operator_contract = KSJ_PHASE_CORRECTION_ESTIMATE_OPERATOR_CONTRACT},
    .phase_correct = {.provider_module = KSJ_KSPACE_CONDITIONING_PROVIDER_MODULE,
                      .operator_contract = KSJ_PHASE_CORRECT_OPERATOR_CONTRACT},
  };
  config.coil_compression = {
    .coil_compression_basis_estimate = {.provider_module = KSJ_CALIBRATION_PROVIDER_MODULE,
                                        .operator_contract = KSJ_COIL_COMPRESSION_BASIS_ESTIMATE_OPERATOR_CONTRACT},
    .coil_compress = {.provider_module = KSJ_KSPACE_CONDITIONING_PROVIDER_MODULE,
                      .operator_contract = KSJ_COIL_COMPRESS_OPERATOR_CONTRACT},
    .virtual_channel_count = 1U,
  };
  config.readout_oversampling_removal = {
    .readout_oversampling_remove = {.provider_module = KSJ_KSPACE_CONDITIONING_PROVIDER_MODULE,
                                    .operator_contract = KSJ_READOUT_OVERSAMPLING_REMOVE_OPERATOR_CONTRACT},
    .readout_offset = 1U,
  };
  return config;
}

TEST(CartesianRssHdf5Reconstruction, ReconstructsMultiCoilRssThroughTheGenericGraph) {
  TestFiles files{
    .input = temporary_path("two_by_two_two_coil_dc.h5"),
    .image = temporary_path("two_by_two_two_coil_dc.mrd"),
  };
  write_two_by_two_two_coil_dc_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_config(files));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(2U, result.value().rows);
  EXPECT_EQ(2U, result.value().cols);
  EXPECT_EQ(2U, result.value().channels);
  EXPECT_EQ(2U, result.value().acquisitions_read);
  EXPECT_EQ(4U * sizeof(float), result.value().image_payload_bytes);
  EXPECT_FALSE(result.value().execution_plan_digest.empty());
  EXPECT_FALSE(result.value().verification_record_digest.empty());

  auto image = read_standard_image(files.image);
  const auto& header = image.getHead();
  EXPECT_EQ(ISMRMRD::ISMRMRD_FLOAT, image.getDataType());
  EXPECT_EQ(2U, header.matrix_size[0]);
  EXPECT_EQ(2U, header.matrix_size[1]);
  EXPECT_EQ(1U, header.matrix_size[2]);
  EXPECT_EQ(1U, header.channels);
  EXPECT_EQ(ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE, header.image_type);
  EXPECT_EQ(42U, header.measurement_uid);
  EXPECT_FLOAT_EQ(200.0F, header.field_of_view[0]);
  EXPECT_FLOAT_EQ(200.0F, header.field_of_view[1]);
  EXPECT_FLOAT_EQ(5.0F, header.field_of_view[2]);
  EXPECT_FLOAT_EQ(1.0F, header.position[0]);
  EXPECT_FLOAT_EQ(2.0F, header.position[1]);
  EXPECT_FLOAT_EQ(3.0F, header.position[2]);
  EXPECT_EQ(123U, header.acquisition_time_stamp);
  for (std::uint16_t row = 0U; row < 2U; ++row) {
    for (std::uint16_t column = 0U; column < 2U; ++column) {
      EXPECT_NEAR(std::sqrt(5.0F), image(column, row), 1.0e-5F);
    }
  }
  expect_standard_metadata(image, 2U);
  EXPECT_FALSE(std::filesystem::exists(files.image.string() + ".json"));
}

TEST(CartesianRssHdf5Reconstruction, RunsAllExplicitCalibrationAndConditioningBranchesThroughTheGenericGraph) {
  TestFiles files{
    .input = temporary_path("full_conditioning_two_by_two.h5"),
    .image = temporary_path("full_conditioning_two_by_two.mrd"),
  };
  write_full_conditioning_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_full_conditioning_config(files));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(2U, result.value().rows);
  EXPECT_EQ(2U, result.value().cols);
  EXPECT_EQ(1U, result.value().channels);
  EXPECT_EQ(5U, result.value().acquisitions_read);

  auto image = read_standard_image(files.image);
  for (std::uint16_t row = 0U; row < 2U; ++row) {
    for (std::uint16_t column = 0U; column < 2U; ++column) {
      EXPECT_NEAR(std::sqrt(2.0F), image(column, row), 2.0e-4F);
    }
  }
  expect_standard_metadata(image, 9U);
}

TEST(CartesianRssHdf5Reconstruction, RejectsDuplicateKyDuringPreflightBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("duplicate_ky.h5"),
    .image = temporary_path("duplicate_ky.mrd"),
  };
  write_duplicate_ky_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
}

TEST(CartesianRssHdf5Reconstruction, RejectsMissingKyDuringPreflightBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("missing_ky.h5"),
    .image = temporary_path("missing_ky.mrd"),
  };
  write_missing_ky_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
}

TEST(CartesianRssHdf5Reconstruction, RejectsFlaggedAcquisitionDuringPreflightBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("flagged_acquisition.h5"),
    .image = temporary_path("flagged_acquisition.mrd"),
  };
  write_flagged_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
}

TEST(CartesianRssHdf5Reconstruction, AllowsNonSemanticControlFlagsThroughTheSharedIngress) {
  TestFiles files{
    .input = temporary_path("control_flagged_acquisitions.h5"),
    .image = temporary_path("control_flagged_acquisitions.mrd"),
  };
  write_control_flagged_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_config(files));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(2U, result.value().acquisitions_read);
  EXPECT_TRUE(std::filesystem::exists(files.image));
}

TEST(CartesianRssHdf5Reconstruction, RejectsMixedFrameSlotSemanticContextsBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("mixed_semantic_contexts.h5"),
    .image = temporary_path("mixed_semantic_contexts.mrd"),
  };
  write_mixed_semantic_context_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_NE(std::string::npos, result.status().message().find("mixed FrameSlot semantic contexts"));
  EXPECT_FALSE(std::filesystem::exists(files.image));
}

} // namespace
