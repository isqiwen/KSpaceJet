#pragma once

/// Finite-difference and adjoint-difference operations with explicit boundary-condition semantics.

#include "kspacejet/array/indexing.hpp"

#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace ksj::array {

enum class DifferenceBoundary {
  zero,
  periodic,
};

namespace detail {

template <typename View> void validate_cube_axis(const View& view, const std::size_t axis, const char* message) {
  if (axis > 2U) {
    throw std::out_of_range(message);
  }
  if (view.extent(axis) == 0U) {
    throw std::out_of_range("cube difference axis has zero extent");
  }
}

template <typename View> void validate_array4d_axis(const View& view, const std::size_t axis, const char* message) {
  if (axis > 3U) {
    throw std::out_of_range(message);
  }
  if (view.extent(axis) == 0U) {
    throw std::out_of_range("array4d difference axis has zero extent");
  }
}

template <typename InputT, typename OutputT>
void forward_difference_contiguous(CubeView<InputT> input, CubeView<OutputT> output, const std::size_t axis,
                                   const DifferenceBoundary boundary) {
  const auto dim0 = input.dim0();
  const auto dim1 = input.dim1();
  const auto dim2 = input.dim2();
  const auto* source = input.data();
  auto* destination = output.data();

  switch (axis) {
    case 0:
      for (std::size_t i0 = 0U; i0 < dim0; ++i0) {
        const auto next_i0 = i0 + 1U;
        for (std::size_t i1 = 0U; i1 < dim1; ++i1) {
          for (std::size_t i2 = 0U; i2 < dim2; ++i2) {
            const auto index = (i0 * dim1 + i1) * dim2 + i2;
            if (next_i0 < dim0) {
              destination[index] = source[index + dim1 * dim2] - source[index];
            } else if (boundary == DifferenceBoundary::periodic) {
              destination[index] = source[i1 * dim2 + i2] - source[index];
            } else {
              destination[index] = {};
            }
          }
        }
      }
      break;
    case 1:
      for (std::size_t i0 = 0U; i0 < dim0; ++i0) {
        for (std::size_t i1 = 0U; i1 < dim1; ++i1) {
          const auto next_i1 = i1 + 1U;
          for (std::size_t i2 = 0U; i2 < dim2; ++i2) {
            const auto index = (i0 * dim1 + i1) * dim2 + i2;
            if (next_i1 < dim1) {
              destination[index] = source[index + dim2] - source[index];
            } else if (boundary == DifferenceBoundary::periodic) {
              destination[index] = source[i0 * dim1 * dim2 + i2] - source[index];
            } else {
              destination[index] = {};
            }
          }
        }
      }
      break;
    case 2:
      for (std::size_t i0 = 0U; i0 < dim0; ++i0) {
        for (std::size_t i1 = 0U; i1 < dim1; ++i1) {
          for (std::size_t i2 = 0U; i2 < dim2; ++i2) {
            const auto index = (i0 * dim1 + i1) * dim2 + i2;
            if (i2 + 1U < dim2) {
              destination[index] = source[index + 1U] - source[index];
            } else if (boundary == DifferenceBoundary::periodic) {
              destination[index] = source[(i0 * dim1 + i1) * dim2] - source[index];
            } else {
              destination[index] = {};
            }
          }
        }
      }
      break;
    default:
      break;
  }
}

template <typename InputT, typename OutputT>
void forward_difference_strided(Array4DView<InputT> input, Array4DView<OutputT> output, const std::size_t axis,
                                const DifferenceBoundary boundary) {
  const auto* source = input.data();
  auto* destination = output.data();

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        for (std::size_t i3 = 0U; i3 < input.dim3(); ++i3) {
          const auto source_index =
            i0 * input.dim0_stride() + i1 * input.dim1_stride() + i2 * input.dim2_stride() + i3 * input.dim3_stride();
          const auto destination_index = i0 * output.dim0_stride() + i1 * output.dim1_stride() +
                                         i2 * output.dim2_stride() + i3 * output.dim3_stride();

          auto next0 = i0;
          auto next1 = i1;
          auto next2 = i2;
          auto next3 = i3;
          bool has_next = true;
          switch (axis) {
            case 0:
              if (i0 + 1U < input.dim0()) {
                next0 = i0 + 1U;
              } else if (boundary == DifferenceBoundary::periodic) {
                next0 = 0U;
              } else {
                has_next = false;
              }
              break;
            case 1:
              if (i1 + 1U < input.dim1()) {
                next1 = i1 + 1U;
              } else if (boundary == DifferenceBoundary::periodic) {
                next1 = 0U;
              } else {
                has_next = false;
              }
              break;
            case 2:
              if (i2 + 1U < input.dim2()) {
                next2 = i2 + 1U;
              } else if (boundary == DifferenceBoundary::periodic) {
                next2 = 0U;
              } else {
                has_next = false;
              }
              break;
            case 3:
              if (i3 + 1U < input.dim3()) {
                next3 = i3 + 1U;
              } else if (boundary == DifferenceBoundary::periodic) {
                next3 = 0U;
              } else {
                has_next = false;
              }
              break;
            default:
              has_next = false;
              break;
          }

          if (!has_next) {
            destination[destination_index] = {};
            continue;
          }

          const auto next_index = next0 * input.dim0_stride() + next1 * input.dim1_stride() +
                                  next2 * input.dim2_stride() + next3 * input.dim3_stride();
          destination[destination_index] = source[next_index] - source[source_index];
        }
      }
    }
  }
}

template <typename InputT, typename OutputT>
void adjoint_forward_difference_strided(Array4DView<InputT> input, Array4DView<OutputT> output, const std::size_t axis,
                                        const DifferenceBoundary boundary) {
  const auto* source = input.data();
  auto* destination = output.data();

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        for (std::size_t i3 = 0U; i3 < input.dim3(); ++i3) {
          const auto source_index =
            i0 * input.dim0_stride() + i1 * input.dim1_stride() + i2 * input.dim2_stride() + i3 * input.dim3_stride();
          const auto destination_index = i0 * output.dim0_stride() + i1 * output.dim1_stride() +
                                         i2 * output.dim2_stride() + i3 * output.dim3_stride();

          auto previous0 = i0;
          auto previous1 = i1;
          auto previous2 = i2;
          auto previous3 = i3;
          bool has_previous = true;
          bool singleton_axis = false;
          switch (axis) {
            case 0:
              singleton_axis = input.dim0() == 1U;
              if (boundary == DifferenceBoundary::periodic) {
                previous0 = i0 == 0U ? input.dim0() - 1U : i0 - 1U;
              } else if (i0 == 0U) {
                has_previous = false;
              } else {
                previous0 = i0 - 1U;
              }
              break;
            case 1:
              singleton_axis = input.dim1() == 1U;
              if (boundary == DifferenceBoundary::periodic) {
                previous1 = i1 == 0U ? input.dim1() - 1U : i1 - 1U;
              } else if (i1 == 0U) {
                has_previous = false;
              } else {
                previous1 = i1 - 1U;
              }
              break;
            case 2:
              singleton_axis = input.dim2() == 1U;
              if (boundary == DifferenceBoundary::periodic) {
                previous2 = i2 == 0U ? input.dim2() - 1U : i2 - 1U;
              } else if (i2 == 0U) {
                has_previous = false;
              } else {
                previous2 = i2 - 1U;
              }
              break;
            case 3:
              singleton_axis = input.dim3() == 1U;
              if (boundary == DifferenceBoundary::periodic) {
                previous3 = i3 == 0U ? input.dim3() - 1U : i3 - 1U;
              } else if (i3 == 0U) {
                has_previous = false;
              } else {
                previous3 = i3 - 1U;
              }
              break;
            default:
              singleton_axis = true;
              break;
          }

          const auto is_last = (axis == 0U && i0 + 1U == input.dim0()) || (axis == 1U && i1 + 1U == input.dim1()) ||
                               (axis == 2U && i2 + 1U == input.dim2()) || (axis == 3U && i3 + 1U == input.dim3());

          if (singleton_axis) {
            destination[destination_index] = {};
          } else if (!has_previous) {
            destination[destination_index] = -source[source_index];
          } else if (boundary == DifferenceBoundary::zero && is_last) {
            const auto previous_index = previous0 * input.dim0_stride() + previous1 * input.dim1_stride() +
                                        previous2 * input.dim2_stride() + previous3 * input.dim3_stride();
            destination[destination_index] = source[previous_index];
          } else {
            const auto previous_index = previous0 * input.dim0_stride() + previous1 * input.dim1_stride() +
                                        previous2 * input.dim2_stride() + previous3 * input.dim3_stride();
            destination[destination_index] = source[previous_index] - source[source_index];
          }
        }
      }
    }
  }
}

template <typename InputT, typename OutputT>
void adjoint_forward_difference_contiguous(CubeView<InputT> input, CubeView<OutputT> output, const std::size_t axis,
                                           const DifferenceBoundary boundary) {
  const auto dim0 = input.dim0();
  const auto dim1 = input.dim1();
  const auto dim2 = input.dim2();
  const auto* source = input.data();
  auto* destination = output.data();

  switch (axis) {
    case 0:
      for (std::size_t i0 = 0U; i0 < dim0; ++i0) {
        for (std::size_t i1 = 0U; i1 < dim1; ++i1) {
          for (std::size_t i2 = 0U; i2 < dim2; ++i2) {
            const auto index = (i0 * dim1 + i1) * dim2 + i2;
            if (dim0 == 1U) {
              destination[index] = {};
            } else if (boundary == DifferenceBoundary::periodic) {
              const auto previous_i0 = i0 == 0U ? dim0 - 1U : i0 - 1U;
              destination[index] = source[(previous_i0 * dim1 + i1) * dim2 + i2] - source[index];
            } else if (i0 == 0U) {
              destination[index] = -source[index];
            } else if (i0 + 1U < dim0) {
              destination[index] = source[index - dim1 * dim2] - source[index];
            } else {
              destination[index] = source[index - dim1 * dim2];
            }
          }
        }
      }
      break;
    case 1:
      for (std::size_t i0 = 0U; i0 < dim0; ++i0) {
        for (std::size_t i1 = 0U; i1 < dim1; ++i1) {
          for (std::size_t i2 = 0U; i2 < dim2; ++i2) {
            const auto index = (i0 * dim1 + i1) * dim2 + i2;
            if (dim1 == 1U) {
              destination[index] = {};
            } else if (boundary == DifferenceBoundary::periodic) {
              const auto previous_i1 = i1 == 0U ? dim1 - 1U : i1 - 1U;
              destination[index] = source[(i0 * dim1 + previous_i1) * dim2 + i2] - source[index];
            } else if (i1 == 0U) {
              destination[index] = -source[index];
            } else if (i1 + 1U < dim1) {
              destination[index] = source[index - dim2] - source[index];
            } else {
              destination[index] = source[index - dim2];
            }
          }
        }
      }
      break;
    case 2:
      for (std::size_t i0 = 0U; i0 < dim0; ++i0) {
        for (std::size_t i1 = 0U; i1 < dim1; ++i1) {
          for (std::size_t i2 = 0U; i2 < dim2; ++i2) {
            const auto index = (i0 * dim1 + i1) * dim2 + i2;
            if (dim2 == 1U) {
              destination[index] = {};
            } else if (boundary == DifferenceBoundary::periodic) {
              const auto previous_i2 = i2 == 0U ? dim2 - 1U : i2 - 1U;
              destination[index] = source[(i0 * dim1 + i1) * dim2 + previous_i2] - source[index];
            } else if (i2 == 0U) {
              destination[index] = -source[index];
            } else if (i2 + 1U < dim2) {
              destination[index] = source[index - 1U] - source[index];
            } else {
              destination[index] = source[index - 1U];
            }
          }
        }
      }
      break;
    default:
      break;
  }
}

} // namespace detail

template <typename InputT, typename OutputT>
void forward_difference(CubeView<InputT> input, CubeView<OutputT> output, const std::size_t axis,
                        const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  detail::validate_same_cube_shape(input, output, "cube forward difference shape mismatch");
  if (input.empty()) {
    return;
  }
  detail::validate_cube_axis(input, axis, "cube forward difference axis must be 0, 1, or 2");
  if (input.is_contiguous() && output.is_contiguous()) {
    detail::forward_difference_contiguous(input, output, axis, boundary);
    return;
  }

  switch (axis) {
    case 0:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        const auto next_i0 = i0 + 1U;
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
            if (next_i0 < input.dim0()) {
              output(i0, i1, i2) = input(next_i0, i1, i2) - input(i0, i1, i2);
            } else if (boundary == DifferenceBoundary::periodic) {
              output(i0, i1, i2) = input(0U, i1, i2) - input(i0, i1, i2);
            } else {
              output(i0, i1, i2) = {};
            }
          }
        }
      }
      break;
    case 1:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          const auto next_i1 = i1 + 1U;
          for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
            if (next_i1 < input.dim1()) {
              output(i0, i1, i2) = input(i0, next_i1, i2) - input(i0, i1, i2);
            } else if (boundary == DifferenceBoundary::periodic) {
              output(i0, i1, i2) = input(i0, 0U, i2) - input(i0, i1, i2);
            } else {
              output(i0, i1, i2) = {};
            }
          }
        }
      }
      break;
    case 2:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
            const auto next_i2 = i2 + 1U;
            if (next_i2 < input.dim2()) {
              output(i0, i1, i2) = input(i0, i1, next_i2) - input(i0, i1, i2);
            } else if (boundary == DifferenceBoundary::periodic) {
              output(i0, i1, i2) = input(i0, i1, 0U) - input(i0, i1, i2);
            } else {
              output(i0, i1, i2) = {};
            }
          }
        }
      }
      break;
    default:
      break;
  }
}

template <typename InputT, typename OutputT>
void adjoint_forward_difference(CubeView<InputT> input, CubeView<OutputT> output, const std::size_t axis,
                                const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  detail::validate_same_cube_shape(input, output, "cube adjoint forward difference shape mismatch");
  if (input.empty()) {
    return;
  }
  detail::validate_cube_axis(input, axis, "cube adjoint forward difference axis must be 0, 1, or 2");
  if (input.is_contiguous() && output.is_contiguous()) {
    detail::adjoint_forward_difference_contiguous(input, output, axis, boundary);
    return;
  }

  switch (axis) {
    case 0:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
            if (input.dim0() == 1U) {
              output(i0, i1, i2) = {};
            } else if (boundary == DifferenceBoundary::periodic) {
              const auto previous_i0 = i0 == 0U ? input.dim0() - 1U : i0 - 1U;
              output(i0, i1, i2) = input(previous_i0, i1, i2) - input(i0, i1, i2);
            } else if (i0 == 0U) {
              output(i0, i1, i2) = -input(i0, i1, i2);
            } else if (i0 + 1U < input.dim0()) {
              output(i0, i1, i2) = input(i0 - 1U, i1, i2) - input(i0, i1, i2);
            } else {
              output(i0, i1, i2) = input(i0 - 1U, i1, i2);
            }
          }
        }
      }
      break;
    case 1:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
            if (input.dim1() == 1U) {
              output(i0, i1, i2) = {};
            } else if (boundary == DifferenceBoundary::periodic) {
              const auto previous_i1 = i1 == 0U ? input.dim1() - 1U : i1 - 1U;
              output(i0, i1, i2) = input(i0, previous_i1, i2) - input(i0, i1, i2);
            } else if (i1 == 0U) {
              output(i0, i1, i2) = -input(i0, i1, i2);
            } else if (i1 + 1U < input.dim1()) {
              output(i0, i1, i2) = input(i0, i1 - 1U, i2) - input(i0, i1, i2);
            } else {
              output(i0, i1, i2) = input(i0, i1 - 1U, i2);
            }
          }
        }
      }
      break;
    case 2:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
            if (input.dim2() == 1U) {
              output(i0, i1, i2) = {};
            } else if (boundary == DifferenceBoundary::periodic) {
              const auto previous_i2 = i2 == 0U ? input.dim2() - 1U : i2 - 1U;
              output(i0, i1, i2) = input(i0, i1, previous_i2) - input(i0, i1, i2);
            } else if (i2 == 0U) {
              output(i0, i1, i2) = -input(i0, i1, i2);
            } else if (i2 + 1U < input.dim2()) {
              output(i0, i1, i2) = input(i0, i1, i2 - 1U) - input(i0, i1, i2);
            } else {
              output(i0, i1, i2) = input(i0, i1, i2 - 1U);
            }
          }
        }
      }
      break;
    default:
      break;
  }
}

template <typename InputT, typename OutputT>
void forward_difference(Array4DView<InputT> input, Array4DView<OutputT> output, const std::size_t axis,
                        const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  detail::validate_same_array4d_shape(input, output, "array4d forward difference shape mismatch");
  if (input.empty()) {
    return;
  }
  detail::validate_array4d_axis(input, axis, "array4d forward difference axis must be 0, 1, 2, or 3");
  detail::forward_difference_strided(input, output, axis, boundary);
}

template <typename InputT, typename OutputT>
void adjoint_forward_difference(Array4DView<InputT> input, Array4DView<OutputT> output, const std::size_t axis,
                                const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  detail::validate_same_array4d_shape(input, output, "array4d adjoint forward difference shape mismatch");
  if (input.empty()) {
    return;
  }
  detail::validate_array4d_axis(input, axis, "array4d adjoint forward difference axis must be 0, 1, 2, or 3");
  detail::adjoint_forward_difference_strided(input, output, axis, boundary);
}

template <typename InputT, typename OutputT>
void forward_difference(const PooledCube<InputT>& input, PooledCube<OutputT>& output, const std::size_t axis,
                        const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  forward_difference(input.view(), output.view(), axis, boundary);
}

template <typename InputT, typename OutputT>
void adjoint_forward_difference(const PooledCube<InputT>& input, PooledCube<OutputT>& output, const std::size_t axis,
                                const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  adjoint_forward_difference(input.view(), output.view(), axis, boundary);
}

template <typename InputT, typename OutputT>
void forward_difference(const PooledArray4D<InputT>& input, PooledArray4D<OutputT>& output, const std::size_t axis,
                        const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  forward_difference(input.view(), output.view(), axis, boundary);
}

template <typename InputT, typename OutputT>
void adjoint_forward_difference(const PooledArray4D<InputT>& input, PooledArray4D<OutputT>& output,
                                const std::size_t axis, const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  adjoint_forward_difference(input.view(), output.view(), axis, boundary);
}

template <typename InputT>
[[nodiscard]] PooledCube<std::remove_const_t<InputT>>
forward_difference(CubeView<InputT> input, const std::size_t axis,
                   const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  auto output = make_pooled_cube<std::remove_const_t<InputT>>(input.dim0(), input.dim1(), input.dim2());
  forward_difference(input, output.view(), axis, boundary);
  return output;
}

template <typename InputT>
[[nodiscard]] PooledCube<std::remove_const_t<InputT>>
adjoint_forward_difference(CubeView<InputT> input, const std::size_t axis,
                           const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  auto output = make_pooled_cube<std::remove_const_t<InputT>>(input.dim0(), input.dim1(), input.dim2());
  adjoint_forward_difference(input, output.view(), axis, boundary);
  return output;
}

template <typename InputT>
[[nodiscard]] PooledArray4D<std::remove_const_t<InputT>>
forward_difference(Array4DView<InputT> input, const std::size_t axis,
                   const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  auto output =
    make_pooled_array4d<std::remove_const_t<InputT>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  forward_difference(input, output.view(), axis, boundary);
  return output;
}

template <typename InputT>
[[nodiscard]] PooledArray4D<std::remove_const_t<InputT>>
adjoint_forward_difference(Array4DView<InputT> input, const std::size_t axis,
                           const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  auto output =
    make_pooled_array4d<std::remove_const_t<InputT>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  adjoint_forward_difference(input, output.view(), axis, boundary);
  return output;
}

template <typename InputT>
[[nodiscard]] PooledCube<InputT> forward_difference(const PooledCube<InputT>& input, const std::size_t axis,
                                                    const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  return forward_difference(input.view(), axis, boundary);
}

template <typename InputT>
[[nodiscard]] PooledCube<InputT>
adjoint_forward_difference(const PooledCube<InputT>& input, const std::size_t axis,
                           const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  return adjoint_forward_difference(input.view(), axis, boundary);
}

template <typename InputT>
[[nodiscard]] PooledArray4D<InputT> forward_difference(const PooledArray4D<InputT>& input, const std::size_t axis,
                                                       const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  return forward_difference(input.view(), axis, boundary);
}

template <typename InputT>
[[nodiscard]] PooledArray4D<InputT>
adjoint_forward_difference(const PooledArray4D<InputT>& input, const std::size_t axis,
                           const DifferenceBoundary boundary = DifferenceBoundary::zero) {
  return adjoint_forward_difference(input.view(), axis, boundary);
}

} // namespace ksj::array
