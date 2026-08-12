# kspacejet-special

`kspacejet-special` 负责 Bessel、Gamma、elementary special functions 等特殊函数。Eigen special functions 是基础
后端；Intel MKL/VML 作为批量向量 fast path 候选，只在 benchmark 证明稳定更快的函数、类型和元素数区间
进入公开 API policy。

当前已提供：

- `gamma(value)` / `gamma(vector)`
- `log_gamma(value)` / `log_gamma(vector)`
- `bessel_i0(value)` / `bessel_i0(vector)`
- `bessel_j0(value)` / `bessel_j0(vector)`
- `bessel_j1(value)` / `bessel_j1(vector)`
- `bessel_j(order, value)`，支持实数参数以及 NUFFT 当前需要的纯虚数复数参数
- `exp2(value)`：计算 `2^value`
- `expm1(value)`：计算 `exp(value) - 1`，在零附近保持精度
- `log1p(value)`：计算 `log(1 + value)`，在零附近保持精度
- `erfc(value)`：计算互补误差函数 `1 - erf(value)`
- `sinpi(value)` / `cospi(value)`：计算 `sin(pi * value)` / `cos(pi * value)`

标量函数可以直接返回标量；dense 函数以 `ksj::array::*View` 输入和显式 output View 作为计算入口，也提供返回
新的 `Pooled*` 的便捷 overload。对应的 `Pooled*` 输入 overload 只做 `.view()` 转发。

后端接入计划和当前 benchmark 结论见 [BACKEND_CAPABILITY_MATRIX.md](BACKEND_CAPABILITY_MATRIX.md)。
特殊函数不会在没有能力确认和 benchmark 数据前切换到新的第三方后端。

和其它 numerics 模块一样，`kspacejet-special` 的 public API 不直接绑定某个第三方库。后端候选按功能放在
`detail/<backend>/<backend>_special_<feature>.hpp` 和 `src/<backend>/<backend>_special_<feature>.cpp`，
例如当前的 `detail/eigen/eigen_special_functions.hpp` / `src/eigen/eigen_special_functions.cpp`。选择规则放在
`detail/special_policy.hpp`。新增后端时必须先扩展 `ksj_special_backend_benchmark`，再把稳定阈值写回
policy。
