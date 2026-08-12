# kspacejet-image

`kspacejet-image` 负责通用图像数值核。当前落地能力聚焦于 `PooledImage<T>` 上的 threshold、
min-max normalization、padding、crop/ROI copy、nearest/bilinear/cubic/area/lanczos4 resize、connected components、
box/gaussian/bilateral/median filter、Sobel/gradient/Laplacian edge operators、unsharp mask、region grow mask
和基础 morphology。segmentation、更多高级 filter variants 仍属于后续规划。MRI 专属参数编排和业务解释应留在
`libs/mri`。

当前已提供：

- `threshold(input, threshold, low, high)`
- `normalize_minmax(input)`
- `pad(input, top, bottom, left, right, mode, constant)`
- `crop(input, row, col, rows, cols)` / `crop(input, output, row, col)`
- `center_crop(input, rows, cols)` / `center_crop(input, output)`
- `copy_roi(source, source_row, source_col, destination, destination_row, destination_col, rows, cols)`
- `connected_components(input, connectivity)` / `connected_components(input, labels, stats, connectivity)`
- `region_grow(input, seed_row, seed_col, low, high, connectivity)` /
  `region_grow(input, mask, seed_row, seed_col, low, high, connectivity)`
- `resize(input, rows, cols, method)` / `resize(input, output, method)`
- `resize_nearest(input, rows, cols)` / `resize_nearest(input, output)`
- `resize_linear(input, rows, cols)` / `resize_linear(input, output)`
- `resize_cubic(input, rows, cols)` / `resize_cubic(input, output)`
- `resize_area(input, rows, cols)` / `resize_area(input, output)`
- `resize_lanczos4(input, rows, cols)` / `resize_lanczos4(input, output)`
- `box_filter(input, kernel, border)` / `box_filter(input, output, kernel, border)`
- `gaussian_blur(input, kernel, sigma, border)` / `gaussian_blur(input, output, kernel, sigma, border)`
- `bilateral_filter(input, diameter, sigma_color, sigma_space, border)` /
  `bilateral_filter(input, output, diameter, sigma_color, sigma_space, border)`
- `median_filter(input, kernel, border)` / `median_filter(input, output, kernel, border)`
- `sobel_x(input, border)` / `sobel_x(input, output, border)`
- `sobel_y(input, border)` / `sobel_y(input, output, border)`
- `gradient_magnitude(input, border)` / `gradient_magnitude(input, output, border)`
- `laplacian(input, border)` / `laplacian(input, output, border)`
- `unsharp_mask(input, amount, kernel, sigma, border)` /
  `unsharp_mask(input, output, amount, kernel, sigma, border)`
- `dilate(input, kernel, border)` / `dilate(input, output, kernel, border)`
- `erode(input, kernel, border)` / `erode(input, output, kernel, border)`
- `morph_open(input, kernel, border)` / `morph_open(input, output, kernel, border)`
- `morph_close(input, kernel, border)` / `morph_close(input, output, kernel, border)`

公开接口使用 `ksj::array::PooledImage<T>`。`PooledImage` 是行主序二维图像对象，
更贴合 OpenCV/IPP 的 row-major image buffer 和 row stride 模型。

当前后端结构：

- Eigen backend：默认基线实现，覆盖 float/double。
- Intel IPP backend：当前接入 `threshold` 和 `normalize_minmax` 的 float fast path 候选。
- OpenCV backend：当前接入 `threshold`、`normalize_minmax`、nearest/linear/cubic/area/lanczos4 resize、
  `box_filter`、`gaussian_blur`、float `bilateral_filter`、float `median_filter`、connected components、
  Sobel/gradient 和 morphology 的候选。

公开 API 通过 `detail::ImageDispatchPolicy` 选择后端。当前 image benchmark 显示：

- `box_filter_5x5` 和 `morph_close_5x5` 从 `64x64` 起 OpenCV 稳定领先，因此 float/double 在
  `4096` pixels 起切 OpenCV。
- `gaussian_blur_5x5` 从 `64x64` 起 OpenCV 稳定领先，因此 float/double 在 `4096` pixels 起切
  OpenCV。
- `bilateral_filter_5x5` 使用圆形邻域 reference path 对齐 OpenCV bilateral semantics；`20260616-162626`
  sweep 显示 float 在 `32x32..128x128` 由 OpenCV 明显领先，`20260616-162727` validation sweep
  确认 public/output-buffer policy 从 `1024` pixels 起切 OpenCV。double 当前保持 Eigen/reference path。
- `median_filter_3x3` 的 float 从 `64x64` 起 OpenCV 稳定领先，因此 float 在 `4096` pixels 起切
  OpenCV；double 当前保持 Eigen/reference path。
- `sobel_x_3x3` / `sobel_y_3x3` / `gradient_magnitude_3x3` 从 `64x64` 起 OpenCV 稳定领先，因此
  float/double 在 `4096` pixels 起切 OpenCV。`gradient_magnitude` 的 Eigen fallback 使用 fused
  3x3 reference path，benchmark 中比 two-pass Sobel 合成更快。
- `laplacian_3x3` 和 `unsharp_mask_5x5` 当前保持 Eigen/reference path；`20260616-103601`
  smoke benchmark 覆盖了 eigen/output-buffer/public API，其中 `unsharp_mask_5x5` 的 output-buffer/eigen_output-buffer
  path 在短 sweep 中领先。
- `resize_linear_half` 的 float 从 `64x64` 起 OpenCV 更快；double 在 `64x64` 到 `256x256`
  更快，但 `512x512` 回到 Eigen/output-buffer 更快，因此 double resize policy 带有上界。
- `resize_nearest_half` 和 `resize_cubic_half` 从 `64x64` 起 OpenCV 明显领先，因此 float/double 在
  `4096` pixels 起切 OpenCV。cubic reference path 使用 OpenCV 兼容的 `a=-0.75` cubic kernel。
- `resize_area_half` 使用精确面积覆盖作为 reference path，OpenCV `INTER_AREA` 作为 float/double 候选；
  `20260616-133427` sweep 显示 `32x32..512x512` 均由 OpenCV/public output-buffer 路径领先，因此非空
  float/double area resize 直接优先尝试 OpenCV。
- `resize_lanczos4_half` 使用归一化 4-lobe windowed-sinc reference path 和 replicate 边界，OpenCV
  `INTER_LANCZOS4` 作为 float/double 候选；`20260616-153400` sweep 显示 `32x32..512x512` 均由
  OpenCV 明显领先，`20260616-153455` 小尺寸 sweep 显示 `8x8` 起 OpenCV 仍领先；`20260616-153549`
  validation sweep 后已反写 policy，float/double 从 `64` pixels 起切 OpenCV。
- `crop_center_half` 保持 Eigen/reference path；`center_crop` 可复用输出缓冲，适合热路径。
- `connected_components_8` 的 double 从 `32x32` 起 OpenCV 更快，float 在 `64x64` 附近有小幅波动、
  从 `128x128` 起 OpenCV 明显更快，因此 double 在 `1024` pixels 起切 OpenCV，float 在 `16384`
  pixels 起切 OpenCV。
- `region_grow_8` 当前保持 Eigen/reference path；`20260616-102647` smoke benchmark 覆盖了
  output-buffer/public API，短 sweep 不反写后端 policy。
- `pad` 继续保持 Eigen/reference path。

生产环境需要按 `docs/conventions/benchmark.md` 运行完整 sweep，再根据报告调整 policy 阈值。
