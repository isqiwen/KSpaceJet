#pragma once

/// Fourier-transform constants and normalization factors used by the public FFT APIs.

namespace ksj::fft {

inline constexpr int kRowFft = 0;
inline constexpr int kColumnFft = 1;

inline constexpr int kForwardCosineTransform = 0;
inline constexpr int kInverseCosineTransform = 1;

} // namespace ksj::fft
