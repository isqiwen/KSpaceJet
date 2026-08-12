#include "kspacejet/fft/detail/intel/intel_fft_transforms.hpp"

#include "intel_fft_common.hpp"

namespace ksj::fft::detail::intel {

bool fft_1d(ksj::array::VectorView<const std::complex<float>> input, ksj::array::VectorView<std::complex<float>> output,
            const Direction direction, const Normalization normalization) {
  return detail::intel_impl::fft_1d(input, output, direction, normalization);
}

bool fft_1d(ksj::array::VectorView<const std::complex<double>> input,
            ksj::array::VectorView<std::complex<double>> output, const Direction direction,
            const Normalization normalization) {
  return detail::intel_impl::fft_1d(input, output, direction, normalization);
}

bool fft_2d(ksj::array::MatrixView<const std::complex<float>> input, ksj::array::MatrixView<std::complex<float>> output,
            const Direction direction, const Normalization normalization) {
  return detail::intel_impl::fft_2d(input, output, direction, normalization);
}

bool fft_2d(ksj::array::MatrixView<const std::complex<double>> input,
            ksj::array::MatrixView<std::complex<double>> output, const Direction direction,
            const Normalization normalization) {
  return detail::intel_impl::fft_2d(input, output, direction, normalization);
}

bool fft_2d_batch(const ksj::array::PooledCube<std::complex<float>>& input,
                  ksj::array::PooledCube<std::complex<float>>& output, const Direction direction,
                  const Normalization normalization) {
  return detail::intel_impl::fft_2d_batch(input, output, direction, normalization);
}

bool fft_2d_batch(const ksj::array::PooledCube<std::complex<double>>& input,
                  ksj::array::PooledCube<std::complex<double>>& output, const Direction direction,
                  const Normalization normalization) {
  return detail::intel_impl::fft_2d_batch(input, output, direction, normalization);
}

bool fft_3d(ksj::array::CubeView<const std::complex<float>> input, ksj::array::CubeView<std::complex<float>> output,
            const Direction direction, const Normalization normalization) {
  return detail::intel_impl::fft_3d(input, output, direction, normalization);
}

bool fft_3d(ksj::array::CubeView<const std::complex<double>> input, ksj::array::CubeView<std::complex<double>> output,
            const Direction direction, const Normalization normalization) {
  return detail::intel_impl::fft_3d(input, output, direction, normalization);
}

bool fft_3d_batch(const ksj::array::PooledArray4D<std::complex<float>>& input,
                  ksj::array::PooledArray4D<std::complex<float>>& output, const Direction direction,
                  const Normalization normalization) {
  return detail::intel_impl::fft_3d_batch(input, output, direction, normalization);
}

bool fft_3d_batch(const ksj::array::PooledArray4D<std::complex<double>>& input,
                  ksj::array::PooledArray4D<std::complex<double>>& output, const Direction direction,
                  const Normalization normalization) {
  return detail::intel_impl::fft_3d_batch(input, output, direction, normalization);
}

bool fft_3d_batch_strided(const ksj::array::PooledArray4D<std::complex<float>>& input,
                          ksj::array::PooledArray4D<std::complex<float>>& output, const Direction direction,
                          const Normalization normalization) {
  return detail::intel_impl::fft_3d_batch_strided(input, output, direction, normalization);
}

bool fft_3d_batch_strided(const ksj::array::PooledArray4D<std::complex<double>>& input,
                          ksj::array::PooledArray4D<std::complex<double>>& output, const Direction direction,
                          const Normalization normalization) {
  return detail::intel_impl::fft_3d_batch_strided(input, output, direction, normalization);
}

} // namespace ksj::fft::detail::intel
