#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_types.hpp"

#include <cstddef>

namespace ksj::stats::detail::eigen {

[[nodiscard]] float otsu_threshold(ksj::array::VectorView<const float> input, std::size_t interval_count);
[[nodiscard]] double otsu_threshold(ksj::array::VectorView<const double> input, std::size_t interval_count);

} // namespace ksj::stats::detail::eigen
