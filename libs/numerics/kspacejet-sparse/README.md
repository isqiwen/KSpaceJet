# kspacejet-sparse

`kspacejet-sparse` 负责 sparse matrix handle 和 sparse-dense bridge。当前落地能力是 CSR matrix 与 SpMV；COO/CSC 仍是保留的格式边界，尚未作为完整公开数据结构实现。

当前已提供：

- `CsrMatrix<T>`
- `spmv(matrix, vector)`

`spmv` 输入/输出向量使用 `ksj::array::PooledVector<T>`。CSR 的 row offsets、row starts、row ends、
column indices 和 values 都存储在池化 vector 中；构造入口接受 `VectorView` 或 `std::span`，构造时会校验范围并转换为内部
`int` 索引，以匹配 Eigen sparse 和 Intel MKL sparse 当前接入路径。后续如需更大索引范围，必须先补 benchmark 和后端兼容性验证。

当前后端结构包含 Eigen sparse 路径和 Intel MKL sparse 路径，公开 API 通过
`detail::sparse_policy.hpp` 的阈值选择。阈值必须由 `ksj_sparse_backend_benchmark` 在目标机器上验证后再固化。
