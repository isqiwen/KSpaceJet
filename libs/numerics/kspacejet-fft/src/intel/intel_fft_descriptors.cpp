#include "kspacejet/fft/detail/intel/intel_fft_descriptors.hpp"

#include "intel_fft_common.hpp"

namespace ksj::fft::detail::intel {
namespace {

template <typename Impl> [[nodiscard]] Impl* impl_cast(std::shared_ptr<void>& impl) noexcept {
  return static_cast<Impl*>(impl.get());
}

template <typename Impl> [[nodiscard]] const Impl* impl_cast(const std::shared_ptr<void>& impl) noexcept {
  return static_cast<const Impl*>(impl.get());
}

} // namespace

bool OrthonormalFft1Descriptor::reset(const std::size_t size, const Direction direction) {
  auto impl = std::make_shared<detail::intel_impl::OrthonormalFft1Descriptor>();
  const auto ready = impl->reset(size, direction);
  impl_ = std::move(impl);
  return ready;
}

std::size_t OrthonormalFft1Descriptor::size() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::OrthonormalFft1Descriptor>(impl_);
  return impl != nullptr ? impl->size() : 0U;
}

bool OrthonormalFft1Descriptor::ready() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::OrthonormalFft1Descriptor>(impl_);
  return impl != nullptr && impl->ready();
}

bool OrthonormalFft1Descriptor::compute(ksj::array::VectorView<const std::complex<float>> input,
                                        ksj::array::VectorView<std::complex<float>> output) noexcept {
  auto* impl = impl_cast<detail::intel_impl::OrthonormalFft1Descriptor>(impl_);
  return impl != nullptr && impl->compute(input, output);
}

template <typename T>
bool Fft1Descriptor<T>::reset(const std::size_t size, const Direction direction, const Normalization normalization,
                              const bool in_place) {
  if constexpr (!dfti_scalar_v<T>) {
    impl_.reset();
    return false;
  } else {
    auto impl = std::make_shared<detail::intel_impl::Fft1Descriptor<T>>();
    const auto ready = impl->reset(size, direction, normalization, in_place);
    impl_ = std::move(impl);
    return ready;
  }
}

template <typename T> bool Fft1Descriptor<T>::ready() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft1Descriptor<T>>(impl_);
  return impl != nullptr && impl->ready();
}

template <typename T> std::size_t Fft1Descriptor<T>::size() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft1Descriptor<T>>(impl_);
  return impl != nullptr ? impl->size() : 0U;
}

template <typename T> Direction Fft1Descriptor<T>::direction() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft1Descriptor<T>>(impl_);
  return impl != nullptr ? impl->direction() : Direction::forward;
}

template <typename T> Normalization Fft1Descriptor<T>::normalization() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft1Descriptor<T>>(impl_);
  return impl != nullptr ? impl->normalization() : Normalization::none;
}

template <typename T> bool Fft1Descriptor<T>::in_place() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft1Descriptor<T>>(impl_);
  return impl != nullptr && impl->in_place();
}

template <typename T>
bool Fft1Descriptor<T>::compute(const ksj::array::PooledVector<std::complex<T>>& input,
                                ksj::array::PooledVector<std::complex<T>>& output) const {
  const auto* impl = impl_cast<detail::intel_impl::Fft1Descriptor<T>>(impl_);
  return impl != nullptr && impl->compute(input, output);
}

template <typename T> bool Fft1Descriptor<T>::compute(const std::complex<T>* input, std::complex<T>* output) const {
  const auto* impl = impl_cast<detail::intel_impl::Fft1Descriptor<T>>(impl_);
  return impl != nullptr && impl->compute(input, output);
}

template <typename T> bool Fft1Descriptor<T>::compute(std::complex<T>* data) const {
  const auto* impl = impl_cast<detail::intel_impl::Fft1Descriptor<T>>(impl_);
  return impl != nullptr && impl->compute(data);
}

template <typename T>
bool Fft2Descriptor<T>::reset(const std::size_t rows, const std::size_t cols, const Direction direction,
                              const Normalization normalization) {
  return reset_with_strides(rows, cols, direction, normalization, cols, 1U);
}

template <typename T>
bool Fft2Descriptor<T>::reset_with_strides(const std::size_t rows, const std::size_t cols, const Direction direction,
                                           const Normalization normalization, const std::size_t row_stride,
                                           const std::size_t col_stride) {
  if constexpr (!dfti_scalar_v<T>) {
    impl_.reset();
    return false;
  } else {
    auto impl = std::make_shared<detail::intel_impl::Fft2Descriptor<T>>();
    const auto ready = impl->reset_with_strides(rows, cols, direction, normalization, row_stride, col_stride);
    impl_ = std::move(impl);
    return ready;
  }
}

template <typename T> bool Fft2Descriptor<T>::ready() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft2Descriptor<T>>(impl_);
  return impl != nullptr && impl->ready();
}

template <typename T> std::size_t Fft2Descriptor<T>::rows() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft2Descriptor<T>>(impl_);
  return impl != nullptr ? impl->rows() : 0U;
}

template <typename T> std::size_t Fft2Descriptor<T>::cols() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft2Descriptor<T>>(impl_);
  return impl != nullptr ? impl->cols() : 0U;
}

template <typename T> Direction Fft2Descriptor<T>::direction() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft2Descriptor<T>>(impl_);
  return impl != nullptr ? impl->direction() : Direction::forward;
}

template <typename T> Normalization Fft2Descriptor<T>::normalization() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft2Descriptor<T>>(impl_);
  return impl != nullptr ? impl->normalization() : Normalization::none;
}

template <typename T>
bool Fft2Descriptor<T>::compute(const ksj::array::PooledMatrix<std::complex<T>>& input,
                                ksj::array::PooledMatrix<std::complex<T>>& output) const {
  const auto* impl = impl_cast<detail::intel_impl::Fft2Descriptor<T>>(impl_);
  return impl != nullptr && impl->compute(input, output);
}

template <typename T> bool Fft2Descriptor<T>::compute(const std::complex<T>* input, std::complex<T>* output) const {
  const auto* impl = impl_cast<detail::intel_impl::Fft2Descriptor<T>>(impl_);
  return impl != nullptr && impl->compute(input, output);
}

template <typename T> bool CenteredFft2Descriptor<T>::reset(const std::size_t rows, const std::size_t cols) {
  if constexpr (!dfti_scalar_v<T>) {
    impl_.reset();
    return false;
  } else {
    auto impl = std::make_shared<detail::intel_impl::CenteredFft2Descriptor<T>>();
    const auto ready = impl->reset(rows, cols);
    impl_ = std::move(impl);
    return ready;
  }
}

template <typename T> bool CenteredFft2Descriptor<T>::ready() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::CenteredFft2Descriptor<T>>(impl_);
  return impl != nullptr && impl->ready();
}

template <typename T> std::size_t CenteredFft2Descriptor<T>::rows() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::CenteredFft2Descriptor<T>>(impl_);
  return impl != nullptr ? impl->rows() : 0U;
}

template <typename T> std::size_t CenteredFft2Descriptor<T>::cols() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::CenteredFft2Descriptor<T>>(impl_);
  return impl != nullptr ? impl->cols() : 0U;
}

template <typename T> bool CenteredFft2Descriptor<T>::compute(std::complex<T>* data, const Direction direction) const {
  const auto* impl = impl_cast<detail::intel_impl::CenteredFft2Descriptor<T>>(impl_);
  return impl != nullptr && impl->compute(data, direction);
}

template <typename T>
bool Fft3Descriptor<T>::reset(const std::size_t rows, const std::size_t cols, const std::size_t slices,
                              const Direction direction, const Normalization normalization) {
  return reset_with_strides(rows, cols, slices, direction, normalization, cols * slices, slices, 1U);
}

template <typename T>
bool Fft3Descriptor<T>::reset_with_strides(const std::size_t rows, const std::size_t cols, const std::size_t slices,
                                           const Direction direction, const Normalization normalization,
                                           const std::size_t row_stride, const std::size_t col_stride,
                                           const std::size_t slice_stride) {
  if constexpr (!dfti_scalar_v<T>) {
    impl_.reset();
    return false;
  } else {
    auto impl = std::make_shared<detail::intel_impl::Fft3Descriptor<T>>();
    const auto ready =
      impl->reset_with_strides(rows, cols, slices, direction, normalization, row_stride, col_stride, slice_stride);
    impl_ = std::move(impl);
    return ready;
  }
}

template <typename T> bool Fft3Descriptor<T>::ready() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft3Descriptor<T>>(impl_);
  return impl != nullptr && impl->ready();
}

template <typename T> std::size_t Fft3Descriptor<T>::rows() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft3Descriptor<T>>(impl_);
  return impl != nullptr ? impl->rows() : 0U;
}

template <typename T> std::size_t Fft3Descriptor<T>::cols() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft3Descriptor<T>>(impl_);
  return impl != nullptr ? impl->cols() : 0U;
}

template <typename T> std::size_t Fft3Descriptor<T>::slices() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft3Descriptor<T>>(impl_);
  return impl != nullptr ? impl->slices() : 0U;
}

template <typename T> Direction Fft3Descriptor<T>::direction() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft3Descriptor<T>>(impl_);
  return impl != nullptr ? impl->direction() : Direction::forward;
}

template <typename T> Normalization Fft3Descriptor<T>::normalization() const noexcept {
  const auto* impl = impl_cast<detail::intel_impl::Fft3Descriptor<T>>(impl_);
  return impl != nullptr ? impl->normalization() : Normalization::none;
}

template <typename T>
bool Fft3Descriptor<T>::compute(const ksj::array::PooledCube<std::complex<T>>& input,
                                ksj::array::PooledCube<std::complex<T>>& output) const {
  const auto* impl = impl_cast<detail::intel_impl::Fft3Descriptor<T>>(impl_);
  return impl != nullptr && impl->compute(input, output);
}

template <typename T>
bool Fft3Descriptor<T>::compute(ksj::array::CubeView<const std::complex<T>> input,
                                ksj::array::CubeView<std::complex<T>> output) const {
  const auto* impl = impl_cast<detail::intel_impl::Fft3Descriptor<T>>(impl_);
  return impl != nullptr && impl->compute(input, output);
}

template <typename T> bool Fft3Descriptor<T>::compute(const std::complex<T>* input, std::complex<T>* output) const {
  const auto* impl = impl_cast<detail::intel_impl::Fft3Descriptor<T>>(impl_);
  return impl != nullptr && impl->compute(input, output);
}

template class Fft1Descriptor<float>;
template class Fft1Descriptor<double>;
template class Fft2Descriptor<float>;
template class Fft2Descriptor<double>;
template class CenteredFft2Descriptor<float>;
template class CenteredFft2Descriptor<double>;
template class Fft3Descriptor<float>;
template class Fft3Descriptor<double>;

} // namespace ksj::fft::detail::intel
