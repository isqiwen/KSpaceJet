#include "kspacejet/nufft/detail/eigen/eigen_radial_gridding.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace ksj::nufft::detail::eigen {
namespace {

struct ByteRange {
  std::uintptr_t begin{};
  std::uintptr_t end{};
  bool empty{true};
};

[[nodiscard]] std::size_t checked_add(const std::size_t lhs, const std::size_t rhs, const char* const description) {
  if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
    throw std::length_error(description);
  }
  return lhs + rhs;
}

[[nodiscard]] std::size_t checked_multiply(const std::size_t lhs, const std::size_t rhs,
                                           const char* const description) {
  if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    throw std::length_error(description);
  }
  return lhs * rhs;
}

template <typename T>
[[nodiscard]] ByteRange byte_range(const T* const data, const std::size_t last_element_offset,
                                   const char* const description) {
  if (data == nullptr) {
    throw std::invalid_argument(description);
  }

  const auto element_count = checked_add(last_element_offset, 1U, "radial gridding view element count overflows");
  const auto byte_count = checked_multiply(element_count, sizeof(T), "radial gridding view byte count overflows");
  const auto begin = reinterpret_cast<std::uintptr_t>(data);
  if (byte_count > std::numeric_limits<std::uintptr_t>::max() - begin) {
    throw std::length_error("radial gridding view address range overflows");
  }
  return {begin, begin + static_cast<std::uintptr_t>(byte_count), false};
}

template <typename T>
[[nodiscard]] ByteRange byte_range(const ksj::array::VectorView<T> view, const char* const description) {
  if (view.empty()) {
    return {};
  }
  return byte_range(view.data(),
                    checked_multiply(view.size() - 1U, view.stride(), "radial gridding vector offset overflows"),
                    description);
}

template <typename T>
[[nodiscard]] ByteRange byte_range(const ksj::array::MatrixView<T> view, const char* const description) {
  if (view.empty()) {
    return {};
  }
  const auto row_offset =
    checked_multiply(view.rows() - 1U, view.row_stride(), "radial gridding matrix row offset overflows");
  const auto col_offset =
    checked_multiply(view.cols() - 1U, view.col_stride(), "radial gridding matrix column offset overflows");
  return byte_range(view.data(), checked_add(row_offset, col_offset, "radial gridding matrix offset overflows"),
                    description);
}

[[nodiscard]] bool overlaps(const ByteRange lhs, const ByteRange rhs) noexcept {
  return !lhs.empty && !rhs.empty && lhs.begin < rhs.end && rhs.begin < lhs.end;
}

void reject_overlap(const ByteRange lhs, const ByteRange rhs, const char* const description) {
  if (overlaps(lhs, rhs)) {
    throw std::invalid_argument(description);
  }
}

template <typename T> void validate_grid(const Grid2D grid) {
  if (grid.rows == 0U || grid.cols == 0U) {
    throw std::invalid_argument("radial gridding grid must be non-empty");
  }
  (void)checked_multiply(grid.rows, grid.cols, "radial gridding grid element count overflows");
  if (!std::isfinite(grid.row_origin) || !std::isfinite(grid.col_origin)) {
    throw std::invalid_argument("radial gridding grid origins must be finite");
  }
  if (!std::isfinite(static_cast<T>(grid.row_origin)) || !std::isfinite(static_cast<T>(grid.col_origin))) {
    throw std::invalid_argument("radial gridding grid origins exceed scalar range");
  }
  if (static_cast<long double>(grid.rows) > static_cast<long double>(std::numeric_limits<T>::max()) ||
      static_cast<long double>(grid.cols) > static_cast<long double>(std::numeric_limits<T>::max())) {
    throw std::length_error("radial gridding grid dimensions exceed scalar range");
  }
}

template <typename T> void validate_trajectory(const ksj::array::MatrixView<const T> trajectory_radians_per_pixel) {
  if (trajectory_radians_per_pixel.cols() != 2U) {
    throw std::invalid_argument("radial gridding 2D trajectory must have two columns");
  }

  constexpr auto kPi = std::numbers::pi_v<T>;
  for (std::size_t sample = 0U; sample < trajectory_radians_per_pixel.rows(); ++sample) {
    const auto row_frequency = trajectory_radians_per_pixel(sample, 0U);
    const auto col_frequency = trajectory_radians_per_pixel(sample, 1U);
    if (!std::isfinite(row_frequency) || !std::isfinite(col_frequency)) {
      throw std::invalid_argument("radial gridding trajectory must be finite");
    }
    if (row_frequency < -kPi || row_frequency > kPi || col_frequency < -kPi || col_frequency > kPi) {
      throw std::invalid_argument("radial gridding trajectory is outside [-pi, pi] radians per pixel");
    }
  }
}

template <typename T> void validate_density_compensation(const ksj::array::VectorView<const T> density_compensation) {
  for (std::size_t sample = 0U; sample < density_compensation.size(); ++sample) {
    if (!std::isfinite(density_compensation(sample)) || density_compensation(sample) < T{}) {
      throw std::invalid_argument("radial gridding density compensation must be finite and non-negative");
    }
  }
}

template <typename T> void validate_samples(const ksj::array::VectorView<const std::complex<T>> samples) {
  for (std::size_t sample = 0U; sample < samples.size(); ++sample) {
    if (!std::isfinite(samples(sample).real()) || !std::isfinite(samples(sample).imag())) {
      throw std::invalid_argument("radial gridding samples must be finite");
    }
  }
}

// This transform deliberately owns no plan object.  Eigen::FFT caches plans
// on the heap, which is appropriate for the general convenience API but
// would make a Provider callback allocate storage outside its firing lease.
// A radix-2 transform therefore derives every twiddle from scalar state and
// uses only the supplied line buffers.  The non-power-of-two fallback keeps
// the public primitive total for small development grids without creating a
// hidden allocator dependency; the bounded Provider envelope accepts only
// power-of-two image axes and therefore uses the fast radix-2 path.
template <typename T>
void inverse_direct_transform(const ksj::array::VectorView<const std::complex<T>> source,
                              const ksj::array::VectorView<std::complex<T>> destination, const std::size_t length) {
  const auto length_as_scalar = static_cast<T>(length);
  for (std::size_t output = 0U; output < length; ++output) {
    const auto angle = (T{2} * std::numbers::pi_v<T> * static_cast<T>(output)) / length_as_scalar;
    const std::complex<T> phase_step{std::cos(angle), std::sin(angle)};
    std::complex<T> phase{T{1}, T{0}};
    std::complex<T> sum{};
    for (std::size_t input = 0U; input < length; ++input) {
      sum += source(input) * phase;
      phase *= phase_step;
    }
    destination(output) = sum;
  }
}

[[nodiscard]] std::size_t reversed_radix2_index(const std::size_t index, const std::size_t length) noexcept {
  std::size_t reversed = 0U;
  auto source = index;
  for (auto remaining = length; remaining > 1U; remaining >>= 1U) {
    reversed = (reversed << 1U) | (source & 1U);
    source >>= 1U;
  }
  return reversed;
}

template <typename T>
void inverse_radix2_transform(const ksj::array::VectorView<const std::complex<T>> source,
                              const ksj::array::VectorView<std::complex<T>> destination, const std::size_t length) {
  if (length == 1U) {
    destination(0U) = source(0U);
    return;
  }

  for (std::size_t input = 0U; input < length; ++input) {
    destination(reversed_radix2_index(input, length)) = source(input);
  }
  for (std::size_t stage_length = 2U;; stage_length <<= 1U) {
    const auto angle = (T{2} * std::numbers::pi_v<T>) / static_cast<T>(stage_length);
    const std::complex<T> stage_step{std::cos(angle), std::sin(angle)};
    const auto half_stage = stage_length / 2U;
    for (std::size_t block = 0U; block < length; block += stage_length) {
      std::complex<T> phase{T{1}, T{0}};
      for (std::size_t offset = 0U; offset < half_stage; ++offset) {
        const auto even = destination(block + offset);
        const auto odd = destination(block + offset + half_stage) * phase;
        destination(block + offset) = even + odd;
        destination(block + offset + half_stage) = even - odd;
        phase *= stage_step;
      }
    }
    if (stage_length == length) {
      return;
    }
  }
}

template <typename T>
void inverse_transform(const ksj::array::VectorView<const std::complex<T>> source,
                       const ksj::array::VectorView<std::complex<T>> destination, const std::size_t length) {
  if (std::has_single_bit(length)) {
    inverse_radix2_transform(source, destination, length);
    return;
  }
  inverse_direct_transform(source, destination, length);
}

template <typename T>
void validate_workspace(const Grid2D grid, const ksj::array::MatrixView<std::complex<T>> image,
                        const RadialGridding2Workspace<T> workspace) {
  if (image.rows() != grid.rows || image.cols() != grid.cols) {
    throw std::invalid_argument("radial gridding image dimensions do not match the grid");
  }
  if (workspace.fft_intermediate.rows() != grid.rows || workspace.fft_intermediate.cols() != grid.cols) {
    throw std::invalid_argument("radial gridding FFT intermediate dimensions do not match the grid");
  }
  const auto line_extent = std::max(grid.rows, grid.cols);
  if (workspace.fft_source.size() < line_extent || workspace.fft_destination.size() < line_extent) {
    throw std::invalid_argument("radial gridding FFT line workspace is too small");
  }
  if (!image.is_contiguous() || !workspace.fft_intermediate.is_contiguous() || !workspace.fft_source.is_contiguous() ||
      !workspace.fft_destination.is_contiguous()) {
    throw std::invalid_argument("radial gridding writable storage must be contiguous");
  }
}

template <typename T>
void validate_writable_storage(const ksj::array::VectorView<const std::complex<T>> samples,
                               const ksj::array::MatrixView<const T> trajectory_radians_per_pixel,
                               const ksj::array::VectorView<const T> density_compensation,
                               const ksj::array::MatrixView<std::complex<T>> image,
                               const RadialGridding2Workspace<T> workspace) {
  const auto samples_range = byte_range(samples, "radial gridding samples data is null");
  const auto trajectory_range = byte_range(trajectory_radians_per_pixel, "radial gridding trajectory data is null");
  const auto density_range = byte_range(density_compensation, "radial gridding density compensation data is null");
  const auto image_range = byte_range(image, "radial gridding image data is null");
  const auto intermediate_range =
    byte_range(workspace.fft_intermediate, "radial gridding FFT intermediate data is null");
  const auto source_range = byte_range(workspace.fft_source, "radial gridding FFT source data is null");
  const auto destination_range = byte_range(workspace.fft_destination, "radial gridding FFT destination data is null");

  reject_overlap(image_range, intermediate_range, "radial gridding image and FFT intermediate must not alias");
  reject_overlap(image_range, source_range, "radial gridding image and FFT source must not alias");
  reject_overlap(image_range, destination_range, "radial gridding image and FFT destination must not alias");
  reject_overlap(intermediate_range, source_range, "radial gridding FFT workspaces must not alias");
  reject_overlap(intermediate_range, destination_range, "radial gridding FFT workspaces must not alias");
  reject_overlap(source_range, destination_range, "radial gridding FFT workspaces must not alias");

  for (const auto writable_range : {image_range, intermediate_range, source_range, destination_range}) {
    reject_overlap(writable_range, samples_range, "radial gridding writable storage must not alias samples");
    reject_overlap(writable_range, trajectory_range, "radial gridding writable storage must not alias trajectory");
    reject_overlap(writable_range, density_range,
                   "radial gridding writable storage must not alias density compensation");
  }
}

template <typename T>
[[nodiscard]] std::pair<std::size_t, T> grid_coordinate(const T frequency, const std::size_t extent) {
  const auto extent_as_scalar = static_cast<T>(extent);
  auto coordinate = frequency * extent_as_scalar / (T{2} * std::numbers::pi_v<T>);
  if (coordinate < T{}) {
    coordinate += extent_as_scalar;
  }
  if (coordinate >= extent_as_scalar) {
    coordinate -= extent_as_scalar;
  }
  if (!std::isfinite(coordinate) || coordinate < T{} || coordinate >= extent_as_scalar) {
    throw std::invalid_argument("radial gridding trajectory cannot be mapped to the Cartesian grid");
  }
  const auto lower = static_cast<std::size_t>(std::floor(coordinate));
  return {lower, coordinate - static_cast<T>(lower)};
}

template <typename T>
void radial_analytic_ramp_dcf2_impl(const ksj::array::MatrixView<const T> trajectory_radians_per_pixel,
                                    const ksj::array::VectorView<T> density_compensation) {
  if (density_compensation.size() != trajectory_radians_per_pixel.rows()) {
    throw std::invalid_argument("radial ramp DCF size must match trajectory sample count");
  }
  validate_trajectory(trajectory_radians_per_pixel);

  const auto trajectory_range = byte_range(trajectory_radians_per_pixel, "radial ramp trajectory data is null");
  const auto density_range = byte_range(density_compensation, "radial ramp DCF output data is null");
  reject_overlap(trajectory_range, density_range, "radial ramp DCF output must not alias trajectory");

  for (std::size_t sample = 0U; sample < trajectory_radians_per_pixel.rows(); ++sample) {
    density_compensation(sample) =
      std::hypot(trajectory_radians_per_pixel(sample, 0U), trajectory_radians_per_pixel(sample, 1U));
  }
}

template <typename T>
void radial_linear_gridding2_adjoint_impl(const Grid2D grid,
                                          const ksj::array::VectorView<const std::complex<T>> samples,
                                          const ksj::array::MatrixView<const T> trajectory_radians_per_pixel,
                                          const ksj::array::VectorView<const T> density_compensation,
                                          const ksj::array::MatrixView<std::complex<T>> image,
                                          const RadialGridding2Workspace<T> workspace) {
  validate_grid<T>(grid);
  if (samples.size() != trajectory_radians_per_pixel.rows() || samples.size() != density_compensation.size()) {
    throw std::invalid_argument("radial gridding sample, trajectory, and DCF counts must match");
  }
  validate_trajectory(trajectory_radians_per_pixel);
  validate_samples(samples);
  validate_density_compensation(density_compensation);
  validate_workspace(grid, image, workspace);
  validate_writable_storage(samples, trajectory_radians_per_pixel, density_compensation, image, workspace);

  for (std::size_t row = 0U; row < grid.rows; ++row) {
    for (std::size_t col = 0U; col < grid.cols; ++col) {
      image(row, col) = {};
    }
  }

  const auto row_origin = static_cast<T>(grid.row_origin);
  const auto col_origin = static_cast<T>(grid.col_origin);
  for (std::size_t sample = 0U; sample < samples.size(); ++sample) {
    const auto row_frequency = trajectory_radians_per_pixel(sample, 0U);
    const auto col_frequency = trajectory_radians_per_pixel(sample, 1U);
    const auto [row_lower, row_fraction] = grid_coordinate(row_frequency, grid.rows);
    const auto [col_lower, col_fraction] = grid_coordinate(col_frequency, grid.cols);
    const auto row_upper = (row_lower + 1U) % grid.rows;
    const auto col_upper = (col_lower + 1U) % grid.cols;

    const auto phase = -(row_frequency * row_origin + col_frequency * col_origin);
    if (!std::isfinite(phase)) {
      throw std::invalid_argument("radial gridding origin phase must be finite");
    }
    const std::complex<T> origin_phase{std::cos(phase), std::sin(phase)};
    const auto weighted_sample = samples(sample) * density_compensation(sample) * origin_phase;
    const auto row_lower_weight = T{1} - row_fraction;
    const auto col_lower_weight = T{1} - col_fraction;

    image(row_lower, col_lower) += weighted_sample * (row_lower_weight * col_lower_weight);
    image(row_lower, col_upper) += weighted_sample * (row_lower_weight * col_fraction);
    image(row_upper, col_lower) += weighted_sample * (row_fraction * col_lower_weight);
    image(row_upper, col_upper) += weighted_sample * (row_fraction * col_fraction);
  }

  for (std::size_t row = 0U; row < grid.rows; ++row) {
    for (std::size_t col = 0U; col < grid.cols; ++col) {
      workspace.fft_source(col) = image(row, col);
    }
    inverse_transform(ksj::array::as_const_view(workspace.fft_source), workspace.fft_destination, grid.cols);
    for (std::size_t col = 0U; col < grid.cols; ++col) {
      workspace.fft_intermediate(row, col) = workspace.fft_destination(col);
    }
  }
  for (std::size_t col = 0U; col < grid.cols; ++col) {
    for (std::size_t row = 0U; row < grid.rows; ++row) {
      workspace.fft_source(row) = workspace.fft_intermediate(row, col);
    }
    inverse_transform(ksj::array::as_const_view(workspace.fft_source), workspace.fft_destination, grid.rows);
    for (std::size_t row = 0U; row < grid.rows; ++row) {
      image(row, col) = workspace.fft_destination(row);
    }
  }
}

} // namespace

void radial_analytic_ramp_dcf2(const ksj::array::MatrixView<const float> trajectory_radians_per_pixel,
                               const ksj::array::VectorView<float> density_compensation) {
  radial_analytic_ramp_dcf2_impl(trajectory_radians_per_pixel, density_compensation);
}

void radial_analytic_ramp_dcf2(const ksj::array::MatrixView<const double> trajectory_radians_per_pixel,
                               const ksj::array::VectorView<double> density_compensation) {
  radial_analytic_ramp_dcf2_impl(trajectory_radians_per_pixel, density_compensation);
}

void radial_linear_gridding2_adjoint(const Grid2D grid, const ksj::array::VectorView<const ksj::base::cf32> samples,
                                     const ksj::array::MatrixView<const float> trajectory_radians_per_pixel,
                                     const ksj::array::VectorView<const float> density_compensation,
                                     const ksj::array::MatrixView<ksj::base::cf32> image,
                                     const RadialGridding2Workspace<float> workspace) {
  radial_linear_gridding2_adjoint_impl(grid, samples, trajectory_radians_per_pixel, density_compensation, image,
                                       workspace);
}

void radial_linear_gridding2_adjoint(const Grid2D grid, const ksj::array::VectorView<const ksj::base::cf64> samples,
                                     const ksj::array::MatrixView<const double> trajectory_radians_per_pixel,
                                     const ksj::array::VectorView<const double> density_compensation,
                                     const ksj::array::MatrixView<ksj::base::cf64> image,
                                     const RadialGridding2Workspace<double> workspace) {
  radial_linear_gridding2_adjoint_impl(grid, samples, trajectory_radians_per_pixel, density_compensation, image,
                                       workspace);
}

} // namespace ksj::nufft::detail::eigen
