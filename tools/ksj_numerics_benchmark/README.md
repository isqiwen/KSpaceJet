# KSpaceJet Numerics Benchmark

`tools/ksj_numerics_benchmark` 是 KSpaceJet numerics 后端微基准测试驱动工具，用于统一运行 `tests/benchmarks` 生成的 numerics benchmark 可执行程序，并生成可追溯的 CSV 与 Markdown 报告。单独运行 benchmark 可执行程序时默认生成 txt 报告；本驱动会显式传入 `--csv`，并统一设置可复现的子进程线程与等待环境。

## 运行方式

所有 benchmark 可执行程序需要位于同一个目录，例如：

```bash
tools/devenv/linux/run.sh cmake --build --preset linux-release-benchmark --target \
  ksj_array_backend_benchmark \
  ksj_nufft_backend_benchmark \
  ksj_linalg_backend_benchmark \
  ksj_fft_backend_benchmark \
  ksj_signal_backend_benchmark \
  ksj_image_backend_benchmark \
  ksj_stats_backend_benchmark \
  ksj_optimization_backend_benchmark \
  ksj_sparse_backend_benchmark \
  ksj_special_backend_benchmark

tools/devenv/linux/run.sh python tools/ksj_numerics_benchmark/run.py \
  --bin-dir out/build/linux-release-benchmark/bin \
  --cpu-affinity 0 \
  --iterations 50 \
  --module-sizes linalg=16,32,64,128,256,512,1024 \
  --module-sizes array=256,1024,4096,16384,65536,262144,1048576 \
  --module-sizes nufft=4,8,16,32,64
```

正常运行时 `--bin-dir` 是必选参数，指向所有 benchmark 可执行程序所在的同一个目录。常用可选参数：

- `--out-dir <dir>`：指定 CSV 和 Markdown 报告输出目录；默认写入 `out/benchmarks/kspacejet-numerics-suite/<timestamp>/`。
- `--evaluate-only`：不执行 benchmark，只读取 `--out-dir` 下已有的模块 CSV 并重新生成 gate/report；
  用于局部重跑后复核完整 suite，且必须同时指定 `--out-dir`。
- `--iterations <N>`：每个 trial 内每个 case 的最小重复次数；harness 会自动增加 tiny case 的重复次数。
- `--min-sample-time-us <N>`：每个 trial 的最小校准采样时长，默认 `1000 us`；CSV 的 `iterations`
  列记录每一行实际采用的重复次数。
- `--trials <N>`：每个 case 的独立采样次数；输出 CSV 会保留 median、mean、标准差、95% 均值置信区间和 min/max。
- `--process-repetitions <N>`：独立 benchmark 进程数；`smoke` 默认 `1`，`policy` 默认 `3`。多进程结果以
  每次进程的 median 为样本重新计算 median 和 95% Student-t 置信区间，并轮换 size 顺序以暴露运行上下文敏感性。
- `--cpu-affinity <list>`：Linux 上把驱动和所有 benchmark 子进程固定到指定 CPU，例如 `0` 或
  `0,2-3`；双路 NUMA 或动态调频机器上的正式 sweep 应固定到一个代表性物理核。
- `--backend-threads <N>`：每个数值后端允许的线程数，默认 `1`。policy sweep 的 benchmark 子进程和
  case 都严格串行；多核研究必须显式指定大于 `1` 的值，并使用独立的 baseline。
- `--module-sizes <module>=<A,B,C>`：指定一个模块的规模序列；可重复传入多个模块。规模语义由模块定义，
  例如 linalg 的值是 square matrix order，array 的值是 element count。未指定的模块使用自己的默认序列，
  其中 linalg 默认最大 order 为 `1024`；nufft 的值是二维 grid 的边长，sample count 为边长的四倍。
- `--only array fft ...`：只运行指定模块，合法值包括 `array`、`nufft`、`linalg`、`fft`、`signal`、`image`、`stats`、`optimization`、`sparse`、`special`。
- `--gate-mode smoke|policy`：smoke 只因正确性失败返回非零；policy 还会门禁可信的 policy miss 和性能回归。
- `--min-speedup-percent <P>`：声明后端胜出所需的最小 median 收益，默认 `5%`，且要求置信区间分离。
- `--max-policy-gap-percent <P>`：公开 policy 相对同 scope 最快候选后端允许的最大差距，默认 `5%`。
- `--baseline-dir <dir>`：读取上一次完整 sweep 的同名 CSV，执行逐 case/backend/scope 性能回归检查；
  baseline manifest 的 CPU affinity、thread 数和采样配置必须与当前运行一致。
- `--save-baseline-dir <dir>`：保存当前 CSV、报告、门禁 JSON 和机器 manifest，供后续回归比较。

runner 会为每个 benchmark 子进程统一设置与正式重建一致的 Intel 等待策略，并显式覆盖后端线程数，
以保证 policy 数据可复现：

```text
OMP_WAIT_POLICY=PASSIVE
KMP_BLOCKTIME=0
MKL_THREADING_LAYER=SEQUENTIAL
OMP_NUM_THREADS=1
MKL_NUM_THREADS=1
OPENBLAS_NUM_THREADS=1
BLIS_NUM_THREADS=1
OPENCV_FOR_THREADS_NUM=1
```

等待策略遵循 shell 的 `${NAME:-default}` 语义：调用方提供非空值时保留该值；变量未设置或为空时，
runner 才使用上表默认值。线程数仍始终由 `--backend-threads` 统一控制。

`--module-sizes` 是 runner 的唯一规模覆盖接口；它不会改变各 benchmark 可执行程序内部的 `--sizes` 参数。

`--backend-threads` 会同时设置 OpenMP、MKL、OpenBLAS、BLIS、Accelerate、NumExpr、TBB 和 OpenCV 的
对应线程环境，并禁用 OpenMP/MKL 的动态线程调节。进程重复是串行的：run 1 完成后才启动 run 2。

输出默认写入：

```text
out/benchmarks/kspacejet-numerics-suite/<timestamp>/
```

其中包含：

- `linalg.csv`
- `array.csv`
- `nufft.csv`
- `fft.csv`
- `signal.csv`
- `image.csv`
- `stats.csv`
- `optimization.csv`
- `sparse.csv`
- `special.csv`
- `benchmark_report.md`
- `benchmark_gate.json`
- `raw/<module>/run-<N>.csv`：每个独立进程的原始结果；模块根目录下的 CSV 是跨进程聚合结果。

每行 CSV 还带有：

- `comparison_group`：数学语义相同、可以做正确性比较的显式分组。
- `timing_scope`：例如 `output_reuse`、`allocating`、`warm_plan`；性能只在相同 scope 内比较。
- `role`：`oracle`、`reference`、`candidate` 或 `policy`；每一行都必须显式填写。
- `abs_tolerance` / `rel_tolerance`：可由 case 覆盖的 checksum 正确性容差；负值表示使用 float/double 默认值。
- `process_repetitions`：聚合结果包含的独立进程数；`iterations` / `iterations_max` 记录各进程自适应校准范围。

NUFFT 的 formal rows 使用 `direct_dft=true`，以便 Eigen direct NUDFT 与 BART direct DFT 具有同一数学语义；
每个方向分别记录 `cold_plan` 和调用者 workspace 的 `warm_plan`。BART 近似 gridding NUFFT 不是与 direct
NUDFT 可互换的候选，因此不参与这个 policy 比较。

CSV 必须包含完整 schema，且每一行都必须有非空的 `comparison_group`、`timing_scope` 和合法 `role`。
缺列、空 metadata、无效 role 或缺少 `selected_backend` 的 policy 行会立即失败，不会静默降级。
C++ benchmark 的每次 `print_row` 调用都必须传入 `RowMetadata`；缺少它会直接编译失败。所有观察都必须先
声明数学比较关系和 timing scope，才能成为可用于 correctness 或 policy 的基准数据。

局部重跑受影响模块后，可以在保留其他模块 CSV 的同一个目录中重新复核整套结果：

```bash
tools/devenv/linux/run.sh python tools/ksj_numerics_benchmark/run.py \
  --out-dir /path/to/existing-suite \
  --evaluate-only \
  --cpu-affinity 0 \
  --iterations 50 \
  --trials 9 \
  --process-repetitions 3 \
  --gate-mode policy
```

该模式不会修改模块 CSV 或 `raw/<module>/`；只更新 `benchmark_report.md` 和 `benchmark_gate.json`，
并可与 `--save-baseline-dir` 一起使用。

## 回归基线

完整生产 sweep 可以直接保存为下一次运行的基线：

```bash
tools/devenv/linux/run.sh python tools/ksj_numerics_benchmark/run.py \
  --bin-dir out/build/linux-release-benchmark/bin \
  --cpu-affinity 0 \
  --backend-threads 1 \
  --iterations 50 \
  --trials 9 \
  --process-repetitions 3 \
  --gate-mode policy \
  --save-baseline-dir /path/to/kspacejet-numerics-baseline
```

后续运行加入：

```text
--baseline-dir /path/to/kspacejet-numerics-baseline
```

只有超过回归门限且当前与基线 95% 置信区间分离时，policy 模式才判定性能回归失败。baseline 的
thread 数、CPU affinity、trial、进程重复数和最小采样时长必须匹配。CI 脚本也接受
`KSJ_NUMERICS_BENCHMARK_BASELINE` 指向基线目录。

## 设计原则

- benchmark 使用 KSpaceJet 的池化对象，不使用裸 Eigen 对象作为正式输入对象。
- benchmark 对比具体后端实现，例如 Eigen、Intel MKL、Intel IPP。
- 公开 API 的 policy 只应根据相同 timing scope、跨独立进程仍达到最小收益且置信区间分离的 benchmark
  结果反写；单进程内很窄的 trial CI 不能掩盖进程间频率、热状态或执行顺序波动。
- 生产机器需要重新跑完整 sweep，因为最优后端和阈值与 CPU、NUMA、Intel 库版本、线程策略有关。
- 正式 sweep 和其回归基线必须使用相同 CPU affinity；实际 affinity 会写入报告、gate JSON 和 baseline
  manifest。

CI 和本地门禁使用 [tools/checks/linux/benchmark_smoke.sh](../checks/linux/benchmark_smoke.sh) 调用本工具。默认 smoke
验证构建、运行、报告结构和正确性；`--full` 使用 policy 门禁与 9 个 trials。release 分支或生产标定时应再提供生产机器回归基线。

## 内部结构

`run.py` 是稳定的命令行入口；它只转交给内部 package，不承载测量逻辑。实现按可验证的测量生命周期划分：

- `ksj_numerics_benchmark/execution.py`：CPU affinity、后端线程环境，以及严格串行的子进程采样。
- `ksj_numerics_benchmark/csv_io.py`：严格 CSV schema 校验、原始进程结果聚合和 Student-t 置信区间。
- `ksj_numerics_benchmark/evaluation.py`：正确性、同 timing scope 的 policy-vs-best 和 baseline 回归判定。
- `ksj_numerics_benchmark/reporting.py`：Markdown、gate JSON 和 baseline manifest 的序列化。
- `ksj_numerics_benchmark/models.py`：benchmark catalogue 与跨层不可变数据模型。
- `ksj_numerics_benchmark/cli.py`：参数解析及各阶段的协调，不包含统计或门禁规则。

测试同样按这些边界拆分。修改某一层时，必须保持 CLI 参数、CSV 列、`benchmark_gate.json` schema 和
baseline manifest 的兼容性；先跑模块单测，再用 `--evaluate-only` 对既有 CSV 做协议回归。
