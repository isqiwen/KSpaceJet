#pragma once

#include "kspacejet/nufft/types.hpp"

namespace ksj::nufft::detail {

[[nodiscard]] constexpr bool prefer_bart_nufft2(const Backend backend) noexcept {
  // Tuned by docs/benchmark_reports/2026-07-24/kspacejet-numerics/xeon-silver-4410y-avx512-linux/benchmark_report.md.
  // The local direct-DFT benchmark shows Eigen is faster for every measured cold and warm workload.
  // BART remains available when callers explicitly request its separate implementation.
  return backend == Backend::bart;
}

[[nodiscard]] constexpr bool require_bart_nufft2(const Backend backend) noexcept {
  return backend == Backend::bart;
}

} // namespace ksj::nufft::detail
