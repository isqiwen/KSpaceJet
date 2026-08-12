# kspacejet-performance

`kspacejet-performance` 提供 KSpaceJet 的性能采集和重建生命周期分析能力。该组件属于 `KSpaceJet::core`，面向流式 scan 性能诊断、人工计数器、耗时统计、trace 事件和 CPU profile 产物管理。

性能分析默认关闭。关闭时接口仍可被调用，但实现为 no-op，宏也会在编译期消除主要开销。

无论是否启用完整性能分析，backend 都会记录并在每个成功完成的 scan 输出一次轻量汇总日志：

```text
[PERF][SCAN-TIMING] ScanUID=<uid>
  +0.000 ms  Scan start
  +<...> ms  Acquisition start
  +<...> ms  Acquisition end
  +<...> ms  Image ready #<transfer> [<varying coordinates>]
```

所有时间都是相对 `Scan start` 的绝对时间点，按实际发生顺序排列。`Acquisition start` 是 backend 接收首条采集数据的时刻，
并非设备物理采集起点；`Acquisition end` 是 backend 收到 `MMS_SCAN_END` 的时刻。正常情况下最后一条 `Image ready`
就是最终图像就绪，它表示图像完成 checksum、准备构造发送 payload 的时刻，不表示 CUI/SC 已接收图像。只有异常地
没有图像记录时，才会输出 `Final image ready` 兜底事件。

该汇总由 `analysis.hpp` 的同一组 scan 生命周期事件驱动，只记录 scan start、首条 acquired data、scan end、
最后输出图像四个时间点；不写 JSON、不启动 profiler、不保存逐图事件，也不在每条 RUN_PROC 上加锁。它适合
常规重建日志和版本性能对比。

每张图像就绪时只记录 `scan_start_to_image_ready_ms`；最后一张图像就绪时，它们会统一列在 `Images ready` 下。
其中时间在图像校验和完成之后、构造发送 payload 之前记录，表示 backend 内图像已准备好输出，而非 CUI/SC 接收或显示完成。
`#` 是输出顺序；方括号只显示本次 scan 中实际变化的坐标维度：`f` frame、`b` batch、`v` volume、`m` map、
`e` echo、`s` slice、`p` phase、`c` coil。没有变化的维度会省略。

## 目录结构

```text
include/kspacejet/performance/analysis.hpp
  性能采集公共接口、scan 生命周期事件、metric/trace 宏

src/analysis.cpp
  KSJ_PERFORMANCE_LEVEL>0 时的实际采集实现；level 2 下额外启用 gperftools CPU profiler

src/analysis_noop.cpp
  性能分析关闭或不支持平台上的 no-op 实现
```

## 构建开关

性能分析由顶层 CMake 变量 `KSJ_PERFORMANCE_LEVEL` 控制：

| 选项 | 含义 |
| --- | --- |
| `KSJ_PERFORMANCE_LEVEL=0` | 关闭性能分析，使用 no-op 实现，不链接 `gperftools::profiler`。 |
| `KSJ_PERFORMANCE_LEVEL=1` | 启用 JSON/manual metrics 和 `KSJ_PERF_SCOPE`。不链接、不启动 gperftools CPU profiler。当前仅 Linux 支持。 |
| `KSJ_PERFORMANCE_LEVEL=2` | 在等级 1 基础上启用 trace mark、trace counter、trace span 和 gperftools CPU profiler。 |

编译期宏 `KSJ_PERFORMANCE_LEVEL` 会由 CMake 写入 `KSpaceJet::core`：

| 等级 | 能力 |
| --- | --- |
| `0` | 性能分析关闭，metric 和 trace 宏为 no-op。 |
| `1` | 启用 counter、gauge、duration 和 scan 生命周期 JSON 采集。 |
| `2` | 在等级 1 基础上启用 trace mark、trace counter、trace span 和 CPU profile 采集。 |

## 进程产物

进程启动后可调用 `initialize_process_artifacts()` 设置性能分析产物目录和进程角色名。未显式指定目录时，默认位置为：

```text
KSJ_PERFORMANCE_OUTPUT_ROOT
KSJ_BENCHMARK_OUTPUT_ROOT/PerformanceAnalysis
<current-run-output-root>/PerformanceAnalysis
```

优先使用显式环境变量 `KSJ_PERFORMANCE_OUTPUT_ROOT`；benchmark 场景会使用
`KSJ_BENCHMARK_OUTPUT_ROOT/PerformanceAnalysis`。如果当前运行没有 output root，则回退到 KSpaceJet state
directory 下的 `performance_analysis`。

启用性能分析后会生成以下目录和文件：

```text
PerformanceAnalysis/
  recon_perf_<role>_<pid>.json
  cpu_profiles/       # 仅 KSJ_PERFORMANCE_LEVEL=2 生成
    cpu_profile_<role>_<pid>_scan<N>[_uid<uid>].prof
  pprof_reports/      # 仅 KSJ_PERFORMANCE_LEVEL=2 生成
```

JSON 报告包含进程角色、PID、scan 状态、scan/recon 时间戳、首张和末张最终图像时间、输出计数、吞吐、进程 CPU/RSS 快照、人工 metric 和 trace 事件。`KSJ_PERFORMANCE_LEVEL=2` 时，`cpu_profiles/` 保存 gperftools 原始 profile；`pprof_reports/` 供后续报告工具写入分析结果。

性能产物可由任意兼容 JSON 的外部分析工具消费。

## Scan 生命周期接口

backend 在 scan 生命周期关键节点调用以下接口：

| 接口 | 语义 |
| --- | --- |
| `on_scan_start()` | scan 开始，创建新的 scan record。 |
| `on_reconstruction_started()` | 首条 acquired data 即将进入重建，记录重建起点。 |
| `on_scan_uid_resolved(scan_uid)` | 记录当前 scan UID。 |
| `on_provider_resolved(name, version)` | 记录开放重建 provider 的名称和版本。 |
| `on_ismrmrd_dataset_resolved(path)` | 记录 ISMRMRD 输入数据集路径。 |
| `on_slice_sent(scan_uid, slice_info, transfer_index, expected_transfer_count, is_final_image)` | 记录输出 slice，最终图像会进入 final image event 列表。 |
| `on_reconstruction_completed(reason)` | 标记重建完成。 |
| `on_reconstruction_failed(reason)` | 标记重建失败；level 2 下同时停止 profiler。 |
| `on_reconstruction_stopped(reason)` | 标记重建停止；level 2 下同时停止 profiler。 |
| `on_scan_end()` | 标记 acquisition 结束，并在输出已完成时结束 scan record。 |
| `flush_process_artifacts()` | 写出当前 JSON 状态；进程退出时也会自动 flush。 |

`SliceEventInfo` 描述输出图像的 frame、batch、volume、map、echo、slice、phase、coil、图像大小、数据长度、元素大小和图像类型等字段。

## Metric 和 Trace

人工 metric 用于记录稳定的跨 scan 或跨流程指标：

```cpp
KSJ_PERF_COUNTER_ADD("queue.run_proc", 1);
KSJ_PERF_GAUGE_SET("queue.depth", depth);
KSJ_PERF_SCOPE("backend.dispatch");
```

trace 事件用于更细粒度的阶段分析：

```cpp
KSJ_TRACE_MARK("scan.start");
KSJ_TRACE_COUNTER("queue.depth", depth);
KSJ_TRACE_SCOPE("recon.slice");
KSJ_TRACE_SCOPE_EX("provider.stage", "streaming", "reconstruct", "slice_12", "worker");
```

直接接口 `counter_add()`、`gauge_set()`、`duration_add()`、`trace_mark()`、`trace_counter()`、`trace_span_begin()` 和 `trace_span_end()` 可在需要动态控制时使用。

## 使用规则

- 默认使用宏接口；宏会根据 `KSJ_PERFORMANCE_LEVEL` 编译期裁剪。
- metric 名称应稳定、短小，并使用点分命名，例如 `backend.dispatch`、`queue.run_proc`。
- scan 生命周期接口应只由拥有 scan 状态的运行时代码调用，算法代码不应自行伪造 scan 生命周期事件。
- 算法局部耗时优先使用 `KSJ_PERF_SCOPE()`；需要完整时间线时再使用 trace。
- 性能采集路径不得改变重建结果，也不得成为 scan 成败判定的一部分。
- 高频路径新增采集点前，应确认关闭性能分析时不会引入可观开销。

## 维护边界

`kspacejet-performance` 只维护通用性能采集和产物格式，不应包含以下内容：

- 具体算法的业务判断。
- FE/BE 进程私有控制流。
- pprof 报告渲染逻辑。
- 在线运行必须依赖的强制行为。

性能产物解析和报告生成属于外部工具。进程何时调用 scan 生命周期接口由对应 provider 维护。
