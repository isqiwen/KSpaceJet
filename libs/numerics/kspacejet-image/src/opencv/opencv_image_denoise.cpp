#include "kspacejet/image/denoise.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_denoise.hpp"
#include "kspacejet/base/types.hpp"
#include "opencv_image_common.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::image::detail::opencv_impl {

[[nodiscard]] inline bool bm3d_denoise_magnitude(ksj::array::ImageView<const ksj::base::cf32> input,
                                                 ksj::array::ImageView<ksj::base::cf32> output, const float sigma) {
  if (input.rows() != output.rows() || input.cols() != output.cols() || !fits_cv_size(input.rows(), input.cols())) {
    return false;
  }
  if (input.empty()) {
    return true;
  }

  auto magnitude_32f_buffer = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  auto magnitude_32f = as_opencv(magnitude_32f_buffer.view());
  for (std::size_t row = 0; row < input.rows(); ++row) {
    auto* row_data = magnitude_32f.ptr<float>(static_cast<int>(row));
    for (std::size_t col = 0; col < input.cols(); ++col) {
      row_data[col] = std::abs(input(row, col));
    }
  }

  double min_value = 0.0;
  double max_value = 0.0;
  cv::minMaxIdx(magnitude_32f, &min_value, &max_value);
  if (max_value <= 0.0) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      for (std::size_t col = 0; col < output.cols(); ++col) {
        output(row, col) = {};
      }
    }
    return true;
  }

  auto magnitude_16u_buffer = ksj::array::make_pooled_image<std::uint16_t>(input.rows(), input.cols());
  auto magnitude_16u = as_opencv(magnitude_16u_buffer.view());
  const double scaling = 65535.0 / max_value;
  magnitude_32f.convertTo(magnitude_16u, CV_16U, scaling);

  constexpr int template_window_size = 4;
  constexpr int search_window_size = 16;
  constexpr int block_matching_step1 = 2500;
  constexpr int block_matching_step2 = 400;
  constexpr int group_size = 8;
  constexpr int sliding_step = 1;
  constexpr float beta = 2.0F;
  constexpr int norm_type = cv::NORM_L1;

  auto denoised_16u_buffer = ksj::array::make_pooled_image<std::uint16_t>(input.rows(), input.cols());
  auto denoised_16u = as_opencv(denoised_16u_buffer.view());
  cv::xphoto::bm3dDenoising(magnitude_16u, denoised_16u, sigma, template_window_size, search_window_size,
                            block_matching_step1, block_matching_step2, group_size, sliding_step, beta, norm_type);

  auto denoised_32f_buffer = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  auto denoised_32f = as_opencv(denoised_32f_buffer.view());
  denoised_16u.convertTo(denoised_32f, CV_32F, 1.0 / scaling);
  for (std::size_t row = 0; row < output.rows(); ++row) {
    const auto* row_data = denoised_32f.ptr<float>(static_cast<int>(row));
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = ksj::base::cf32(row_data[col], 0.0F);
    }
  }
  return true;
}

[[nodiscard]] inline bool bm3d_denoise_magnitude(const ksj::array::PooledImage<ksj::base::cf32>& input,
                                                 ksj::array::PooledImage<ksj::base::cf32>& output, const float sigma) {
  return bm3d_denoise_magnitude(ksj::array::as_const_view(input.view()), output.view(), sigma);
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace {

[[nodiscard]] bool fits_cv_size(const std::size_t rows, const std::size_t cols) noexcept {
  return rows <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
         cols <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

template <typename T> [[nodiscard]] int cv_type() {
  if constexpr (std::is_same_v<T, float>) {
    return CV_32FC1;
  } else if constexpr (std::is_same_v<T, std::uint16_t>) {
    return CV_16UC1;
  } else {
    return -1;
  }
}

template <typename T> [[nodiscard]] cv::Mat image_view_mat(ksj::array::ImageView<T> image) {
  using value_type = std::remove_const_t<T>;
  return cv::Mat(static_cast<int>(image.rows()), static_cast<int>(image.cols()), cv_type<value_type>(),
                 const_cast<value_type*>(image.data()), image.row_stride_bytes());
}

[[nodiscard]] ksj::array::PooledImage<float> complex_magnitude_32f(ksj::array::ImageView<const ksj::base::cf32> input) {
  auto magnitude_buffer = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  auto magnitude = image_view_mat(magnitude_buffer.view());
  for (std::size_t row = 0; row < input.rows(); ++row) {
    auto* row_data = magnitude.ptr<float>(static_cast<int>(row));
    for (std::size_t col = 0; col < input.cols(); ++col) {
      row_data[col] = std::abs(input(row, col));
    }
  }
  return magnitude_buffer;
}

void copy_real_32f_to_complex_image(const cv::Mat& image, ksj::array::ImageView<ksj::base::cf32> output) {
  if (image.rows != static_cast<int>(output.rows()) || image.cols != static_cast<int>(output.cols()) ||
      image.type() != CV_32F) {
    throw std::runtime_error("denoise_magnitude produced an unexpected image layout");
  }

  for (std::size_t row = 0; row < output.rows(); ++row) {
    const auto* row_data = image.ptr<float>(static_cast<int>(row));
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = ksj::base::cf32(row_data[col], 0.0F);
    }
  }
}

class AnisotropicDiffusionBody : public cv::ParallelLoopBody {
public:
  AnisotropicDiffusionBody(cv::Mat& dst, cv::Mat& diff, const float kappa, const float lambda, const int option)
      : dst_(dst), diff_(diff), kappa_(kappa), lambda_(lambda), option_(option), height_(dst_.rows), width_(dst_.cols),
        kappa_squared_(kappa_ * kappa_) {}

  void operator()(const cv::Range& range) const override {
    for (int y = range.start; y < range.end; ++y) {
      if (y == 0 || y == height_ - 1) {
        continue;
      }

      float* dst_row = dst_.ptr<float>(y);
      float* diff_row = diff_.ptr<float>(y);

      for (int x = 1; x < width_ - 1; ++x) {
        const float center = dst_row[x];

        const float grad_north = dst_.ptr<float>(y - 1)[x] - center;
        const float grad_south = dst_.ptr<float>(y + 1)[x] - center;
        const float grad_east = dst_row[x + 1] - center;
        const float grad_west = dst_row[x - 1] - center;

        float c_north = 0.0F;
        float c_south = 0.0F;
        float c_east = 0.0F;
        float c_west = 0.0F;

        if (option_ == 0) {
          c_north = std::exp(-(grad_north * grad_north) / kappa_squared_);
          c_south = std::exp(-(grad_south * grad_south) / kappa_squared_);
          c_east = std::exp(-(grad_east * grad_east) / kappa_squared_);
          c_west = std::exp(-(grad_west * grad_west) / kappa_squared_);
        } else {
          c_north = 1.0F / (1.0F + (grad_north * grad_north) / kappa_squared_);
          c_south = 1.0F / (1.0F + (grad_south * grad_south) / kappa_squared_);
          c_east = 1.0F / (1.0F + (grad_east * grad_east) / kappa_squared_);
          c_west = 1.0F / (1.0F + (grad_west * grad_west) / kappa_squared_);
        }

        const float diffusion =
          lambda_ * (c_north * grad_north + c_south * grad_south + c_east * grad_east + c_west * grad_west);
        diff_row[x] = center + diffusion;
      }
    }
  }

private:
  cv::Mat& dst_;
  cv::Mat& diff_;
  float kappa_;
  float lambda_;
  int option_;
  int height_;
  int width_;
  float kappa_squared_;
};

void anisotropic_diffusion_optimized(const cv::Mat& src_32f, cv::Mat& dst, cv::Mat& diff, const float kappa,
                                     const int iterations, const float lambda, const int option) {
  CV_Assert(src_32f.type() == CV_32F);
  src_32f.copyTo(dst);
  dst.copyTo(diff);

  const int width = dst.cols;
  const int height = dst.rows;
  if (width < 2 || height < 2) {
    return;
  }

  for (int i = 0; i < iterations; ++i) {
    cv::parallel_for_(cv::Range(1, height - 1), AnisotropicDiffusionBody(dst, diff, kappa, lambda, option));

    std::swap(dst, diff);

    for (int y = 0; y < height; ++y) {
      dst.at<float>(y, 0) = src_32f.at<float>(y, 0);
      dst.at<float>(y, width - 1) = src_32f.at<float>(y, width - 1);
    }
    for (int x = 0; x < width; ++x) {
      dst.at<float>(0, x) = src_32f.at<float>(0, x);
      dst.at<float>(height - 1, x) = src_32f.at<float>(height - 1, x);
    }
  }
}

void unsharp_mask_inplace(cv::Mat& image, const float sigma, const float amount) {
  CV_Assert(sigma > 0);
  CV_Assert(amount >= 0);
  CV_Assert(image.type() == CV_32F);

  auto blurred_buffer =
    ksj::array::make_pooled_image<float>(static_cast<std::size_t>(image.rows), static_cast<std::size_t>(image.cols));
  auto blurred = image_view_mat(blurred_buffer.view());
  cv::GaussianBlur(image, blurred, cv::Size(0, 0), sigma);

  cv::subtract(image, blurred, blurred);

  blurred *= amount;
  cv::add(image, blurred, image);
}

} // namespace

namespace detail::opencv {

bool bm3d_denoise_magnitude(ksj::array::ImageView<const ksj::base::cf32> input,
                            ksj::array::ImageView<ksj::base::cf32> output, const float sigma) {
  return detail::opencv_impl::bm3d_denoise_magnitude(input, output, sigma);
}
} // namespace detail::opencv

void denoise_magnitude(ksj::array::ImageView<const ksj::base::cf32> input,
                       ksj::array::ImageView<ksj::base::cf32> output, const MagnitudeDenoiseParameters& parameters) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("denoise_magnitude requires matching input and output dimensions");
  }
  if (input.empty()) {
    return;
  }
  if (!fits_cv_size(input.rows(), input.cols())) {
    throw std::invalid_argument("denoise_magnitude image is too large for the OpenCV backend");
  }

  auto magnitude_32f_buffer = complex_magnitude_32f(input);
  auto magnitude_32f = image_view_mat(magnitude_32f_buffer.view());
  auto magnitude_16u_buffer = ksj::array::make_pooled_image<std::uint16_t>(input.rows(), input.cols());
  auto magnitude_16u = image_view_mat(magnitude_16u_buffer.view());
  magnitude_32f.convertTo(magnitude_16u, CV_16U);

  const std::vector<float> nlm_h{parameters.nlm_h};
  cv::fastNlMeansDenoising(magnitude_16u, magnitude_16u, nlm_h, parameters.nlm_template_window_size,
                           parameters.nlm_search_window_size, cv::NORM_L1);

  auto denoised_32f_buffer = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  auto denoised_32f = image_view_mat(denoised_32f_buffer.view());
  magnitude_16u.convertTo(denoised_32f, CV_32F);

  auto diffusion_scratch_buffer = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  auto diffusion_scratch = image_view_mat(diffusion_scratch_buffer.view());
  anisotropic_diffusion_optimized(denoised_32f, denoised_32f, diffusion_scratch, parameters.diffusion_kappa,
                                  parameters.diffusion_iterations, parameters.diffusion_lambda,
                                  parameters.diffusion_option);
  unsharp_mask_inplace(denoised_32f, parameters.sharpen_sigma, parameters.sharpen_amount);

  copy_real_32f_to_complex_image(denoised_32f, output);
}
} // namespace ksj::image
