#include "kspacejet/image/detail/intel/intel_image_volume.hpp"

#include "intel_image_common.hpp"

namespace ksj::image::detail::intel {

[[nodiscard]] InterpolationResult resize_volume_cubic(ksj::array::CubeView<const ksj::base::cf32> input,
                                                      ksj::array::CubeView<ksj::base::cf32> output,
                                                      ResizeVolumeCubicWorkspace& workspace) {
  if (input.empty() || output.empty()) {
    return {InterpolationStatus::empty_input, 0};
  }

  if (!fits_int(input.dim0()) || !fits_int(input.dim1()) || !fits_int(input.dim2()) || !fits_int(output.dim0()) ||
      !fits_int(output.dim1()) || !fits_int(output.dim2())) {
    return {InterpolationStatus::backend_error, ippStsSizeErr};
  }

  const auto first_value = input(0U, 0U, 0U);
  bool is_constant = true;
  for (std::size_t i2 = 0U; i2 < input.dim2() && is_constant; ++i2) {
    for (std::size_t i0 = 0U; i0 < input.dim0() && is_constant; ++i0) {
      for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
        if (input(i0, i1, i2) != first_value) {
          is_constant = false;
          break;
        }
      }
    }
  }
  if (is_constant) {
    ksj::array::fill(output, first_value);
    return {InterpolationStatus::success, 0};
  }

  const auto input_plane_size = input.dim0() * input.dim1();
  const auto output_plane_size = output.dim0() * output.dim1();
  workspace.real_input.resize(input.size());
  workspace.imag_input.resize(input.size());
  workspace.real_output.resize(output.size());
  workspace.imag_output.resize(output.size());

  for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
    for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
      for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
        const auto index = i2 * input_plane_size + i0 * input.dim1() + i1;
        const auto value = input(i0, i1, i2);
        workspace.real_input(index) = value.real();
        workspace.imag_input(index) = value.imag();
      }
    }
  }

  const IpprVolume source_volume = {static_cast<int>(input.dim1()), static_cast<int>(input.dim0()),
                                    static_cast<int>(input.dim2())};
  const IpprCuboid source_roi = {
    0, 0, 0, static_cast<int>(input.dim1()), static_cast<int>(input.dim0()), static_cast<int>(input.dim2())};
  const auto source_step = static_cast<int>(input.dim1() * sizeof(float));
  const auto source_plane_step = static_cast<int>(input_plane_size * sizeof(float));
  const auto destination_step = static_cast<int>(output.dim1() * sizeof(float));
  const auto destination_plane_step = static_cast<int>(output_plane_size * sizeof(float));
  const auto x_factor = static_cast<double>(output.dim1()) / static_cast<double>(input.dim1());
  const auto y_factor = static_cast<double>(output.dim0()) / static_cast<double>(input.dim0());
  const auto z_factor = static_cast<double>(output.dim2()) / static_cast<double>(input.dim2());
  constexpr double x_shift = 0.0;
  constexpr double y_shift = 0.0;
  constexpr double z_shift = 0.0;
  constexpr int interpolation = IPPI_INTER_CUBIC2P_BSPLINE;
  IpprCuboid destination_roi = {0, 0, 0, 0, 0, 0};
  ipprGetResizeCuboid(source_roi, &destination_roi, x_factor, y_factor, z_factor, x_shift, y_shift, z_shift,
                      interpolation);

  int buffer_size = 0;
  auto status = ipprResizeGetBufSize(source_roi, destination_roi, 1, interpolation, &buffer_size);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }

  Ipp8u* work_buffer_data = nullptr;
  if (buffer_size > 0) {
    workspace.work_buffer.resize(static_cast<std::size_t>(buffer_size));
    work_buffer_data = workspace.work_buffer.data();
  }

  status = ipprResize_32f_C1V(workspace.real_input.data(), source_volume, source_step, source_plane_step, source_roi,
                              workspace.real_output.data(), destination_step, destination_plane_step, destination_roi,
                              x_factor, y_factor, z_factor, x_shift, y_shift, z_shift, interpolation, work_buffer_data);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }

  status = ipprResize_32f_C1V(workspace.imag_input.data(), source_volume, source_step, source_plane_step, source_roi,
                              workspace.imag_output.data(), destination_step, destination_plane_step, destination_roi,
                              x_factor, y_factor, z_factor, x_shift, y_shift, z_shift, interpolation, work_buffer_data);
  if (status != ippStsNoErr) {
    return {InterpolationStatus::backend_error, status};
  }

  for (std::size_t i2 = 0U; i2 < output.dim2(); ++i2) {
    for (std::size_t i0 = 0U; i0 < output.dim0(); ++i0) {
      for (std::size_t i1 = 0U; i1 < output.dim1(); ++i1) {
        const auto index = i2 * output_plane_size + i0 * output.dim1() + i1;
        output(i0, i1, i2) = ksj::base::cf32(workspace.real_output(index), workspace.imag_output(index));
      }
    }
  }

  return {InterpolationStatus::success, 0};
}

[[nodiscard]] InterpolationResult resize_volume_cubic(ksj::array::CubeView<const ksj::base::cf32> input,
                                                      ksj::array::CubeView<ksj::base::cf32> output) {
  ResizeVolumeCubicWorkspace workspace;
  return resize_volume_cubic(input, output, workspace);
}

} // namespace ksj::image::detail::intel
