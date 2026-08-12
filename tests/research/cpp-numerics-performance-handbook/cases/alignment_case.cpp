#include "common.hpp"

#include <cstdint>

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T> class OffsetVector {
public:
  explicit OffsetVector(std::size_t size) : storage_(size + 1U), size_(size) {}

  [[nodiscard]] T* data() noexcept { return storage_.data() + 1U; }
  [[nodiscard]] const T* data() const noexcept { return storage_.data() + 1U; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }

  [[nodiscard]] T& operator()(std::size_t index) noexcept { return data()[index]; }
  [[nodiscard]] const T& operator()(std::size_t index) const noexcept { return data()[index]; }

private:
  std::vector<T, AlignedAllocator<T>> storage_{};
  std::size_t size_{0};
};

inline void require_not_cache_line_aligned(std::string_view name, const void* ptr) {
  if (ptr == nullptr) {
    throw std::runtime_error("research object has null storage: " + std::string{name});
  }
  const auto address = reinterpret_cast<std::uintptr_t>(ptr);
  if (address % kCacheLineAlignment == 0U) {
    throw std::runtime_error("research object unexpectedly cache-line aligned: " + std::string{name});
  }
}

template <typename T> void fill_offset_vector(OffsetVector<T>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    vector(i) = static_cast<T>(static_cast<double>((i % 251U) + 1U) * 0.125);
  }
}

template <typename T> [[nodiscard]] double checksum_offset_vector(const OffsetVector<T>& vector) {
  auto sum = T{};
  for (std::size_t i = 0; i < vector.size(); ++i) {
    sum += vector(i);
  }
  return static_cast<double>(sum);
}

template <typename T> void pointer_fma(const T* lhs, const T* rhs, T* output, std::size_t size, T scale) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = lhs[i] * rhs[i] + scale;
  }
}

template <typename T> void aligned_eigen_fma(const Vector<T>& lhs, const Vector<T>& rhs, Vector<T>& output, T scale) {
  auto output_map = as_eigen(output);
  const auto lhs_map = as_eigen(lhs);
  const auto rhs_map = as_eigen(rhs);
  output_map.array() = lhs_map.array() * rhs_map.array() + scale;
}

template <typename T>
void unaligned_eigen_fma(const OffsetVector<T>& lhs, const OffsetVector<T>& rhs, OffsetVector<T>& output, T scale) {
  using vector_type = Eigen::Matrix<T, Eigen::Dynamic, 1>;
  using map_type = Eigen::Map<vector_type, Eigen::Unaligned>;
  using const_map_type = Eigen::Map<const vector_type, Eigen::Unaligned>;

  map_type output_map(output.data(), static_cast<Eigen::Index>(output.size()));
  const_map_type lhs_map(lhs.data(), static_cast<Eigen::Index>(lhs.size()));
  const_map_type rhs_map(rhs.data(), static_cast<Eigen::Index>(rhs.size()));
  output_map.array() = lhs_map.array() * rhs_map.array() + scale;
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto aligned_lhs = make_vector<T>(size);
    auto aligned_rhs = make_vector<T>(size);
    auto aligned_output = make_vector<T>(size);
    auto unaligned_lhs = OffsetVector<T>(size);
    auto unaligned_rhs = OffsetVector<T>(size);
    auto unaligned_output = OffsetVector<T>(size);

    require_cache_line_aligned("alignment aligned lhs", aligned_lhs.data());
    require_cache_line_aligned("alignment aligned rhs", aligned_rhs.data());
    require_cache_line_aligned("alignment aligned output", aligned_output.data());
    require_not_cache_line_aligned("alignment unaligned lhs", unaligned_lhs.data());
    require_not_cache_line_aligned("alignment unaligned rhs", unaligned_rhs.data());
    require_not_cache_line_aligned("alignment unaligned output", unaligned_output.data());

    fill_vector(aligned_lhs);
    fill_vector(aligned_rhs);
    fill_offset_vector(unaligned_lhs);
    fill_offset_vector(unaligned_rhs);

    const auto aligned_pointer = measure(config, [&] {
      pointer_fma(aligned_lhs.data(), aligned_rhs.data(), aligned_output.data(), aligned_output.size(), T{0.125});
      do_not_optimize(aligned_output.data()[0]);
    });
    print_row("alignment", "aligned_pointer_fma", type_name, size, 0, config, aligned_pointer,
              checksum(aligned_output));

    const auto unaligned_pointer = measure(config, [&] {
      pointer_fma(unaligned_lhs.data(), unaligned_rhs.data(), unaligned_output.data(), unaligned_output.size(),
                  T{0.125});
      do_not_optimize(unaligned_output.data()[0]);
    });
    print_row("alignment", "unaligned_pointer_fma", type_name, size, 0, config, unaligned_pointer,
              checksum_offset_vector(unaligned_output));

    const auto aligned_eigen = measure(config, [&] {
      aligned_eigen_fma(aligned_lhs, aligned_rhs, aligned_output, T{0.125});
      do_not_optimize(aligned_output.data()[0]);
    });
    print_row("alignment", "aligned_eigen_fma", type_name, size, 0, config, aligned_eigen, checksum(aligned_output));

    const auto unaligned_eigen = measure(config, [&] {
      unaligned_eigen_fma(unaligned_lhs, unaligned_rhs, unaligned_output, T{0.125});
      do_not_optimize(unaligned_output.data()[0]);
    });
    print_row("alignment", "unaligned_eigen_fma", type_name, size, 0, config, unaligned_eigen,
              checksum_offset_vector(unaligned_output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_alignment [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
