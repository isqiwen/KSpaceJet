# KSpaceJet Research

`tests/research/` 存放研究型、手册型 C++ 工程。这里的代码用于解释性能现象和验证写法取舍，不属于正式单元测试、集成测试或生产阈值 benchmark。

## 当前工程

- [cpp-numerics-performance-handbook](cpp-numerics-performance-handbook/README.md)：C++ 高性能数值计算代码实现与优化手册，用可运行 case 对比 fused kernel、layout 和内存访问方式。

## 边界

- research 工程默认不构建，由 `KSJ_BUILD_RESEARCH=ON` 显式开启。
- research 可执行程序不注册为 CTest。
- research 结果用于理解具体代码写法，不直接反写生产性能阈值。
- 需要反写生产性能阈值的结论，应进入 `tests/benchmarks/` 和 `tools/ksj_numerics_benchmark/` 的正式 benchmark 流程。
