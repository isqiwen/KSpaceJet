#pragma once

#include <span>

#include "kspacejet/base/types.hpp"

namespace ksj::base {

template <typename T, std::size_t Extent = std::dynamic_extent> using Span = std::span<T, Extent>;

using ByteSpan = std::span<byte>;
using ConstByteSpan = std::span<const byte>;

} // namespace ksj::base
