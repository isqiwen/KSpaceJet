#pragma once

#include "kspacejet/array/array.hpp"

#include <Eigen/Core>
#include <unsupported/Eigen/CXX11/Tensor>

#include <type_traits>

namespace ksj::array::detail::eigen_adapter {

template <typename T> using scalar_t = std::remove_const_t<T>;

template <typename T> using EigenVector = Eigen::Matrix<scalar_t<T>, Eigen::Dynamic, 1>;

template <typename T> using EigenDense2D = Eigen::Matrix<scalar_t<T>, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

template <typename T> using EigenCube = Eigen::Tensor<scalar_t<T>, 3, Eigen::RowMajor>;

template <typename T> using EigenArray4D = Eigen::Tensor<scalar_t<T>, 4, Eigen::RowMajor>;

template <typename T>
using MaybeConstVector = std::conditional_t<std::is_const_v<T>, const EigenVector<T>, EigenVector<T>>;

template <typename T>
using MaybeConstDense2D = std::conditional_t<std::is_const_v<T>, const EigenDense2D<T>, EigenDense2D<T>>;

template <typename T>
using MaybeConstCube =
  std::conditional_t<std::is_const_v<T>, Eigen::Tensor<const scalar_t<T>, 3, Eigen::RowMajor>, EigenCube<T>>;

template <typename T>
using MaybeConstArray4D =
  std::conditional_t<std::is_const_v<T>, Eigen::Tensor<const scalar_t<T>, 4, Eigen::RowMajor>, EigenArray4D<T>>;

template <typename T>
using VectorViewMap = Eigen::Map<MaybeConstVector<T>, Eigen::Unaligned, Eigen::InnerStride<Eigen::Dynamic>>;

template <typename T>
using Dense2DViewMap =
  Eigen::Map<MaybeConstDense2D<T>, Eigen::Unaligned, Eigen::Stride<Eigen::Dynamic, Eigen::Dynamic>>;

template <typename T> using VectorMap = Eigen::Map<MaybeConstVector<T>, Eigen::Aligned64>;

template <typename T> using Dense2DMap = Eigen::Map<MaybeConstDense2D<T>, Eigen::Aligned64>;

template <typename T> using CubeMap = Eigen::TensorMap<MaybeConstCube<T>, Eigen::Aligned>;

template <typename T> using Array4DMap = Eigen::TensorMap<MaybeConstArray4D<T>, Eigen::Aligned>;

template <typename T> [[nodiscard]] auto as_eigen(VectorView<T> input) {
  return VectorViewMap<T>(input.data(), static_cast<Eigen::Index>(input.size()),
                          Eigen::InnerStride<Eigen::Dynamic>(static_cast<Eigen::Index>(input.stride())));
}

template <typename T> [[nodiscard]] auto as_eigen(MatrixView<T> input) {
  return Dense2DViewMap<T>(
    input.data(), static_cast<Eigen::Index>(input.rows()), static_cast<Eigen::Index>(input.cols()),
    Eigen::Stride<Eigen::Dynamic, Eigen::Dynamic>(static_cast<Eigen::Index>(input.row_stride()),
                                                  static_cast<Eigen::Index>(input.col_stride())));
}

template <typename T> [[nodiscard]] auto as_eigen(ImageView<T> input) {
  return Dense2DViewMap<T>(
    input.data(), static_cast<Eigen::Index>(input.rows()), static_cast<Eigen::Index>(input.cols()),
    Eigen::Stride<Eigen::Dynamic, Eigen::Dynamic>(static_cast<Eigen::Index>(input.row_stride()), 1));
}

template <typename T> [[nodiscard]] auto as_eigen(const PooledVector<T>& input) {
  return VectorMap<const T>(input.data(), static_cast<Eigen::Index>(input.size()));
}

template <typename T> [[nodiscard]] auto as_eigen(PooledVector<T>& input) {
  return VectorMap<T>(input.data(), static_cast<Eigen::Index>(input.size()));
}

template <typename T> [[nodiscard]] auto as_eigen(const PooledMatrix<T>& input) {
  return Dense2DMap<const T>(input.data(), static_cast<Eigen::Index>(input.rows()),
                             static_cast<Eigen::Index>(input.cols()));
}

template <typename T> [[nodiscard]] auto as_eigen(PooledMatrix<T>& input) {
  return Dense2DMap<T>(input.data(), static_cast<Eigen::Index>(input.rows()), static_cast<Eigen::Index>(input.cols()));
}

template <typename T> [[nodiscard]] auto as_eigen(const PooledImage<T>& input) {
  return Dense2DMap<const T>(input.data(), static_cast<Eigen::Index>(input.rows()),
                             static_cast<Eigen::Index>(input.cols()));
}

template <typename T> [[nodiscard]] auto as_eigen(PooledImage<T>& input) {
  return Dense2DMap<T>(input.data(), static_cast<Eigen::Index>(input.rows()), static_cast<Eigen::Index>(input.cols()));
}

template <typename T> [[nodiscard]] auto as_eigen(const PooledCube<T>& input) {
  return CubeMap<const T>(input.data(), static_cast<Eigen::Index>(input.dim0()),
                          static_cast<Eigen::Index>(input.dim1()), static_cast<Eigen::Index>(input.dim2()));
}

template <typename T> [[nodiscard]] auto as_eigen(PooledCube<T>& input) {
  return CubeMap<T>(input.data(), static_cast<Eigen::Index>(input.dim0()), static_cast<Eigen::Index>(input.dim1()),
                    static_cast<Eigen::Index>(input.dim2()));
}

template <typename T> [[nodiscard]] auto as_eigen(const PooledArray4D<T>& input) {
  return Array4DMap<const T>(input.data(), static_cast<Eigen::Index>(input.dim0()),
                             static_cast<Eigen::Index>(input.dim1()), static_cast<Eigen::Index>(input.dim2()),
                             static_cast<Eigen::Index>(input.dim3()));
}

template <typename T> [[nodiscard]] auto as_eigen(PooledArray4D<T>& input) {
  return Array4DMap<T>(input.data(), static_cast<Eigen::Index>(input.dim0()), static_cast<Eigen::Index>(input.dim1()),
                       static_cast<Eigen::Index>(input.dim2()), static_cast<Eigen::Index>(input.dim3()));
}

} // namespace ksj::array::detail::eigen_adapter
