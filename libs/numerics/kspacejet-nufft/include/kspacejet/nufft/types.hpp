#pragma once

/// NUFFT direction, normalization, trajectory, and plan configuration value types.

#include <cstddef>

namespace ksj::nufft {

struct Grid2D {
  std::size_t rows{};
  std::size_t cols{};
  double row_origin{};
  double col_origin{};
};

enum class Backend {
  /// Choose the benchmarked exact Eigen direct NUDFT implementation.
  automatic,
  /// Require the exact Eigen direct NUDFT implementation.
  eigen,
  /// Require BART's single-precision implementation; unavailable scalar types throw.
  bart,
};

struct Nufft2Options {
  /// Backend dispatch choice. Automatic uses Eigen on the supported local CPU profile.
  Backend backend{Backend::automatic};
  /// When BART is explicitly selected, request its exact direct-DFT operator rather than approximate NUFFT gridding.
  bool direct_dft{};
};

} // namespace ksj::nufft
