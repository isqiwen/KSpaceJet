# KSpaceJet Conventions

本目录存放 KSpaceJet 全仓库通用开发规范。这里的内容适用于所有模块、工具和测试代码；模块内部设计细节仍放在对应模块 README 或 `docs/` 的专题设计文档中。

建议阅读顺序：

1. [Commit Message Convention](commit.md)
2. [Coding Convention](coding.md)
3. [Clang-Format Convention](clang_format.md)
4. [Build Convention](build.md)
5. [Test Convention](testing.md)
6. [Benchmark Convention](benchmark.md)
7. [Numerics API Convention](numerics_api.md)
8. [Reconstruction State Ownership](reconstruction_state.md)
9. [Parallelism Convention](parallelism.md)
10. [Release Convention](release.md)

本目录关注“团队如何协作”和“仓库如何保持一致”。如果某条规则已经被本地 hook 或 CI 自动检查，文档会说明对应脚本入口。

## Markdown 绘图

- Markdown 文档中的系统架构图、流程图、状态机、时序图和依赖图必须优先使用 Mermaid，并将 Mermaid 源码作为可评审、可维护的图定义。
- 只有目标渲染器不支持所需语义时才使用其他绘图形式，并在文档中说明原因；不要用截图或难以维护的 ASCII 图代替 Mermaid。
- 代码、伪代码、目录树和数据格式示例不是绘图，不强制改写为 Mermaid；应继续使用标注了正确语言的 fenced code block。
