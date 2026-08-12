#include "kspacejet/image/detail/itk/itk_image_foreground_mask.hpp"

#ifdef SUCCESS
#pragma push_macro("SUCCESS")
#undef SUCCESS
#define KSJ_IMAGE_RESTORE_SUCCESS_MACRO
#endif

#ifdef FAILURE
#pragma push_macro("FAILURE")
#undef FAILURE
#define KSJ_IMAGE_RESTORE_FAILURE_MACRO
#endif

#include <itkAddImageFilter.h>
#include <itkBinaryBallStructuringElement.h>
#include <itkBinaryDilateImageFilter.h>
#include <itkBinaryErodeImageFilter.h>
#include <itkBinaryFillholeImageFilter.h>
#include <itkBinaryThresholdImageFilter.h>
#include <itkConnectedComponentImageFilter.h>
#include <itkDiscreteGaussianImageFilter.h>
#include <itkExtractImageFilter.h>
#include <itkImage.h>
#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIterator.h>
#include <itkImportImageFilter.h>
#include <itkLinearInterpolateImageFunction.h>
#include <itkMeanImageFilter.h>
#include <itkMedianImageFilter.h>
#include <itkMultiplyImageFilter.h>
#include <itkNearestNeighborInterpolateImageFunction.h>
#include <itkNumericTraits.h>
#include <itkRelabelComponentImageFilter.h>
#include <itkResampleImageFilter.h>
#include <itkUnsharpMaskImageFilter.h>

#ifdef KSJ_IMAGE_RESTORE_FAILURE_MACRO
#pragma pop_macro("FAILURE")
#undef KSJ_IMAGE_RESTORE_FAILURE_MACRO
#endif

#ifdef KSJ_IMAGE_RESTORE_SUCCESS_MACRO
#pragma pop_macro("SUCCESS")
#undef KSJ_IMAGE_RESTORE_SUCCESS_MACRO
#endif

#include "kspacejet/memory/pooled_buffer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

namespace {

using ImageType = itk::Image<float, 3>;
using MaskType = itk::Image<unsigned char, 3>;

template <typename T> [[nodiscard]] T clamp_value(const T value, const T min_value, const T max_value) {
  return (value < min_value) ? min_value : (value > max_value) ? max_value : value;
}

[[nodiscard]] bool is_valid_volume_shape(const std::size_t rows, const std::size_t cols, const std::size_t slices) {
  return rows > 0 && cols > 0 && slices > 0;
}

template <typename Lhs, typename Rhs> [[nodiscard]] bool has_same_shape(const Lhs& lhs, const Rhs& rhs) {
  return lhs.rows() == rhs.rows() && lhs.columns() == rhs.columns() && lhs.slices() == rhs.slices();
}

[[nodiscard]] bool has_required_storage(const ksj::image::VolumeForegroundMaskInput& input,
                                        const ksj::image::VolumeForegroundMaskOutput& output) {
  if (input.mask_source.empty() || input.normalization_source.empty() || output.mask.empty() ||
      output.normalized_volume.empty()) {
    return false;
  }

  return is_valid_volume_shape(input.mask_source.rows(), input.mask_source.columns(), input.mask_source.slices()) &&
         is_valid_volume_shape(output.mask.rows(), output.mask.columns(), output.mask.slices()) &&
         has_same_shape(input.mask_source, input.normalization_source) &&
         has_same_shape(output.mask, output.normalized_volume);
}

template <typename TImage> struct ImportedVolume {
  ksj::memory::PooledBuffer<typename TImage::PixelType> packed_storage;
  typename TImage::Pointer image;
};

template <typename TImage>
[[nodiscard]] typename TImage::Pointer import_itk_volume_pointer(typename TImage::PixelType* data,
                                                                 const std::size_t rows, const std::size_t columns,
                                                                 const std::size_t slices) {
  typename TImage::SizeType size;
  size[0] = static_cast<typename TImage::SizeType::SizeValueType>(rows);
  size[1] = static_cast<typename TImage::SizeType::SizeValueType>(columns);
  size[2] = static_cast<typename TImage::SizeType::SizeValueType>(slices);

  typename TImage::IndexType start;
  start.Fill(0);
  typename TImage::RegionType region;
  region.SetIndex(start);
  region.SetSize(size);

  auto import_filter = itk::ImportImageFilter<typename TImage::PixelType, 3>::New();
  import_filter->SetRegion(region);
  import_filter->SetImportPointer(data, rows * columns * slices, false);
  import_filter->Update();

  auto image = import_filter->GetOutput();
  image->DisconnectPipeline();
  return image;
}

template <typename TImage, typename T>
[[nodiscard]] ImportedVolume<TImage> make_imported_itk_volume(const ksj::image::VolumeView<T> volume) {
  using PixelType = typename TImage::PixelType;
  ImportedVolume<TImage> imported;

  const auto pixel_count = volume.size();
  imported.packed_storage = ksj::memory::allocate_array<PixelType>(pixel_count);
  PixelType* import_data = imported.packed_storage.data();
  for (std::size_t slice = 0; slice < volume.slices(); ++slice) {
    for (std::size_t col = 0; col < volume.columns(); ++col) {
      for (std::size_t row = 0; row < volume.rows(); ++row) {
        const auto offset = row + volume.rows() * (col + volume.columns() * slice);
        import_data[offset] = volume(row, col, slice);
      }
    }
  }

  imported.image = import_itk_volume_pointer<TImage>(import_data, volume.rows(), volume.columns(), volume.slices());
  return imported;
}

template <typename TImage>
[[nodiscard]] ImportedVolume<TImage> make_pooled_itk_volume(const std::size_t rows, const std::size_t columns,
                                                            const std::size_t slices) {
  ImportedVolume<TImage> imported;
  imported.packed_storage = ksj::memory::allocate_array<typename TImage::PixelType>(rows * columns * slices);
  imported.image = import_itk_volume_pointer<TImage>(imported.packed_storage.data(), rows, columns, slices);
  return imported;
}

[[nodiscard]] std::vector<double> otsu_multi_threshold(const ksj::memory::PooledBuffer<double>& data,
                                                       const int num_thresholds) {
  if (data.size() == 0U || num_thresholds <= 0) {
    return {};
  }

  const auto [min_it, max_it] = std::minmax_element(data.data(), data.data() + data.size());
  const double min_val = *min_it;
  const double max_val = *max_it;
  const double data_range = max_val - min_val;
  if (data_range <= std::numeric_limits<double>::epsilon()) {
    return {};
  }

  constexpr int num_bins = 256;
  std::array<double, num_bins> hist{};

  for (std::size_t index = 0; index < data.size(); ++index) {
    const double val = data.data()[index];
    if (std::isnan(val)) {
      continue;
    }
    const double normalized = (val - min_val) / data_range;
    auto bin = static_cast<int>(normalized * (num_bins - 1));
    bin = clamp_value(bin, 0, num_bins - 1);
    hist[bin] += 1.0;
  }

  const double total_weight = std::accumulate(hist.begin(), hist.end(), 0.0);
  if (total_weight <= std::numeric_limits<double>::epsilon()) {
    return {};
  }

  std::array<double, num_bins> omega{};
  std::array<double, num_bins> mu{};
  double global_mean = 0.0;

  for (int i = 0; i < num_bins; ++i) {
    const double p = hist[i] / total_weight;
    omega[i] = (i == 0) ? p : omega[i - 1] + p;
    mu[i] = (i == 0) ? i * p : mu[i - 1] + i * p;
    global_mean += i * p;
  }

  std::vector<double> thresholds;
  auto dp = ksj::memory::allocate_array<double>(static_cast<std::size_t>(num_thresholds + 1) * num_bins);
  auto path = ksj::memory::allocate_array<int>(static_cast<std::size_t>(num_thresholds + 1) * num_bins);
  std::fill(dp.data(), dp.data() + dp.size(), 0.0);
  std::fill(path.data(), path.data() + path.size(), 0);
  const auto table_index = [](const int row, const int col) {
    return static_cast<std::size_t>(row) * num_bins + static_cast<std::size_t>(col);
  };

  for (int t = 0; t < num_bins; ++t) {
    if (omega[t] > 1.0e-10 && omega[t] < 1.0 - 1.0e-10) {
      const double mu_k = mu[t] / omega[t];
      dp.data()[table_index(1, t)] = omega[t] * std::pow(mu_k - global_mean, 2);
    }
  }

  for (int k = 2; k <= num_thresholds; ++k) {
    for (int t = k; t < num_bins; ++t) {
      double max_var = 0.0;
      for (int q = k - 1; q < t; ++q) {
        const double omega_diff = omega[t] - omega[q];
        if (omega_diff > 1.0e-10) {
          const double mu_seg = (mu[t] - mu[q]) / omega_diff;
          const double var = dp.data()[table_index(k - 1, q)] + omega_diff * std::pow(mu_seg - global_mean, 2);
          if (var > max_var) {
            max_var = var;
            path.data()[table_index(k, t)] = q;
          }
        }
      }
      dp.data()[table_index(k, t)] = max_var;
    }
  }

  int pos = num_bins - 1;
  for (int k = num_thresholds; k >= 1; --k) {
    pos = path.data()[table_index(k, pos)];
    thresholds.push_back(pos);
  }
  std::reverse(thresholds.begin(), thresholds.end());

  for (auto& threshold : thresholds) {
    threshold = min_val + (threshold / (num_bins - 1)) * data_range;
  }

  if (!std::is_sorted(thresholds.begin(), thresholds.end())) {
    std::sort(thresholds.begin(), thresholds.end());
  }

  for (auto& threshold : thresholds) {
    threshold = clamp_value(threshold, min_val, max_val);
  }

  return thresholds;
}

[[nodiscard]] MaskType::Pointer find_largest_connected_component(MaskType::Pointer mask, const unsigned connectivity) {
  using ConnectedFilterType = itk::ConnectedComponentImageFilter<MaskType, MaskType>;
  using RelabelType = itk::RelabelComponentImageFilter<MaskType, MaskType>;

  auto connected = ConnectedFilterType::New();
  connected->SetInput(mask);
  connected->SetFullyConnected(connectivity == 6);
  connected->Update();

  auto relabel = RelabelType::New();
  relabel->SetInput(connected->GetOutput());
  relabel->Update();

  auto threshold = itk::BinaryThresholdImageFilter<MaskType, MaskType>::New();
  threshold->SetInput(relabel->GetOutput());
  threshold->SetLowerThreshold(1);
  threshold->SetUpperThreshold(1);
  threshold->SetInsideValue(1);
  threshold->SetOutsideValue(0);
  threshold->Update();

  return threshold->GetOutput();
}

[[nodiscard]] bool calculate_volume_foreground_mask_impl(const ksj::image::VolumeForegroundMaskInput& input,
                                                         const ksj::image::VolumeForegroundMaskOutput& output,
                                                         const float threshold_index, const float normalization_lower,
                                                         const float normalization_upper) {
  auto itk_image_import = make_imported_itk_volume<ImageType>(input.mask_source);
  auto itk_normalization_source_import = make_imported_itk_volume<ImageType>(input.normalization_source);
  ImageType::Pointer itk_image = itk_image_import.image;
  ImageType::Pointer itk_normalization_source = itk_normalization_source_import.image;

  auto median_filter = itk::MedianImageFilter<ImageType, ImageType>::New();
  itk::Size<3> median_radius;
  median_radius.Fill(2);
  median_filter->SetRadius(median_radius);
  median_filter->SetInput(itk_image);

  auto mean_filter = itk::MeanImageFilter<ImageType, ImageType>::New();
  itk::Size<3> mean_radius;
  mean_radius.Fill(2);
  mean_filter->SetRadius(mean_radius);
  mean_filter->SetInput(median_filter->GetOutput());
  mean_filter->Update();

  auto unsharp_filter = itk::UnsharpMaskImageFilter<ImageType, ImageType>::New();
  unsharp_filter->SetInput(mean_filter->GetOutput());
  unsharp_filter->SetAmount(1.6);
  unsharp_filter->SetSigma(0.85);
  unsharp_filter->SetThreshold(0);
  unsharp_filter->Update();
  ImageType::Pointer enhanced_volume = unsharp_filter->GetOutput();

  auto pixel_values =
    ksj::memory::allocate_array<double>(enhanced_volume->GetLargestPossibleRegion().GetNumberOfPixels());
  std::size_t pixel_value_index = 0U;
  itk::ImageRegionConstIterator<ImageType> enhanced_it(enhanced_volume, enhanced_volume->GetLargestPossibleRegion());
  for (enhanced_it.GoToBegin(); !enhanced_it.IsAtEnd(); ++enhanced_it) {
    pixel_values.data()[pixel_value_index++] = enhanced_it.Get();
  }

  constexpr int threshold_count = 20;
  const auto thresholds = otsu_multi_threshold(pixel_values, threshold_count);
  const auto lower_threshold_index = static_cast<int>(threshold_index);
  const auto upper_threshold_index = static_cast<int>(threshold_index + 1.0F);
  if (lower_threshold_index < 0 || upper_threshold_index < 0 ||
      static_cast<std::size_t>(upper_threshold_index) >= thresholds.size()) {
    return false;
  }

  const float threshold1 = static_cast<float>(thresholds[static_cast<std::size_t>(lower_threshold_index)]);
  const float threshold2 = static_cast<float>(thresholds[static_cast<std::size_t>(upper_threshold_index)]);

  auto lower_filter = itk::BinaryThresholdImageFilter<ImageType, MaskType>::New();
  lower_filter->SetInput(enhanced_volume);
  lower_filter->SetUpperThreshold(threshold1);
  lower_filter->SetInsideValue(1);
  lower_filter->SetOutsideValue(0);

  auto middle_filter = itk::BinaryThresholdImageFilter<ImageType, MaskType>::New();
  middle_filter->SetInput(enhanced_volume);
  middle_filter->SetLowerThreshold(threshold1);
  middle_filter->SetUpperThreshold(threshold2);
  middle_filter->SetInsideValue(2);
  middle_filter->SetOutsideValue(0);

  auto upper_filter = itk::BinaryThresholdImageFilter<ImageType, MaskType>::New();
  upper_filter->SetInput(enhanced_volume);
  upper_filter->SetLowerThreshold(threshold2);
  upper_filter->SetInsideValue(3);
  upper_filter->SetOutsideValue(0);

  auto add_filter = itk::AddImageFilter<MaskType>::New();
  add_filter->SetInput1(lower_filter->GetOutput());
  add_filter->SetInput2(middle_filter->GetOutput());

  auto final_filter = itk::AddImageFilter<MaskType>::New();
  final_filter->SetInput1(add_filter->GetOutput());
  final_filter->SetInput2(upper_filter->GetOutput());
  final_filter->Update();
  MaskType::Pointer segmented_image = final_filter->GetOutput();

  const itk::Size<3> segmented_size = segmented_image->GetLargestPossibleRegion().GetSize();
  auto processed_mask_import =
    make_pooled_itk_volume<MaskType>(segmented_size[0], segmented_size[1], segmented_size[2]);
  MaskType::Pointer processed_mask = processed_mask_import.image;
  processed_mask->CopyInformation(segmented_image);

  for (unsigned z = 0; z < segmented_size[2]; ++z) {
    MaskType::RegionType slice_region;
    MaskType::SizeType slice_size;
    slice_size[0] = segmented_size[0];
    slice_size[1] = segmented_size[1];
    slice_size[2] = 1;
    MaskType::IndexType slice_start;
    slice_start[0] = 0;
    slice_start[1] = 0;
    slice_start[2] = z;
    slice_region.SetSize(slice_size);
    slice_region.SetIndex(slice_start);

    auto extract_filter = itk::ExtractImageFilter<MaskType, MaskType>::New();
    extract_filter->SetInput(segmented_image);
    extract_filter->SetExtractionRegion(slice_region);
    extract_filter->SetDirectionCollapseToIdentity();
    extract_filter->Update();

    auto threshold_filter = itk::BinaryThresholdImageFilter<MaskType, MaskType>::New();
    threshold_filter->SetInput(extract_filter->GetOutput());
    threshold_filter->SetLowerThreshold(2);
    threshold_filter->SetUpperThreshold(itk::NumericTraits<MaskType::PixelType>::max());
    threshold_filter->SetInsideValue(1);
    threshold_filter->SetOutsideValue(0);
    threshold_filter->Update();

    auto largest_component = find_largest_connected_component(threshold_filter->GetOutput(), 4);

    auto fill_filter = itk::BinaryFillholeImageFilter<MaskType>::New();
    fill_filter->SetInput(largest_component);
    fill_filter->SetForegroundValue(1);
    fill_filter->Update();

    itk::ImageRegionConstIterator<MaskType> src_it(fill_filter->GetOutput(),
                                                   fill_filter->GetOutput()->GetLargestPossibleRegion());
    itk::ImageRegionIterator<MaskType> dst_it(processed_mask, slice_region);
    while (!src_it.IsAtEnd()) {
      dst_it.Set(src_it.Get());
      ++src_it;
      ++dst_it;
    }
  }

  using StructuringElementType = itk::BinaryBallStructuringElement<MaskType::PixelType, 3>;
  StructuringElementType structuring_element;
  structuring_element.SetRadius(2);
  structuring_element.CreateStructuringElement();

  auto erode_filter = itk::BinaryErodeImageFilter<MaskType, MaskType, StructuringElementType>::New();
  erode_filter->SetInput(processed_mask);
  erode_filter->SetKernel(structuring_element);
  erode_filter->SetForegroundValue(1);
  erode_filter->SetErodeValue(1);
  erode_filter->Update();

  auto largest_component_3d = find_largest_connected_component(erode_filter->GetOutput(), 6);

  auto multiply_filter1 = itk::MultiplyImageFilter<MaskType, MaskType, MaskType>::New();
  multiply_filter1->SetInput1(largest_component_3d);
  multiply_filter1->SetInput2(processed_mask);
  multiply_filter1->Update();

  auto erode_filter2 = itk::BinaryErodeImageFilter<MaskType, MaskType, StructuringElementType>::New();
  erode_filter2->SetInput(multiply_filter1->GetOutput());
  erode_filter2->SetKernel(structuring_element);
  erode_filter2->SetForegroundValue(1);
  erode_filter2->SetErodeValue(1);
  erode_filter2->Update();

  auto largest_component_3d_2 = find_largest_connected_component(erode_filter2->GetOutput(), 6);

  auto dilate_filter = itk::BinaryDilateImageFilter<MaskType, MaskType, StructuringElementType>::New();
  dilate_filter->SetInput(largest_component_3d_2);
  dilate_filter->SetKernel(structuring_element);
  dilate_filter->SetForegroundValue(1);
  dilate_filter->SetDilateValue(1);
  dilate_filter->Update();

  auto multiply_filter2 = itk::MultiplyImageFilter<MaskType, MaskType, MaskType>::New();
  multiply_filter2->SetInput1(dilate_filter->GetOutput());
  multiply_filter2->SetInput2(processed_mask);
  multiply_filter2->Update();

  auto gaussian_filter = itk::DiscreteGaussianImageFilter<MaskType, ImageType>::New();
  gaussian_filter->SetInput(multiply_filter2->GetOutput());
  gaussian_filter->SetUseImageSpacing(false);
  gaussian_filter->SetVariance(1.5);
  gaussian_filter->Update();

  auto threshold_filter = itk::BinaryThresholdImageFilter<ImageType, MaskType>::New();
  threshold_filter->SetInput(gaussian_filter->GetOutput());
  threshold_filter->SetLowerThreshold(0.1);
  threshold_filter->SetInsideValue(1);
  threshold_filter->SetOutsideValue(0);
  threshold_filter->Update();

  StructuringElementType dilate_element;
  dilate_element.SetRadius(1.0);
  dilate_element.CreateStructuringElement();

  auto dilate_final_mask = itk::BinaryDilateImageFilter<MaskType, MaskType, StructuringElementType>::New();
  dilate_final_mask->SetInput(threshold_filter->GetOutput());
  dilate_final_mask->SetKernel(dilate_element);
  dilate_final_mask->SetForegroundValue(0);
  dilate_final_mask->SetDilateValue(1);
  dilate_final_mask->Update();

  itk::Size<3> target_size;
  target_size[0] = output.mask.rows();
  target_size[1] = output.mask.columns();
  target_size[2] = output.mask.slices();

  using ResampleMaskFilterType = itk::ResampleImageFilter<MaskType, MaskType>;
  auto resample_mask = ResampleMaskFilterType::New();
  using MaskInterpolatorType = itk::NearestNeighborInterpolateImageFunction<MaskType, double>;
  auto mask_interpolator = MaskInterpolatorType::New();

  MaskType::Pointer original_mask = dilate_final_mask->GetOutput();
  const auto old_size = original_mask->GetLargestPossibleRegion().GetSize();
  const auto old_spacing = original_mask->GetSpacing();

  MaskType::SpacingType new_spacing;
  for (unsigned int i = 0; i < 3; ++i) {
    if (target_size[i] <= 1) {
      new_spacing[i] = old_spacing[i];
    } else {
      const double original_physical_extent = old_spacing[i] * (old_size[i] - 1);
      new_spacing[i] = original_physical_extent / (target_size[i] - 1);
    }
  }

  resample_mask->SetInput(original_mask);
  resample_mask->SetOutputSpacing(new_spacing);
  resample_mask->SetOutputOrigin(original_mask->GetOrigin());
  resample_mask->SetOutputDirection(original_mask->GetDirection());
  resample_mask->SetSize(target_size);
  resample_mask->SetInterpolator(mask_interpolator);
  resample_mask->Update();

  using ResampleImageFilterType = itk::ResampleImageFilter<ImageType, ImageType>;
  auto resample_normalization_source = ResampleImageFilterType::New();
  using ImageInterpolatorType = itk::LinearInterpolateImageFunction<ImageType, double>;
  auto image_interpolator = ImageInterpolatorType::New();

  ImageType::Pointer original_normalization_source = itk_normalization_source;
  const auto old_region = original_normalization_source->GetLargestPossibleRegion();
  const auto old_sizes = old_region.GetSize();
  const auto old_spacings = original_normalization_source->GetSpacing();

  ImageType::SpacingType new_spacings;
  for (unsigned int i = 0; i < 3; ++i) {
    if (target_size[i] <= 1) {
      new_spacings[i] = old_spacings[i];
    } else {
      const double original_physical_extent = old_spacings[i] * (old_sizes[i] - 1);
      new_spacings[i] = original_physical_extent / (target_size[i] - 1);
    }
  }

  resample_normalization_source->SetInput(original_normalization_source);
  resample_normalization_source->SetOutputSpacing(new_spacings);
  resample_normalization_source->SetOutputOrigin(original_normalization_source->GetOrigin());
  resample_normalization_source->SetOutputDirection(original_normalization_source->GetDirection());
  resample_normalization_source->SetSize(target_size);
  resample_normalization_source->SetInterpolator(image_interpolator);
  resample_normalization_source->Update();

  auto mask = resample_mask->GetOutput();
  auto normalized_volume = resample_normalization_source->GetOutput();

  itk::ImageRegionIterator<ImageType> normalized_volume_it(normalized_volume,
                                                           normalized_volume->GetLargestPossibleRegion());
  itk::ImageRegionIterator<MaskType> mask_it(mask, mask->GetLargestPossibleRegion());

  float min_val = std::numeric_limits<float>::max();
  float max_val = std::numeric_limits<float>::lowest();
  for (normalized_volume_it.GoToBegin(), mask_it.GoToBegin(); !normalized_volume_it.IsAtEnd();
       ++normalized_volume_it, ++mask_it) {
    if (mask_it.Get() == 1) {
      const float value = normalized_volume_it.Get();
      min_val = std::min(min_val, value);
      max_val = std::max(max_val, value);
    }
  }

  const bool can_normalize = normalization_lower != normalization_upper && max_val > min_val &&
                             min_val != std::numeric_limits<float>::max() &&
                             max_val != std::numeric_limits<float>::lowest();
  if (can_normalize) {
    for (normalized_volume_it.GoToBegin(); !normalized_volume_it.IsAtEnd(); ++normalized_volume_it) {
      const float normalized_value = normalization_lower + (normalization_upper - normalization_lower) *
                                                             (normalized_volume_it.Get() - min_val) /
                                                             (max_val - min_val);
      normalized_volume_it.Set(normalized_value);
    }
  }

  for (std::size_t slice = 0; slice < output.mask.slices(); ++slice) {
    for (std::size_t row = 0; row < output.mask.rows(); ++row) {
      for (std::size_t col = 0; col < output.mask.columns(); ++col) {
        ImageType::IndexType pixel_index;
        pixel_index[0] = row;
        pixel_index[1] = col;
        pixel_index[2] = slice;
        output.mask(row, col, slice) = mask->GetPixel(pixel_index);
        output.normalized_volume(row, col, slice) = normalized_volume->GetPixel(pixel_index);
      }
    }
  }

  return true;
}

} // namespace

namespace ksj::image::detail::itk {

bool calculate_volume_foreground_mask(const VolumeForegroundMaskInput& input, const VolumeForegroundMaskOutput& output,
                                      const float threshold_index, const float normalization_lower,
                                      const float normalization_upper) {
  if (!has_required_storage(input, output)) {
    return false;
  }

  try {
    return calculate_volume_foreground_mask_impl(input, output, threshold_index, normalization_lower,
                                                 normalization_upper);
  } catch (...) {
    return false;
  }
}

} // namespace ksj::image::detail::itk

namespace ksj::image {

bool calculate_volume_foreground_mask(const VolumeForegroundMaskInput& input, const VolumeForegroundMaskOutput& output,
                                      const float threshold_index, const float normalization_lower,
                                      const float normalization_upper) {
  return detail::itk::calculate_volume_foreground_mask(input, output, threshold_index, normalization_lower,
                                                       normalization_upper);
}

} // namespace ksj::image
