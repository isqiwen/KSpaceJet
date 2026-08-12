#include "kspacejet/signal/detail/intel/intel_signal_windows.hpp"

#include "intel_signal_common.hpp"

#include <ipp.h>

#include <cstddef>

namespace ksj::signal::detail::intel {
namespace {

template <typename T> void fill_ones(ksj::array::VectorView<T> output) {
  auto* values = output.data();
  for (std::size_t index = 0; index < output.size(); ++index) {
    values[index] = T{1};
  }
}

template <typename T>
[[nodiscard]] bool window_impl(ksj::array::VectorView<T> output, const WindowKind kind, IppStatus (*hann)(T*, int),
                               IppStatus (*hamming)(T*, int), IppStatus (*blackman)(T*, int)) {
  if (!impl::fits_ipp_length(output.size()) || !output.is_contiguous()) {
    return false;
  }
  if (output.empty()) {
    return true;
  }

  const auto size = static_cast<int>(output.size());
  fill_ones(output);
  switch (kind) {
    case WindowKind::rectangular:
      return true;
    case WindowKind::hann:
      return impl::check_status(hann(output.data(), size));
    case WindowKind::hamming:
      return impl::check_status(hamming(output.data(), size));
    case WindowKind::blackman:
      return impl::check_status(blackman(output.data(), size));
  }
  return false;
}

} // namespace

bool window(ksj::array::VectorView<float> output, const WindowKind kind) {
  return window_impl(output, kind, ippsWinHann_32f_I, ippsWinHamming_32f_I, ippsWinBlackmanStd_32f_I);
}

bool window(ksj::array::VectorView<double> output, const WindowKind kind) {
  return window_impl(output, kind, ippsWinHann_64f_I, ippsWinHamming_64f_I, ippsWinBlackmanStd_64f_I);
}

} // namespace ksj::signal::detail::intel
