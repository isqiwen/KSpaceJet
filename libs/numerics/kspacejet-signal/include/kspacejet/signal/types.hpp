#pragma once

/// Signal-domain enums and lightweight value types used by filtering and resampling APIs.

namespace ksj::signal {

enum class ResampleKernel {
  nearest,
  linear,
  cubic,
  mitchell,
  lanczos3,
};

enum class WindowKind {
  rectangular,
  hann,
  hamming,
  blackman,
};

enum class SignalBorderMode {
  replicate,
  zero,
  causal_replicate,
};

} // namespace ksj::signal
