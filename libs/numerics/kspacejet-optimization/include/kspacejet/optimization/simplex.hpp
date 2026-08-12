#pragma once

/// Nelder-Mead simplex optimization API and convergence-control parameters.

#include "kspacejet/array/array.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace ksj::optimization::detail::simplex {

template <class T, class Objective>
void downhill(Objective& objective, ksj::array::VectorView<const T> initial,
              ksj::array::VectorView<const T> lower_bounds, ksj::array::VectorView<const T> upper_bounds,
              ksj::array::VectorView<T> result, T tolerance, ksj::array::MatrixView<const T> simplex, int iterations) {
  const auto dimensions = initial.size();
  if (lower_bounds.size() != dimensions || upper_bounds.size() != dimensions || result.size() != dimensions) {
    throw std::invalid_argument("downhill_simplex vector dimension mismatch");
  }
  if (!simplex.empty() && (simplex.rows() != dimensions + 1U || simplex.cols() != dimensions)) {
    throw std::invalid_argument("downhill_simplex simplex dimension mismatch");
  }

  auto initial_values = ksj::array::make_pooled_vector(initial);
  auto lower_bound_values = ksj::array::make_pooled_vector(lower_bounds);
  auto upper_bound_values = ksj::array::make_pooled_vector(upper_bounds);
  auto simplex_points = ksj::array::make_pooled_matrix<T>(dimensions + 1U, dimensions);
  auto previous_centroid = ksj::array::make_pooled_vector<T>(dimensions);
  auto current_centroid = ksj::array::make_pooled_vector<T>(dimensions);
  auto values = ksj::array::make_pooled_vector<T>(dimensions + 1U);
  auto centroid = ksj::array::make_pooled_vector<T>(dimensions);
  auto reflected = ksj::array::make_pooled_vector<T>(dimensions);
  auto expanded = ksj::array::make_pooled_vector<T>(dimensions);
  auto contracted = ksj::array::make_pooled_vector<T>(dimensions);

  auto as_const = [](ksj::array::VectorView<T> input) {
    return ksj::array::as_const_view(input);
  };

  auto clamp_to_bounds = [&](ksj::array::VectorView<T> vertex) {
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      vertex(dimension) = std::clamp(vertex(dimension), lower_bound_values(dimension), upper_bound_values(dimension));
    }
  };
  clamp_to_bounds(initial_values.view());

  auto copy_vector = [dimensions](auto input, ksj::array::VectorView<T> destination) {
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      destination(dimension) = input(dimension);
    }
  };

  if (simplex.empty()) {
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      auto vertex = simplex_points.row(dimension);
      copy_vector(initial_values.view(), vertex);

      auto delta = initial_values(dimension) / static_cast<T>(20);
      if (std::abs(delta) <= std::numeric_limits<T>::epsilon()) {
        delta = (upper_bound_values(dimension) - lower_bound_values(dimension)) / static_cast<T>(20);
      }
      if (std::abs(delta) <= std::numeric_limits<T>::epsilon()) {
        delta = static_cast<T>(0.05);
      }

      vertex(dimension) += delta;
      clamp_to_bounds(vertex);
    }
    copy_vector(initial_values.view(), simplex_points.row(dimensions));

    const auto centroid_scale = static_cast<T>(dimensions + 1U);
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      previous_centroid(dimension) = initial_values(dimension) * centroid_scale;
    }
  } else {
    for (std::size_t vertex = 0; vertex < dimensions + 1U; ++vertex) {
      auto simplex_row = simplex_points.row(vertex);
      for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
        simplex_row(dimension) = simplex(vertex, dimension);
      }
      clamp_to_bounds(simplex_row);
    }
    ksj::array::fill(previous_centroid.view(), T{});
  }

  for (int iteration = 0; iteration < iterations; ++iteration) {
    for (std::size_t vertex = 0; vertex < dimensions + 1U; ++vertex) {
      values(vertex) = objective(as_const(simplex_points.row(vertex)));
    }

    std::size_t best = 0;
    std::size_t second_worst = 0;
    std::size_t worst = 0;
    for (std::size_t vertex = 0; vertex < values.size(); ++vertex) {
      if (values(vertex) < values(best)) {
        best = vertex;
      }
      if (values(vertex) > values(worst)) {
        worst = vertex;
      }
    }

    second_worst = best;
    for (std::size_t vertex = 0; vertex < values.size(); ++vertex) {
      if (values(vertex) < values(worst) && values(vertex) > values(second_worst)) {
        second_worst = vertex;
      }
    }

    ksj::array::fill(centroid.view(), T{});
    for (std::size_t vertex = 0; vertex < dimensions + 1U; ++vertex) {
      if (vertex != worst) {
        const auto point = simplex_points.row(vertex);
        for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
          centroid(dimension) += point(dimension);
        }
      }
    }

    const auto worst_point = simplex_points.row(worst);
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      current_centroid(dimension) = centroid(dimension) + worst_point(dimension);
    }
    const auto average_scale = static_cast<T>(dimensions);
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      centroid(dimension) /= average_scale;
    }

    T difference = 0;
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      difference += std::abs(previous_centroid(dimension) - current_centroid(dimension));
    }
    if (difference / static_cast<T>(dimensions) < tolerance) {
      break;
    }
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      previous_centroid(dimension) = current_centroid(dimension);
    }

    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      reflected(dimension) = centroid(dimension) + (centroid(dimension) - worst_point(dimension));
    }
    clamp_to_bounds(reflected.view());

    const T reflected_value = objective(as_const(reflected.view()));
    if (values(best) <= reflected_value && reflected_value <= values(second_worst)) {
      copy_vector(reflected.view(), simplex_points.row(worst));
    } else if (reflected_value < values(best)) {
      for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
        expanded(dimension) = reflected(dimension) + (reflected(dimension) - centroid(dimension));
      }
      clamp_to_bounds(expanded.view());
      copy_vector(objective(as_const(expanded.view())) < reflected_value ? expanded.view() : reflected.view(),
                  simplex_points.row(worst));
    } else {
      for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
        contracted(dimension) =
          centroid(dimension) + static_cast<T>(0.5) * (worst_point(dimension) - centroid(dimension));
      }
      clamp_to_bounds(contracted.view());

      if (objective(as_const(contracted.view())) < values(worst)) {
        copy_vector(contracted.view(), simplex_points.row(worst));
      } else {
        const auto best_point = simplex_points.row(best);
        for (std::size_t vertex = 0; vertex < dimensions + 1U; ++vertex) {
          if (vertex == best) {
            continue;
          }
          auto point = simplex_points.row(vertex);
          for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
            point(dimension) = best_point(dimension) + static_cast<T>(0.5) * (point(dimension) - best_point(dimension));
          }
          clamp_to_bounds(point);
        }
      }
    }
  }

  std::size_t best = 0;
  T best_value = objective(as_const(simplex_points.row(0)));
  for (std::size_t vertex = 1; vertex < dimensions + 1U; ++vertex) {
    const T value = objective(as_const(simplex_points.row(vertex)));
    if (value < best_value) {
      best = vertex;
      best_value = value;
    }
  }

  const auto best_point = simplex_points.row(best);
  for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
    result(dimension) = best_point(dimension);
  }
}

} // namespace ksj::optimization::detail::simplex

namespace ksj::optimization {

template <class T, class Objective>
void downhill_simplex(Objective& objective, ksj::array::VectorView<const T> initial,
                      ksj::array::VectorView<const T> lower_bounds, ksj::array::VectorView<const T> upper_bounds,
                      ksj::array::VectorView<T> output,
                      T tolerance = static_cast<T>(1E8) * std::numeric_limits<T>::epsilon(),
                      ksj::array::MatrixView<const T> simplex = {}, int iterations = 10000) {
  detail::simplex::downhill(objective, initial, lower_bounds, upper_bounds, output, tolerance, simplex, iterations);
}

template <class T, class Objective>
void downhill_simplex(Objective& objective, ksj::array::VectorView<T> initial, ksj::array::VectorView<T> lower_bounds,
                      ksj::array::VectorView<T> upper_bounds, ksj::array::VectorView<T> output,
                      T tolerance = static_cast<T>(1E8) * std::numeric_limits<T>::epsilon(),
                      ksj::array::MatrixView<T> simplex = {}, int iterations = 10000)
  requires(!std::is_const_v<T>)
{
  downhill_simplex(objective, ksj::array::as_const_view(initial), ksj::array::as_const_view(lower_bounds),
                   ksj::array::as_const_view(upper_bounds), output, tolerance, ksj::array::as_const_view(simplex),
                   iterations);
}

template <class T, class Objective>
[[nodiscard]] ksj::array::PooledVector<T>
downhill_simplex(Objective& objective, ksj::array::VectorView<const T> initial,
                 ksj::array::VectorView<const T> lower_bounds, ksj::array::VectorView<const T> upper_bounds,
                 T tolerance = static_cast<T>(1E8) * std::numeric_limits<T>::epsilon(),
                 ksj::array::MatrixView<const T> simplex = {}, int iterations = 10000) {
  auto output = ksj::array::make_pooled_vector<T>(initial.size());
  downhill_simplex(objective, initial, lower_bounds, upper_bounds, output.view(), tolerance, simplex, iterations);
  return output;
}

template <class T, class Objective>
[[nodiscard]] ksj::array::PooledVector<T>
downhill_simplex(Objective& objective, ksj::array::VectorView<T> initial, ksj::array::VectorView<T> lower_bounds,
                 ksj::array::VectorView<T> upper_bounds,
                 T tolerance = static_cast<T>(1E8) * std::numeric_limits<T>::epsilon(),
                 ksj::array::MatrixView<T> simplex = {}, int iterations = 10000)
  requires(!std::is_const_v<T>)
{
  return downhill_simplex(objective, ksj::array::as_const_view(initial), ksj::array::as_const_view(lower_bounds),
                          ksj::array::as_const_view(upper_bounds), tolerance, ksj::array::as_const_view(simplex),
                          iterations);
}

template <class T, class Objective>
void downhill_simplex(Objective& objective, const ksj::array::PooledVector<T>& initial,
                      const ksj::array::PooledVector<T>& lower_bounds, const ksj::array::PooledVector<T>& upper_bounds,
                      ksj::array::PooledVector<T>& output,
                      T tolerance = static_cast<T>(1E8) * std::numeric_limits<T>::epsilon(), int iterations = 10000) {
  downhill_simplex(objective, initial.view(), lower_bounds.view(), upper_bounds.view(), output.view(), tolerance, {},
                   iterations);
}

template <class T, class Objective>
[[nodiscard]] ksj::array::PooledVector<T>
downhill_simplex(Objective& objective, const ksj::array::PooledVector<T>& initial,
                 const ksj::array::PooledVector<T>& lower_bounds, const ksj::array::PooledVector<T>& upper_bounds,
                 T tolerance = static_cast<T>(1E8) * std::numeric_limits<T>::epsilon(), int iterations = 10000) {
  return downhill_simplex(objective, initial.view(), lower_bounds.view(), upper_bounds.view(), tolerance, {},
                          iterations);
}

template <class T, class Objective>
void downhill_simplex(Objective& objective, const ksj::array::PooledVector<T>& initial,
                      const ksj::array::PooledVector<T>& lower_bounds, const ksj::array::PooledVector<T>& upper_bounds,
                      const ksj::array::PooledMatrix<T>& simplex, ksj::array::PooledVector<T>& output,
                      T tolerance = static_cast<T>(1E8) * std::numeric_limits<T>::epsilon(), int iterations = 10000) {
  downhill_simplex(objective, initial.view(), lower_bounds.view(), upper_bounds.view(), output.view(), tolerance,
                   simplex.view(), iterations);
}

template <class T, class Objective>
[[nodiscard]] ksj::array::PooledVector<T>
downhill_simplex(Objective& objective, const ksj::array::PooledVector<T>& initial,
                 const ksj::array::PooledVector<T>& lower_bounds, const ksj::array::PooledVector<T>& upper_bounds,
                 const ksj::array::PooledMatrix<T>& simplex,
                 T tolerance = static_cast<T>(1E8) * std::numeric_limits<T>::epsilon(), int iterations = 10000) {
  return downhill_simplex(objective, initial.view(), lower_bounds.view(), upper_bounds.view(), tolerance,
                          simplex.view(), iterations);
}

template <class T, class Objective>
[[nodiscard]] std::vector<T> downhill_simplex(Objective& objective, std::vector<T> initial,
                                              const std::vector<T>& lower_bounds, const std::vector<T>& upper_bounds,
                                              T tolerance = static_cast<T>(1E8) * std::numeric_limits<T>::epsilon(),
                                              std::vector<std::vector<T>> simplex = {}, int iterations = 10000) {
  const auto initial_view = ksj::array::VectorView<const T>(initial.data(), initial.size());
  const auto lower_bounds_view = ksj::array::VectorView<const T>(lower_bounds.data(), lower_bounds.size());
  const auto upper_bounds_view = ksj::array::VectorView<const T>(upper_bounds.data(), upper_bounds.size());

  auto simplex_storage = ksj::array::PooledMatrix<T>{};
  auto simplex_view = ksj::array::MatrixView<const T>{};
  if (!simplex.empty()) {
    simplex_storage = ksj::array::make_pooled_matrix<T>(simplex.size(), initial.size());
    for (std::size_t row = 0; row < simplex.size(); ++row) {
      if (simplex[row].size() != initial.size()) {
        throw std::invalid_argument("downhill_simplex simplex row dimension mismatch");
      }
      for (std::size_t col = 0; col < simplex[row].size(); ++col) {
        simplex_storage(row, col) = simplex[row][col];
      }
    }
    simplex_view =
      ksj::array::MatrixView<const T>(simplex_storage.data(), simplex_storage.rows(), simplex_storage.cols());
  }

  const auto output = downhill_simplex(objective, initial_view, lower_bounds_view, upper_bounds_view, tolerance,
                                       simplex_view, iterations);
  auto result = std::vector<T>(output.size());
  for (std::size_t i = 0; i < output.size(); ++i) {
    result[i] = output(i);
  }
  return result;
}

} // namespace ksj::optimization
