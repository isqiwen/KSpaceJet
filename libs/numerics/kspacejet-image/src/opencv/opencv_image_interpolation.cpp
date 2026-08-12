#include "kspacejet/image/detail/opencv/opencv_image_interpolation.hpp"
#include "kspacejet/base/types.hpp"
#include "opencv_image_common.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::image::detail::opencv_impl {

template <typename T> [[nodiscard]] cv::Mat as_opencv(ksj::array::MatrixView<T> matrix) {
  using value_type = std::remove_const_t<T>;
  return cv::Mat(static_cast<int>(matrix.rows()), static_cast<int>(matrix.cols()), cv_type<value_type>(),
                 const_cast<value_type*>(matrix.data()), matrix.row_stride() * sizeof(value_type));
}

[[nodiscard]] inline InterpolationResult cubic_interpolate_2d_inplace(ksj::array::MatrixView<ksj::base::cf32> matrix,
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
  if (matrix.col_stride() != 1U || !fits_cv_size(rows, cols)) {
    return {InterpolationStatus::backend_error, 0};
  }

  auto source_buffer = ksj::array::make_pooled_matrix<float>(rows, cols);
  auto destination_buffer = ksj::array::make_pooled_matrix<float>(rows, cols);
  auto source = source_buffer.view();
  auto destination = destination_buffer.view();
  ksj::array::transform(matrix, source, [](const auto& value) {
    const auto real = value.real();
    return real < 0.0F ? 0.0F : real;
  });

  auto map_x_buffer = ksj::array::make_pooled_image<float>(rows, cols);
  auto map_y_buffer = ksj::array::make_pooled_image<float>(rows, cols);
  auto map_x = as_opencv(map_x_buffer.view());
  auto map_y = as_opencv(map_y_buffer.view());
  const auto row_factor = (axis == InterpolationAxis::column) ? static_cast<double>(ratio) : 1.0;
  const auto col_factor = (axis == InterpolationAxis::row) ? static_cast<double>(ratio) : 1.0;
  const auto row_shift = (axis == InterpolationAxis::column)
                           ? ((1.0 - static_cast<double>(ratio)) * ((static_cast<double>(rows) + 1.0) / 2.0))
                           : 0.0;
  const auto col_shift = (axis == InterpolationAxis::row)
                           ? ((1.0 - static_cast<double>(ratio)) * ((static_cast<double>(cols) + 1.0) / 2.0))
                           : 0.0;

  for (std::size_t row = 0; row < rows; ++row) {
    auto* map_x_row = map_x.ptr<float>(static_cast<int>(row));
    auto* map_y_row = map_y.ptr<float>(static_cast<int>(row));
    for (std::size_t col = 0; col < cols; ++col) {
      map_x_row[col] = static_cast<float>((static_cast<double>(col) - col_shift) / col_factor);
      map_y_row[col] = static_cast<float>((static_cast<double>(row) - row_shift) / row_factor);
    }
  }

  cv::remap(as_opencv(source), as_opencv(destination), map_x, map_y, cv::INTER_CUBIC, cv::BORDER_CONSTANT,
            cv::Scalar(0.0));
  ksj::array::transform(destination, matrix, [](float value) {
    return ksj::base::cf32(value < 0.0F ? 0.0F : value, 0.0F);
  });
  return {InterpolationStatus::success, 0};
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace detail::opencv {

InterpolationResult cubic_interpolate_2d_inplace(ksj::array::MatrixView<ksj::base::cf32> matrix,
                                                 const InterpolationAxis axis, const float ratio) {
  return detail::opencv_impl::cubic_interpolate_2d_inplace(matrix, axis, ratio);
}
} // namespace detail::opencv
} // namespace ksj::image
