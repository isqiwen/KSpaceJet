#pragma once

/// Regular signal resampling APIs that define source, destination, and interpolation behavior explicitly.

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/types.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_resampling.hpp"
#include <cstddef>

namespace ksj::signal {

template <typename T>
void resample(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
              const ResampleKernel kernel = ResampleKernel::linear) {
  detail::eigen::resample(input, output, kernel);
}

template <typename T>
void resample(ksj::array::VectorView<T> input, ksj::array::VectorView<T> output,
              const ResampleKernel kernel = ResampleKernel::linear) {
  resample(ksj::array::as_const_view(input), output, kernel);
}

template <typename T>
void resample(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output,
              const ResampleKernel kernel = ResampleKernel::linear) {
  resample(ksj::array::as_const_view(input.view()), output.view(), kernel);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> resample(ksj::array::VectorView<const T> input, const std::size_t size,
                                                   const ResampleKernel kernel = ResampleKernel::linear) {
  auto output = ksj::array::make_pooled_vector<T>(size);
  resample(input, output.view(), kernel);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> resample(const ksj::array::PooledVector<T>& input, const std::size_t size,
                                                   const ResampleKernel kernel = ResampleKernel::linear) {
  return resample(ksj::array::as_const_view(input.view()), size, kernel);
}

} // namespace ksj::signal
