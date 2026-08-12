# kspacejet-base

`kspacejet-base` 是 KSpaceJet core 层的基础类型和基础工具组件。它不单独生成库，而是通过 `ksj_add_core_component()` 汇入 aggregate target `KSpaceJet::core`。

## 职责

`kspacejet-base` 提供所有 core、transport、MRI runtime 和工具模块可以共同依赖的最小公共能力：

- 固定宽度整数、byte/span、cache-line 常量等基础类型。
- `Status` / `Result<T>` / `Error` 等错误表达类型。
- 强类型 ID，例如 `SessionId`、`WorkerId`、`FrameId`。
- 路径、时间戳、文件复制、字符串比较等轻量工具。
- `BlockingQueue<T>` 等基础同步容器。
- legacy reconstruction status code 的现代 C++ 封装。
- 构建版本信息和组件版本信息。

该组件不承载业务语义，不依赖 MRI reconstruction 代码，也不依赖 numerics、transport 或 process runtime。

## Public API

主要 public headers 位于 `include/kspacejet/base/`：

| Header | 内容 |
| --- | --- |
| `types.hpp` | `i8/i16/i32/i64`、`u8/u16/u32/u64`、`byte`、`kCacheLineSize`。 |
| `status.hpp` / `result.hpp` | `StatusCode`、`Status`、`Result<T>`。 |
| `exception.hpp` | `Error`、`ValidationError`、`NotImplementedError`。 |
| `ids.hpp` | `StrongId<Tag>` 和 KSpaceJet 常用强类型 ID。 |
| `path.hpp` | 路径拼接、规范化、目录创建、组件清理。 |
| `timestamp.hpp` / `clock.hpp` | 日志、输出目录和运行诊断使用的时间工具。 |
| `file.hpp` | 二进制文件复制。 |
| `blocking_queue.hpp` | 简单阻塞队列。 |
| `span.hpp` | `Span<T>`、`ByteSpan`、`ConstByteSpan`。 |
| `binary_layout.hpp` | legacy KSpaceJet word-size 布局辅助。 |
| `checksum/crc32c.hpp` | CRC-32C（Castagnoli）完整性校验。 |
| `version.hpp` | `BuildInfo`、`VersionInfo`、`build_summary()`。 |
| `compiler.hpp` | branch prediction、symbol visibility、C ABI、OpenMP pragma 辅助宏。 |

## 构建集成

`libs/core/kspacejet-base/CMakeLists.txt` 把以下实现文件加入 `KSpaceJet::core`：

- `src/status.cpp`
- `src/path.cpp`
- `src/file.cpp`
- `src/timestamp.cpp`
- `src/clock.cpp`
- `src/version.cpp`

消费侧不直接链接 `kspacejet-base`，而是链接 `KSpaceJet::core`。

## 维护约束

- 新增 API 前应确认它是否真的属于全仓基础能力；如果依赖进程布局、线程、配置、socket 或 numerics，应放到对应 core/numerics 子模块。
- `kspacejet-base` 不应引入 heavyweight 第三方库。
- 基础失败使用 `Status` / `Result<T>`；热路径的正常分支（例如队列状态、取消状态）使用局部强类型枚举，不建立全局业务错误码表。
- `compiler.hpp` 中的兼容宏用于 public header 边界，删除或改名前需要检查所有 C ABI 和 legacy include 使用点。
