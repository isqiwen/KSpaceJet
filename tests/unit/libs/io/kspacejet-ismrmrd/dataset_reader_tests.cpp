#include "kspacejet/ismrmrd/dataset_reader.hpp"

#include <ismrmrd/dataset.h>

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path make_test_dataset_path() {
  const auto directory = std::filesystem::temp_directory_path() / "ksj_ismrmrd_tests";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  const auto path = directory / "reader_round_trip.h5";
  std::filesystem::remove(path, error);
  return path;
}

void write_test_dataset(const std::filesystem::path& path) {
  {
    const auto filename = path.string();
    ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
    dataset.writeHeader("<ismrmrdHeader xmlns=\"http://www.ismrm.org/ISMRMRD\"><experimentalConditions>"
                        "<H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz>"
                        "</experimentalConditions></ismrmrdHeader>");

    ISMRMRD::Acquisition acquisition(3, 2, 2);
    acquisition.measurement_uid() = 17U;
    acquisition.scan_counter() = 23U;
    acquisition.acquisition_time_stamp() = 29U;
    acquisition.idx().slice = 3U;
    acquisition.idx().repetition = 5U;
    acquisition.discard_pre() = 1U;
    acquisition.center_sample() = 2U;
    acquisition.sample_time_us() = 4.5F;
    for (std::uint16_t channel = 0; channel < acquisition.active_channels(); ++channel) {
      for (std::uint16_t sample = 0; sample < acquisition.number_of_samples(); ++sample) {
        const auto value = static_cast<float>(sample + channel * acquisition.number_of_samples());
        acquisition.data(sample, channel) = {value, 100.0F + value};
      }
    }
    for (std::uint16_t sample = 0; sample < acquisition.number_of_samples(); ++sample) {
      acquisition.traj(0, sample) = static_cast<float>(sample);
      acquisition.traj(1, sample) = static_cast<float>(10U + sample);
    }
    dataset.appendAcquisition(acquisition);
  }
}

} // namespace

TEST(KSpaceJetIsmrmrd, StreamsStandardAcquisitionDataWithoutExposingUpstreamTypes) {
  const auto path = make_test_dataset_path();
  write_test_dataset(path);

  ksj::ismrmrd::DatasetReader reader;
  std::string error;
  ASSERT_TRUE(reader.open(path, "dataset", error)) << error;
  ASSERT_TRUE(reader.is_open());
  EXPECT_EQ(reader.metadata().group, "dataset");
  EXPECT_EQ(reader.metadata().acquisition_count, 1U);
  EXPECT_FALSE(reader.metadata().xml_header.empty());

  std::vector<ksj::ismrmrd::AcquisitionHeader> headers;
  std::vector<std::complex<float>> samples;
  std::vector<float> trajectory;
  EXPECT_EQ(reader.for_each_acquisition(
              [&](const ksj::ismrmrd::AcquisitionView& acquisition) {
                headers.push_back(acquisition.header);
                samples.assign(acquisition.samples.begin(), acquisition.samples.end());
                trajectory.assign(acquisition.trajectory.begin(), acquisition.trajectory.end());
                return true;
              },
              error),
            ksj::ismrmrd::AcquisitionIterationResult::completed)
    << error;

  ASSERT_EQ(headers.size(), 1U);
  EXPECT_EQ(headers.front().measurement_uid, 17U);
  EXPECT_EQ(headers.front().scan_counter, 23U);
  EXPECT_EQ(headers.front().acquisition_time_stamp, 29U);
  EXPECT_EQ(headers.front().index.slice, 3U);
  EXPECT_EQ(headers.front().index.repetition, 5U);
  EXPECT_EQ(headers.front().number_of_samples, 3U);
  EXPECT_EQ(headers.front().active_channels, 2U);
  EXPECT_EQ(headers.front().trajectory_dimensions, 2U);
  EXPECT_FLOAT_EQ(headers.front().sample_time_us, 4.5F);

  ASSERT_EQ(samples.size(), 6U);
  EXPECT_EQ(samples[0], std::complex<float>(0.0F, 100.0F));
  EXPECT_EQ(samples[1], std::complex<float>(1.0F, 101.0F));
  EXPECT_EQ(samples[3], std::complex<float>(3.0F, 103.0F));
  ASSERT_EQ(trajectory.size(), 6U);
  EXPECT_FLOAT_EQ(trajectory[0], 0.0F);
  EXPECT_FLOAT_EQ(trajectory[1], 10.0F);
  EXPECT_FLOAT_EQ(trajectory[5], 12.0F);

  EXPECT_EQ(reader.for_each_acquisition(
              [](const ksj::ismrmrd::AcquisitionView&) {
                return false;
              },
              error),
            ksj::ismrmrd::AcquisitionIterationResult::stopped)
    << error;
}
