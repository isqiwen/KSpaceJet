# kspacejet-special 后端能力矩阵

`kspacejet-special` 的目标是为特殊函数提供稳定、可 benchmark 的高性能实现。特殊函数和线性代数、FFT 不同：
很多第三方库只覆盖部分函数或部分标量类型，且不同库在精度、定义域、向量化策略上差异较大。
因此本模块先维护后端能力矩阵，再决定哪些函数值得加入正式后端。

## 当前公开函数

| 函数 | 当前实现 | 输入形态 | 说明 |
| --- | --- | --- | --- |
| `gamma(value)` | Eigen scalar special function | scalar | 标量路径主要关注精度和调用开销。 |
| `log_gamma(value)` | Eigen scalar special function | scalar | 常用于避免 `gamma` 溢出。 |
| `bessel_i0(value)` | Eigen scalar special function | scalar | MRI 和信号处理中可能作为窗函数或核函数的一部分。 |
| `gamma(vector)` | Eigen array expression | `PooledVector<T>` | 结果返回池化向量。 |
| `log_gamma(vector)` | Eigen array expression | `PooledVector<T>` | 批量 log-gamma，常用于避免批量 gamma 溢出。 |
| `bessel_i0(vector)` | Eigen array expression | `PooledVector<T>` | 结果返回池化向量。 |
| `bessel_j0(vector)` | Eigen array expression / MKL VML policy | `PooledVector<T>` | float 批量路径在稳定阈值后使用 VML。 |
| `bessel_j1(vector)` | Eigen array expression / MKL VML policy | `PooledVector<T>` | float/double 批量路径在稳定阈值后使用 VML。 |

## 候选后端能力

| 后端 | `gamma` | `log_gamma` | `bessel_i0` | `bessel_j0/j1` | 向量批处理 | 当前结论 |
| --- | --- | --- | --- | --- | --- | --- |
| Eigen unsupported SpecialFunctions | 支持 | 支持 | 支持 | 支持 | 支持 Eigen array expression | 当前基线实现，覆盖面最好。 |
| C++ 标准库 | `std::tgamma` | `std::lgamma` | 不完整 | 支持部分标量函数 | 不支持原生向量批处理 | 可作为标量正确性对照，不适合批量 fast path。 |
| Intel MKL / VML | `LGamma + Exp` 支持现有 unsigned gamma 语义 | 支持 | 支持但实测慢 | 支持 `J0/J1` | 支持 contiguous float/double vector | 已接入 candidate；只在 benchmark 稳定更快的区间进入 policy。 |
| Intel IPP | 未确认 | 未确认 | 未确认 | 未确认 | 可能支持部分基础数学核 | 当前不作为特殊函数后端。 |
| Boost.Math | 支持 | 支持 | 支持 | 支持 | 不支持原生池化向量批处理 | 可用于高精度或特殊定义域验证，但不是首选 fast path。 |

## 当前 policy 结论

`ksj_special_backend_benchmark` 在 oneAPI 2026 MKL/VML 上验证后，当前 public policy 为：

| 函数 | float | double | 说明 |
| --- | --- | --- | --- |
| `gamma(vector)` | VML when `size >= 1024` | Eigen | `gamma` 保持现有 `exp(lgamma(x))` 语义，不直接调用 signed `TGamma`。 |
| `log_gamma(vector)` | VML when `size >= 256` | Eigen | double VML `LGamma` 实测慢于 Eigen。 |
| `bessel_i0(vector)` | Eigen | Eigen | VML `I0` 在当前环境显著慢于 Eigen，不能启用。 |
| `bessel_j0(vector)` | VML when `size >= 64` | Eigen | double VML `J0` 实测慢于 Eigen。 |
| `bessel_j1(vector)` | VML when `size >= 256` | VML when `size >= 256` | 小 size 波动较大，阈值保守设置。 |

## 后续接入规则

1. 只有当候选库明确提供目标函数、目标标量类型和批处理接口时，才新增 detail 后端。
2. 后端文件必须按函数域命名，例如 `intel_special.hpp`、`boost_special.hpp`。
3. benchmark 必须比较 Eigen、候选后端和必要的标量基线。
4. public API 只按 policy 调用 benchmark 证明最快的路径；没有数据时保持 Eigen 基线。
5. 对精度敏感的函数需要同时输出误差报告，不能只比较耗时。

## 维护注意

- 若输入定义域扩展到负非整数，需要重新确认 `gamma` 的数学语义，因为当前 vector `gamma` 按既有
  `exp(lgamma(x))` 行为返回非负幅值。
- 若升级 MKL/VML 或切换 CPU/compiler，重新运行 `ksj_special_backend_benchmark` 并更新 policy 阈值。
