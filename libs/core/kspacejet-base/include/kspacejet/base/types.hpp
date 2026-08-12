#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>

namespace ksj::base {

using byte = std::byte;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using f32 = float;
using f64 = double;

using cf32 = std::complex<f32>;
using cf64 = std::complex<f64>;

inline constexpr std::size_t kCacheLineSize = 64;

} // namespace ksj::base
