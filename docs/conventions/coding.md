# KSpaceJet Coding Convention

本文档定义 KSpaceJet 仓库通用代码规范。更细的模块设计约定应放在对应模块 README 或专题设计文档中。

## 基本原则

- 新代码使用 C++20。
- KSpaceJet 支持 Linux 和受控范围内的 Windows VS2022 构建；新增平台分支必须保持职责清楚，并经过 owner review。
- 优先使用标准库、KSpaceJet core helper 和现有模块 API。
- 避免把业务语义泄漏到 core 层；MRI 语义放在 `libs/mri`，通用数值计算放在 `libs/numerics`。

## 格式

- C/C++ 使用仓库根目录 `.clang-format`。
- `.clang-format` 的具体规则和代码示例见 [Clang-Format Convention](clang_format.md)。
- CMake 使用仓库根目录 `.cmake-format.py`。
- 本地 `pre-commit` 会对暂存文件运行格式检查。

## 命名

- public C++ API 使用清晰完整的英文命名。
- 库 target 和组件名使用 `ksj_` 加下划线，例如 `ksj_ismrmrd`、`ksj_fft`。
- 可执行程序的 CMake target 也使用下划线，例如 `ksj_recon`；安装/运行时
  output name 使用统一的 `ksj` 或 `ksj-<role>` kebab-case，例如
  `ksj-recon`。源目录使用 `kspacejet-<role>`，例如
  `apps/kspacejet-recon`。
- 文件名使用小写和下划线。
- 新模块的命名应体现职责边界，避免 `misc`、`common2`、`utils_new` 这类泛化名称。

## Include

- 优先 include 自己的头文件。
- KSpaceJet 头文件使用项目 include 路径，例如 `#include "kspacejet/base/version.hpp"`。
- 避免依赖 include 顺序；头文件应能独立编译。
- 不在 public header 中暴露不必要的第三方实现细节。

## 数值接口

详细规则见 [Numerics API Convention](numerics_api.md)。核心原则如下：

- `libs/numerics` 及开放 provider 的计算接口默认使用输入/输出分离的形式，例如
  `algorithm(input, output)` 或返回新的 pooled 对象。
- 如果算法确实需要原地修改输入，函数名必须显式使用 `_in_place` 后缀，例如
  `fftshift_in_place(data)`。
- `fill(output)`、`copy(input, output)`、`transform(input, output, op)` 这类只写输出或输入/输出分离的接口不需要
  `_in_place` 后缀。
- 长期持有数据、workspace 和 state 使用 `Pooled*`；算法核心输入输出使用 `View`。
- 对外接口同时提供 View 和 Pooled 版本时，Pooled 版本应直接转发到 View 版本；后端适配层只借用 View 内存，
  除非算法必须拥有临时缓冲。

## 错误处理和日志

- 运行时错误应使用现有 status/result/logging 体系，不吞掉错误。
- 日志应说明上下文、路径、scan/channel 等关键诊断信息。
- 不在库代码中直接写 `std::cout`/`std::cerr`，命令行工具入口除外。

## 注释

- 删除与当前代码无关的旧文件头、Revision History 和过期说明。
- 注释用于解释意图、约束和非显然边界，不重复描述显然代码。
- 历史信息应依赖 Git，不放在文件头中维护。

## 生成物

不得提交 build/output/cache 产物，例如：

```text
out/
build/
cmake-build-*/
__pycache__/
*.pyc
*.o
*.so
*.log
```

`pre-commit` 会拒绝常见生成物。
