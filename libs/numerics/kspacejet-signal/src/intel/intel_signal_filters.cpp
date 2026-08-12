#include "kspacejet/signal/detail/intel/intel_signal_filters.hpp"

#include "intel_signal_common.hpp"

#include <ipp.h>

#include <algorithm>
#include <type_traits>
#include <vector>

namespace ksj::signal::detail::intel {
namespace {

template <typename T>
[[nodiscard]] bool fits_filter_views(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output) noexcept {
  return input.size() == output.size() && input.is_contiguous() && output.is_contiguous() &&
         input.data() != output.data() && impl::fits_ipp_length(input.size());
}

template <typename T>
[[nodiscard]] bool fir_filter_impl(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> taps,
                                   ksj::array::VectorView<T> output, FirFilterWorkspace<T>& workspace) {
  if (!fits_filter_views(input, output) || taps.empty() || !taps.is_contiguous() ||
      !impl::fits_ipp_length(taps.size())) {
    return false;
  }
  if (input.empty()) {
    return true;
  }

  int spec_size = 0;
  int buffer_size = 0;
  IppStatus status = ippStsNoErr;
  if constexpr (std::is_same_v<T, float>) {
    status = ippsFIRSRGetSize(static_cast<int>(taps.size()), ipp32f, &spec_size, &buffer_size);
  } else if constexpr (std::is_same_v<T, double>) {
    status = ippsFIRSRGetSize(static_cast<int>(taps.size()), ipp64f, &spec_size, &buffer_size);
  } else {
    return false;
  }
  if (!impl::check_status(status) || spec_size < 0 || buffer_size < 0) {
    return false;
  }

  workspace.spec_storage.resize(static_cast<std::size_t>(spec_size));
  workspace.buffer_storage.resize(static_cast<std::size_t>(buffer_size));
  auto* buffer_data = workspace.buffer_storage.empty() ? nullptr : workspace.buffer_storage.data();

  if constexpr (std::is_same_v<T, float>) {
    auto* spec = reinterpret_cast<IppsFIRSpec_32f*>(workspace.spec_storage.data());
    status = ippsFIRSRInit_32f(taps.data(), static_cast<int>(taps.size()), ippAlgAuto, spec);
    if (!impl::check_status(status)) {
      return false;
    }
    status =
      ippsFIRSR_32f(input.data(), output.data(), static_cast<int>(input.size()), spec, nullptr, nullptr, buffer_data);
  } else {
    auto* spec = reinterpret_cast<IppsFIRSpec_64f*>(workspace.spec_storage.data());
    status = ippsFIRSRInit_64f(taps.data(), static_cast<int>(taps.size()), ippAlgAuto, spec);
    if (!impl::check_status(status)) {
      return false;
    }
    status =
      ippsFIRSR_64f(input.data(), output.data(), static_cast<int>(input.size()), spec, nullptr, nullptr, buffer_data);
  }
  return impl::check_status(status);
}

template <typename T>
[[nodiscard]] bool iir_filter_impl(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> numerator,
                                   ksj::array::VectorView<const T> denominator, ksj::array::VectorView<T> output,
                                   IirFilterWorkspace<T>& workspace) {
  if (!fits_filter_views(input, output) || numerator.empty() || denominator.empty() || denominator(0U) == T{} ||
      !numerator.is_contiguous() || !denominator.is_contiguous()) {
    return false;
  }
  if (input.empty()) {
    return true;
  }

  const auto order = std::max(numerator.size(), denominator.size()) - 1U;
  if (order == 0U || !impl::fits_ipp_length(order)) {
    return false;
  }

  int state_size = 0;
  IppStatus status = ippStsNoErr;
  if constexpr (std::is_same_v<T, float>) {
    status = ippsIIRGetStateSize_32f(static_cast<int>(order), &state_size);
  } else if constexpr (std::is_same_v<T, double>) {
    status = ippsIIRGetStateSize_64f(static_cast<int>(order), &state_size);
  } else {
    return false;
  }
  if (!impl::check_status(status) || state_size < 0) {
    return false;
  }

  workspace.taps_storage.resize(2U * (order + 1U));
  ksj::array::fill(workspace.taps_storage.view(), T{});
  for (std::size_t index = 0; index < numerator.size(); ++index) {
    workspace.taps_storage(index) = numerator(index);
  }
  for (std::size_t index = 0; index < denominator.size(); ++index) {
    workspace.taps_storage(order + 1U + index) = denominator(index);
  }
  workspace.state_storage.resize(static_cast<std::size_t>(state_size));
  auto* state_data = workspace.state_storage.empty() ? nullptr : workspace.state_storage.data();

  if constexpr (std::is_same_v<T, float>) {
    IppsIIRState_32f* state = nullptr;
    status = ippsIIRInit_32f(&state, workspace.taps_storage.data(), static_cast<int>(order), nullptr, state_data);
    if (!impl::check_status(status)) {
      return false;
    }
    status = ippsIIR_32f(input.data(), output.data(), static_cast<int>(input.size()), state);
  } else {
    IppsIIRState_64f* state = nullptr;
    status = ippsIIRInit_64f(&state, workspace.taps_storage.data(), static_cast<int>(order), nullptr, state_data);
    if (!impl::check_status(status)) {
      return false;
    }
    status = ippsIIR_64f(input.data(), output.data(), static_cast<int>(input.size()), state);
  }
  return impl::check_status(status);
}

} // namespace

bool median_filter_in_place(ksj::array::VectorView<float> input, const std::size_t kernel_size,
                            const SignalBorderMode border_mode, MedianFilterWorkspace<float>& workspace) {
  if (border_mode != SignalBorderMode::causal_replicate || !input.is_contiguous() ||
      !impl::fits_ipp_length(input.size()) || kernel_size == 0U || kernel_size % 2U == 0U ||
      !impl::fits_ipp_length(kernel_size)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }

  int buffer_size = 0;
  auto status = ippsFilterMedianGetBufferSize(static_cast<int>(kernel_size), ipp32f, &buffer_size);
  if (!impl::check_status(status) || buffer_size < 0) {
    return false;
  }

  workspace.buffer_storage.resize(static_cast<std::size_t>(buffer_size));
  auto* buffer_data = workspace.buffer_storage.empty() ? nullptr : workspace.buffer_storage.data();
  status = ippsFilterMedian_32f_I(input.data(), static_cast<int>(input.size()), static_cast<int>(kernel_size), nullptr,
                                  nullptr, buffer_data);
  return impl::check_status(status);
}

bool median_filter_in_place(ksj::array::VectorView<float> input, const std::size_t kernel_size,
                            const SignalBorderMode border_mode) {
  MedianFilterWorkspace<float> workspace;
  return median_filter_in_place(input, kernel_size, border_mode, workspace);
}

bool fir_filter(ksj::array::VectorView<const float> input, ksj::array::VectorView<const float> taps,
                ksj::array::VectorView<float> output, FirFilterWorkspace<float>& workspace) {
  return fir_filter_impl(input, taps, output, workspace);
}

bool fir_filter(ksj::array::VectorView<const double> input, ksj::array::VectorView<const double> taps,
                ksj::array::VectorView<double> output, FirFilterWorkspace<double>& workspace) {
  return fir_filter_impl(input, taps, output, workspace);
}

bool fir_filter(ksj::array::VectorView<const float> input, ksj::array::VectorView<const float> taps,
                ksj::array::VectorView<float> output) {
  FirFilterWorkspace<float> workspace;
  return fir_filter(input, taps, output, workspace);
}

bool fir_filter(ksj::array::VectorView<const double> input, ksj::array::VectorView<const double> taps,
                ksj::array::VectorView<double> output) {
  FirFilterWorkspace<double> workspace;
  return fir_filter(input, taps, output, workspace);
}

bool iir_filter(ksj::array::VectorView<const float> input, ksj::array::VectorView<const float> numerator,
                ksj::array::VectorView<const float> denominator, ksj::array::VectorView<float> output,
                IirFilterWorkspace<float>& workspace) {
  return iir_filter_impl(input, numerator, denominator, output, workspace);
}

bool iir_filter(ksj::array::VectorView<const double> input, ksj::array::VectorView<const double> numerator,
                ksj::array::VectorView<const double> denominator, ksj::array::VectorView<double> output,
                IirFilterWorkspace<double>& workspace) {
  return iir_filter_impl(input, numerator, denominator, output, workspace);
}

bool iir_filter(ksj::array::VectorView<const float> input, ksj::array::VectorView<const float> numerator,
                ksj::array::VectorView<const float> denominator, ksj::array::VectorView<float> output) {
  IirFilterWorkspace<float> workspace;
  return iir_filter(input, numerator, denominator, output, workspace);
}

bool iir_filter(ksj::array::VectorView<const double> input, ksj::array::VectorView<const double> numerator,
                ksj::array::VectorView<const double> denominator, ksj::array::VectorView<double> output) {
  IirFilterWorkspace<double> workspace;
  return iir_filter(input, numerator, denominator, output, workspace);
}

} // namespace ksj::signal::detail::intel
