#include "kspacejet/image/detail/intel/intel_image_filters.hpp"

#include "intel_image_common.hpp"

#include <ipp/ippcv.h>

namespace ksj::image::detail::intel {
namespace {

[[nodiscard]] bool valid_filter_pair(ksj::array::ImageView<const float> input,
                                     ksj::array::ImageView<float> output) noexcept {
  return input.rows() == output.rows() && input.cols() == output.cols() && !input.empty() &&
         fits_int(input.row_stride_bytes()) && fits_int(output.row_stride_bytes()) && fits_int(input.rows()) &&
         fits_int(input.cols());
}

[[nodiscard]] IppiMaskSize mask_3x3() noexcept {
  return ippMskSize3x3;
}

[[nodiscard]] bool allocate_buffer(const int buffer_size, ksj::array::PooledVector<Ipp8u>& buffer,
                                   Ipp8u*& buffer_data) {
  if (buffer_size < 0) {
    return false;
  }
  if (buffer_size == 0) {
    buffer_data = nullptr;
    return true;
  }
  buffer = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(buffer_size));
  buffer_data = buffer.data();
  return true;
}

} // namespace

[[nodiscard]] bool gaussian_blur(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                 const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  if (kernel_size == 0U || kernel_size % 2U == 0U || sigma <= 0.0 || input.rows() != output.rows() ||
      input.cols() != output.cols() || input.empty() || !fits_int(input.row_stride_bytes()) ||
      !fits_int(output.row_stride_bytes()) || !fits_int(input.rows()) || !fits_int(input.cols()) ||
      !fits_int(kernel_size)) {
    return false;
  }

  int specification_size = 0;
  int work_buffer_size = 0;
  auto status =
    ippiFilterGaussianGetBufferSize(image_size(input.rows(), input.cols()), static_cast<Ipp32u>(kernel_size), ipp32f, 1,
                                    &specification_size, &work_buffer_size);
  if (status != ippStsNoErr || specification_size <= 0 || work_buffer_size < 0) {
    return false;
  }

  auto specification = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(specification_size));
  auto work_buffer = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* work_buffer_data = nullptr;
  if (work_buffer_size > 0) {
    work_buffer = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(work_buffer_size));
    work_buffer_data = work_buffer.data();
  }
  auto* specification_data = reinterpret_cast<IppFilterGaussianSpec*>(specification.data());
  status = ippiFilterGaussianInit(image_size(input.rows(), input.cols()), static_cast<Ipp32u>(kernel_size),
                                  static_cast<Ipp32f>(sigma), border_type(border_mode), ipp32f, 1, specification_data,
                                  work_buffer_data);
  if (status != ippStsNoErr) {
    return false;
  }

  status = ippiFilterGaussianBorder_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()), output.data(),
                                            static_cast<int>(output.row_stride_bytes()),
                                            image_size(input.rows(), input.cols()), 0.0F, specification_data,
                                            work_buffer_data);
  return status == ippStsNoErr;
}

[[nodiscard]] bool box_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                              const std::size_t kernel_rows, const std::size_t kernel_cols,
                              const BorderMode border_mode) {
  if (!valid_filter_pair(input, output) || kernel_rows == 0U || kernel_cols == 0U || !fits_int(kernel_rows) ||
      !fits_int(kernel_cols)) {
    return false;
  }

  int work_buffer_size = 0;
  auto status = ippiFilterBoxBorderGetBufferSize(image_size(input.rows(), input.cols()),
                                                 image_size(kernel_rows, kernel_cols), ipp32f, 1, &work_buffer_size);
  if (status != ippStsNoErr) {
    return false;
  }

  auto work_buffer = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* work_buffer_data = nullptr;
  if (!allocate_buffer(work_buffer_size, work_buffer, work_buffer_data)) {
    return false;
  }

  const Ipp32f border_value[1] = {0.0F};
  status = ippiFilterBoxBorder_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()), output.data(),
                                       static_cast<int>(output.row_stride_bytes()),
                                       image_size(input.rows(), input.cols()), image_size(kernel_rows, kernel_cols),
                                       border_type(border_mode), border_value, work_buffer_data);
  return status == ippStsNoErr;
}

[[nodiscard]] bool median_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                 const std::size_t kernel_size, const BorderMode border_mode) {
  if (!valid_filter_pair(input, output) || kernel_size == 0U || kernel_size % 2U == 0U || !fits_int(kernel_size)) {
    return false;
  }

  int work_buffer_size = 0;
  auto status = ippiFilterMedianBorderGetBufferSize(image_size(input.rows(), input.cols()),
                                                    image_size(kernel_size, kernel_size), ipp32f, 1, &work_buffer_size);
  if (status != ippStsNoErr) {
    return false;
  }

  auto work_buffer = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* work_buffer_data = nullptr;
  if (!allocate_buffer(work_buffer_size, work_buffer, work_buffer_data)) {
    return false;
  }

  status = ippiFilterMedianBorder_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()), output.data(),
                                          static_cast<int>(output.row_stride_bytes()),
                                          image_size(input.rows(), input.cols()), image_size(kernel_size, kernel_size),
                                          border_type(border_mode), 0.0F, work_buffer_data);
  return status == ippStsNoErr;
}

[[nodiscard]] bool sobel_x(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                           const BorderMode border_mode) {
  if (!valid_filter_pair(input, output)) {
    return false;
  }

  int work_buffer_size = 0;
  auto status = ippiFilterSobelVertBorderGetBufferSize(image_size(input.rows(), input.cols()), mask_3x3(), ipp32f,
                                                       ipp32f, 1, &work_buffer_size);
  if (status != ippStsNoErr) {
    return false;
  }

  auto work_buffer = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* work_buffer_data = nullptr;
  if (!allocate_buffer(work_buffer_size, work_buffer, work_buffer_data)) {
    return false;
  }

  status = ippiFilterSobelVertBorder_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()), output.data(),
                                             static_cast<int>(output.row_stride_bytes()),
                                             image_size(input.rows(), input.cols()), mask_3x3(),
                                             border_type(border_mode), 0.0F, work_buffer_data);
  if (status != ippStsNoErr) {
    return false;
  }
  return ippiMulC_32f_C1IR(-1.0F, output.data(), static_cast<int>(output.row_stride_bytes()),
                           image_size(output.rows(), output.cols())) == ippStsNoErr;
}

[[nodiscard]] bool sobel_y(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                           const BorderMode border_mode) {
  if (!valid_filter_pair(input, output)) {
    return false;
  }

  int work_buffer_size = 0;
  auto status = ippiFilterSobelHorizBorderGetBufferSize(image_size(input.rows(), input.cols()), mask_3x3(), ipp32f,
                                                        ipp32f, 1, &work_buffer_size);
  if (status != ippStsNoErr) {
    return false;
  }

  auto work_buffer = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* work_buffer_data = nullptr;
  if (!allocate_buffer(work_buffer_size, work_buffer, work_buffer_data)) {
    return false;
  }

  status = ippiFilterSobelHorizBorder_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()), output.data(),
                                              static_cast<int>(output.row_stride_bytes()),
                                              image_size(input.rows(), input.cols()), mask_3x3(),
                                              border_type(border_mode), 0.0F, work_buffer_data);
  return status == ippStsNoErr;
}

[[nodiscard]] bool filter2d_region(ksj::array::ImageView<const float> input, ksj::array::ImageView<const float> kernel,
                                   ksj::array::ImageView<float> output, const ksj::array::detail::NormalizedSlice rows,
                                   const ksj::array::detail::NormalizedSlice cols, const FilterAnchor anchor) {
  if (input.empty() || kernel.empty() || output.empty() || rows.count == 0 || cols.count == 0 || rows.step != 1U ||
      cols.step != 1U || rows.start > input.rows() || cols.start > input.cols() ||
      rows.count > input.rows() - rows.start || cols.count > input.cols() - cols.start || rows.start > output.rows() ||
      cols.start > output.cols() || rows.count > output.rows() - rows.start ||
      cols.count > output.cols() - cols.start || !fits_int(input.row_stride_bytes()) ||
      !fits_int(output.row_stride_bytes()) || !fits_int(rows.count) || !fits_int(cols.count) ||
      !fits_int(kernel.rows()) || !fits_int(kernel.cols())) {
    return false;
  }

  if (anchor != FilterAnchor::center) {
    return false;
  }

  const auto anchor_row = kernel.rows() / 2U;
  const auto anchor_col = kernel.cols() / 2U;
  const auto row_after = kernel.rows() - anchor_row - 1U;
  const auto col_after = kernel.cols() - anchor_col - 1U;
  if (rows.start < anchor_row || cols.start < anchor_col || rows.start + rows.count + row_after > input.rows() ||
      cols.start + cols.count + col_after > input.cols()) {
    return false;
  }

  const auto* source = input.data() + rows.start * input.row_stride() + cols.start;
  auto* destination = output.data() + rows.start * output.row_stride() + cols.start;

  int specification_size = 0;
  int work_buffer_size = 0;
  auto status = ippiFilterBorderGetSize(image_size(kernel.rows(), kernel.cols()), image_size(rows.count, cols.count),
                                        ipp32f, ipp32f, 1, &specification_size, &work_buffer_size);
  if (status != ippStsNoErr || specification_size <= 0 || work_buffer_size < 0) {
    return false;
  }

  auto specification = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(specification_size));
  auto* specification_data = reinterpret_cast<IppiFilterBorderSpec*>(specification.data());
  status = ippiFilterBorderInit_32f(kernel.data(), image_size(kernel.rows(), kernel.cols()), ipp32f, 1, ippRndNear,
                                    specification_data);
  if (status != ippStsNoErr) {
    return false;
  }

  auto work_buffer = ksj::array::PooledVector<Ipp8u>{};
  Ipp8u* work_buffer_data = nullptr;
  if (work_buffer_size > 0) {
    work_buffer = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(work_buffer_size));
    work_buffer_data = work_buffer.data();
  }

  const Ipp32f border_value[1] = {0.0F};
  status = ippiFilterBorder_32f_C1R(source, static_cast<int>(input.row_stride_bytes()), destination,
                                    static_cast<int>(output.row_stride_bytes()), image_size(rows.count, cols.count),
                                    ippBorderInMem, border_value, specification_data, work_buffer_data);
  return status == ippStsNoErr;
}

} // namespace ksj::image::detail::intel
