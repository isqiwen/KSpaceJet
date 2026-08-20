# KSpaceJet Commit Message Convention

KSpaceJet 使用规范化 commit message，便于多人协作、代码审查、历史检索和后续 release note 自动化。

本地 `commit-msg` hook 会检查 commit message 第一行。开发者第一次进入仓库后应先执行：

```bash
bash tools/devenv/linux/bootstrap.sh
```

Windows PowerShell：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1
```

bootstrap 会在当前仓库配置 Git/Git-LFS 并安装 `.githooks/commit-msg`；锁定的项目级
`uv` 环境提供 Python 开发工具，独立的 checksum-pinned project-local `just` 提供共享命令入口。
Git、Git LFS 和编译器/SDK 仍是宿主前置条件。完整环境说明见
[tools/devenv/README.md](../../tools/devenv/README.md)。

## 格式

commit message 第一行必须使用以下格式之一：

```text
type(scope): subject
type: subject
```

示例：

```text
docs: align developer setup documentation
fix(kspacejet-fe): handle missing runtime config
build(cmake): update numerics benchmark target
```

## Type

允许的 `type` 如下：

| Type | 含义 |
| --- | --- |
| `build` | 构建系统、CMake、preset、第三方依赖或打包相关变更 |
| `chore` | 不改变功能行为的仓库维护工作 |
| `ci` | GitHub Actions、hook、门禁脚本或自动化流程 |
| `docs` | README、设计文档、使用说明等文档变更 |
| `feat` | 新功能或新的用户可见能力 |
| `fix` | bug 修复或行为错误修正 |
| `perf` | 性能优化 |
| `refactor` | 不改变外部行为的代码结构调整 |
| `revert` | 回滚已有提交 |
| `style` | 纯格式、排版、命名风格调整，不改变行为 |
| `test` | 单元测试、集成测试、benchmark 测试代码变更 |

## Scope

`scope` 可选，但推荐在跨模块仓库里使用。它应描述受影响的模块、工具或领域。

常见示例：

```text
kspacejet-fe
kspacejet-be
core
memory
numerics
array
linalg
fft
cmake
checks
docs
```

scope 只允许字母、数字、点、下划线、短横线和斜杠。

## Subject

`subject` 是简短动作描述：

- 使用英文短句。
- 不以句号结尾。
- 第一行最长 120 个字符。
- 保持具体，避免 `update`、`misc changes`、`fix bug` 这类模糊描述。

推荐：

```text
fix(kspacejet-be): reload site config at scan start
docs: document developer bootstrap flow
perf(linalg): tune MKL dot threshold
```

不推荐：

```text
update readme
fix bug
misc
```

## 例外

以下 Git 自动化或临时修复格式允许通过：

```text
Merge ...
Revert ...
fixup! ...
squash! ...
```

## 手动检查

可以直接运行：

```bash
tools/devenv/linux/run.sh tools/checks/linux/commit_msg_check.sh .git/COMMIT_EDITMSG
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\checks\windows\commit_msg_check.ps1 `
  .git\COMMIT_EDITMSG
```

本地 hook 可以被 `--no-verify` 绕过，因此团队级强制门禁仍应依赖 GitHub Actions、分支保护规则或服务端 hook。
