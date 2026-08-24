# KSpaceJet

KSpaceJet 是预发布的开源 C++20 MRI 重建框架。它以
[ISMRMRD](https://ismrmrd.github.io/) 作为唯一原始采集数据语义；当前可执行的参考
路径读取标准 ISMRMRD HDF5。框架不定义私有 wire protocol，也不包含旧回放格式、私有
操作队列或任何专有重建算法。

当前工作项、阶段进度、验收证据和外部阻塞项均在唯一的
[KSpaceJet 主实施计划](docs/architecture/KSpaceJet_project_plan_and_acceptance.md) 中追踪。
用户已确定 ksj-gateway 将成为唯一的外部集成边界；其候选稳定设计见
[网关架构](docs/architecture/KSpaceJet_gateway_architecture.md)。当前可执行文件仍是 scaffold，
尚未提供 listener、TLS、认证、公开 profile 或在线服务能力。

## 必需的双仓库数据工作区

KSpaceJet **不保存原始 MRI 重建数据**（包括 `.mrd`、`.h5`、`.hdf5` 和
`.ismrmrd` payload）。原始数据、其来源、许可、校验和与 Git-LFS 管理只属于
[KSpaceJet-ismrmrd-data](https://github.com/isqiwen/KSpaceJet-ismrmrd-data)。开发时两个
仓库必须是同级目录：

```text
<workspace>/
  KSpaceJet/
  KSpaceJet-ismrmrd-data/
```

例如，在同一个父目录中分别 clone 两个仓库；不要把数据仓库作为 submodule、复制品或
KSpaceJet 内的 symlink。数据相关命令应显式传入 sibling 数据仓库下的路径。完成 bootstrap
后，可运行以下离线检查；Linux/Windows pre-commit 也会自动执行它：

```bash
just workspace-check
```

该检查只验证同级布局、数据仓库 identity/结构和 KSpaceJet 中不存在 raw payload；它不下载
或校验数据内容。需要完整校验数据仓库时，在 sibling 仓库中运行
`bash tools/verify-data.sh`。

## 设计重点

- 流式读取 ISMRMRD acquisition，回调内以零额外复制的 `std::span` 暴露样本和轨迹。
- 使用 `KSpaceJet::` CMake target、`ksj` C++ namespace 和 `kspacejet/` public
  include 根；项目尚未发布，API、ABI 和安装 surface 均可演进。
- 使用 Eigen 作为可移植数值基线，并可通过仓库内 Intel IPP/MKL/OpenMP payload
  加速；使用者不必安装 oneAPI。
- 提供 Linux x86_64 与 Windows x86_64/MSVC 的构建配置；当前仅 Linux toolchain、
  build 和 install 已有验证证据，Windows 验证仍待实际 Windows 主机完成。

## 主要模块

- `libs/io/kspacejet-ismrmrd`：ISMRMRD 流式输入 facade，target `KSpaceJet::ismrmrd`。
- `libs/numerics`：数组、FFT、线性代数、图像、稀疏和优化原语。
- `libs/core`：内存、线程、日志、平台和进程基础设施。
- `libs/mri/kspacejet-mri-debug`：与具体重建 provider 无关的 MRI 数据诊断工具。
- `apps`：统一命名的五个应用工程：`ksj`、`ksj-gateway`、`ksj-recon`、
  `ksj-research` 和 `ksj-viewer`；五者均默认构建并安装。当前 `ksj` 提供 pipeline validation 和
  Provider scaffold 工具，`ksj-recon` 提供离线 HDF5 reference route；`ksj-gateway`
  是计划中的唯一外部集成入口、当前仍为未实现 scaffold，`ksj-research` 也仍为 scaffold。具体边界见
  [apps/README.md](apps/README.md)。

## 构建

### 首次准备开发工具

先准备项目级开发工具环境，再使用 VS Code 或命令行构建。Linux x86_64 的宿主机需要
Git、Git LFS，以及默认的 GCC/G++ 14；Linux bootstrap 会通过 `apt` 确保安装 `just`。Windows x86_64
的宿主机需要 Git、Git LFS、Visual Studio 2022 的 v143 C++ 工具和 Windows SDK；若未安装 `just`，Windows
bootstrap 会通过 `winget` 安装它。这些与操作系统/SDK 集成的工具保留在
宿主机上；Conan、CMake、Ninja、`clang-format` 和 `cmake-format` 由固定版本的 `uv` 环境
安装到本仓库，不会修改全局 Python 或 shell `PATH`。

Linux 首次 bootstrap 还需要普通的宿主下载/解包工具：`curl` 或 `wget`、`tar`、`sha256sum` 或
`shasum`。Linux 的 VS Code 调试另需宿主 `gdb`；仅 `linux-release-static-analysis` 需要宿主
`clang++`。这些都不影响普通构建。

Linux：

```bash
bash tools/devenv/linux/bootstrap.sh
```

Windows PowerShell：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1
```

完成 bootstrap 后，日常命令直接经过根 `justfile`，Linux 与 Windows 使用相同的 recipe 名称：

```bash
just prepare-release
just build-release-applications
just install-release-applications
just check
just workspace-check
```

```powershell
just prepare-release
just build-release-applications
just install-release-applications
just check
just workspace-check
```

`prepare-debug` / `prepare-release` 自动选择对应的 Conan profile、Intel payload 验证与 CMake preset；
`build-*-applications` 和 `install-*-applications` 保持纯增量，不会隐式重新 prepare。使用
`just --list` 查看所有 recipes。对尚未提供 recipe 的低层诊断，仍可通过 platform runner
直接调用项目锁定的工具；`just` 是宿主机工具，`conan`、`cmake` 或格式化工具仍不得依赖系统 PATH。
完整的工具归属、离线验证和维护规则见
[开发环境说明](tools/devenv/README.md)。

Intel payload 位于 `third_party/intel/payload/`，由 Git LFS 管理。发布前执行
`git lfs pull`，以获得 Linux 和 Windows 运行时文件。

标准 Conan profile 为所有提供 `shared` 选项的依赖固定使用动态库；Linux 同时启用
PIC。不要在 KSpaceJet 构建中以 `*:shared=False` 覆盖此策略。

### VS Code 工作流

在 VS Code 中，也可以通过 **Tasks: Run Task** 先运行可见的
`KSJ: bootstrap developer environment` 任务；它会按当前平台调用同一 bootstrap。完成 bootstrap
后，首次使用某个“平台 + 配置”组合时，再运行对应的准备任务：

- `KSJ: prepare Linux Debug environment`
- `KSJ: prepare Linux Release environment`
- `KSJ: prepare Windows Debug environment`
- `KSJ: prepare Windows Release environment`

准备任务调用与命令行相同的项目 bootstrap：它先确保 Intel Git-LFS payload 完整、全量校验 manifest，
再导出仓库内的 Conan recipe、执行 `conan install`，并运行 CMake configure，从而生成该配置的
`conan_toolchain.cmake` 和构建系统。仅在首次使用、对应 `out/build/`
目录被删除，或依赖、本地 Conan recipe/Intel payload、CMake 配置或 Conan profile 发生变化时，
才需要重新运行准备任务。

日常修改 C++ 源码后，运行对应的 `KSJ: build … applications` 任务即可。它只执行 CMake
增量构建，并构建五个可执行程序：`ksj`、`ksj-gateway`、`ksj-recon`、`ksj-research` 和
`ksj-viewer`；不会
重新 export、下载/解析 Conan 依赖或 configure。F5 的调试前构建也遵循同一规则，不会自动准备
环境。若尚未准备该配置，先运行匹配的 `KSJ: prepare … environment`。

要安装五个已构建的程序，运行对应的可见安装任务。每个安装任务只依赖同一平台和配置的
`KSJ: build … applications` 增量构建任务，不会运行 prepare、Conan export/install 或 CMake
configure：

| 平台与配置 | VS Code 安装任务 | CMake install preset | 安装目录 |
| --- | --- | --- | --- |
| Linux Debug | `KSJ: install Linux Debug applications` | `linux-debug-install` | `out/install/linux-debug` |
| Linux Release | `KSJ: install Linux Release applications` | `linux-release-install` | `out/install/linux-release` |
| Windows Debug | `KSJ: install Windows Debug applications` | `windows-vs2022-debug-install` | `out/install/windows-vs2022-debug` |
| Windows Release | `KSJ: install Windows Release applications` | `windows-vs2022-release-install` | `out/install/windows-vs2022-release` |

安装前缀由匹配的 CMake configure preset 的 `CMAKE_INSTALL_PREFIX` 决定；不要将安装任务
用作环境准备的替代步骤。

`ksj-research` 是随应用安装的研究工具 scaffold；其操作尚未实现，且不得成为其他应用的
runtime/data-plane 依赖。`KSJ_BUILD_RESEARCH` 仍只控制 `tests/research` 中的测试和实验
target，不控制这个可执行程序。

## Provider 输入边界

当前离线 reference runtime 从 HDF5 source 读取并将 host-owned `AcquisitionFrame` 传给
Provider plugin。底层 reader 的样本布局为 `sample + channel * number_of_samples`；trajectory
布局为 `sample * trajectory_dimensions + dimension`。借用的 reader view 在 callback 返回后
失效；runtime 会在异步调用 Provider 前将 frame materialize 到 host-managed buffer。plugin
不得自行打开 source、保留借用 view 或创建未记账的 buffer pool。

旧私有采集格式和队列格式不属于 KSpaceJet，也没有兼容开关。
