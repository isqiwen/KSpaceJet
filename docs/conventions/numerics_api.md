# Numerics API Convention

本文档定义 KSpaceJet 数值计算接口的统一规则。它适用于 `libs/numerics` 和调用 numerics 的开放 provider。模块 README 可以补充局部设计，但不能放宽这里的边界。

## 核心原则

- `PooledVector` / `PooledMatrix` / `PooledImage` / `PooledCube` / `PooledArray4D` 表示拥有内存的数据对象。
- `VectorView` / `MatrixView` / `ImageView` / `CubeView` / `Array4DView` 表示不拥有内存的借用视图。
- `Pooled*` 永远使用 KSpaceJet row-major 逻辑布局，不能提供 layout 参数，也不能新增 column-major owning
  对象。
- `View` 保留 stride 能力，但不能用来表达 column-major 数据布局。业务代码默认只能从
  `Pooled*::view()`、`row()`、`col()`、`slice()`、`subview()` 或 runtime facade 获得 View。
- 长期持有数据、workspace、scan/shared state 和需要返回新对象的便捷接口使用 `Pooled*`。
- 算法核心接口使用 `View` 作为输入输出。
- 输入输出默认分开。只有算法语义确实要求原地修改时，才提供 `_in_place` 接口。
- 第三方后端适配层只接受 `View`，并尽量借用调用方内存。
- 第三方库类型、头文件和 target 不能从 public API 泄漏出去。Eigen、OpenCV、ITK、MKL、IPP、FFTW、BART
  等后端必须隔离在 backend detail 或 `.cpp` 实现中；CMake 依赖默认使用 `PRIVATE`。
- `kspacejet-array` 也遵守同一规则：它对外提供 KSpaceJet 的 row-major `Pooled*` / `View` 抽象和基础标量 traits，
  不把 Eigen 类型、Eigen include 或 Eigen 表达式作为稳定 public contract。现存 Eigen 暴露属于迁移债务，
  新代码不得继续扩散。
- 每个 numerics 子模块必须有一个 umbrella 入口头，例如 `kspacejet/stats/stats.hpp`；公开头按数学功能划分，
  不能按容器类型或旧代码来源划分。
- `kspacejet-array` 是 dense View/Pooled 的公共 primitive 层。`libs/numerics` 的其它模块和单独授权 provider
  遇到通用 elementwise、copy/fill、reduction、complex magnitude/phase、dimwise、gather/scatter、reshape/subview
  等数组操作时，应优先调用 `kspacejet-array` 的公开接口，让已有 backend policy、IPP/Eigen 加速、连续块 traversal
  和 alias 处理统一生效。不要在上层模块重复写同语义循环或直接绕到 Eigen/IPP。
- numerics 算法必须可重入、线程安全；workspace 必须有明确所有者，不能通过隐藏的共享 scratch 实现性能优化。

## Public API 注释

每个 `libs/numerics/*/include/kspacejet/...` public feature header 必须在入口用简洁注释说明该 feature 的数学或数据
语义，以及 View/Pooled 的主要调用约定。umbrella header 说明它聚合的 API 范围，不重复具体函数说明。

每个新增的 public 函数 family 必须在第一个 overload 前注明：

- 它计算的数学量或执行的状态变化；
- 重要的定义域、单位、边界或 alias 约束；
- View output overload 写入调用方 buffer，返回值 overload 分配一个新的 Pooled 结果（若同时提供）。

同一函数的 View、Pooled forwarding 和返回值 overload 共享一个 contract 时，可以用一组简洁注释覆盖该 overload
family；不要为机械转发复制相同的长说明。缩写或历史兼容别名必须明确指出对应的 canonical API。

推荐形态：

```cpp
void normalize(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output);

[[nodiscard]] ksj::array::PooledImage<float>
normalize(ksj::array::ImageView<const float> input) {
  auto output = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  normalize(input, output.view());
  return output;
}

[[nodiscard]] ksj::array::PooledImage<float>
normalize(const ksj::array::PooledImage<float>& input) {
  return normalize(input.view());
}
```

不推荐形态：

```cpp
void normalize(const ksj::array::PooledImage<float>& input,
               ksj::array::PooledImage<float>& output); // 算法核心被迫拥有完整 Pooled 对象

void normalize(ksj::array::ImageView<float> input_output); // 输入输出混在一起，但函数名没有 _in_place
```

## View 优先作为算法接口

算法接口使用 View 的原因：

- 不分配内存，适合数值 kernel。
- 可以表示 ROI、row/column view、regular stride view，不需要拷贝。
- `View<const T>` 清楚表达只读输入，`View<T>` 清楚表达输出。
- Eigen、OpenCV、ITK、MKL、IPP 等后端可以通过 map/import/native view 借用现有内存。
- 更容易替换旧 `MatrixAdapter` / `VectorAdapter`。

View 的限制：

- 不负责生命周期，调用方必须保证底层数据仍然有效。
- 不拥有容量，不能 `resize`。
- 不适合作为长期状态成员保存，除非它只是短生命周期 facade，且底层 owner 明确。

## Pooled 用作存储和便捷包装

Pooled 类型的职责：

- 拥有数值内存。
- 固定使用 KSpaceJet row-major 逻辑布局：
  - `PooledVector` 是一维连续存储。
  - `PooledMatrix` / `PooledImage` 使用 `row_stride == cols`、`col_stride == 1`。
  - `PooledCube` 使用 `(row * cols + col) * slices + slice`。
  - `PooledArray4D` 使用最后一维连续的 row-major 逻辑布局。
- 作为 runtime workspace、scan/shared state、算法缓存和返回对象。
- 为调用者提供方便的 `view()`、`row()`、`col()`、`subview()`、`reshape_view()` 等访问能力。

Pooled 类型不得提供 layout 模板参数、layout 枚举、column-major 构造函数或 column-major factory。需要从
外部 column-major 数据导入时，必须在 IO/后端边界显式转换为 KSpaceJet row-major Pooled 对象，或者把该边界封装在
numerics detail 中。

Pooled 类型不应该承载复杂算法成员函数。复杂计算放到对应 numerics 模块的 free function 中。

如果一个公开算法提供 View 版本，也应该提供对应 Pooled 便捷版本；Pooled 版本只做 `.view()` 转发或创建
输出对象后转发。不要为 Pooled 和 View 写两套不同计算逻辑。

## 返回值便捷接口与热路径

numerics 的主计算接口仍然是 View 输入和 View 输出：

```cpp
void phase(ksj::array::ImageView<const std::complex<float>> input,
           ksj::array::ImageView<float> output);
```

当一个函数的语义天然是“产生一个新的 dense 结果”时，可以提供返回 `Pooled*` 的便捷重载：

```cpp
[[nodiscard]] ksj::array::PooledImage<float>
phase(ksj::array::ImageView<const std::complex<float>> input) {
  auto output = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  phase(input, output.view());
  return output;
}
```

这类返回值接口的规则：

- 返回 `Pooled*` 本身不应产生数据拷贝；依赖 NRVO 或 move，只移动 owner 句柄和尺寸元数据。
- 真正的额外成本是创建新的输出 buffer。若调用点原本就需要 `make_pooled_* + op(input, output.view())`，
  返回值版本只是把同一件事封装起来，不应多一轮计算或拷贝。
- 返回值重载只能分配最终输出并转发到 View 输出接口；不能隐藏大型 workspace、不能绕过 backend policy，
  也不能另写一套计算逻辑。
- 重建热循环、重复调用、OpenMP worker 内部、已能复用 scratch/output 的路径，应优先使用 output-param、
  workspace、plan 或 fused primitive。不要为了代码短，把每一步都写成返回新 `Pooled*` 的链式调用。
- 链式返回值调用会产生中间 `Pooled*`，可能增加分配次数和内存带宽压力。热路径里的
  `abs -> square -> sum`、`scale -> add`、`phasor -> conjugate product` 等组合，应优先使用 output-param
  或补充融合 primitive。
- benchmark 和 backend policy 决策必须基于预分配 output 的计算核结果；返回值重载的 benchmark 只用于衡量
  API 便利层带来的分配成本。

适合提供返回值重载的场景：

- 纯创建型函数，例如 `zeros_matrix(rows, cols)`、`window(size)`、`identity_matrix(n)`。
- 一次性产生新结果的常规计算，例如 `phase(input)`、`real(input)`、`fft2(input)`、`matmul(a, b)`。
- 测试、诊断、小工具和非热点 orchestration 代码，需要更清晰表达数据流。

不适合只提供返回值重载的场景：

- 原地修改，例如 `fftshift_in_place(data)`。
- 明确写入调用方已有区域、ROI、mask 或 sparse structure 的 API。
- 需要复用大 workspace 或 plan 才能达到性能目标的 API。
- 只改变输出的一部分、绘制/填充 region、IO/import/export 等副作用型操作。

Pooled 类型可以提供少量 Eigen 风格的基础便利成员，例如 `sum()`、`mean()`、`min()`、`max()`、
`squared_norm()`、`norm()`、`fill()`、`set_zero()`、`set_ones()`、`set_identity()` 等。这些成员必须满足：

- 不隐藏分配；reduction 只读输入，初始化只写当前对象。
- 直接转发到对应 View primitive，不能另写一套循环。
- 不引入复杂业务语义；业务算法仍应放在 `kspacejet-image`、`kspacejet-linalg`、`kspacejet-fft` 等模块的 free function 中。
- 不提供会隐式创建临时大对象的 `operator+`、`operator-`、`operator*` 等表达式接口。逐元素计算使用
  `add(input, input, output)`、`scale(input, scalar, output)` 这类显式 output API。

`resize()` 只改变逻辑尺寸和容量，不保证新增元素初始化为零；需要确定初值时使用 `zeros()`、`set_zero()`、
`constant()`、`fill()` 或带显式 value 的 resize/assign API。

## 复用 kspacejet-array Primitive

`kspacejet-array` 是整个 numerics 和重建代码共享的 dense 数组基础层。新增或迁移 `libs/numerics` 子模块、
开放 provider 的计算代码时，先检查目标操作是否已经由 `kspacejet-array` 提供：

- elementwise：`add`、`subtract`、`multiply`、`divide`、`scale`、`scale_add`、`minimum`、`maximum`、
  `clamp`、`where`、`replace_nan`、`isfinite` 等。
- initialization/copy：`fill`、`set_zero`、`copy`、`copy_centered`、pack/contiguous helpers。
- reductions/norms：`sum`、`min`、`max`、`argmin`、`argmax`、`squared_norm`、`norm`、按 `Dim` 的 reduction。
- complex：`absolute`/magnitude、`phase`、`conjugate`、`unit_phasor`、conjugate-product 类融合 primitive。
- indexing/shape：`subview`、`reshape_view`、`flatten_view`、`take`、`scatter`、`for_each_indexed`、
  `for_each_zip`。

这些 primitive 已经集中处理 row-major 语义、非连续 View、连续块 traversal、alias 规则和 backend policy；
上层模块重复手写同语义循环会绕过优化，也会让行为分叉。只有以下情况才应在调用模块本地实现循环：

- 循环表达的是该模块的核心算法语义，例如特定 stencil、动态规划、图搜索、region growing 或 MRI 专用控制流。
- `kspacejet-array` 没有对应 primitive。此时如果语义通用，应优先补到 `kspacejet-array` 并加测试/benchmark；如果只属于
  某个算法域，才留在该模块。
- 第三方后端有模块特有接口，且无法由 `kspacejet-array` 的通用 primitive 表达；这类代码必须留在对应 backend
  `.cpp` 或 `detail/<backend>/` 边界，不得暴露到 public API 或 provider 业务层。

迁移旧代码时，常见替换方向是：

```cpp
// 避免：上层重复手写普通逐元素循环
for (std::size_t i = 0; i < input.size(); ++i) {
  output(i) = input(i) * scale + bias(i);
}

// 推荐：使用 kspacejet-array primitive，policy 自行选择 direct/Eigen/IPP/fallback
ksj::array::scale_add(input, scale, bias, output);
```

## View 创建边界

View 的 stride 能力用于表达借用内存、ROI 和行列切片；它不是让代码继续使用旧 column-major 语义的入口。
业务代码应优先通过以下方式获得 View：

```cpp
auto image_view = image.view();
auto row = matrix.row(row_index);
auto roi = image.subview(row_start, row_count, col_start, col_count);
auto workspace_view = op.workspace().main_matrix_view();
```

任何新代码都不应手写 column-major stride View，例如二维 row stride 为 1、column stride 为 rows，
或三维 row stride 为 1、column stride 为 rows、slice stride 为 rows * cols 的构造。

允许手写 stride View 的场景必须局限在：

- `libs/numerics/*/detail` 中适配 Eigen/OpenCV/ITK/MKL/IPP/FFTW/BART 等后端，但适配结果不得作为
  column-major View 暴露给 public API 或业务层。
- IO/transport/runtime 边界把外部 row-major buffer 临时解释为 View。
- 单元测试/benchmark 为了覆盖非连续 view、第三方布局或迁移 golden case。

如果确实收到外部 column-major 数据，必须在边界处显式 pack 到 row-major Pooled，或把转换封装到
numerics/backend detail。不能创建 column-major stride View 并向下游传递。

## 输入输出分离

默认接口应显式分开输入和输出：

```cpp
void filter(ksj::array::ImageView<const float> input,
            ksj::array::ImageView<float> output);
```

原地接口必须显式命名：

```cpp
void fftshift_in_place(ksj::array::VectorView<std::complex<float>> data);
```

如果算法可以安全支持输入输出 alias，应在实现中显式处理。若不支持 alias，应在文档、测试或运行时检查中说明。

## Runtime Precondition Checks

廉价的 View size/shape、axis/rank 等前置条件检查应默认保留，即使在 Release 构建中也应继续抛出明确异常。它们通常只是
少量整数比较，不应为了微小收益牺牲错误输入的可诊断性。

`kspacejet-array` 使用 `KSJ_ARRAY_RUNTIME_CHECKS` 只控制明显昂贵、且位于热路径的防御性检查：

- 未显式定义时，Debug 默认开启，Release 默认关闭。
- 需要在 Release 中保留这些重检查时，可在编译参数中设置 `-DKSJ_ARRAY_RUNTIME_CHECKS=1`。
- 适合该开关控制的检查包括 elementwise/backend dispatch 中需要遍历 view block 或计算完整 span 集合的重 alias guard，
  例如 `views_may_overlap` 一类检查；也包括 benchmark 已证明对小 size 热路径有明显影响的 alias guard。
- 不要用该开关关闭普通 size/shape mismatch、unsupported axis、rank 越界。连续内存区间的简单指针 overlap
  检查默认保留，只有 benchmark 证明它在具体热路径上有实质成本时才纳入该开关。
- 不能用该开关关闭会改变算法语义的逻辑，例如 `copy` 对重叠输入输出的 temporary/backward-copy 处理、长度溢出保护、
  后端能力判定中的 contiguous/size 上限检查，或必须向调用方返回失败状态的 API。

关闭 runtime checks 的 Release 构建只把这些昂贵 alias guard 视为调用方 contract；普通形状和 axis 错误仍应由公开接口检查。

## 线程安全与 Workspace

`libs/numerics` 的公开计算接口必须能在单线程、多线程和 OpenMP worker 中安全使用。线程安全的基本含义是：
调用同一个 numerics 函数的多个线程，只要没有写入同一片输出内存，就不会因为 numerics 内部共享状态而产生
data race 或结果污染。

必须遵守：

- numerics kernel 必须可重入。不得使用可变全局对象、函数内 `static` scratch、隐藏 singleton workspace 或
  进程级 mutable cache 保存中间计算数据。
- 输入 View 可以多线程共享读取；输出 View 必须由调用方保证独占写入，或保证不同线程写入的 subview/region
  不重叠。
- 如果算法需要随输入规模增长的临时 workspace，优先由调用方显式传入，或在函数内部创建局部 `Pooled*`
  对象。多线程热循环中的 workspace 必须是线程私有，例如在 OpenMP `parallel` block 内为每个 worker 创建
  一份 scratch。
- 不要把大型 scratch 隐藏在普通 `as_eigen()`、`as_opencv()`、`as_itk()`、`as_mkl()` helper 里。这些 helper
  默认只借用内存；确实需要 pack/copy 时，应在 backend implementation 中显式可见。
- 不要用 `thread_local` 作为默认 workspace 优化。它虽然按线程隔离，但在嵌套调用、任务调度复用线程、
  测试并发和未来 fiber/task runtime 中容易隐藏生命周期问题。只有在 benchmark 证明收益显著，并且有明确
  生命周期、容量上限和测试覆盖时，才可以作为 backend detail 的局部实现选择。
- policy 判定函数必须是纯函数或只读常量判断。不能在 policy 中写入统计计数、缓存后端选择结果或更新全局状态。

允许的状态：

- `constexpr` / immutable lookup table。
- 第三方库要求的进程级初始化，例如 `ksj::numerics::initialize_numerics_runtime()`；这类初始化必须放在
  numerics runtime 或 process runtime 中，通过 `std::call_once` 等机制保证幂等，不得保存算法 scratch。
- 调用方显式传入的 workspace 对象，或函数内部局部 `Pooled*` 临时对象。

推荐形态：

```cpp
void left_singular_vectors(ksj::array::MatrixView<const std::complex<float>> input,
                           ksj::array::MatrixView<std::complex<float>> output);

#pragma omp parallel
{
  auto matrix_work = ksj::array::make_pooled_matrix<std::complex<float>>(rows, cols);
  auto output_work = ksj::array::make_pooled_matrix<std::complex<float>>(rows, std::min(rows, cols));

#pragma omp for
  for (...) {
    fill_input(matrix_work.view());
    ksj::linalg::left_singular_vectors(ksj::array::as_const_view(matrix_work.view()), output_work.view());
    store_output(output_work.view());
  }
}
```

不推荐形态：

```cpp
MatrixWork& global_svd_scratch(); // 多线程共享 scratch

void left_singular_vectors(...);  // 内部偷偷复用 global_svd_scratch()
```

## 后端适配

后端 detail helper 应接受 View，而不是 Pooled：

```cpp
namespace ksj::image::detail::opencv {
bool resize(ksj::array::ImageView<const float> input,
            ksj::array::ImageView<float> output);
}
```

适配第三方库时默认借用内存：

- Eigen 使用 `Map` / strided map。
- OpenCV 使用 `cv::Mat(rows, cols, type, data, step)`。
- ITK 使用 import image 或等价 view/import 机制。
- MKL/IPP 使用 data pointer + stride/leading dimension。

只有这些情况允许分配新的 pooled scratch：

- 第三方 API 强制要求连续布局。
- 输入 stride/layout 与后端不兼容，需要 pack。
- 输出尺寸、类型或布局发生变化。
- 算法本身需要随输入规模增长的临时缓冲。

这类拷贝必须在实现中可见，不能隐藏在普通 `as_*` helper 里。

## 非连续 View 与第三方后端

很多第三方计算库只接受连续内存，或者虽然接受 stride/leading dimension，但性能特征和限制各不相同。
KSpaceJet 的规则是：**业务层只传 View，不在业务代码里手写 pack/fallback；连续性判断、pack scratch 和后端选择
都属于 numerics backend/detail/policy 的责任。**

处理顺序如下：

1. **输入和输出都是后端支持的布局**
   - 直接借用调用方内存进入最快后端。
   - 例如连续 `Pooled*::view()`、OpenCV 可接受的 `step`、Eigen strided map、MKL 支持的 stride/leading
     dimension。

2. **View 不连续，但存在可靠的 stride-aware 后端**
   - 优先使用 stride-aware 后端或 reference loop。
   - 典型场景是 ROI、row/col view、regular stride view 的轻量逐元素操作。
   - 不要为了简单的 `fill`、`scale`、`axpy`、`abs`、`conj`、`sum` 等 O(n) primitive 盲目 pack，因为
     copy 成本可能高于计算本身。

3. **后端要求连续，且计算足够重**
   - 可以在 backend implementation 中 pack 到连续 `Pooled*` scratch，再调用高性能后端。
   - 典型场景是 FFT、SVD/eigen decomposition、large GEMM、large convolution、NUFFT 等高算术强度或后端
     优势明显的计算。
   - pack 必须在 detail/backend 代码中可见，不能藏在 `as_eigen()`、`as_opencv()` 等普通 adapter helper
     里。
   - 统一使用 `ksj::array::pack_contiguous(input_view, scratch)`。该函数在输入已经连续时只返回借用 View，
     不触碰 scratch；输入不连续时才 resize/copy 到调用方提供的 `Pooled*` scratch。

4. **输出不连续且后端不能写 strided output**
   - 可以先写连续 scratch，再 scatter/copy 回输出 View。
   - 这会多一次写回，应只在 benchmark 或后端限制证明合理时使用。

5. **重复调用或循环热路径**
   - 不要每次调用都隐藏分配 scratch。
   - 优先提供 plan/workspace API，或让调用方在循环外创建 `Pooled*` scratch 并传入。
   - OpenMP 或多线程场景中的 scratch 必须是线程私有，不能用全局/static 共享缓冲。

简单决策表：

| 情况 | 默认选择 |
| --- | --- |
| contiguous View + 高性能后端可用 | 直接调用后端 |
| strided View + 后端支持 stride | 直接调用 stride-aware 后端 |
| strided View + 轻量 O(n) primitive | reference/eigen/手写 stride-aware fallback |
| strided View + 重型 kernel 且后端只吃连续内存 | backend detail 内 pack 到 Pooled scratch |
| output strided 且后端只写连续 | backend detail 内连续 scratch 后 scatter |
| 热循环重复调用 | 显式 workspace/plan 或循环外复用 scratch |

如果 pack 与 fallback 的性能胜负不明确，必须补 benchmark。benchmark 至少比较：

- contiguous Pooled view。
- 非连续 ROI/subview。
- pack + 第三方后端。
- stride-aware reference/Eigen/backend fallback。
- 单次调用和循环复用 workspace 两种情况。

只有 benchmark 证明 pack 后端在目标尺寸和类型上有稳定收益时，才能把它放进默认 policy。否则保留更简单的
stride-aware fallback，把 pack 后端作为候选或特定尺寸路径。

## Dispatch Policy 结构

`libs/numerics` 下每个能力模块都必须采用同一套 backend dispatch 结构，即使当前只有一个正式后端：

- umbrella header：`include/kspacejet/<module>/<module>.hpp`，只 include 本模块所有需要对外的功能头，不承载实现。
- public feature headers：`include/kspacejet/<module>/<feature>.hpp`，按数学功能划分，例如 `moments.hpp`、
  `reductions.hpp`、`thresholds.hpp`、`error_metrics.hpp`。这些头只暴露稳定数学语义、View/Pooled API、
  必要枚举和轻量转发，不包含第三方后端头。
- backend declarations：按 backend 和功能分目录放在
  `include/kspacejet/<module>/detail/<backend>/<backend>_<module>_<feature>.hpp`。这些头只放后端函数声明、
  轻量 support trait 和 forward declaration，不 include Eigen/OpenCV/ITK/MKL/IPP/FFTW/BART 等第三方头。
- backend implementations：按 backend 和功能分目录放在
  `src/<backend>/<backend>_<module>_<feature>.cpp`，并只在这里 include 第三方头。例如
  `src/eigen/eigen_linalg_blas.cpp`、`src/intel/intel_linalg_solvers.cpp`、
  `src/opencv/opencv_image_resize.cpp`、`src/fftw/fftw_fft_transform.cpp`。
- policy detail：`include/kspacejet/<module>/detail/<module>_policy.hpp`，统一保存阈值、backend eligibility 和
  `prefer_*` 判定函数。
- benchmark：`tests/benchmarks/kspacejet-<module>/`，至少能单独比较 public API、reference backend 和候选 backend。

public facade 不能直接把“选择哪个后端”的逻辑散落在业务代码或调用方。正确形态是：

```cpp
if (detail::prefer_intel_foo<T>(input.size()) && detail::intel::foo(input, output)) {
  return;
}

detail::eigen::foo(input, output);
```

如果某个模块当前只有 Eigen/reference backend，也仍然应保留 `<module>_policy.hpp`，让 public facade 通过
policy 进入当前 backend，再以同一个 backend 作为 reference fallback。这样后续新增 Intel、OpenCV、ITK、
FFTW、BART 或自研 SIMD candidate 时，只需要增加 detail backend 和 policy 阈值，不改变 public API。

policy 阈值可以是编译期常量，但选择发生在运行时调用路径上，必须综合输入类型、shape、size、stride、是否
contiguous、后端可用性和 benchmark 结果。不能通过配置文件或环境变量在生产运行中随意改后端策略。

### Backend 目录和功能拆分

每个 numerics 模块都按两个维度组织后端实现：

1. backend 维度：`eigen/`、`intel/`、`opencv/`、`itk/`、`fftw/`、`mkl/`、`bart/` 等。
2. feature 维度：按数学功能拆分，例如 `blas`、`solvers`、`decompositions`、`whitening`、`resize`、
   `morphology`、`transforms`、`thresholds`。

`kspacejet-linalg` 的目标结构如下，其它模块迁移时按同一范式套用：

```text
include/kspacejet/linalg/
  linalg.hpp
  blas.hpp
  solvers.hpp
  decompositions.hpp
  whitening.hpp
  types.hpp
  workspace.hpp
  detail/
    linalg_types.hpp          # 可选：内部 concepts / supported scalar rules
    linalg_policy.hpp
    eigen/
      eigen_linalg_blas.hpp
      eigen_linalg_solvers.hpp
      eigen_linalg_decompositions.hpp
      eigen_linalg_whitening.hpp
    intel/
      intel_linalg_blas.hpp
      intel_linalg_solvers.hpp
      intel_linalg_decompositions.hpp
      intel_linalg_whitening.hpp
src/
  eigen/
    eigen_linalg_blas.cpp
    eigen_linalg_solvers.cpp
    eigen_linalg_decompositions.cpp
    eigen_linalg_whitening.cpp
  intel/
    intel_linalg_blas.cpp
    intel_linalg_solvers.cpp
    intel_linalg_decompositions.cpp
    intel_linalg_whitening.cpp
```

规则：

- public feature header 只 include 自己需要的 backend declaration header。例如 `blas.hpp` include
  `detail/eigen/eigen_linalg_blas.hpp` 和 `detail/intel/intel_linalg_blas.hpp`，不 include solvers 或
  decompositions backend 声明。
- backend declaration header 必须以 KSpaceJet 类型声明 concrete backend symbols。不要把 Eigen/MKL/OpenCV/ITK
  表达式模板留给使用者实例化。
- public wrapper 必须用 concept 或 `static_assert` 限定该功能支持的 scalar/layout 类型，让 unsupported
  `T` 在编译期给出清晰错误；不能依赖链接期 undefined symbol 暴露问题。
- 避免用一个巨大的 `COMMON_WRAPPERS` 或 `COMMON_DECLS` 宏横跨整个模块。确实需要减少重复时，只能使用
  小范围、单 feature 的 helper 宏，且 declaration 和 implementation 要在同一功能边界内同步。
- backend `.cpp` 可以有模板实现和 concrete wrapper，但 wrapper 应按 feature 文件拆小。不要把整个模块的
  Eigen/MKL 实现实例化塞进一个超大 translation unit。
- 如果模块规模很小，仍应保留 backend 目录；可以暂时只有一个 feature 文件，但不要回到所有 backend/feature
  混在 `src/<backend>_<module>.cpp` 的形态。

### kspacejet-stats 迁移模板

迁移其它 numerics 模块时，以当前 `kspacejet-stats` 结构作为默认模板：

```text
include/kspacejet/stats/stats.hpp                  # umbrella，只 include 对外功能头
include/kspacejet/stats/moments.hpp                # mean / variance / covariance
include/kspacejet/stats/reductions.hpp             # sum / sum_abs / extrema / pair_sum
include/kspacejet/stats/norms.hpp                  # sum_of_squares / rss / l2
include/kspacejet/stats/error_metrics.hpp          # rmse / equal
include/kspacejet/stats/thresholds.hpp             # otsu_threshold
include/kspacejet/stats/fitting.hpp                # linear_fit API 和必要 public accumulator
include/kspacejet/stats/detail/stats_policy.hpp    # backend policy / 阈值 / eligibility
include/kspacejet/stats/detail/eigen/eigen_stats_moments.hpp    # Eigen backend 声明，不 include Eigen
include/kspacejet/stats/detail/intel/intel_stats_reductions.hpp # IPP backend 声明，不 include ipp.h
src/eigen/eigen_stats_moments.cpp                         # include Eigen/Core，完整 Eigen 实现
src/intel/intel_stats_reductions.cpp                      # include ipp.h，完整 IPP 实现
```

模板规则：

- public feature header 负责参数校验、policy 分发和 Pooled/View overload 转发；批量计算实现放在 backend
  `.cpp` 中。
- public wrapper/dispatch 必须在选择 backend 前完成面向用户的语义前置条件校验，包括 size/shape mismatch、
  unsupported axis、输出维度、空输入规则、枚举/参数取值范围等。backend implementation 不再重复这些校验，也
  不应抛这类语义异常。
- backend implementation 只保留后端能力和调用安全 guard，例如 contiguous/layout 要求、vendor length 上限、
  支持的 scalar/storage 类型、alias/in-place 约束、第三方返回状态等。作为 policy 候选的 backend 不满足 guard
  时返回 `false`，由 dispatch fallback 或 public API 统一报错。
- Eigen 是基础通用后端，`eigen_*.cpp` 应覆盖该模块的基础通用计算；IPP/MKL/OpenCV/ITK/FFTW/BART
  等是扩展或加速后端，不能替代 Eigen 基线，也不能强迫调用者感知第三方库。
- `detail/<backend>/<backend>_<module>_<feature>.hpp` 只声明后端函数。不要在这些头里写 Eigen
  expression、大段循环、IPP 调用或 OpenCV/ITK 类型适配。
- 不要创建 `generic_*.hpp`、`reference_*.hpp` 之类伪后端来承载“顺手写的循环”。如果一段实现是
  基础通用后端的一部分，放进 `eigen_*.cpp`；如果只是某个 public API 的状态对象或小标量工具，放在
  对应功能头并保持最小化。
- public API 返回类型使用 `kspacejet-array` 的基础 traits，例如 `ksj::array::reduction_result_t<T>` 和
  `ksj::array::magnitude_result_t<T>`。不要在上层模块重复创建 `result_types.hpp`、`stats_traits.hpp` 这类
  模块私有类型规则。
- CMake 中第三方后端依赖必须是 `PRIVATE`。模块 public link 默认只暴露 `KSpaceJet::array` 和真正稳定的 KSpaceJet
  public dependency。
- 迁移一个模块时，先整理 header 入口和 backend 边界，再移动实现；每一步都跑该模块 unit test，并按影响
  范围补 ISMRMRD reader 测试或相关 benchmark。

## 性能与后端选择

`libs/numerics` 的默认目标是高吞吐、低拷贝和可预测内存行为。接口设计不能只追求易写；任何会进入热路径的
primitive 都必须能被 benchmark 验证。

基础规则：

- View/Pooled 的 primitive 在进入热循环前完成尺寸和形状校验，热循环内部优先使用 unchecked `operator[]`
  或后端原生 view，避免每个元素重复边界检查。
- `for_each`、`transform`、`accumulate` 这类通用循环只是 reference implementation，不等价于最终最快实现。
- 编译器是否能自动 SIMD 化取决于循环形态、alias、stride、函数调用和数学函数。不能假设 `std::abs`、
  `std::arg`、lambda 或非连续 View 一定会自动向量化。
- 不因为“看起来更高级”就默认选择 Eigen/OpenCV/IPP/MKL 后端；也不因为手写循环简单就默认保留手写循环。
  后端选择必须通过 `tests/benchmarks` 中对应 benchmark 的数据决定。
- 第三方 backend 只有在关键尺寸、关键类型和目标机器上表现出稳定且有意义的收益时，才应该进入默认
  policy。若收益只是几个纳秒、接近 benchmark 噪声、只在少数尺寸成立，或 public API 总收益被调用开销抵消，
  应保留更简单的 Eigen/reference 路径，把第三方实现只作为 benchmark candidate。IPP/MKL/OpenCV/ITK 等
  backend 的维护成本高于普通 Eigen/reference 代码，不能因为微小差距增加默认路径复杂度。
- benchmark 必须把“返回 Pooled 带来的分配成本”和“预分配 output 的计算核成本”分开。用于后端决策的是
  output 版本的计算核结果；返回 Pooled 的便捷接口只用于评估调用便利性的额外成本。
- 对返回值便捷接口的性能判断要看调用上下文：单次调用主要看是否多分配；热循环还要看是否失去 output/workspace
  复用，以及链式调用是否引入中间对象。
- 根据 benchmark 引入 backend policy 时，阈值必须保守，并在注释或 README 中说明对应 benchmark case。
  例如小尺寸可能手写 loop 更快，大尺寸连续内存可能 Eigen/IPP/MKL 更快。

新增或调整计算后端时，至少检查：

- contiguous Pooled view。
- ROI/subview 或非连续 View。
- float/double 以及必要的 complex float/complex double。
- 小尺寸和大尺寸两端，避免只优化单一规模。

## Layout 迁移

KSpaceJet 新的 numerics owning 对象默认按 row-major 语义组织，业务层不应继续为了兼容历史 column-major 容器而
无条件 pack/unpack 数据。删除 layout copy 前必须先判断这次拷贝的真实目的：

- 如果拷贝只是在 row-major owner、row-major view 或 row-major 后端之间搬运数据，优先删除，改为 View
  借用内存。
- 如果拷贝是在调用第三方后端前进行 pack，必须确认该后端是否真的要求连续内存、特定 stride 或特定
  leading dimension。
- 如果拷贝来自旧 Armadillo/Eigen column-major 语义，不能只按内存连续性删除；必须确认算法的线性索引、
  slice 顺序、row/column loop 顺序和输出坐标语义。

以下区域属于高风险遗留热点，column-major 假设已经混入线性索引或算法分支，不能做机械替换：

- Spectroscopy 频谱数据重排和复数向量处理。
- Shim / field-mapping 中的矩阵展开、拟合和发送路径。
- Noncartesian radial GRASP / NUFFT 周边的 frame、coil、slice 展开。

迁移这些区域时必须按算法分支准备 golden case，至少覆盖非方阵、非对称维度、multi-slice/multi-coil 和
ROI/subview 情况。只有 golden case 验证通过后，才能删除历史 layout copy 或改变线性索引公式。

## 例外

- 纯创建型函数只返回 Pooled，例如 `window(size)`、`zeros(rows, cols)`。
- 标量、小固定大小对象可以使用栈类型，例如 `Vector3f`、`Matrix3d`。
- 稀疏矩阵这类 owning 数据结构可以暴露自己的 owner 类型；相关 dense 输入输出仍遵守 View/Pooled 规则。
- Legacy 迁移中临时保留的 Pooled-only API 必须视为待迁移项，新代码不得仿照。

## 迁移检查清单

修改或新增 numerics API 时检查：

- 算法核心是否使用 View 输入输出。
- Pooled overload 是否只创建输出或 `.view()` 转发。
- 返回值重载是否只作为便捷层存在；热路径是否仍保留 output-param、workspace、plan 或 fused primitive。
- 是否避免了无意义的 `std::vector` 大块数值存储。
- 是否避免了隐藏共享 workspace、`static` scratch 或非线程安全 mutable cache。
- 是否避免了业务层直接使用 Eigen/OpenCV/ITK/MKL/IPP 类型。
- 非连续 View 进入第三方后端时，pack/fallback 是否只发生在 numerics backend/detail/policy 中，而不是业务层。
- pack 到连续 scratch 是否有明确后端限制或 benchmark 依据；轻量 O(n) primitive 是否优先保留 stride-aware
  fallback。
- 输入输出是否默认分离；原地修改是否使用 `_in_place`。
- 是否覆盖了非连续 view 或 ROI 的单元测试。
