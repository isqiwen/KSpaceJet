# C++ 高性能数值计算代码实现与优化手册

`tests/research/cpp-numerics-performance-handbook` 是一个“手册 + 可运行案例”工程。它用于研究 KSpaceJet 数值计算热点代码应该如何组织数据访问、选择 layout、融合循环、调用 Eigen/MKL 等实现路径，以及怎样用数据解释这些代码写法。

这里的 case 不注册为 CTest，也不作为正式生产阈值 benchmark。正式性能阈值仍由 `tests/benchmarks` 和 `tools/ksj_numerics_benchmark` 产生的报告驱动。

完整手册正文见 [cpp_numerics_performance_handbook.md](cpp_numerics_performance_handbook.md)。

## 目录结构

- `cases/`：独立研究案例，每个 `*_case.cpp` 生成一个可执行程序。
- `support/`：研究案例共用的参数解析、计时、输出、初始化和 checksum 工具。
- `CMakeLists.txt`：只负责构建本手册工程的 case target 和聚合 target。

## 研究目标

当前重点观察：

- Eigen 表达式路径和显式 Intel MKL BLAS 路径的差异。
- 热路径分配、workspace 复用和 fused kernel 的差异。
- 多次 memory pass 和 fused kernel 的差异。
- loop order、transpose、ROI materialization 等访问模式的差异。
- AoS/SoA、mask density、branchless、fixed-size Eigen 等热点代码写法的差异。
- column-major matrix 与 row-major image 在 coil/RSS 类访问中的差异。
- CG update 这类迭代求解内层操作拆开写和融合写的差异。

这些 case 的目标不是“直接证明生产实现应该怎么写”，而是让开发者理解性能现象：为什么一次 fused pass 可能比多个清晰的 BLAS/vector primitive 更快，为什么 layout 会改变 cache 行为，为什么小尺寸后端调用可能输给简单循环。

研究 case 使用本工程内部的 64-byte aligned buffer 和 Eigen Map，不使用 `ksj::array::Pooled*` 或 kspacejet-memory。这样可以避免把池化分配器、NUMA broker、KSpaceJet array wrapper 的成本混入结果，使 case 更专注于 C++ kernel 写法、layout、Eigen/MKL 后端和内存访问模式本身。

## 构建

```bash
cmake --preset linux-release-research
cmake --build --preset linux-release-research
```

`linux-release-research` 是 Linux Release 专用 preset，默认构建 `ksj_cpp_numerics_performance_handbook`
聚合 target，也就是一次性编译全部 handbook case。`ksj_cpp_numerics_performance_handbook`
只负责编译全部 case，不产生同名可执行程序。该 preset 使用 research-only 配置，只生成
`tests/research` 下的 target，不生成主工程 apps/libs。

需要带到生产服务器运行时，可以安装到独立运行目录：

```bash
cmake --build --preset linux-release-research-install
```

安装产物位于 `out/install/linux-release-research/`，包含 handbook case 可执行程序和必要运行时库。

## 运行

每个 case 在进程入口调用 `ksj::numerics::initialize_numerics_runtime()` 统一设置 Intel runtime 默认策略：

```text
MKL internal threading = 1
IPP internal threading = 1
```

该策略不通过配置文件或环境变量修改，用来模拟 KSpaceJet 外层拥有并行度、Intel 库内部保持顺序执行的默认策略。

```bash
out/build/linux-release-research/bin/ksj_numerics_perf_complex_mul_scale_mask \
  --iterations 50 \
  --trials 7 \
  --sizes 1024,4096,16384,65536
```

默认输出是面向人工阅读的对齐表格，同时会覆盖写入可执行程序同目录下的 `reports/` 子目录：

```text
<executable-dir>/reports/<executable-name>.txt
```

需要给脚本处理时使用 `--csv` 或 `--format csv`，此时 stdout 和 report 文件都会使用 CSV 格式：

```bash
out/build/linux-release-research/bin/ksj_numerics_perf_cg_update --csv > cg_update.csv
```

对应 report 文件为：

```text
<executable-dir>/reports/<executable-name>.csv
```

CSV 格式为：

```text
case,variant,type,size,coils,iterations,trials,mean_ns,min_ns,max_ns,checksum
```

## 当前案例

- `ksj_numerics_perf_matmul`：比较 `Eigen` 矩阵乘与 `Intel MKL cblas_*gemm`。
- `ksj_numerics_perf_workspace_reuse`：比较热路径内部分配、workspace 复用和完全融合写法。
- `ksj_numerics_perf_complex_mul_scale_mask`：比较三次 pass 和单次 fused pass。
- `ksj_numerics_perf_loop_order`：比较 row-major image 按连续方向和非连续方向遍历。
- `ksj_numerics_perf_contiguity`：比较 row-major/column-major 下连续访问和跨 stride 访问执行同一逐点 FMA 的差异。
- `ksj_numerics_perf_fftshift_access`：比较 materialized `fftshift` 和 shifted-index 访问。
- `ksj_numerics_perf_eigen_noalias`：比较 Eigen 普通矩阵乘赋值和 `.noalias()`。
- `ksj_numerics_perf_restrict_simd`：比较普通指针循环和 restrict/`omp simd` hint。
- `ksj_numerics_perf_alignment`：比较 64-byte aligned buffer 和非 cache-line aligned buffer 参与同一数学计算的差异。
- `ksj_numerics_perf_complex_layout`：比较 `std::complex` AoS 和 real/imag SoA。
- `ksj_numerics_perf_mask_density`：比较 dense mask 全量扫描和 sparse index list。
- `ksj_numerics_perf_cg_update`：比较拆分的 axpy/reduction 写法与单次 fused update。
- `ksj_numerics_perf_rss_coil_combine`：比较 column-major coil matrix 与 row-major voxel/coil image 的访问成本。
- `ksj_numerics_perf_blocked_transpose`：比较 naive transpose 和 cache-blocked transpose。
- `ksj_numerics_perf_roi_materialization`：比较 ROI 复制后计算和直接 ROI 访问。
- `ksj_numerics_perf_branchless`：比较 branchy 和 branchless soft-threshold。
- `ksj_numerics_perf_fixed_size`：比较小固定尺寸 Eigen 和动态尺寸 Eigen。
- `ksj_numerics_perf_compute_scenarios_baseline` /
  `ksj_numerics_perf_compute_scenarios_avx512_auto_vectorized` /
  `ksj_numerics_perf_compute_scenarios_avx512_intrinsics`：用同一组可能受益于 AVX512 的计算场景比较默认编译基线、
  AVX512 自动向量化、AVX512 手写 intrinsics。当前覆盖经典 float/double FMA 和 dot、fused elementwise、
  L2/sum+sumsq/min-max reduction、complex AoS multiply/conj-multiply/normalize/magnitude/deinterleave、
  indirect gather、masked compact、strided pack、ROI materialization、transpose scatter、1D/2D/3D stencil/filter、
  volume zpad/scale、coil 小维度 aggregation、int16/int32 affine clamp。
  同时从 KSpaceJet 代码中抽取了多类真实循环形状：`libs/mri/kspacejet-math` 的 complex/phase functor，
  KSpaceJet array view 的 real/imag 分量写回，以及代表性的 waterfat、prewhiten、
  RSS、hamming 2D complex filter、mask morphology、B0/shim phase quality，以及 `libs/numerics` 的 image threshold、
  signal FIR convolution 和 transpose/layout 转换。

## AVX512 计算场景三档对照

`cases/avx512_compute_scenarios_case.cpp` 会被编成三个独立程序：

- `ksj_numerics_perf_compute_scenarios_baseline`：默认编译基线，用于观察 portable codegen，不启用 AVX512 专用编译选项。
- `ksj_numerics_perf_compute_scenarios_avx512_auto_vectorized`：同一份普通 C++ loop，显式使用 `-march=x86-64-v4`，用于观察编译器自动向量化。
- `ksj_numerics_perf_compute_scenarios_avx512_intrinsics`：显式使用 `-march=x86-64-v4`，并启用 AVX512 intrinsics kernel。

示例：

```bash
for exe in \
  ksj_numerics_perf_compute_scenarios_baseline \
  ksj_numerics_perf_compute_scenarios_avx512_auto_vectorized \
  ksj_numerics_perf_compute_scenarios_avx512_intrinsics
do
  out/build/linux-release-research/bin/${exe} \
    --csv \
    --iterations 50 \
    --trials 7 \
    --sizes 4096,16384,65536,262144 \
    --coils 8,16,32
done
```

`avx512_auto_vectorized` 和 `avx512_intrinsics` 目标需要运行机器支持 AVX512/x86-64-v4；不支持时可以只运行
`baseline` 目标或换到支持 AVX512 的机器。

## 新增案例规范

- 每个案例单独一个 `cases/*_case.cpp` 和一个 `ksj_numerics_perf_*` 可执行程序。
- 输入对象必须使用本工程 `support/common.hpp` 中的 research-local aligned buffer，不使用 `kspacejet-array` 池化对象。
- 如果 case 调用 Intel MKL/IPP 等后端，必须沿用公共入口设置的 Intel runtime policy。
- 每个案例必须输出 checksum，避免编译器消除计算。
- 每个案例应支持多次 trial，并输出 mean/min/max。
- README 中应解释 case 研究的问题、对比的实现路径和应该如何阅读结果。
