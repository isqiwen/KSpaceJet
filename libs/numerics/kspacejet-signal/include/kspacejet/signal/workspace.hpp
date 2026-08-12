#pragma once

/// Reusable caller-owned workspace for signal algorithms that require temporary dense storage.

#include "kspacejet/array/array.hpp"

#include <cstdint>

namespace ksj::signal {

template <typename T> struct FirFilterWorkspace {
  ksj::array::PooledVector<std::uint8_t> spec_storage;
  ksj::array::PooledVector<std::uint8_t> buffer_storage;
  ksj::array::PooledVector<T> temp_output;
};

template <typename T> struct IirFilterWorkspace {
  ksj::array::PooledVector<T> taps_storage;
  ksj::array::PooledVector<std::uint8_t> state_storage;
  ksj::array::PooledVector<T> temp_output;
};

template <typename T> struct MedianFilterWorkspace {
  ksj::array::PooledVector<std::uint8_t> buffer_storage;
  ksj::array::PooledVector<T> temp_output;
  ksj::array::PooledVector<T> window_storage;
};

} // namespace ksj::signal
