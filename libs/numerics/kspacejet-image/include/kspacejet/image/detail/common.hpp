#pragma once

#include "kspacejet/image/types.hpp"

#include "kspacejet/array/array.hpp"

#include <stdexcept>

namespace ksj::image::detail {

template <typename InputT, typename OutputT>
void validate_image_shape(ksj::array::ImageView<InputT> input, ksj::array::ImageView<OutputT> output,
                          const char* message) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument(message);
  }
}

inline void validate_region_grow_connectivity(const Connectivity connectivity) {
  if (connectivity != Connectivity::four && connectivity != Connectivity::eight) {
    throw std::invalid_argument("region_grow connectivity must be four or eight");
  }
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool aliases_image_view_storage(ksj::array::ImageView<InputT> input,
                                              ksj::array::ImageView<OutputT> output) noexcept {
  return !input.empty() && !output.empty() &&
         static_cast<const void*>(input.data()) == static_cast<const void*>(output.data());
}

} // namespace ksj::image::detail
