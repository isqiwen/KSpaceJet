#pragma once

#include "kspacejet/base/result.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace ksj::recon::runtime {

struct ReorderBufferLimits {
  std::uint64_t max_ahead_items{0};
  std::uint64_t max_ahead_bytes{0};
};

// A deterministic, bounded reorder window for one declared order domain.  A
// caller allocates ordinals before parallel dispatch; submit returns only the
// contiguous prefix beginning at next_expected.  This header-only primitive is
// intentionally not a queue: it never blocks and has no implicit flush policy.
template <typename T> class ReorderBuffer {
public:
  explicit ReorderBuffer(std::uint64_t first_expected, ReorderBufferLimits limits)
      : next_expected_(first_expected), limits_(limits) {}

  [[nodiscard]] ksj::base::Result<std::vector<T>> submit(std::uint64_t ordinal, std::uint64_t charged_bytes,
                                                           T value) {
    if (ordinal < next_expected_) {
      return ksj::base::Status::ValidationError("reorder ordinal is behind next_expected");
    }
    if (ordinal - next_expected_ >= limits_.max_ahead_items) {
      return ksj::base::Status::Unavailable("reorder item window is full");
    }
    if (charged_bytes > limits_.max_ahead_bytes - retained_bytes_) {
      return ksj::base::Status::Unavailable("reorder byte window is full");
    }
    if (entries_.contains(ordinal)) {
      return ksj::base::Status::ValidationError("duplicate reorder ordinal");
    }

    retained_bytes_ += charged_bytes;
    entries_.emplace(ordinal, Entry{.charged_bytes = charged_bytes, .value = std::move(value)});

    std::vector<T> ready;
    for (;;) {
      const auto it = entries_.find(next_expected_);
      if (it == entries_.end()) {
        break;
      }
      retained_bytes_ -= it->second.charged_bytes;
      ready.push_back(std::move(it->second.value));
      entries_.erase(it);
      ++next_expected_;
    }
    return ready;
  }

  [[nodiscard]] ksj::base::Status close() const {
    if (!entries_.empty()) {
      return ksj::base::Status::ValidationError("REORDER_GAP_AT_EOI");
    }
    return ksj::base::Status::Ok();
  }

  [[nodiscard]] std::uint64_t next_expected() const noexcept { return next_expected_; }
  [[nodiscard]] std::uint64_t retained_items() const noexcept { return entries_.size(); }
  [[nodiscard]] std::uint64_t retained_bytes() const noexcept { return retained_bytes_; }

private:
  struct Entry {
    std::uint64_t charged_bytes;
    T value;
  };

  std::uint64_t next_expected_;
  ReorderBufferLimits limits_;
  std::uint64_t retained_bytes_{0};
  std::map<std::uint64_t, Entry> entries_;
};

} // namespace ksj::recon::runtime
