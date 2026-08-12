#pragma once

/// Caller-owned temporary storage for repeated NUFFT execution without hidden allocations.

#include "kspacejet/array/array.hpp"

#include <complex>
#include <memory>

namespace ksj::nufft::detail::bart {
struct Nufft2Plan;
}

namespace ksj::nufft {

template <typename T> struct Nufft2Workspace {
  // The opaque BART plan keeps its own immutable packed trajectory. It is invalidated when the grid, direction,
  // direct-DFT mode, or trajectory changes.
  std::shared_ptr<detail::bart::Nufft2Plan> bart_plan;
  ksj::array::PooledVector<std::complex<T>> bart_trajectory;
  ksj::array::PooledMatrix<std::complex<T>> packed_image;
  ksj::array::PooledVector<std::complex<T>> packed_samples;
  ksj::array::PooledVector<std::complex<T>> output_buffer;
  ksj::array::PooledMatrix<std::complex<T>> image_buffer;
};

} // namespace ksj::nufft
