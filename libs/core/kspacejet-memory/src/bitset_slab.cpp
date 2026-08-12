#include "kspacejet/memory/detail/bitset_slab.hpp"

#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ksj::memory::detail {

namespace {

[[nodiscard]] std::size_t checked_capacity(const std::size_t block_size, const std::size_t block_count) {
  if (block_count != 0 && block_size > std::numeric_limits<std::size_t>::max() / block_count) {
    throw std::overflow_error("BitsetSlab capacity overflow");
  }
  return block_size * block_count;
}

} // namespace

BitsetSlab::BitsetSlab(const std::size_t numa_node, const std::size_t block_size, const std::size_t block_count,
                       const std::size_t alignment, const PagePolicy page_policy, const std::size_t worker_hint_count,
                       const std::string_view label)
    : block_size_(block_size), block_count_(block_count), word_count_((block_count + 63U) / 64U),
      words_(std::make_unique<std::atomic<std::uint64_t>[]>(word_count_)), worker_hint_count_(worker_hint_count),
      worker_hints_(worker_hint_count_ == 0 ? nullptr : std::make_unique<ScanCursor[]>(worker_hint_count_)) {
  if (block_size_ == 0 || block_count_ == 0) {
    throw std::invalid_argument("BitsetSlab requires non-zero block size and block count");
  }
  if ((block_size_ % NumaHostSpace::cache_line_size()) != 0) {
    throw std::invalid_argument("BitsetSlab block size must be a multiple of cache line size");
  }
  for (std::size_t i = 0; i < word_count_; ++i) {
    words_[i].store(0, std::memory_order_relaxed);
  }
  for (std::size_t i = 0; i < worker_hint_count_; ++i) {
    worker_hints_[i].value.store(i, std::memory_order_relaxed);
  }
  allocation_ =
    NumaHostSpace(numa_node).allocate(label, checked_capacity(block_size_, block_count_), alignment, page_policy);
}

BitsetSlab::~BitsetSlab() {
  NumaHostSpace(allocation_.numa_node).deallocate(allocation_);
}

std::uint64_t BitsetSlab::valid_mask(const std::size_t word_index) const noexcept {
  const auto remaining = block_count_ - (word_index * 64U);
  if (remaining >= 64U) {
    return ~std::uint64_t{0};
  }
  return (std::uint64_t{1} << remaining) - 1U;
}

std::uint64_t BitsetSlab::next_scan_start(const std::size_t worker_index) noexcept {
  if (worker_hint_count_ == 0 || worker_hints_ == nullptr) {
    return hint_.fetch_add(1, std::memory_order_relaxed);
  }
  return worker_hints_[worker_index % worker_hint_count_].value.fetch_add(1, std::memory_order_relaxed);
}

std::byte* BitsetSlab::try_acquire(const std::size_t worker_index) noexcept {
  if (allocation_.data == nullptr) {
    return nullptr;
  }

  const auto start = next_scan_start(worker_index);
  for (std::size_t probe = 0; probe < word_count_; ++probe) {
    const auto word_index = (start + probe) % word_count_;
    const auto mask = valid_mask(word_index);
    auto snapshot = words_[word_index].load(std::memory_order_relaxed);

    while ((~snapshot & mask) != 0) {
      const auto free_bits = ~snapshot & mask;
      const auto bit = static_cast<std::size_t>(std::countr_zero(free_bits));
      const auto bit_mask = std::uint64_t{1} << bit;
      auto expected = snapshot;
      if (words_[word_index].compare_exchange_weak(expected, snapshot | bit_mask, std::memory_order_acq_rel,
                                                   std::memory_order_relaxed)) {
        used_count_.fetch_add(1, std::memory_order_relaxed);
        const auto block_index = (word_index * 64U) + bit;
        return allocation_.data + (block_index * block_size_);
      }
      snapshot = expected;
    }
  }
  return nullptr;
}

bool BitsetSlab::contains(const void* ptr) const noexcept {
  const auto* bytes = static_cast<const std::byte*>(ptr);
  return allocation_.data != nullptr && bytes >= allocation_.data && bytes < allocation_.data + capacity_bytes();
}

std::size_t BitsetSlab::block_index_from_ptr(const std::byte* ptr) const noexcept {
  return static_cast<std::size_t>(ptr - allocation_.data) / block_size_;
}

bool BitsetSlab::release(std::byte* ptr) noexcept {
  if (!contains(ptr)) {
    return false;
  }
  const auto offset = static_cast<std::size_t>(ptr - allocation_.data);
  if ((offset % block_size_) != 0) {
    return false;
  }

  const auto block_index = block_index_from_ptr(ptr);
  const auto word_index = block_index / 64U;
  const auto bit = block_index % 64U;
  const auto bit_mask = std::uint64_t{1} << bit;
  const auto previous = words_[word_index].fetch_and(~bit_mask, std::memory_order_acq_rel);
  if ((previous & bit_mask) == 0) {
    return false;
  }
  used_count_.fetch_sub(1, std::memory_order_relaxed);
  return true;
}

} // namespace ksj::memory::detail
