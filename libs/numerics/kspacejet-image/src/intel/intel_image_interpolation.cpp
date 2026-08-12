#include "kspacejet/image/detail/intel/intel_image_interpolation.hpp"

#include "intel_image_common.hpp"

namespace ksj::image::detail::intel {
namespace {

enum class ResizeKernel {
  nearest,
  linear,
  cubic,
};

[[nodiscard]] IppiInterpolationType interpolation_type(const ResizeKernel kernel) noexcept {
  switch (kernel) {
    case ResizeKernel::nearest:
      return ippNearest;
    case ResizeKernel::linear:
      return ippLinear;
    case ResizeKernel::cubic:
      return ippCubic;
  }
  return ippLinear;
}

[[nodiscard]] bool initialize_resize_spec(const ResizeKernel kernel, const IppiSize source_size,
                                          const IppiSize destination_size, IppiResizeSpec_32f* specification,
                                          Ipp8u* initialization_buffer) {
  switch (kernel) {
    case ResizeKernel::nearest:
      return ippiResizeNearestInit_32f(source_size, destination_size, specification) == ippStsNoErr;
    case ResizeKernel::linear:
      return ippiResizeLinearInit_32f(source_size, destination_size, specification) == ippStsNoErr;
    case ResizeKernel::cubic:
      return ippiResizeCubicInit_32f(source_size, destination_size, 0.0F, 0.75F, specification,
                                     initialization_buffer) == ippStsNoErr;
  }
  return false;
}

[[nodiscard]] bool run_resize(const ResizeKernel kernel, const ksj::array::PooledImage<float>& input,
                              ksj::array::PooledImage<float>& output, IppiResizeSpec_32f* specification,
                              Ipp8u* work_buffer) {
  const IppiPoint destination_offset{0, 0};
  const auto source_step = static_cast<Ipp32s>(input.row_stride_bytes());
  const auto destination_step = static_cast<Ipp32s>(output.row_stride_bytes());
  const auto destination_size = image_size(output.rows(), output.cols());
  switch (kernel) {
    case ResizeKernel::nearest:
      return ippiResizeNearest_32f_C1R(input.data(), source_step, output.data(), destination_step, destination_offset,
                                       destination_size, specification, work_buffer) == ippStsNoErr;
    case ResizeKernel::linear:
      return ippiResizeLinear_32f_C1R(input.data(), source_step, output.data(), destination_step, destination_offset,
                                      destination_size, ippBorderRepl, nullptr, specification,
                                      work_buffer) == ippStsNoErr;
    case ResizeKernel::cubic:
      return ippiResizeCubic_32f_C1R(input.data(), source_step, output.data(), destination_step, destination_offset,
                                     destination_size, ippBorderRepl, nullptr, specification,
                                     work_buffer) == ippStsNoErr;
  }
  return false;
}

[[nodiscard]] bool resize_32f(const ResizeKernel kernel, const ksj::array::PooledImage<float>& input,
                              ksj::array::PooledImage<float>& output) {
  if (input.empty() || output.empty() || !fits_int(input.rows()) || !fits_int(input.cols()) ||
      !fits_int(output.rows()) || !fits_int(output.cols()) || !fits_int(input.row_stride_bytes()) ||
      !fits_int(output.row_stride_bytes())) {
    return false;
  }

  const auto source_size = image_size(input.rows(), input.cols());
  const auto destination_size = image_size(output.rows(), output.cols());
  int specification_size = 0;
  int initialization_size = 0;
  auto status = ippiResizeGetSize_32f(source_size, destination_size, interpolation_type(kernel), 0U,
                                      &specification_size, &initialization_size);
  if (status != ippStsNoErr || specification_size <= 0 || initialization_size < 0) {
    return false;
  }

  auto specification = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(specification_size));
  auto initialization = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* initialization_data = nullptr;
  if (initialization_size > 0) {
    initialization = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(initialization_size));
    initialization_data = initialization.data();
  }
  auto* specification_data = reinterpret_cast<IppiResizeSpec_32f*>(specification.data());
  if (!initialize_resize_spec(kernel, source_size, destination_size, specification_data, initialization_data)) {
    return false;
  }

  int work_buffer_size = 0;
  status = ippiResizeGetBufferSize_32f(specification_data, destination_size, 1U, &work_buffer_size);
  if (status != ippStsNoErr || work_buffer_size < 0) {
    return false;
  }

  auto work_buffer = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* work_buffer_data = nullptr;
  if (work_buffer_size > 0) {
    work_buffer = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(work_buffer_size));
    work_buffer_data = work_buffer.data();
  }

  return run_resize(kernel, input, output, specification_data, work_buffer_data);
}

} // namespace

[[nodiscard]] bool resize_nearest(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output) {
  return resize_32f(ResizeKernel::nearest, input, output);
}

[[nodiscard]] bool resize_linear(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output) {
  return resize_32f(ResizeKernel::linear, input, output);
}

[[nodiscard]] bool resize_cubic(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output) {
  return resize_32f(ResizeKernel::cubic, input, output);
}

[[nodiscard]] InterpolationResult rotate_cubic_bspline_smooth(ksj::array::ImageView<const float> input,
                                                              ksj::array::ImageView<float> output,
                                                              const double angle_degrees) {
  if (input.empty()) {
    return {InterpolationStatus::empty_input, 0};
  }
  if (!valid_float_roi(input, output) || !fits_int(input.cols()) || !std::isfinite(angle_degrees)) {
    return {InterpolationStatus::backend_error, ippStsSizeErr};
  }

  const auto size = roi_size(input);
  double x_shift = 0.0;
  double y_shift = 0.0;
  auto status = ippiGetRotateShift(static_cast<double>(input.cols()) / 2.0, static_cast<double>(input.rows()) / 2.0,
                                   angle_degrees, &x_shift, &y_shift);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }

  double coefficients[2][3]{};
  status = ippiGetRotateTransform(angle_degrees, x_shift, y_shift, coefficients);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }

  int specification_size = 0;
  int initialization_size = 0;
  status = ippiWarpAffineGetSize(size, size, ipp32f, coefficients, ippCubic, ippWarpForward, ippBorderTransp,
                                 &specification_size, &initialization_size);
  if (status != ippStsNoErr || specification_size <= 0 || initialization_size < 0) {
    return {InterpolationStatus::backend_error, status != ippStsNoErr ? status : ippStsSizeErr};
  }

  auto specification = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(specification_size));
  auto initialization = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* initialization_data = nullptr;
  if (initialization_size > 0) {
    initialization = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(initialization_size));
    initialization_data = initialization.data();
  }

  auto* specification_data = reinterpret_cast<IppiWarpSpec*>(specification.data());
  constexpr Ipp64f cubic_b = 1.0;
  constexpr Ipp64f cubic_c = 0.0;
  status = ippiWarpAffineCubicInit(size, size, ipp32f, coefficients, ippWarpForward, 1, cubic_b, cubic_c,
                                   ippBorderTransp, nullptr, 0, specification_data, initialization_data);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }

  int work_buffer_size = 0;
  status = ippiWarpGetBufferSize(specification_data, size, &work_buffer_size);
  if (status != ippStsNoErr || work_buffer_size < 0) {
    return {InterpolationStatus::backend_error, status != ippStsNoErr ? status : ippStsSizeErr};
  }

  auto work_buffer = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* work_buffer_data = nullptr;
  if (work_buffer_size > 0) {
    work_buffer = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(work_buffer_size));
    work_buffer_data = work_buffer.data();
  }

  const auto output_step = static_cast<int>(output.row_stride_bytes());
  status = ippiSet_32f_C1R(0.0F, output.data(), output_step, size);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }

  const IppiPoint destination_offset{0, 0};
  status = ippiWarpAffineCubic_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()), output.data(),
                                       output_step, destination_offset, size, specification_data, work_buffer_data);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }
  return {InterpolationStatus::success, 0};
}

[[nodiscard]] InterpolationResult rotate_cubic_bspline_smooth(ksj::array::ImageView<const ksj::base::cf32> input,
                                                              ksj::array::ImageView<ksj::base::cf32> output,
                                                              const double angle_degrees) {
  if (input.empty()) {
    return {InterpolationStatus::empty_input, 0};
  }
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    return {InterpolationStatus::backend_error, ippStsSizeErr};
  }

  auto real_input = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  auto imaginary_input = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  auto real_output = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  auto imaginary_output = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      real_input(row, col) = input(row, col).real();
      imaginary_input(row, col) = input(row, col).imag();
    }
  }

  auto result =
    rotate_cubic_bspline_smooth(ksj::array::as_const_view(real_input.view()), real_output.view(), angle_degrees);
  if (result.status != InterpolationStatus::success) {
    return result;
  }
  result = rotate_cubic_bspline_smooth(ksj::array::as_const_view(imaginary_input.view()), imaginary_output.view(),
                                       angle_degrees);
  if (result.status != InterpolationStatus::success) {
    return result;
  }

  for (std::size_t row = 0U; row < output.rows(); ++row) {
    for (std::size_t col = 0U; col < output.cols(); ++col) {
      output(row, col) = ksj::base::cf32{real_output(row, col), imaginary_output(row, col)};
    }
  }
  return {InterpolationStatus::success, 0};
}

[[nodiscard]] InterpolationResult warp_affine_cubic_zero_32f(ksj::array::MatrixView<const float> input,
                                                             ksj::array::MatrixView<float> output,
                                                             const double coefficients[2][3]) {
  if (input.rows() != output.rows() || input.cols() != output.cols() || !fits_int(input.rows()) ||
      !fits_int(input.cols()) || !fits_int(input.row_stride_bytes()) || !fits_int(output.row_stride_bytes())) {
    return {InterpolationStatus::backend_error, ippStsSizeErr};
  }

  const auto size = image_size(input.rows(), input.cols());
  int specification_size = 0;
  int initialization_size = 0;
  auto status = ippiWarpAffineGetSize(size, size, ipp32f, coefficients, ippCubic, ippWarpBackward, ippBorderConst,
                                      &specification_size, &initialization_size);
  if (status != ippStsNoErr || specification_size <= 0 || initialization_size < 0) {
    return {InterpolationStatus::backend_error, status != ippStsNoErr ? status : ippStsSizeErr};
  }

  auto specification = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(specification_size));
  auto initialization = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* initialization_data = nullptr;
  if (initialization_size > 0) {
    initialization = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(initialization_size));
    initialization_data = initialization.data();
  }

  auto* specification_data = reinterpret_cast<IppiWarpSpec*>(specification.data());
  constexpr Ipp64f cubic_b = 0.0;
  constexpr Ipp64f cubic_c = 0.75;
  const Ipp64f border_value[1] = {0.0};
  status = ippiWarpAffineCubicInit(size, size, ipp32f, coefficients, ippWarpBackward, 1, cubic_b, cubic_c,
                                   ippBorderConst, border_value, 0, specification_data, initialization_data);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }

  int work_buffer_size = 0;
  status = ippiWarpGetBufferSize(specification_data, size, &work_buffer_size);
  if (status != ippStsNoErr || work_buffer_size < 0) {
    return {InterpolationStatus::backend_error, status != ippStsNoErr ? status : ippStsSizeErr};
  }

  auto work_buffer = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* work_buffer_data = nullptr;
  if (work_buffer_size > 0) {
    work_buffer = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(work_buffer_size));
    work_buffer_data = work_buffer.data();
  }

  const IppiPoint destination_offset{0, 0};
  status = ippiWarpAffineCubic_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()), output.data(),
                                       static_cast<int>(output.row_stride_bytes()), destination_offset, size,
                                       specification_data, work_buffer_data);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }
  return {InterpolationStatus::success, 0};
}

[[nodiscard]] InterpolationResult cubic_interpolate_2d_inplace(ksj::array::MatrixView<ksj::base::cf32> matrix,
                                                               InterpolationAxis axis, float ratio) {
  if (matrix.empty()) {
    return {InterpolationStatus::empty_input, 0};
  }

  if (ratio <= 0.0F) {
    return {InterpolationStatus::backend_error, 0};
  }

  if (ratio == 1.0F) {
    return {InterpolationStatus::success, 0};
  }

  const auto rows = matrix.rows();
  const auto cols = matrix.cols();
  if (rows > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      cols > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return {InterpolationStatus::backend_error, ippStsSizeErr};
  }

  auto source_buffer = ksj::array::make_pooled_matrix<float>(rows, cols);
  auto destination_buffer = ksj::array::make_pooled_matrix<float>(rows, cols);
  auto source = source_buffer.view();
  auto destination = destination_buffer.view();
  ksj::array::transform(matrix, source, [](const auto& value) {
    const auto real = value.real();
    return real < 0.0F ? 0.0F : real;
  });

  const auto scale = static_cast<double>(ratio);
  const auto row_factor = (axis == InterpolationAxis::column) ? scale : 1.0;
  const auto col_factor = (axis == InterpolationAxis::row) ? scale : 1.0;
  const auto row_shift =
    (axis == InterpolationAxis::column) ? ((1.0 - scale) * ((static_cast<double>(rows) + 1.0) / 2.0)) : 0.0;
  const auto col_shift =
    (axis == InterpolationAxis::row) ? ((1.0 - scale) * ((static_cast<double>(cols) + 1.0) / 2.0)) : 0.0;

  const double coefficients[2][3] = {
    {1.0 / col_factor, 0.0, -col_shift / col_factor},
    {0.0, 1.0 / row_factor, -row_shift / row_factor},
  };
  const auto result = warp_affine_cubic_zero_32f(ksj::array::as_const_view(source), destination, coefficients);
  if (result.status != InterpolationStatus::success) {
    return result;
  }

  ksj::array::transform(destination, matrix, [](float value) {
    return ksj::base::cf32(value < 0.0F ? 0.0F : value, 0.0F);
  });
  return {InterpolationStatus::success, 0};
}

} // namespace ksj::image::detail::intel
