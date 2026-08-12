# kspacejet-signal

`kspacejet-signal` 负责通用信号处理：window、convolution、resample、phase unwrap 和
2D correlation 等数值核。它不包含 MRI 业务语义，只依赖 numerics 数据对象和后端实现。

当前已提供：

- `window<T>(size, kind)`
- `triangle_filter`、`half_hamming_filter`、`hamming_bandpass_filter`、
  `dual_hamming_bandpass_filter`、`half_hann_filter`、`half_blackman_filter`、
  `hbrr_filter`、`tukey_filter`、`exponential_filter`、`fermi_filter`、
  `quadratic_exponential_filter`、`t2_linear_filter`、`t2_exponential_filter`
- `cosine_laplacian_denominator(rows, cols, dimension)`
- `fermi_bandpass_window(size, low_radius, high_radius, width)`
- `dual_fermi_band_window(size, center_offset, radius, width)`
- `convolve(signal, kernel)`
- `resample(signal, output_size, ResampleKernel::linear|nearest|cubic|mitchell|lanczos3)`
- `wrap_phase(vector_or_image)` / `unwrap_phase(vector)` / `unwrap_phase_2d(image)`
- `compose_separable_kernel(row_kernel, col_kernel)`
- `correlate2d_same(image, kernel)`
- `correlate2d_same_fft(image, kernel)`
- `correlate2d_same_separable(image, row_kernel, col_kernel)`

公开算法接口遵循 numerics 约定：核心路径使用 `VectorView` / `ImageView` 显式分离输入输出，
返回 `PooledVector<T>` / `PooledImage<T>` 的 overload 只是便捷包装，负责创建输出对象并转发到
View-output 版本。当前后端结构：

- `window`：Eigen 路径和 Intel IPP 候选路径，具体阈值由 `detail::signal_policy.hpp` 控制。
- `convolve`：优先尝试 Intel IPP，失败或类型不支持时 fallback 到 Eigen/reference path。
- `correlate2d_same`：Eigen/reference path、OpenCV `filter2D` 候选路径、Intel IPP float large-kernel
  候选路径和 FFT-based large-kernel path。float/double 在 input pixels `>=64` 且 kernel pixels `>=9`
  时优先尝试 OpenCV；kernel pixels `>=31x31` 时，float 从 `32x32` 起优先尝试 IPP
  `ippiCrossCorrNorm_32f_C1R` 的 unnormalized same-correlation path，double 在 `32x32..256x256` 切 FFT，
  其他尺寸继续 OpenCV，失败 fallback Eigen。
- `correlate2d_same_separable`：Eigen two-pass reference path 与 OpenCV `sepFilter2D` 候选路径；float/double
  在 input pixels `>=256` 且 row+col kernel elements `>=6` 时优先尝试 OpenCV，失败 fallback Eigen。
- `resample`、2D phase unwrap：当前为 Eigen/reference path，benchmark 已覆盖返回值 public API，后续再根据
  完整生产 sweep 决定是否引入 IPP/OpenCV/FFT 后端。

最近一次 correlation policy benchmark：

```bash
tools/devenv/linux/run.sh python tools/ksj_numerics_benchmark/run.py \
  --bin-dir out/build/linux-release-benchmark/bin \
  --iterations 20 --trials 5 --sizes 32,64,128,256,512 --only signal
```

报告：`out/benchmarks/kspacejet-numerics-suite/20260616-111920/benchmark_report.md`。这次结果显示
`correlate2d_same_5x5` 的 OpenCV `filter2D` path 对 float/double、`32..512` 全部明显领先；
`20260616-111951` 小尺寸 sweep 进一步确认 `8x8` 起 OpenCV 仍有优势，因此当前 public policy 从
input pixels `64` 起切 OpenCV。

`20260616-112835` sweep 新增 separable correlation 对比，显示 `opencv_sepfilter2d` 在 `16x16` 起快于
Eigen separable path，并且在较大图像上快于 full-kernel `filter2D`。`20260616-112932` policy 校验确认
`correlate2d_same_separable` 在 `8x8` 保持 Eigen，在 `16x16` 起切 OpenCV `sepFilter2D`。

`20260616-132217` sweep 新增 `correlate2d_same_31x31`，发现朴素线性卷积尺寸 padding 会在
`512x512` 退化；`20260616-132351` 改为 2/3/5 smooth FFT padding 后，detail FFT path 在 float
`64..512` 和 double `32..256` 上领先。`20260616-132529` policy 校验确认 public policy 已按这些
阈值切换；double `512x512` 保持 OpenCV。

`20260616-142540` sweep 新增 IPP `ippiCrossCorrNorm_32f_C1R` 候选，使用
`ippiROISame | ippiNormNone` 对齐现有 zero-padded same-correlation 语义。结果显示 float
`32..512` 的 `correlate2d_same_31x31` 全部由 IPP 领先；`20260616-142706` validation sweep 后已反写
float large-kernel public policy。double 仍没有 IPP path，继续使用 FFT/OpenCV policy。

`20260616-135002` sweep 新增 `resample_lanczos3_x2`。`lanczos3` 使用归一化 3-lobe windowed-sinc
reference path 和 replicate 边界，质量目标高于 cubic，但在 `32..512` 上约为 cubic 的一个数量级成本；
当前不反写后端 policy，保持返回值 public API。

`20260616-162626` sweep 新增 `resample_mitchell_x2`。Mitchell-Netravali kernel 使用 `B=C=1/3`
和 replicate 边界，质量/成本位于 cubic 与 lanczos3 之间；当前保持 reference path，不引入额外后端 policy。

`resample_nearest_x2`、`resample_linear_x2`、`resample_cubic_x2`、`resample_mitchell_x2`、
`resample_lanczos3_x2`、`window_fermi_bandpass` 和 `window_dual_fermi_band` 仍以返回值 public API 作为公开路径。
`unwrap_phase_2d` 暂保持 Eigen/reference path。
