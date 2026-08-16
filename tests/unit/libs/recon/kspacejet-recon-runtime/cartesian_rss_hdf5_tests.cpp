#include "kspacejet/recon/runtime/cartesian_rss_hdf5.hpp"

#include <ismrmrd/dataset.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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
  std::filesystem::path metadata;

  ~TestFiles() {
    std::error_code error;
    std::filesystem::remove(input, error);
    std::filesystem::remove(image, error);
    std::filesystem::remove(metadata, error);
  }
};

void append_two_coil_line(ISMRMRD::Dataset& dataset, const std::uint16_t ky, const float coil_zero_dc,
                          const float coil_one_dc,
                          const std::optional<ISMRMRD::ISMRMRD_AcquisitionFlags> flag = std::nullopt) {
  ISMRMRD::Acquisition acquisition(2U, 2U, 0U);
  acquisition.idx().kspace_encode_step_1 = ky;
  acquisition.idx().kspace_encode_step_2 = 0U;
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

[[nodiscard]] ksj::recon::runtime::CartesianRssHdf5ReconstructionConfig make_config(const TestFiles& files) {
  return {
    .input_file = files.input,
    .output_image_file = files.image,
    .output_metadata_file = files.metadata,
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
    .image = temporary_path("two_by_two_two_coil_dc.f32"),
    .metadata = temporary_path("two_by_two_two_coil_dc.f32.json"),
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

  const auto image = read_float32_file(files.image);
  ASSERT_EQ(4U, image.size());
  for (const auto value : image) {
    EXPECT_NEAR(std::sqrt(5.0F), value, 1.0e-5F);
  }
  std::ifstream metadata(files.metadata, std::ios::binary);
  ASSERT_TRUE(metadata.is_open());
  const std::string document{std::istreambuf_iterator<char>{metadata}, std::istreambuf_iterator<char>{}};
  EXPECT_NE(std::string::npos, document.find("\"rows\":2"));
  EXPECT_NE(std::string::npos, document.find("\"cols\":2"));
  EXPECT_NE(std::string::npos, document.find("\"channels\":2"));
  EXPECT_NE(std::string::npos, document.find("\"cartesian_ifft2_coil_images\""));
  EXPECT_NE(std::string::npos, document.find("\"coil_combine_rss\""));
}

TEST(CartesianRssHdf5Reconstruction, RunsAllExplicitCalibrationAndConditioningBranchesThroughTheGenericGraph) {
  TestFiles files{
    .input = temporary_path("full_conditioning_two_by_two.h5"),
    .image = temporary_path("full_conditioning_two_by_two.f32"),
    .metadata = temporary_path("full_conditioning_two_by_two.f32.json"),
  };
  write_full_conditioning_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_full_conditioning_config(files));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(2U, result.value().rows);
  EXPECT_EQ(2U, result.value().cols);
  EXPECT_EQ(1U, result.value().channels);
  EXPECT_EQ(5U, result.value().acquisitions_read);

  const auto image = read_float32_file(files.image);
  ASSERT_EQ(4U, image.size());
  for (const auto value : image) {
    EXPECT_NEAR(std::sqrt(2.0F), value, 2.0e-4F);
  }
  std::ifstream metadata(files.metadata, std::ios::binary);
  ASSERT_TRUE(metadata.is_open());
  const std::string document{std::istreambuf_iterator<char>{metadata}, std::istreambuf_iterator<char>{}};
  EXPECT_NE(std::string::npos, document.find("\"noise_model_estimate\""));
  EXPECT_NE(std::string::npos, document.find("\"noise_prewhiten\""));
  EXPECT_NE(std::string::npos, document.find("\"phase_correction_estimate\""));
  EXPECT_NE(std::string::npos, document.find("\"phase_correct\""));
  EXPECT_NE(std::string::npos, document.find("\"coil_compression_basis_estimate\""));
  EXPECT_NE(std::string::npos, document.find("\"coil_compress\""));
  EXPECT_NE(std::string::npos, document.find("\"readout_oversampling_remove\""));
  EXPECT_NE(std::string::npos, document.find("\"cartesian_ifft2_coil_images\""));
  EXPECT_NE(std::string::npos, document.find("\"coil_combine_rss\""));
}

TEST(CartesianRssHdf5Reconstruction, RejectsDuplicateKyDuringPreflightBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("duplicate_ky.h5"),
    .image = temporary_path("duplicate_ky.f32"),
    .metadata = temporary_path("duplicate_ky.f32.json"),
  };
  write_duplicate_ky_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
  EXPECT_FALSE(std::filesystem::exists(files.metadata));
}

TEST(CartesianRssHdf5Reconstruction, RejectsMissingKyDuringPreflightBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("missing_ky.h5"),
    .image = temporary_path("missing_ky.f32"),
    .metadata = temporary_path("missing_ky.f32.json"),
  };
  write_missing_ky_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
  EXPECT_FALSE(std::filesystem::exists(files.metadata));
}

TEST(CartesianRssHdf5Reconstruction, RejectsFlaggedAcquisitionDuringPreflightBeforeWritingOutputs) {
  TestFiles files{
    .input = temporary_path("flagged_acquisition.h5"),
    .image = temporary_path("flagged_acquisition.f32"),
    .metadata = temporary_path("flagged_acquisition.f32.json"),
  };
  write_flagged_frame(files.input);

  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(make_config(files));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, result.status().code());
  EXPECT_FALSE(std::filesystem::exists(files.image));
  EXPECT_FALSE(std::filesystem::exists(files.metadata));
}

} // namespace
