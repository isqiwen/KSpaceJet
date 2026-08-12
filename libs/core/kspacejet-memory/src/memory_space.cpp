#include "kspacejet/memory/memory_space.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

#if KSJ_MEMORY_HAVE_LIBNUMA
#include <numa.h>
#include <sys/mman.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <malloc.h>
#endif

namespace ksj::memory {

namespace {

inline constexpr std::size_t kMinimumCacheLineSize = 64;
inline constexpr std::size_t kDefaultHugePageSize = 2ULL * 1024ULL * 1024ULL;

[[nodiscard]] std::size_t sysconf_size_or(const int name, const std::size_t fallback) noexcept {
#if KSJ_MEMORY_HAVE_LIBNUMA
  const auto value = ::sysconf(name);
  if (value <= 0) {
    return fallback;
  }
  return static_cast<std::size_t>(value);
#else
  (void)name;
  return fallback;
#endif
}

[[nodiscard]] std::size_t round_up_power_of_two(std::size_t value) noexcept {
  if (std::has_single_bit(value)) {
    return value;
  }
  return std::bit_ceil(value);
}

[[nodiscard]] std::size_t round_up_to_multiple(const std::size_t value, const std::size_t multiple) {
  if (multiple == 0) {
    return value;
  }
  if (value > std::numeric_limits<std::size_t>::max() - (multiple - 1U)) {
    throw std::overflow_error("allocation size overflow");
  }
  return ((value + multiple - 1U) / multiple) * multiple;
}

void request_transparent_hugepage(std::byte* ptr, const std::size_t bytes) noexcept {
#if defined(MADV_HUGEPAGE) && KSJ_MEMORY_HAVE_LIBNUMA
  if (ptr != nullptr && bytes != 0) {
    static_cast<void>(::madvise(ptr, bytes, MADV_HUGEPAGE));
  }
#else
  (void)ptr;
  (void)bytes;
#endif
}

} // namespace

NumaHostSpace::NumaHostSpace(const std::size_t numa_node) noexcept : numa_node_(numa_node) {}

const char* NumaHostSpace::name() noexcept {
  return "numa_host";
}

bool NumaHostSpace::available() noexcept {
#if KSJ_MEMORY_HAVE_LIBNUMA
  return numa_available() >= 0;
#else
  return true;
#endif
}

std::size_t NumaHostSpace::cache_line_size() noexcept {
#if defined(_SC_LEVEL1_DCACHE_LINESIZE) && KSJ_MEMORY_HAVE_LIBNUMA
  const auto detected = sysconf_size_or(_SC_LEVEL1_DCACHE_LINESIZE, kMinimumCacheLineSize);
#else
  const auto detected = kMinimumCacheLineSize;
#endif
  return std::max(kMinimumCacheLineSize, round_up_power_of_two(detected));
}

RawAllocation NumaHostSpace::allocate(const std::string_view label, const std::size_t bytes,
                                      const std::size_t alignment, const PagePolicy page_policy,
                                      const bool lock_pages) const {
  (void)label;
  if (bytes == 0) {
    return RawAllocation{.data = nullptr, .bytes = 0, .numa_node = numa_node_};
  }
  if (alignment == 0 || !std::has_single_bit(alignment)) {
    throw std::invalid_argument("NumaHostSpace alignment must be a non-zero power of two");
  }
  const auto effective_alignment = std::max(alignment, cache_line_size());
#if KSJ_MEMORY_HAVE_LIBNUMA
  const auto page_size = sysconf_size_or(_SC_PAGESIZE, 4096);
#else
  const auto page_size = std::size_t{4096};
#endif
  if (effective_alignment > page_size) {
    throw std::invalid_argument("NumaHostSpace alignment greater than page size is not supported yet");
  }
#if KSJ_MEMORY_HAVE_LIBNUMA
  if (!available()) {
    throw std::runtime_error("libnuma is available at link time, but NUMA support is unavailable at runtime");
  }

  void* ptr = nullptr;
  std::size_t mapped_bytes = bytes;
  RawAllocationKind kind = RawAllocationKind::numa_alloc;

  if (page_policy == PagePolicy::explicit_hugepage) {
#ifdef MAP_HUGETLB
    mapped_bytes = round_up_to_multiple(bytes, kDefaultHugePageSize);
    ptr = ::mmap(nullptr, mapped_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (ptr == MAP_FAILED) {
      throw std::bad_alloc();
    }
    numa_tonode_memory(ptr, mapped_bytes, static_cast<int>(numa_node_));
    kind = RawAllocationKind::mmap_hugepage;
#else
    throw std::runtime_error("explicit hugepage allocation is not supported on this platform");
#endif
  } else {
    ptr = numa_alloc_onnode(bytes, static_cast<int>(numa_node_));
  }
#else
  if (page_policy == PagePolicy::explicit_hugepage) {
    throw std::runtime_error("explicit hugepage allocation is not supported without libnuma");
  }
  if (lock_pages) {
    throw std::runtime_error("pinned host memory is not supported without libnuma");
  }
  void* ptr = nullptr;
  std::size_t mapped_bytes = bytes;
  RawAllocationKind kind = RawAllocationKind::numa_alloc;
#if defined(_WIN32)
  ptr = _aligned_malloc(bytes, effective_alignment);
#else
  mapped_bytes = round_up_to_multiple(bytes, effective_alignment);
  ptr = std::aligned_alloc(effective_alignment, mapped_bytes);
#endif
#endif

  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  if ((reinterpret_cast<std::uintptr_t>(ptr) % effective_alignment) != 0) {
#if KSJ_MEMORY_HAVE_LIBNUMA
    if (kind == RawAllocationKind::mmap_hugepage) {
      static_cast<void>(::munmap(ptr, mapped_bytes));
    } else {
      numa_free(ptr, bytes);
    }
#else
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
#endif
    throw std::runtime_error("host allocator returned memory that does not satisfy cache-line alignment");
  }
  if (page_policy == PagePolicy::transparent_hugepage) {
    request_transparent_hugepage(static_cast<std::byte*>(ptr), bytes);
  }
#if KSJ_MEMORY_HAVE_LIBNUMA
  if (lock_pages && ::mlock(ptr, mapped_bytes) != 0) {
    if (kind == RawAllocationKind::mmap_hugepage) {
      static_cast<void>(::munmap(ptr, mapped_bytes));
    } else {
      numa_free(ptr, bytes);
    }
    throw std::runtime_error("failed to lock NUMA host memory pages");
  }
#else
  (void)lock_pages;
#endif
  return RawAllocation{
    .data = static_cast<std::byte*>(ptr),
    .bytes = bytes,
    .mapped_bytes = mapped_bytes,
    .numa_node = numa_node_,
    .kind = kind,
    .locked = lock_pages,
  };
}

void NumaHostSpace::deallocate(RawAllocation allocation) const noexcept {
  if (allocation.data == nullptr || allocation.bytes == 0) {
    return;
  }
  const auto mapped_bytes = allocation.mapped_bytes == 0 ? allocation.bytes : allocation.mapped_bytes;
#if KSJ_MEMORY_HAVE_LIBNUMA
  if (allocation.locked) {
    static_cast<void>(::munlock(allocation.data, mapped_bytes));
  }
  if (allocation.kind == RawAllocationKind::mmap_hugepage) {
    static_cast<void>(::munmap(allocation.data, mapped_bytes));
    return;
  }
  numa_free(allocation.data, allocation.bytes);
#else
  (void)mapped_bytes;
  (void)allocation;
#if defined(_WIN32)
  _aligned_free(allocation.data);
#else
  std::free(allocation.data);
#endif
#endif
}

} // namespace ksj::memory
