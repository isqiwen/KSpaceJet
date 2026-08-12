# kspacejet-array

`kspacejet-array` 是 KSpaceJet numerics 层的数据对象模块，提供基于 `kspacejet-memory` 的池化向量、矩阵、二维 image、三维 cube 和四维 array。

模块构建为动态库，但 `Pooled*` 和 `View` 仍然是模板类型，定义保留在 public header 中。动态库主要承载少量非模板后端 candidate，例如 IPP/MKL 这类 Intel backend 的固定标量类型实现。

## 设计原则

- 内存由 `kspacejet-memory` 分配和释放。
- 计算表达式、向量化、alias 处理和矩阵算法交给对应 numerics 后端；Eigen 等第三方实现只允许留在
  private/detail/source 边界，不能成为 public contract。
- `PooledMatrix`、`PooledImage`、`PooledCube` 和 `PooledArray4D` 使用 KSpaceJet row-major 布局，贴合现有 MRI buffer、OpenCV/IPP row stride 模型。
- `PooledVector` 是一维连续对象，不参与二维或高维布局策略。
- `Pooled*` 类型不提供 layout 参数，也不提供 column-major owning storage。KSpaceJet 内部长期持有的数据必须使用 row-major Pooled 对象。
- `View` 类型保留 stride 能力，用于 ROI、行列切片和非连续 subview，但不能用来表达 column-major 数据布局。业务代码默认应从 `Pooled*::view()`、`row()`、`col()`、`slice()`、`subview()` 或 runtime facade 获取 View。
- 二维对象的底层 detail 实现是 `PooledDense2D<T>`；公开算法接口应使用 `PooledMatrix<T>` 或 `PooledImage<T>`，让矩阵和图像在类型系统中保持可区分。

## 基本用法

```cpp
#include "kspacejet/array/array.hpp"

auto a = ksj::array::PooledMatrix<double>::uniform_random(128, 128, -1.0, 1.0);
auto c = ksj::array::make_pooled_matrix<double>(128, 128);

ksj::array::copy(a.view(), c.view());

auto volume = ksj::array::make_pooled_cube<float>(rows, cols, slices);
volume(0, 0, 0) = 1.0F;
volume.set_zero();

auto series = ksj::array::make_pooled_array4d<float>(rows, cols, slices, frames);
series.set_zero();

auto image = ksj::array::make_pooled_image<float>(height, width);
image(row, col) = 1.0F;
auto bytes_per_row = image.row_stride_bytes();

auto matrix_from_image = ksj::array::copy_as_matrix(image);
auto image_from_matrix = ksj::array::copy_as_image(matrix_from_image);
```

公开使用应通过 `view()`、`row()`、`col()`、`slice()`、`subview()` 和显式 output primitive 表达。现存
public Eigen adapter 属于迁移债务；新代码不得把 Eigen Map、TensorMap 或 Eigen expression 作为跨模块接口。

## 头文件结构

`kspacejet/array/array.hpp` 是模块的统一对外入口，外部模块应该只包含这个头文件。内部实现按职责拆分：

- `kspacejet/array/pooled_vector.hpp`：池化一维向量。
- `kspacejet/array/pooled_matrix.hpp`：池化二维矩阵。
- `kspacejet/array/pooled_image.hpp`：池化二维行主序图像。
- `kspacejet/array/layout_conversion.hpp`：矩阵和图像之间的显式拷贝转换。
- `kspacejet/array/pooled_cube.hpp`：池化三维 cube。
- `kspacejet/array/pooled_array4d.hpp`：池化四维 array。
- `kspacejet/array/scalar_traits.hpp`：标量类型 trait，例如复数识别和实数标量类型。
- `kspacejet/array/views.hpp`：view 类型定义和零拷贝 view adapter。
- `kspacejet/array/slicing.hpp`：subview、row、col、slice 等选择接口。
- `kspacejet/array/initialization.hpp`：`fill`、`fill_linspace`、`set_identity`、`fill_uniform_random` 等初始化接口。
- `kspacejet/array/indexing.hpp`：线性下标、索引集合、gather/scatter 等索引类算法。
- `kspacejet/array/copy.hpp`：copy、centered copy、transpose、masked copy 等拷贝类算法。
- `kspacejet/array/transforms.hpp`：for_each、transform、reverse_in_place、rotate_left_in_place 等变换类算法。
- `kspacejet/array/reductions.hpp`：accumulate、min/max、argmin、count、forward difference 等归约类算法。

`kspacejet/array/detail/` 下的头文件只供 `kspacejet-array` 自己实现使用，不作为跨模块 include 入口。后续迁移应继续把
第三方头和后端实现收进 `.cpp` 或 private detail 边界。

## Layout 类型边界

`PooledMatrix<T>` 和 `PooledImage<T>` 都是行主序二维对象。二者共享 detail 层二维 dense 实现，但不是同一个类型，因此 `kspacejet-linalg`、`kspacejet-image` 等模块可以通过函数签名在编译期区分输入：

```cpp
void run_linalg(const ksj::array::PooledMatrix<float>& matrix);
void run_image_op(const ksj::array::PooledImage<float>& image);
```

这样矩阵不会因为底层也是二维 dense storage 就被误传给图像算法；图像专属的 `height()`、`width()`、`row_stride_elements()` 和 `row_stride_bytes()` 也只在 `PooledImage<T>` 上可用。

矩阵和图像之间的 owning 转换必须显式表达为拷贝：

```cpp
auto matrix = ksj::array::copy_as_matrix(image);
auto image = ksj::array::copy_as_image(matrix);
```

`copy_as_matrix` 和 `copy_as_image` 会保持逻辑下标不变；二者都是 row-major 到 row-major 的显式拷贝。

如果已经持有兼容的二维对象或 View，可以使用 `matrix_view(ImageView<T>)`、`matrix_view(PooledImage<T>&)`、`image_view(MatrixView<T>)` 或 `image_view(PooledMatrix<T>&)` 做零拷贝借用。`image_view(MatrixView<T>)` 仅接受 column stride 为 1 的 image-like matrix view。

所有 `*_view(...)` adapter 都只借用已有内存，不拥有内存，不分配内存，也不会为了适配而隐式拷贝。如果 View 不能零拷贝表达，接口必须抛错或让调用方显式选择 `copy_as_*`、`pack` 或调用方提供的 scratch/output。

模块不提供 `to_matrix`、`to_image`、隐式构造或隐式转换，避免性能敏感路径误以为这是零拷贝 view。

## View Layout 边界

`VectorView`、`MatrixView`、`ImageView`、`CubeView` 和 `Array4DView` 的公开构造只接受
`data + shape`，stride 由 row-major 语义内部推导。View 内部仍保存 stride，因此 `subview(...)`、
`as_const_view(...)`、`matrix_view(...)` / `image_view(...)` 这类内部借用路径可以保留非连续 ROI 的
stride。这个能力只表示“借用已有内存”，不表示 KSpaceJet 支持 column-major owning storage，也不表示外部可以
手写 stride 创建任意 layout View。

推荐：

```cpp
auto view = matrix.view();
auto row = matrix.row(row_index);
auto every_other = vector.view().subview(ksj::array::slice(0U, vector.size(), 2U));
auto roi = image.subview(ksj::array::slice(row_start, row_start + row_count),
                         ksj::array::slice(col_start, col_start + col_count));
```

一维隔点访问使用 `subview(slice(start, stop, step))` 表达，例如历史 vector decimation 或 interleaved
channel/real-imag 边界。它不分配、不拷贝，也不是 layout 转换接口。

任何新代码都不应新增 column-major stride View，例如二维 row stride 为 1、column stride 为 rows，或三维 row stride 为 1、column stride 为 rows、slice stride 为 rows * cols 的构造。

如果外部库或旧数据格式必须使用 column-major，必须在边界处显式 pack 到 row-major Pooled，或把转换封装到 numerics/backend detail。column-major stride View 不能作为 public API 或业务算法输入输出继续存在。

## 后端策略

`kspacejet-array` 是 numerics 的基础数据层，和 `kspacejet-image`、`kspacejet-linalg` 这类算法 facade 不完全一样：它不对所有 `fill`、`copy`、`for_each`、`transform`、`accumulate` 这类 primitive 建立全局运行时后端分派。这些 primitive 是轻量的基准实现，要求简单、可内联、可预测，并作为其它模块的最小公共 building block。

对性能敏感且语义明确的命名算法，可以在 `detail/` / `src/` 下增加后端实现和 policy，例如复数幅值、相位、极坐标转换等。公开 API 仍然只暴露 View/Pooled 接口；Pooled 接口转发到 View 接口，View 接口根据 policy 选择后端或回退到基础 loop。

当前策略：

- contiguous `VectorView`、`MatrixView`、`ImageView` 的复数 `magnitude` 可以走 Eigen 后端。
- contiguous `VectorView`、`MatrixView`、`ImageView` 的复数 `magnitude`/`phase` 已有 Intel IPP candidate，但 policy 默认关闭，只在 benchmark 中直接测。
- `phase` 和 `rectangular_to_polar` 已有 Eigen benchmark candidate，但默认仍使用基础 loop，等完整 benchmark sweep 证明收益稳定后再打开。
- 语义明确的 contiguous `CubeView`/`Array4DView` dimwise 算法可以走 Eigen/detail 快路径；轴命名保持
  `dim0/dim1/dim2/dim3`，不在 numerics API 中编码 MRI 的 channel/slice 语义。
- `rectangular_to_polar` 当前输出是 interleaved `std::complex{magnitude, phase}`；IPP 的 `CartToPolar` 输出是两个连续实数数组，因此不能零拷贝接入现有 API。除非新增双输出 View API，否则不接 Intel candidate。

后端阈值和开启条件必须来自 `tests/benchmarks/kspacejet-array/array_backend_benchmark.cpp` 的结果。新增后端时，应同时提供：

- `detail/<backend>/<backend>_array_<feature>.hpp` 中的后端声明和
  `src/<backend>/<backend>_array_<feature>.cpp` 中的 View 输入输出实现。例如
  `detail/eigen/eigen_array_complex.hpp`、`detail/eigen/eigen_array_dimwise.hpp` 和匹配的
  `src/eigen/eigen_array_*.cpp`。
- `detail/array_policy.hpp` 中的选择条件，默认保守关闭，确认收益后打开。
- benchmark 中的 `public_output`、`pooled_<backend>` 和 `manual_output` 对照项。

普通算法接口不应该隐藏临时 Pooled 分配。高频路径优先提供 `output View` 版本；需要链式写法时，可以额外提供返回 `Pooled*` 的便利函数，但它必须显式表现为会分配输出对象。
