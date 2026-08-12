#include "kspacejet/crash/ring_buffer.hpp"

#include <algorithm>
#include <cstring>

namespace ksj::crash {

namespace {

constexpr char kUnknownThreadName[] = "unknown-thread";
constexpr char kUnknownCategory[] = "general";

} // namespace

void RingBuffer::copy_text(std::string_view source, char* destination, std::size_t destination_size) noexcept {
  if (destination == nullptr || destination_size == 0) {
    return;
  }

  const std::size_t copy_size = std::min(source.size(), destination_size - 1);
  if (copy_size > 0) {
    std::memcpy(destination, source.data(), copy_size);
  }
  destination[copy_size] = '\0';
  if (copy_size + 1 < destination_size) {
    std::memset(destination + copy_size + 1, 0, destination_size - copy_size - 1);
  }
}

void RingBuffer::push(std::uint64_t thread_id, std::string_view thread_name, std::string_view category,
                      std::string_view message, std::uint64_t monotonic_ms) noexcept {
  if (thread_name.empty()) {
    thread_name = kUnknownThreadName;
  }
  if (category.empty()) {
    category = kUnknownCategory;
  }

  const std::lock_guard<std::mutex> lock(write_mutex_);
  const std::uint64_t sequence = write_index_.fetch_add(1, std::memory_order_acq_rel) + 1;
  Slot& slot = slots_[(sequence - 1) % kCapacity];

  slot.sequence.store(0, std::memory_order_relaxed);
  slot.entry.sequence = sequence;
  slot.entry.monotonic_ms = monotonic_ms;
  slot.entry.thread_id = thread_id;
  copy_text(thread_name, slot.entry.thread_name.data(), slot.entry.thread_name.size());
  copy_text(category, slot.entry.category.data(), slot.entry.category.size());
  copy_text(message, slot.entry.message.data(), slot.entry.message.size());
  std::atomic_thread_fence(std::memory_order_release);
  slot.sequence.store(sequence, std::memory_order_release);
}

void RingBuffer::clear() noexcept {
  const std::lock_guard<std::mutex> lock(write_mutex_);
  write_index_.store(0, std::memory_order_release);
  for (Slot& slot : slots_) {
    slot.entry = Entry{};
    slot.sequence.store(0, std::memory_order_release);
  }
}

std::size_t RingBuffer::snapshot_recent(Entry* destination, std::size_t max_entries) const noexcept {
  if (destination == nullptr || max_entries == 0) {
    return 0;
  }

  std::size_t copied = 0;
  for_each_recent(max_entries, [&](const Entry& entry) noexcept {
    if (copied < max_entries) {
      destination[copied++] = entry;
    }
  });
  return copied;
}

RingBuffer& GlobalRingBuffer() noexcept {
  static RingBuffer ring_buffer;
  return ring_buffer;
}

} // namespace ksj::crash
