# KSpaceJet Numerics

`kspacejet-numerics` 是 `libs/numerics` 的聚合模块。它不承载具体数学实现，只负责把各个能力域模块组合成一个统一入口 target 和一个 umbrella header。

它还提供一个很小的 numerics runtime，用来固定 Intel IPP/MKL 后端的进程级运行时策略。这个策略不从 site config 或环境变量读取；使用 Intel-backed numerics 的进程应在入口处显式调用一次 `ksj::numerics::initialize_numerics_runtime()`，把 IPP/MKL 内部线程固定为单线程。函数内部使用 `std::call_once`，重复调用是 no-op，但代码约定仍然是每个进程入口只调用一次。

完整分区规划见 [libs/numerics README](../README.md)。

## 职责

`KSpaceJet::numerics` 聚合以下 target：

- `KSpaceJet::array`
- `KSpaceJet::linalg`
- `KSpaceJet::fft`
- `KSpaceJet::signal`
- `KSpaceJet::image`
- `KSpaceJet::stats`
- `KSpaceJet::optimization`
- `KSpaceJet::special`
- `KSpaceJet::sparse`
- `KSpaceJet::numerics_runtime`

对应头文件：

```cpp
#include "kspacejet/numerics/numerics.hpp"
```

## 边界

`kspacejet-numerics` 不再放置旧的 `kspacejet/numerics/array`、`kspacejet/numerics/fft`、`kspacejet/numerics/linalg`、`kspacejet/numerics/detail` 等实现目录。具体实现应放在对应能力域模块中：

```text
kspacejet-array        -> kspacejet/array/...
kspacejet-linalg       -> kspacejet/linalg/...
kspacejet-fft          -> kspacejet/fft/...
kspacejet-signal       -> kspacejet/signal/...
kspacejet-image        -> kspacejet/image/...
kspacejet-stats        -> kspacejet/stats/...
kspacejet-optimization -> kspacejet/optimization/...
kspacejet-special      -> kspacejet/special/...
kspacejet-sparse       -> kspacejet/sparse/...
```

这样可以避免 `kspacejet-numerics` 变成“大杂烩”模块，也能让上层按需依赖具体能力域。
