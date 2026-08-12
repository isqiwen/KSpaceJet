#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/fft/detail/intel/intel_fft_types.hpp"
#include "kspacejet/fft/types.hpp"

#include <complex>
#include <cstddef>
#include <memory>

namespace ksj::fft::detail::intel {

class OrthonormalFft1Descriptor {
public:
  OrthonormalFft1Descriptor() = default;

  OrthonormalFft1Descriptor(const OrthonormalFft1Descriptor&) = delete;
  OrthonormalFft1Descriptor& operator=(const OrthonormalFft1Descriptor&) = delete;

  OrthonormalFft1Descriptor(OrthonormalFft1Descriptor&&) noexcept = default;
  OrthonormalFft1Descriptor& operator=(OrthonormalFft1Descriptor&&) noexcept = default;

  [[nodiscard]] bool reset(std::size_t size, Direction direction);
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool compute(ksj::array::VectorView<const std::complex<float>> input,
                             ksj::array::VectorView<std::complex<float>> output) noexcept;

private:
  std::shared_ptr<void> impl_{};
};

template <typename T> class Fft1Descriptor {
public:
  Fft1Descriptor() = default;

  Fft1Descriptor(const Fft1Descriptor&) = delete;
  Fft1Descriptor& operator=(const Fft1Descriptor&) = delete;

  Fft1Descriptor(Fft1Descriptor&&) noexcept = default;
  Fft1Descriptor& operator=(Fft1Descriptor&&) noexcept = default;

  [[nodiscard]] bool reset(std::size_t size, Direction direction, Normalization normalization, bool in_place = false);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] Direction direction() const noexcept;
  [[nodiscard]] Normalization normalization() const noexcept;
  [[nodiscard]] bool in_place() const noexcept;
  [[nodiscard]] bool compute(const ksj::array::PooledVector<std::complex<T>>& input,
                             ksj::array::PooledVector<std::complex<T>>& output) const;
  [[nodiscard]] bool compute(const std::complex<T>* input, std::complex<T>* output) const;
  [[nodiscard]] bool compute(std::complex<T>* data) const;

private:
  std::shared_ptr<void> impl_{};
};

template <typename T> class Fft2Descriptor {
public:
  Fft2Descriptor() = default;

  Fft2Descriptor(const Fft2Descriptor&) = delete;
  Fft2Descriptor& operator=(const Fft2Descriptor&) = delete;

  Fft2Descriptor(Fft2Descriptor&&) noexcept = default;
  Fft2Descriptor& operator=(Fft2Descriptor&&) noexcept = default;

  [[nodiscard]] bool reset(std::size_t rows, std::size_t cols, Direction direction, Normalization normalization);
  [[nodiscard]] bool reset_with_strides(std::size_t rows, std::size_t cols, Direction direction,
                                        Normalization normalization, std::size_t row_stride, std::size_t col_stride);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] std::size_t rows() const noexcept;
  [[nodiscard]] std::size_t cols() const noexcept;
  [[nodiscard]] Direction direction() const noexcept;
  [[nodiscard]] Normalization normalization() const noexcept;
  [[nodiscard]] bool compute(const ksj::array::PooledMatrix<std::complex<T>>& input,
                             ksj::array::PooledMatrix<std::complex<T>>& output) const;
  [[nodiscard]] bool compute(const std::complex<T>* input, std::complex<T>* output) const;

private:
  std::shared_ptr<void> impl_{};
};

template <typename T> class CenteredFft2Descriptor {
public:
  CenteredFft2Descriptor() = default;

  CenteredFft2Descriptor(const CenteredFft2Descriptor&) = delete;
  CenteredFft2Descriptor& operator=(const CenteredFft2Descriptor&) = delete;

  CenteredFft2Descriptor(CenteredFft2Descriptor&&) noexcept = default;
  CenteredFft2Descriptor& operator=(CenteredFft2Descriptor&&) noexcept = default;

  [[nodiscard]] bool reset(std::size_t rows, std::size_t cols);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] std::size_t rows() const noexcept;
  [[nodiscard]] std::size_t cols() const noexcept;
  [[nodiscard]] bool compute(std::complex<T>* data, Direction direction) const;

private:
  std::shared_ptr<void> impl_{};
};

template <typename T> class Fft3Descriptor {
public:
  Fft3Descriptor() = default;

  Fft3Descriptor(const Fft3Descriptor&) = delete;
  Fft3Descriptor& operator=(const Fft3Descriptor&) = delete;

  Fft3Descriptor(Fft3Descriptor&&) noexcept = default;
  Fft3Descriptor& operator=(Fft3Descriptor&&) noexcept = default;

  [[nodiscard]] bool reset(std::size_t rows, std::size_t cols, std::size_t slices, Direction direction,
                           Normalization normalization);
  [[nodiscard]] bool reset_with_strides(std::size_t rows, std::size_t cols, std::size_t slices, Direction direction,
                                        Normalization normalization, std::size_t row_stride, std::size_t col_stride,
                                        std::size_t slice_stride);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] std::size_t rows() const noexcept;
  [[nodiscard]] std::size_t cols() const noexcept;
  [[nodiscard]] std::size_t slices() const noexcept;
  [[nodiscard]] Direction direction() const noexcept;
  [[nodiscard]] Normalization normalization() const noexcept;
  [[nodiscard]] bool compute(const ksj::array::PooledCube<std::complex<T>>& input,
                             ksj::array::PooledCube<std::complex<T>>& output) const;
  [[nodiscard]] bool compute(ksj::array::CubeView<const std::complex<T>> input,
                             ksj::array::CubeView<std::complex<T>> output) const;
  [[nodiscard]] bool compute(const std::complex<T>* input, std::complex<T>* output) const;

private:
  std::shared_ptr<void> impl_{};
};

} // namespace ksj::fft::detail::intel
