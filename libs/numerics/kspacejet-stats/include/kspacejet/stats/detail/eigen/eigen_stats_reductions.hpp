#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_types.hpp"

#include <cstddef>

namespace ksj::stats::detail::eigen {

[[nodiscard]] float sum(ksj::array::VectorView<const float> input);
[[nodiscard]] double sum(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::base::cf32 sum(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::base::cf64 sum(ksj::array::VectorView<const ksj::base::cf64> input);

[[nodiscard]] float sum_abs(ksj::array::VectorView<const float> input);
[[nodiscard]] double sum_abs(ksj::array::VectorView<const double> input);
[[nodiscard]] float sum_abs(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] double sum_abs(ksj::array::VectorView<const ksj::base::cf64> input);

[[nodiscard]] float kahan_sum(ksj::array::VectorView<const float> input);
[[nodiscard]] double kahan_sum(ksj::array::VectorView<const double> input);

[[nodiscard]] float pair_sum(ksj::array::VectorView<const float> input);
[[nodiscard]] double pair_sum(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::base::cf32 pair_sum(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::base::cf64 pair_sum(ksj::array::VectorView<const ksj::base::cf64> input);

[[nodiscard]] std::size_t max_index(ksj::array::VectorView<const float> input);
[[nodiscard]] std::size_t max_index(ksj::array::VectorView<const double> input);
[[nodiscard]] std::size_t max_index(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] std::size_t max_index(ksj::array::VectorView<const ksj::base::cf64> input);

[[nodiscard]] std::size_t min_index(ksj::array::VectorView<const float> input);
[[nodiscard]] std::size_t min_index(ksj::array::VectorView<const double> input);
[[nodiscard]] std::size_t min_index(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] std::size_t min_index(ksj::array::VectorView<const ksj::base::cf64> input);

} // namespace ksj::stats::detail::eigen
