#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_types.hpp"

namespace ksj::stats::detail::intel {

[[nodiscard]] bool rmse(ksj::array::VectorView<const float> diff, float& output);
[[nodiscard]] bool rmse(ksj::array::VectorView<const double> diff, double& output);
[[nodiscard]] bool rmse(ksj::array::VectorView<const ksj::base::cf32> diff, float& output);
[[nodiscard]] bool rmse(ksj::array::VectorView<const ksj::base::cf64> diff, double& output);

template <typename T, typename Output> [[nodiscard]] bool rmse(ksj::array::VectorView<const T> diff, Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return rmse(diff, output);
  } else {
    (void)diff;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool rmse(ksj::array::VectorView<const float> data, ksj::array::VectorView<const float> reference,
                        float& output);
[[nodiscard]] bool rmse(ksj::array::VectorView<const double> data, ksj::array::VectorView<const double> reference,
                        double& output);
[[nodiscard]] bool rmse(ksj::array::VectorView<const ksj::base::cf32> data,
                        ksj::array::VectorView<const ksj::base::cf32> reference, float& output);
[[nodiscard]] bool rmse(ksj::array::VectorView<const ksj::base::cf64> data,
                        ksj::array::VectorView<const ksj::base::cf64> reference, double& output);

template <typename T, typename Output>
[[nodiscard]] bool rmse(ksj::array::VectorView<const T> data, ksj::array::VectorView<const T> reference,
                        Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return rmse(data, reference, output);
  } else {
    (void)data;
    (void)reference;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool equal(ksj::array::VectorView<const float> data, ksj::array::VectorView<const float> reference,
                         float precision, bool& output);
[[nodiscard]] bool equal(ksj::array::VectorView<const double> data, ksj::array::VectorView<const double> reference,
                         double precision, bool& output);
[[nodiscard]] bool equal(ksj::array::VectorView<const ksj::base::cf32> data,
                         ksj::array::VectorView<const ksj::base::cf32> reference, float precision, bool& output);
[[nodiscard]] bool equal(ksj::array::VectorView<const ksj::base::cf64> data,
                         ksj::array::VectorView<const ksj::base::cf64> reference, double precision, bool& output);

template <typename T, typename Precision>
[[nodiscard]] bool equal(ksj::array::VectorView<const T> data, ksj::array::VectorView<const T> reference,
                         Precision precision, bool& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return equal(data, reference, precision, output);
  } else {
    (void)data;
    (void)reference;
    (void)precision;
    (void)output;
    return false;
  }
}

} // namespace ksj::stats::detail::intel
