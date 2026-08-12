# kspacejet-stats

`kspacejet-stats` 负责通用统计计算。当前落地能力是 sum、mean、variance、vector covariance 和 matrix covariance；metrics、histogram 等仍属于后续规划。

当前已提供：

- `sum(input)`
- `mean(input)`
- `variance(input, normalization)`
- `covariance(lhs, rhs, normalization)`
- `covariance(samples, output, normalization)`
- `covariance(samples, normalization)`

公开算法如果有 View 版本，也提供对应的 Pooled 版本；Pooled 版本只转发到 View 版本。matrix covariance 约定为
`rows=samples/observations`、`cols=variables/channels`，默认使用 sample normalization；complex covariance 使用
conjugated left-hand side。

当前后端结构：

- `sum` / `mean`：`double` 按 policy 尝试 Intel IPP，失败或规模不匹配时使用 Eigen；`float` 默认使用
  Eigen/reference 路径，IPP 只保留为 benchmark candidate。
- `sum_of_squares` / `root_sum_of_squares`：`double` 和部分 complex 路径按 policy 尝试 Intel IPP；`float`
  默认使用 Eigen/reference 路径，除非后续 benchmark 证明收益足以抵消 IPP 维护成本。
- `variance` / `covariance`：当前使用 Eigen 实现。

后端阈值由 `ksj_stats_backend_benchmark` 在目标机器上验证后反写到 policy。
