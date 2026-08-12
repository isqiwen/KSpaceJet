# kspacejet-config

`kspacejet-config` 是 KSpaceJet core 层的配置解析和配置模型组件。它通过 `ksj_add_core_component()` 汇入 `KSpaceJet::core`，供进程启动、运行时服务、日志、debug dump 和开放 provider 读取配置。

## 职责

`kspacejet-config` 负责把 KSpaceJet 使用的文本配置解析为强类型 C++ 结构：

- `key=value` 配置文件解析。
- 进程级 runtime config 的强类型模型。
- 运行时配置文件名和配置路径辅助。

该组件只描述配置格式和配置数据，不负责启动进程、创建目录、初始化日志或安装 crash handler。这些动作由 `kspacejet-process-runtime` 等上层组件完成。

## Public API

主要 public headers 位于 `include/kspacejet/config/`：

| Header | 内容 |
| --- | --- |
| `key_value_config.hpp` | `KeyValueConfig`、`parse_key_value_config()`、`load_key_value_config_file()`。 |
| `parameter_document.hpp` | `ParameterDocument`、`ParameterRecord`、`ParameterField`，用于简单参数文档。 |
| `runtime_config.hpp` | `RuntimeConfig` 和 crash、memory、logging、debug、provider 配置结构。 |
| `site_config.hpp` | 站点配置和 runtime config 文件名/路径辅助接口。 |

## RuntimeConfig

`RuntimeConfig` 是 KSpaceJet 进程运行时使用的核心配置对象。它聚合以下配置域：

- crash handler 行为。
- memory pool 启用开关、size class 和 per-class block count 配置。
- 结果、日志和 debug 输出目录。
- debug dump 目录、类别和 slice 过滤。
- 日志 backend、level、pattern、flush 和异步队列。
- 可选开放 provider 的独立配置文件。

`runtime_config_from_key_value_config()` 将 `KeyValueConfig` 转换为 `RuntimeConfig`；`parse_runtime_config()` 和 `load_runtime_config_file()` 是文本和文件入口。

## 构建集成

`libs/core/kspacejet-config/CMakeLists.txt` 把以下实现文件加入 `KSpaceJet::core`：

- `src/key_value_config.cpp`
- `src/parameter_document.cpp`
- `src/runtime_config.cpp`
- `src/site_config.cpp`

消费侧不直接链接 `kspacejet-config`，而是链接 `KSpaceJet::core`。

## 维护约束

- 新增配置项时，应同时更新 `RuntimeConfig`、解析逻辑、默认值、相关测试和使用方文档。
- 配置解析应返回 `ksj::base::Result` / `Status`，不要在解析底层直接退出进程。
- Site config 相关路径和语义由运行时布局和配置文档共同约束。
