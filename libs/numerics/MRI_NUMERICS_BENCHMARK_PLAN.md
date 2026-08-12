# MRI numerics benchmark plan

本文件记录 `libs/mri` 只读审计后，`libs/numerics` 后续抽象和性能优化的 benchmark 路线。`libs/mri` 仍然作为需求来源，不作为 benchmark 链接依赖，也不在本轮迁移中修改。

## 原则

- 继续抽象前先补 benchmark。没有性能数据前，不修改 `libs/numerics` 的 backend policy 或阈值。
- benchmark 使用 `libs/numerics` 的公开 API 和 KSpaceJet pooled array/matrix/image/cube 对象。
- 输入规模优先模拟 MRI 重建常见形状，而不是只跑玩具尺寸。
- benchmark 结果只说明当前机器和当前构建的表现；正式 policy 仍需要 `docs/conventions/benchmark.md` 要求的完整报告。

## 从 MRI 代码得到的计算清单

| 计算面 | MRI 只读证据 | numerics 状态 | 下一步 |
| --- | --- | --- | --- |
| Complex magnitude/phase/polar/conjugate | `src/matrix/q_abs2.cpp`, `src/matrix/q_polar1.cpp`, `src/matrix/q_rect1.cpp`, `src/matrix/q_conj1.cpp` | 第一批 API 已补入 `kspacejet-array` | benchmark `complex_magnitude`, `complex_phase`, `rectangular_to_polar` |
| FFT/shift | `src/kspace/q_ft1.cpp`, `src/kspace/q_ft2.cpp`, `src/kspace/q_ift1.cpp`, `src/kspace/q_ift2.cpp`, `ksj::fft` public API | 1D FFT 有 policy 和 cached descriptor plan，2D/3D FFT 有默认 MKL policy 和 cached MKL descriptor plan，centered 2D FFT 有 row-major cached MKL descriptor executor，3D batch FFT 有 MKL batch policy，FFTW 3D/3D batch 候选已验证但不进入默认 policy，2D/3D FFT/shift 有 reference API 和 plan API，2D batch、1D/2D block FFT、FFT-based 2D full/same/valid convolution/correlation 已有显式 API | benchmark `fft1d`、`fftshift2`、`fft2`、`centered_fft2`、`fft2_batch_x8`、`fft3_slab`、`fft3_batch_x4`、`fft1d_blocks_x8`、`fft2_blocks_cols_x4`、`fft2_blocks_rows_x4`、`convolve2d_full_15x15`、`convolve2d_same_15x15`、`convolve2d_valid_15x15`、`correlate2d_full_15x15`、`correlate2d_same_15x15`、`correlate2d_valid_15x15`；后续补 block/overlap FFT composition |
| Coil/RSS reductions | 历史 MRI pipeline 的 coil combine 阶段 | `kspacejet-stats` 已有 vector/cube SoS/RSS | benchmark coil count 8/16/32 的 `root_sum_of_squares_across_dim2` |
| Image padding/resize/filter/morphology | `src/shared/uniformity/uniformity.cpp`, `src/shared/mask/basic_morphology.cpp`, `src/shared/mask/morphology.cpp` | `kspacejet-image` 已有 pad/crop/ROI copy/nearest-linear-cubic-area-lanczos4 resize/connected components/region grow/box/gaussian/bilateral/median/Sobel/gradient/Laplacian/unsharp/morphology，旧 padding 头已删除 | benchmark resize variants、crop、connected components、region grow、box/gaussian/bilateral/median filter、Sobel/gradient/Laplacian/unsharp、morph close；后续补更多高级 filter variants |
| Phase wrap/unwrap | 历史 MRI field-mapping workflow | `kspacejet-signal` 已有 1D wrap/unwrap、row/column 2D unwrap 和 Laplacian FFT 2D unwrap reference path | 完整生产 sweep 后再决定 2D unwrap backend policy |
| Window/filter composition | `src/filtering/q_fermi.cpp`, `src/filtering/q_expon.cpp`, `src/filtering/q_filter.cpp`, `src/filtering/q_lowpass2.cpp` | `kspacejet-signal` 已有 Tukey/exponential/Fermi/band-pass/dual-band window、convolve fallback、separable kernel composition、2D same-correlation/separable correlation OpenCV policy、IPP float large-kernel correlation policy、FFT large-kernel correlation policy、1D nearest/linear/cubic/mitchell/lanczos3 resample | 后续补更多 resampling/filter variants |
| Dense linear algebra | `src/pi/sms/sms.cpp`, `src/pi/grappa/grappa.cpp`, `src/storage/q_send_row_data.cpp` | `kspacejet-linalg` 已有 solve/inverse/axpy/scale/norm/covariance/whitening/Cholesky/QR/SVD values/full SVD U/V/self-adjoint eigen/general eigen/small solve/least-squares variants；float/double vector 与 matrix RHS LU/Cholesky/QR/SVD values/full SVD/self-adjoint eigen 已有 MKL LAPACKE policy；real/complex covariance 和 whiten_samples 已有 MKL GEMM policy；real/complex whitening matrix 已有 LAPACKE self-adjoint eigen policy；complex LU/Cholesky/QR solve、singular values、full SVD 和 Hermitian eigen 已有 LAPACKE policy；complex double general eigen 已有 LAPACKE `zgeev` policy，real/complex float general eigen 仍为 candidate；SVD least-squares 已有 LAPACKE `gelss` policy；rank-deficient least-squares 已有 Eigen COD / LAPACKE `gelsy` matrix-RHS policy | 后续继续评估更多 solver variants |
| Non-Cartesian/NUFFT | `kspacejet-nufft`, `src/noncartesian`, `src/shared/noncartesian` | `kspacejet-nufft` 已迁入 `libs/numerics`，仍保留 historical compatibility API | 继续把 public API 收敛到 `View/Pooled`，并补 gridding/DCF/interp table benchmark |

## 已根据 benchmark 反写的 API 调整

- `kspacejet-linalg`：`scale` / `axpy` 的返回值 API 保留，新增 `scale` / `axpy`，用于重建热路径复用输出缓冲，避免把分配成本混进逐点向量更新。
- `kspacejet-fft`：FFT/IFFT 和 `fftshift` / `ifftshift` 的返回值 API 保留，新增 1D/2D `fft` / `ifft` / `fft2` / `ifft2` / `fftshift` / `ifftshift`，用于 k-space 热路径复用输出缓冲。
- `kspacejet-fft`：`Fft1Plan<T>` / `Fft2Plan<T>` / `Fft3Plan<T>` 缓存可用的 MKL DFTI descriptor；
  `Fft3Plan<T>::batch` 可跨调用复用同一个 3D MKL descriptor。公开 API 不提供调用方可见的
  scratch 复用对象。
- `kspacejet-fft`：2D/3D 默认 policy 已按 benchmark 反写：2D complex float 从 `32x32` 起切 MKL，
  complex double 从 `16x16` 起切 MKL；3D complex float/double 从 `4x4x4` 起切 MKL。
- `kspacejet-fft`：基于 `PooledArray4D<std::complex<T>>` 的 `fft3_batch` / `fft3_batch` /
  `ifft3_batch` / `ifft3_batch` 已落地，其中 `dim0/dim1/dim2` 表达单个 volume，`dim3`
  表达 batch；公开 output-buffer API 按 3D policy 优先尝试 MKL batch descriptor。
- `kspacejet-fft`：新增 optional detail 级 FFTW 3D/3D batch 候选；构建发现 `FFTW3::fftw3` /
  `FFTW3::fftw3f` 时启用 `KSJ_NUMERICS_HAS_FFTW`，并用反向维度顺序适配 KSpaceJet row-fastest
  cube/4D 连续布局。float/double 单测已覆盖 single volume 和 batch correctness；`20260616-161416`
  sweep 显示 `fftw_estimate` / `fftw_many_estimate` 在 `16..128` 上均明显慢于
  `intel_mkl` / `intel_mkl_batch`，因此不反写默认 policy，仅保留为 benchmark candidate。
- `kspacejet-fft`：新增 `convolve2d_same_fft` / `convolve2d_same_fft`、
  `convolve2d_valid_fft` / `convolve2d_valid_fft`、`correlate2d_same_fft` /
  `correlate2d_same_fft`、`correlate2d_valid_fft` / `correlate2d_valid_fft`，并保留
  `convolve2d_full_fft` / `convolve2d_full_fft`、`correlate2d_full_fft` /
  `correlate2d_full_fft`。same 是 full 结果中心裁剪，valid 要求 input 尺寸不小于 kernel。
- `kspacejet-signal`：新增返回式 `correlate2d_same_fft`；`20260616-132351` sweep 采用 2/3/5 smooth FFT
  padding 后，`31x31` large-kernel correlation 在 float `64..512` 与 double `32..256` 上由 detail
  FFT path 领先；`20260616-132529` validation sweep 后已反写默认 policy，double `512x512` 保持 OpenCV。
- `kspacejet-signal`：新增 detail 级 IPP `ippiCrossCorrNorm_32f_C1R` 候选，用
  `ippiROISame | ippiNormNone` 对齐现有 zero-padded same-correlation 语义；`20260616-142540`
  sweep 显示 `correlate2d_same_31x31` 的 float `32..512` 全部由 IPP 领先，`20260616-142706`
  validation sweep 后已反写 float large-kernel public policy。double 仍无 IPP path，继续使用 FFT/OpenCV
  policy。
- `kspacejet-fft`：新增 `fft_segmented` / `ifft_segmented`；API 表达沿 `ksj::array::Dim`
  的 segmented FFT，不做隐式 shift。
- `kspacejet-image`：`resize_linear` / `box_filter` / `dilate` / `erode` / `morph_open` / `morph_close` 的返回值 API 保留，新增对应 `*` API；`box_filter_5x5` 和 `morph_close_5x5` 基于 benchmark 从 `4096` pixels 起切 OpenCV，`resize_linear_half` 对 float 从 `4096` pixels 起切 OpenCV，对 double 仅在 `4096..65536` pixels 区间切 OpenCV。
- `kspacejet-image`：新增 `gaussian_blur` / `median_filter` 及对应 `*` API；`gaussian_blur_5x5` 对 float/double 从 `4096` pixels 起切 OpenCV，`median_filter_3x3` 对 float 从 `4096` pixels 起切 OpenCV，double 仍保持 Eigen/reference path。
- `kspacejet-image`：新增 `bilateral_filter` / `bilateral_filter`，reference path 使用圆形邻域、空间权重和强度权重归一化以对齐 OpenCV bilateral semantics；`20260616-162626` sweep 显示 `bilateral_filter_5x5` 的 float 在 `32..128` 上由 OpenCV 明显领先，`20260616-162727` validation sweep 后已反写 policy，float 从 `1024` pixels 起切 OpenCV，double 保持 Eigen/reference path。
- `kspacejet-image`：新增 `sobel_x` / `sobel_y` / `gradient_magnitude` 及对应 `*` API；Sobel 和 gradient magnitude 对 float/double 从 `4096` pixels 起切 OpenCV，Eigen gradient fallback 使用 fused 3x3 path，避免 two-pass Sobel 中间图。
- `kspacejet-image`：新增 `crop` / `center_crop` / `copy_roi` 及对应 `*` API；crop/ROI 保持 Eigen/reference path，热路径优先使用 output-buffer 版本复用输出缓冲。
- `kspacejet-image`：新增 `connected_components` / `connected_components`，label 类型为 `std::int32_t`，背景为 `0`，支持 4/8 连通并可返回 area/bbox/centroid stats；`connected_components_8` benchmark 显示 double 从 `1024` pixels 起切 OpenCV，float 从 `16384` pixels 起切 OpenCV。
- `kspacejet-image`：新增 `resize_nearest` / `resize_cubic` / 泛型 `resize(..., ResizeMethod)` 及对应 `*` API；`resize_nearest_half` 和 `resize_cubic_half` 对 float/double 从 `4096` pixels 起切 OpenCV，cubic reference path 使用 OpenCV 兼容的 `a=-0.75` kernel。
- `kspacejet-image`：新增 `resize_area` / `resize_area` 和 `ResizeMethod::area`，reference path 使用精确面积覆盖平均，OpenCV 候选使用 `INTER_AREA`；`20260616-133427` sweep 显示 `resize_area_half` 的 float/double 在 `32..512` 上均由 OpenCV/public output-buffer 路径领先，因此非空 float/double area resize 直接优先尝试 OpenCV。
- `kspacejet-image`：新增 `resize_lanczos4` / `resize_lanczos4` 和 `ResizeMethod::lanczos4`，
  reference path 使用归一化 4-lobe windowed-sinc 权重和 replicate 边界，OpenCV 候选使用
  `INTER_LANCZOS4`；`20260616-153400` sweep 显示 `32..512` 均由 OpenCV 明显领先，
  `20260616-153455` 小尺寸 sweep 显示 `8x8` 起 OpenCV 仍领先；`20260616-153549` validation
  sweep 后已反写 policy，float/double 从 `64` pixels 起切 OpenCV。
- `kspacejet-linalg`：新增 `covariance` / `covariance`、`whitening_matrix_from_covariance`、`whiten_samples` / `whiten_samples`、`cholesky_lower`、`solve_cholesky`、`solve_qr`、`singular_values`、`solve_small`；`20260616-100941` smoke benchmark 显示 output-buffer path 对 scale/axpy/whiten_samples 更适合热路径复用，Cholesky solve 在 SPD case 下优于通用 LU，2x2 `solve_small` 需要按 scalar type 和生产机器继续确认。
- `kspacejet-linalg`：新增 detail 级 MKL LAPACKE 候选后端 `solve_lu`、`cholesky_lower`、
  `solve_cholesky`、`solve_qr`、`singular_values`；`20260616-110621` sweep 用
  `16,32,64,128,256` 复核 LU/Cholesky/QR/SVD values，随后 `20260616-111114` 通过
  `public_policy` benchmark 行校验公开 API。当前 policy：LU float >=64、double >=32 切 LAPACKE；
  Cholesky factor 非空 float/double 切 LAPACKE；Cholesky solve >=64 切 LAPACKE；QR float >=64 columns、
  double >=128 columns 切 LAPACKE；SVD values 非空 float/double 切 LAPACKE；全部保留 Eigen fallback。
- `kspacejet-linalg`：新增 matrix RHS 的 LAPACKE 多 RHS 候选后端，`solve` / `solve_cholesky` /
  `solve_qr` 的 matrix RHS public API 已按独立 policy 选择；新增 `svd` / `full_svd`、`self_adjoint_eigen_decomposition`、
  `eigen_decomposition`、`solve_least_squares(..., LeastSquaresSolver)`，其中 least-squares 支持 QR、SVD 和
  normal-equations variants。`20260616-164326` 小 sweep 显示 matrix RHS 的 LAPACKE 胜出点早于 vector
  RHS：LU matrix RHS float >=32、double >=16；Cholesky matrix RHS >=32；QR matrix RHS float >=64 columns、
  double >=64 columns；`20260616-164643` validation sweep 已复核 public policy rows。normal-equations 在
  well-conditioned benchmark 输入上最快，但默认仍保持 QR，避免隐式降低数值稳定性。
- `kspacejet-linalg`：新增 detail 级 MKL LAPACKE full SVD U/V、self-adjoint eigen 和 general eigen candidates；
  `20260616-173317` 小 sweep 显示 full SVD U/V 的 `gesvd` 在 float/double、`16..128` 上均领先 Eigen，
  self-adjoint eigen 的 `syev` 也整体领先；`20260616-173707` / `20260616-173845` validation sweep 后已
  反写 public policy：`full_svd` 的 float/double 非空矩阵切 LAPACKE，`self_adjoint_eigen_decomposition`
  的 double 非空矩阵切 LAPACKE、float 从 `32x32` 起切 LAPACKE。general eigen 的 `geev` 只在
  `128x128` 观察到领先，小尺寸 Eigen 更好，因此保留为 benchmark candidate，不进入默认 policy。
- `kspacejet-linalg`：新增 complex LAPACKE decomposition candidates：`cgesvd` / `zgesvd`、`cheev` /
  `zheev` 和 complex `geev`。`20260617-083809` production sweep 显示 complex singular values、
  complex full SVD U/V 和 complex Hermitian eigen 在 `16..256` 上均由 LAPACKE 领先；complex general
  eigen 仍然混合，complex float 小中尺寸偏 Eigen，complex double 从 `64x64` 起才明显偏 LAPACKE。
  `20260617-084223` validation sweep 后已反写 public policy：complex `singular_values`、
  `full_svd` 和 `self_adjoint_eigen_decomposition` 对非空矩阵优先尝试 LAPACKE，并保留 Eigen fallback；
  complex `eigen_decomposition` 继续只作为 benchmark candidate。
- `kspacejet-linalg`：新增 complex LAPACKE solve/factor candidates：`cgesv` / `zgesv`、`cpotrf` /
  `zpotrf` + `cpotrs` / `zpotrs`、`cgels` / `zgels`。`20260617-103940` production sweep 显示
  `16..256` 上 complex LU、Cholesky factor/solve 和 QR least-squares 的 vector/matrix RHS 均由
  LAPACKE 领先；`20260617-104218` tiny sweep 用 `2,4,8,16` 复核小尺寸 fallback。`20260617-104558`
  validation sweep 后已反写 public policy：complex LU vector RHS 从 `4x4` 起、LU matrix RHS 从
  `2x2` 起、Cholesky factor 从 `2x2` 起、Cholesky solve vector RHS float/double 分别从 `8x8` /
  `16x16` 起、Cholesky solve matrix RHS 从 `8x8` 起、QR vector RHS 从 `16` columns 起、QR matrix
  RHS float/double 分别从 `8` / `16` columns 起优先尝试 LAPACKE，并保留 Eigen fallback。
- `kspacejet-linalg`：新增 `whitening_matrix` 和 `complex_whitening_matrix` 的 Eigen/LAPACKE/public
  policy benchmark rows；`20260617-110928` production sweep 显示 whitening matrix reconstruction
  的 tiny-size 胜负点不同于单独 self-adjoint eigen，`20260617-111342` validation sweep 后已反写独立
  public policy：float 从 `8x8` 起、double 从 `32x32` 起、complex float 从 `32x32` 起、complex
  double 从 `16x16` 起优先尝试 LAPACKE self-adjoint eigen path，并保留 Eigen fallback。
- `kspacejet-linalg`：新增 centered MKL GEMM covariance、MKL GEMM whiten_samples、LAPACKE `gelss`
  SVD least-squares 和 normal-equations LLT variants；`20260617-133126` production sweep 与
  `20260617-133806` validation sweep 后已反写 public policy：covariance 从 `8` variables 起切
  centered MKL GEMM，real whiten_samples 从约 `256` ops 起切 MKL GEMM，complex whiten_samples
  从约 `2048` ops 起切 MKL GEMM，complex double general eigen 从 `64x64` 起切 LAPACKE `zgeev`，
  SVD least-squares real 从 `32` columns 起、complex 从 `16` columns 起切 LAPACKE `gelss`。
  normal-equations LLT 在 well-conditioned 输入上通常最快，但保持显式 solver variant，默认仍为 QR。
- `kspacejet-linalg`：新增 `LeastSquaresSolver::rank_revealing_qr`，Eigen reference 使用
  `CompleteOrthogonalDecomposition`，detail 级 LAPACKE `gelsy` candidate 使用显式 `sqrt(epsilon)`
  rank cutoff。`20260617-150410` production sweep 与 `20260617-150921` validation sweep 后已反写
  public policy：vector RHS 保持 Eigen COD，real matrix RHS 和 complex double matrix RHS 从 `128`
  columns 起优先尝试 LAPACKE `gelsy`，complex float `gelsy` 继续只作为 candidate；同期 real 和
  complex-float general eigen 仍然 mixed，继续保留 candidate。
- `kspacejet-linalg`：新增 `solve` 输出参数 API。默认 `solve()` 继续保持返回式 API，热路径可由调用方复用输出缓冲。
- `kspacejet-signal`：新增 `resample`、image `wrap_phase`、`unwrap_phase_2d`、`compose_separable_kernel`、
  `correlate2d_same`；当前保留返回式 public API，2D unwrap/correlation 暂保持 reference path，
  不反写 backend policy。
- `kspacejet-signal`：新增 `fermi_bandpass_window`、`dual_fermi_band_window`；当前保留返回式 public API。
- `kspacejet-image`：新增 `region_grow` / `region_grow`，输出 mask 类型为 `std::uint8_t`，支持 4/8 连通和阈值区间；`20260616-102647` smoke benchmark 已覆盖 output-buffer/public API，当前保持 Eigen/reference path。
- `kspacejet-signal`：`ResampleKernel` 新增 `cubic`，reference path 与 image cubic 一致使用 `a=-0.75` 权重和 replicate 边界。
- `kspacejet-signal`：`ResampleKernel` 新增 `lanczos3`，reference path 使用归一化 3-lobe windowed-sinc 权重和 replicate 边界；`20260616-135002` sweep 新增 `resample_lanczos3_x2`，显示其质量目标更高但成本约为 cubic 的一个数量级，当前只保留 reference path，不反写后端 policy。
- `kspacejet-signal`：`ResampleKernel` 新增 `mitchell`，reference path 使用 Mitchell-Netravali `B=C=1/3`
  权重和 replicate 边界；`20260616-162626` sweep 新增 `resample_mitchell_x2`，显示其成本位于 cubic
  和 lanczos3 之间，当前不反写后端 policy。
- `kspacejet-signal`：新增 detail 级 OpenCV `filter2D` 候选后端用于 `correlate2d_same`，并在 public policy 中对
  float/double、input pixels `>=64`、kernel pixels `>=9` 时优先尝试 OpenCV；`20260616-111920` sweep
  显示 `correlate2d_same_5x5` 在 `32..512` 全部由 OpenCV 明显领先，`20260616-111951` 小尺寸 sweep
  显示 `8x8` 起 OpenCV 仍领先。
- `kspacejet-signal`：新增返回式 `correlate2d_same_separable`；新增 detail 级 OpenCV `sepFilter2D` 候选后端。`20260616-112835`
  sweep 显示 `opencv_sepfilter2d` 在 `16x16` 起快于 Eigen separable，并在较大图像上快于 full-kernel
  `filter2D`；`20260616-112932` policy 校验确认 separable public API 在 input pixels `>=256` 时切
  OpenCV。
- `kspacejet-image`：新增 `laplacian` / `laplacian`、`unsharp_mask` / `unsharp_mask`；`20260616-103601` smoke benchmark 已覆盖 eigen/output-buffer/public API，Laplacian 和 unsharp 当前保持 Eigen/reference path，不反写 OpenCV/IPP policy。
- IPP large-kernel correlation 评估结论：modern `ippiCrossCorrNorm_32f_C1R` 可用
  `ippiROISame | ippiNormNone` 对齐当前 float same-correlation 语义，已进入 policy；historical
  `ippiCrossCorrSame_Norm_32f_C1R` / `ippiConvFull_32f_C1R` / `ippiConvValid_32f_C1R` 暂不接入，避免引入
  compatibility 依赖和 normalized/unnormalized 语义差异。

## 第一版 benchmark case

第一版不新增独立 target，而是按 `tests/benchmarks` 现有模式扩展各模块的 `*_backend_benchmark.cpp`。这样每个函数后续要选择实现时，数据就留在对应模块的 benchmark 里。

新增 case 覆盖 `libs/numerics` 中已抽出来、且在 MRI 侧出现频率较高的通用计算：

- complex image magnitude/phase/rectangular-to-polar
- 1D FFT cached descriptor plan、2D fftshift、2D FFT、batched 2D FFT、3D FFT slab、batched 3D FFT、FFTW 3D candidate、1D/2D block FFT、FFT-based full/same/valid 2D convolution/correlation
- coil cube RSS across slices
- image resize variants/crop/connected components/region grow/box/gaussian/bilateral/median filter/Sobel/gradient/Laplacian/unsharp/morph close
- Fermi/band-pass/dual-band window、phase unwrap、2D phase unwrap、nearest/linear/cubic/mitchell/lanczos3 resample、2D same-correlation/separable correlation
- vector axpy、covariance/whitening、Cholesky/QR/SVD values、matrix RHS solve、least-squares variants、full SVD U/V、self-adjoint/general eigen、small 2x2 solve
- Bessel J0/J1

默认 standalone 尺寸建议使用：

```text
128,256,512
```

smoke/CI 可以继续用小尺寸：

```text
16,32
```

full sweep 中这些 case 会跳过过大的 2D MRI-shaped 输入，避免把通用 benchmark driver 的 `1024,2048` 默认尺寸变成不成比例的 FFT/RSS 测试。

## 后续抽象顺序

1. 继续补 `kspacejet-signal/image` 的高级 filter/resampling variants。
2. 后续补 `kspacejet-fft` 的 block/overlap FFT composition，并继续保留 FFTW candidate 作为非默认对比项。
3. 继续补 `kspacejet-linalg` 的更多 solver variants；real/complex-float general eigen 继续作为 candidate 等更大生产 sweep。
4. 继续收敛 non-cartesian/NUFFT 边界，把 gridding/DCF/interp table 等纯数学核改成 numerics `View/Pooled` API。
