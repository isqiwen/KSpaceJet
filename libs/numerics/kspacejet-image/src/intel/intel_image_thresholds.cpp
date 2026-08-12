#include "kspacejet/image/detail/intel/intel_image_thresholds.hpp"

#include "intel_image_common.hpp"

namespace ksj::image::detail::intel {

[[nodiscard]] bool threshold(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                             const float threshold_value, const float low_value, const float high_value) {
  if (!valid_float_roi(input, output)) {
    return false;
  }

  const auto input_step = static_cast<int>(input.row_stride_bytes());
  const auto output_step = static_cast<int>(output.row_stride_bytes());
  const auto roi = roi_size(input);
  if (ippiSet_32f_C1R(low_value, output.data(), output_step, roi) != ippStsNoErr) {
    return false;
  }

  auto mask = ksj::array::make_pooled_image<Ipp8u>(input.rows(), input.cols());
  const auto mask_step = static_cast<int>(input.cols() * sizeof(Ipp8u));
  if (ippiCompareC_32f_C1R(input.data(), input_step, threshold_value, mask.data(), mask_step, roi, ippCmpGreaterEq) !=
      ippStsNoErr) {
    return false;
  }

  return ippiSet_32f_C1MR(high_value, output.data(), output_step, roi, mask.data(), mask_step) == ippStsNoErr;
}

[[nodiscard]] bool normalize_minmax(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output) {
  if (!valid_float_roi(input, output)) {
    return false;
  }

  const auto input_step = static_cast<int>(input.row_stride_bytes());
  const auto output_step = static_cast<int>(output.row_stride_bytes());
  const auto roi = roi_size(input);
  float min_value = 0.0F;
  float max_value = 0.0F;
  if (ippiMinMax_32f_C1R(input.data(), input_step, roi, &min_value, &max_value) != ippStsNoErr) {
    return false;
  }

  const auto range = max_value - min_value;
  if (range == 0.0F) {
    return ippiSet_32f_C1R(0.0F, output.data(), output_step, roi) == ippStsNoErr;
  }

  if (ippiSubC_32f_C1R(input.data(), input_step, min_value, output.data(), output_step, roi) != ippStsNoErr) {
    return false;
  }
  return ippiMulC_32f_C1IR(1.0F / range, output.data(), output_step, roi) == ippStsNoErr;
}

} // namespace ksj::image::detail::intel
