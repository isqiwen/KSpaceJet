# kspacejet-logging

`kspacejet-logging` 是 KSpaceJet core 层的统一日志组件。它通过 `ksj_add_core_component()` 汇入 `KSpaceJet::core`，底层使用 `spdlog`，上层通过 KSpaceJet 自己的配置结构和宏接口记录日志。

## 职责

`kspacejet-logging` 负责：

- 根据 `ksj::config::LoggingConfig` 初始化 console/file logger。
- 提供统一日志 level。
- 提供源文件、行号和函数名感知的日志宏。
- 提供 formatted logging。
- 支持 `*_EVERY_N` 节流日志。
- 支持 flush 和 shutdown。

该组件不决定日志目录。日志目录通常由 `kspacejet-process-runtime` 根据当前可执行程序和 runtime config 解析后传入。

## Public API

主要 public header：

```text
include/kspacejet/logging/logging.hpp
```

核心 API：

| API | 作用 |
| --- | --- |
| `Configure(const ksj::config::LoggingConfig&, ...)` | 使用已解析配置初始化日志。 |
| `Configure(const char* config_path, ...)` | 从配置文件路径初始化日志。 |
| `EnsureConfigured()` | 确保 logger 已经可用。 |
| `IsConfigured()` | 查询 logger 是否已初始化。 |
| `ShouldLog(Level)` | 查询某个 level 当前是否会被记录。 |
| `Log()` / `LogFormatted()` | 底层日志写入入口。 |
| `Flush()` / `Shutdown()` | 刷新和关闭日志系统。 |

常用宏：

- `KSJ_LOG_TRACE`
- `KSJ_LOG_DEBUG`
- `KSJ_LOG_INFO`
- `KSJ_LOG_WARN`
- `KSJ_LOG_ERROR`
- `KSJ_LOG_CRITICAL`
- `KSJ_LOG_*_EVERY_N`
- `KSJ_LOG_ERROR_RETURN_IF`

## 构建集成

`libs/core/kspacejet-logging/CMakeLists.txt`：

- `find_package(spdlog CONFIG REQUIRED)`
- 将 `src/logging.cpp` 加入 `KSpaceJet::core`
- 通过 `PUBLIC_LINK_LIBS spdlog::spdlog` 让消费侧获得必要的 spdlog include/link 依赖

消费侧不直接链接 `kspacejet-logging`，而是链接 `KSpaceJet::core`。

## 维护约束

- 日志宏是广泛使用的 public API，改名或改变参数语义会影响全仓调用点。
- 格式化失败时应保持容错，不应因为日志 format error 中断业务路径。
- 日志初始化应由进程启动层完成，库代码不应随意重新配置全局 logger。
- 普通 `KSJ_LOG_INFO`、`KSJ_LOG_WARN` 和 `KSJ_LOG_ERROR` 调用不应再套同级 `ShouldLog()`；日志宏内部已经检查级别，重复判断只会增加锁和分支。
- 高频 `KSJ_LOG_DEBUG`/`KSJ_LOG_TRACE` 路径，或日志参数需要昂贵计算、分配和遍历时，应先用对应级别的 `ShouldLog()` 保护参数构造和日志调用。
- 需要降低输出频率时直接使用 `KSJ_LOG_*_EVERY_N`；不要再为限频宏套同级 `ShouldLog()`。高频且通常关闭的 DEBUG/TRACE 限频日志可以在外层使用 `ShouldLog()`，从而在该级别关闭时避免原子计数开销。
