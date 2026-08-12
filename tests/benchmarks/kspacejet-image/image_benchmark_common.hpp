#pragma once

#include "benchmark_common.hpp"
#include "kspacejet/image/image.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_basic.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_thresholds.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_regions.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_components.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_interpolation.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_filters.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_morphology.hpp"
#include "kspacejet/image/detail/intel/intel_image_filters.hpp"
#include "kspacejet/image/detail/intel/intel_image_interpolation.hpp"
#include "kspacejet/image/detail/intel/intel_image_thresholds.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_components.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_filters.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_interpolation.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_morphology.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_resize.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_thresholds.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ksj::benchmarks::image_benchmarks {

inline constexpr std::size_t kMaxNeighborhoodSize = 512;

[[nodiscard]] inline std::string image_benchmark_comparison_group(const std::string_view case_name,
                                                                  const std::string_view backend) {
  const auto family = backend.starts_with("eigen")    ? "eigen"
                      : backend.starts_with("opencv") ? "opencv"
                      : backend.starts_with("intel")  ? "intel"
                                                      : "public";
  return std::string{case_name} + "." + family;
}

inline void print_image_benchmark_row(const std::string_view case_name, const std::string_view backend,
                                      const std::string_view type_name, const std::size_t size,
                                      const ksj::benchmarks::Config& config,
                                      const ksj::benchmarks::Measurement& measurement, const double checksum) {
  const auto comparison_group = image_benchmark_comparison_group(case_name, backend);
  const auto timing_scope = backend == "api" || backend.ends_with("_output") ? std::string_view{"output_reuse"}
                                                                             : std::string_view{"allocating"};
  const auto metadata = backend.starts_with("eigen") ? ksj::benchmarks::reference_row(comparison_group, timing_scope)
                                                     : ksj::benchmarks::candidate_row(comparison_group, timing_scope);
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum, metadata);
}

template <typename T> void fill_component_mask(ksj::array::PooledImage<T>& image) {
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      const auto tile_row = row / 8U;
      const auto tile_col = col / 8U;
      image(row, col) = ((tile_row + tile_col) % 3U == 0U) ? T{1} : T{};
    }
  }
}

template <typename T> void fill_region_grow_input(ksj::array::PooledImage<T>& image) {
  const auto center_row = static_cast<double>(image.rows()) * 0.5;
  const auto center_col = static_cast<double>(image.cols()) * 0.5;
  const auto radius = static_cast<double>(std::min(image.rows(), image.cols())) * 0.28;
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      const auto dy = static_cast<double>(row) - center_row;
      const auto dx = static_cast<double>(col) - center_col;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      image(row, col) = distance <= radius ? static_cast<T>(0.75) : static_cast<T>(0.1);
    }
  }
}

[[nodiscard]] inline double checksum_mask(const ksj::array::PooledImage<ksj::image::RegionGrowMaskValue>& mask) {
  double sum = 0.0;
  for (std::size_t index = 0; index < mask.size(); ++index) {
    sum += static_cast<double>(mask.data()[index]);
  }
  return sum;
}

void run_basic_benchmarks_float(const ksj::benchmarks::Config& config);
void run_basic_benchmarks_double(const ksj::benchmarks::Config& config);

void run_resize_benchmarks_float(const ksj::benchmarks::Config& config);
void run_resize_benchmarks_double(const ksj::benchmarks::Config& config);

void run_filter_benchmarks_float(const ksj::benchmarks::Config& config);
void run_filter_benchmarks_double(const ksj::benchmarks::Config& config);

void run_primitive_benchmarks_float(const ksj::benchmarks::Config& config);
void run_primitive_benchmarks_double(const ksj::benchmarks::Config& config);

void run_policy_benchmarks_float(const ksj::benchmarks::Config& config);
void run_policy_benchmarks_double(const ksj::benchmarks::Config& config);

} // namespace ksj::benchmarks::image_benchmarks
