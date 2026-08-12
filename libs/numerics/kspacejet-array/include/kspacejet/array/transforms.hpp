#pragma once

/// Generic unary, binary, and multi-input View transforms with explicit output storage.

#include "kspacejet/array/detail/traversal.hpp"
#include "kspacejet/array/indexing.hpp"
#include "kspacejet/array/iteration.hpp"

namespace ksj::array {

template <typename InputT, typename OutputT, typename F>
F transform(VectorView<InputT> input, VectorView<OutputT> output, F f) {
  detail::validate_same_size(input.size(), output.size(), "vector view transform size mismatch");
  return detail::transform_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform(MatrixView<InputT> input, MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(input, output, "matrix view transform shape mismatch");
  return detail::transform_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform(MatrixView<InputT> input, ImageView<OutputT> output, F f) {
  detail::validate_same_shape(input, output, "2d view transform shape mismatch");
  return detail::transform_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform(ImageView<InputT> input, MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(input, output, "2d view transform shape mismatch");
  return detail::transform_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform(ImageView<InputT> input, ImageView<OutputT> output, F f) {
  detail::validate_same_shape(input, output, "image view transform shape mismatch");
  return detail::transform_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform(CubeView<InputT> input, CubeView<OutputT> output, F f) {
  detail::validate_same_cube_shape(input, output, "cube view transform shape mismatch");
  return detail::transform_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform(Array4DView<InputT> input, Array4DView<OutputT> output, F f) {
  detail::validate_same_array4d_shape(input, output, "array4d view transform shape mismatch");
  return detail::transform_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform_linear_indexed(VectorView<InputT> input, VectorView<OutputT> output, F f) {
  detail::validate_same_size(input.size(), output.size(), "vector view indexed transform size mismatch");
  return detail::transform_indexed_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform_linear_indexed(MatrixView<InputT> input, MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(input, output, "matrix view indexed transform shape mismatch");
  return detail::transform_indexed_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform_linear_indexed(ImageView<InputT> input, ImageView<OutputT> output, F f) {
  detail::validate_same_shape(input, output, "image view indexed transform shape mismatch");
  return detail::transform_indexed_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform_linear_indexed(CubeView<InputT> input, CubeView<OutputT> output, F f) {
  detail::validate_same_cube_shape(input, output, "cube view indexed transform shape mismatch");
  return detail::transform_indexed_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform_linear_indexed(Array4DView<InputT> input, Array4DView<OutputT> output, F f) {
  detail::validate_same_array4d_shape(input, output, "array4d view indexed transform shape mismatch");
  return detail::transform_indexed_elements(input, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform_indexed(VectorView<InputT> input, VectorView<OutputT> output, F f) {
  detail::validate_same_size(input.size(), output.size(), "vector view indexed transform size mismatch");
  for_each_indexed(input, [&](const Index<1U> index, const auto& value) {
    output(index[0U]) = f(index, value);
  });
  return f;
}

template <typename InputT, typename OutputT, typename F>
F transform_indexed(MatrixView<InputT> input, MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(input, output, "matrix view indexed transform shape mismatch");
  for_each_indexed(input, [&](const Index<2U> index, const auto& value) {
    output(index[0U], index[1U]) = f(index, value);
  });
  return f;
}

template <typename InputT, typename OutputT, typename F>
F transform_indexed(ImageView<InputT> input, ImageView<OutputT> output, F f) {
  detail::validate_same_shape(input, output, "image view indexed transform shape mismatch");
  for_each_indexed(input, [&](const Index<2U> index, const auto& value) {
    output(index[0U], index[1U]) = f(index, value);
  });
  return f;
}

template <typename InputT, typename OutputT, typename F>
F transform_indexed(CubeView<InputT> input, CubeView<OutputT> output, F f) {
  detail::validate_same_cube_shape(input, output, "cube view indexed transform shape mismatch");
  for_each_indexed(input, [&](const Index<3U> index, const auto& value) {
    output(index[0U], index[1U], index[2U]) = f(index, value);
  });
  return f;
}

template <typename InputT, typename OutputT, typename F>
F transform_indexed(Array4DView<InputT> input, Array4DView<OutputT> output, F f) {
  detail::validate_same_array4d_shape(input, output, "array4d view indexed transform shape mismatch");
  for_each_indexed(input, [&](const Index<4U> index, const auto& value) {
    output(index[0U], index[1U], index[2U], index[3U]) = f(index, value);
  });
  return f;
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(VectorView<LhsT> lhs, VectorView<RhsT> rhs, VectorView<OutputT> output, F f) {
  detail::validate_same_size(lhs.size(), rhs.size(), "vector view binary transform input size mismatch");
  detail::validate_same_size(lhs.size(), output.size(), "vector view binary transform output size mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(MatrixView<LhsT> lhs, MatrixView<RhsT> rhs, MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(lhs, rhs, "matrix view binary transform input shape mismatch");
  detail::validate_same_shape(lhs, output, "matrix view binary transform output shape mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(MatrixView<LhsT> lhs, ImageView<RhsT> rhs, ImageView<OutputT> output, F f) {
  detail::validate_same_shape(lhs, rhs, "2d view binary transform input shape mismatch");
  detail::validate_same_shape(lhs, output, "2d view binary transform output shape mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(ImageView<LhsT> lhs, MatrixView<RhsT> rhs, ImageView<OutputT> output, F f) {
  detail::validate_same_shape(lhs, rhs, "2d view binary transform input shape mismatch");
  detail::validate_same_shape(lhs, output, "2d view binary transform output shape mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(ImageView<LhsT> lhs, ImageView<RhsT> rhs, MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(lhs, rhs, "2d view binary transform input shape mismatch");
  detail::validate_same_shape(lhs, output, "2d view binary transform output shape mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(MatrixView<LhsT> lhs, ImageView<RhsT> rhs, MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(lhs, rhs, "2d view binary transform input shape mismatch");
  detail::validate_same_shape(lhs, output, "2d view binary transform output shape mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(ImageView<LhsT> lhs, MatrixView<RhsT> rhs, MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(lhs, rhs, "2d view binary transform input shape mismatch");
  detail::validate_same_shape(lhs, output, "2d view binary transform output shape mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(ImageView<LhsT> lhs, ImageView<RhsT> rhs, ImageView<OutputT> output, F f) {
  detail::validate_same_shape(lhs, rhs, "image view binary transform input shape mismatch");
  detail::validate_same_shape(lhs, output, "image view binary transform output shape mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(CubeView<LhsT> lhs, CubeView<RhsT> rhs, CubeView<OutputT> output, F f) {
  detail::validate_same_cube_shape(lhs, rhs, "cube view binary transform input shape mismatch");
  detail::validate_same_cube_shape(lhs, output, "cube view binary transform output shape mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(Array4DView<LhsT> lhs, Array4DView<RhsT> rhs, Array4DView<OutputT> output, F f) {
  detail::validate_same_array4d_shape(lhs, rhs, "array4d view binary transform input shape mismatch");
  detail::validate_same_array4d_shape(lhs, output, "array4d view binary transform output shape mismatch");
  return detail::transform_elements(lhs, rhs, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(VectorView<FirstT> first, VectorView<SecondT> second, VectorView<ThirdT> third, VectorView<OutputT> output,
            F f) {
  detail::validate_same_size(first.size(), second.size(), "vector view ternary transform input size mismatch");
  detail::validate_same_size(first.size(), third.size(), "vector view ternary transform input size mismatch");
  detail::validate_same_size(first.size(), output.size(), "vector view ternary transform output size mismatch");
  return detail::transform_elements(first, second, third, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(MatrixView<FirstT> first, MatrixView<SecondT> second, MatrixView<ThirdT> third, MatrixView<OutputT> output,
            F f) {
  detail::validate_same_shape(first, second, "matrix view ternary transform input shape mismatch");
  detail::validate_same_shape(first, third, "matrix view ternary transform input shape mismatch");
  detail::validate_same_shape(first, output, "matrix view ternary transform output shape mismatch");
  return detail::transform_elements(first, second, third, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(ImageView<FirstT> first, ImageView<SecondT> second, ImageView<ThirdT> third, ImageView<OutputT> output,
            F f) {
  detail::validate_same_shape(first, second, "image view ternary transform input shape mismatch");
  detail::validate_same_shape(first, third, "image view ternary transform input shape mismatch");
  detail::validate_same_shape(first, output, "image view ternary transform output shape mismatch");
  return detail::transform_elements(first, second, third, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(CubeView<FirstT> first, CubeView<SecondT> second, CubeView<ThirdT> third, CubeView<OutputT> output, F f) {
  detail::validate_same_cube_shape(first, second, "cube view ternary transform input shape mismatch");
  detail::validate_same_cube_shape(first, third, "cube view ternary transform input shape mismatch");
  detail::validate_same_cube_shape(first, output, "cube view ternary transform output shape mismatch");
  return detail::transform_elements(first, second, third, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(Array4DView<FirstT> first, Array4DView<SecondT> second, Array4DView<ThirdT> third,
            Array4DView<OutputT> output, F f) {
  detail::validate_same_array4d_shape(first, second, "array4d view ternary transform input shape mismatch");
  detail::validate_same_array4d_shape(first, third, "array4d view ternary transform input shape mismatch");
  detail::validate_same_array4d_shape(first, output, "array4d view ternary transform output shape mismatch");
  return detail::transform_elements(first, second, third, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(VectorView<FirstT> first, VectorView<SecondT> second, VectorView<ThirdT> third, VectorView<FourthT> fourth,
            VectorView<OutputT> output, F f) {
  detail::validate_same_size(first.size(), second.size(), "vector view quaternary transform input size mismatch");
  detail::validate_same_size(first.size(), third.size(), "vector view quaternary transform input size mismatch");
  detail::validate_same_size(first.size(), fourth.size(), "vector view quaternary transform input size mismatch");
  detail::validate_same_size(first.size(), output.size(), "vector view quaternary transform output size mismatch");
  return detail::transform_elements(first, second, third, fourth, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(MatrixView<FirstT> first, MatrixView<SecondT> second, MatrixView<ThirdT> third, MatrixView<FourthT> fourth,
            MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(first, second, "matrix view quaternary transform input shape mismatch");
  detail::validate_same_shape(first, third, "matrix view quaternary transform input shape mismatch");
  detail::validate_same_shape(first, fourth, "matrix view quaternary transform input shape mismatch");
  detail::validate_same_shape(first, output, "matrix view quaternary transform output shape mismatch");
  return detail::transform_elements(first, second, third, fourth, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(ImageView<FirstT> first, ImageView<SecondT> second, ImageView<ThirdT> third, ImageView<FourthT> fourth,
            ImageView<OutputT> output, F f) {
  detail::validate_same_shape(first, second, "image view quaternary transform input shape mismatch");
  detail::validate_same_shape(first, third, "image view quaternary transform input shape mismatch");
  detail::validate_same_shape(first, fourth, "image view quaternary transform input shape mismatch");
  detail::validate_same_shape(first, output, "image view quaternary transform output shape mismatch");
  return detail::transform_elements(first, second, third, fourth, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(CubeView<FirstT> first, CubeView<SecondT> second, CubeView<ThirdT> third, CubeView<FourthT> fourth,
            CubeView<OutputT> output, F f) {
  detail::validate_same_cube_shape(first, second, "cube view quaternary transform input shape mismatch");
  detail::validate_same_cube_shape(first, third, "cube view quaternary transform input shape mismatch");
  detail::validate_same_cube_shape(first, fourth, "cube view quaternary transform input shape mismatch");
  detail::validate_same_cube_shape(first, output, "cube view quaternary transform output shape mismatch");
  return detail::transform_elements(first, second, third, fourth, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(Array4DView<FirstT> first, Array4DView<SecondT> second, Array4DView<ThirdT> third,
            Array4DView<FourthT> fourth, Array4DView<OutputT> output, F f) {
  detail::validate_same_array4d_shape(first, second, "array4d view quaternary transform input shape mismatch");
  detail::validate_same_array4d_shape(first, third, "array4d view quaternary transform input shape mismatch");
  detail::validate_same_array4d_shape(first, fourth, "array4d view quaternary transform input shape mismatch");
  detail::validate_same_array4d_shape(first, output, "array4d view quaternary transform output shape mismatch");
  return detail::transform_elements(first, second, third, fourth, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(VectorView<FirstT> first, VectorView<SecondT> second, VectorView<ThirdT> third, VectorView<FourthT> fourth,
            VectorView<FifthT> fifth, VectorView<OutputT> output, F f) {
  detail::validate_same_size(first.size(), second.size(), "vector view quinary transform input size mismatch");
  detail::validate_same_size(first.size(), third.size(), "vector view quinary transform input size mismatch");
  detail::validate_same_size(first.size(), fourth.size(), "vector view quinary transform input size mismatch");
  detail::validate_same_size(first.size(), fifth.size(), "vector view quinary transform input size mismatch");
  detail::validate_same_size(first.size(), output.size(), "vector view quinary transform output size mismatch");
  return detail::transform_elements(first, second, third, fourth, fifth, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(MatrixView<FirstT> first, MatrixView<SecondT> second, MatrixView<ThirdT> third, MatrixView<FourthT> fourth,
            MatrixView<FifthT> fifth, MatrixView<OutputT> output, F f) {
  detail::validate_same_shape(first, second, "matrix view quinary transform input shape mismatch");
  detail::validate_same_shape(first, third, "matrix view quinary transform input shape mismatch");
  detail::validate_same_shape(first, fourth, "matrix view quinary transform input shape mismatch");
  detail::validate_same_shape(first, fifth, "matrix view quinary transform input shape mismatch");
  detail::validate_same_shape(first, output, "matrix view quinary transform output shape mismatch");
  return detail::transform_elements(first, second, third, fourth, fifth, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(ImageView<FirstT> first, ImageView<SecondT> second, ImageView<ThirdT> third, ImageView<FourthT> fourth,
            ImageView<FifthT> fifth, ImageView<OutputT> output, F f) {
  detail::validate_same_shape(first, second, "image view quinary transform input shape mismatch");
  detail::validate_same_shape(first, third, "image view quinary transform input shape mismatch");
  detail::validate_same_shape(first, fourth, "image view quinary transform input shape mismatch");
  detail::validate_same_shape(first, fifth, "image view quinary transform input shape mismatch");
  detail::validate_same_shape(first, output, "image view quinary transform output shape mismatch");
  return detail::transform_elements(first, second, third, fourth, fifth, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(CubeView<FirstT> first, CubeView<SecondT> second, CubeView<ThirdT> third, CubeView<FourthT> fourth,
            CubeView<FifthT> fifth, CubeView<OutputT> output, F f) {
  detail::validate_same_cube_shape(first, second, "cube view quinary transform input shape mismatch");
  detail::validate_same_cube_shape(first, third, "cube view quinary transform input shape mismatch");
  detail::validate_same_cube_shape(first, fourth, "cube view quinary transform input shape mismatch");
  detail::validate_same_cube_shape(first, fifth, "cube view quinary transform input shape mismatch");
  detail::validate_same_cube_shape(first, output, "cube view quinary transform output shape mismatch");
  return detail::transform_elements(first, second, third, fourth, fifth, output, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(Array4DView<FirstT> first, Array4DView<SecondT> second, Array4DView<ThirdT> third,
            Array4DView<FourthT> fourth, Array4DView<FifthT> fifth, Array4DView<OutputT> output, F f) {
  detail::validate_same_array4d_shape(first, second, "array4d view quinary transform input shape mismatch");
  detail::validate_same_array4d_shape(first, third, "array4d view quinary transform input shape mismatch");
  detail::validate_same_array4d_shape(first, fourth, "array4d view quinary transform input shape mismatch");
  detail::validate_same_array4d_shape(first, fifth, "array4d view quinary transform input shape mismatch");
  detail::validate_same_array4d_shape(first, output, "array4d view quinary transform output shape mismatch");
  return detail::transform_elements(first, second, third, fourth, fifth, output, f);
}

template <typename InputT, typename OutputT, typename F>
F transform(const PooledVector<InputT>& input, PooledVector<OutputT>& output, F f) {
  return transform(input.view(), output.view(), f);
}

template <typename InputT, typename OutputT, typename F>
F transform(const PooledMatrix<InputT>& input, PooledMatrix<OutputT>& output, F f) {
  return transform(input.view(), output.view(), f);
}

template <typename InputT, typename OutputT, typename F>
F transform(const PooledImage<InputT>& input, PooledImage<OutputT>& output, F f) {
  return transform(input.view(), output.view(), f);
}

template <typename InputT, typename OutputT, typename F>
F transform(const PooledCube<InputT>& input, PooledCube<OutputT>& output, F f) {
  return transform(input.view(), output.view(), f);
}

template <typename InputT, typename OutputT, typename F>
F transform(const PooledArray4D<InputT>& input, PooledArray4D<OutputT>& output, F f) {
  return transform(input.view(), output.view(), f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(const PooledVector<LhsT>& lhs, const PooledVector<RhsT>& rhs, PooledVector<OutputT>& output, F f) {
  return transform(lhs.view(), rhs.view(), output.view(), f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(const PooledMatrix<LhsT>& lhs, const PooledMatrix<RhsT>& rhs, PooledMatrix<OutputT>& output, F f) {
  return transform(lhs.view(), rhs.view(), output.view(), f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(const PooledImage<LhsT>& lhs, const PooledImage<RhsT>& rhs, PooledImage<OutputT>& output, F f) {
  return transform(lhs.view(), rhs.view(), output.view(), f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(const PooledCube<LhsT>& lhs, const PooledCube<RhsT>& rhs, PooledCube<OutputT>& output, F f) {
  return transform(lhs.view(), rhs.view(), output.view(), f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
F transform(const PooledArray4D<LhsT>& lhs, const PooledArray4D<RhsT>& rhs, PooledArray4D<OutputT>& output, F f) {
  return transform(lhs.view(), rhs.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(const PooledVector<FirstT>& first, const PooledVector<SecondT>& second, const PooledVector<ThirdT>& third,
            PooledVector<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(const PooledMatrix<FirstT>& first, const PooledMatrix<SecondT>& second, const PooledMatrix<ThirdT>& third,
            PooledMatrix<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(const PooledImage<FirstT>& first, const PooledImage<SecondT>& second, const PooledImage<ThirdT>& third,
            PooledImage<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(const PooledCube<FirstT>& first, const PooledCube<SecondT>& second, const PooledCube<ThirdT>& third,
            PooledCube<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename OutputT, typename F>
F transform(const PooledArray4D<FirstT>& first, const PooledArray4D<SecondT>& second,
            const PooledArray4D<ThirdT>& third, PooledArray4D<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(const PooledVector<FirstT>& first, const PooledVector<SecondT>& second, const PooledVector<ThirdT>& third,
            const PooledVector<FourthT>& fourth, PooledVector<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(const PooledMatrix<FirstT>& first, const PooledMatrix<SecondT>& second, const PooledMatrix<ThirdT>& third,
            const PooledMatrix<FourthT>& fourth, PooledMatrix<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(const PooledImage<FirstT>& first, const PooledImage<SecondT>& second, const PooledImage<ThirdT>& third,
            const PooledImage<FourthT>& fourth, PooledImage<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(const PooledCube<FirstT>& first, const PooledCube<SecondT>& second, const PooledCube<ThirdT>& third,
            const PooledCube<FourthT>& fourth, PooledCube<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename OutputT, typename F>
F transform(const PooledArray4D<FirstT>& first, const PooledArray4D<SecondT>& second,
            const PooledArray4D<ThirdT>& third, const PooledArray4D<FourthT>& fourth, PooledArray4D<OutputT>& output,
            F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(const PooledVector<FirstT>& first, const PooledVector<SecondT>& second, const PooledVector<ThirdT>& third,
            const PooledVector<FourthT>& fourth, const PooledVector<FifthT>& fifth, PooledVector<OutputT>& output,
            F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), fifth.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(const PooledMatrix<FirstT>& first, const PooledMatrix<SecondT>& second, const PooledMatrix<ThirdT>& third,
            const PooledMatrix<FourthT>& fourth, const PooledMatrix<FifthT>& fifth, PooledMatrix<OutputT>& output,
            F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), fifth.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(const PooledImage<FirstT>& first, const PooledImage<SecondT>& second, const PooledImage<ThirdT>& third,
            const PooledImage<FourthT>& fourth, const PooledImage<FifthT>& fifth, PooledImage<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), fifth.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(const PooledCube<FirstT>& first, const PooledCube<SecondT>& second, const PooledCube<ThirdT>& third,
            const PooledCube<FourthT>& fourth, const PooledCube<FifthT>& fifth, PooledCube<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), fifth.view(), output.view(), f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename FourthT, typename FifthT, typename OutputT,
          typename F>
F transform(const PooledArray4D<FirstT>& first, const PooledArray4D<SecondT>& second,
            const PooledArray4D<ThirdT>& third, const PooledArray4D<FourthT>& fourth,
            const PooledArray4D<FifthT>& fifth, PooledArray4D<OutputT>& output, F f) {
  return transform(first.view(), second.view(), third.view(), fourth.view(), fifth.view(), output.view(), f);
}

} // namespace ksj::array
