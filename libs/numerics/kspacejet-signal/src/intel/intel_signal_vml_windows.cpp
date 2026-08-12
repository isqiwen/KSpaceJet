#include "kspacejet/signal/detail/intel/intel_signal_vml_windows.hpp"

#include "kspacejet/special/special_functions.hpp"

#include <cmath>
#include <cstddef>

namespace ksj::signal::detail::intel {
namespace {

template <typename T>
[[nodiscard]] bool exponential_window_impl(ksj::array::VectorView<T> output, const T alpha, const T exponent) {
  if (!output.is_contiguous()) {
    return false;
  }
  if (output.empty()) {
    return true;
  }

  const auto center = static_cast<T>(output.size() - 1U) / T{2};
  const auto denom = output.size() > 1U ? static_cast<T>(output.size() - 1U) : T{1};
  auto* output_data = output.data();
  for (std::size_t index = 0; index < output.size(); ++index) {
    const auto normalized = std::abs((static_cast<T>(index) - center) / denom);
    const auto powered = exponent == T{2} ? normalized * normalized : std::pow(normalized, exponent);
    output_data[index] = -alpha * powered;
  }

  ksj::special::exp(ksj::array::as_const_view(output), output);
  return true;
}

} // namespace

bool exponential_window(ksj::array::VectorView<float> output, const float alpha, const float exponent) {
  return exponential_window_impl(output, alpha, exponent);
}

bool exponential_window(ksj::array::VectorView<double> output, const double alpha, const double exponent) {
  return exponential_window_impl(output, alpha, exponent);
}

} // namespace ksj::signal::detail::intel
