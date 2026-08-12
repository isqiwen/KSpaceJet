#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_types.hpp"

#include <optional>

namespace ksj::stats::detail::eigen {

struct LinearFitParameters {
  double slope{};
  double intercept{};
};

[[nodiscard]] std::optional<LinearFitParameters> linear_fit(ksj::array::VectorView<const float> x,
                                                            ksj::array::VectorView<const float> y);
[[nodiscard]] std::optional<LinearFitParameters> linear_fit(ksj::array::VectorView<const float> x,
                                                            ksj::array::VectorView<const double> y);
[[nodiscard]] std::optional<LinearFitParameters> linear_fit(ksj::array::VectorView<const double> x,
                                                            ksj::array::VectorView<const float> y);
[[nodiscard]] std::optional<LinearFitParameters> linear_fit(ksj::array::VectorView<const double> x,
                                                            ksj::array::VectorView<const double> y);

} // namespace ksj::stats::detail::eigen
