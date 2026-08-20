# kspacejet-platform

`kspacejet-platform` 是 KSpaceJet core 层的平台适配组件。它通过 `ksj_add_core_component()` 汇入 `KSpaceJet::core`，封装 KSpaceJet 在 Linux 和受控 Windows VS2022 构建中所需的进程、socket、dynamic library、archive 和系统信息接口。

## 职责

`kspacejet-platform` 负责隔离直接系统调用和平台细节：

- 当前工作目录和可执行程序路径。
- TCP socket 创建、bind/listen/connect/accept/send/receive。
- socket subsystem RAII。
- `dlopen` / `dlsym` 风格的动态库加载和符号查询。
- 系统和进程内存状态。
- CPU core 数量。
- 目录压缩为 zip archive。
- 同一文件系统内、绝不替换目标目录的原子目录发布。
- 环境变量默认值设置。

Linux 是完整测试和运行治理主平台。Windows VS2022 构建覆盖 `apps/` 下四个应用 target；新增平台 API 时必须同时说明 Linux 语义和 Windows fallback 行为。

## Public API

主要 public headers 位于 `include/kspacejet/platform/`：

| Header | 内容 |
| --- | --- |
| `process.hpp` | `current_working_directory()`、`executable_path()`。 |
| `socket.hpp` | TCP socket handle、IPv4 endpoint、send/receive、socket option 和等待可读接口。 |
| `socket_subsystem.hpp` | socket subsystem RAII wrapper。 |
| `dynamic_library.hpp` | `DynamicLibrary`、`LoadMode`、`shared_library_file_name()`。 |
| `system.hpp` | 系统/进程内存状态、CPU core 数量、环境变量辅助。 |
| `archive.hpp` | `archive_directory_to_zip()`。 |
| `filesystem.hpp` | `publish_directory_no_replace()`：同一文件系统内原子发布一个新目录，目标已存在时返回 `already_exists`，绝不覆盖。 |

## 构建集成

`libs/core/kspacejet-platform/CMakeLists.txt` 把以下实现文件加入 `KSpaceJet::core`：

- `src/process.cpp`
- `src/socket.cpp`
- `src/socket_subsystem.cpp`
- `src/dynamic_library.cpp`
- `src/system.cpp`
- `src/archive.cpp`
- `src/filesystem.cpp`

该组件私有链接 `KSpaceJet::platform_runtime_libraries`，在 Windows 下包含 Winsock/PSAPI 等平台运行时库。

消费侧不直接链接 `kspacejet-platform`，而是链接 `KSpaceJet::core`。

## 维护约束

- 新增平台 API 时应优先保持 Linux 语义明确；Windows fallback 必须服务于已支持的 VS2022 target 范围。
- socket API 只提供低层 primitive；流式协议由使用它的开放 provider 定义。
- dynamic library API 只负责加载和 symbol 查询；具体 provider 的符号契约由 provider 自己定义。
- 文件系统发布 API 只负责平台级目录提交语义；调用者负责创建、填充和在失败时清理 staging 目录。它不复制目录或用非原子操作替代 no-replace 发布。
- 系统状态接口用于诊断和容量估算，不应在性能关键路径中频繁调用。
