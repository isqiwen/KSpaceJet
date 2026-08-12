#pragma once

/// Caller-owned reusable workspace for image algorithms that need temporary dense storage.

#include "kspacejet/array/array.hpp"

#include <cstdint>

namespace ksj::image {

struct ResizeVolumeCubicWorkspace {
  ksj::array::PooledVector<float> real_input;
  ksj::array::PooledVector<float> imag_input;
  ksj::array::PooledVector<float> real_output;
  ksj::array::PooledVector<float> imag_output;
  ksj::array::PooledVector<std::uint8_t> work_buffer;
};

} // namespace ksj::image
