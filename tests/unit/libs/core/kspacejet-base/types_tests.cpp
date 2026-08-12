#include "kspacejet/base/types.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

static_assert(std::is_same_v<ksj::base::byte, std::byte>);

static_assert(std::is_same_v<ksj::base::i8, std::int8_t>);
static_assert(std::is_same_v<ksj::base::i16, std::int16_t>);
static_assert(std::is_same_v<ksj::base::i32, std::int32_t>);
static_assert(std::is_same_v<ksj::base::i64, std::int64_t>);

static_assert(std::is_same_v<ksj::base::u8, std::uint8_t>);
static_assert(std::is_same_v<ksj::base::u16, std::uint16_t>);
static_assert(std::is_same_v<ksj::base::u32, std::uint32_t>);
static_assert(std::is_same_v<ksj::base::u64, std::uint64_t>);

static_assert(std::is_same_v<ksj::base::f32, float>);
static_assert(std::is_same_v<ksj::base::f64, double>);

static_assert(std::is_same_v<ksj::base::cf32, std::complex<ksj::base::f32>>);
static_assert(std::is_same_v<ksj::base::cf64, std::complex<ksj::base::f64>>);

} // namespace
