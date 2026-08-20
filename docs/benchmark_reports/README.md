# Benchmark Reports

本目录保存用于审查 numerics policy 或 threshold 变更的正式 benchmark 摘要；它不保存运行时自动生成的原始产物，也不表示当前已有任何已接受的性能结论。

每份摘要使用以下目录形状：

```text
docs/benchmark_reports/<yyyy-mm-dd>/<suite>/<machine-id>/benchmark_report.md
```

从 [TEMPLATE.md](TEMPLATE.md) 开始，并填写提交或 tag、CMake preset、硬件与线程配置、输入矩阵、原始数据位置、正确性门禁、统计结果、推荐 policy 和限制。大型 CSV、原始 samples 和构建输出仍保留在受控制品存储或发布记录中，不提交 `out/` 产物。

研究型结果应放在 [docs/research_reports](../research_reports/README.md)；它们在独立正式 benchmark 摘要支持之前不得反写 production policy。
