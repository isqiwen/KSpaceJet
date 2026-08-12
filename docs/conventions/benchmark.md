# KSpaceJet Benchmark Convention

KSpaceJet numerics 和 array 相关性能策略必须由 benchmark 数据驱动，而不是凭经验写死。

## 原则

- benchmark 使用 KSpaceJet 正式数据对象，例如池化 array/matrix/image。
- benchmark 对比具体后端，例如 Eigen、Intel MKL、Intel IPP；只有 timing scope 一致的行可以比较性能。
- 正式 API 可以根据输入规模、layout、stride、contiguity 选择不同后端，但选择规则必须来自 benchmark 报告。
- 阈值应固化在 header/policy 中；正常发布版本不从环境变量读取阈值。
- benchmark 模式可以允许环境变量覆盖阈值，便于 sweep。

## Intel 环境

benchmark 应设置与正式重建一致的 Intel 线程行为：

```text
OMP_WAIT_POLICY=PASSIVE
KMP_BLOCKTIME=0
MKL_THREADING_LAYER=SEQUENTIAL
```

`tests/benchmarks` 和 `tests/research` 的可执行程序会在入口统一设置这些默认值；如果调用方已经设置了同名环境变量，则保留调用方设置。外部 shell 或 Python 驱动不需要再注入这些环境变量。

`tools/ksj_numerics_benchmark/run.py` 的 policy sweep 额外强制每个 benchmark 子进程使用
`--backend-threads 1`（默认值），并按进程、模块、case、trial 的顺序串行运行。它会覆盖
OpenMP/MKL/OpenBLAS/BLIS/Accelerate/NumExpr/TBB/OpenCV 的线程数环境并关闭 OpenMP/MKL 动态线程。
多核吞吐研究必须显式传入更大的线程数，且不得与单线程 policy 或其性能 baseline 混用。

## 输出

benchmark 报告应至少包含：

- 机器和 CPU 信息。
- 构建 preset。
- 后端版本。
- 输入规模、layout、stride、contiguity。
- 多次运行的 `trials`、`median`、`mean`、标准差、95% 置信区间和 `min`/`max`。
- 每个 timing sample 应达到足够的最小时长；tiny case 使用自动校准后的实际 iteration 数，避免用低于计时
  分辨率的样本反写 policy。
- 显式正确性分组、绝对/相对容差与 policy-vs-best 检查结果。
- 声明后端胜出所需的最小收益门限，默认不低于 `5%`。
- 若用于持续性能门禁，保存的基线目录和允许回归门限。
- 实际 CPU affinity；多 socket/NUMA 机器上的正式 sweep 应固定到一个代表性物理核。
- 每个后端的线程数；policy sweep 默认且应使用 `1`，进程重复必须串行。
- 推荐后端和阈值。
- 未覆盖或结果不稳定的 case。

## 报告归档

benchmark 产物分为本地原始产物和正式 policy 证据：

- 本地原始产物：直接运行 benchmark 可执行程序时默认写入 `<executable-dir>/reports/<executable-name>.txt`；传入 `--csv` 或 `--format csv` 时写入 `<executable-dir>/reports/<executable-name>.csv`。每次运行覆盖同名报告，不提交到 Git。
- 正式 policy 证据：当某次 benchmark 结果用于修改 `libs/numerics` 或 `libs/numerics/kspacejet-array` 的 policy/threshold 时，应把报告摘要随同 policy 变更提交。摘要必须放在：

  ```text
  docs/benchmark_reports/<yyyy-mm-dd>/<suite>/<machine-id>/benchmark_report.md
  ```

正式摘要格式见 [benchmark report template](../benchmark_reports/TEMPLATE.md)，目录规则见
[docs/benchmark_reports](../benchmark_reports/README.md)。

`tests/research` 产生的研究型结论不直接反写 production policy。需要归档时放在
[docs/research_reports](../research_reports/README.md)，并在进入 policy 调整前再补正式 benchmark 摘要。

正式摘要应至少保留：

- Git commit 或 tag。
- CMake preset 和 build type。
- CPU 型号、NUMA/socket 信息和内存配置。
- 关键第三方库版本，例如 Eigen、Intel MKL、Intel IPP、OpenCV。
- benchmark 命令行、iteration、trial、size sweep、stride/layout sweep。
- 每个公开 API 的候选后端对比结果。
- 推荐 policy 变更。
- 未覆盖或不稳定的 case。

如果原始 CSV 很大，不要求全部提交；但摘要必须足够让 reviewer 判断 policy 变更是否有数据依据。release 或生产上线前的完整 sweep 原始报告应在团队约定的制品存储或发布记录中保留。

## Policy 反写

修改 policy/threshold 前应满足：

1. benchmark 在目标生产机器或等价机器上运行。
2. 报告显示候选后端有稳定优势。
3. 变更记录引用正式 benchmark 报告路径或摘要。
4. 单元测试仍覆盖正确性。

推荐反写流程：

1. 使用 `linux-release-benchmark` 或 release 等价 preset 构建 benchmark target。
2. 需要带到生产服务器运行时，使用 `linux-release-benchmark-install` 生成 install tree。
3. 运行完整 sweep，而不是 smoke；统一 benchmark 驱动会显式使用 `--csv` 收集可解析 CSV，直接运行单个 benchmark 时默认生成 txt 报告。
   多 socket、NUMA 或动态调频机器应传入 `--cpu-affinity <cpu>`，并确保当前结果和回归基线使用同一
   affinity。保留默认的最小采样时长自动校准，或在命令行中显式记录 `--min-sample-time-us`。正式
   policy sweep 至少运行 3 个独立进程（默认 `--process-repetitions 3`），以每次进程的 median
   重新计算跨进程置信区间；各进程严格串行，且应保留 `--backend-threads 1`；原始结果保留在
   `raw/<module>/`。
4. 给可比较行设置相同 `comparison_group`，并按实际调用边界设置 `timing_scope`，例如
   `output_reuse`、`allocating`、`warm_plan` 或 `cold_call`。不同 scope 不得混合排名。
5. 先通过显式 absolute/relative tolerance 的正确性门禁，再确认候选后端的跨进程 median 收益达到
   门限且 95% 置信区间分离；只有 smoke、单个进程、单次最低值或均值排序不能作为反写依据。若不同独立
   进程的赢家反转，应先修 benchmark 隔离/覆盖或保留现有 policy，不得增加单点阈值来拟合一次运行。
6. 更新对应 policy header，例如：

   ```text
   libs/numerics/kspacejet-linalg/include/kspacejet/linalg/detail/linalg_policy.hpp
   libs/numerics/kspacejet-fft/include/kspacejet/fft/detail/fft_policy.hpp
   libs/numerics/kspacejet-image/include/kspacejet/image/detail/image_policy.hpp
   libs/numerics/kspacejet-signal/include/kspacejet/signal/detail/signal_policy.hpp
   libs/numerics/kspacejet-stats/include/kspacejet/stats/detail/stats_policy.hpp
   libs/numerics/kspacejet-sparse/include/kspacejet/sparse/detail/sparse_policy.hpp
   ```

7. 在 policy header 附近保留简短 provenance 注释，说明阈值来自哪份正式 benchmark 摘要：

   ```cpp
   // Tuned by docs/benchmark_reports/2026-06-05/kspacejet-linalg/production-avx512/benchmark_report.md.
   ```

8. 用 `--save-baseline-dir` 保存通过审核的完整 sweep；后续通过 `--baseline-dir` 执行同
   case/backend/scope 的 median 回归检查。baseline 的 CPU affinity、线程数、trial、进程重复数和
   最小采样时长必须匹配。
   若只重跑失败模块，可把新的完整模块 CSV/raw 结果写回原 suite 目录，再用 `--evaluate-only`
   重新生成整套 gate/report；该模式不得替代受影响模块本身的完整 size sweep。
9. 重新运行对应 unit tests、benchmark smoke 和 policy gate。
10. 提交时把 policy 变更、报告摘要和必要 README 更新放在同一个 commit 或同一个 MR 中，方便 reviewer 一次性审查。
11. MR 描述中必须列出正式摘要路径、policy header 路径、验证命令和 owner 确认结果。

确认责任：

- benchmark 执行人负责报告可复现、环境信息完整、命令行和输入 sweep 清楚。
- 模块 owner 负责确认数学语义和 policy 变更没有破坏公开 API。
- release 或生产负责人负责确认 benchmark 机器代表目标生产环境。

MR 审查 checklist：

- 正式摘要位于 `docs/benchmark_reports/<yyyy-mm-dd>/<suite>/<machine-id>/benchmark_report.md`。
- policy header 中有 provenance 注释指向正式摘要。
- benchmark 覆盖了目标输入规模、layout、stride、contiguity 和 value type。
- 报告列出了未覆盖或不稳定 case。
- policy 变更只覆盖报告支持的范围。
- 对应 unit tests 和 benchmark smoke 已运行并记录结果。

policy 变更 commit message 推荐使用：

```text
perf(linalg): tune MKL dispatch thresholds
perf(fft): tune DFTI dispatch threshold
```

没有正式 benchmark 摘要的 policy/threshold 修改，不应进入 release 分支。

## Smoke 与 Sweep

- smoke 验证 benchmark 可运行、报告结构和显式分组的正确性；policy miss 与回归只警告，不用于性能结论。
- sweep 使用 policy gate，必须覆盖关键规模、layout、stride、timing scope 和后端组合。
- 后端 winner 只有在 median 收益达到最小门限且 95% 置信区间分离时才成立；否则记录为 tie。
- 性能回归只有在超过门限且当前/基线置信区间分离时才失败，避免把普通测量噪声当回归。
