# kspacejet-linalg

`kspacejet-linalg` 负责 dense linear algebra：矩阵乘法、矩阵向量乘法、向量内积、BLAS1 风格向量更新、转置、求逆、线性方程求解、whitening 和常用 dense 分解。数据对象使用 `kspacejet-array` 的 row-major Pooled/View vector/matrix。统计意义的 covariance 属于 `kspacejet-stats`。

当前公开 API 保持后端无关：

```cpp
auto c = ksj::linalg::matmul(a, b);
auto y = ksj::linalg::gemv(a, x);
auto s = ksj::linalg::dot(x, y);
auto z = ksj::linalg::axpy(0.5F, x, y);
auto at = ksj::linalg::transpose(a);
auto solution = ksj::linalg::solve(a, y);
auto covariance = ksj::stats::covariance(samples);
auto whitening = ksj::linalg::whitening_matrix_from_covariance(covariance);
auto lower = ksj::linalg::cholesky_lower(spd);
auto least_squares = ksj::linalg::solve_qr(tall_matrix, rhs);
auto stable_least_squares = ksj::linalg::solve_least_squares(tall_matrix, rhs, ksj::linalg::LeastSquaresSolver::svd);
auto rank_deficient_least_squares =
  ksj::linalg::solve_least_squares(tall_matrix, rhs, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
auto singular = ksj::linalg::singular_values(a);
auto full = ksj::linalg::full_svd(a);
auto destructive_svd = ksj::linalg::svd_in_place(work_matrix);
auto symmetric_eigen = ksj::linalg::self_adjoint_eigen_decomposition(spd);
auto general_eigen = ksj::linalg::eigen_decomposition(a);
auto tiny = ksj::linalg::solve_small(two_by_two, rhs2);

ksj::linalg::LuFactorWorkspace<ksj::base::cf32> inverse_workspace;
ksj::linalg::LuSolveWorkspace<ksj::base::cf32> lu_workspace;
ksj::linalg::SvdWorkspace<ksj::base::cf32> svd_workspace;
ksj::linalg::LeastSquaresSvdWorkspace<ksj::base::cf32> least_squares_svd_workspace;
ksj::linalg::GeneralEigenWorkspace<ksj::base::cf32> eigen_workspace;
ksj::linalg::inverse(ksj::array::as_const_view(a.view()), inverse.view(), inverse_workspace);
ksj::linalg::solve_lu(ksj::array::as_const_view(a.view()), ksj::array::as_const_view(y.view()),
                      vector_solution.view(), lu_workspace);
ksj::linalg::solve_lu(ksj::array::as_const_view(a.view()), ksj::array::as_const_view(matrix_rhs.view()),
                      solution.view(), lu_workspace);
ksj::linalg::svd(ksj::array::as_const_view(a.view()), u.view(), singular_values.view(), v_adjoint.view(),
                 svd_workspace);
ksj::linalg::solve_least_squares_svd(tall_matrix, rhs, least_squares_out, least_squares_svd_workspace);
ksj::linalg::eigen_decomposition(ksj::array::as_const_view(a.view()), eigenvalues.view(), eigenvectors.view(),
                                 eigen_workspace);

ksj::linalg::solve(a, y, solution_out);
ksj::linalg::axpy(0.5F, x, y, z_out);
ksj::stats::covariance(samples, covariance_out);
ksj::linalg::whiten_samples(samples, whitening, whitened_out);
```

返回式 API 会分配输出对象；`svd_in_place` / `full_svd_in_place` 会允许后端覆盖输入矩阵以复用 LAPACK
work buffer，只有调用方不再需要原矩阵时才应使用。热路径需要复用输出缓冲时优先使用 `scale` / `axpy` /
`solve` / `whiten_samples` 这类 output-buffer API。重复执行 SVD、SVD least-squares、LU solve
或一般特征分解时，使用 `SvdWorkspace` / `LeastSquaresSvdWorkspace` / `LuFactorWorkspace` /
`LuSolveWorkspace` / `GeneralEigenWorkspace` 的 output/workspace API，让调用方明确持有并复用
factorization scratch。同一个 workspace 不能由多个线程同时写；OpenMP 热循环应为每个 worker 持有一份。
covariance 的 output-buffer API 在 `kspacejet-stats`。

内部实现包含 Eigen 路径、Intel MKL BLAS 路径和 Intel MKL LAPACKE 分解路径。正式选择由
`detail::LinalgDispatchPolicy` 的阈值控制，阈值必须通过 benchmark 在目标生产机器上确认后再固化。
benchmark 可以直接调用 `detail` 后端实现比较 Eigen 与 Intel，但业务代码不应依赖这些内部头文件。

当前 policy：

- `matmul`：float/double 优先尝试 MKL BLAS GEMM，失败 fallback Eigen。
- `gemv` / `dot`：按 `LinalgDispatchPolicy` 的规模阈值在 Eigen 与 MKL BLAS 间切换。complex
  conjugate dot 从 `64` elements 起优先尝试 MKL CBLAS `dotc`，更小尺寸保留 Eigen/reference，以避免
  tiny-size 调用开销。
- `solve` / `solve_lu` vector RHS：float 从 `64x64` 起、double 从 `32x32` 起、complex float/double 从 `128x128` 起优先尝试 LAPACKE `gesv`。显式传入 `LuSolveWorkspace` 的 complex 路径从 `4x4` 起尝试 LAPACKE。
- `solve` / `solve_lu` matrix RHS：float 从 `32x32` 起、double 从 `16x16` 起、complex float 从 `256x256` 起、complex double 从 `128x128` 起优先尝试 LAPACKE `gesv` 多 RHS path。显式传入 `LuSolveWorkspace` 的 complex 多 RHS 路径从非空矩阵起尝试 LAPACKE。
- `cholesky_lower`：float/double 非空矩阵、complex float/double 从 `2x2` 起优先尝试 LAPACKE `potrf`。
- `solve_cholesky` vector RHS：float/double 从 `64x64` 起、complex float 从 `8x8` 起、complex double 从 `16x16` 起优先尝试 LAPACKE `potrf` + `potrs`。
- `solve_cholesky` matrix RHS：float/double 从 `32x32` 起、complex float/double 从 `8x8` 起优先尝试 LAPACKE `potrf` + `potrs` 多 RHS path。
- `solve_qr` vector RHS：float 从 `64` columns 起、double 从 `128` columns 起、complex float/double 从 `32` columns 起优先尝试 LAPACKE `gels`。显式传入 `LeastSquaresQrWorkspace` 的 complex 路径从 `8` columns 起尝试 LAPACKE。
- `solve_qr` matrix RHS：float/double 从 `64` columns 起、complex float 从 `64` columns 起、complex double 从 `32` columns 起优先尝试 LAPACKE `gels` 多 RHS path。显式传入 `LeastSquaresQrWorkspace` 的 complex 多 RHS 路径分别从 `32` / `8` columns 起尝试 LAPACKE。
- `whiten_samples` / `whiten_samples`：float/double 从约 `256` multiply-add ops 起、complex float/double 从约 `2048` multiply-add ops 起优先尝试 MKL GEMM。
- `whitening_matrix_from_covariance`：float 从 `8x8` 起、double 从 `32x32` 起、complex float 从 `32x32` 起、complex double 从 `16x16` 起优先尝试 LAPACKE self-adjoint eigen path。
- `cholesky_prewhiten_calibration`：complex float/double 优先使用 LAPACKE row-major `potrf` + `trtri`，
  保持 MRI PreWhiten 校准与旧实现一致；仅在 LAPACK 尺寸不支持时 fallback Eigen/reference。
- `singular_values`：普通返回式 API 默认保留 Eigen；当前 32/64/128 sweep 中 LAPACKE values-only path
  未赢过 Eigen，后端实现保留给直接 benchmark 和未来 policy 调整。
- `svd` / `full_svd`：real 普通返回式 API 从 `32x32` 起、complex double 从 `128x128` 起优先尝试 LAPACKE `gesvd` thin/full U/V path；complex float 普通返回式 API 保留 Eigen。`svd_in_place` / `full_svd_in_place` 在输入 View 可由 LAPACK 直接覆盖时从非空矩阵起尝试 LAPACKE，避免额外输入 copy。`SvdWorkspace` 的 View-output API 从非空矩阵起尝试 LAPACKE，并复用输入 work buffer 和 `superb` scratch。
- `left_singular_vectors`：float/double/complex float/complex double 非空矩阵优先尝试 LAPACKE `gesvd` left-U-only path；View API
  在 policy 选择 Intel 时先 pack 到 row-major Pooled scratch，失败时 fallback Eigen。
- `self_adjoint_eigen_decomposition`：double 非空矩阵优先尝试 LAPACKE `syev`，float 从 `32x32` 起优先尝试 LAPACKE `syev`；complex Hermitian 非空矩阵优先尝试 LAPACKE `heev`。
- `eigen_decomposition`：real `geev` 保持 Eigen/reference；当前 complex `cgeev` / `zgeev` 在 allocating 与 workspace-reuse sweep 中均未达到稳定收益，因此也保持 Eigen/reference。其 LAPACKE 实现保留为 benchmark candidate。
- `solve_least_squares`：公开 QR、rank-revealing QR、SVD、normal-equations LDLT 和 normal-equations Cholesky/LLT variants；默认仍为 QR。SVD variant 对 real 从 `32` columns 起、complex 从 `16` columns 起优先尝试 LAPACKE `gelss`；热路径使用 `solve_least_squares_svd(..., LeastSquaresSvdWorkspace&)` 复用 `matrix_work` / RHS work / singular-values scratch。rank-revealing QR 对 vector RHS 和大多数尺寸保持 Eigen `CompleteOrthogonalDecomposition`，real matrix RHS 从 `128` columns 起、complex double matrix RHS 从 `128` columns 起优先尝试 LAPACKE `gelsy`，complex float `gelsy` 继续只作为 benchmark candidate；normal-equations variants 只适合调用方确认输入 well-conditioned 的热路径。
- decomposition 的公开 View/Pooled API 都应通过 public wrapper 的 policy 选择后端；View API 在 Intel
  后端需要 owning scratch 时由 wrapper 显式 pack，不能绕过 policy 直接固定到单一后端。

最近一次 linalg policy sweep：

```bash
tools/devenv/linux/run.sh python tools/ksj_numerics_benchmark/run.py \
  --bin-dir out/build/linux-release-benchmark/bin \
  --iterations 50 --trials 7 --sizes 16,32,64,128,256 --only linalg
```

报告：`out/benchmarks/kspacejet-numerics-suite/20260616-110621/benchmark_report.md`。这次结果用于反写
LU/Cholesky/QR/SVD values 的 LAPACKE 阈值。随后 `20260616-111114` policy 校验 sweep 新增
`public_policy` 行，确认公开 API 在阈值以上走 LAPACKE、阈值以下保留 Eigen fallback。

`20260616-164326` 小 sweep 新增 matrix RHS、least-squares variants、full SVD U/V、self-adjoint eigen 和
general eigen rows；`20260616-164643` validation sweep 复核反写后的 public policy。结果显示 matrix RHS 能摊薄
LAPACKE 调用成本：LU matrix RHS float 从 `32x32` 起、double 从 `16x16` 起切 LAPACKE；Cholesky matrix RHS
从 `32x32` 起切 LAPACKE；QR matrix RHS float/double 从 `64` columns 起切 LAPACKE。least-squares 的
`normal_equations_ldlt` 在本机 well-conditioned benchmark 输入上最快，但默认 API 仍保持 QR，以避免把数值稳定性
假设隐式塞进 policy。

`20260616-173317` 小 sweep 新增 MKL full SVD U/V、self-adjoint eigen 和 general eigen candidates；
`20260616-173707` / `20260616-173845` validation sweep 复核 public policy。结果显示 full SVD U/V 的
LAPACKE `gesvd` 对 float/double、`16..128` 均领先 Eigen，已进入 `full_svd` public policy；self-adjoint
eigen 的 LAPACKE `syev` 对 double 非空矩阵和 float `>=32x32` 进入 public policy。general eigen 的
LAPACKE `geev` 只在 `128x128` 观察到领先，小尺寸仍由 Eigen 更好，因此保留 benchmark candidate，不进入默认 policy。

`solve_small_2x2` 仍不反写统一 policy：double 的手写 2x2 更快，但 float 在本机 sweep 中更容易受噪声和
编译器选择影响，继续保持当前 `solve_small` 显式 API，由调用方在小系统热路径主动使用。

`20260617-083809` production sweep 新增 complex LAPACKE candidates：`cgesvd` / `zgesvd`、
`cheev` / `zheev` 和 complex `geev`。结果显示 complex singular values、complex full SVD U/V 和
complex Hermitian eigen 在 `16..256` 上均由 LAPACKE 领先；complex general eigen 仍然混合，complex
float 小中尺寸偏 Eigen，complex double 从 `64x64` 起才明显偏 LAPACKE。`20260617-084223` validation
sweep 后已反写 public policy：complex `singular_values`、`full_svd` 和
`self_adjoint_eigen_decomposition` 对非空矩阵优先尝试 LAPACKE，并保留 Eigen fallback；complex
`eigen_decomposition` 继续只作为 benchmark candidate。

`20260617-103940` production sweep 新增 complex solve/factor candidates：`cgesv` / `zgesv`、
`cpotrf` / `zpotrf` + `cpotrs` / `zpotrs`、`cgels` / `zgels`。`16..256` 上 complex LU、
Cholesky factor/solve 和 QR least-squares 的 vector/matrix RHS 均由 LAPACKE 领先；`20260617-104218`
tiny sweep 用 `2,4,8,16` 复核小尺寸 fallback，并给出独立阈值。`20260617-104558` validation sweep
后已反写 public policy：complex LU vector RHS 从 `4x4` 起、LU matrix RHS 从 `2x2` 起、Cholesky
factor 从 `2x2` 起、Cholesky solve vector RHS float/double 分别从 `8x8` / `16x16` 起、Cholesky
solve matrix RHS 从 `8x8` 起、QR vector RHS 从 `16` columns 起、QR matrix RHS float/double 分别从
`8` / `16` columns 起优先尝试 LAPACKE，并保留 Eigen fallback。

`20260617-110928` production sweep 新增 `whitening_matrix` 和 `complex_whitening_matrix` 的
Eigen/LAPACKE/public policy rows；`20260617-111342` validation sweep 后已反写独立 whitening policy：
float 从 `8x8` 起、double 从 `32x32` 起、complex float 从 `32x32` 起、complex double 从 `16x16`
起优先尝试 LAPACKE self-adjoint eigen path，并保留 Eigen fallback。该 policy 不直接复用
`self_adjoint_eigen_decomposition` 的阈值，因为 whitening 的矩阵重组成本会改变 tiny-size 胜负点。

`20260617-133126` production sweep 新增 covariance backend candidate、complex whiten samples、LAPACKE `gelss` SVD
least-squares 和 normal-equations LLT rows；`20260617-133806` validation sweep 后已反写 public policy：
real whiten samples 从约 `256` ops 起切 MKL GEMM，complex whiten samples 从约 `2048` ops 起切 MKL GEMM，
complex double general eigen 从 `64x64` 起切 LAPACKE `zgeev`，SVD least-squares real 从 `32` columns 起、
complex 从 `16` columns 起切 LAPACKE `gelss`。covariance public API 后续已迁到 `kspacejet-stats`。
normal-equations LLT 在 well-conditioned benchmark 中通常快于 LDLT，但继续作为显式 solver
variant，不改变默认 QR。

`20260617-150410` production sweep 新增 rank-deficient least-squares rows，对比 Eigen
`CompleteOrthogonalDecomposition`、Eigen JacobiSVD、LAPACKE `gelsy` 和 LAPACKE `gelss`；`gelsy`
使用显式 `sqrt(epsilon)` rank cutoff 后数值结果与 reference 对齐。原始 backend rows 显示 vector RHS
由 Eigen COD 稳定领先，matrix RHS 只有 real `128` columns 和 complex double `128` columns 观察到
`gelsy` 领先；`20260617-150921` validation sweep 后 public
`LeastSquaresSolver::rank_revealing_qr` 已按该边界反写 policy。同期复核的 real/complex-float general
eigen 仍然 mixed，暂不进入默认 policy；complex double general eigen 继续维持 `>=64x64` 的 `zgeev`
policy。

`20260617-152421` production sweep 新增 `solve_vector_lu` / `solve_matrix_lu` 的 output-buffer rows。
默认 `solve()` 仍保持返回式 API；需要复用输出对象时使用 `solve`。

`20260706` quick sweep 新增 `complex_dot` rows，对比 manual serial、manual OpenMP reduction、Eigen View、
MKL CBLAS `dotc` 和 public policy。`4,16` elements 上 CBLAS 调用成本高于 Eigen/reference；`64,256`
elements 上 CBLAS 明显领先。public policy 因此对 `ksj::base::cf32` / `ksj::base::cf64` conjugate dot
从 `64` elements 起切到 MKL CBLAS，低于该阈值保持 Eigen/reference。OpenMP reduction 对这些尺寸线程开销
远高于计算本身，不进入 numerics 默认路径。

`20260710` quick sweep 新增 complex inverse、matrix-RHS LU 和 general eigen 的 `public_workspace` rows，在
`4,8,16,32` 阶、50 iterations、7 trials 下验证调用方 workspace。complex-float inverse workspace 分别约为
`0.29/0.67/1.90/8.05 us`，旧 allocating LAPACKE 路径约为 `6.68/6.22/7.73/16.15 us`；LU workspace 分别约为
`0.26/0.67/1.87/6.32 us`，旧 public-output 路径约为 `8.80/9.47/10.81/15.40 us`；general eigen workspace
相对 allocating public policy 也更快。因此 radial AMF 等重复小矩阵热路径使用 View-output workspace API，
同时保持 complex-float LAPACKE policy。

`20260723` 将 complex inverse、LU、QR、full SVD 与 general eigen 的后端比较正式纳入 policy gate。每个比较组
显式分为 `allocating`（输出和临时 scratch 在调用内分配）与 `workspace_reuse`（调用方已持有输出和 workspace）两种
timing scope，二者不会交叉比较。public wrapper 也按同一成本模型派发：普通 API 使用 allocating 阈值，显式 workspace
overload 使用 workspace-reuse 阈值。该 sweep 显示 complex general eigen 的 Intel 路径在 `2..128` 不存在稳定收益，
因此当前默认保留 Eigen；后续只有在生产 CPU profile 上得到可重复收益后才重新启用它。
