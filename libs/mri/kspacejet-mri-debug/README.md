# kspacejet-mri-debug

`kspacejet-mri-debug` 是 MRI 层的调试数据输出组件，CMake target 为 `KSpaceJet::mri_debug`。它提供面向重建算法的 slice、matrix、array dump 能力，用于离线排查图像、k-space、中间矩阵和池化 array 数据。

该模块属于 MRI 诊断辅助层，不属于 core logging，也不属于生产数据输出路径。正常日志使用 `KSpaceJet::core` 中的 `kspacejet-logging`；重建结果输出由接入 KSpaceJet 的 provider 负责。

## 职责

`kspacejet-mri-debug` 负责：

- 按 runtime debug 配置判断 dump category 是否启用。
- 生成 matrix、image、cube、array view 的轻量布局、统计和比较报告。
- 输出结构化 JSONL 调试事件和 scope timing。
- 将 matrix、array 和 volume 数据输出到调试目录。
- 将 `ksj::array` 池化对象 dump 为 MAT 文件。
- 为 MRI 算法代码提供统一的调试文件路径和 category 过滤语义。

## CMake target

```cmake
target_link_libraries(<target> PRIVATE KSpaceJet::mri_debug)
```

`KSpaceJet::mri_debug` 当前 public 依赖：

- `KSpaceJet::core`
- `KSpaceJet::array`

MATIO/HDF5 只作为 `ksj_mri_debug` 的 private 实现依赖。业务层不应该直接看到 MATIO 类型，也不应该因为链接 `KSpaceJet::mri_debug` 而继承 MATIO include/link 接口。

新代码使用 namespaced include：

```cpp
#include "kspacejet/mri/debug/array_dump.hpp"
#include "kspacejet/mri/debug/cfl_dump.hpp"
```

## 目录布局

- `include/kspacejet/mri/debug/`：现代 public headers。
- `src/`：`.cpp` 实现文件，第三方实现细节只能放在这里。
- 模块根目录只保留 CMake 和 README；public header 必须放在 `include/kspacejet/mri/debug/`。

## Public API

| Header | 内容 |
| --- | --- |
| `kspacejet/mri/debug/array_analysis.hpp` | `Pooled*` 和 `*View` array 对象的 layout 摘要、统计摘要和差异比较。 |
| `kspacejet/mri/debug/array_dump.hpp` | `Pooled*` 和 `*View` array 对象的 MAT dump。 |
| `kspacejet/mri/debug/cfl_dump.hpp` | BART CFL/HDR 调试文件输出。 |
| `kspacejet/mri/debug/debug_event.hpp` | 结构化 JSONL debug event 和 scoped timer。 |
| `kspacejet/mri/debug/debug.hpp` | 聚合头，包含所有 public debug dump 入口。 |

## Array Layout/Summary/Compare

`array_analysis.hpp` 面向新的 `libs/numerics/kspacejet-array` 池化对象和 View。它不写大文件，适合在调查正确性
或性能问题时先快速确认数据的形状、连续性、数值范围和差异分布。

支持对象与 MAT dump 保持一致：

- `ksj::array::PooledVector<T>` / `VectorView<T>`
- `ksj::array::PooledMatrix<T>` / `MatrixView<T>`
- `ksj::array::PooledImage<T>` / `ImageView<T>`
- `ksj::array::PooledCube<T>` / `CubeView<T>`
- `ksj::array::PooledArray4D<T>` / `Array4DView<T>`

常用入口：

```cpp
#include "kspacejet/mri/debug/array_analysis.hpp"

auto layout = ksj::mri::debug::describe_layout(matrix.view());
auto summary = ksj::mri::debug::summarize_array(matrix.view());
auto diff = ksj::mri::debug::compare_arrays(golden.view(), candidate.view());

KSJ_LOG_INFO("matrix layout: {}", ksj::mri::debug::format_layout(layout));
KSJ_LOG_INFO("matrix summary: {}", ksj::mri::debug::format_summary(summary));
KSJ_LOG_INFO("matrix diff: {}", ksj::mri::debug::format_comparison(diff));
```

`describe_layout(...)` 会报告：

- rank、shape、stride、元素大小和逻辑字节数；
- `is_contiguous()` 结果；
- data pointer 的 16/32/64 字节对齐情况；
- 如果不是连续 row-major logical order，会指出第一处 stride mismatch。

`summarize_array(...)` 会报告：

- finite / NaN / Inf / zero / nonzero 计数；
- real、abs 的 min/max/mean/stddev；
- complex 数据额外报告 imag 和 phase 的 min/max/mean/stddev；
- 按逻辑顺序计算的 FNV-1a fingerprint，便于快速判断中间结果是否改变。

`compare_arrays(...)` 会报告：

- shape 是否一致、比较了多少元素；
- exact mismatch 数量；
- 在给定 tolerance 下的 mismatch 数量；
- max abs diff、mean abs diff、RMSE、max relative diff；
- 最大差异的 linear index 和多维 coordinate；
- 前 N 个差异样本；
- 对整数数组生成 `rhs - lhs` 的 delta histogram，例如 `-1:105, 1:80`。

这类摘要优先用于“先判断问题形态”，再决定是否需要 MAT/CFL dump。比如 bit-exact compare 失败时，先看
`compare_arrays(...)` 可以区分“大面积算法错误”和“少量 1 LSB 量化差异”。

## Array MAT Dump

`array_dump.hpp` 面向新的 `libs/numerics/kspacejet-array` 池化对象。支持：

- `ksj::array::PooledVector<T>`
- `ksj::array::PooledMatrix<T>`
- `ksj::array::PooledImage<T>`
- `ksj::array::PooledCube<T>`
- `ksj::array::PooledArray4D<T>`
- `ksj::array::VectorView<T>`
- `ksj::array::MatrixView<T>`
- `ksj::array::ImageView<T>`
- `ksj::array::CubeView<T>`
- `ksj::array::Array4DView<T>`

`ArrayMatDumpOptions` 控制输出目录、是否覆盖、是否追加、是否压缩和 MAT 文件版本：

```cpp
ksj::mri::debug::ArrayMatDumpOptions options;
options.directory = "debug/matrices";
options.force = true;
options.compress = true;
options.file_version = ksj::mri::debug::ArrayMatFileVersion::mat73;
```

MAT dump 支持常用实数、整数和 `std::complex<T>` 类型。变量名会经过 MATLAB 变量名清理，输出路径由 options 或 runtime debug matrix dump 目录决定。

`dump_mat_array(expression, file_prefix, variable_name, options)` 的参数语义：

- `expression`：要输出的 `Pooled*` 或 `*View` 对象。
- `file_prefix`：输出 `.mat` 文件名前缀；没有 `.mat` 后缀时自动补上。
- `variable_name`：MAT 文件里的变量名。
- `options.force`：跳过 category 判断并强制输出。默认 `false`。
- `options.append`：尝试追加到已有 MAT 文件。默认 `false`。
- `options.compress`：使用 ZLIB 压缩 MAT 变量。默认 `true`，可减少大矩阵 dump 的磁盘占用，但会增加一些 dump CPU 开销。

默认情况下，`dump_mat_array` 不会无条件写文件。它会先计算 effective name：`file_prefix` 非空时使用 `file_prefix`，否则使用 `variable_name`，然后调用 `array_dump_enabled(effective_name)`。只有 runtime debug category 允许时才输出；`options.force=true` 是少数显式调试场景使用的逃生口，不应作为常规路径。

### `dump_mat_array` 输出路径

`dump_mat_array` 的最终输出路径由输出目录和文件名两部分组成：

```text
resolved_directory / sanitized_file_prefix.mat
```

文件名规则：

- `file_prefix` 非空时使用 `file_prefix`；否则使用 `variable_name`；两者都为空时使用 `array`。
- 文件名里的非字母、非数字、非 `_`、非 `-`、非 `.` 字符会替换成 `_`。
- 如果文件名没有 `.mat` 后缀，会自动补 `.mat`。

目录规则：

- `options.directory` 为空：输出到 runtime 解析后的 `debug.matrix_dump_dir`。
- `options.directory` 是绝对路径：直接输出到该绝对路径。
- `options.directory` 是相对路径：相对 debug root 解析，而不是相对 `debug.matrix_dump_dir`。

默认配置下：

```text
debug.root_dir=
debug.matrix_dump_dir=matrices
```

`dump_mat_array(matrix, "q_regrid_apply_ch0", "matrix")` 会输出到当前 run 的 debug matrix 目录：

```text
<run-output-root>/debug/matrices/q_regrid_apply_ch0.mat
```

如果显式指定目录：

```cpp
ksj::mri::debug::ArrayMatDumpOptions options;
options.directory = "epi";

ksj::mri::debug::dump_mat_array(matrix_view, "after_regrid_ch0", "matrix", options);
```

则输出到：

```text
<debug-root>/epi/after_regrid_ch0.mat
```

注意这里的 `epi` 是相对 debug root，不是相对 `debug.matrix_dump_dir`。

### 使用示例

输出一个 workspace matrix view：

```cpp
const auto matrix = ksj::array::as_const_view(op.workspace().main_matrix_storage_view());

(void)ksj::mri::debug::dump_mat_array(
  matrix,
  std::format("q_regrid_apply_ch{}", op.vars().Channel),
  "matrix");
```

只在具体 category/detail 打开时输出：

```text
debug.enabled=true
debug.categories=q_regrid_apply_ch0
```

强制输出到一个指定 debug 子目录，适合临时排查：

```cpp
ksj::mri::debug::ArrayMatDumpOptions options;
options.directory = "tmp/regrid";
options.force = true;
options.compress = false;

(void)ksj::mri::debug::dump_mat_array(matrix, "before_apply_ch0", "matrix", options);
```

向同一个 MAT 文件追加多个变量：

```cpp
ksj::mri::debug::ArrayMatDumpOptions options;
options.append = true;

(void)ksj::mri::debug::dump_mat_array(input_view, "regrid_debug", "input", options);
(void)ksj::mri::debug::dump_mat_array(output_view, "regrid_debug", "output", options);
```

## Debug Category

dump 是否执行由 `kspacejet-process-runtime` 的 debug dump 配置控制。例如 provider 的配置可包含：

```text
debug.enabled=true
debug.categories=matrix_dump,slice_dump,array_dump
debug.dump_slice_index=-1
debug.slice_dump_dir=slices
debug.matrix_dump_dir=matrices
```

category 语义：

- `slice_dump`：启用 storage AUX slice MAT dump。
- `matrix_dump`：启用 binary matrix dump helper。
- `array_dump`：启用池化 array MAT dump。
- provider 可以使用更细粒度 category，例如 `trajectory`、`coil_map`、`intermediate_image`。

当 `debug.categories` 为空且 `debug.enabled=true` 时，所有 category 都允许。category 匹配由 `kspacejet/process_runtime/debug_dump.hpp` 统一实现。

全局开关和 category 的判断规则：

- `debug.enabled=false`：所有 debug dump 都关闭。
- `debug.enabled=true` 且 `debug.categories` 为空：所有 category 都允许。
- `debug.enabled=true` 且 `debug.categories` 非空：只允许匹配 allow-list 的 category。
- category 匹配大小写不敏感，支持 `*`、`all`、父 category 前缀和路径分段匹配。

`kspacejet-mri-debug` 的 helper 通常同时检查“大类 category”和“具体 detail”。例如 array dump 会检查 `array_dump` 和 `matrix_dump`，也会检查本次 dump 的 effective name：

```cpp
array_dump_enabled("q_regrid_apply")
```

因此下面的配置只会打开 `q_regrid_apply` 这一类具体 dump，而不会打开所有 array dump：

```text
debug.enabled=true
debug.categories=q_regrid_apply
```

但 detail 不能绕过全局开关；`debug.enabled=false` 时任何 detail 都不会输出。

## JSONL Debug Event And Scoped Timer

`debug_event.hpp` 提供结构化 JSONL 输出和 RAII scope timer。它们默认受 runtime debug category 控制；
未开启对应 category 时不会写文件。

```cpp
#include "kspacejet/mri/debug/debug_event.hpp"

void run_expensive_step() {
  KSJ_DEBUG_SCOPE_TIMER("epi.regrid.apply");
  // ...
}
```

默认 timer category 是 `debug_timer`，输出到：

```text
<debug-root>/events/debug_timing.jsonl
```

配置示例：

```text
debug.enabled=true
debug.categories=debug_timer
```

也可以写自定义事件：

```cpp
(void)ksj::mri::debug::append_debug_jsonl(
  "array_summary",
  R"({"name":"after_fft","max_abs":123.0})",
  {.category = "debug_event", .file_name = "events/array_summary.jsonl"});
```

输出是一行一个 JSON object，便于后续用 Python、jq 或 benchmark 工具收集分析。

## 输出目录

输出路径由 `kspacejet-process-runtime` 解析：

- `debug.slice_dump_dir` 控制 storage AUX slice MAT dump 目录。
- `debug.matrix_dump_dir` 控制 matrix/array dump 目录。
- 相对路径基于 debug root。
- 空路径按 runtime 默认 debug 目录解析。

路径语义详见 `config/README.md` 和 `libs/core/kspacejet-process-runtime/README.md`。

## 维护约束

- 新 dump 能力应优先接入 runtime debug category，不应绕过 `debug.enabled`。
- 高频路径必须先查询 category，避免未启用 dump 时仍产生大量格式化或拷贝开销。
- 生产输出和 debug dump 不能混用目录语义。
- summary/compare/layout helper 本身不写文件；如果需要持久化，请通过 JSONL event 或普通日志显式输出。
- 新池化数值对象需要 dump 时，应在 `array_dump_traits` 中显式声明 rank、extent 和访问方式。
- 旧的根目录头和宏入口不再保留；调用点应直接 include `kspacejet/mri/debug/*`。
- public header 不能 include `matio.h`、HDF5、OpenCV、IPP、MKL 等第三方实现头；这些只能出现在 `.cpp` 或 detail 后端实现里。
