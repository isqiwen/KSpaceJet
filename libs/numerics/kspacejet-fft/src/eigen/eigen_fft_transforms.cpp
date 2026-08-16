#include "kspacejet/array/array.hpp"
#include "kspacejet/fft/detail/eigen/eigen_fft_transforms.hpp"

#include <algorithm>
#include <complex>
#include <cmath>
#include <cstddef>
#include <stdexcept>

#include <unsupported/Eigen/FFT>

namespace ksj::fft::detail::eigen_impl {
template <typename T>
[[nodiscard]] T normalization_scale(const std::size_t size, const Direction direction,
                                    const Normalization normalization) {
  switch (normalization) {
    case Normalization::none:
      return T{1};
    case Normalization::forward:
      return direction == Direction::forward ? T{1} / static_cast<T>(size) : T{1};
    case Normalization::inverse:
      return direction == Direction::inverse ? T{1} / static_cast<T>(size) : T{1};
    case Normalization::orthonormal:
      return T{1} / std::sqrt(static_cast<T>(size));
  }
  return T{1};
}

template <typename T>
void fft_1d(ksj::array::VectorView<const std::complex<T>> input, ksj::array::VectorView<std::complex<T>> output,
            Direction direction, Normalization normalization);

template <typename T>
void fft_2d(ksj::array::MatrixView<const std::complex<T>> input, ksj::array::MatrixView<std::complex<T>> output,
            Direction direction, Normalization normalization);

template <typename T>
void fft_1d(const ksj::array::PooledVector<std::complex<T>>& input, ksj::array::PooledVector<std::complex<T>>& output,
            const Direction direction, const Normalization normalization) {
  fft_1d(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

template <typename T>
void fft_1d(ksj::array::VectorView<const std::complex<T>> input, ksj::array::VectorView<std::complex<T>> output,
            const Direction direction, const Normalization normalization) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("fft1 output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  Eigen::FFT<T> backend;
  backend.SetFlag(Eigen::FFT<T>::Unscaled);

  const auto transform = [&backend, direction, size = static_cast<Eigen::Index>(input.size())](
                           std::complex<T>* destination, const std::complex<T>* source) {
    if (direction == Direction::forward) {
      backend.fwd(destination, source, size);
    } else {
      backend.inv(destination, source, size);
    }
  };

  if (input.is_contiguous() && output.is_contiguous()) {
    transform(output.data(), input.data());
  } else {
    // Eigen 3.4's MatrixBase overload takes the address of a dynamically
    // strided Map element, which is an rvalue for const Maps. Pack strided
    // views explicitly so the pointer overload is both C++20-safe and has
    // contiguous FFT input/output storage.
    auto packed_source = ksj::array::make_pooled_vector<std::complex<T>>(input.size());
    auto packed_destination = ksj::array::make_pooled_vector<std::complex<T>>(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
      packed_source(index) = input(index);
    }
    transform(packed_destination.data(), packed_source.data());
    for (std::size_t index = 0; index < output.size(); ++index) {
      output(index) = packed_destination(index);
    }
  }

  const auto scale = normalization_scale<T>(input.size(), direction, normalization);
  if (scale != T{1}) {
    ksj::array::scale(output, static_cast<std::complex<T>>(scale), output);
  }
}

template <typename T>
void fft_2d(const ksj::array::PooledMatrix<std::complex<T>>& input, ksj::array::PooledMatrix<std::complex<T>>& output,
            const Direction direction, const Normalization normalization) {
  fft_2d(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

template <typename T>
void fft_2d(ksj::array::MatrixView<const std::complex<T>> input, ksj::array::MatrixView<std::complex<T>> output,
            const Direction direction, const Normalization normalization) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("fft2 output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  Eigen::FFT<T> backend;
  backend.SetFlag(Eigen::FFT<T>::Unscaled);

  auto temp = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
  auto source = ksj::array::make_pooled_vector<std::complex<T>>(std::max(input.rows(), input.cols()));
  auto destination = ksj::array::make_pooled_vector<std::complex<T>>(std::max(input.rows(), input.cols()));

  const auto row_scale = normalization_scale<T>(input.cols(), direction, normalization);
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      source(col) = input(row, col);
    }

    if (direction == Direction::forward) {
      backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.cols()));
    } else {
      backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.cols()));
    }

    for (std::size_t col = 0; col < input.cols(); ++col) {
      temp(row, col) = destination(col) * static_cast<std::complex<T>>(row_scale);
    }
  }

  const auto col_scale = normalization_scale<T>(input.rows(), direction, normalization);
  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      source(row) = temp(row, col);
    }

    if (direction == Direction::forward) {
      backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.rows()));
    } else {
      backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.rows()));
    }

    for (std::size_t row = 0; row < input.rows(); ++row) {
      output(row, col) = destination(row) * static_cast<std::complex<T>>(col_scale);
    }
  }
}

template <typename T>
void fft_2d_inplace_with_workspace(ksj::array::MatrixView<std::complex<T>> data,
                                   ksj::array::MatrixView<std::complex<T>> intermediate,
                                   ksj::array::VectorView<std::complex<T>> source,
                                   ksj::array::VectorView<std::complex<T>> destination, const Direction direction,
                                   const Normalization normalization) {
  if (data.rows() != intermediate.rows() || data.cols() != intermediate.cols()) {
    throw std::invalid_argument("fft2 in-place workspace matrix dimension mismatch");
  }
  const auto vector_extent = std::max(data.rows(), data.cols());
  if (source.size() < vector_extent || destination.size() < vector_extent) {
    throw std::invalid_argument("fft2 in-place workspace vector extent is too small");
  }
  if (!data.is_contiguous() || !intermediate.is_contiguous() || !source.is_contiguous() ||
      !destination.is_contiguous()) {
    throw std::invalid_argument("fft2 in-place workspace requires contiguous storage");
  }
  if (data.empty()) {
    return;
  }
  if (data.data() == intermediate.data() || source.data() == destination.data() || source.data() == data.data() ||
      source.data() == intermediate.data() || destination.data() == data.data() ||
      destination.data() == intermediate.data()) {
    throw std::invalid_argument("fft2 in-place workspace storage must not alias");
  }

  Eigen::FFT<T> backend;
  backend.SetFlag(Eigen::FFT<T>::Unscaled);

  const auto row_scale = normalization_scale<T>(data.cols(), direction, normalization);
  for (std::size_t row = 0; row < data.rows(); ++row) {
    for (std::size_t col = 0; col < data.cols(); ++col) {
      source(col) = data(row, col);
    }
    if (direction == Direction::forward) {
      backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(data.cols()));
    } else {
      backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(data.cols()));
    }
    for (std::size_t col = 0; col < data.cols(); ++col) {
      intermediate(row, col) = destination(col) * static_cast<std::complex<T>>(row_scale);
    }
  }

  const auto col_scale = normalization_scale<T>(data.rows(), direction, normalization);
  for (std::size_t col = 0; col < data.cols(); ++col) {
    for (std::size_t row = 0; row < data.rows(); ++row) {
      source(row) = intermediate(row, col);
    }
    if (direction == Direction::forward) {
      backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(data.rows()));
    } else {
      backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(data.rows()));
    }
    for (std::size_t row = 0; row < data.rows(); ++row) {
      data(row, col) = destination(row) * static_cast<std::complex<T>>(col_scale);
    }
  }
}

template <typename T>
void fft_2d_batch(const ksj::array::PooledCube<std::complex<T>>& input, ksj::array::PooledCube<std::complex<T>>& output,
                  const Direction direction, const Normalization normalization) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
    throw std::invalid_argument("fft2 batch output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  Eigen::FFT<T> backend;
  backend.SetFlag(Eigen::FFT<T>::Unscaled);

  auto temp = ksj::array::make_pooled_matrix<std::complex<T>>(input.dim0(), input.dim1());
  auto source = ksj::array::make_pooled_vector<std::complex<T>>(std::max(input.dim0(), input.dim1()));
  auto destination = ksj::array::make_pooled_vector<std::complex<T>>(std::max(input.dim0(), input.dim1()));

  const auto dim0_scale = normalization_scale<T>(input.dim1(), direction, normalization);
  const auto dim1_scale = normalization_scale<T>(input.dim0(), direction, normalization);
  for (std::size_t i2 = 0; i2 < input.dim2(); ++i2) {
    for (std::size_t i0 = 0; i0 < input.dim0(); ++i0) {
      for (std::size_t i1 = 0; i1 < input.dim1(); ++i1) {
        source(i1) = input(i0, i1, i2);
      }

      if (direction == Direction::forward) {
        backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim1()));
      } else {
        backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim1()));
      }

      for (std::size_t i1 = 0; i1 < input.dim1(); ++i1) {
        temp(i0, i1) = destination(i1) * static_cast<std::complex<T>>(dim0_scale);
      }
    }

    for (std::size_t i1 = 0; i1 < input.dim1(); ++i1) {
      for (std::size_t i0 = 0; i0 < input.dim0(); ++i0) {
        source(i0) = temp(i0, i1);
      }

      if (direction == Direction::forward) {
        backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim0()));
      } else {
        backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim0()));
      }

      for (std::size_t i0 = 0; i0 < input.dim0(); ++i0) {
        output(i0, i1, i2) = destination(i0) * static_cast<std::complex<T>>(dim1_scale);
      }
    }
  }
}

template <typename T>
void fft_3d(ksj::array::CubeView<const std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
            const Direction direction, const Normalization normalization) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
    throw std::invalid_argument("fft3 output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  Eigen::FFT<T> backend;
  backend.SetFlag(Eigen::FFT<T>::Unscaled);

  auto temp = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  auto source = ksj::array::make_pooled_vector<std::complex<T>>(std::max({input.dim0(), input.dim1(), input.dim2()}));
  auto destination =
    ksj::array::make_pooled_vector<std::complex<T>>(std::max({input.dim0(), input.dim1(), input.dim2()}));

  const auto dim1_scale = normalization_scale<T>(input.dim1(), direction, normalization);
  for (std::size_t i2 = 0; i2 < input.dim2(); ++i2) {
    for (std::size_t i0 = 0; i0 < input.dim0(); ++i0) {
      for (std::size_t i1 = 0; i1 < input.dim1(); ++i1) {
        source(i1) = input(i0, i1, i2);
      }

      if (direction == Direction::forward) {
        backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim1()));
      } else {
        backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim1()));
      }

      for (std::size_t i1 = 0; i1 < input.dim1(); ++i1) {
        temp(i0, i1, i2) = destination(i1) * static_cast<std::complex<T>>(dim1_scale);
      }
    }
  }

  const auto dim0_scale = normalization_scale<T>(input.dim0(), direction, normalization);
  for (std::size_t i2 = 0; i2 < input.dim2(); ++i2) {
    for (std::size_t i1 = 0; i1 < input.dim1(); ++i1) {
      for (std::size_t i0 = 0; i0 < input.dim0(); ++i0) {
        source(i0) = temp(i0, i1, i2);
      }

      if (direction == Direction::forward) {
        backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim0()));
      } else {
        backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim0()));
      }

      for (std::size_t i0 = 0; i0 < input.dim0(); ++i0) {
        output(i0, i1, i2) = destination(i0) * static_cast<std::complex<T>>(dim0_scale);
      }
    }
  }

  const auto dim2_scale = normalization_scale<T>(input.dim2(), direction, normalization);
  for (std::size_t i1 = 0; i1 < input.dim1(); ++i1) {
    for (std::size_t i0 = 0; i0 < input.dim0(); ++i0) {
      for (std::size_t i2 = 0; i2 < input.dim2(); ++i2) {
        source(i2) = output(i0, i1, i2);
      }

      if (direction == Direction::forward) {
        backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim2()));
      } else {
        backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim2()));
      }

      for (std::size_t i2 = 0; i2 < input.dim2(); ++i2) {
        output(i0, i1, i2) = destination(i2) * static_cast<std::complex<T>>(dim2_scale);
      }
    }
  }
}

template <typename T>
void fft_3d(const ksj::array::PooledCube<std::complex<T>>& input, ksj::array::PooledCube<std::complex<T>>& output,
            const Direction direction, const Normalization normalization) {
  fft_3d(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

template <typename T>
void fft_3d_batch(const ksj::array::PooledArray4D<std::complex<T>>& input,
                  ksj::array::PooledArray4D<std::complex<T>>& output, const Direction direction,
                  const Normalization normalization) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2() ||
      input.dim3() != output.dim3()) {
    throw std::invalid_argument("fft3 batch output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  Eigen::FFT<T> backend;
  backend.SetFlag(Eigen::FFT<T>::Unscaled);

  auto temp = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  auto source = ksj::array::make_pooled_vector<std::complex<T>>(std::max({input.dim0(), input.dim1(), input.dim2()}));
  auto destination =
    ksj::array::make_pooled_vector<std::complex<T>>(std::max({input.dim0(), input.dim1(), input.dim2()}));

  const auto col_scale = normalization_scale<T>(input.dim1(), direction, normalization);
  for (std::size_t batch = 0; batch < input.dim3(); ++batch) {
    for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        for (std::size_t col = 0; col < input.dim1(); ++col) {
          source(col) = input(row, col, slice, batch);
        }

        if (direction == Direction::forward) {
          backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim1()));
        } else {
          backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim1()));
        }

        for (std::size_t col = 0; col < input.dim1(); ++col) {
          temp(row, col, slice) = destination(col) * static_cast<std::complex<T>>(col_scale);
        }
      }
    }

    const auto row_scale = normalization_scale<T>(input.dim0(), direction, normalization);
    for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
      for (std::size_t col = 0; col < input.dim1(); ++col) {
        for (std::size_t row = 0; row < input.dim0(); ++row) {
          source(row) = temp(row, col, slice);
        }

        if (direction == Direction::forward) {
          backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim0()));
        } else {
          backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim0()));
        }

        for (std::size_t row = 0; row < input.dim0(); ++row) {
          output(row, col, slice, batch) = destination(row) * static_cast<std::complex<T>>(row_scale);
        }
      }
    }

    const auto slice_scale = normalization_scale<T>(input.dim2(), direction, normalization);
    for (std::size_t col = 0; col < input.dim1(); ++col) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
          source(slice) = output(row, col, slice, batch);
        }

        if (direction == Direction::forward) {
          backend.fwd(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim2()));
        } else {
          backend.inv(destination.data(), source.data(), static_cast<Eigen::Index>(input.dim2()));
        }

        for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
          output(row, col, slice, batch) = destination(slice) * static_cast<std::complex<T>>(slice_scale);
        }
      }
    }
  }
}

} // namespace ksj::fft::detail::eigen_impl

namespace ksj::fft::detail::eigen {

void fft_1d(ksj::array::VectorView<const std::complex<float>> input, ksj::array::VectorView<std::complex<float>> output,
            const Direction direction, const Normalization normalization) {
  detail::eigen_impl::fft_1d(input, output, direction, normalization);
}

void fft_1d(ksj::array::VectorView<const std::complex<double>> input,
            ksj::array::VectorView<std::complex<double>> output, const Direction direction,
            const Normalization normalization) {
  detail::eigen_impl::fft_1d(input, output, direction, normalization);
}

void fft_2d(ksj::array::MatrixView<const std::complex<float>> input, ksj::array::MatrixView<std::complex<float>> output,
            const Direction direction, const Normalization normalization) {
  detail::eigen_impl::fft_2d(input, output, direction, normalization);
}

void fft_2d(ksj::array::MatrixView<const std::complex<double>> input,
            ksj::array::MatrixView<std::complex<double>> output, const Direction direction,
            const Normalization normalization) {
  detail::eigen_impl::fft_2d(input, output, direction, normalization);
}

void fft_2d_inplace_with_workspace(ksj::array::MatrixView<std::complex<float>> data,
                                   ksj::array::MatrixView<std::complex<float>> intermediate,
                                   ksj::array::VectorView<std::complex<float>> source,
                                   ksj::array::VectorView<std::complex<float>> destination, const Direction direction,
                                   const Normalization normalization) {
  detail::eigen_impl::fft_2d_inplace_with_workspace(data, intermediate, source, destination, direction, normalization);
}

void fft_2d_inplace_with_workspace(ksj::array::MatrixView<std::complex<double>> data,
                                   ksj::array::MatrixView<std::complex<double>> intermediate,
                                   ksj::array::VectorView<std::complex<double>> source,
                                   ksj::array::VectorView<std::complex<double>> destination, const Direction direction,
                                   const Normalization normalization) {
  detail::eigen_impl::fft_2d_inplace_with_workspace(data, intermediate, source, destination, direction, normalization);
}

void fft_2d_batch(const ksj::array::PooledCube<std::complex<float>>& input,
                  ksj::array::PooledCube<std::complex<float>>& output, const Direction direction,
                  const Normalization normalization) {
  detail::eigen_impl::fft_2d_batch(input, output, direction, normalization);
}

void fft_2d_batch(const ksj::array::PooledCube<std::complex<double>>& input,
                  ksj::array::PooledCube<std::complex<double>>& output, const Direction direction,
                  const Normalization normalization) {
  detail::eigen_impl::fft_2d_batch(input, output, direction, normalization);
}

void fft_3d(ksj::array::CubeView<const std::complex<float>> input, ksj::array::CubeView<std::complex<float>> output,
            const Direction direction, const Normalization normalization) {
  detail::eigen_impl::fft_3d(input, output, direction, normalization);
}

void fft_3d(ksj::array::CubeView<const std::complex<double>> input, ksj::array::CubeView<std::complex<double>> output,
            const Direction direction, const Normalization normalization) {
  detail::eigen_impl::fft_3d(input, output, direction, normalization);
}

void fft_3d_batch(const ksj::array::PooledArray4D<std::complex<float>>& input,
                  ksj::array::PooledArray4D<std::complex<float>>& output, const Direction direction,
                  const Normalization normalization) {
  detail::eigen_impl::fft_3d_batch(input, output, direction, normalization);
}

void fft_3d_batch(const ksj::array::PooledArray4D<std::complex<double>>& input,
                  ksj::array::PooledArray4D<std::complex<double>>& output, const Direction direction,
                  const Normalization normalization) {
  detail::eigen_impl::fft_3d_batch(input, output, direction, normalization);
}

} // namespace ksj::fft::detail::eigen
