#include "kspacejet/array/detail/eigen/eigen_array_elementwise.hpp"

#include <Eigen/Core>

#include <cmath>
#include <complex>
#include <limits>

namespace ksj::array::detail::eigen {
namespace {

template <typename T> using ArrayMap = Eigen::Map<Eigen::Array<T, Eigen::Dynamic, 1>, Eigen::Unaligned>;

template <typename T> using ConstArrayMap = Eigen::Map<const Eigen::Array<T, Eigen::Dynamic, 1>, Eigen::Unaligned>;

[[nodiscard]] bool fits_eigen_index(const std::size_t size) noexcept {
  return size <= static_cast<std::size_t>(std::numeric_limits<Eigen::Index>::max());
}

template <typename T> [[nodiscard]] bool valid_contiguous_view(VectorView<T> view) noexcept {
  return view.is_contiguous() && fits_eigen_index(view.size());
}

template <typename T> [[nodiscard]] auto map(VectorView<T> view) {
  return ArrayMap<T>(view.data(), static_cast<Eigen::Index>(view.size()));
}

template <typename T> [[nodiscard]] auto const_map(VectorView<const T> view) {
  return ConstArrayMap<T>(view.data(), static_cast<Eigen::Index>(view.size()));
}

template <typename T, typename F>
[[nodiscard]] bool binary_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output, F&& f) {
  if (lhs.size() != rhs.size() || lhs.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(lhs) || !valid_contiguous_view(rhs) || !valid_contiguous_view(output)) {
    return false;
  }
  f(const_map(lhs), const_map(rhs), map(output));
  return true;
}

template <typename T, typename F>
[[nodiscard]] bool scalar_impl(VectorView<const T> input, const T scalar, VectorView<T> output, F&& f) {
  if (input.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(input) || !valid_contiguous_view(output)) {
    return false;
  }
  f(const_map(input), scalar, map(output));
  return true;
}

template <typename InputT, typename OutputT, typename F>
[[nodiscard]] bool unary_impl(VectorView<const InputT> input, VectorView<OutputT> output, F&& f) {
  if (input.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(input) || !valid_contiguous_view(output)) {
    return false;
  }
  f(const_map(input), map(output));
  return true;
}

template <typename T>
[[nodiscard]] bool add_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {
  return binary_impl(lhs, rhs, output, [](const auto& lhs_map, const auto& rhs_map, auto output_map) {
    output_map = lhs_map + rhs_map;
  });
}

template <typename T>
[[nodiscard]] bool subtract_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {
  return binary_impl(lhs, rhs, output, [](const auto& lhs_map, const auto& rhs_map, auto output_map) {
    output_map = lhs_map - rhs_map;
  });
}

template <typename T>
[[nodiscard]] bool multiply_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {
  return binary_impl(lhs, rhs, output, [](const auto& lhs_map, const auto& rhs_map, auto output_map) {
    output_map = lhs_map * rhs_map;
  });
}

template <typename T>
[[nodiscard]] bool multiply_accumulate_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {
  return binary_impl(lhs, rhs, output, [](const auto& lhs_map, const auto& rhs_map, auto output_map) {
    output_map += lhs_map * rhs_map;
  });
}

template <typename T>
[[nodiscard]] bool divide_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {
  return binary_impl(lhs, rhs, output, [](const auto& lhs_map, const auto& rhs_map, auto output_map) {
    output_map = lhs_map / rhs_map;
  });
}

template <typename T>
[[nodiscard]] bool add_scalar_impl(VectorView<const T> input, const T scalar, VectorView<T> output) {
  return scalar_impl(input, scalar, output, [](const auto& input_map, const auto value, auto output_map) {
    output_map = input_map + value;
  });
}

template <typename T>
[[nodiscard]] bool subtract_scalar_impl(VectorView<const T> input, const T scalar, VectorView<T> output) {
  return scalar_impl(input, scalar, output, [](const auto& input_map, const auto value, auto output_map) {
    output_map = input_map - value;
  });
}

template <typename T>
[[nodiscard]] bool scalar_subtract_impl(VectorView<const T> input, const T scalar, VectorView<T> output) {
  return scalar_impl(input, scalar, output, [](const auto& input_map, const auto value, auto output_map) {
    output_map = value - input_map;
  });
}

template <typename T> [[nodiscard]] bool scale_impl(VectorView<const T> input, const T scalar, VectorView<T> output) {
  return scalar_impl(input, scalar, output, [](const auto& input_map, const auto value, auto output_map) {
    output_map = input_map * value;
  });
}

template <typename T>
[[nodiscard]] bool divide_scalar_impl(VectorView<const T> input, const T scalar, VectorView<T> output) {
  return scalar_impl(input, scalar, output, [](const auto& input_map, const auto value, auto output_map) {
    output_map = input_map / value;
  });
}

template <typename T>
[[nodiscard]] bool scalar_divide_impl(VectorView<const T> input, const T scalar, VectorView<T> output) {
  return scalar_impl(input, scalar, output, [](const auto& input_map, const auto value, auto output_map) {
    output_map = value / input_map;
  });
}

template <typename T> [[nodiscard]] bool negate_impl(VectorView<const T> input, VectorView<T> output) {
  return unary_impl(input, output, [](const auto& input_map, auto output_map) {
    output_map = -input_map;
  });
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool absolute_impl(VectorView<const InputT> input, VectorView<OutputT> output) {
  return unary_impl(input, output, [](const auto& input_map, auto output_map) {
    output_map = input_map.abs();
  });
}

template <typename T> [[nodiscard]] bool square_impl(VectorView<const T> input, VectorView<T> output) {
  return unary_impl(input, output, [](const auto& input_map, auto output_map) {
    output_map = input_map * input_map;
  });
}

template <typename T> [[nodiscard]] bool inverse_impl(VectorView<const T> input, VectorView<T> output) {
  return unary_impl(input, output, [](const auto& input_map, auto output_map) {
    output_map = input_map.inverse();
  });
}

template <typename T> [[nodiscard]] bool inverse_sqrt_impl(VectorView<const T> input, VectorView<T> output) {
  return unary_impl(input, output, [](const auto& input_map, auto output_map) {
    output_map = input_map.sqrt().inverse();
  });
}

template <typename T> [[nodiscard]] bool sqrt_impl(VectorView<const T> input, VectorView<T> output) {
  return unary_impl(input, output, [](const auto& input_map, auto output_map) {
    output_map = input_map.unaryExpr([](const T& value) {
      using std::sqrt;
      return sqrt(value);
    });
  });
}

template <typename T> [[nodiscard]] bool exp_impl(VectorView<const T> input, VectorView<T> output) {
  return unary_impl(input, output, [](const auto& input_map, auto output_map) {
    output_map = input_map.unaryExpr([](const T& value) {
      using std::exp;
      return exp(value);
    });
  });
}

template <typename T> [[nodiscard]] bool log_impl(VectorView<const T> input, VectorView<T> output) {
  return unary_impl(input, output, [](const auto& input_map, auto output_map) {
    output_map = input_map.unaryExpr([](const T& value) {
      using std::log;
      return log(value);
    });
  });
}

template <typename T>
[[nodiscard]] bool minimum_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {
  return binary_impl(lhs, rhs, output, [](const auto& lhs_map, const auto& rhs_map, auto output_map) {
    output_map = lhs_map.min(rhs_map);
  });
}

template <typename T>
[[nodiscard]] bool maximum_impl(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {
  return binary_impl(lhs, rhs, output, [](const auto& lhs_map, const auto& rhs_map, auto output_map) {
    output_map = lhs_map.max(rhs_map);
  });
}

template <typename T>
[[nodiscard]] bool clamp_impl(VectorView<const T> input, const T lower, const T upper, VectorView<T> output) {
  return scalar_impl(input, lower, output, [upper](const auto& input_map, const auto low, auto output_map) {
    output_map = input_map.max(low).min(upper);
  });
}

} // namespace

#define KSJ_ARRAY_EIGEN_ELEMENTWISE_DEFS(T)                                                                            \
  bool add(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                                   \
    return add_impl(lhs, rhs, output);                                                                                 \
  }                                                                                                                    \
  bool subtract(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                              \
    return subtract_impl(lhs, rhs, output);                                                                            \
  }                                                                                                                    \
  bool multiply(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                              \
    return multiply_impl(lhs, rhs, output);                                                                            \
  }                                                                                                                    \
  bool multiply_accumulate(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                   \
    return multiply_accumulate_impl(lhs, rhs, output);                                                                 \
  }                                                                                                                    \
  bool divide(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output) {                                \
    return divide_impl(lhs, rhs, output);                                                                              \
  }                                                                                                                    \
  bool add_scalar(VectorView<const T> input, T scalar, VectorView<T> output) {                                         \
    return add_scalar_impl(input, scalar, output);                                                                     \
  }                                                                                                                    \
  bool subtract_scalar(VectorView<const T> input, T scalar, VectorView<T> output) {                                    \
    return subtract_scalar_impl(input, scalar, output);                                                                \
  }                                                                                                                    \
  bool scalar_subtract(VectorView<const T> input, T scalar, VectorView<T> output) {                                    \
    return scalar_subtract_impl(input, scalar, output);                                                                \
  }                                                                                                                    \
  bool scale(VectorView<const T> input, T scalar, VectorView<T> output) {                                              \
    return scale_impl(input, scalar, output);                                                                          \
  }                                                                                                                    \
  bool divide_scalar(VectorView<const T> input, T scalar, VectorView<T> output) {                                      \
    return divide_scalar_impl(input, scalar, output);                                                                  \
  }                                                                                                                    \
  bool scalar_divide(VectorView<const T> input, T scalar, VectorView<T> output) {                                      \
    return scalar_divide_impl(input, scalar, output);                                                                  \
  }                                                                                                                    \
  bool negate(VectorView<const T> input, VectorView<T> output) {                                                       \
    return negate_impl(input, output);                                                                                 \
  }                                                                                                                    \
  bool square(VectorView<const T> input, VectorView<T> output) {                                                       \
    return square_impl(input, output);                                                                                 \
  }                                                                                                                    \
  bool sqrt(VectorView<const T> input, VectorView<T> output) {                                                         \
    return sqrt_impl(input, output);                                                                                   \
  }                                                                                                                    \
  bool exp(VectorView<const T> input, VectorView<T> output) {                                                          \
    return exp_impl(input, output);                                                                                    \
  }                                                                                                                    \
  bool log(VectorView<const T> input, VectorView<T> output) {                                                          \
    return log_impl(input, output);                                                                                    \
  }

KSJ_ARRAY_EIGEN_ELEMENTWISE_DEFS(ksj::base::f32)
KSJ_ARRAY_EIGEN_ELEMENTWISE_DEFS(ksj::base::f64)
KSJ_ARRAY_EIGEN_ELEMENTWISE_DEFS(ksj::base::cf32)
KSJ_ARRAY_EIGEN_ELEMENTWISE_DEFS(ksj::base::cf64)

#undef KSJ_ARRAY_EIGEN_ELEMENTWISE_DEFS

bool absolute(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return absolute_impl(input, output);
}

bool absolute(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return absolute_impl(input, output);
}

bool absolute(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output) {
  return absolute_impl(input, output);
}

bool absolute(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output) {
  return absolute_impl(input, output);
}

bool inverse(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return inverse_impl(input, output);
}

bool inverse(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return inverse_impl(input, output);
}

bool inverse_sqrt(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return inverse_sqrt_impl(input, output);
}

bool inverse_sqrt(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return inverse_sqrt_impl(input, output);
}

bool minimum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
             VectorView<ksj::base::f32> output) {
  return minimum_impl(lhs, rhs, output);
}

bool minimum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
             VectorView<ksj::base::f64> output) {
  return minimum_impl(lhs, rhs, output);
}

bool maximum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
             VectorView<ksj::base::f32> output) {
  return maximum_impl(lhs, rhs, output);
}

bool maximum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
             VectorView<ksj::base::f64> output) {
  return maximum_impl(lhs, rhs, output);
}

bool clamp(VectorView<const ksj::base::f32> input, ksj::base::f32 lower, ksj::base::f32 upper,
           VectorView<ksj::base::f32> output) {
  return clamp_impl(input, lower, upper, output);
}

bool clamp(VectorView<const ksj::base::f64> input, ksj::base::f64 lower, ksj::base::f64 upper,
           VectorView<ksj::base::f64> output) {
  return clamp_impl(input, lower, upper, output);
}

} // namespace ksj::array::detail::eigen
