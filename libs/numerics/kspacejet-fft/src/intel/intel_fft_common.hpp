#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/fft/detail/intel/intel_fft_descriptors.hpp"
#include "kspacejet/fft/detail/intel/intel_fft_transforms.hpp"
#include "kspacejet/fft/types.hpp"

#include <complex>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>

#include <ipp.h>
#include <mkl_dfti.h>

namespace ksj::fft::detail::intel_impl {

template <typename T> inline constexpr bool dfti_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;

static_assert(sizeof(std::complex<float>) == sizeof(Ipp32fc));
static_assert(alignof(std::complex<float>) >= alignof(Ipp32fc));
static_assert(std::is_trivially_copyable_v<std::complex<float>>);
static_assert(std::is_trivially_copyable_v<Ipp32fc>);
static_assert(sizeof(Ipp32f) == sizeof(float));
static_assert(sizeof(Ipp32fc) == 2U * sizeof(float));
static_assert(offsetof(Ipp32fc, re) == 0U);
static_assert(offsetof(Ipp32fc, im) == sizeof(float));

struct IppMemoryDeleter {
  void operator()(void* memory) const noexcept {
    if (memory != nullptr) {
      ippFree(memory);
    }
  }
};

using IppMemory = std::unique_ptr<Ipp8u, IppMemoryDeleter>;

[[nodiscard]] inline IppMemory allocate_ipp_memory(const int byte_count) {
  if (byte_count <= 0) {
    return {};
  }
  return IppMemory(static_cast<Ipp8u*>(ippMalloc(byte_count)));
}

inline bool check_status(const IppStatus status) noexcept {
  return status == ippStsNoErr;
}

template <typename SizeT> [[nodiscard]] constexpr bool fits_int(const SizeT value) noexcept {
  return value <= static_cast<SizeT>(std::numeric_limits<int>::max());
}

class OrthonormalFft1Descriptor {
public:
  OrthonormalFft1Descriptor() = default;

  OrthonormalFft1Descriptor(const OrthonormalFft1Descriptor&) = delete;
  OrthonormalFft1Descriptor& operator=(const OrthonormalFft1Descriptor&) = delete;

  OrthonormalFft1Descriptor(OrthonormalFft1Descriptor&&) = delete;
  OrthonormalFft1Descriptor& operator=(OrthonormalFft1Descriptor&&) = delete;

  [[nodiscard]] bool reset(const std::size_t size, const Direction direction) {
    size_ = size;
    direction_ = direction;
    fft_spec_ = nullptr;
    dft_spec_ = nullptr;
    spec_memory_.reset();
    init_memory_.reset();
    work_memory_.reset();
    is_power_of_two_ = false;

    if (size == 0U || !fits_int(size)) {
      return false;
    }

    is_power_of_two_ = (size & (size - 1U)) == 0U;
    int spec_size = 0;
    int init_size = 0;
    int work_size = 0;

    if (is_power_of_two_) {
      int order = 0;
      for (auto length = size; length > 1U; length >>= 1U) {
        ++order;
      }
      if (!check_status(
            ippsFFTGetSize_C_32fc(order, IPP_FFT_DIV_BY_SQRTN, ippAlgHintFast, &spec_size, &init_size, &work_size))) {
        return false;
      }
    } else if (!check_status(ippsDFTGetSize_C_32fc(static_cast<int>(size), IPP_FFT_DIV_BY_SQRTN, ippAlgHintFast,
                                                   &spec_size, &init_size, &work_size))) {
      return false;
    }

    if (spec_size <= 0 || init_size < 0 || work_size < 0) {
      return false;
    }

    spec_memory_ = allocate_ipp_memory(spec_size);
    init_memory_ = allocate_ipp_memory(init_size);
    work_memory_ = allocate_ipp_memory(work_size);
    if (spec_memory_ == nullptr || (init_size > 0 && init_memory_ == nullptr) ||
        (work_size > 0 && work_memory_ == nullptr)) {
      return false;
    }

    if (is_power_of_two_) {
      int order = 0;
      for (auto length = size; length > 1U; length >>= 1U) {
        ++order;
      }
      return check_status(ippsFFTInit_C_32fc(&fft_spec_, order, IPP_FFT_DIV_BY_SQRTN, ippAlgHintFast,
                                             spec_memory_.get(), init_memory_.get())) &&
             fft_spec_ != nullptr;
    }

    dft_spec_ = reinterpret_cast<IppsDFTSpec_C_32fc*>(spec_memory_.get());
    return check_status(
      ippsDFTInit_C_32fc(static_cast<int>(size), IPP_FFT_DIV_BY_SQRTN, ippAlgHintFast, dft_spec_, init_memory_.get()));
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool ready() const noexcept { return is_power_of_two_ ? fft_spec_ != nullptr : dft_spec_ != nullptr; }

  [[nodiscard]] bool compute(ksj::array::VectorView<const std::complex<float>> input,
                             ksj::array::VectorView<std::complex<float>> output) noexcept {
    if (!ready() || input.size() != size_ || output.size() != size_ || input.empty() || !input.is_contiguous() ||
        !output.is_contiguous()) {
      return false;
    }

    const auto* source = reinterpret_cast<const Ipp32fc*>(input.data());
    auto* destination = reinterpret_cast<Ipp32fc*>(output.data());
    const auto status =
      direction_ == Direction::forward
        ? (is_power_of_two_ ? ippsFFTFwd_CToC_32fc(source, destination, fft_spec_, work_memory_.get())
                            : ippsDFTFwd_CToC_32fc(source, destination, dft_spec_, work_memory_.get()))
        : (is_power_of_two_ ? ippsFFTInv_CToC_32fc(source, destination, fft_spec_, work_memory_.get())
                            : ippsDFTInv_CToC_32fc(source, destination, dft_spec_, work_memory_.get()));
    return check_status(status);
  }

private:
  std::size_t size_{0};
  Direction direction_{Direction::forward};
  bool is_power_of_two_{false};
  IppsFFTSpec_C_32fc* fft_spec_{nullptr};
  IppsDFTSpec_C_32fc* dft_spec_{nullptr};
  IppMemory spec_memory_{};
  IppMemory init_memory_{};
  IppMemory work_memory_{};
};

template <typename T> [[nodiscard]] constexpr DFTI_CONFIG_VALUE dfti_precision() {
  if constexpr (std::is_same_v<T, float>) {
    return DFTI_SINGLE;
  } else {
    return DFTI_DOUBLE;
  }
}

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

inline bool check_status(const MKL_LONG status) noexcept {
  return status == DFTI_NO_ERROR;
}

template <typename T> class Fft1Descriptor {
public:
  Fft1Descriptor() = default;

  Fft1Descriptor(const Fft1Descriptor&) = delete;
  Fft1Descriptor& operator=(const Fft1Descriptor&) = delete;

  Fft1Descriptor(Fft1Descriptor&& other) noexcept
      : descriptor_(other.descriptor_), size_(other.size_), direction_(other.direction_),
        normalization_(other.normalization_), in_place_(other.in_place_) {
    other.descriptor_ = nullptr;
    other.size_ = 0;
    other.in_place_ = false;
  }

  Fft1Descriptor& operator=(Fft1Descriptor&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    release();
    descriptor_ = other.descriptor_;
    size_ = other.size_;
    direction_ = other.direction_;
    normalization_ = other.normalization_;
    in_place_ = other.in_place_;
    other.descriptor_ = nullptr;
    other.size_ = 0;
    other.in_place_ = false;
    return *this;
  }

  ~Fft1Descriptor() { release(); }

  [[nodiscard]] bool reset(const std::size_t size, const Direction direction, const Normalization normalization,
                           const bool in_place = false) {
    release();
    size_ = size;
    direction_ = direction;
    normalization_ = normalization;
    in_place_ = in_place;

    if constexpr (!dfti_scalar_v<T>) {
      return false;
    } else {
      if (size == 0U) {
        return false;
      }

      const auto descriptor_size = static_cast<MKL_LONG>(size);
      auto status = DftiCreateDescriptor(&descriptor_, dfti_precision<T>(), DFTI_COMPLEX, 1, descriptor_size);
      if (!check_status(status)) {
        descriptor_ = nullptr;
        return false;
      }

      const auto scale = normalization_scale<T>(size, direction, normalization);
      const auto scale_parameter = direction == Direction::forward ? DFTI_FORWARD_SCALE : DFTI_BACKWARD_SCALE;
      status = DftiSetValue(descriptor_, DFTI_PLACEMENT, in_place_ ? DFTI_INPLACE : DFTI_NOT_INPLACE);
      status = check_status(status) ? DftiSetValue(descriptor_, scale_parameter, static_cast<double>(scale)) : status;
      status = check_status(status) ? DftiCommitDescriptor(descriptor_) : status;
      if (!check_status(status)) {
        release();
        return false;
      }

      return true;
    }
  }

  [[nodiscard]] bool ready() const noexcept { return descriptor_ != nullptr; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] Direction direction() const noexcept { return direction_; }
  [[nodiscard]] Normalization normalization() const noexcept { return normalization_; }
  [[nodiscard]] bool in_place() const noexcept { return in_place_; }

  [[nodiscard]] bool compute(const ksj::array::PooledVector<std::complex<T>>& input,
                             ksj::array::PooledVector<std::complex<T>>& output) const {
    if (!ready() || input.size() != size_ || output.size() != size_) {
      return false;
    }

    return compute(input.data(), output.data());
  }

  [[nodiscard]] bool compute(const std::complex<T>* input, std::complex<T>* output) const {
    if (!ready() || in_place_ || input == nullptr || output == nullptr) {
      return false;
    }

    auto* source = const_cast<std::complex<T>*>(input);
    const auto status = direction_ == Direction::forward ? DftiComputeForward(descriptor_, source, output)
                                                         : DftiComputeBackward(descriptor_, source, output);
    return check_status(status);
  }

  [[nodiscard]] bool compute(std::complex<T>* data) const {
    if (!ready() || !in_place_ || data == nullptr) {
      return false;
    }

    const auto status =
      direction_ == Direction::forward ? DftiComputeForward(descriptor_, data) : DftiComputeBackward(descriptor_, data);
    return check_status(status);
  }

private:
  void release() noexcept {
    if (descriptor_ == nullptr) {
      return;
    }
    (void)DftiFreeDescriptor(&descriptor_);
    descriptor_ = nullptr;
  }

  DFTI_DESCRIPTOR_HANDLE descriptor_{nullptr};
  std::size_t size_{0};
  Direction direction_{Direction::forward};
  Normalization normalization_{Normalization::none};
  bool in_place_{false};
};

template <typename T> class Fft2Descriptor {
public:
  Fft2Descriptor() = default;

  Fft2Descriptor(const Fft2Descriptor&) = delete;
  Fft2Descriptor& operator=(const Fft2Descriptor&) = delete;

  Fft2Descriptor(Fft2Descriptor&& other) noexcept
      : descriptor_(other.descriptor_), rows_(other.rows_), cols_(other.cols_), direction_(other.direction_),
        normalization_(other.normalization_) {
    other.descriptor_ = nullptr;
    other.rows_ = 0;
    other.cols_ = 0;
  }

  Fft2Descriptor& operator=(Fft2Descriptor&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    release();
    descriptor_ = other.descriptor_;
    rows_ = other.rows_;
    cols_ = other.cols_;
    direction_ = other.direction_;
    normalization_ = other.normalization_;
    other.descriptor_ = nullptr;
    other.rows_ = 0;
    other.cols_ = 0;
    return *this;
  }

  ~Fft2Descriptor() { release(); }

  [[nodiscard]] bool reset(const std::size_t rows, const std::size_t cols, const Direction direction,
                           const Normalization normalization) {
    return reset_impl(rows, cols, direction, normalization, cols, 1U);
  }

  [[nodiscard]] bool reset_with_strides(const std::size_t rows, const std::size_t cols, const Direction direction,
                                        const Normalization normalization, const std::size_t row_stride,
                                        const std::size_t col_stride) {
    return reset_impl(rows, cols, direction, normalization, row_stride, col_stride);
  }

private:
  [[nodiscard]] bool reset_impl(const std::size_t rows, const std::size_t cols, const Direction direction,
                                const Normalization normalization, const std::size_t row_stride,
                                const std::size_t col_stride) {
    release();
    rows_ = rows;
    cols_ = cols;
    direction_ = direction;
    normalization_ = normalization;

    if constexpr (!dfti_scalar_v<T>) {
      return false;
    } else {
      if (rows == 0U || cols == 0U) {
        return false;
      }

      const MKL_LONG dimensions[2] = {static_cast<MKL_LONG>(rows), static_cast<MKL_LONG>(cols)};
      auto status = DftiCreateDescriptor(&descriptor_, dfti_precision<T>(), DFTI_COMPLEX, 2, dimensions);
      if (!check_status(status)) {
        descriptor_ = nullptr;
        return false;
      }

      const MKL_LONG strides[3] = {0, static_cast<MKL_LONG>(row_stride), static_cast<MKL_LONG>(col_stride)};
      const auto scale = normalization_scale<T>(rows * cols, direction, normalization);
      const auto scale_parameter = direction == Direction::forward ? DFTI_FORWARD_SCALE : DFTI_BACKWARD_SCALE;
      status = DftiSetValue(descriptor_, DFTI_PLACEMENT, DFTI_NOT_INPLACE);
      status = check_status(status) ? DftiSetValue(descriptor_, DFTI_INPUT_STRIDES, strides) : status;
      status = check_status(status) ? DftiSetValue(descriptor_, DFTI_OUTPUT_STRIDES, strides) : status;
      status = check_status(status) ? DftiSetValue(descriptor_, scale_parameter, static_cast<double>(scale)) : status;
      status = check_status(status) ? DftiCommitDescriptor(descriptor_) : status;
      if (!check_status(status)) {
        release();
        return false;
      }

      return true;
    }
  }

public:
  [[nodiscard]] bool ready() const noexcept { return descriptor_ != nullptr; }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] Direction direction() const noexcept { return direction_; }
  [[nodiscard]] Normalization normalization() const noexcept { return normalization_; }

  [[nodiscard]] bool compute(const ksj::array::PooledMatrix<std::complex<T>>& input,
                             ksj::array::PooledMatrix<std::complex<T>>& output) const {
    if (!ready() || input.rows() != rows_ || input.cols() != cols_ || output.rows() != rows_ ||
        output.cols() != cols_) {
      return false;
    }

    return compute(input.data(), output.data());
  }

  [[nodiscard]] bool compute(const std::complex<T>* input, std::complex<T>* output) const {
    if (!ready() || input == nullptr || output == nullptr) {
      return false;
    }

    auto* source = const_cast<std::complex<T>*>(input);
    const auto status = direction_ == Direction::forward ? DftiComputeForward(descriptor_, source, output)
                                                         : DftiComputeBackward(descriptor_, source, output);
    return check_status(status);
  }

private:
  void release() noexcept {
    if (descriptor_ == nullptr) {
      return;
    }
    (void)DftiFreeDescriptor(&descriptor_);
    descriptor_ = nullptr;
  }

  DFTI_DESCRIPTOR_HANDLE descriptor_{nullptr};
  std::size_t rows_{0};
  std::size_t cols_{0};
  Direction direction_{Direction::forward};
  Normalization normalization_{Normalization::none};
};

template <typename T> class CenteredFft2Descriptor {
public:
  CenteredFft2Descriptor() = default;

  CenteredFft2Descriptor(const CenteredFft2Descriptor&) = delete;
  CenteredFft2Descriptor& operator=(const CenteredFft2Descriptor&) = delete;

  CenteredFft2Descriptor(CenteredFft2Descriptor&& other) noexcept
      : descriptor_(other.descriptor_), rows_(other.rows_), cols_(other.cols_) {
    other.descriptor_ = nullptr;
    other.rows_ = 0;
    other.cols_ = 0;
  }

  CenteredFft2Descriptor& operator=(CenteredFft2Descriptor&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    release();
    descriptor_ = other.descriptor_;
    rows_ = other.rows_;
    cols_ = other.cols_;
    other.descriptor_ = nullptr;
    other.rows_ = 0;
    other.cols_ = 0;
    return *this;
  }

  ~CenteredFft2Descriptor() { release(); }

  [[nodiscard]] bool reset(const std::size_t rows, const std::size_t cols) {
    release();
    rows_ = rows;
    cols_ = cols;

    if constexpr (!dfti_scalar_v<T>) {
      return false;
    } else {
      if (rows == 0U || cols == 0U || rows > static_cast<std::size_t>(std::numeric_limits<MKL_LONG>::max()) ||
          cols > static_cast<std::size_t>(std::numeric_limits<MKL_LONG>::max()) ||
          cols > std::numeric_limits<std::size_t>::max() / rows) {
        return false;
      }

      const MKL_LONG dimensions[2] = {static_cast<MKL_LONG>(rows), static_cast<MKL_LONG>(cols)};
      auto status = DftiCreateDescriptor(&descriptor_, dfti_precision<T>(), DFTI_COMPLEX, 2, dimensions);
      if (!check_status(status)) {
        descriptor_ = nullptr;
        return false;
      }

      const auto element_count = rows * cols;
      const auto scale = normalization_scale<T>(element_count, Direction::forward, Normalization::orthonormal);
      status = DftiSetValue(descriptor_, DFTI_PLACEMENT, DFTI_INPLACE);
      status =
        check_status(status) ? DftiSetValue(descriptor_, DFTI_FORWARD_SCALE, static_cast<double>(scale)) : status;
      status =
        check_status(status) ? DftiSetValue(descriptor_, DFTI_BACKWARD_SCALE, static_cast<double>(scale)) : status;
      status = check_status(status) ? DftiCommitDescriptor(descriptor_) : status;
      if (!check_status(status)) {
        release();
        return false;
      }

      return true;
    }
  }

  [[nodiscard]] bool ready() const noexcept { return descriptor_ != nullptr; }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

  [[nodiscard]] bool compute(std::complex<T>* data, const Direction direction) const {
    if (!ready() || data == nullptr) {
      return false;
    }

    const auto status =
      direction == Direction::forward ? DftiComputeForward(descriptor_, data) : DftiComputeBackward(descriptor_, data);
    return check_status(status);
  }

private:
  void release() noexcept {
    if (descriptor_ == nullptr) {
      return;
    }
    (void)DftiFreeDescriptor(&descriptor_);
    descriptor_ = nullptr;
  }

  DFTI_DESCRIPTOR_HANDLE descriptor_{nullptr};
  std::size_t rows_{0};
  std::size_t cols_{0};
};

template <typename T> class Fft3Descriptor {
public:
  Fft3Descriptor() = default;

  Fft3Descriptor(const Fft3Descriptor&) = delete;
  Fft3Descriptor& operator=(const Fft3Descriptor&) = delete;

  Fft3Descriptor(Fft3Descriptor&& other) noexcept
      : descriptor_(other.descriptor_), rows_(other.rows_), cols_(other.cols_), slices_(other.slices_),
        direction_(other.direction_), normalization_(other.normalization_) {
    other.descriptor_ = nullptr;
    other.rows_ = 0;
    other.cols_ = 0;
    other.slices_ = 0;
  }

  Fft3Descriptor& operator=(Fft3Descriptor&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    release();
    descriptor_ = other.descriptor_;
    rows_ = other.rows_;
    cols_ = other.cols_;
    slices_ = other.slices_;
    direction_ = other.direction_;
    normalization_ = other.normalization_;
    other.descriptor_ = nullptr;
    other.rows_ = 0;
    other.cols_ = 0;
    other.slices_ = 0;
    return *this;
  }

  ~Fft3Descriptor() { release(); }

  [[nodiscard]] bool reset(const std::size_t rows, const std::size_t cols, const std::size_t slices,
                           const Direction direction, const Normalization normalization) {
    return reset_with_strides(rows, cols, slices, direction, normalization, cols * slices, slices, 1U);
  }

  [[nodiscard]] bool reset_with_strides(const std::size_t rows, const std::size_t cols, const std::size_t slices,
                                        const Direction direction, const Normalization normalization,
                                        const std::size_t row_stride, const std::size_t col_stride,
                                        const std::size_t slice_stride) {
    release();
    rows_ = rows;
    cols_ = cols;
    slices_ = slices;
    direction_ = direction;
    normalization_ = normalization;

    if constexpr (!dfti_scalar_v<T>) {
      return false;
    } else {
      if (rows == 0U || cols == 0U || slices == 0U) {
        return false;
      }

      const MKL_LONG dimensions[3] = {static_cast<MKL_LONG>(rows), static_cast<MKL_LONG>(cols),
                                      static_cast<MKL_LONG>(slices)};
      auto status = DftiCreateDescriptor(&descriptor_, dfti_precision<T>(), DFTI_COMPLEX, 3, dimensions);
      if (!check_status(status)) {
        descriptor_ = nullptr;
        return false;
      }

      const MKL_LONG strides[4] = {0, static_cast<MKL_LONG>(row_stride), static_cast<MKL_LONG>(col_stride),
                                   static_cast<MKL_LONG>(slice_stride)};
      const auto scale = normalization_scale<T>(rows * cols * slices, direction, normalization);
      const auto scale_parameter = direction == Direction::forward ? DFTI_FORWARD_SCALE : DFTI_BACKWARD_SCALE;
      status = DftiSetValue(descriptor_, DFTI_PLACEMENT, DFTI_NOT_INPLACE);
      status = check_status(status) ? DftiSetValue(descriptor_, DFTI_INPUT_STRIDES, strides) : status;
      status = check_status(status) ? DftiSetValue(descriptor_, DFTI_OUTPUT_STRIDES, strides) : status;
      status = check_status(status) ? DftiSetValue(descriptor_, scale_parameter, static_cast<double>(scale)) : status;
      status = check_status(status) ? DftiCommitDescriptor(descriptor_) : status;
      if (!check_status(status)) {
        release();
        return false;
      }

      return true;
    }
  }

  [[nodiscard]] bool ready() const noexcept { return descriptor_ != nullptr; }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t slices() const noexcept { return slices_; }
  [[nodiscard]] Direction direction() const noexcept { return direction_; }
  [[nodiscard]] Normalization normalization() const noexcept { return normalization_; }

  [[nodiscard]] bool compute(const ksj::array::PooledCube<std::complex<T>>& input,
                             ksj::array::PooledCube<std::complex<T>>& output) const {
    if (!ready() || input.dim0() != rows_ || input.dim1() != cols_ || input.dim2() != slices_ ||
        output.dim0() != rows_ || output.dim1() != cols_ || output.dim2() != slices_) {
      return false;
    }

    return compute(input.data(), output.data());
  }

  [[nodiscard]] bool compute(ksj::array::CubeView<const std::complex<T>> input,
                             ksj::array::CubeView<std::complex<T>> output) const {
    if (!ready() || input.dim0() != rows_ || input.dim1() != cols_ || input.dim2() != slices_ ||
        output.dim0() != rows_ || output.dim1() != cols_ || output.dim2() != slices_) {
      return false;
    }

    return compute(input.data(), output.data());
  }

  [[nodiscard]] bool compute(const std::complex<T>* input, std::complex<T>* output) const {
    if (!ready() || input == nullptr || output == nullptr) {
      return false;
    }

    auto* source = const_cast<std::complex<T>*>(input);
    const auto status = direction_ == Direction::forward ? DftiComputeForward(descriptor_, source, output)
                                                         : DftiComputeBackward(descriptor_, source, output);
    return check_status(status);
  }

private:
  void release() noexcept {
    if (descriptor_ == nullptr) {
      return;
    }
    (void)DftiFreeDescriptor(&descriptor_);
    descriptor_ = nullptr;
  }

  DFTI_DESCRIPTOR_HANDLE descriptor_{nullptr};
  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t slices_{0};
  Direction direction_{Direction::forward};
  Normalization normalization_{Normalization::none};
};

template <typename T>
[[nodiscard]] bool fft_1d(ksj::array::VectorView<const std::complex<T>> input,
                          ksj::array::VectorView<std::complex<T>> output, Direction direction,
                          Normalization normalization);

template <typename T>
[[nodiscard]] bool fft_2d(ksj::array::MatrixView<const std::complex<T>> input,
                          ksj::array::MatrixView<std::complex<T>> output, Direction direction,
                          Normalization normalization);

template <typename T>
[[nodiscard]] bool fft_1d(const ksj::array::PooledVector<std::complex<T>>& input,
                          ksj::array::PooledVector<std::complex<T>>& output, const Direction direction,
                          const Normalization normalization) {
  return fft_1d(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

template <typename T>
[[nodiscard]] bool fft_1d(ksj::array::VectorView<const std::complex<T>> input,
                          ksj::array::VectorView<std::complex<T>> output, const Direction direction,
                          const Normalization normalization) {
  if constexpr (!dfti_scalar_v<T>) {
    return false;
  } else {
    if (input.size() != output.size()) {
      return false;
    }
    if (input.empty()) {
      return true;
    }
    if (!input.is_contiguous() || !output.is_contiguous()) {
      return false;
    }

    DFTI_DESCRIPTOR_HANDLE descriptor = nullptr;
    const auto size = static_cast<MKL_LONG>(input.size());
    auto status = DftiCreateDescriptor(&descriptor, dfti_precision<T>(), DFTI_COMPLEX, 1, size);
    if (!check_status(status)) {
      return false;
    }

    const auto scale = normalization_scale<T>(input.size(), direction, normalization);
    const auto scale_parameter = direction == Direction::forward ? DFTI_FORWARD_SCALE : DFTI_BACKWARD_SCALE;
    status = DftiSetValue(descriptor, DFTI_PLACEMENT, DFTI_NOT_INPLACE);
    status = check_status(status) ? DftiSetValue(descriptor, scale_parameter, static_cast<double>(scale)) : status;
    status = check_status(status) ? DftiCommitDescriptor(descriptor) : status;
    if (check_status(status)) {
      auto* source = const_cast<std::complex<T>*>(input.data());
      status = direction == Direction::forward ? DftiComputeForward(descriptor, source, output.data())
                                               : DftiComputeBackward(descriptor, source, output.data());
    }

    const auto free_status = DftiFreeDescriptor(&descriptor);
    return check_status(status) && check_status(free_status);
  }
}

template <typename T>
[[nodiscard]] bool fft_2d(const ksj::array::PooledMatrix<std::complex<T>>& input,
                          ksj::array::PooledMatrix<std::complex<T>>& output, const Direction direction,
                          const Normalization normalization) {
  return fft_2d(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

template <typename T>
[[nodiscard]] bool fft_2d(ksj::array::MatrixView<const std::complex<T>> input,
                          ksj::array::MatrixView<std::complex<T>> output, const Direction direction,
                          const Normalization normalization) {
  if constexpr (!dfti_scalar_v<T>) {
    return false;
  } else {
    if (input.rows() != output.rows() || input.cols() != output.cols()) {
      return false;
    }
    if (input.empty()) {
      return true;
    }
    if (input.row_stride() != output.row_stride() || input.col_stride() != output.col_stride()) {
      return false;
    }

    Fft2Descriptor<T> descriptor;
    if (!descriptor.reset_with_strides(input.rows(), input.cols(), direction, normalization, input.row_stride(),
                                       input.col_stride())) {
      return false;
    }
    return descriptor.compute(input.data(), output.data());
  }
}

template <typename T>
[[nodiscard]] bool fft_2d_batch(const ksj::array::PooledCube<std::complex<T>>& input,
                                ksj::array::PooledCube<std::complex<T>>& output, const Direction direction,
                                const Normalization normalization) {
  if constexpr (!dfti_scalar_v<T>) {
    return false;
  } else {
    if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
      return false;
    }
    if (input.empty()) {
      return true;
    }

    Fft2Descriptor<T> descriptor;
    if (!descriptor.reset_with_strides(input.dim0(), input.dim1(), direction, normalization,
                                       input.dim1() * input.dim2(), input.dim2())) {
      return false;
    }

    for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
      if (!descriptor.compute(input.data() + slice, output.data() + slice)) {
        return false;
      }
    }
    return true;
  }
}

template <typename T>
[[nodiscard]] bool fft_3d(ksj::array::CubeView<const std::complex<T>> input,
                          ksj::array::CubeView<std::complex<T>> output, const Direction direction,
                          const Normalization normalization) {
  if constexpr (!dfti_scalar_v<T>) {
    return false;
  } else {
    if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
      return false;
    }
    if (input.empty()) {
      return true;
    }
    if (input.dim0_stride() != output.dim0_stride() || input.dim1_stride() != output.dim1_stride() ||
        input.dim2_stride() != output.dim2_stride()) {
      return false;
    }

    Fft3Descriptor<T> descriptor;
    if (!descriptor.reset_with_strides(input.dim0(), input.dim1(), input.dim2(), direction, normalization,
                                       input.dim0_stride(), input.dim1_stride(), input.dim2_stride())) {
      return false;
    }
    return descriptor.compute(input, output);
  }
}

template <typename T>
[[nodiscard]] bool fft_3d(const ksj::array::PooledCube<std::complex<T>>& input,
                          ksj::array::PooledCube<std::complex<T>>& output, const Direction direction,
                          const Normalization normalization) {
  return fft_3d(ksj::array::as_const_view(input.view()), output.view(), direction, normalization);
}

template <typename T>
[[nodiscard]] bool fft_3d_batch(const ksj::array::PooledArray4D<std::complex<T>>& input,
                                ksj::array::PooledArray4D<std::complex<T>>& output, const Direction direction,
                                const Normalization normalization) {
  if constexpr (!dfti_scalar_v<T>) {
    return false;
  } else {
    if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2() ||
        input.dim3() != output.dim3()) {
      return false;
    }
    if (input.empty()) {
      return true;
    }

    auto volume_input = ksj::array::make_pooled_cube<std::complex<T>>(input.dim0(), input.dim1(), input.dim2());
    auto volume_output = ksj::array::make_pooled_cube<std::complex<T>>(output.dim0(), output.dim1(), output.dim2());
    Fft3Descriptor<T> descriptor;
    if (!descriptor.reset(input.dim0(), input.dim1(), input.dim2(), direction, normalization)) {
      return false;
    }

    for (std::size_t batch = 0; batch < input.dim3(); ++batch) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        for (std::size_t col = 0; col < input.dim1(); ++col) {
          for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
            volume_input(row, col, slice) = input(row, col, slice, batch);
          }
        }
      }
      if (!descriptor.compute(volume_input, volume_output)) {
        return false;
      }
      for (std::size_t row = 0; row < output.dim0(); ++row) {
        for (std::size_t col = 0; col < output.dim1(); ++col) {
          for (std::size_t slice = 0; slice < output.dim2(); ++slice) {
            output(row, col, slice, batch) = volume_output(row, col, slice);
          }
        }
      }
    }
    return true;
  }
}

template <typename T>
[[nodiscard]] bool fft_3d_batch_strided(const ksj::array::PooledArray4D<std::complex<T>>& input,
                                        ksj::array::PooledArray4D<std::complex<T>>& output, const Direction direction,
                                        const Normalization normalization) {
  if constexpr (!dfti_scalar_v<T>) {
    return false;
  } else {
    if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2() ||
        input.dim3() != output.dim3()) {
      return false;
    }
    if (input.empty()) {
      return true;
    }

    const auto row_stride = input.dim1() * input.dim2() * input.dim3();
    const auto col_stride = input.dim2() * input.dim3();
    const auto slice_stride = input.dim3();
    Fft3Descriptor<T> descriptor;
    if (!descriptor.reset_with_strides(input.dim0(), input.dim1(), input.dim2(), direction, normalization, row_stride,
                                       col_stride, slice_stride)) {
      return false;
    }

    for (std::size_t batch = 0; batch < input.dim3(); ++batch) {
      if (!descriptor.compute(input.data() + batch, output.data() + batch)) {
        return false;
      }
    }
    return true;
  }
}

} // namespace ksj::fft::detail::intel_impl
