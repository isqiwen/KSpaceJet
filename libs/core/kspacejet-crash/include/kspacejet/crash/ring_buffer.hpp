#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace ksj::crash {

class RingBuffer {
public:
  static constexpr std::size_t kCapacity = 256;
  static constexpr std::size_t kThreadNameCapacity = 32;
  static constexpr std::size_t kCategoryCapacity = 32;
  static constexpr std::size_t kMessageCapacity = 192;

  struct Entry {
    std::uint64_t sequence = 0;
    std::uint64_t monotonic_ms = 0;
    std::uint64_t thread_id = 0;
    std::array<char, kThreadNameCapacity> thread_name{};
    std::array<char, kCategoryCapacity> category{};
    std::array<char, kMessageCapacity> message{};
  };

  RingBuffer() = default;
  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;

  void push(std::uint64_t thread_id, std::string_view thread_name, std::string_view category, std::string_view message,
            std::uint64_t monotonic_ms) noexcept;
  void clear() noexcept;

  [[nodiscard]] std::uint64_t published_count() const noexcept { return write_index_.load(std::memory_order_acquire); }

  [[nodiscard]] constexpr std::size_t capacity() const noexcept { return kCapacity; }

  [[nodiscard]] std::size_t snapshot_recent(Entry* destination, std::size_t max_entries) const noexcept;

  template <typename Visitor> void for_each_recent(std::size_t max_entries, Visitor&& visitor) const noexcept {
    const std::uint64_t published = published_count();
    if (published == 0 || max_entries == 0) {
      return;
    }

    const std::uint64_t clamped_limit = static_cast<std::uint64_t>((max_entries < kCapacity) ? max_entries : kCapacity);
    std::uint64_t first_sequence = 1;
    if (published > clamped_limit) {
      first_sequence = published - clamped_limit + 1;
    }
    if (published > kCapacity) {
      const std::uint64_t earliest_available = published - kCapacity + 1;
      if (first_sequence < earliest_available) {
        first_sequence = earliest_available;
      }
    }

    for (std::uint64_t sequence = first_sequence; sequence <= published; ++sequence) {
      const Slot& slot = slots_[(sequence - 1) % kCapacity];
      const std::uint64_t observed_sequence = slot.sequence.load(std::memory_order_acquire);
      if (observed_sequence != sequence) {
        continue;
      }
      visitor(slot.entry);
    }
  }

private:
  struct Slot {
    Entry entry{};
    std::atomic<std::uint64_t> sequence{0};
  };

  static void copy_text(std::string_view source, char* destination, std::size_t destination_size) noexcept;

  mutable std::mutex write_mutex_{};
  std::atomic<std::uint64_t> write_index_{0};
  std::array<Slot, kCapacity> slots_{};
};

[[nodiscard]] RingBuffer& GlobalRingBuffer() noexcept;

} // namespace ksj::crash
