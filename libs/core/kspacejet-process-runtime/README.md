# kspacejet-process-runtime

`kspacejet-process-runtime` 是 KSpaceJet core 层的进程运行时组件。它通过 `ksj_add_core_component()` 汇入 `KSpaceJet::core`，为流式重建 provider 和工具提供一致的进程启动、配置、日志、状态路径、消息循环和清理机制。

## 职责

`kspacejet-process-runtime` 负责进程级运行时能力：

- 当前可执行程序布局和 runtime layout root。
- 当前进程配置查找、解析和缓存。
- 日志初始化。
- crash handler 安装入口。
- 运行输出目录、results/debug/log 路径解析。
- process-local typed message loop。
- cleanup registry 和 quit-scan cleanup helper。
- 运行时 external procedure 加载。
- debug dump 开关、类别匹配和输出路径解析。

该组件不承载 MRI 算法语义，不解析 socket packet，也不决定 FE/BE 业务流程。它只提供进程运行时基础设施。

## Public API

主要 public headers 位于 `include/kspacejet/process_runtime/`：

| Header | 内容 |
| --- | --- |
| `message_loop.hpp` | `MessageLoop`，process-local typed message queue 和 worker thread。 |
| `runtime_config.hpp` | 当前可执行程序配置和 runtime config 缓存/查找。 |
| `state_paths.hpp` | runtime output、logging、results、debug 目录解析。 |
| `logging.hpp` | 当前可执行程序日志初始化。 |
| `crash.hpp` | 当前可执行程序 crash handler 安装。 |
| `executable_layout.hpp` | executable dir 和 runtime layout root。 |
| `external_procedure.hpp` | 外部动态库和 symbol 加载。 |
| `cleanup_registry.hpp` | 进程清理回调注册和执行。 |
| `channel_synchronization.hpp` | channel-level shared lock wrapper。 |
| `debug_dump.hpp` | debug dump category、slice filter 和路径辅助。 |

## MessageLoop

`MessageLoop` 是 KSpaceJet process-local actor 风格对象的公共基类。它负责：

- worker thread 生命周期。
- stop request。
- asynchronous `post()`。
- synchronous `send()`。
- private queue synchronization。
- typed dispatch 到派生类 handler。

它只管理线程和消息调度，不定义网络协议、payload layout 或重建语义。

## Runtime Config 和 State Paths

`runtime_config` namespace 负责查找当前可执行程序对应的配置并缓存解析结果。`state_paths` namespace 根据 runtime config 和当前运行状态解析输出路径：

- logging base dir。
- results output dir。
- debug report、algorithm、slice dump、matrix dump dir。
- 当前 run output root。

进程启动层应优先通过这些 API 获取路径，避免各模块重复拼接运行目录。

## 构建集成

`libs/core/kspacejet-process-runtime/CMakeLists.txt` 把以下实现文件加入 `KSpaceJet::core`：

- `src/executable_layout.cpp`
- `src/runtime_config.cpp`
- `src/state_paths.cpp`
- `src/logging.cpp`
- `src/crash.cpp`
- `src/message_loop.cpp`
- `src/cleanup_registry.cpp`
- `src/external_procedure.cpp`

该组件私有链接 `KSpaceJet::platform_runtime_libraries`。

消费侧不直接链接 `kspacejet-process-runtime`，而是链接 `KSpaceJet::core`。

## 维护约束

- 进程启动路径、runtime config 查找和 state path 语义会影响 provider、tests、tools 和 CI。
- `MessageLoop` 应保持只负责线程和 dispatch，不应引入具体 packet 或 MRI payload 依赖。
- debug dump helper 是运行时诊断入口，应保持低侵入；算法输出格式和 dump 内容由调用方决定。
- cleanup registry 应只用于进程生命周期清理，不应替代普通 RAII。
