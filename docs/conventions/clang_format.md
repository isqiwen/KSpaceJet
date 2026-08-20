# KSpaceJet Clang-Format Convention

本文解释仓库根目录 [.clang-format](../../.clang-format) 的 C/C++ 格式规则，并给出常见代码示例。`.clang-format` 是机器可执行规则，本文是给开发者阅读的说明文档；如两者不一致，以 `.clang-format` 和 `clang-format` 实际输出为准。

本地 `pre-commit` 会对暂存的 C/C++ 文件执行：

```bash
clang-format --dry-run --Werror <files>
```

格式检查脚本入口：

```text
tools/devenv/linux/run.sh just format-staged
tools/devenv/linux/run.sh just pre-commit
```

## 适用范围

会被格式检查的文件类型：

```text
*.c
*.cc
*.cpp
*.cxx
*.h
*.hh
*.hpp
*.hxx
*.ipp
```

## 基础风格

配置：

```yaml
Language: Cpp
BasedOnStyle: LLVM
```

含义：

- 使用 C/C++ 语言规则解析代码。
- 以 LLVM 风格为基础，再由 KSpaceJet 的配置覆盖局部规则。

## 缩进

配置：

```yaml
IndentWidth: 2
ContinuationIndentWidth: 2
TabWidth: 2
UseTab: Never
```

含义：

- 普通缩进使用 2 个空格。
- 续行缩进使用 2 个空格。
- 不使用 tab。

示例：

```cpp
auto status = op.load_current_slice(
  op.workspace().main_matrix_adapter.GetRanges(),
  REAL_AND_IMAG);

if (!status.ok()) {
  return status;
}
```

## 行宽

配置：

```yaml
ColumnLimit: 120
```

含义：

- 目标单行宽度是 120 列。
- 超过时 `clang-format` 会按语法结构自动换行。

示例：

```cpp
const auto output_path =
  ksj::base::join_path({state_paths.results_dir_path(), scan_uid, "image", file_name});
```

## 访问控制符

配置：

```yaml
AccessModifierOffset: -2
```

含义：

- `public:`、`private:`、`protected:` 相对 class 内部缩进少 2 个空格。

示例：

```cpp
class QueueOpContext {
public:
  QueueOpContext(QueueOpContextComponents components);

private:
  QueueOpDataRuntime* queue_op_data_runtime_;
};
```

## switch / case

配置：

```yaml
IndentCaseLabels: true
IndentCaseBlocks: true
```

含义：

- `case` 标签缩进。
- `case` 里的代码块继续缩进。

示例：

```cpp
switch (message_id) {
  case MMS_SCAN_START:
    handle_scan_start(message);
    break;
  case MMS_SCAN_END:
    handle_scan_end(message);
    break;
  default:
    return false;
}
```

## 大括号

配置：

```yaml
BreakBeforeBraces: Attach
```

含义：

- 左大括号贴在当前行末尾。

示例：

```cpp
if (enabled) {
  start_scan();
}

void BackendManager::stop() {
  request_stop();
}
```

不是：

```cpp
if (enabled)
{
  start_scan();
}
```

## 短代码块

配置：

```yaml
AllowShortBlocksOnASingleLine: Empty
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLambdasOnASingleLine: Empty
```

含义：

- 只有空 block 可以单行。
- 类内 inline 短函数可以单行。
- `if` 不压成单行。
- 只有空 lambda 可以单行。

示例：

```cpp
class Status {
public:
  bool ok() const { return code_ == Code::success; }
};

if (status.ok()) {
  return ksj::base::Status::Ok();
}

auto noop = [] {};
```

不推荐，也会被格式化拆开：

```cpp
if (status.ok()) return ksj::base::Status::Ok();
```

## 指针和引用

配置：

```yaml
PointerAlignment: Left
ReferenceAlignment: Left
DerivePointerAlignment: false
```

含义：

- `*` 和 `&` 绑定到类型左侧。
- 不根据已有代码推断指针风格。

示例：

```cpp
int* data = nullptr;
const Packet& packet = current_packet();
QueueOpContext& op = context();
```

不是：

```cpp
int *data = nullptr;
const Packet &packet = current_packet();
```

## 括号空格

配置：

```yaml
SpaceBeforeParens: ControlStatements
SpacesInParentheses: false
SpacesInSquareBrackets: false
SpaceInEmptyParentheses: false
```

含义：

- 控制语句关键字后有空格，例如 `if (`、`for (`、`while (`。
- 函数调用和函数声明不在函数名后加空格。
- 圆括号、方括号内部不额外加空格。
- 空参数括号不加空格。

示例：

```cpp
if (channel_id >= 0) {
  process_channel(channel_id);
}

auto value = samples[index];
auto count = active_channels();
```

不是：

```cpp
if( channel_id >= 0 ) {
  process_channel ( channel_id );
}
```

## C++11 初始化列表

配置：

```yaml
Cpp11BracedListStyle: true
Standard: Latest
```

含义：

- 使用现代 C++ braced-init 风格。
- `clang-format` 按其支持的最新 C++ 语法解析代码。

示例：

```cpp
std::vector<int> channels{0, 1, 2, 3};
ThreadPoolOptions options{
  .name = "recon",
  .worker_count = 4,
};
```

## Include 顺序

配置：

```yaml
SortIncludes: Never
IncludeBlocks: Preserve
```

含义：

- 不自动排序 include。
- 不自动合并或重组 include block。
- include 顺序由开发者和模块 owner 维护。

示例：

```cpp
#include "kspacejet/mri/queue_op_context.hpp"

#include "kspacejet/base/status.hpp"
#include "kspacejet/logging/logging.hpp"

#include <vector>
```

规则说明：

- KSpaceJet 仍然要求头文件尽量自包含，不能依赖偶然 include 顺序。
- 这里关闭自动排序，是为了避免 legacy 边界和第三方头文件顺序产生大规模无意义 diff。

## 连续声明和赋值

配置：

```yaml
AlignConsecutiveAssignments: false
AlignConsecutiveDeclarations: false
```

含义：

- 不按列对齐连续赋值。
- 不按列对齐连续声明。
- 避免变量名变化导致大面积格式 diff。

示例：

```cpp
int channel = 0;
std::size_t slice_count = 0;
std::string output_dir;
```

不是：

```cpp
int         channel     = 0;
std::size_t slice_count = 0;
std::string output_dir;
```

## 行尾注释

配置：

```yaml
AlignTrailingComments: true
```

含义：

- 相邻行尾注释会尝试对齐。

示例：

```cpp
int rows = 256;      // image height
int cols = 256;      // image width
int channels = 32;   // coil count
```

如果对齐会让代码难读，应优先把说明移到单独注释块。

## Namespace

配置：

```yaml
NamespaceIndentation: None
FixNamespaceComments: true
```

含义：

- namespace 内部不额外整体缩进。
- 自动补充或修正 namespace 结束注释。

示例：

```cpp
namespace ksj::mri::debug {

void inspect_example(ksj::array::ImageView<const float> image) {
  (void)image;
}

}  // namespace ksj::mri::debug
```

## 注释和空行

配置：

```yaml
ReflowComments: true
KeepEmptyLinesAtTheStartOfBlocks: false
MaxEmptyLinesToKeep: 1
```

含义：

- 长注释会根据 120 列规则自动换行。
- block 开头不保留空行。
- 连续空行最多保留 1 行。

示例：

```cpp
void run() {
  initialize();

  execute();
}
```

不是：

```cpp
void run() {


  initialize();
}
```

## 手动检查命令

检查暂存文件：

```bash
tools/devenv/linux/run.sh just format-staged
```

检查全仓非 legacy 范围：

```bash
tools/devenv/linux/run.sh just format-all
```

只格式化一个文件：

```bash
clang-format -i path/to/file.cpp
```

只检查一个文件：

```bash
clang-format --dry-run --Werror path/to/file.cpp
```

## 维护要求

- `.clang-format` 变化会影响全仓格式，应单独提交，避免和业务改动混在一起。
- 大规模格式化应单独 commit。
- legacy 排除目录如需纳入格式门禁，必须先由模块 owner 确认。
- `third_party/intel/payload/` 是 checksum-verified vendor payload，不属于项目格式化范围；其完整性由 Intel manifest verifier 和 Git LFS 检查负责。
- 新增格式规则时，应同步更新本文档和 [coding.md](coding.md)。
