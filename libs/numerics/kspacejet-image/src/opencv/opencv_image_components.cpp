#include "kspacejet/image/detail/opencv/opencv_image_components.hpp"
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

template <typename T>
[[nodiscard]] bool connected_components(const ksj::array::PooledImage<T>& input,
                                        ksj::array::PooledImage<ConnectedComponentLabel>& labels,
                                        std::vector<ConnectedComponentStats>* stats, const Connectivity connectivity,
                                        std::size_t& component_count) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (connectivity != Connectivity::four && connectivity != Connectivity::eight) {
      return false;
    }
    if (labels.rows() != input.rows() || labels.cols() != input.cols() || !fits_cv_size(input.rows(), input.cols())) {
      return false;
    }
    if (stats != nullptr) {
      stats->clear();
    }
    component_count = 0U;
    if (input.empty()) {
      return true;
    }

    auto mask_buffer = ksj::array::make_pooled_image<unsigned char>(input.rows(), input.cols());
    auto mask = as_opencv(mask_buffer.view());
    cv::compare(as_opencv(input.view()), cv::Scalar(0.0), mask, cv::CMP_NE);
    cv::Mat stats_mat;
    cv::Mat centroids_mat;
    const auto labels_count = cv::connectedComponentsWithStats(mask, as_opencv(labels.view()), stats_mat, centroids_mat,
                                                               static_cast<int>(connectivity), CV_32S);
    if (labels_count <= 0) {
      return false;
    }
    component_count = static_cast<std::size_t>(labels_count - 1);

    if (stats != nullptr) {
      stats->reserve(component_count);
      for (int label = 1; label < labels_count; ++label) {
        ConnectedComponentStats component{};
        component.label = static_cast<ConnectedComponentLabel>(label);
        component.area = static_cast<std::size_t>(stats_mat.at<int>(label, cv::CC_STAT_AREA));
        const auto left = static_cast<std::size_t>(stats_mat.at<int>(label, cv::CC_STAT_LEFT));
        const auto top = static_cast<std::size_t>(stats_mat.at<int>(label, cv::CC_STAT_TOP));
        const auto width = static_cast<std::size_t>(stats_mat.at<int>(label, cv::CC_STAT_WIDTH));
        const auto height = static_cast<std::size_t>(stats_mat.at<int>(label, cv::CC_STAT_HEIGHT));
        component.min_row = top;
        component.min_col = left;
        component.max_row = top + height - 1U;
        component.max_col = left + width - 1U;
        component.centroid_col = centroids_mat.at<double>(label, 0);
        component.centroid_row = centroids_mat.at<double>(label, 1);
        stats->push_back(component);
      }
    }
    return true;
  } else {
    return false;
  }
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace detail::opencv {

bool connected_components(const ksj::array::PooledImage<float>& input,
                          ksj::array::PooledImage<ConnectedComponentLabel>& labels,
                          std::vector<ConnectedComponentStats>* stats, const Connectivity connectivity,
                          std::size_t& component_count) {
  return detail::opencv_impl::connected_components(input, labels, stats, connectivity, component_count);
}

bool connected_components(const ksj::array::PooledImage<double>& input,
                          ksj::array::PooledImage<ConnectedComponentLabel>& labels,
                          std::vector<ConnectedComponentStats>* stats, const Connectivity connectivity,
                          std::size_t& component_count) {
  return detail::opencv_impl::connected_components(input, labels, stats, connectivity, component_count);
}
} // namespace detail::opencv
} // namespace ksj::image
