# ksj_static_analysis

`tools/kspacejet_static_analysis` 存放开发期静态分析脚本。工具检查源码树或构建产生的 metadata，不属于 KSpaceJet 运行时，也不参与普通构建。

当前主要能力包括：

- 静态内存泄漏风险检查。
- numerics 迁移依赖边界检查。

## 内存泄漏检查

`check_memory_leaks.py` 读取 `compile_commands.json`，用 Clang static analyzer 对 C/C++ 编译单元运行内存管理相关检查器。

支持的入口是 CMake target：

```bash
tools/devenv/linux/run.sh cmake --preset linux-release-static-analysis
tools/devenv/linux/run.sh cmake --build --preset linux-release-static-analysis --target ksj_static_memory_leak_check
```

该 target 目前只在 Linux 上可用，普通构建 preset 默认不会运行它。报告输出到：

```text
out/build/linux-release-static-analysis/static_analysis/ksj_static_memory_leak_report.md
```

## 检查范围

脚本聚焦内存所有权风险，例如：

- `new` / `delete` 不匹配。
- `malloc` 相关所有权问题。
- 错误释放器。
- 潜在泄漏。

报告会把告警按检查器和文件汇总，并给每条告警标记置信度：

- `High confidence`
- `Likely leak`
- `Needs review`

Clang analyzer 无法分析的编译单元会列入覆盖缺口，方便区分“没有风险”和“没有覆盖到”。

## 维护入口

- CMake target 定义在 `cmake/KSpaceJetStaticAnalysis.cmake`。
- Python 实现是 `tools/kspacejet_static_analysis/check_memory_leaks.py`。
- 默认报告路径由 CMake 传给脚本，也可以通过脚本参数 `--report` 覆盖。

脚本支持的关键参数包括：

- `--compile-commands`
- `--project-root`
- `--clangxx`
- `--jobs`
- `--path-prefix`
- `--fail-on-analyzer-error`
- `--report`

常规使用应通过 CMake target 运行，以保证路径、检查器和报告位置一致。

## Numerics 依赖边界检查

`check_numeric_dependency_boundaries.py` 扫描源码中仍然直接使用历史遗留拼写（例如
`kspacejet-math/include`、`KSpaceJet::math`）、Armadillo，以及在 `libs/numerics` 之外直接使用
MKL/IPP/OpenCV/ITK 的位置。这些历史拼写是迁移违规，而不是当前模块；检查用于把基础计算收敛到
`libs/numerics`，并让 MRI 业务层只依赖 numerics API。

支持的入口是 CMake target：

```bash
tools/devenv/linux/run.sh cmake --build --preset linux-release --target ksj_numeric_dependency_boundary_check
```

报告输出到：

```text
out/build/linux-release/static_analysis/ksj_numeric_dependency_boundary_report.md
```

当前仓库仍有历史调用点，所以该 target 默认只报告、不失败。需要把它收紧成门禁时，在配置阶段打开：

```bash
tools/devenv/linux/run.sh cmake --preset linux-release -DKSJ_NUMERIC_DEPENDENCY_BOUNDARY_FAIL_ON_VIOLATION=ON
```

也可以直接运行脚本：

```bash
tools/devenv/linux/run.sh python tools/kspacejet_static_analysis/check_numeric_dependency_boundaries.py --project-root .
```

脚本支持的关键参数包括：

- `--project-root`
- `--path-prefix`
- `--report`
- `--show-limit`
- `--fail-on-violation`

## 维护范围

- 源码静态分析脚本归本目录维护。
- 开发环境和检查入口归 `tools/devenv/`、`tools/checks/` 等对应目录维护。
- 编译型工具归对应 `tools/<tool_name>` 目录维护。
- 静态分析报告属于构建产物，输出到 `out/`，不得提交到源码目录。
