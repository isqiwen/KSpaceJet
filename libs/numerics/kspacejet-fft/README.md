# kspacejet-fft

`kspacejet-fft` 负责 FFT/IFFT 等频域变换类数值计算。公开接口使用 `kspacejet-array` 的池化对象，
例如 `ksj::array::PooledVector<std::complex<T>>`，调用方不需要感知底层后端。

重复调用时应复用输出对象。需要同时复用 pack/shift/output scratch 时，使用 `PolicyFft1Executor`；
需要缓存确定后端的 FFT descriptor 时，使用 `Fft1Plan` / `Fft2Plan` / `Fft3Plan` 或 `Fft2Executor`。
单次 1D free API 会包含后端 plan 创建：本机 policy 对长度小于 `131072` 的 `complex<float>` 和长度小于
`32768` 的 `complex<double>` 使用 Eigen，其余分别使用 MKL DFTI；反复调用同一规格应优先使用 `Fft1Plan`，
以复用 DFTI descriptor。

当前已提供：

- `fft(input, direction, normalization)` / `ifft(input, normalization)`
- `fft(input, dim, direction, normalization)` / `ifft(input, dim, normalization)`
- `fft(cube, dim, direction, normalization)` / `ifft(cube, dim, normalization)`：对 cube 中沿 `dim0`、`dim1`
  或 `dim2` 的每条线独立执行 1D FFT/IFFT。
- `fft2(input, direction, normalization)` / `ifft2(input, normalization)`
- `fft3(input, direction, normalization)` / `ifft3(input, normalization)`
- `fft2_batch(input, direction, normalization)` / `ifft2_batch(input, normalization)`
- `fft3_batch(input, direction, normalization)` / `ifft3_batch(input, normalization)`
- `fft_segmented(input, segments, direction, normalization)` / `ifft_segmented(input, segments, normalization)`
- `fft_segmented(input, dim, segments, direction, normalization)` /
  `ifft_segmented(input, dim, segments, normalization)`
- `fft(input, output, direction, normalization)` / `ifft(input, output, normalization)`
- `fft(input, output, dim, direction, normalization)` / `ifft(input, output, dim, normalization)`
- `fft(cube, output, dim, direction, normalization)` / `ifft(cube, output, dim, normalization)`
- `fft_inplace(cube, dim, direction, normalization)` / `ifft_inplace(cube, dim, normalization)`
- `fft2(input, output, direction, normalization)` / `ifft2(input, output, normalization)`
- `fft3(input, output, direction, normalization)` / `ifft3(input, output, normalization)`
- `fft2_batch(input, output, direction, normalization)` /
  `ifft2_batch(input, output, normalization)`
- `fft3_batch(input, output, direction, normalization)` /
  `ifft3_batch(input, output, normalization)`
- `fft_segmented(input, output, segments, direction, normalization)` /
  `ifft_segmented(input, output, segments, normalization)`
- `fft_segmented(input, output, dim, segments, direction, normalization)` /
  `ifft_segmented(input, output, dim, segments, normalization)`
- `Fft1Plan<T>`：缓存 1D FFT 的尺寸、方向、normalization 和可用的 MKL DFTI descriptor。
- `PolicyFft1Executor<T>`：保持 free 1D FFT API 的 runtime policy，只复用 pack、shift 和输出 scratch；
  适合业务代码需要保持既有后端选择和浮点结果的重复 FFT。
- `Fft1Executor<T>`：复用 1D scratch，并缓存固定尺寸、方向和 normalization 的 plan；支持 vector、matrix
  和 cube 的原地按轴变换。后端语义与 `Fft1Plan<T>` 相同，不应拿它替换依赖 free API policy 浮点结果的路径。
- `Fft2Plan<T>`：缓存 2D FFT 的尺寸、方向、normalization 和可用的 MKL DFTI descriptor。
- `Fft2Executor<T>`：根据 `MatrixView` 的 row/column stride 缓存 2D MKL DFTI descriptor；输入输出
  shape 和 stride 一致时直接复用，否则回退到公开 `fft2` 路径。`execute_inplace` 复用 executor 内部
  输出 scratch，避免 repeated in-place call 反复分配 alias 临时输出。executor 由调用者持有，不共享可变状态。
- `CenteredFft2Executor<T>`：缓存 centered orthonormal 2D FFT 的 MKL DFTI descriptor；KSpaceJet row-major
  contiguous `MatrixView` 是 native in-place fast path，strided view 会 pack 到 executor 内部 row-major
  scratch。该 executor 不暴露也不要求 column-major 输入。
- `Fft3Plan<T>`：缓存 3D FFT 的尺寸、方向、normalization 和可用的 MKL DFTI descriptor；
  `batch` 可按 4D array 的最后一维复用同一个 3D plan。
- `convolve2d_full_fft(input, kernel)` / `convolve2d_full_fft(input, kernel, output)`
- `convolve2d_same_fft(input, kernel)` / `convolve2d_same_fft(input, kernel, output)`
- `convolve2d_valid_fft(input, kernel)` / `convolve2d_valid_fft(input, kernel, output)`
- `correlate2d_full_fft(input, kernel)` / `correlate2d_full_fft(input, kernel, output)`
- `correlate2d_same_fft(input, kernel)` / `correlate2d_same_fft(input, kernel, output)`
- `correlate2d_valid_fft(input, kernel)` / `correlate2d_valid_fft(input, kernel, output)`
- `fftshift(input)` / `ifftshift(input)`
- `fftshift(input, output)` / `ifftshift(input, output)`
- `fftshift_in_place(data)` / `ifftshift_in_place(data)` for explicit in-place shift.

Segmented FFT API 不做隐式 shift：1D 按连续等长 segment 分段，matrix overload 使用
`ksj::array::Dim::dim1` 表示每一行沿列方向分段，使用 `ksj::array::Dim::dim0` 表示每一列沿
行方向分段。被分段维度必须能被 `segments` 整除。3D batch API 使用 4D array 的
`dim0/dim1/dim2` 表达单个 volume，`dim3` 表达 batch。

Cube 单轴 API 使用 `dim0`、`dim1`、`dim2` 表示沿对应维度的每条线独立变换；`dim3` 对 cube 无效。

实现包含这些路径：

- Intel MKL DFTI：按 `detail::fft_policy.hpp` 的 1D/2D/3D policy helper 判断当前规模是否优先尝试。
- Eigen FFT：作为通用路径和正确性基线。
- FFT-based 2D convolution/correlation：公开为显式 FFT API，不进入隐式默认 policy；支持
  full/same/valid 输出模式，same 使用 full 结果的中心裁剪，valid 要求 input 尺寸不小于 kernel。
  correlation 使用反转并共轭的 kernel。

公开 API 不暴露后端类型。`detail::fft_policy.hpp` 中的阈值必须由
`ksj_fft_backend_benchmark` 在目标机器上验证后再固化。
