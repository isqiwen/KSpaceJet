#pragma once

/// One- through four-dimensional Fourier transform APIs with explicit input, output, and normalization semantics.

#include "kspacejet/array/array.hpp"
#include "kspacejet/fft/detail/eigen/eigen_fft_transforms.hpp"
#include "kspacejet/fft/detail/fft_policy.hpp"
#include "kspacejet/fft/detail/intel/intel_fft_descriptors.hpp"
#include "kspacejet/fft/detail/intel/intel_fft_transforms.hpp"
#include "kspacejet/fft/shift.hpp"
#include "kspacejet/fft/types.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace ksj::fft {

namespace detail::algorithms {
template <typename Storage> void copy_storage(const Storage& source, Storage& destination);
} // namespace detail::algorithms

// Forward complex transform with orthonormal 1/sqrt(N) scaling: IPP FFT for
// power-of-two lengths and IPP DFT otherwise. Shift/preshift semantics
// intentionally stay with the caller so algorithms can own their layout and
// input-mutation behavior.
class OrthonormalForwardFft1Plan {
public:
  explicit OrthonormalForwardFft1Plan(const std::size_t size) : size_(size) {
    if (!descriptor_.reset(size, Direction::forward)) {
      throw std::runtime_error("failed to initialize orthonormal forward FFT plan");
    }
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }

  // Executes the cached orthonormal forward transform. Input and output must be contiguous and match the plan size.
  void execute(ksj::array::VectorView<const std::complex<float>> input,
               ksj::array::VectorView<std::complex<float>> output) {
    if (input.size() != size_ || output.size() != size_) {
      throw std::invalid_argument("orthonormal forward FFT plan dimension mismatch");
    }
    if (!input.is_contiguous() || !output.is_contiguous()) {
      throw std::invalid_argument("orthonormal forward FFT plan requires contiguous input and output");
    }
    if (!descriptor_.compute(input, output)) {
      throw std::runtime_error("orthonormal forward FFT plan execution failed");
    }
  }

private:
  std::size_t size_{0};
  detail::intel::OrthonormalFft1Descriptor descriptor_{};
};

template <typename T>
void fft(ksj::array::VectorView<const std::complex<T>> input, ksj::array::VectorView<std::complex<T>> output,
         const Direction direction = Direction::forward, const Normalization normalization = Normalization::none) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("fft output dimension mismatch");
  }
  if (input.data() == output.data() && !input.empty()) {
    auto temp = ksj::array::make_pooled_vector<std::complex<T>>(input.size());
    fft(input, temp.view(), direction, normalization);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_intel_fft<T>(input.size()) && detail::intel::fft_1d(input, output, direction, normalization)) {
    return;
  }

  detail::eigen::fft_1d(input, output, direction, normalization);
}

template <typename T>
void fft(ksj::array::VectorView<std::complex<T>> input, ksj::array::VectorView<std::complex<T>> output,
         const Direction direction = Direction::forward, const Normalization normalization = Normalization::none) {
  fft(ksj::array::as_const_view(input), output, direction, normalization);
}

template <typename T>
void fft(const ComplexVector<T>& input, ComplexVector<T>& output, const Direction direction = Direction::forward,
         const Normalization normalization = Normalization::none) {
  fft(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

template <typename T>
void ifft(const ComplexVector<T>& input, ComplexVector<T>& output,
          const Normalization normalization = Normalization::inverse) {
  fft(input, output, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexVector<T> fft(ksj::array::VectorView<const std::complex<T>> input,
                                   const Direction direction = Direction::forward,
                                   const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_vector<std::complex<T>>(input.size());
  fft(input, output.view(), direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexVector<T> fft(ksj::array::VectorView<std::complex<T>> input,
                                   const Direction direction = Direction::forward,
                                   const Normalization normalization = Normalization::none) {
  return fft(ksj::array::as_const_view(input), direction, normalization);
}

template <typename T>
[[nodiscard]] ComplexVector<T> ifft(ksj::array::VectorView<const std::complex<T>> input,
                                    const Normalization normalization = Normalization::inverse) {
  return fft(input, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexVector<T> ifft(ksj::array::VectorView<std::complex<T>> input,
                                    const Normalization normalization = Normalization::inverse) {
  return ifft(ksj::array::as_const_view(input), normalization);
}

template <typename T>
[[nodiscard]] ComplexVector<T> fft(const ComplexVector<T>& input, const Direction direction = Direction::forward,
                                   const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_vector<std::complex<T>>(input.size());
  fft(input, output, direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexVector<T> ifft(const ComplexVector<T>& input,
                                    const Normalization normalization = Normalization::inverse) {
  return fft(input, Direction::inverse, normalization);
}

template <typename T>
void fft_inplace(ksj::array::VectorView<std::complex<T>> data, const Direction direction = Direction::forward,
                 const Normalization normalization = Normalization::none, const bool preshift = false,
                 const bool postshift = false) {
  auto shifted_input = preshift ? ksj::array::make_pooled_vector<std::complex<T>>(data.size()) : ComplexVector<T>{};
  auto transform_input = ksj::array::as_const_view(data);
  if (preshift) {
    ifftshift(ksj::array::as_const_view(data), shifted_input.view());
    transform_input = ksj::array::as_const_view(shifted_input.view());
  }

  if (postshift) {
    auto output = ksj::array::make_pooled_vector<std::complex<T>>(data.size());
    fft(transform_input, output.view(), direction, normalization);
    fftshift(ksj::array::as_const_view(output.view()), data);
    return;
  }

  fft(transform_input, data, direction, normalization);
}

template <typename T>
void ifft_inplace(ksj::array::VectorView<std::complex<T>> data,
                  const Normalization normalization = Normalization::inverse, const bool preshift = false,
                  const bool postshift = false) {
  fft_inplace(data, Direction::inverse, normalization, preshift, postshift);
}

template <typename T> class Fft1Plan {
public:
  Fft1Plan(std::size_t size, Direction direction = Direction::forward,
           Normalization normalization = Normalization::none)
      : size_(size), direction_(direction), normalization_(normalization) {
    if constexpr (std::is_same_v<T, float>) {
      ipp_descriptor_ready_ = normalization_ == Normalization::orthonormal && ipp_descriptor_.reset(size_, direction_);
    }
    descriptor_ready_ = descriptor_.reset(size_, direction_, normalization_);
    in_place_descriptor_ready_ = in_place_descriptor_.reset(size_, direction_, normalization_, true);
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] Direction direction() const noexcept { return direction_; }
  [[nodiscard]] Normalization normalization() const noexcept { return normalization_; }
  [[nodiscard]] bool has_cached_descriptor() const noexcept { return descriptor_ready_; }
  [[nodiscard]] bool has_cached_in_place_descriptor() const noexcept { return in_place_descriptor_ready_; }

  void execute(ksj::array::VectorView<const std::complex<T>> input, ksj::array::VectorView<std::complex<T>> output) {
    validate(input.size(), output.size());
    if (input.empty()) {
      return;
    }
    if (input.data() == output.data()) {
      auto temp = ksj::array::make_pooled_vector<std::complex<T>>(size_);
      execute(input, temp.view());
      ksj::array::copy(temp.view(), output);
      return;
    }

    if constexpr (std::is_same_v<T, float>) {
      if (ipp_descriptor_ready_ && input.is_contiguous() && output.is_contiguous() &&
          ipp_descriptor_.compute(input, output)) {
        return;
      }
    }

    if (descriptor_ready_ && input.is_contiguous() && output.is_contiguous() &&
        descriptor_.compute(input.data(), output.data())) {
      return;
    }

    detail::eigen::fft_1d(input, output, direction_, normalization_);
  }

  void execute(ksj::array::VectorView<std::complex<T>> input, ksj::array::VectorView<std::complex<T>> output) {
    execute(ksj::array::as_const_view(input), output);
  }

  void execute(ksj::array::VectorView<const std::complex<T>> input, ksj::array::VectorView<std::complex<T>> output,
               ComplexVector<T>& scratch) {
    validate(input.size(), output.size());
    if (input.empty()) {
      return;
    }
    if (input.data() == output.data()) {
      scratch.resize(size_);
      execute(input, scratch.view());
      ksj::array::copy(scratch.view(), output);
      return;
    }

    execute(input, output);
  }

  void execute(ksj::array::VectorView<std::complex<T>> input, ksj::array::VectorView<std::complex<T>> output,
               ComplexVector<T>& scratch) {
    execute(ksj::array::as_const_view(input), output, scratch);
  }

  void execute(const ComplexVector<T>& input, ComplexVector<T>& output) {
    execute(ksj::array::as_const_view(input.view()), output.view());
  }

  void execute_in_place(ksj::array::VectorView<std::complex<T>> data) {
    validate(data.size(), data.size());
    if (data.empty()) {
      return;
    }

    if (in_place_descriptor_ready_ && data.is_contiguous() && in_place_descriptor_.compute(data.data())) {
      return;
    }

    auto temp = ksj::array::make_pooled_vector<std::complex<T>>(size_);
    execute(ksj::array::as_const_view(data), temp.view());
    ksj::array::copy(temp.view(), data);
  }

  void execute_in_place(ksj::array::VectorView<std::complex<T>> data, ComplexVector<T>& scratch) {
    validate(data.size(), data.size());
    if (data.empty()) {
      return;
    }

    if (in_place_descriptor_ready_ && data.is_contiguous() && in_place_descriptor_.compute(data.data())) {
      return;
    }

    scratch.resize(size_);
    execute(ksj::array::as_const_view(data), scratch.view());
    ksj::array::copy(scratch.view(), data);
  }

private:
  void validate(const std::size_t input_size, const std::size_t output_size) const {
    if (input_size != size_ || output_size != size_) {
      throw std::invalid_argument("fft1 plan dimension mismatch");
    }
  }

  std::size_t size_{0};
  Direction direction_{Direction::forward};
  Normalization normalization_{Normalization::none};
  detail::intel::OrthonormalFft1Descriptor ipp_descriptor_{};
  detail::intel::Fft1Descriptor<T> descriptor_{};
  detail::intel::Fft1Descriptor<T> in_place_descriptor_{};
  bool ipp_descriptor_ready_{false};
  bool descriptor_ready_{false};
  bool in_place_descriptor_ready_{false};
};

template <typename T> struct Fft1Workspace {
  ComplexVector<T> packed_input;
  ComplexVector<T> shifted_input;
  ComplexVector<T> output;
  ComplexVector<T> plan_scratch;

  void clear() noexcept {
    packed_input.release();
    shifted_input.release();
    output.release();
    plan_scratch.release();
  }
};

namespace detail::algorithms {
template <typename T> inline void ensure_vector_size(ComplexVector<T>& vector, const std::size_t size) {
  if (vector.size() != size) {
    vector.resize(size);
  }
}

inline void validate_fft_segment_count(const std::size_t extent, const std::size_t segments,
                                       const char* function_name) {
  if (segments == 0U) {
    throw std::invalid_argument(std::string(function_name) + " segment count must be positive");
  }
  if (extent % segments != 0U) {
    throw std::invalid_argument(std::string(function_name) + " extent must be divisible by segment count");
  }
}

template <typename T>
[[nodiscard]] inline std::size_t matrix_dim_extent(const ksj::array::MatrixView<T> data, const ksj::array::Dim dim,
                                                   const char* function_name) {
  switch (dim) {
    case ksj::array::Dim::dim0:
      return data.rows();
    case ksj::array::Dim::dim1:
      return data.cols();
    default:
      throw std::invalid_argument(std::string(function_name) + " supports only matrix dim0 or dim1");
  }
}

template <typename T>
[[nodiscard]] inline ksj::array::VectorView<const std::complex<T>>
prepare_fft_input(ksj::array::VectorView<std::complex<T>> input, Fft1Workspace<T>& workspace, const bool preshift) {
  if (preshift) {
    ensure_vector_size(workspace.shifted_input, input.size());
    ifftshift(ksj::array::as_const_view(input), workspace.shifted_input.view());
    return ksj::array::as_const_view(workspace.shifted_input.view());
  }

  if (input.is_contiguous()) {
    return ksj::array::as_const_view(input);
  }

  return ksj::array::pack_contiguous(input, workspace.packed_input);
}

template <typename T>
inline void fft_view_inplace(ksj::array::VectorView<std::complex<T>> data, Fft1Workspace<T>& workspace,
                             Fft1Plan<T>& plan, const bool preshift, const bool postshift) {
  if (!preshift && !postshift && data.is_contiguous()) {
    plan.execute_in_place(data, workspace.plan_scratch);
    return;
  }

  const auto transform_input = prepare_fft_input(data, workspace, preshift);
  if (!postshift && data.is_contiguous()) {
    plan.execute(transform_input, data, workspace.plan_scratch);
    return;
  }

  ensure_vector_size(workspace.output, data.size());
  plan.execute(transform_input, workspace.output.view(), workspace.plan_scratch);
  if (postshift) {
    fftshift(ksj::array::as_const_view(workspace.output.view()), data);
  } else {
    ksj::array::copy(workspace.output.view(), data);
  }
}

template <typename T>
[[nodiscard]] inline std::size_t cube_dim_extent(const ksj::array::CubeView<T> data, const ksj::array::Dim dim,
                                                 const char* function_name) {
  switch (dim) {
    case ksj::array::Dim::dim0:
      return data.dim0();
    case ksj::array::Dim::dim1:
      return data.dim1();
    case ksj::array::Dim::dim2:
      return data.dim2();
    default:
      throw std::invalid_argument(std::string(function_name) + " supports only cube dim0, dim1, or dim2");
  }
}

template <typename InputT, typename OutputT, typename LineFunction>
inline void for_each_cube_line(const ksj::array::CubeView<InputT> input, const ksj::array::CubeView<OutputT> output,
                               const ksj::array::Dim dim, LineFunction&& function) {
  switch (dim) {
    case ksj::array::Dim::dim0:
      for (std::size_t dim1 = 0U; dim1 < input.dim1(); ++dim1) {
        for (std::size_t dim2 = 0U; dim2 < input.dim2(); ++dim2) {
          function(input.subview(ksj::array::_, dim1, dim2), output.subview(ksj::array::_, dim1, dim2));
        }
      }
      return;
    case ksj::array::Dim::dim1:
      for (std::size_t dim0 = 0U; dim0 < input.dim0(); ++dim0) {
        for (std::size_t dim2 = 0U; dim2 < input.dim2(); ++dim2) {
          function(input.subview(dim0, ksj::array::_, dim2), output.subview(dim0, ksj::array::_, dim2));
        }
      }
      return;
    case ksj::array::Dim::dim2:
      for (std::size_t dim0 = 0U; dim0 < input.dim0(); ++dim0) {
        for (std::size_t dim1 = 0U; dim1 < input.dim1(); ++dim1) {
          function(input.subview(dim0, dim1, ksj::array::_), output.subview(dim0, dim1, ksj::array::_));
        }
      }
      return;
    default:
      throw std::invalid_argument("cube line iteration supports only cube dim0, dim1, or dim2");
  }
}

template <typename T, typename LineFunction>
inline void for_each_cube_line(const ksj::array::CubeView<T> data, const ksj::array::Dim dim, LineFunction&& function) {
  switch (dim) {
    case ksj::array::Dim::dim0:
      for (std::size_t dim1 = 0U; dim1 < data.dim1(); ++dim1) {
        for (std::size_t dim2 = 0U; dim2 < data.dim2(); ++dim2) {
          function(data.subview(ksj::array::_, dim1, dim2));
        }
      }
      return;
    case ksj::array::Dim::dim1:
      for (std::size_t dim0 = 0U; dim0 < data.dim0(); ++dim0) {
        for (std::size_t dim2 = 0U; dim2 < data.dim2(); ++dim2) {
          function(data.subview(dim0, ksj::array::_, dim2));
        }
      }
      return;
    case ksj::array::Dim::dim2:
      for (std::size_t dim0 = 0U; dim0 < data.dim0(); ++dim0) {
        for (std::size_t dim1 = 0U; dim1 < data.dim1(); ++dim1) {
          function(data.subview(dim0, dim1, ksj::array::_));
        }
      }
      return;
    default:
      throw std::invalid_argument("cube line iteration supports only cube dim0, dim1, or dim2");
  }
}
} // namespace detail::algorithms

template <typename T>
void fft(ksj::array::MatrixView<const std::complex<T>> input, ksj::array::MatrixView<std::complex<T>> output,
         const ksj::array::Dim dim, const Direction direction = Direction::forward,
         const Normalization normalization = Normalization::none) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("fft output dimension mismatch");
  }
  const auto length = detail::algorithms::matrix_dim_extent(input, dim, "fft");
  if (input.empty() || length == 0U) {
    return;
  }
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
    fft(input, temp.view(), dim, direction, normalization);
    ksj::array::copy(temp.view(), output);
    return;
  }

  Fft1Workspace<T> workspace;
  auto plan = Fft1Plan<T>(length, direction, normalization);
  if (dim == ksj::array::Dim::dim1) {
    for (std::size_t row = 0U; row < input.rows(); ++row) {
      plan.execute(input.row(row), output.row(row), workspace.plan_scratch);
    }
    return;
  }

  for (std::size_t col = 0U; col < input.cols(); ++col) {
    plan.execute(input.col(col), output.col(col), workspace.plan_scratch);
  }
}

template <typename T>
void fft(ksj::array::MatrixView<std::complex<T>> input, ksj::array::MatrixView<std::complex<T>> output,
         const ksj::array::Dim dim, const Direction direction = Direction::forward,
         const Normalization normalization = Normalization::none) {
  fft(ksj::array::as_const_view(input), output, dim, direction, normalization);
}

template <typename T>
void fft(const ComplexMatrix<T>& input, ComplexMatrix<T>& output, const ksj::array::Dim dim,
         const Direction direction = Direction::forward, const Normalization normalization = Normalization::none) {
  fft(ksj::array::as_const_view(input.view()), output.view(), dim, direction, normalization);
}

template <typename T>
void ifft(const ComplexMatrix<T>& input, ComplexMatrix<T>& output, const ksj::array::Dim dim,
          const Normalization normalization = Normalization::inverse) {
  fft(input, output, dim, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> fft(ksj::array::MatrixView<const std::complex<T>> input, const ksj::array::Dim dim,
                                   const Direction direction = Direction::forward,
                                   const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
  fft(input, output.view(), dim, direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> fft(ksj::array::MatrixView<std::complex<T>> input, const ksj::array::Dim dim,
                                   const Direction direction = Direction::forward,
                                   const Normalization normalization = Normalization::none) {
  return fft(ksj::array::as_const_view(input), dim, direction, normalization);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> ifft(ksj::array::MatrixView<const std::complex<T>> input, const ksj::array::Dim dim,
                                    const Normalization normalization = Normalization::inverse) {
  return fft(input, dim, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> ifft(ksj::array::MatrixView<std::complex<T>> input, const ksj::array::Dim dim,
                                    const Normalization normalization = Normalization::inverse) {
  return ifft(ksj::array::as_const_view(input), dim, normalization);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> fft(const ComplexMatrix<T>& input, const ksj::array::Dim dim,
                                   const Direction direction = Direction::forward,
                                   const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
  fft(input, output, dim, direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> ifft(const ComplexMatrix<T>& input, const ksj::array::Dim dim,
                                    const Normalization normalization = Normalization::inverse) {
  return fft(input, dim, Direction::inverse, normalization);
}

// Applies independent 1D complex transforms to every cube line along dim0, dim1, or dim2. View output overloads
// write the caller-provided cube; return-value overloads allocate a same-shaped PooledCube. Input and output shapes
// must match, and an aliased input/output view is handled through a temporary cube.
template <typename T>
void fft(ksj::array::CubeView<const std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
         const ksj::array::Dim dim, const Direction direction = Direction::forward,
         const Normalization normalization = Normalization::none) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
    throw std::invalid_argument("cube axis fft output dimension mismatch");
  }
  const auto length = detail::algorithms::cube_dim_extent(input, dim, "fft");
  if (input.empty() || length == 0U) {
    return;
  }
  if (input.data() == output.data()) {
    auto temporary = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
    fft(input, temporary.view(), dim, direction, normalization);
    ksj::array::copy(temporary.view(), output);
    return;
  }

  Fft1Workspace<T> workspace;
  auto plan = Fft1Plan<T>(length, direction, normalization);
  detail::algorithms::for_each_cube_line(input, output, dim,
                                         [&plan, &workspace](const auto input_line, const auto output_line) {
                                           plan.execute(input_line, output_line, workspace.plan_scratch);
                                         });
}

template <typename T>
void fft(ksj::array::CubeView<std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
         const ksj::array::Dim dim, const Direction direction = Direction::forward,
         const Normalization normalization = Normalization::none) {
  fft(ksj::array::as_const_view(input), output, dim, direction, normalization);
}

template <typename T>
void fft(const ComplexCube<T>& input, ComplexCube<T>& output, const ksj::array::Dim dim,
         const Direction direction = Direction::forward, const Normalization normalization = Normalization::none) {
  fft(ksj::array::as_const_view(input.view()), output.view(), dim, direction, normalization);
}

template <typename T>
void ifft(ksj::array::CubeView<const std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
          const ksj::array::Dim dim, const Normalization normalization = Normalization::inverse) {
  fft(input, output, dim, Direction::inverse, normalization);
}

template <typename T>
void ifft(ksj::array::CubeView<std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
          const ksj::array::Dim dim, const Normalization normalization = Normalization::inverse) {
  ifft(ksj::array::as_const_view(input), output, dim, normalization);
}

template <typename T>
void ifft(const ComplexCube<T>& input, ComplexCube<T>& output, const ksj::array::Dim dim,
          const Normalization normalization = Normalization::inverse) {
  ifft(ksj::array::as_const_view(input.view()), output.view(), dim, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft(ksj::array::CubeView<const std::complex<T>> input, const ksj::array::Dim dim,
                                 const Direction direction = Direction::forward,
                                 const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  fft(input, output.view(), dim, direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft(ksj::array::CubeView<std::complex<T>> input, const ksj::array::Dim dim,
                                 const Direction direction = Direction::forward,
                                 const Normalization normalization = Normalization::none) {
  return fft(ksj::array::as_const_view(input), dim, direction, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft(ksj::array::CubeView<const std::complex<T>> input, const ksj::array::Dim dim,
                                  const Normalization normalization = Normalization::inverse) {
  return fft(input, dim, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft(ksj::array::CubeView<std::complex<T>> input, const ksj::array::Dim dim,
                                  const Normalization normalization = Normalization::inverse) {
  return ifft(ksj::array::as_const_view(input), dim, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft(const ComplexCube<T>& input, const ksj::array::Dim dim,
                                 const Direction direction = Direction::forward,
                                 const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  fft(input, output, dim, direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft(const ComplexCube<T>& input, const ksj::array::Dim dim,
                                  const Normalization normalization = Normalization::inverse) {
  return fft(input, dim, Direction::inverse, normalization);
}

template <typename T> class PolicyFft1Executor {
public:
  void execute_inplace(ksj::array::VectorView<std::complex<T>> data, const Direction direction = Direction::forward,
                       const Normalization normalization = Normalization::none, const bool preshift = false,
                       const bool postshift = false) {
    if (data.empty()) {
      return;
    }

    const auto transform_input = detail::algorithms::prepare_fft_input(data, workspace_, preshift);
    if (!postshift && data.is_contiguous()) {
      fft(transform_input, data, direction, normalization);
      return;
    }

    detail::algorithms::ensure_vector_size(workspace_.output, data.size());
    fft(transform_input, workspace_.output.view(), direction, normalization);
    if (postshift) {
      fftshift(ksj::array::as_const_view(workspace_.output.view()), data);
    } else {
      ksj::array::copy(workspace_.output.view(), data);
    }
  }

  [[nodiscard]] Fft1Workspace<T>& workspace() noexcept { return workspace_; }
  [[nodiscard]] const Fft1Workspace<T>& workspace() const noexcept { return workspace_; }

  void clear_workspace() noexcept { workspace_.clear(); }

private:
  Fft1Workspace<T> workspace_;
};

template <typename T> class Fft1Executor {
public:
  void execute_inplace(ksj::array::VectorView<std::complex<T>> data, const Direction direction = Direction::forward,
                       const Normalization normalization = Normalization::none, const bool preshift = false,
                       const bool postshift = false) {
    if (data.empty()) {
      return;
    }
    auto& plan = ensure_plan(data.size(), direction, normalization);
    detail::algorithms::fft_view_inplace(data, workspace_, plan, preshift, postshift);
  }

  void execute_inplace(ksj::array::MatrixView<std::complex<T>> data, const ksj::array::Dim dim,
                       const Direction direction = Direction::forward,
                       const Normalization normalization = Normalization::none, const bool preshift = false,
                       const bool postshift = false) {
    if (data.empty()) {
      return;
    }

    const auto length = detail::algorithms::matrix_dim_extent(data, dim, "Fft1Executor::execute_inplace");
    if (length == 0U) {
      return;
    }

    auto& plan = ensure_plan(length, direction, normalization);
    if (dim == ksj::array::Dim::dim1) {
      for (std::size_t row = 0U; row < data.rows(); ++row) {
        detail::algorithms::fft_view_inplace(data.row(row), workspace_, plan, preshift, postshift);
      }
      return;
    }

    for (std::size_t col = 0U; col < data.cols(); ++col) {
      detail::algorithms::fft_view_inplace(data.col(col), workspace_, plan, preshift, postshift);
    }
  }

  // Reuses the cached 1D plan and workspace while transforming every line of a cube along the selected axis.
  void execute_inplace(ksj::array::CubeView<std::complex<T>> data, const ksj::array::Dim dim,
                       const Direction direction = Direction::forward,
                       const Normalization normalization = Normalization::none, const bool preshift = false,
                       const bool postshift = false) {
    const auto length = detail::algorithms::cube_dim_extent(data, dim, "Fft1Executor::execute_inplace");
    if (data.empty() || length == 0U) {
      return;
    }

    auto& plan = ensure_plan(length, direction, normalization);
    detail::algorithms::for_each_cube_line(data, dim, [this, &plan, preshift, postshift](const auto line) {
      detail::algorithms::fft_view_inplace(line, workspace_, plan, preshift, postshift);
    });
  }

  void execute_segmented_inplace(ksj::array::VectorView<std::complex<T>> data, const std::size_t segments,
                                 const Direction direction = Direction::forward,
                                 const Normalization normalization = Normalization::none, const bool preshift = false,
                                 const bool postshift = false) {
    detail::algorithms::validate_fft_segment_count(data.size(), segments, "Fft1Executor::execute_segmented_inplace");
    if (data.empty()) {
      return;
    }

    const auto segment_size = data.size() / segments;
    auto& plan = ensure_plan(segment_size, direction, normalization);
    for (std::size_t segment = 0U; segment < segments; ++segment) {
      const auto segment_start = segment * segment_size;
      detail::algorithms::fft_view_inplace(data.subview(ksj::array::slice(segment_start, segment_start + segment_size)),
                                           workspace_, plan, preshift, postshift);
    }
  }

  void execute_segmented_inplace(ksj::array::MatrixView<std::complex<T>> data, const ksj::array::Dim dim,
                                 const std::size_t segments, const Direction direction = Direction::forward,
                                 const Normalization normalization = Normalization::none, const bool preshift = false,
                                 const bool postshift = false) {
    const auto extent = detail::algorithms::matrix_dim_extent(data, dim, "Fft1Executor::execute_segmented_inplace");
    detail::algorithms::validate_fft_segment_count(extent, segments, "Fft1Executor::execute_segmented_inplace");
    if (data.empty()) {
      return;
    }

    const auto segment_size = extent / segments;
    auto& plan = ensure_plan(segment_size, direction, normalization);
    if (dim == ksj::array::Dim::dim1) {
      for (std::size_t row = 0U; row < data.rows(); ++row) {
        auto row_view = data.row(row);
        for (std::size_t segment = 0U; segment < segments; ++segment) {
          const auto segment_start = segment * segment_size;
          detail::algorithms::fft_view_inplace(
            row_view.subview(ksj::array::slice(segment_start, segment_start + segment_size)), workspace_, plan,
            preshift, postshift);
        }
      }
      return;
    }

    for (std::size_t col = 0U; col < data.cols(); ++col) {
      auto col_view = data.col(col);
      for (std::size_t segment = 0U; segment < segments; ++segment) {
        const auto segment_start = segment * segment_size;
        detail::algorithms::fft_view_inplace(
          col_view.subview(ksj::array::slice(segment_start, segment_start + segment_size)), workspace_, plan, preshift,
          postshift);
      }
    }
  }

  [[nodiscard]] Fft1Workspace<T>& workspace() noexcept { return workspace_; }
  [[nodiscard]] const Fft1Workspace<T>& workspace() const noexcept { return workspace_; }

  void clear_workspace() noexcept { workspace_.clear(); }
  void reset_plan() noexcept { plan_.reset(); }

private:
  [[nodiscard]] Fft1Plan<T>& ensure_plan(const std::size_t size, const Direction direction,
                                         const Normalization normalization) {
    if (!plan_.has_value() || plan_->size() != size || plan_->direction() != direction ||
        plan_->normalization() != normalization) {
      plan_.emplace(size, direction, normalization);
    }
    return *plan_;
  }

  Fft1Workspace<T> workspace_;
  std::optional<Fft1Plan<T>> plan_;
};

template <typename T>
void fft_inplace(ksj::array::MatrixView<std::complex<T>> data, const ksj::array::Dim dim,
                 const Direction direction = Direction::forward,
                 const Normalization normalization = Normalization::none, const bool preshift = false,
                 const bool postshift = false) {
  if (data.empty()) {
    return;
  }

  const auto length = detail::algorithms::matrix_dim_extent(data, dim, "fft_inplace");
  if (length == 0U) {
    return;
  }

  Fft1Workspace<T> workspace;
  auto plan = Fft1Plan<T>(length, direction, normalization);
  if (dim == ksj::array::Dim::dim1) {
    for (std::size_t row = 0U; row < data.rows(); ++row) {
      detail::algorithms::fft_view_inplace(data.row(row), workspace, plan, preshift, postshift);
    }
    return;
  }

  for (std::size_t col = 0U; col < data.cols(); ++col) {
    detail::algorithms::fft_view_inplace(data.col(col), workspace, plan, preshift, postshift);
  }
}

template <typename T>
void ifft_inplace(ksj::array::MatrixView<std::complex<T>> data, const ksj::array::Dim dim,
                  const Normalization normalization = Normalization::inverse, const bool preshift = false,
                  const bool postshift = false) {
  fft_inplace(data, dim, Direction::inverse, normalization, preshift, postshift);
}

// Transforms every 1D cube line along dim0, dim1, or dim2 in place. preshift applies ifftshift before each line's
// transform and postshift applies fftshift afterwards.
template <typename T>
void fft_inplace(ksj::array::CubeView<std::complex<T>> data, const ksj::array::Dim dim,
                 const Direction direction = Direction::forward,
                 const Normalization normalization = Normalization::none, const bool preshift = false,
                 const bool postshift = false) {
  const auto length = detail::algorithms::cube_dim_extent(data, dim, "fft_inplace");
  if (data.empty() || length == 0U) {
    return;
  }

  Fft1Workspace<T> workspace;
  auto plan = Fft1Plan<T>(length, direction, normalization);
  detail::algorithms::for_each_cube_line(data, dim, [&workspace, &plan, preshift, postshift](const auto line) {
    detail::algorithms::fft_view_inplace(line, workspace, plan, preshift, postshift);
  });
}

template <typename T>
void ifft_inplace(ksj::array::CubeView<std::complex<T>> data, const ksj::array::Dim dim,
                  const Normalization normalization = Normalization::inverse, const bool preshift = false,
                  const bool postshift = false) {
  fft_inplace(data, dim, Direction::inverse, normalization, preshift, postshift);
}

template <typename T>
void fft_segmented_inplace(ksj::array::VectorView<std::complex<T>> data, const std::size_t segments,
                           const Direction direction = Direction::forward,
                           const Normalization normalization = Normalization::none, const bool preshift = false,
                           const bool postshift = false) {
  detail::algorithms::validate_fft_segment_count(data.size(), segments, "fft_segmented_inplace");
  if (data.empty()) {
    return;
  }

  const auto segment_size = data.size() / segments;
  Fft1Workspace<T> workspace;
  auto plan = Fft1Plan<T>(segment_size, direction, normalization);
  for (std::size_t segment = 0U; segment < segments; ++segment) {
    const auto segment_start = segment * segment_size;
    detail::algorithms::fft_view_inplace(data.subview(ksj::array::slice(segment_start, segment_start + segment_size)),
                                         workspace, plan, preshift, postshift);
  }
}

template <typename T>
void fft_segmented_inplace(ksj::array::MatrixView<std::complex<T>> data, const ksj::array::Dim dim,
                           const std::size_t segments, const Direction direction = Direction::forward,
                           const Normalization normalization = Normalization::none, const bool preshift = false,
                           const bool postshift = false) {
  const auto extent = detail::algorithms::matrix_dim_extent(data, dim, "fft_segmented_inplace");
  detail::algorithms::validate_fft_segment_count(extent, segments, "fft_segmented_inplace");
  if (data.empty()) {
    return;
  }

  const auto segment_size = extent / segments;
  Fft1Workspace<T> workspace;
  auto plan = Fft1Plan<T>(segment_size, direction, normalization);
  if (dim == ksj::array::Dim::dim1) {
    for (std::size_t row = 0U; row < data.rows(); ++row) {
      auto row_view = data.row(row);
      for (std::size_t segment = 0U; segment < segments; ++segment) {
        const auto segment_start = segment * segment_size;
        detail::algorithms::fft_view_inplace(
          row_view.subview(ksj::array::slice(segment_start, segment_start + segment_size)), workspace, plan, preshift,
          postshift);
      }
    }
    return;
  }

  for (std::size_t col = 0U; col < data.cols(); ++col) {
    auto col_view = data.col(col);
    for (std::size_t segment = 0U; segment < segments; ++segment) {
      const auto segment_start = segment * segment_size;
      detail::algorithms::fft_view_inplace(
        col_view.subview(ksj::array::slice(segment_start, segment_start + segment_size)), workspace, plan, preshift,
        postshift);
    }
  }
}

template <typename T>
void fft2(ksj::array::MatrixView<const std::complex<T>> input, ksj::array::MatrixView<std::complex<T>> output,
          const Direction direction = Direction::forward, const Normalization normalization = Normalization::none) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("fft2 output dimension mismatch");
  }
  if (input.data() == output.data() && !input.empty()) {
    auto temp = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
    fft2(input, temp.view(), direction, normalization);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_intel_fft2<T>(input.rows(), input.cols()) &&
      detail::intel::fft_2d(input, output, direction, normalization)) {
    return;
  }

  detail::eigen::fft_2d(input, output, direction, normalization);
}

template <typename T>
void fft2(ksj::array::MatrixView<std::complex<T>> input, ksj::array::MatrixView<std::complex<T>> output,
          const Direction direction = Direction::forward, const Normalization normalization = Normalization::none) {
  fft2(ksj::array::as_const_view(input), output, direction, normalization);
}

template <typename T>
void fft2(const ComplexMatrix<T>& input, ComplexMatrix<T>& output, const Direction direction = Direction::forward,
          const Normalization normalization = Normalization::none) {
  fft2(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

template <typename T>
void ifft2(const ComplexMatrix<T>& input, ComplexMatrix<T>& output,
           const Normalization normalization = Normalization::inverse) {
  fft2(input, output, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> fft2(ksj::array::MatrixView<const std::complex<T>> input,
                                    const Direction direction = Direction::forward,
                                    const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
  fft2(input, output.view(), direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> fft2(ksj::array::MatrixView<std::complex<T>> input,
                                    const Direction direction = Direction::forward,
                                    const Normalization normalization = Normalization::none) {
  return fft2(ksj::array::as_const_view(input), direction, normalization);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> ifft2(ksj::array::MatrixView<const std::complex<T>> input,
                                     const Normalization normalization = Normalization::inverse) {
  return fft2(input, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> ifft2(ksj::array::MatrixView<std::complex<T>> input,
                                     const Normalization normalization = Normalization::inverse) {
  return ifft2(ksj::array::as_const_view(input), normalization);
}

template <typename T> class Fft2Executor {
public:
  void execute(ksj::array::MatrixView<const std::complex<T>> input, ksj::array::MatrixView<std::complex<T>> output,
               const Direction direction = Direction::forward,
               const Normalization normalization = Normalization::none) {
    if (input.rows() != output.rows() || input.cols() != output.cols()) {
      throw std::invalid_argument("fft2 executor output dimension mismatch");
    }
    if (input.empty()) {
      return;
    }

    if (input.data() != output.data() && detail::prefer_intel_fft2<T>(input.rows(), input.cols()) &&
        input.row_stride() == output.row_stride() && input.col_stride() == output.col_stride() &&
        ensure_descriptor(input, direction, normalization).compute(input.data(), output.data())) {
      return;
    }

    fft2(input, output, direction, normalization);
  }

  void execute(ksj::array::MatrixView<std::complex<T>> input, ksj::array::MatrixView<std::complex<T>> output,
               const Direction direction = Direction::forward,
               const Normalization normalization = Normalization::none) {
    execute(ksj::array::as_const_view(input), output, direction, normalization);
  }

  void execute_inplace(ksj::array::MatrixView<std::complex<T>> data, const Direction direction = Direction::forward,
                       const Normalization normalization = Normalization::none) {
    if (data.empty()) {
      return;
    }
    if (in_place_output_.rows() != data.rows() || in_place_output_.cols() != data.cols()) {
      in_place_output_.resize(data.rows(), data.cols());
    }
    execute(ksj::array::as_const_view(data), in_place_output_.view(), direction, normalization);
    ksj::array::copy(in_place_output_.view(), data);
  }

  void reset() noexcept {
    descriptor_ = {};
    in_place_output_.release();
    rows_ = 0U;
    cols_ = 0U;
    row_stride_ = 0U;
    col_stride_ = 0U;
  }

private:
  [[nodiscard]] detail::intel::Fft2Descriptor<T>& ensure_descriptor(ksj::array::MatrixView<const std::complex<T>> input,
                                                                    const Direction direction,
                                                                    const Normalization normalization) {
    if (!descriptor_.ready() || rows_ != input.rows() || cols_ != input.cols() || row_stride_ != input.row_stride() ||
        col_stride_ != input.col_stride() || direction_ != direction || normalization_ != normalization) {
      rows_ = input.rows();
      cols_ = input.cols();
      row_stride_ = input.row_stride();
      col_stride_ = input.col_stride();
      direction_ = direction;
      normalization_ = normalization;
      (void)descriptor_.reset_with_strides(rows_, cols_, direction_, normalization_, row_stride_, col_stride_);
    }
    return descriptor_;
  }

  detail::intel::Fft2Descriptor<T> descriptor_{};
  ComplexMatrix<T> in_place_output_{};
  std::size_t rows_{0U};
  std::size_t cols_{0U};
  std::size_t row_stride_{0U};
  std::size_t col_stride_{0U};
  Direction direction_{Direction::forward};
  Normalization normalization_{Normalization::none};
};

template <typename T> class CenteredFft2Executor {
public:
  void execute_inplace(ksj::array::MatrixView<std::complex<T>> matrix, const Direction direction, const bool preshift,
                       const bool postshift) {
    if (matrix.empty()) {
      return;
    }
    validate_input(matrix);

    const auto logical_rows = matrix.rows();
    const auto logical_cols = matrix.cols();
    ensure_shape(logical_rows, logical_cols);

    if (is_row_major_contiguous(matrix)) {
      execute_contiguous(matrix.data(), direction, preshift, postshift);
      return;
    }

    pack(matrix, preshift);
    execute_storage(direction);
    unpack(matrix, postshift);
  }

  void reset() noexcept {
    descriptor_ = {};
    storage_.release();
    logical_rows_ = 0U;
    logical_cols_ = 0U;
  }

private:
  static bool is_row_major_contiguous(const ksj::array::MatrixView<std::complex<T>> input) noexcept {
    return input.row_stride() == input.cols() && input.col_stride() == 1U;
  }

  static std::size_t checked_product(const std::size_t lhs, const std::size_t rhs, const char* description) {
    if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
      throw std::length_error(std::string(description) + " overflows size_t");
    }
    return lhs * rhs;
  }

  static void validate_input(const ksj::array::MatrixView<std::complex<T>> input) {
    if (input.data() == nullptr) {
      throw std::invalid_argument("centered fft2 input data is null");
    }

    const auto logical_rows = input.rows();
    const auto logical_cols = input.cols();
    (void)checked_product(logical_rows, logical_cols, "centered fft2 element count");

    const auto max_row_offset =
      checked_product(logical_rows - 1U, input.row_stride(), "centered fft2 input row offset");
    const auto max_col_offset =
      checked_product(logical_cols - 1U, input.col_stride(), "centered fft2 input column offset");
    if (max_col_offset > std::numeric_limits<std::size_t>::max() - max_row_offset) {
      throw std::length_error("centered fft2 input offset overflows size_t");
    }
  }

  void ensure_shape(const std::size_t logical_rows, const std::size_t logical_cols) {
    if (descriptor_.ready() && logical_rows_ == logical_rows && logical_cols_ == logical_cols &&
        storage_.rows() == logical_rows && storage_.cols() == logical_cols) {
      return;
    }

    if (logical_rows > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) ||
        logical_cols > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) ||
        checked_product(logical_rows, logical_cols, "centered fft2 element count") >
          static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
      throw std::length_error("centered fft2 dimensions exceed supported limits");
    }

    if (!descriptor_.reset(logical_rows, logical_cols)) {
      descriptor_ = {};
      logical_rows_ = 0U;
      logical_cols_ = 0U;
      storage_.release();
      throw std::runtime_error("centered fft2 descriptor creation failed");
    }

    storage_.resize(logical_rows, logical_cols);
    logical_rows_ = logical_rows;
    logical_cols_ = logical_cols;
  }

  void execute_storage(const Direction direction) {
    if (!descriptor_.compute(storage_.data(), direction)) {
      descriptor_ = {};
      throw std::runtime_error("centered fft2 compute failed");
    }
  }

  void execute_contiguous(std::complex<T>* data, const Direction direction, const bool preshift, const bool postshift) {
    if (preshift) {
      shift_left(data, logical_rows_, logical_cols_, logical_rows_ / 2U, logical_cols_ / 2U);
    }

    if (!descriptor_.compute(data, direction)) {
      descriptor_ = {};
      throw std::runtime_error("centered fft2 compute failed");
    }

    if (postshift) {
      shift_left(data, logical_rows_, logical_cols_, (logical_rows_ + 1U) / 2U, (logical_cols_ + 1U) / 2U);
    }
  }

  void pack(const ksj::array::MatrixView<std::complex<T>> input, const bool preshift) noexcept {
    const auto logical_rows = input.rows();
    const auto logical_cols = input.cols();
    if (!preshift) {
      pack_unshifted(input, logical_rows, logical_cols);
      return;
    }

    const auto row_shift = logical_rows / 2U;
    const auto col_shift = logical_cols / 2U;
    for (std::size_t row = 0U; row < logical_rows; ++row) {
      const auto storage_row = left_shift_destination_index(row, logical_rows, row_shift);
      auto* storage_row_data = storage_.data() + storage_row * logical_cols;
      const auto* input_row = input.data() + row * input.row_stride();
      pack_shifted_row(input_row, input.col_stride(), storage_row_data, logical_cols, col_shift);
    }
  }

  void pack_unshifted(const ksj::array::MatrixView<std::complex<T>> input, const std::size_t logical_rows,
                      const std::size_t logical_cols) noexcept {
    for (std::size_t row = 0U; row < logical_rows; ++row) {
      const auto* input_row = input.data() + row * input.row_stride();
      auto* storage_row = storage_.data() + row * logical_cols;
      for (std::size_t col = 0U; col < logical_cols; ++col) {
        storage_row[col] = input_row[col * input.col_stride()];
      }
    }
  }

  void unpack(const ksj::array::MatrixView<std::complex<T>> output, const bool postshift) const noexcept {
    const auto logical_rows = output.rows();
    const auto logical_cols = output.cols();
    if (!postshift) {
      unpack_unshifted(output, logical_rows, logical_cols);
      return;
    }

    const auto row_shift = (logical_rows + 1U) / 2U;
    const auto col_shift = (logical_cols + 1U) / 2U;
    for (std::size_t row = 0U; row < logical_rows; ++row) {
      auto* output_row = output.data() + row * output.row_stride();
      const auto storage_row = left_shift_source_index(row, logical_rows, row_shift);
      const auto* storage_row_data = storage_.data() + storage_row * logical_cols;
      unpack_shifted_row(storage_row_data, output_row, output.col_stride(), logical_cols, col_shift);
    }
  }

  void unpack_unshifted(const ksj::array::MatrixView<std::complex<T>> output, const std::size_t logical_rows,
                        const std::size_t logical_cols) const noexcept {
    for (std::size_t row = 0U; row < logical_rows; ++row) {
      auto* output_row = output.data() + row * output.row_stride();
      const auto* storage_row = storage_.data() + row * logical_cols;
      for (std::size_t col = 0U; col < logical_cols; ++col) {
        output_row[col * output.col_stride()] = storage_row[col];
      }
    }
  }

  static void pack_shifted_row(const std::complex<T>* input_row, const std::size_t input_col_stride,
                               std::complex<T>* storage_row, const std::size_t logical_cols,
                               const std::size_t col_shift) noexcept {
    const auto normalized_shift = col_shift % logical_cols;
    for (std::size_t col = normalized_shift; col < logical_cols; ++col) {
      storage_row[col - normalized_shift] = input_row[col * input_col_stride];
    }
    for (std::size_t col = 0U; col < normalized_shift; ++col) {
      storage_row[logical_cols - normalized_shift + col] = input_row[col * input_col_stride];
    }
  }

  static void unpack_shifted_row(const std::complex<T>* storage_row, std::complex<T>* output_row,
                                 const std::size_t output_col_stride, const std::size_t logical_cols,
                                 const std::size_t col_shift) noexcept {
    const auto normalized_shift = col_shift % logical_cols;
    const auto first_segment_cols = logical_cols - normalized_shift;
    for (std::size_t col = 0U; col < first_segment_cols; ++col) {
      output_row[col * output_col_stride] = storage_row[col + normalized_shift];
    }
    for (std::size_t col = first_segment_cols; col < logical_cols; ++col) {
      output_row[col * output_col_stride] = storage_row[col - first_segment_cols];
    }
  }

  static std::size_t left_shift_destination_index(const std::size_t index, const std::size_t size,
                                                  const std::size_t shift) noexcept {
    const auto normalized_shift = shift % size;
    return index >= normalized_shift ? index - normalized_shift : index + size - normalized_shift;
  }

  static std::size_t left_shift_source_index(const std::size_t index, const std::size_t size,
                                             const std::size_t shift) noexcept {
    const auto normalized_shift = shift % size;
    const auto shifted_index = index + normalized_shift;
    return shifted_index < size ? shifted_index : shifted_index - size;
  }

  static void shift_left(std::complex<T>* data, const std::size_t backend_rows, const std::size_t backend_cols,
                         const std::size_t row_shift, const std::size_t col_shift) noexcept {
    const auto normalized_row_shift = row_shift % backend_rows;
    const auto normalized_col_shift = col_shift % backend_cols;
    if (shift_even_half(data, backend_rows, backend_cols, normalized_row_shift, normalized_col_shift)) {
      return;
    }

    if (normalized_row_shift != 0U) {
      std::rotate(data, data + normalized_row_shift * backend_cols, data + backend_rows * backend_cols);
    }

    if (normalized_col_shift == 0U) {
      return;
    }
    for (std::size_t row = 0U; row < backend_rows; ++row) {
      auto* row_begin = data + row * backend_cols;
      std::rotate(row_begin, row_begin + normalized_col_shift, row_begin + backend_cols);
    }
  }

  static bool shift_even_half(std::complex<T>* data, const std::size_t rows, const std::size_t cols,
                              const std::size_t row_shift, const std::size_t col_shift) noexcept {
    constexpr std::size_t kMaxQuadrantSwapElements = 256U * 256U;
    if (rows % 2U != 0U || cols % 2U != 0U || row_shift != rows / 2U || col_shift != cols / 2U) {
      return false;
    }
    if (rows > kMaxQuadrantSwapElements / cols) {
      return false;
    }

    const auto half_rows = rows / 2U;
    const auto half_cols = cols / 2U;
    for (std::size_t row = 0U; row < half_rows; ++row) {
      auto* top = data + row * cols;
      auto* bottom = data + (row + half_rows) * cols;
      for (std::size_t col = 0U; col < half_cols; ++col) {
        std::swap(top[col], bottom[col + half_cols]);
        std::swap(top[col + half_cols], bottom[col]);
      }
    }
    return true;
  }

  ComplexMatrix<T> storage_{};
  detail::intel::CenteredFft2Descriptor<T> descriptor_{};
  std::size_t logical_rows_{0U};
  std::size_t logical_cols_{0U};
};

template <typename T>
[[nodiscard]] ComplexMatrix<T> fft2(const ComplexMatrix<T>& input, const Direction direction = Direction::forward,
                                    const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_matrix<std::complex<T>>(input.rows(), input.cols());
  fft2(input, output, direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexMatrix<T> ifft2(const ComplexMatrix<T>& input,
                                     const Normalization normalization = Normalization::inverse) {
  return fft2(input, Direction::inverse, normalization);
}

template <typename T>
void fft2_inplace(ksj::array::MatrixView<std::complex<T>> data, const Direction direction = Direction::forward,
                  const Normalization normalization = Normalization::none, const bool preshift = false,
                  const bool postshift = false) {
  auto shifted_input =
    preshift ? ksj::array::make_pooled_matrix<std::complex<T>>(data.rows(), data.cols()) : ComplexMatrix<T>{};
  auto transform_input = ksj::array::as_const_view(data);
  if (preshift) {
    ifftshift(ksj::array::as_const_view(data), shifted_input.view());
    transform_input = ksj::array::as_const_view(shifted_input.view());
  }

  if (postshift) {
    auto output = ksj::array::make_pooled_matrix<std::complex<T>>(data.rows(), data.cols());
    fft2(transform_input, output.view(), direction, normalization);
    fftshift(ksj::array::as_const_view(output.view()), data);
    return;
  }

  fft2(transform_input, data, direction, normalization);
}

template <typename T>
void ifft2_inplace(ksj::array::MatrixView<std::complex<T>> data,
                   const Normalization normalization = Normalization::inverse, const bool preshift = false,
                   const bool postshift = false) {
  fft2_inplace(data, Direction::inverse, normalization, preshift, postshift);
}

template <typename T>
void fft2_batch(const ComplexCube<T>& input, ComplexCube<T>& output, const Direction direction = Direction::forward,
                const Normalization normalization = Normalization::none) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
    throw std::invalid_argument("fft2 batch output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }
  if (input.data() == output.data() && !input.empty()) {
    auto temp = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
    fft2_batch(input, temp, direction, normalization);
    detail::algorithms::copy_storage(temp, output);
    return;
  }

  if (detail::prefer_intel_fft2_batch<T>(input.dim0(), input.dim1(), input.dim2()) &&
      detail::intel::fft_2d_batch(input, output, direction, normalization)) {
    return;
  }

  detail::eigen::fft_2d_batch(input, output, direction, normalization);
}

template <typename T>
void ifft2_batch(const ComplexCube<T>& input, ComplexCube<T>& output,
                 const Normalization normalization = Normalization::inverse) {
  fft2_batch(input, output, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft2_batch(const ComplexCube<T>& input, const Direction direction = Direction::forward,
                                        const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  fft2_batch(input, output, direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft2_batch(const ComplexCube<T>& input,
                                         const Normalization normalization = Normalization::inverse) {
  return fft2_batch(input, Direction::inverse, normalization);
}

template <typename T>
void fft3(ksj::array::CubeView<const std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
          const Direction direction = Direction::forward, const Normalization normalization = Normalization::none) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
    throw std::invalid_argument("fft3 output dimension mismatch");
  }
  if (input.data() == output.data() && !input.empty()) {
    auto temp = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
    fft3(input, temp.view(), direction, normalization);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_intel_fft3<T>(input.dim0(), input.dim1(), input.dim2()) &&
      detail::intel::fft_3d(input, output, direction, normalization)) {
    return;
  }

  detail::eigen::fft_3d(input, output, direction, normalization);
}

template <typename T>
void fft3(const ComplexCube<T>& input, ComplexCube<T>& output, const Direction direction = Direction::forward,
          const Normalization normalization = Normalization::none) {
  fft3(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

template <typename T>
void ifft3(ksj::array::CubeView<const std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
           const Normalization normalization = Normalization::inverse) {
  fft3(input, output, Direction::inverse, normalization);
}

template <typename T>
void ifft3(const ComplexCube<T>& input, ComplexCube<T>& output,
           const Normalization normalization = Normalization::inverse) {
  ifft3(ksj::array::as_const_view(input.view()), output.view(), normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft3(ksj::array::CubeView<const std::complex<T>> input,
                                  const Direction direction = Direction::forward,
                                  const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  fft3(input, output.view(), direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft3(ksj::array::CubeView<std::complex<T>> input,
                                  const Direction direction = Direction::forward,
                                  const Normalization normalization = Normalization::none) {
  return fft3(ksj::array::as_const_view(input), direction, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft3(ksj::array::CubeView<const std::complex<T>> input,
                                   const Normalization normalization = Normalization::inverse) {
  return fft3(input, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft3(ksj::array::CubeView<std::complex<T>> input,
                                   const Normalization normalization = Normalization::inverse) {
  return ifft3(ksj::array::as_const_view(input), normalization);
}

template <typename T>
void fft3_centered(ksj::array::CubeView<const std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
                   const Direction direction = Direction::forward,
                   const Normalization normalization = Normalization::none) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
    throw std::invalid_argument("fft3 centered output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  auto shifted_input = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  fftshift(input, shifted_input.view());
  fft3(ksj::array::as_const_view(shifted_input.view()), output, direction, normalization);
  fftshift_in_place(output);
}

template <typename T>
void fft3_centered(ksj::array::CubeView<std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
                   const Direction direction = Direction::forward,
                   const Normalization normalization = Normalization::none) {
  fft3_centered(ksj::array::as_const_view(input), output, direction, normalization);
}

template <typename T>
void ifft3_centered(ksj::array::CubeView<const std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
                    const Normalization normalization = Normalization::inverse) {
  fft3_centered(input, output, Direction::inverse, normalization);
}

template <typename T>
void ifft3_centered(ksj::array::CubeView<std::complex<T>> input, ksj::array::CubeView<std::complex<T>> output,
                    const Normalization normalization = Normalization::inverse) {
  ifft3_centered(ksj::array::as_const_view(input), output, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft3_centered(ksj::array::CubeView<const std::complex<T>> input,
                                           const Direction direction = Direction::forward,
                                           const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  fft3_centered(input, output.view(), direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft3_centered(ksj::array::CubeView<std::complex<T>> input,
                                           const Direction direction = Direction::forward,
                                           const Normalization normalization = Normalization::none) {
  return fft3_centered(ksj::array::as_const_view(input), direction, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft3_centered(ksj::array::CubeView<const std::complex<T>> input,
                                            const Normalization normalization = Normalization::inverse) {
  return fft3_centered(input, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft3_centered(ksj::array::CubeView<std::complex<T>> input,
                                            const Normalization normalization = Normalization::inverse) {
  return ifft3_centered(ksj::array::as_const_view(input), normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft3_centered(const ComplexCube<T>& input, const Direction direction = Direction::forward,
                                           const Normalization normalization = Normalization::none) {
  return fft3_centered(ksj::array::as_const_view(input.view()), direction, normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft3_centered(const ComplexCube<T>& input,
                                            const Normalization normalization = Normalization::inverse) {
  return ifft3_centered(ksj::array::as_const_view(input.view()), normalization);
}

template <typename T>
[[nodiscard]] ComplexCube<T> fft3(const ComplexCube<T>& input, const Direction direction = Direction::forward,
                                  const Normalization normalization = Normalization::none) {
  auto output = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
  fft3(input, output, direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexCube<T> ifft3(const ComplexCube<T>& input,
                                   const Normalization normalization = Normalization::inverse) {
  return fft3(input, Direction::inverse, normalization);
}

template <typename T>
void fft3_batch(const ComplexArray4D<T>& input, ComplexArray4D<T>& output,
                const Direction direction = Direction::forward,
                const Normalization normalization = Normalization::none) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2() ||
      input.dim3() != output.dim3()) {
    throw std::invalid_argument("fft3 batch output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }
  if (input.data() == output.data() && !input.empty()) {
    auto temp =
      ksj::array::make_pooled_array4d<std::complex<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
    fft3_batch(input, temp, direction, normalization);
    detail::algorithms::copy_storage(temp, output);
    return;
  }

  if (detail::prefer_intel_fft3_batch<T>(input.dim0(), input.dim1(), input.dim2(), input.dim3()) &&
      detail::intel::fft_3d_batch(input, output, direction, normalization)) {
    return;
  }

  detail::eigen::fft_3d_batch(input, output, direction, normalization);
}

template <typename T>
void ifft3_batch(const ComplexArray4D<T>& input, ComplexArray4D<T>& output,
                 const Normalization normalization = Normalization::inverse) {
  fft3_batch(input, output, Direction::inverse, normalization);
}

template <typename T>
void fft3_volume_batch(ksj::array::Array4DView<const std::complex<T>> input,
                       ksj::array::Array4DView<std::complex<T>> output, const Direction direction = Direction::forward,
                       const Normalization normalization = Normalization::none) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2() ||
      input.dim3() != output.dim3()) {
    throw std::invalid_argument("fft3 volume batch output dimension mismatch");
  }

  for (std::size_t batch = 0U; batch < input.dim0(); ++batch) {
    fft3(input.subview(batch, ksj::array::_, ksj::array::_, ksj::array::_),
         output.subview(batch, ksj::array::_, ksj::array::_, ksj::array::_), direction, normalization);
  }
}

template <typename T>
void fft3_volume_batch(ksj::array::Array4DView<std::complex<T>> input, ksj::array::Array4DView<std::complex<T>> output,
                       const Direction direction = Direction::forward,
                       const Normalization normalization = Normalization::none) {
  fft3_volume_batch(ksj::array::as_const_view(input), output, direction, normalization);
}

template <typename T>
void ifft3_volume_batch(ksj::array::Array4DView<const std::complex<T>> input,
                        ksj::array::Array4DView<std::complex<T>> output,
                        const Normalization normalization = Normalization::inverse) {
  fft3_volume_batch(input, output, Direction::inverse, normalization);
}

template <typename T>
void ifft3_volume_batch(ksj::array::Array4DView<std::complex<T>> input, ksj::array::Array4DView<std::complex<T>> output,
                        const Normalization normalization = Normalization::inverse) {
  ifft3_volume_batch(ksj::array::as_const_view(input), output, normalization);
}

template <typename T>
[[nodiscard]] ComplexArray4D<T> fft3_volume_batch(ksj::array::Array4DView<const std::complex<T>> input,
                                                  const Direction direction = Direction::forward,
                                                  const Normalization normalization = Normalization::none) {
  auto output =
    ksj::array::make_pooled_array4d<std::complex<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  fft3_volume_batch(input, output.view(), direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexArray4D<T> fft3_volume_batch(ksj::array::Array4DView<std::complex<T>> input,
                                                  const Direction direction = Direction::forward,
                                                  const Normalization normalization = Normalization::none) {
  return fft3_volume_batch(ksj::array::as_const_view(input), direction, normalization);
}

template <typename T>
[[nodiscard]] ComplexArray4D<T> ifft3_volume_batch(ksj::array::Array4DView<const std::complex<T>> input,
                                                   const Normalization normalization = Normalization::inverse) {
  return fft3_volume_batch(input, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexArray4D<T> ifft3_volume_batch(ksj::array::Array4DView<std::complex<T>> input,
                                                   const Normalization normalization = Normalization::inverse) {
  return ifft3_volume_batch(ksj::array::as_const_view(input), normalization);
}

template <typename T>
void fft3_centered_volume_batch_in_place_input(ksj::array::Array4DView<std::complex<T>> input,
                                               ksj::array::Array4DView<std::complex<T>> output,
                                               const Direction direction = Direction::forward,
                                               const Normalization normalization = Normalization::none) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2() ||
      input.dim3() != output.dim3()) {
    throw std::invalid_argument("fft3 centered volume batch output dimension mismatch");
  }

  for (std::size_t batch = 0U; batch < input.dim0(); ++batch) {
    auto input_volume = input.subview(batch, ksj::array::_, ksj::array::_, ksj::array::_);
    auto output_volume = output.subview(batch, ksj::array::_, ksj::array::_, ksj::array::_);
    fftshift_in_place(input_volume);
    fft3(ksj::array::as_const_view(input_volume), output_volume, direction, normalization);
    fftshift_in_place(output_volume);
  }
}

template <typename T>
void ifft3_centered_volume_batch_in_place_input(ksj::array::Array4DView<std::complex<T>> input,
                                                ksj::array::Array4DView<std::complex<T>> output,
                                                const Normalization normalization = Normalization::inverse) {
  fft3_centered_volume_batch_in_place_input(input, output, Direction::inverse, normalization);
}

template <typename T>
[[nodiscard]] ComplexArray4D<T> fft3_batch(const ComplexArray4D<T>& input,
                                           const Direction direction = Direction::forward,
                                           const Normalization normalization = Normalization::none) {
  auto output =
    ksj::array::make_pooled_array4d<std::complex<T>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  fft3_batch(input, output, direction, normalization);
  return output;
}

template <typename T>
[[nodiscard]] ComplexArray4D<T> ifft3_batch(const ComplexArray4D<T>& input,
                                            const Normalization normalization = Normalization::inverse) {
  return fft3_batch(input, Direction::inverse, normalization);
}

template <typename T> class Fft2Plan {
public:
  Fft2Plan(std::size_t rows, std::size_t cols, Direction direction = Direction::forward,
           Normalization normalization = Normalization::none)
      : rows_(rows), cols_(cols), direction_(direction), normalization_(normalization) {
    descriptor_ready_ = descriptor_.reset(rows_, cols_, direction_, normalization_);
  }

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] Direction direction() const noexcept { return direction_; }
  [[nodiscard]] Normalization normalization() const noexcept { return normalization_; }
  [[nodiscard]] bool has_cached_descriptor() const noexcept { return descriptor_ready_; }

  void execute(const ComplexMatrix<T>& input, ComplexMatrix<T>& output) {
    validate(input.rows(), input.cols());
    validate(output.rows(), output.cols());
    if (!input.empty() && input.data() == output.data()) {
      auto temp = ksj::array::make_pooled_matrix<std::complex<T>>(rows_, cols_);
      execute(input, temp);
      detail::algorithms::copy_storage(temp, output);
      return;
    }

    if (descriptor_ready_ && descriptor_.compute(input, output)) {
      return;
    }

    detail::eigen::fft_2d(input, output, direction_, normalization_);
  }

  void execute_batch(const ComplexCube<T>& input, ComplexCube<T>& output) {
    validate(input.dim0(), input.dim1());
    validate(output.dim0(), output.dim1());
    if (input.dim2() != output.dim2()) {
      throw std::invalid_argument("fft2 batch plan output dimension mismatch");
    }
    if (input.empty()) {
      return;
    }
    if (input.data() == output.data()) {
      auto temp = ksj::array::make_pooled_cube<std::complex<T>>(rows_, cols_, input.dim2());
      execute_batch(input, temp);
      detail::algorithms::copy_storage(temp, output);
      return;
    }

    if (detail::prefer_intel_fft2_batch<T>(rows_, cols_, input.dim2()) &&
        detail::intel::fft_2d_batch(input, output, direction_, normalization_)) {
      return;
    }

    detail::eigen::fft_2d_batch(input, output, direction_, normalization_);
  }

private:
  void validate(const std::size_t rows, const std::size_t cols) const {
    if (rows != rows_ || cols != cols_) {
      throw std::invalid_argument("fft2 plan dimension mismatch");
    }
  }

  std::size_t rows_{0};
  std::size_t cols_{0};
  Direction direction_{Direction::forward};
  Normalization normalization_{Normalization::none};
  detail::intel::Fft2Descriptor<T> descriptor_{};
  bool descriptor_ready_{false};
};

template <typename T> class Fft3Plan {
public:
  Fft3Plan(std::size_t rows, std::size_t cols, std::size_t slices, Direction direction = Direction::forward,
           Normalization normalization = Normalization::none)
      : rows_(rows), cols_(cols), slices_(slices), direction_(direction), normalization_(normalization),
        batch_input_(ksj::array::make_pooled_cube<std::complex<T>>(rows, cols, slices)),
        batch_output_(ksj::array::make_pooled_cube<std::complex<T>>(rows, cols, slices)) {
    descriptor_ready_ = descriptor_.reset(rows_, cols_, slices_, direction_, normalization_);
  }

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t slices() const noexcept { return slices_; }
  [[nodiscard]] Direction direction() const noexcept { return direction_; }
  [[nodiscard]] Normalization normalization() const noexcept { return normalization_; }
  [[nodiscard]] bool has_cached_descriptor() const noexcept { return descriptor_ready_; }

  void execute(const ComplexCube<T>& input, ComplexCube<T>& output) {
    validate(input.dim0(), input.dim1(), input.dim2());
    validate(output.dim0(), output.dim1(), output.dim2());
    if (input.data() == output.data()) {
      auto temp = ksj::array::make_pooled_cube<std::complex<T>>(rows_, cols_, slices_);
      execute(input, temp);
      detail::algorithms::copy_storage(temp, output);
      return;
    }

    if (descriptor_ready_ && descriptor_.compute(input, output)) {
      return;
    }

    detail::eigen::fft_3d(input, output, direction_, normalization_);
  }

  void execute_batch(const ComplexArray4D<T>& input, ComplexArray4D<T>& output) {
    validate(input.dim0(), input.dim1(), input.dim2());
    validate(output.dim0(), output.dim1(), output.dim2());
    if (input.dim3() != output.dim3()) {
      throw std::invalid_argument("fft3 batch plan output dimension mismatch");
    }
    if (input.empty()) {
      return;
    }
    if (input.data() == output.data() && !input.empty()) {
      auto temp = ksj::array::make_pooled_array4d<std::complex<T>>(rows_, cols_, slices_, input.dim3());
      execute_batch(input, temp);
      detail::algorithms::copy_storage(temp, output);
      return;
    }

    if (descriptor_ready_) {
      for (std::size_t batch = 0; batch < input.dim3(); ++batch) {
        for (std::size_t row = 0; row < rows_; ++row) {
          for (std::size_t col = 0; col < cols_; ++col) {
            for (std::size_t slice = 0; slice < slices_; ++slice) {
              batch_input_(row, col, slice) = input(row, col, slice, batch);
            }
          }
        }
        if (!descriptor_.compute(batch_input_, batch_output_)) {
          descriptor_ready_ = false;
          break;
        }
        for (std::size_t row = 0; row < rows_; ++row) {
          for (std::size_t col = 0; col < cols_; ++col) {
            for (std::size_t slice = 0; slice < slices_; ++slice) {
              output(row, col, slice, batch) = batch_output_(row, col, slice);
            }
          }
        }
      }
      if (descriptor_ready_) {
        return;
      }
    }

    if (detail::intel::fft_3d_batch(input, output, direction_, normalization_)) {
      return;
    }

    detail::eigen::fft_3d_batch(input, output, direction_, normalization_);
  }

private:
  void validate(const std::size_t rows, const std::size_t cols, const std::size_t slices) const {
    if (rows != rows_ || cols != cols_ || slices != slices_) {
      throw std::invalid_argument("fft3 plan dimension mismatch");
    }
  }

  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t slices_{0};
  Direction direction_{Direction::forward};
  Normalization normalization_{Normalization::none};
  ComplexCube<T> batch_input_;
  ComplexCube<T> batch_output_;
  detail::intel::Fft3Descriptor<T> descriptor_{};
  bool descriptor_ready_{false};
};

} // namespace ksj::fft
