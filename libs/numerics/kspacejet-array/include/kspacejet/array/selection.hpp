#pragma once

/// Gather, scatter, masking, and index-selection operations over dense row-major arrays.

#include "kspacejet/array/transforms.hpp"

#include <type_traits>

namespace ksj::array {

namespace detail {
template <typename TrueT, typename FalseT>
using where_result_t = std::common_type_t<std::remove_const_t<TrueT>, std::remove_const_t<FalseT>>;
} // namespace detail

template <typename MaskT, typename TrueT, typename FalseT, typename OutputT>
void where(VectorView<MaskT> mask, VectorView<TrueT> true_values, VectorView<FalseT> false_values,
           VectorView<OutputT> output) {
  transform(mask, true_values, false_values, output,
            [](const auto& mask_value, const auto& true_value, const auto& false_value) {
              return mask_value ? true_value : false_value;
            });
}

template <typename MaskT, typename TrueT, typename FalseT, typename OutputT>
void where(MatrixView<MaskT> mask, MatrixView<TrueT> true_values, MatrixView<FalseT> false_values,
           MatrixView<OutputT> output) {
  transform(mask, true_values, false_values, output,
            [](const auto& mask_value, const auto& true_value, const auto& false_value) {
              return mask_value ? true_value : false_value;
            });
}

template <typename MaskT, typename TrueT, typename FalseT, typename OutputT>
void where(ImageView<MaskT> mask, ImageView<TrueT> true_values, ImageView<FalseT> false_values,
           ImageView<OutputT> output) {
  transform(mask, true_values, false_values, output,
            [](const auto& mask_value, const auto& true_value, const auto& false_value) {
              return mask_value ? true_value : false_value;
            });
}

template <typename MaskT, typename TrueT, typename FalseT, typename OutputT>
void where(CubeView<MaskT> mask, CubeView<TrueT> true_values, CubeView<FalseT> false_values, CubeView<OutputT> output) {
  transform(mask, true_values, false_values, output,
            [](const auto& mask_value, const auto& true_value, const auto& false_value) {
              return mask_value ? true_value : false_value;
            });
}

template <typename MaskT, typename TrueT, typename FalseT, typename OutputT>
void where(Array4DView<MaskT> mask, Array4DView<TrueT> true_values, Array4DView<FalseT> false_values,
           Array4DView<OutputT> output) {
  transform(mask, true_values, false_values, output,
            [](const auto& mask_value, const auto& true_value, const auto& false_value) {
              return mask_value ? true_value : false_value;
            });
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledVector<detail::where_result_t<TrueT, FalseT>>
where(VectorView<MaskT> mask, VectorView<TrueT> true_values, VectorView<FalseT> false_values) {
  using output_type = detail::where_result_t<TrueT, FalseT>;
  auto output = make_pooled_vector<output_type>(mask.size());
  where(mask, true_values, false_values, output.view());
  return output;
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledMatrix<detail::where_result_t<TrueT, FalseT>>
where(MatrixView<MaskT> mask, MatrixView<TrueT> true_values, MatrixView<FalseT> false_values) {
  using output_type = detail::where_result_t<TrueT, FalseT>;
  auto output = make_pooled_matrix<output_type>(mask.rows(), mask.cols());
  where(mask, true_values, false_values, output.view());
  return output;
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledImage<detail::where_result_t<TrueT, FalseT>>
where(ImageView<MaskT> mask, ImageView<TrueT> true_values, ImageView<FalseT> false_values) {
  using output_type = detail::where_result_t<TrueT, FalseT>;
  auto output = make_pooled_image<output_type>(mask.rows(), mask.cols());
  where(mask, true_values, false_values, output.view());
  return output;
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledCube<detail::where_result_t<TrueT, FalseT>> where(CubeView<MaskT> mask, CubeView<TrueT> true_values,
                                                                      CubeView<FalseT> false_values) {
  using output_type = detail::where_result_t<TrueT, FalseT>;
  auto output = make_pooled_cube<output_type>(mask.dim0(), mask.dim1(), mask.dim2());
  where(mask, true_values, false_values, output.view());
  return output;
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledArray4D<detail::where_result_t<TrueT, FalseT>>
where(Array4DView<MaskT> mask, Array4DView<TrueT> true_values, Array4DView<FalseT> false_values) {
  using output_type = detail::where_result_t<TrueT, FalseT>;
  auto output = make_pooled_array4d<output_type>(mask.dim0(), mask.dim1(), mask.dim2(), mask.dim3());
  where(mask, true_values, false_values, output.view());
  return output;
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledVector<detail::where_result_t<TrueT, FalseT>> where(const PooledVector<MaskT>& mask,
                                                                        const PooledVector<TrueT>& true_values,
                                                                        const PooledVector<FalseT>& false_values) {
  return where(mask.view(), true_values.view(), false_values.view());
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledMatrix<detail::where_result_t<TrueT, FalseT>> where(const PooledMatrix<MaskT>& mask,
                                                                        const PooledMatrix<TrueT>& true_values,
                                                                        const PooledMatrix<FalseT>& false_values) {
  return where(mask.view(), true_values.view(), false_values.view());
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledImage<detail::where_result_t<TrueT, FalseT>>
where(const PooledImage<MaskT>& mask, const PooledImage<TrueT>& true_values, const PooledImage<FalseT>& false_values) {
  return where(mask.view(), true_values.view(), false_values.view());
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledCube<detail::where_result_t<TrueT, FalseT>>
where(const PooledCube<MaskT>& mask, const PooledCube<TrueT>& true_values, const PooledCube<FalseT>& false_values) {
  return where(mask.view(), true_values.view(), false_values.view());
}

template <typename MaskT, typename TrueT, typename FalseT>
[[nodiscard]] PooledArray4D<detail::where_result_t<TrueT, FalseT>> where(const PooledArray4D<MaskT>& mask,
                                                                         const PooledArray4D<TrueT>& true_values,
                                                                         const PooledArray4D<FalseT>& false_values) {
  return where(mask.view(), true_values.view(), false_values.view());
}

} // namespace ksj::array
