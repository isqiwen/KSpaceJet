# kspacejet-optimization

`kspacejet-optimization` 负责通用优化算法。当前落地能力是 least squares 和 downhill simplex；line search 和更多迭代优化器仍属于后续规划。
它可依赖 `kspacejet-linalg`，但不依赖 MRI 业务模块。

当前已提供：

- `least_squares(matrix, rhs, method)`
- `downhill_simplex(objective, initial, lower_bounds, upper_bounds, ...)`

公开接口使用 `ksj::array::PooledMatrix<T>` 和 `ksj::array::PooledVector<T>`。

当前后端结构：

- `least_squares(..., qr/svd)` 直接委托 `kspacejet-linalg` 的公开 solver；后端选择、workspace reuse 和 policy
  都由 `ksj::linalg` 统一负责。
- `normal_equations` 与 downhill simplex 当前为 Eigen/reference 实现。

`ksj_optimization_backend_benchmark` 用 linalg 的同一 detail candidate 和 workspace-reuse scope 验证
optimization facade 没有改变真实的 linalg policy。optimization 不维护第二套独立阈值。
