#pragma once

#include <cstddef>

namespace ksj::base {

inline constexpr std::size_t kKSpaceJetWordSize = sizeof(int);

template <typename T> [[nodiscard]] constexpr std::size_t ksj_word_count() noexcept {
  return (sizeof(T) + (kKSpaceJetWordSize - 1U)) / kKSpaceJetWordSize;
}

} // namespace ksj::base
