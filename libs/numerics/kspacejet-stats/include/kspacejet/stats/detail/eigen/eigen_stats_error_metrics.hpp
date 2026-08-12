#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_types.hpp"

namespace ksj::stats::detail::eigen {

[[nodiscard]] float rmse(ksj::array::VectorView<const float> diff);
[[nodiscard]] double rmse(ksj::array::VectorView<const double> diff);
[[nodiscard]] float rmse(ksj::array::VectorView<const ksj::base::cf32> diff);
[[nodiscard]] double rmse(ksj::array::VectorView<const ksj::base::cf64> diff);

[[nodiscard]] float rmse(ksj::array::VectorView<const float> data, ksj::array::VectorView<const float> reference);
[[nodiscard]] double rmse(ksj::array::VectorView<const double> data, ksj::array::VectorView<const double> reference);
[[nodiscard]] float rmse(ksj::array::VectorView<const ksj::base::cf32> data,
                         ksj::array::VectorView<const ksj::base::cf32> reference);
[[nodiscard]] double rmse(ksj::array::VectorView<const ksj::base::cf64> data,
                          ksj::array::VectorView<const ksj::base::cf64> reference);

[[nodiscard]] bool equal(ksj::array::VectorView<const float> data, ksj::array::VectorView<const float> reference,
                         float precision);
[[nodiscard]] bool equal(ksj::array::VectorView<const double> data, ksj::array::VectorView<const double> reference,
                         double precision);
[[nodiscard]] bool equal(ksj::array::VectorView<const ksj::base::cf32> data,
                         ksj::array::VectorView<const ksj::base::cf32> reference, float precision);
[[nodiscard]] bool equal(ksj::array::VectorView<const ksj::base::cf64> data,
                         ksj::array::VectorView<const ksj::base::cf64> reference, double precision);

} // namespace ksj::stats::detail::eigen
