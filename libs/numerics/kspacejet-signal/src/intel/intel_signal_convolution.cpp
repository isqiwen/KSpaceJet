#include "kspacejet/signal/detail/intel/intel_signal_convolution.hpp"

#include "intel_signal_common.hpp"

#include <ipp.h>

namespace ksj::signal::detail::intel {
namespace {

template <typename T, IppDataType data_type>
[[nodiscard]] bool convolve_impl(ksj::array::VectorView<const T> signal, ksj::array::VectorView<const T> kernel,
                                 ksj::array::VectorView<T> output,
                                 IppStatus (*convolve)(const T*, int, const T*, int, T*, IppEnum, Ipp8u*)) {
  if (!impl::fits_ipp_length(signal.size()) || !impl::fits_ipp_length(kernel.size()) || !signal.is_contiguous() ||
      !kernel.is_contiguous() || !output.is_contiguous()) {
    return false;
  }

  int buffer_size = 0;
  auto status = ippsConvolveGetBufferSize(static_cast<int>(signal.size()), static_cast<int>(kernel.size()), data_type,
                                          ippAlgAuto, &buffer_size);
  if (!impl::check_status(status) || buffer_size < 0) {
    return false;
  }

  auto buffer = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(buffer_size));
  status = convolve(signal.data(), static_cast<int>(signal.size()), kernel.data(), static_cast<int>(kernel.size()),
                    output.data(), ippAlgAuto, buffer.data());
  return impl::check_status(status);
}

} // namespace

bool convolve(ksj::array::VectorView<const float> signal, ksj::array::VectorView<const float> kernel,
              ksj::array::VectorView<float> output) {
  return convolve_impl<float, ipp32f>(signal, kernel, output, ippsConvolve_32f);
}

bool convolve(ksj::array::VectorView<const double> signal, ksj::array::VectorView<const double> kernel,
              ksj::array::VectorView<double> output) {
  return convolve_impl<double, ipp64f>(signal, kernel, output, ippsConvolve_64f);
}

bool convolve2d_full(ksj::array::MatrixView<const float> input, ksj::array::MatrixView<const float> kernel,
                     ksj::array::MatrixView<float> output) {
  const auto expected_rows = input.empty() || kernel.empty() ? 0U : input.rows() + kernel.rows() - 1U;
  const auto expected_cols = input.empty() || kernel.empty() ? 0U : input.cols() + kernel.cols() - 1U;
  if (input.empty() || kernel.empty() || output.rows() != expected_rows || output.cols() != expected_cols ||
      input.col_stride() != 1U || kernel.col_stride() != 1U || output.col_stride() != 1U ||
      !impl::fits_ipp_size(input.rows(), input.cols()) || !impl::fits_ipp_size(kernel.rows(), kernel.cols()) ||
      !impl::fits_ipp_size(output.rows(), output.cols()) || !impl::fits_ipp_step(input.row_stride_bytes()) ||
      !impl::fits_ipp_step(kernel.row_stride_bytes()) || !impl::fits_ipp_step(output.row_stride_bytes())) {
    return false;
  }

  const IppEnum alg_type = static_cast<int>(ippAlgAuto) | static_cast<int>(ippiROIFull);
  int buffer_size = 0;
  auto status = ippiConvGetBufferSize(impl::ipp_size(input.rows(), input.cols()),
                                      impl::ipp_size(kernel.rows(), kernel.cols()), ipp32f, 1, alg_type, &buffer_size);
  if (!impl::check_status(status) || buffer_size < 0) {
    return false;
  }

  auto buffer = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(buffer_size));
  status = ippiConv_32f_C1R(input.data(), static_cast<int>(input.row_stride_bytes()),
                            impl::ipp_size(input.rows(), input.cols()), kernel.data(),
                            static_cast<int>(kernel.row_stride_bytes()), impl::ipp_size(kernel.rows(), kernel.cols()),
                            output.data(), static_cast<int>(output.row_stride_bytes()), alg_type, buffer.data());
  return impl::check_status(status);
}

bool correlate2d_same(ksj::array::ImageView<const float> input, ksj::array::ImageView<const float> kernel,
                      ksj::array::ImageView<float> output) {
  if (input.empty() || kernel.empty() || output.rows() != input.rows() || output.cols() != input.cols() ||
      kernel.rows() > input.rows() || kernel.cols() > input.cols() ||
      !impl::fits_ipp_size(input.rows(), input.cols()) || !impl::fits_ipp_size(kernel.rows(), kernel.cols())) {
    return false;
  }

  const IppEnum alg_type =
    static_cast<int>(ippAlgAuto) | static_cast<int>(ippiROISame) | static_cast<int>(ippiNormNone);
  int buffer_size = 0;
  auto status = ippiCrossCorrNormGetBufferSize(impl::ipp_size(input.rows(), input.cols()),
                                               impl::ipp_size(kernel.rows(), kernel.cols()), alg_type, &buffer_size);
  if (!impl::check_status(status) || buffer_size < 0) {
    return false;
  }

  auto buffer = ksj::array::make_pooled_vector<Ipp8u>(static_cast<std::size_t>(buffer_size));
  status = ippiCrossCorrNorm_32f_C1R(
    input.data(), static_cast<int>(input.row_stride_bytes()), impl::ipp_size(input.rows(), input.cols()), kernel.data(),
    static_cast<int>(kernel.row_stride_bytes()), impl::ipp_size(kernel.rows(), kernel.cols()), output.data(),
    static_cast<int>(output.row_stride_bytes()), alg_type, buffer.data());
  return impl::check_status(status);
}

} // namespace ksj::signal::detail::intel
