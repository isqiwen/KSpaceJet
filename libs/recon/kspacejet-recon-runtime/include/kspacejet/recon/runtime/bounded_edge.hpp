#pragma once

#include "kspacejet/base/result.hpp"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace ksj::recon::runtime {

enum class EdgeState {
  open,
  close_pending,
  closed,
};

struct BoundedEdgeCapacity {
  std::uint64_t items{0};
  std::uint64_t charged_bytes{0};
};

// A FIFO edge with explicit item and charged-byte caps.  All operations are
// nonblocking: callers register/resume their continuations externally instead
// of waiting for queue capacity while occupying a compute worker.  EndOfInput
// changes Open to ClosePending; only the consumer's final pop changes it to
// Closed, so control closure can never overtake queued MRI data.
template <typename T> class BoundedEdge {
public:
  explicit BoundedEdge(BoundedEdgeCapacity capacity) : capacity_(capacity) {}

  [[nodiscard]] ksj::base::Status try_push(T value, std::uint64_t charged_bytes) {
    std::lock_guard lock(mutex_);
    if (state_ != EdgeState::open) {
      return ksj::base::Status::StateError("cannot publish to a closing or closed edge");
    }
    if (queue_.size() >= capacity_.items || charged_bytes > capacity_.charged_bytes - queued_bytes_) {
      return ksj::base::Status::Unavailable("bounded edge capacity is exhausted");
    }
    queue_.push_back(Entry{.charged_bytes = charged_bytes, .value = std::move(value)});
    queued_bytes_ += charged_bytes;
    return ksj::base::Status::Ok();
  }

  [[nodiscard]] std::optional<T> try_pop() {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
      if (state_ == EdgeState::close_pending) {
        state_ = EdgeState::closed;
      }
      return std::nullopt;
    }
    auto entry = std::move(queue_.front());
    queue_.pop_front();
    queued_bytes_ -= entry.charged_bytes;
    if (queue_.empty() && state_ == EdgeState::close_pending) {
      state_ = EdgeState::closed;
    }
    return std::move(entry.value);
  }

  [[nodiscard]] ksj::base::Status close_input() {
    std::lock_guard lock(mutex_);
    if (state_ != EdgeState::open) {
      return ksj::base::Status::StateError("EndOfInput was already applied to edge");
    }
    state_ = queue_.empty() ? EdgeState::closed : EdgeState::close_pending;
    return ksj::base::Status::Ok();
  }

  [[nodiscard]] EdgeState state() const {
    std::lock_guard lock(mutex_);
    return state_;
  }
  [[nodiscard]] std::uint64_t queued_items() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
  }
  [[nodiscard]] std::uint64_t queued_bytes() const {
    std::lock_guard lock(mutex_);
    return queued_bytes_;
  }

private:
  struct Entry {
    std::uint64_t charged_bytes;
    T value;
  };

  const BoundedEdgeCapacity capacity_;
  mutable std::mutex mutex_;
  std::deque<Entry> queue_;
  std::uint64_t queued_bytes_{0};
  EdgeState state_{EdgeState::open};
};

} // namespace ksj::recon::runtime
