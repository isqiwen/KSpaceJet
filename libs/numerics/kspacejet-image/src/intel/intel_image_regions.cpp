#include "kspacejet/image/detail/intel/intel_image_regions.hpp"

#include "intel_image_common.hpp"

namespace ksj::image::detail::intel {

[[nodiscard]] bool pad(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                       const std::size_t top, const std::size_t bottom, const std::size_t left, const std::size_t right,
                       const BorderMode mode) {
  if (input.empty() || output.rows() != input.rows() + top + bottom || output.cols() != input.cols() + left + right ||
      !fits_int(input.rows()) || !fits_int(input.cols()) || !fits_int(output.rows()) || !fits_int(output.cols()) ||
      !fits_int(input.row_stride_bytes()) || !fits_int(output.row_stride_bytes()) || !fits_int(top) ||
      !fits_int(left)) {
    return false;
  }

  const auto source_size = image_size(input.rows(), input.cols());
  const auto destination_size = image_size(output.rows(), output.cols());
  IppStatus status = ippStsNoErr;
  switch (mode) {
    case BorderMode::replicate:
      status = ippiCopyReplicateBorder_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()), source_size,
                                               output.data(), static_cast<int>(output.row_stride_bytes()),
                                               destination_size, static_cast<int>(top), static_cast<int>(left));
      return status == ippStsNoErr;
    case BorderMode::reflect:
    case BorderMode::reflect101:
      return false;
    case BorderMode::constant:
      status = ippiCopyConstBorder_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()), source_size,
                                           output.data(), static_cast<int>(output.row_stride_bytes()), destination_size,
                                           static_cast<int>(top), static_cast<int>(left), 0.0F);
      return status == ippStsNoErr;
  }
  return false;
}

} // namespace ksj::image::detail::intel
