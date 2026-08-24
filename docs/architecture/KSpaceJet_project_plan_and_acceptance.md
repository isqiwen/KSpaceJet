# KSpaceJet — Codex 主实施计划、功能规范与验收标准

> 文档性质：**唯一主实施规范（Single Source of Implementation Truth）**
> 状态权威：第 12 节唯一执行台账；顶部总览只是可再生成的只读投影
> 适用仓库：`isqiwen/KSpaceJet`
> 文档状态：ACTIVE
> 版本：0.5
> 建立日期：2026-08-19
> 基线分支与提交：main / `8c31b30419ed330688b3f1b90f14a4498503317d`
> 项目阶段：Pre-release；接口、ABI、schema 和源代码布局均可直接演进，不保留兼容层，除非当前任务明确要求。

这是 KSpaceJet 的单一执行总规范。它把项目计划、功能需求、非功能需求、验收标准、工作项依赖、当前进度、证据和决策记录放在同一个文件中，供 Codex 在每次开发工作开始和结束时持续使用。

---

## 0. Codex 使用本文件的强制流程

每个 Codex 实例必须把本节当作执行协议，而不是阅读材料。

1. 先阅读仓库根目录 AGENTS.md，再完整阅读本文件的第 0、1、11、12、13 节；先查看第 0.4 节快速总览，再以第 12 节的逐项状态为准。
2. 执行 Git 状态检查；识别用户已有改动，绝不覆盖、回退或混入无关改动。
3. 若第 12 节已有唯一 `IN_PROGRESS` 或 `VERIFYING` 项，先恢复它；否则选择**唯一一个**状态为 READY 的最小工作项。若没有 READY 项，则按依赖关系找出第一个可解锁项或保留精确 BLOCKED 记录；不要并行改动两个相互耦合的工作项。
4. 阅读该工作项列出的权威文档、代码路径、测试和前置条件。若事实与本文件不一致，先完成 P0-001 的基线复核并更新本文件，而不是基于猜测写代码。
5. 只实现该工作项的范围。新增的接口、配置、Provider 或工具必须同时具有对应测试和文档；不得用临时兼容层掩盖未完成设计。
6. 运行该工作项规定的最小验证，以及受改动影响的扩展验证。若环境无法运行，记录准确阻塞原因、已尝试命令和缺失前提，状态改为 BLOCKED；不得写 ACCEPTED。
7. 在同一次变更中更新第 12 节的状态、证据和下一步；随后用 `tools/checks/check_execution_plan.py --write` 重建第 0.4 节，并用 `--check` 验证投影一致。只有通过全部验收命令并记录证据后，才可把工作项置为 ACCEPTED。
8. 再次执行格式、差异和必要测试检查。向用户报告：完成项、证据、未覆盖的风险和下一项；不要声称整个阶段完成，除非阶段门禁全部通过。

### 0.1 工作项状态机

| 状态 | 含义 | 允许的下一状态 |
| --- | --- | --- |
| PLANNED | 已定义但其依赖尚未满足，或现有代码尚未接受证据审计 | READY、NOT_APPLICABLE、SUPERSEDED |
| READY | 前置条件已 ACCEPTED，可被当前 Codex 选中 | IN_PROGRESS、BLOCKED |
| IN_PROGRESS | 当前唯一正在实施的工作项 | VERIFYING、BLOCKED、READY |
| VERIFYING | 实现、测试、注册和文档已写入，正在执行完整验收 | ACCEPTED、BLOCKED、IN_PROGRESS |
| ACCEPTED | 所有要求、验收命令和证据均已满足 | REOPENED、SUPERSEDED |
| BLOCKED | 需要外部事实、硬件、数据、产品决策或缺失依赖 | READY、NOT_APPLICABLE、SUPERSEDED |
| NOT_APPLICABLE | 当前发布范围明确不启用，必须记录决定人和原因 | READY、SUPERSEDED |
| SUPERSEDED | 被新的、已链接的工作项取代 | 无 |
| REOPENED | 已 ACCEPTED 的事项发生回归、范围错误或证据失效 | IN_PROGRESS、BLOCKED |

禁止把“已有文件”“可编译”或“部分测试存在”当成 ACCEPTED。对于仓库已有的能力，先通过 P0-001 复核后才能提升状态。

### 0.2 证据记录格式

每个完成项在第 12 节必须至少记录以下信息；没有证据的完成项一律视为未完成。

| 字段 | 内容 |
| --- | --- |
| Commit 或工作树版本 | 可复现的 commit SHA；未提交时写明工作树状态 |
| 改动范围 | 主要文件和公开接口 |
| 需求映射 | 本文件的功能 ID 和验收 ID |
| 验证命令 | 实际执行的命令，不是计划命令 |
| 结果 | 成功、失败、跳过及原因 |
| 输出证据 | 测试名、报告、fixture、golden digest、trace 或日志路径 |
| 已知限制 | 未覆盖平台、性能边界、待决决策 |
| 下一项 | 下一条可执行工作项 ID |

不得伪造、概括或删除失败证据。若后续证据推翻一个 ACCEPTED 项，应把它置为 REOPENED 或 BLOCKED，并在变更日志说明原因。

### 0.3 自主性边界

Codex 可自行完成明确属于当前工作项的代码、测试、文档、格式化、局部重构和可逆配置修改。以下情况必须停止推进该工作项，记录 BLOCKED，并向用户提出一个精确问题：

- 需要在 P5-008/P5-009 已定义边界之外引入新的外部 Gateway Profile、Connector vendor adapter、scanner/acquisition control、私有协议或未批准的数据保留；
- 需要授权访问真实病人数据、生产扫描仪、凭据、商业 Provider 或专有算法；
- 需要为性能目标、硬件拓扑、临床用途、监管等级或发布兼容性作产品决定；
- 需要删除、覆盖或迁移用户未确认的重要数据；
- 发现两个权威文档在语义上冲突且无法由本文件的优先级规则消解。

构建失败、测试失败、缺少普通开发依赖、局部设计缺口和可由源码调查解决的问题不构成暂停理由；先诊断并在当前工作项内解决。

### 0.4 当前执行总览（由第 12 节派生；非状态权威）

本总览提供与主实施计划相同的快速进度入口：阶段覆盖度、当前工作项、READY/阻塞项和恢复方向。它**不得**发起状态转换或独立维护任务状态；运行 `tools/checks/check_execution_plan.py --write` 从第 12 节重建，`--check` 会拒绝任何漂移。

<!-- KSJ-PLAN-DASHBOARD:BEGIN -->

#### 执行进度总览（自动生成）

> 此区块是 [第 12 节唯一执行台账](#12-唯一执行台账) 的只读投影。修改任务状态、依赖或证据后，运行 `python3 tools/checks/check_execution_plan.py --write`；不要手工编辑本区块。
> 覆盖度 = `ACCEPTED / 适用项`，不按工作量加权，也不表示阶段门禁已经通过。

```yaml
source: "docs/architecture/KSpaceJet_project_plan_and_acceptance.md#12-唯一执行台账"
ledger_date: 2026-08-24
execution_state: IN_PROGRESS
active_phase: P8
active_work_item: P8-004
next_task: P8-004
ready_items: []
blocked_items:
  - P0-006
  - P0-007
  - P0-010
accepted: 15
applicable: 68
coverage: 22.1%
```

| 阶段 | 目标 | 已接受 | 适用项 | 覆盖度 | 状态分布 |
| --- | --- | ---: | ---: | ---: | --- |
| P0 | 规范、基线和工程治理 | 7 | 10 | 70% | ACCEPTED: 7 · BLOCKED: 3 |
| P1 | 可信离线 reference 基线 | 3 | 9 | 33.3% | ACCEPTED: 3 · PLANNED: 6 |
| P2 | 图、artifact、compiler、verifier 和 CLI 计划工具 | 1 | 7 | 14.3% | ACCEPTED: 1 · REOPENED: 1 · PLANNED: 5 |
| P3 | 有界 generic CPU runtime | 0 | 6 | 0% | PLANNED: 6 |
| P4 | Provider 产品化 | 0 | 6 | 0% | PLANNED: 6 |
| P5 | 外部集成网关与可选嵌入 ISMRMRD ingress | 1 | 12 | 8.3% | ACCEPTED: 1 · PLANNED: 11 · SUPERSEDED: 1 |
| P6 | 并行、NUMA、GPU 与性能 | 0 | 7 | 0% | PLANNED: 7 |
| P7 | Qualification、CI、安装、供应链和发布 | 0 | 7 | 0% | PLANNED: 7 |
| P8 | 离线可视化与检查工具 | 3 | 4 | 75% | ACCEPTED: 3 · IN_PROGRESS: 1 |

#### 最近验收证据（自动生成）

| 工作项 | 证据记录 |
| --- | --- |
| P8-003 | [13.22 P8-003 ACCEPTED 证据](#1322-p8-003-accepted-证据) |
| P8-002 | [13.21 P8-002 ACCEPTED 证据](#1321-p8-002-accepted-证据) |
| P8-001 | [13.20 P8-001 ACCEPTED 证据](#1320-p8-001-accepted-证据) |

#### 当前阻塞项（自动生成）

> 详细依赖、证据和限制以第 12 节为准；下表只显示其下一精确行动。

| 工作项 | 解锁后动作 |
| --- | --- |
| P0-006 | 收集第 6.3.1 所列 case、deployment、performance、data-governance、output、security/release、architecture 及 GWY-DEC-001 至 007 owner/source/scop… |
| P0-007 | 等待用户明确恢复 GitHub CI 工作；恢复后先只读复核 remote branch、PR、workflow run 与 `main` protection 的实际状态，再决定是否继续 P0-007。 |
| P0-010 | 等待用户明确恢复 Linux 验证；恢复后仅在 `kspacejet-linux-test` 中继续，先确认 LFS payload，再执行 `just prepare-release` 与 `just check`，不得写入当前 Windows worktre… |

<!-- KSJ-PLAN-DASHBOARD:END -->

---

## 1. 权威关系、范围和完成定义

### 1.1 权威关系

当文本冲突时，按以下顺序处理：

1. 用户在当前任务中的明确要求；
2. 仓库根目录 AGENTS.md 的产品边界和工程硬约束；
3. 本执行总规范的范围、任务状态和验收标准；
4. 当前受维护源码、schema、fixture 和测试所证明的实现/结构语义；它们不能扩张前三级的产品边界或验收状态；
5. 当前 README、CLI help 和配置说明；
6. 已明确标为历史/非规范性的 architecture、paper、注释和历史提交，仅可作为背景，不拥有当前规范权威。

若第 4 或第 5 项与前三项冲突，不能静默选择：收口实现/文档、建立或更新决策记录，并在第 12 节保留真实验收状态。历史记录中的任何表述均不得恢复已撤回的 scope、mode、artifact authority 或 capability claim。

### 1.2 项目愿景

KSpaceJet 是一个面向 MRI 重建的开源 C++20 框架。框架的责任是处理调用方提交的标准 ISMRMRD 数据，编译并执行显式类型化的重建图，管理可插拔 Provider，并生成可追溯的重建结果 artifact。

它不是私有扫描仪协议、专有算法仓库或历史系统兼容层。具体重建算法归独立 Provider 所有；框架提供可验证的运行时、数据所有权、资源账本、调度、工具、配置和交付边界。

### 1.3 不可违反的产品边界

| ID | 约束 |
| --- | --- |
| BND-001 | ISMRMRD 是唯一 MRI 数据交换语义和持久化 image artifact 格式：当前 reference 输入为标准 ISMRMRD HDF5；未来调用方提交的内存对象以及经已选公开 Gateway Profile 规范化的外部流必须具有相同的 ISMRMRD 语义；对外或持久化的重建图像只能使用标准 ISMRMRD HDF5 的 ImageHeader、image data 与 MetaAttributes。CLI JSON stdout 是命令控制结果，不是图像 artifact。 |
| BND-002 | KSpaceJet 不实现、不配置、不链接或不测试扫描仪、采集卡、FPGA、DMA、PCIe/QDMA、内核驱动、设备缓冲或厂商私有 acquisition protocol。ksj-gateway 只能终止已选公开 Gateway Profile 的 TLS/session，并且不得发明私有 ACK/credit/read-gating/pause/reconnect 协议。 |
| BND-003 | 不引入旧私有数据格式、旧队列表、BRF、ComQ、DPC 兼容层或专有重建算法。 |
| BND-004 | Provider 是独立动态库、信任和生命周期边界；算法不能泄漏到 host ABI entrypoint、CLI 或网关。 |
| BND-005 | gateway 拥有外部连接、协议 decode、认证/授权、session、网关资源与公开交付；runtime 仅拥有规范化输入后的 materialization、buffer、排队、顺序、execution admission 与 result artifact。两者都不是 Provider Operator，也不接管厂商采集职责。 |
| BND-006 | 所有长期异步数据必须由 host 管理、被资源账本计费且具有清晰所有权；借用的 AcquisitionView 不能跨回调保留。 |
| BND-007 | 项目未发布；默认直接替换旧形状，不增加兼容 alias、双格式、迁移 shim 或版本协商。 |
| BND-008 | 数值基线为 Eigen；Intel、FFTW、OpenCV、ITK、MATIO 等仅在私有实现边界出现，不暴露 vendor 类型。 |
| BND-009 | KSpaceJet 不保存原始 MRI 重建 payload。`.mrd`、`.h5`、`.hdf5`、`.ismrmrd` 原始数据及其 provenance/licence/checksum/Git-LFS policy 只属于同级 `KSpaceJet-ismrmrd-data` 仓库；开发 checkout 必须验证该双仓库布局，且不得以 project-internal raw directory、copy、symlink 或 submodule 代替它。 |

### 1.4 v1 完成定义

v1 不是“有一个能运行的 demo”。只有下列条件同时满足才可声明 v1 完成：

- P0 至 P4 全部 ACCEPTED，P6 中当前配置启用的能力全部 ACCEPTED，P7 的 qualification gate 通过；
- 一个公开、可复现的 Cartesian reference pipeline 可以从 HDF5 运行至最终输出，具有完整 run record、输入身份、Provider 身份、配置和结果证据；
- 一个公开、可复现的 non-Cartesian reference pipeline 同样成立；
- Generic PipelineDefinition 路径而非只靠专用 CLI 编排，能够完成 resolve、compile、independent verify、admit、execute、cancel、normal end 和 failure end；
- 动态 Provider SDK、loader、bundle identity 和最小第三方 conformance 流程可用；
- 已支持的 HDF5 和调用方提交的等价 ISMRMRD 输入在 runtime-frame 边界具有相同语义；
- 在声明的 CPU、NUMA、内存和可选 GPU target envelope 下，正确性、资源上界、稳定性和性能证据齐全；
- Linux 和 Windows 的支持范围、安装、CLI JSON 输出、文档、风险和已知限制均经过 release gate。
- 若声明 external-integration-gateway mode，则 P5-009 至 P5-013 及其适用 P7 gate 也必须 ACCEPTED；离线 v1 证据不能替代 gateway mode 资格。

---

## 2. 当前基线与诚实的成熟度判断

本节只记录截至基线提交可从仓库直接观察到的事实，**不代表任何模块已经正式验收**。

| 领域 | 已观察到的事实 | 当前结论 |
| --- | --- | --- |
| 工程基础 | C++20、CMake presets、Conan 2、Linux GCC 14 与 Windows MSVC 2022 profile、项目内 bootstrap 和 checks 已存在。 | 可作为 P0 环境基线，需复跑。 |
| 数值与基础库 | core、memory、threading、logging、platform、array、FFT、linalg、image、NUFFT 等库及大量 unit/benchmark target 已存在。 | 组件基础强，但每个生产路径仍需需求映射。 |
| 类型与 schema | types/registry.json、生成的 C++/C type registry、PipelineDefinition、ExecutionPlan、RunRecord 等 schema 已存在。 | P1/P2 的重要起点；需验证 schema、canonical JSON 与 runtime 一致。 |
| Provider 基础 | provider SDK、loader、catalog、contracts、calibration、Cartesian、non-Cartesian、coil-combine、image-ops、conditioning Provider 及测试存在。 | 可作为 reference Provider 基线；尚不能自动推导“第三方生产可用”。 |
| 图与运行时 | recon-model、recon-graph、synchronous graph compiler/verifier、FiringLease、bounded edge、resource ledger、frame slot、serial Cartesian 等实现和测试存在。 | 已具有 P1-P3 的部分实现，需要按 P0-001 建立已验证能力清单。 |
| 离线应用 | ksj-recon 提供 Cartesian RSS 与 non-Cartesian RSS HDF5 命令；ksj 提供 pipeline validate 和 provider init。 | 有 reference vertical slices；CLI 产品面尚未闭环。 |
| 外部集成目录 | ksj-gateway 当前主程序明确标为 scaffold；用户已把它确定为未来唯一外部集成边界，架构见 `KSpaceJet_gateway_architecture.md`。 | 当前不具备 listener、TLS、认证、profile、session 或 online service 能力；不得从目录推断已实现。 |
| 文档治理 | 已存在多个大型架构文档；旧架构规划提到 work-item schema 与 docs/work-items 目录，但当前仓库未发现其落地。部分 docs/conventions/README.md 链接也指向不存在文件或目录。 | P0 必须先建立真实的执行台账和文档链接基线；本文件暂为唯一进度账本。 |

### 2.1 当前第一目标

当前第一目标不是增加一个新算法，而是完成 P0-001：建立可重复的基线审计。它将把“代码存在”拆分为已实现、已测试、已系统验证、已性能验证和已发布资格五个不同结论。只有完成该审计，才可安全地将后续工作项从 PLANNED 置为 READY。

### 2.2 成熟度分层

| 层级 | 含义 | 可对外宣称 |
| --- | --- | --- |
| L0 设计 | 文档或接口预留，未验证 | planned only |
| L1 实现 | 有源码和局部测试 | implemented, not qualified |
| L2 可复现 | 干净环境可构建，单元和集成测试通过 | reproducibly verified |
| L3 系统 | 端到端场景、故障路径、artifact 和运维证据通过 | system verified |
| L4 资格 | 性能、平台、安全、文档和 release gate 通过 | qualified for declared envelope |

没有 L4 的功能不得标记 production-ready。

---

## 3. 目标产品和系统架构

### 3.1 四个可执行工程

| 可执行文件 | 唯一职责 | 禁止承担的职责 |
| --- | --- | --- |
| ksj | 用户和开发者命令行：检查、生成、验证、解释、重放、比较、Provider 工具和本地运行入口。 | 不实现第二套 runtime，不把 JSON stdout 当日志。 |
| ksj-gateway | 未来唯一外部集成入口；当前仍是 installed scaffold。后续以共享 GatewayRunHost 接入 runtime，公开 profile 由 P5-009 冻结。 | 不实现厂商采集、设备控制、私有 wire protocol、Provider 算法或 gateway-to-recon 私有网络数据面。 |
| ksj-recon | 执行 plan、管理调用方提交后的 ISMRMRD 数据、Provider、资源账本、scan lifecycle 和 run artifact。 | 不隐藏 Provider 算法，不依赖 research data plane，也不接管采集/传输。 |
| ksj-research | 实验编排、证据冻结、跨框架比较、论文和性能报告。 | 不成为正常 runtime 或数据面的依赖。 |

### 3.2 逻辑数据流

    Standard ISMRMRD HDF5, caller-submitted host-owned ISMRMRD data,
    or an accepted Gateway Profile normalized to host-owned ISMRMRD
                     |
                     v
    Structural + semantic validation / materialization / classifier / frame assembler
                     |
                     v
    ResolvedPipeline -> ExecutionPlan -> independent verifier -> admission
                     |
                     v
    Bounded graph executor -> host-enforced Provider firing leases
                     |
                     v
    Result artifact or caller callback / run record / metrics

每个箭头都必须有明确的所有权、失败语义、资源上限和可观察证据。外部 Connector 负责厂商采集与协议；ksj-gateway 只处理已选公开 Profile，KSpaceJet runtime 对规范化输入执行资源保护和明确的 accept/reject/terminal 语义。

### 3.3 核心持久 artifact

| Artifact | 生产者 | 不变性与用途 |
| --- | --- | --- |
| PipelineDefinition | 作者或 CLI | 描述语义图、Provider 选择、参数绑定和 ingress/egress，不描述线程或 buffer。 |
| OperatorContract | Provider | 描述单一 Operator 的端口、类型、配置、资源、terminal 和 capability 事实。 |
| TypeRegistry | 项目 | 所有 payload 类型的唯一来源，生成 C++/C API。 |
| ResolvedPipeline | resolver | 将 Provider、Contract、TypeRef 和默认值固定为一次解析结果。 |
| ExecutionPlan | compiler | 物理执行图、资源、并发、队列、placement、终止和 admission 依据。 |
| RunRecord | runtime | 记录一次 scan 的身份、输入、plan、Provider、配置、结果、状态与可复现证据。 |

PipelineDefinition 绝不是 ExecutionPlan；最大 acquisition 数、队列容量或资源上界绝不是 frame completion 条件。

---

## 4. 功能需求目录

表中 Target 阶段是该功能最晚必须通过的阶段。每个功能均映射到第 10 节验收编号和第 11 节工作项。

### 4.1 基础、数据和 artifact

| ID | 必须具备的功能 | Target | 验收 |
| --- | --- | --- | --- |
| FUN-001 | 可在 Linux 与 Windows 的固定工具链上从干净 checkout 配置、构建、测试和安装。 | P0 | AC-BLD-001 至 004 |
| FUN-002 | TypeRegistry 是唯一类型来源；生成代码失效会使构建失败而非静默重写。 | P0 | AC-TYP-001 至 003 |
| FUN-003 | 所有 JSON artifact 可进行 schema、canonicalization、identity digest 和跨引用校验。 | P1 | AC-ART-001 至 005 |
| FUN-004 | HDF5 reader 以 callback 范围内的零额外复制 view 暴露 acquisition；异步边界使用 host-managed materialization。 | P1 | AC-DAT-001 至 004 |
| FUN-005 | acquisition classification、四类 key、FrameSlot 和真实 frame completion 独立于资源上界工作。 | P1 | AC-DAT-005 至 008 |
| FUN-006 | 所有输入、输出、配置和运行结果均具有稳定 identity、可追溯 provenance 与结构化错误码。 | P1 | AC-ART-006 至 008 |

### 4.2 Pipeline、编译和执行

| ID | 必须具备的功能 | Target | 验收 |
| --- | --- | --- | --- |
| FUN-010 | PipelineDefinition 只能作者化语义图；不允许隐式 merge、drop、barrier、partial 或物理调度字段。 | P2 | AC-PLN-001 至 004 |
| FUN-011 | Resolver 以 catalog、contract、TypeRegistry 和配置生成不可变 ResolvedPipeline。 | P2 | AC-PLN-005 至 007 |
| FUN-012 | Compiler 从 ScanDescriptor、TargetEnvelope、MachinePolicy 和 ResolvedPipeline 生成 ExecutionPlan。 | P2 | AC-PLN-008 至 012 |
| FUN-013 | 独立 verifier 能拒绝不安全、类型不匹配、资源超限、终止不闭合或身份失配的 plan。 | P2 | AC-PLN-013 至 016 |
| FUN-014 | Runtime 有 bounded edge、资源预留、all-or-none fan-out、KeyShard 单写者和无 hold-and-wait 的 firing transaction。 | P3 | AC-RT-001 至 006 |
| FUN-015 | Host 强制 FiringLease；Provider 不能持有借用输入、未计费 buffer 或绕过 terminal 规则。 | P3 | AC-RT-007 至 010 |
| FUN-016 | normal end、cancel、failure、timeout、partial output 和恢复语义清楚、可测并写入 RunRecord。 | P3 | AC-RT-011 至 015 |
| FUN-017 | 多 scan admission、公平性、资源水位与有界背压可观测、可拒绝、可恢复。 | P6 | AC-RT-016 至 019 |

### 4.3 Provider、参考重建和用户工具

| ID | 必须具备的功能 | Target | 验收 |
| --- | --- | --- | --- |
| FUN-020 | Provider SDK 有稳定 C ABI、bundle manifest、identity 和动态 loader；失败诊断可操作。 | P4 | AC-PRV-001 至 006 |
| FUN-021 | Provider init、inspect、doctor、test、package 支持第三方最小开发闭环。 | P4 | AC-CLI-001 至 006 |
| FUN-022 | Reference Cartesian 2-D RSS pipeline 能完成 HDF5 输入、可选 conditioning、Provider execution、image 输出和 metadata。 | P1/P3 | AC-REF-001 至 007 |
| FUN-023 | Reference non-Cartesian 2-D pipeline 能完成 trajectory 处理、重建、coil combine、输出和错误路径。 | P1/P3 | AC-REF-008 至 013 |
| FUN-024 | ksj 可 inspect、dataset validate/generate、pipeline validate/explain/render/dry-run、run、compare、golden 和 doctor。 | P2/P4 | AC-CLI-007 至 016 |
| FUN-025 | RunRecord、verification record 和结果 artifact 可复现地关联到 input、plan、Provider 和配置 digest。 | P4 | AC-OBS-001 至 004 |

### 4.4 宿主嵌入、运维、性能与资格

| ID | 必须具备的功能 | Target | 验收 |
| --- | --- | --- | --- |
| FUN-030 | 若启用增量输入，嵌入宿主只能提交已归一化的 in-process ISMRMRD 数据；它与 HDF5 replay 在 runtime-frame 边界语义等价。 | P5 | AC-FED-001 至 004 |
| FUN-031 | ksj-recon 或嵌入 API 可报告本地 run lifecycle、admission、cancel、状态与结果 artifact；它不提供采集/传输服务。 | P5 | AC-FED-005 至 007 |
| FUN-032 | 已接受输入后的内部 queue、Provider 执行和结果 artifact 写入保持有界、可观测，并能以明确 reject/terminal 处理本地资源不足。 | P3/P5 | AC-FED-008 至 010 |
| FUN-033 | trace、metrics、audit、crash breadcrumb、日志和配置 resolve/explain 足以定位失败和性能退化。 | P7 | AC-OBS-005 至 011 |
| FUN-034 | CPU、NUMA、内存域、可选 GPU DevicePlan、异步取消和动态资源账本正确且无泄漏。 | P6 | AC-PERF-001 至 009 |
| FUN-035 | benchmark、golden、fuzz/property、长稳、故障注入、跨平台安装、供应链和文档门禁共同构成 release qualification。 | P7 | AC-REL-001 至 010 |
| FUN-036 | ksj-gateway 以一个已冻结的公开 Gateway Profile 提供 TLS、身份、授权、header-first admission 和已规范化的 ISMRMRD 外部集成入口；不定义私有 wire protocol。 | P5 | AC-GWY-001 至 005、009 |
| FUN-037 | gateway 的连接/scan/run 生命周期、双资源账本、cancel/disconnect 和 HDF5 equivalence 可验证，且 socket buffer 不会跨异步边界。 | P5 | AC-GWY-003 至 005、007 |
| FUN-038 | 独立 Connector 在自身信任边界处理厂商协议，并通过公开 profile/egress conformance；其制品不成为 Provider 或 runtime 依赖。 | P5/P7 | AC-GWY-006 至 009 |

### 4.5 用户授权的离线可视化

| ID | 必须具备的功能 | Target | 验收 |
| --- | --- | --- | --- |
| FUN-040 | `ksj-viewer` 是本地 Qt 桌面检查器：只读标准 ISMRMRD HDF5 的 header、acquisition 与 image，以及用户提供的 `PipelineDefinition`；它以现代、可用的 desktop workbench 展示 metadata、k-space、image 和 pipeline，并仅导出可视化派生产物。它不重建、不加载 Provider、不连接 gateway，MRI 交换与持久化 image artifact 仍唯一是 `.mrd`。 | P8 | AC-VWR-001 至 006 |

---

## 5. 非功能需求和可测约束

### 5.1 正确性与决定性

- 正确性优先于吞吐；任何优化必须先通过相同输入、相同 Provider、相同配置下的数值验证。
- 每个 reference case 必须指定绝对误差、相对误差、ULP 或领域指标，不能只写“输出看起来正确”。
- 算法的可重复性等级必须写入 contract 和 RunRecord：bitwise deterministic、numerically reproducible、statistical 或 nondeterministic。
- Golden 更新只能通过显式命令和人工可审计理由；普通测试不得重写 golden。

### 5.2 有界资源和实时性

- 所有框架拥有的队列、pool、FrameSlot、local artifact staging、GPU buffer、batch 和 in-flight firing 都必须有 item 与 charged-byte 双上界。
- 对调用方已提交的 in-process ISMRMRD 输入，admission 必须先完成本地资源预留；无法接受时返回明确的本地 reject/terminal，绝不定义或暗示上游 ACK、pause、credit、重试或流控。
- 对 Gateway Profile，header admission、read gating、关闭和慢 peer 行为只能使用该公开 profile 的已冻结标准语义；不得以私有 ACK/credit 补齐。停止读取只能限制 KSpaceJet 进程内增长，不能宣称保证上游采集不丢失。
- gateway 与 runtime 的 admission 必须同时预留各自的有界资源；任一失败必须可回滚且不留下部分可见 scan。
- 慢 Provider、结果 artifact 写入、gateway decoder、egress 与内部队列均不得导致无界堆积。KSpaceJet 只声明已验证的拒绝、取消和失败语义。
- 性能目标由 TargetEnvelope 和 MachinePolicy 参数化。没有测量环境、输入集和硬件拓扑时，不得在代码或本文件硬编码虚假帧率、延迟或内存指标。

### 5.3 性能指标定义

| 指标 | 定义 | 需要的证据 |
| --- | --- | --- |
| first-output latency | 从冻结 timed boundary start 到第一个合格 image egress 的单调时钟时间。 | trace、外部 collector、RunRecord。 |
| completion latency | 从 timed boundary start 到最后一个合格 output 或明确 terminal outcome。 | 同上。 |
| sustained throughput | 在指定 replay rate、case 和 duration 下的成功 frame 或 acquisition 数。 | replay report、sink count、resource trace。 |
| peak resource | RSS/PSS、各 memory domain charged bytes、GPU bytes、queue high-water、线程和 CPU time。 | external collector 加 runtime metrics。 |
| jitter | 延迟分位数与最大值，必须带样本数和时间窗。 | 原始样本或可复算摘要。 |
| correctness loss | 与冻结 reference 的数值误差、非法值、丢帧、重复帧、顺序违例和 terminal 违例。 | comparison report。 |

### 5.4 安全、供应链和故障隔离

- 所有外部 JSON、HDF5 metadata、调用方提交的 ISMRMRD data、Gateway Profile wire data、Provider manifest 和路径必须验证大小、schema、UTF-8、digest、边界和权限。
- 非 loopback Gateway listener 必须在任何 MRI payload 前完成已选 profile 的 TLS/mTLS、身份和 route 授权；secret、原始 payload 与 PHI 不得写入仓库或日志。
- gateway transport、decoder、session、egress 与 runtime 的 trust/resource boundary 必须分别可测；Connector/vendor SDK 不能进入 Provider 或 runtime。
- Provider bundle 在加载前验证 ABI、export、identity、dependency、hash、license 和 SBOM；不信任 Provider 不得自动进入 production registry。
- 进程内 trusted path 与 isolated-strict path 的保证必须分别声明。杀掉 worker 进程不等于 GPU kernel 已终止或显存已安全回收。
- 任何 crash、timeout、cancel、provider failure、input submission failure 和 result-artifact failure 都必须被映射为可审计的 ScanLifecycle terminal outcome。

---

## 6. 产品模式、范围冻结和关键决策

### 6.1 产品模式

功能必须按 mode 宣称。一个 mode 未通过其门禁时，上层 mode 不得启用。

| Mode | 允许能力 | 明确不承诺 | 最低阶段 |
| --- | --- | --- | --- |
| offline-reference | 公开 HDF5、单机、有限 reference Provider、可复现结果和离线分析。 | 增量 feed、临床、故障隔离、实时 deadline。 | P1 |
| bounded-reconstruction-graph | Generic plan 编译、独立验证、有界同步执行、取消和完整 RunRecord。 | 多 scan 并发、任意 async Provider、外部采集/传输。 | P3 |
| provider-development | SDK、in-process trusted Provider、contract、loader、第三方 conformance。 | 不可信 Provider 的 crash/hang 隔离。 | P4 |
| embedded-incremental | 同进程宿主提交 ISMRMRD、明确本地 admission、状态、cancel 和 callback/artifact。 | socket/session/gateway、上游 flow control、未测硬件的 hard real-time。 | P5 |
| external-integration-gateway | 经 P5-009 冻结的公开 Gateway Profile、TLS/mTLS、身份/route 授权、header-first admission、bounded connection/scan/egress 和 shared runtime bridge。 | 厂商设备协议、私有 wire、durable recovery、exactly-once、PACS/DICOM、HA、未测性能或临床宣称。 | P5/P7 |
| isolated-provider-runtime | worker fault boundary、quota、watchdog、审计、恢复、已验证设备策略。 | 临床诊断或监管宣称，除非另有专门项目。 | P7 |

### 6.2 已冻结的决策

| ADR ID | 决策 | 理由 | 后续动作 |
| --- | --- | --- | --- |
| ADR-001 | 本文件是当前唯一进度账本；不把不存在的 docs/work-items YAML 目录当作已实施系统。 | 旧架构计划提出过工作项 schema 和目录，但当前仓库不存在，双重台账会导致状态漂移。 | P0-003 检查是否需要从本文件生成只读报告，生成物不得反向成为第二权威。 |
| ADR-002 | 当前 Core diagnostics 保持 plain text；机器可读观测由 CLI JSON stdout、metrics、trace、RunRecord 和 audit artifact 提供。 | 根目录 AGENTS.md 的工程规则优先于长期架构文档中“结构化 JSON 日志”的冲突表述。 | P0-005 原子更新冲突文档和相应测试。 |
| ADR-003 | KSpaceJet 的 ISMRMRD schema、generic reader、CLI、ScanDescriptor、planner 和 runtime 不得规定通道数上限。 | 当前 Cartesian reference route 的 1 至 64 仅是临时实现限制，必须被移除；256 及更大数据集只是普通 reconstruction case，不是框架能力上限。 | P1-007 删除通道上限并加入任意正整数通道数 generator/property tests；机器资源不足以 bytes/work 失败，不得报“超通道上限”。 |
| ADR-004 | ksj-gateway 是 KSpaceJet 唯一的外部集成边界：它仅承载已选公开 Gateway Profile，终止 TLS/session、实施身份/route/资源/terminal policy，并经 GatewayRunHost 接入共享 runtime。Connector 的厂商协议/SDK 留在独立制品。 | 用户于 2026-08-23 明确要求真实外部集成网关；一个安全、可验证的边界比保留空 scaffold 或在 runtime 内散落 socket 代码更可审计。 | P5-008 冻结设计；P5-009 至 P5-013 依次冻结 profile、实现有界服务、接入 runtime、验证 Connector/egress 并资格化。未接受前不得宣称 service 能力。 |
| ADR-005 | 先完成 feature-gated CPU 可信路径，再增量开放并行、NUMA、GPU 和隔离特性。 | 不能用未证实的并发能力替代正确性和资源闭环。 | P3 是 P6 的强前置条件。 |
| ADR-006 | 输入和持久化重建 image 输出都只采用标准 ISMRMRD HDF5；输入以 runtime-owned `IsmrmrdHdf5ReplaySource` 进入，输出经 runtime-owned `IsmrmrdImageArtifactSink` 终结。移除 raw `.f32` image 与 JSON sidecar，不保留兼容路径。 | 用户于 2026-08-23 明确选择一个可由开源 ISMRMRD 工具直接读取、交流的 MRI data artifact，并于同日明确要求输入/输出成为统一 runtime 边界，而非 Provider Operator 或 CLI 中的文件逻辑。命令的 JSON stdout 仍是控制协议，不构成第二种 image 文件格式。 | P1-002 固定 source/sink 责任、image profile，并以官方 ISMRMRD C++ binding roundtrip 验证三条 reference route；P1-006 不得为持久化 image 新增并行 JSON sidecar。 |
| ADR-007 | 每次 `ksj-recon` 重建必须同时显式提供标准 ISMRMRD 输入文件与作者化 `PipelineDefinition` JSON；仅有 `.mrd` 不能完成重建。`ksj-recon` 本身就是唯一重建命令，不再嵌套 `reconstruct` 子命令。Pipeline 可由用户编辑，声明输入语义 profile、Provider/Operator 选择、逻辑图、静态算法参数及内部 egress；它不得包含输入/输出路径、DLL/SO 或 contract 路径、扫描派生尺寸、线程/队列/内存等物理运行时参数。runtime 负责从 `.mrd` 派生 scan facts、以受控 binding 解析 Provider/contract、编译/验证计划，并绑定统一 Source/Sink。 | 用户于 2026-08-23 明确要求 Pipeline 成为与 `.mrd` 同等必要的 `ksj-recon` 输入，且用户可在其中选择重建算法和参数；同日明确 `ksj-recon` 不需要额外 `reconstruct` 子命令。将流程硬编码进 `cartesian-rss`、`noncartesian-rss`、`radial-rss` CLI flag 或 C++ route 不满足该产品契约。 | P2-001 明确作者配置与 scan facts 的归属；P2-002 增加参数/profile/resolver 语义；P2-007 以 `ksj-recon --input <scan.mrd> --pipeline <pipeline.json> --output <image.mrd>` 取代当前专用路由命令和 caller-supplied Provider 路径。 |
| ADR-008 | Pipeline/scan-facts artifact ownership 必须先于离线 RunRecord 落地：P2-001 先冻结 authored Pipeline、scan-derived facts、effective binding 与 digest 关系，P1-006 再为每次 run 记录这些已冻结 identity。 | RunRecord 不能先于其所关联的用户 Pipeline 与 effective binding 定义来源；原先 `P2-001 → P1-006` 的依赖方向与 ADR-007 相反。用户已要求将可编辑 Pipeline 驱动的根命令作为后续重点。 | P2-001 依赖改为 P0-005、P1-002；P1-006 增加 P2-001 前置。此重排不提前接受 RunRecord，也不允许 P2-007 绕过 P1-006、P2-002 或 P2-004。 |
| ADR-009 | 用户授权的 `ksj-viewer` 采用 Qt 6 Widgets，作为本地、只读的离线 inspection desktop application。它消费标准 `.mrd` 与 `PipelineDefinition`，可导出显示派生产物；不得成为 reconstruction runtime、Provider、gateway 或 research data plane 的依赖，也不创建第二种 MRI artifact。 | 用户于 2026-08-23 明确要求实现能够查看 k-space、image、metadata 和 pipeline 的工具，并明确选择 Qt。将它放在安装的 app surface 而不是开发脚本目录，才能拥有完整 runtime-dependency、help、UI smoke 与安装验证。 | P8-001 冻结 Qt Core/Gui/Widgets、应用与部署边界；P8-002 补齐标准 inspection reader；P8-003 实现各视图与派生导出。P8 是用户授权的附加范围，不阻塞 P0-P7 或 v1。 |
| ADR-010 | `ksj-viewer` 的 source navigation 以 `InspectionReader` 递归、有界验证的**标准 ISMRMRD container**为根，而非硬编码 `/dataset` 或仅根级 group。每个 container 都以标准语义分类为 raw acquisition、image 和/或 waveform；用户只从可读 container 中选择，再进入 header、acquisition/k-space、image、waveform 等内容。HDF Group 的 `HDFView` 是文件树、对象检查、typed data view 与状态反馈的功能/交互参考；KSpaceJet 独立以 C++/Qt 实现。 | 用户于 2026-08-23 明确要求标准 ISMRMRD 文件直接打开、查看和重建，且明确指出标准 HDF5 group 名不是固定 API；真实 `cart_t1.mrd` 同时包含空的 `/dataset` 和可读的 `/dataset_1`、`/dataset_2`，手输默认组既不可靠也不符合 inspection workbench 的可发现性。必须保持 P8 的标准语义、只读、有界和无 Python/Matplotlib 依赖。 | P8-004 用单一有界 container discovery 取代仅根级 group discovery，并将 Viewer 重组为 HDFView 式 file root → `[RAW]/[IMAGE]/[WAVEFORM] container → content` navigation、对象 inspector、typed `Inspect`/`Open As`、页化 acquisition/coil/trajectory 检查、image cine/window-level；不能把任意 HDF5 浏览或无界缓存搬进 UI。 |
| ADR-011 | 标准 ISMRMRD 是唯一必需的输入和可交换 image artifact 语义：任意可读标准 raw container 必须无需 `ksj_*` group 即可被查看和重建；正式 reconstructed image 只用标准 ISMRMRD `image_x` series、ImageHeader、image data 与 MetaAttributes 写出，不定义 `/ksj_recon`、`/ksj_debug` 或 `/ksj_meta` 结果 group。作者化 `PipelineDefinition` 保持为独立、必需的 JSON 输入；只有未来有明确产品理由时，才可在显式 derived file 内附加可选 `/ksj_pipeline`，它绝不替代、污染或成为标准 container 的前置条件，且不得原地修改用户输入文件。 | 用户于 2026-08-23 明确要求正式结果直接采用原生 ISMRMRD `image_x` 机制，KSpaceJet 的潜在扩展只剩 pipeline material；这保持与 Gadgetron、MATLAB、ismrmrd-python 等工具互操作，避免私有 MRI result artifact。当前 Sink 已使用官方 binding 写标准 image series。 | P8-004 只发现和展示标准 container；P2-002/P2-007 必须删除固定 `dataset` 假设，改为确定性 auto-or-explicit standard raw-container selection。若未来确需嵌入 pipeline material，另立 runtime-owned `/ksj_pipeline` work item，冻结 layout、schema、provenance、copy/append/readback/atomic-publication 验证；它不属于 P8-004 或已接受的 pure-image Sink。 |
| ADR-012 | `ksj-viewer` 的主工作流采用 HDFView 式**File → hierarchy tree → selected-object inspector → typed data view → info/status**：菜单/工具栏承载打开、关闭、视图与帮助；打开 MRD 后默认只显示语义树和 selected-object inspector，typed-data 区域保持隐藏，不设置固定的 `Dataset overview` 或 `Image series` dashboard；树的选择只显示 object summary。显式 `Inspect`/`Open As` 才按 bounded typed options 打开 Header/XML 的 XML view、acquisition table/k-space 或 `image_x` image view。对象检查器固定为 `Object Attribute Info` 与 `General Object Info` 两页：后者使用紧凑 Name/Path/Type/Access form、标准 dataset semantics 与 member tables；XML preview 只位于显式打开的 XML typed view。image series 属于 `Images` 语义对象，raw acquisition container 的零 image series 是正常状态。它是 single-purpose、read-only ISMRMRD desktop application，而不是 HDFView 的 Java 代码移植、通用 HDF browser 或 editor。 | 用户于 2026-08-24 明确要求 UI、操作逻辑和功能以本地源码 `E:\hdfview`、安装目录 `D:\HDFView` 与 `HDFGroup/hdfview` 为参考，并使用 C++ Qt 重实现。内置 HDFView manual 证实其主窗口由 menu/toolbar/file bar/tree/metadata/info panel 构成，树选中对象驱动 metadata，`Open`/`Open As` 打开 table/image typed view。用户随后在真实 raw MRD 的视觉复核中指出 file-level `Dataset overview` 和空 `Image series` 区域重复且无价值。KSpaceJet 复用这些可用性原则，但不能扩大 MRI 数据和持久化边界。 | P8-004 替换当前 card/dashboard 重心，完成 HDFView 式 shell、语义对象树、`Object Attribute Info`/`General Object Info` inspector、context `Inspect`/`Open As` 及 data view tabs；测试必须证明只读、有界、标准 MRD-only、默认不显示冗余 dashboard、无 generic HDF traversal/edit/save/URL/file conversion/format support。 |

### 6.3 待决产品参数

下列参数没有用户或真实测量数据时，Codex 不得猜测。P0-006 必须为每一个填入实际值、单位、来源、适用范围和复核日期。

| 参数 | 例子 | 影响 |
| --- | --- | --- |
| ISMRMRD reconstruction envelope | encoding 数、矩阵、采样点、trajectory 维数、实际 channel 数、scan 并发数和算法配置；不得包含 channel 上限。 | FrameSlot、pool、计算、结果质量与本地资源测量。 |
| 性能 SLO | first image、completion、吞吐、p95/p99、允许队列时间 | plan、admission、benchmark gate。 |
| 部署拓扑 | CPU core、NUMA node、内存、GPU model/VRAM、NIC、OS、container 或 bare metal | placement、resource domain、binary packaging。 |
| 数据与隐私 | 是否仅 synthetic/public、脱敏规则、artifact 保留期、访问权限 | fixture、logging、capture、upload。 |
| 输出契约 | image encoding、metadata、ordering、artifact/callback、partial image 规则 | writer、callback、CLI、golden。 |
| 信任等级 | in-process trusted、isolated worker、签名、SBOM、第三方发布策略 | loader、runtime、release gate。 |
| Gateway integration policy | Gateway Profile、transport/TLS/mTLS、listener 网络区、route/Connector、upstream admission、egress、资源上限与数据保留 | P5 gateway 架构、部署、安全、隐私与 qualification。 |

#### 6.3.1 P0-006 参数登记与阻塞证据（2026-08-20）

本表只登记已审计到的事实及其边界；没有任何一行把 fixture、reference-route 派生值、历史/研究数据或本机观察提升为产品承诺。除非同一参数具备不可变来源、具名 owner、适用范围和 review date，否则它不是可接受的 `TargetEnvelope`、`MachinePolicy` 或产品 policy。

| 参数域 | 已观察到的事实（不得提升为产品值） | 来源 / owner / review | 状态与精确解阻输入 |
| --- | --- | --- | --- |
| ISMRMRD reconstruction envelope 与算法配置 | `tests/unit/libs/recon/fixtures/valid/target-envelope-minimal.json` 的 `org.example` 数值（64 KiB XML、1 MiB acquisition、16 MiB frame/image、8192 samples、64 channels、单 scan、1000 items/s）仅为 schema fixture。Cartesian/non-Cartesian reference route 每次从 HDF5 preflight 的 shape 派生单 scan、host-only 值；它们分别是 2-D RSS / non-DCF、non-SENSE direct adjoint 的开发 reference。研究 manifest 记录一个 84,543,864 B Gadgetron input，但 datasets README 明确其是 research/interoperability material，而非产品 case。 | fixture annotations 为空；route 代码和 research manifest 都没有产品 owner/review。research source access date 为 2026-08-11，但 license 为 `not stated by source`、redistribution 为 `unclear`。 | **BLOCKED**：case owner 必须冻结经批准的 ISMRMRD case manifest，逐项提供 encoding、matrix、samples、trajectory dims、实际 channel 数（非上限）、scan concurrency、Provider/algorithm config digest、来源/许可/隐私审核、适用范围和 review date。 |
| channel count 边界 | 当前两个 reference route 仍各有临时 `kMaximumChannels = 64`；ADR-003 禁止把它写入 framework envelope 或容量结论。 | `cartesian_rss_hdf5.cpp:49`、`noncartesian_rss_hdf5.cpp:45`；无产品 owner/review。 | **BLOCKED / P1-007 scope**：删除 reference-route 限制并以任意正整数 channel corpus 验证；P0-006 不登记任何框架 channel 上限。 |
| MachinePolicy 与部署拓扑 | schema/模型具备 host bytes、CPU permits、NUMA、memory domain、device 等字段；唯一 JSON instance 是 `org.example.dev-host` fixture（16 GiB、8 permits、1 NUMA、host-only）。当前主机观测为 Linux `6.12.101+deb13-amd64` x86_64、Intel i7-14700K、28 logical CPUs、1 NUMA node、67,077,595,136 B RAM、RTX 4060 Ti 16,380 MiB/driver 550.163.01、Intel I226-LM PCI device。 | schema/fixture/model 不含 deployment identity、owner 或 review date。本机命令可复现，但只是本次 build host；未发现 committed deployment config。 | **BLOCKED**：deployment owner 必须为每个目标环境给出 CPU affinity/permits、NUMA、RAM reserve、OS、container/bare-metal、NIC 需求，以及明确 CPU-only 或 GPU backend/device/VRAM/driver/stream budget、适用范围与 review date。本机 GPU 不能推断 runtime support；P6-004 仍是 GPU DevicePlan 的前置项。 |
| 性能 SLO | benchmark CMake/工具和空的 report template 存在；默认 preset 关闭 performance/benchmark/native-arch 选项。reference route 的 `acquisitions_read`、minimum drain `1` 和 fixture token-bucket 值是内部派生/测试值，不是测量结果。 | `docs/benchmark_reports/` 没有已接受 report；无 timed boundary、样本、raw artifacts、owner 或 review。 | **BLOCKED**：performance owner 必须提供 first output、completion、throughput、queue time、p50/p95/p99（含单位）、case/目标机/版本、timed boundary、样本和 raw artifacts，并指定 review date。 |
| 数据、隐私与保留 | research Gadgetron manifest 具有 pinned source/hash；raw payload 不提交、本地 research storage 保留。`logging.file.retention_days=30` 是 disabled-by-default 的日志文件配置；RunRecord 仅禁止 reason code 带 PHI。 | research manifest 没有具名 case/data owner；不存在 de-identification、access 或 retention policy/review。 | **BLOCKED**：data-governance owner 必须决定允许的 synthetic/public/approved case 类别、license/redistribution、de-identification/PII、访问控制、日志/结果 artifact retention 与删除规则，并给出 source、适用范围和 review date。 |
| 输出契约 | 用户（output owner）于 2026-08-23 以 ADR-006 冻结了唯一文件交换格式：标准 ISMRMRD HDF5 ImageHeader、image data 与 MetaAttributes；`.f32 + JSON sidecar` 必须移除。P1-002 只实现并验证此 development image profile，使用 test-time synthetic HDF5，不依赖或复制 raw corpus。 | 用户指令（2026-08-23）；P1-002 负责写入字段映射、临时文件验证和原子发布。此决定不提供 deployment owner、retention/access 或 callback policy。 | **BLOCKED（其余治理）**：data/output owner 仍必须冻结 ordering、partial/callback、deployment overwrite/durability/failure 与 retention/access policy，并给出 source、范围和 review date。格式选择本身已不再阻塞 P1-002。 |
| Provider trust 与第三方发布 | loader 可用 caller-supplied absolute path、trusted root 和 optional bundle digest，但明确是 trusted in-process ABI boundary；catalog 的 `implemented-development` 不是 release/trust approval。 | 未提交 trusted root/allowlist、签名、SBOM、第三方审核或 isolation policy；无 owner/review。 | **BLOCKED**：security/release owner 必须定义 trusted root、digest/signing/SBOM/hash、第三方准入/撤销、in-process trust tier 与 isolated-provider activation rule，并给出 source、范围和 review date。 |
| Gateway 外部集成 | 已有 ksj-gateway scaffold、blocking IPv4 socket 基元、HDF5 reader、HostFrameAssembler、ScanLifecycle 与 ResourceVectorLedger；均不能证明公共网络服务。架构候选见 `KSpaceJet_gateway_architecture.md`。 | 未选择公开 Gateway Profile/transport binding；无 listener 网络区、PKI、principal/route、Connector ownership、upstream flow、egress、spool/retention、SLO 或 deployment owner/review。 | **BLOCKED / P5-009 前置**：architecture、security、deployment、integration、output 和 data-governance owner 必须共同提供 GWY-DEC-001 至 007 的来源、范围、值和 review date；未齐全时不得实现或对外监听。 |
| 参数表示权威 | JSON TargetEnvelope schema 与 C++ `planning_inputs.hpp` value model 的字段并不一致，且未发现 JSON TargetEnvelope parser/serializer；两者都不能单独成为部署参数 authority。 | schema 仅结构验证，未定义 owner/source/review metadata。 | **BLOCKED**：architecture owner 必须指定可审计的单一参数 artifact/serialization 和与 C++ model 的一致性计划；在此之前不得从任何一侧生成或接受产品 policy。 |

采集设备、scanner、厂商私有协议和设备控制仍按 ADR-004 不在本登记范围；已选 Gateway Profile 的 transport/security/deployment 参数现在必须由 P0-006 登记。P0-006 的阻塞不改变 P0-002 已接受的 Windows Release developer-install evidence，亦不允许由 Linux 或本机事实扩张为 deployment 或 qualification evidence。

---

## 7. 验收总策略

### 7.1 验收层级

| 层级 | 目标 | 最低证据 |
| --- | --- | --- |
| A0 文档与静态 | 路径、链接、格式、schema、生成物和依赖一致。 | 机器检查、diff check、链接检查和 schema negative fixtures。 |
| A1 单元 | 单个类型、算法、资源事务、状态转换和错误码正确。 | focused CTest 或 Python test。 |
| A2 组件 | Provider、loader、compiler、verifier、reader、sink 等边界正确。 | 可执行 integration test 和 fixture。 |
| A3 端到端 | 从 source 到 image/result 的正常、失败、取消和恢复路径。 | 固定 input、RunRecord、output compare、terminal assertion。 |
| A4 压力与故障 | 已提交输入后的内部资源压力、burst replay、slow Provider、artifact writer failure、hang、crash、内存压力和长稳。 | fault injection、high-water、no-leak、outcome report。 |
| A5 性能与资格 | 目标机、目标数据、重复实验、跨平台和发布包证据。 | benchmark report、artifact manifest、install/smoke、审计清单。 |

一个功能至少通过 A0 至 A3 才能被标为 L3 system verified。P6/P7 的功能还必须通过适用的 A4/A5。

### 7.2 固定验证入口

Linux bootstrap 直接通过 apt 确保安装 `just`，由 apt 的幂等性处理已安装 package；Windows bootstrap 会在缺少时通过 `winget` 安装 `Casey.Just`。两端均不下载项目私有副本、锁定版本或校验版本。bootstrap 仍直接调用平台脚本以 provision 仓库内 Python 工具；之后的日常开发命令直接使用根 `justfile` 的同名 recipe。对无 recipe 的聚焦诊断，使用 platform runner 调用锁定的 Conan、CMake、Ninja 或 formatter，不得依赖它们的系统 PATH。

Linux 首次准备：

    bash tools/devenv/linux/bootstrap.sh
    just prepare-release
    just build-release-applications

Linux 格式与静态基线：

    git diff --check
    just format-all
    just type-check

Linux 单元测试：

    just unit

Linux 完整检查和 benchmark smoke：

    just check
    just full

Windows 基础门禁：

    powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1
    just check
    just pre-push

产品 application 与 unit/benchmark/research 测试必须使用不同 build tree。不得以 application build 成功替代 unit test，也不得把测试 preset 同时配置为 application build。

### 7.3 验收条目

#### AC-BLD：构建、供应链和文档完整性

| ID | 通过条件 |
| --- | --- |
| AC-BLD-001 | 新 clone 的 Linux 环境在依赖前提满足时能 bootstrap、export local recipes、configure linux-release-unit-tests、build 和 ctest。 |
| AC-BLD-002 | Windows MSVC 2022 Release 至少有 configure、build、install smoke；2026-08-22 起 P0 developer-environment evidence 以用户指定的 Release 路径为准。若任一后续工作项需要 Debug，它必须单独声明并记录 configure、build、install evidence；失败时报告缺失 host prerequisite 而不是静默跳过。 |
| AC-BLD-003 | Git LFS Intel payload 的 manifest、hash、Linux/Windows runtime dependency 通过仓库验证。 |
| AC-BLD-004 | docs 站内链接、引用目录、命令和 target 均存在；失效链接会由检查阻止合并。 |
| AC-TYP-001 | 修改 registry 而未更新生成 C++/C header 时，type registry check 必定失败。 |
| AC-TYP-002 | 每个 TypeRef 的 layout、identity digest 和 schema digest 只从 registry/generated factory 取得，无手写副本。 |
| AC-TYP-003 | 类型破坏性变更同时更新 schema、registry、generated headers、Provider contract、fixture 和测试。 |

#### AC-ART / AC-DAT：数据与 artifact

| ID | 通过条件 |
| --- | --- |
| AC-ART-001 | 每个主 artifact 有 valid 与 invalid fixture，schema 验证成功和失败路径均稳定。 |
| AC-ART-002 | canonical JSON 与 digest 对对象键顺序、空白和等价表示具有规定行为，篡改必改变或拒绝 identity。 |
| AC-ART-003 | ResolvedPipeline、ExecutionPlan、RunRecord 跨 artifact 的 digest/ID 关联可验证。 |
| AC-ART-004 | schema pass 不能替代 semantic validation；至少有 schema 合法但 compiler/verifier 拒绝的 fixture。 |
| AC-ART-005 | Provider contract、catalog、plan 和 type registry 的 identity mismatch 有稳定错误码和机器可读诊断。 |
| AC-ART-006 | 每个 input、output、config、pipeline、plan、Provider 和 result artifact 有稳定 identity/provenance，且可由 RunRecord 关联。 |
| AC-ART-007 | artifact 写入具备 atomicity、完整性校验和失败清理语义；中断写入不得伪装为成功结果。 |
| AC-ART-008 | artifact reader 能拒绝截断、schema/digest 不匹配、未知必需字段和非法编码，并保留诊断上下文。 |
| AC-DAT-001 | HDF5 reader 不在 callback 外泄漏 borrowed view；异步路径只能接收 materialized host buffer。 |
| AC-DAT-002 | 同一 scan 的 HDF5 replay 与调用方提交的等价 in-process ISMRMRD 数据在 runtime-frame boundary 产生相同 classification、frame identity 和 completion 语义。 |
| AC-DAT-003 | 空、截断、非法 XML、非有限值、错误 layout、重复/缺失 acquisition、坏 trajectory 的输入被确定拒绝或产生规定 terminal outcome。 |
| AC-DAT-004 | 所有输入都有经过测试的最大字节数、元素数、metadata 长度与整数溢出防护；不得设置或隐含最大 channel 数。 |
| AC-DAT-005 | Scan、Frame、Ordering、Calibration、Partition key 彼此分离；测试证明它们不能被错误混用。 |
| AC-DAT-006 | FrameSlot 只有在真实 completion 条件达成后 Ready；max count 只用于资源规划，不能结束一个 frame。 |
| AC-DAT-007 | 参数化 fixture/generator 能接受任意正整数 channel 数（至少覆盖 1、64、256 和大于 256 的 case）并走相同 generic code path；不得以 channel count 拒绝。若实际 bytes/work 无法预留，必须报告资源不足而非通道上限。 |
| AC-DAT-008 | 一次 scan 结束时，所有 incomplete frame 都转为明确 terminal outcome，绝不靠零填充猜测数据长度。 |
| AC-DAT-009 | 所有 HDF5 reference route 经同一 normalized ISMRMRD semantic-frame ingress：header/layout/finite-value validation、AcquisitionClassifier、control/lane flag 解释和 host-owned materialization 不得由每条 route 重复或矛盾实现。 |
| AC-DAT-010 | Frame key projection 显式保留 encoding、slice、contrast、phase、repetition、set、segment 和必要的 calibration/trajectory 语义；不得把多 echo、cine、EPI 或 calibration acquisition 静默混为一个 frame，也不得凭 flag 数值猜测它们属于 imaging。 |

#### AC-SCH / AC-PLN / AC-RT：并行语义、图、计划与运行时

| ID | 通过条件 |
| --- | --- |
| AC-SCH-001 | 每个可并行 Provider Operator 都在 contract 中声明 `PartitionCapability`：每个候选轴的 independent/grouped/ordered/window/collective 语义、共享 calibration/state、partial-execution、output order、可用 backend 与限制。未声明时 compiler 默认不在该轴并行。 |
| AC-SCH-002 | compiler 仅从实际存在的 ISMRMRD logical group 构造 WorkKey；不得将 encoding、slice、contrast、frame 盲目做笛卡尔积，且每个 WorkKey 都能追溯到输入与 Provider contract。 |
| AC-SCH-003 | WorkKey 只有在对应 FrameSlot 语义 complete/sealed、所需 calibration epoch 与 ordered predecessor 全部 ready 后才可进入 Ready；max acquisition count 不得替代 completion。 |
| AC-SCH-004 | stateful/calibration/ordered path 的同一 KeyShard 在任意时刻只有一个 queued-or-running writer；无共享可变状态的 ComputeTask 才能在 CPU/GPU executor 并行。 |
| AC-SCH-005 | 每个 grouped/window/collective task 都有显式 join、normal/cancel/failure terminal 和 deterministic output order；执行完成顺序不得改变可见 image/result 顺序。 |
| AC-SCH-006 | channel count 不是 partition admission 条件。channel block 只能由实际 bytes/work、cache/device policy 和 Provider contract 导出；1、64、256 及大于 256 的输入不触发框架分支或上限。 |
| AC-SCH-007 | ResourceLedger 在 task 可见于 ReadyQueue 前原子预留其 host/device bytes、queue items/bytes、CPU concurrency、device stream、artifact staging 与 async-token 预算；资源不足时不创建部分 task 或半可见状态。 |
| AC-SCH-008 | 每个 BufferHandle、FiringLease、OutputGrant、device fence 和 async token 有单一 owner、charged resource domain 与一次释放路径；normal/cancel/failure/crash 后实际 ledger 回到规定值。 |
| AC-SCH-009 | 同一 ScanDescriptor、ResolvedPipeline、Provider contract 和 MachinePolicy 生成 deterministic ExecutionPlan；并行运行与 serial oracle 在声明数值规则内等价。 |
| AC-SCH-010 | Runtime 只能在已验证 ExecutionPlan 的合法 ready task 中选择运行顺序；不得临时扩大 partition、改变 grouped/ordered 语义、绕过 reservation 或让 Provider 自行创建未计费调度。 |
| AC-SCH-011 | P6 的 multi-scan fairness、NUMA/GPU placement、batch/fusion 只在 P3 基线通过后启用；每个策略都有 starvation、cancel、high-water、正确性与 fallback evidence。 |

#### AC-PLN / AC-RT：图、计划、运行时

| ID | 通过条件 |
| --- | --- |
| AC-PLN-001 | PipelineDefinition 不能作者化 worker count、queue implementation、allocator 或任意物理 schedule。 |
| AC-PLN-002 | 所有 merge、join、reorder、calibration dependency、drop、partial 和 terminal 行为显式声明并可验证。 |
| AC-PLN-003 | 每条 edge 的 TypeRef、port direction、multiplicity 和 ownership 被静态检查。 |
| AC-PLN-004 | invalid graph 的诊断指向 source location、node/port 和违反规则。 |
| AC-PLN-005 | resolver 对同样输入产生同样 ResolvedPipeline digest；选择、默认值和 config binding 可解释。 |
| AC-PLN-006 | 无法发现 Provider、contract、TypeRef 或 capability 时，resolver 不产生半有效 plan。 |
| AC-PLN-007 | provider interface reservation 不可被当作 executable contract 使用。 |
| AC-PLN-008 | compiler 对 ScanDescriptor、TargetEnvelope、MachinePolicy 和 ResolvedPipeline 产生完整的物理资源和 terminal plan。 |
| AC-PLN-009 | ExecutionPlan 的各队列、pool、batch、threads、memory domain、device 和 in-flight 上限均可追溯至 source field。 |
| AC-PLN-010 | plan 中每个 Provider action 有 required FiringLease 输入/输出与 terminal policy。 |
| AC-PLN-011 | P6 前 compiler 不得偷偷开启 partition、fusion、async 或 GPU scheduling；必须由明确 capability 和 feature flag 支持。 |
| AC-PLN-012 | compiler 对同一固定输入 deterministic，或在计划中记录许可的 nondeterministic choice。 |
| AC-PLN-013 | independent verifier 不调用 compiler 私有状态，且可拒绝篡改的 digest、类型、边界、终止和资源。 |
| AC-PLN-014 | 小图 exhaustive/property corpus 中 compiler 成功的 plan 均由 verifier 接受，非法 plan 均被拒绝。 |
| AC-PLN-015 | verifier 的 pass 只表示已证明的边界；不能宣称未编码的 latency、GPU cancel 或 process isolation。 |
| AC-PLN-016 | plan 证书和实际 runtime high-water 有可对比的字段。 |
| AC-RT-001 | edge reservation 是原子 transaction；资源不足不会产生部分可见 payload 或泄漏 reservation/lease。 |
| AC-RT-002 | fan-out 要么所有 target 成功提交，要么无 target 可见；禁止隐式 drop。 |
| AC-RT-003 | 同一 serial KeyShard 在任意时刻 queued plus running 不超过一；不同 key 的并行只在 capability 允许时开启。 |
| AC-RT-004 | actual item/byte high-water 不超过 ExecutionPlan；违反立即记录 fatal diagnostic。 |
| AC-RT-005 | runtime 不持有未计费 provider-owned pool，也不允许 provider 隐藏 background work。 |
| AC-RT-006 | 资源账本在正常、取消、失败、重复释放和异常 path 后回到零或规定 persistent artifact 占用。 |
| AC-RT-007 | FiringLease 输入只读、输出已预留、deadline/terminal/cancel 可查询；Provider 无法写出 lease 边界。 |
| AC-RT-008 | Provider 尝试 retain borrowed data、写超界、double commit、漏掉 terminal 或返回非法 identity 时被 host 拒绝。 |
| AC-RT-009 | In-process Provider native crash 只允许在 provider-development mode；isolated-provider-runtime mode 必须有隔离证据。 |
| AC-RT-010 | synchronous path 的结果与 serial oracle 数值等价，误差规则明确。 |
| AC-RT-011 | normal end 仅在 EndOfInput 和声明的 normal flush 完成后产生 Completed。 |
| AC-RT-012 | cancel、input submission failure、provider failure、timeout、result-artifact failure 各有唯一 terminal outcome 和稳定错误上下文。 |
| AC-RT-013 | partial output 只有被 contract/sink 明确允许时才交付，且含 ordinal/reason/provenance。 |
| AC-RT-014 | recovery/cleanup 不会把未完成 GPU/worker work 误认为安全回收。 |
| AC-RT-015 | 同一终态重复消息幂等，不会重复输出、重复释放或错误晋级。 |
| AC-RT-016 | 对调用方已提交的 ISMRMRD 数据，admission 按 item、byte、CPU、device 与结果 artifact budget 作出本地 accept/reject；拒绝不得留下部分 scan 状态，也不得定义上游 ACK/credit/pause。 |
| AC-RT-017 | 多 scan 的内部资源占用、queue、thread、device 和 output budget 可分别计账；一个 scan 的 cancel/failure 不可污染其他 scan。 |
| AC-RT-018 | 本地过载、Provider saturation、结果 artifact writer failure 与持久化空间不足均执行已声明策略，不允许无界堆积或静默丢弃已接受的 ISMRMRD 数据。 |
| AC-RT-019 | fairness、priority、quota 和本地 admission decision 被记录为可观察事件，并可通过确定性模拟或系统测试复现。 |

#### AC-PRV / AC-REF / AC-CLI：Provider、参考路径和工具

| ID | 通过条件 |
| --- | --- |
| AC-PRV-001 | Provider C ABI header 在 C 与 C++ 下均可编译，symbol visibility 和 calling convention 在 Linux/Windows 均通过。 |
| AC-PRV-002 | loader 对缺失 export、错误 ABI、错误 manifest、hash mismatch、重复 identity、依赖冲突和错误架构给出稳定失败。 |
| AC-PRV-003 | bundle identity 将 Provider binary、contract、TypeRegistry、ABI、dependencies 和可选签名关联起来。 |
| AC-PRV-004 | provider init 生成的最小项目可独立 build、test、package；生成目录存在时绝不覆盖。 |
| AC-PRV-005 | provider test 在独立 test process 中覆盖正常、cancel、错误、边界和并发 conformance；native crash 明确归因。 |
| AC-PRV-006 | isolated-provider-runtime 只接受经过 policy 许可、可隔离且通过 conformance 的 bundle。 |
| AC-REF-001 | Cartesian reference 明确其 2-D、encoding、layout 和 sampling 语义；不得以 channel count 限制输入。若特定 Provider 的数学 contract 不适用，必须给出 contract diagnostic 而非 framework input rejection。 |
| AC-REF-002 | Cartesian fully sampled fixture 对 reference image、metadata、shape、orientation 和 finite value 进行比较。 |
| AC-REF-003 | optional noise prewhiten、phase correction、coil compression、readout crop 每条分支有独立 baseline、组合测试和关闭分支测试。 |
| AC-REF-004 | channel/coil layout、readout direction、FFT scaling、RSS 公式和 output dtype 均有可复算 oracle。 |
| AC-REF-005 | Cartesian 的 corrupted/missing calibration、invalid offset、provider failure、cancel 和 output failure 有确定结果。 |
| AC-REF-006 | Cartesian 成功输出包含 input、pipeline、plan、Provider、config、result digest 和 timing provenance。 |
| AC-REF-007 | reference 路径不宣称临床算法或超出 contract 的能力。 |
| AC-REF-008 | non-Cartesian reference 明确限定为其实现的 2-D direct adjoint 或已启用能力，不推断 density、trajectory correction 或 SENSE。 |
| AC-REF-009 | trajectory dimensions、sample count、ordering、nonfinite trajectory 和 mismatch 具有 fixture 和确定错误。 |
| AC-REF-010 | non-Cartesian 输出与独立可复算 reference 或冻结 golden 在声明 tolerance 内。 |
| AC-REF-011 | coil combine、metadata、shape、output dtype 和 nonfinite result 通过正常与异常测试。 |
| AC-REF-012 | non-Cartesian route 在 cancel、provider failure 和 sink failure 下资源归零并生成 RunRecord。 |
| AC-REF-013 | 任何 future trajectory correction、phase correction、SENSE 必须先从 planned interface 升级为完整 contract、Provider、测试和 catalog 事实。 |
| AC-CLI-001 | 所有 app 使用 CLI11 声明命令、参数、校验和 help；不得手写 argv parser 或未文档化 alias。 |
| AC-CLI-002 | 每个支持 machine output 的命令提供 format text 或 json；JSON stdout 不混入 log，diagnostic 去 stderr。 |
| AC-CLI-003 | exit code、JSON schema、success/failure envelope 和错误字段有 app test。 |
| AC-CLI-004 | provider init 的名称、路径、catalog 不变性、覆盖保护和生成物有 app test。 |
| AC-CLI-005 | provider inspect/doctor/test/package 能发现 ABI、manifest、runtime dependency 和 contract 问题。 |
| AC-CLI-006 | 每个 CLI command 具有完整 help 与至少一条 success/invalid input 端到端测试。 |
| AC-CLI-007 | inspect 输出 XML、encoding、维度、flags、coil、trajectory、数量和时间线摘要。 |
| AC-CLI-008 | dataset validate/generate 使用公开、可重复 fixture，并输出稳定 validation report。 |
| AC-CLI-009 | pipeline validate/explain/render/dry-run 将 schema、semantic、resolver、compiler、verifier、admission 层次分开报告。 |
| AC-CLI-010 | run 只能使用已验证 plan，输出 artifact、RunRecord、exit status 和 machine-readable summary。 |
| AC-CLI-011 | replay/dataset validate 的输入只接受公开 ISMRMRD；不得新增 capture、socket、session 或私有落盘格式。 |
| AC-CLI-012 | compare/golden 命令报告 absolute/relative/ULP 或明确领域误差及差异 artifact。 |
| AC-CLI-013 | trace 和 benchmark 命令保留 case、硬件、timed boundary、版本和 raw report。 |
| AC-CLI-014 | run list/show/cancel 只针对本地或嵌入宿主已经建立的 reconstruction run 开放，且支持 JSON；不得出现 gateway 命令。 |
| AC-CLI-015 | doctor 报告 CPU/NUMA、dynamic library、Intel payload、文件权限、配置和可执行修复建议；不得探测或配置网络端口、扫描仪或采集设备。 |
| AC-CLI-016 | 未实现 command 不能伪装成功；应在 help/JSON 中明确 scaffold 或 unavailable。 |

#### AC-VWR：Qt 离线检查器

| ID | 通过条件 |
| --- | --- |
| AC-VWR-001 | `ksj-viewer` 是由 CLI11 声明 help/version 的 Qt Widgets desktop application；正常 UI 路径实际创建 `QApplication` 与主窗口，且不以 `WIN32_EXECUTABLE` 隐藏诊断或自动化入口。 |
| AC-VWR-002 | viewer 只链接 `KSpaceJet::ismrmrd`、`KSpaceJet::recon_graph` 和 Qt Core/Gui/Widgets；不得链接 recon runtime、Provider loader/module、gateway、research 或 `mri_debug`，也不得执行、加载或发现 Provider。 |
| AC-VWR-003 | Windows Release build 与 install tree 均部署 Qt 所需的最小平台插件（含 `platforms/qwindows.dll`）；`--ui-smoke` 在实际 QApplication 路径通过，不能以 `--help`、`dumpbin` 或手工 PATH 替代插件加载验证。 |
| AC-VWR-004 | inspection reader 只读标准 ISMRMRD HDF5 的 header、acquisition 和 image；acquisition 按需/有界读取，image/header/meta 的轴、类型与属性保留标准语义，错误输入给出确定性诊断，不保留 raw data。 |
| AC-VWR-005 | metadata、k-space、image 和 pipeline 视图复用同一 reader/parser；k-space 不伪装为重建图像，pipeline 不复制 parser 或执行图，PNG/SVG/CSV/JSON 等 export 被标记为显示派生产物而非第二种 MRI artifact。 |
| AC-VWR-006 | viewer 在 1280×800 及以上提供 HDFView 式 Qt Widgets 主窗口：File/Window/Tools/Help 菜单、常用操作工具栏、当前文件栏、左侧 hierarchy tree、右侧 tabbed object inspector/data views 及跨宽度的 info/status panel。树 selection 只更新 General/ISMRMRD Header/Attributes inspector；打开 typed data view 必须显式使用 `Inspect`/`Open As`。界面可现代化，但功能层级和信息密度必须可对照 HDFView；不得改变 P8-003 的只读数据、parser、export 或依赖边界。以 widget structure/state tests、真实 QApplication UI smoke 与用户视觉复核验收。 |
| AC-VWR-007 | Viewer 只显示由 `InspectionReader` 在 `InspectionReadLimits` 内递归验证通过的标准 ISMRMRD semantic object tree；不得假设路径为 `/dataset`。文件 root 下按 `[RAW]`、`[IMAGE]`、`[WAVEFORM]` 语义展示可切换 container → Header / Acquisitions (k-space) / Images / Waveforms，空、私有或非标准 group 不会阻止有效 container 的发现且不作为标准 source。对象 context menu 只能给出受支持的 `Inspect`、`Open As…`、copy path 或关闭 source；无对应内容必须明确不可用，不得伪造图像或 waveforms。UI 不得直接进行任意 HDF5 traversal、保存 raw payload、无界预读、generic file/object/attribute edit/save、URL loading 或非-MRD format support。以 synthetic nested empty+raw+image+waveform container、focused reader/presentation/widget tests 及 Windows Release UI smoke 验证。 |
| AC-VWR-008 | `Open As…` 以类型特定且有界的 option dialog 开启 acquisition table/k-space 或 image view：Acquisition 使用 Reader 的 header-only、页化索引显示标准 ordinal/flags/encoding/sample/channel/trajectory facts，只在用户选择时读取一个 bounded payload 生成 coil-aware k-space 与 trajectory 显示派生；Image 在不保留 source pixels 的前提下支持 standard `image_x` series 的 ordinal cine、z/channel、auto/manual window-level、zoom、pixel probe 和 histogram（后两者仅对当前有界 display derivative）。不支持的内容必须显式不可用。以 header-only reader、presentation/widget tests 与 Windows Release UI smoke 验证。 |

#### AC-FED / AC-GWY / AC-OBS / AC-PERF / AC-REL：嵌入输入、网关、观测、性能和发布

| ID | 通过条件 |
| --- | --- |
| AC-FED-001 | HDF5 replay 与调用方提交的等价 in-process ISMRMRD 数据在 classification、frame identity、completion 和 result artifact 上等价。 |
| AC-FED-002 | 借用的 ISMRMRD view 只在回调内有效；异步/并行执行前完成 host-owned materialization。 |
| AC-FED-003 | in-process feed API 不引入 socket、session、wire message、gateway、Connector、采集设备或上游流控依赖。 |
| AC-FED-004 | malformed、truncated、duplicate、missing 或 producer-terminated 的输入都映射为明确 local terminal outcome。 |
| AC-FED-005 | local/embedded run 的 live state、version、config digest、Provider readiness、admission 状态可查询且有 JSON 模式。 |
| AC-FED-006 | run list/show/cancel 对 concurrency、terminal states 和 idempotence 有测试。 |
| AC-FED-007 | normal run、cancel、input submission failure、Provider failure、result-artifact failure 和 restart 的 status transition 可追踪。 |
| AC-FED-008 | 本地 admission 在接受输入前完成资源预留；资源不足只返回本地 reject，不提供上游 ACK、pause、credit 或 retry 语义。 |
| AC-FED-009 | bounded internal queue、slow Provider 和 artifact writer failure 不会无限增长，也不会无声丢弃已接受 input 或 image。 |
| AC-FED-010 | paced HDF5 replay 与 in-process feed 的 burst/ordering corpus 可验证 runtime 高水位不超过 plan，结果与 HDF5 oracle 等价。 |
| AC-GWY-001 | 每个 deployed endpoint 只接受 P5-009 冻结的单一公开 Gateway Profile；规范、精确版本、license、transport binding、serialization 和 conformance vectors 都可审计。不得私有 framing、隐式 fallback 或协议降级。 |
| AC-GWY-002 | 非 loopback listener 在任何 MRI payload 前完成 TLS/mTLS、principal、route 与 profile 授权；认证/证书/限速失败有固定资源上限且不泄露 secret。 |
| AC-GWY-003 | connection、gateway scan 与 reconstruction run 状态独立；normal/reject/cancel/fail/disconnect/drain 有确定映射，gateway 不伪造 runtime admitted/completed。 |
| AC-GWY-004 | wire buffer 和 parser scratch 不跨异步边界；已验证的 OwnedIngressEvent 与 HDF5 replay 在 runtime-frame boundary 的 classification、frame identity、completion 与 terminal 语义等价。 |
| AC-GWY-005 | header-first admission 对 gateway/runtime 资源做原子预留和回滚；所有 connection/decode/scan/egress/runtime 域有 item 与 charged-byte 上限，不能无界增长或静默丢弃。 |
| AC-GWY-006 | egress 使用 OutputGrant；partial/result/terminal、slow peer、delivery timeout 与断线的语义明确且可测，不宣称 durable recovery、cross-connection dedupe 或 exactly-once。 |
| AC-GWY-007 | fake-peer 与 mutation/fuzz corpus 覆盖 fragmentation、truncation、非法长度/UTF-8/order、认证失败、route 越权、资源耗尽、slow peer、cancel、disconnect 和 Provider/runtime failure。 |
| AC-GWY-008 | Connector 独立于 Provider/runtime/网关进程；它通过已选公开 profile 的正负 conformance harness，厂商 SDK/凭据不进入 KSpaceJet 默认产品面。 |
| AC-GWY-009 | Gateway config、PKI、route、资源上限、数据/输出保留和审计有 source/owner/scope/review date；PHI、raw payload 与 secret 不进入仓库或普通日志。 |
| AC-OBS-001 | RunRecord 和 verification record 的 schema、identity、write timing 和 failure path 有单元与端到端测试。 |
| AC-OBS-002 | audit event 在权限、admission、Provider identity、terminal、output 和安全决策上不可伪造、可排序、可关联。 |
| AC-OBS-003 | config resolve/explain 输出来源、默认值、优先级、digest 和脱敏后的实际值。 |
| AC-OBS-004 | crash breadcrumb 和 emergency signal path 不调用不安全 logger，并可关联 Scan/Run ID。 |
| AC-OBS-005 | metrics 至少包含 queue/pool high-water、ledger、firing、provider duration、input submission/result-artifact rate、terminal outcome、errors 和 admission reject。 |
| AC-OBS-006 | trace 可以关联外部 monotonic clock；框架内部 trace 不能伪造外部端到端 latency。 |
| AC-OBS-007 | core diagnostics plain text，CLI JSON stdout 和指标/trace/artifact 各自保持协议边界。 |
| AC-OBS-008 | 每个可测 SLO 都暴露采样数、窗口、分位数、单位、版本和输入 case。 |
| AC-OBS-009 | 日志、trace、artifact 的 retention/PII policy 可配置并经审计。 |
| AC-OBS-010 | 失败时最小上下文足以复现：input identity、plan/provider/config digest、terminal reason 和高水位。 |
| AC-OBS-011 | observability 失败不会改变重建结果，也不会导致无界 buffer 或崩溃。 |
| AC-PERF-001 | CPU thread count、affinity、NUMA placement 与 memory domain 是 plan 可见、可验证字段。 |
| AC-PERF-002 | 多 scan scheduler 满足所声明的公平策略，并且一个慢 scan 不会无限占用全局资源。 |
| AC-PERF-003 | 每个 parallelization、partition 和 fusion 只在 Provider capability 显式声明后开启。 |
| AC-PERF-004 | 资源 transaction 在并发压力下无 deadlock、leak、credit drift 或 double release。 |
| AC-PERF-005 | GPU DevicePlan 描述 device、stream、buffer、event、fallback 和 resource accounting。 |
| AC-PERF-006 | GPU async cancel、device loss、worker kill、event timeout 后不会重用尚在执行的 device buffer。 |
| AC-PERF-007 | 性能宣称使用冻结 case、目标机、warmup、重复次数、统计方法、原始样本和外部 collector。 |
| AC-PERF-008 | 每项优化先通过数值和资源正确性，再显示相对于固定 baseline 的实测收益。 |
| AC-PERF-009 | 未启用 GPU/NUMA feature 时，默认 CPU path 不携带隐藏依赖或性能回退。 |
| AC-REL-001 | GitHub 或既有 CI 平台在每个受保护变更上执行 format、type registry、unit 和适用 app tests。 |
| AC-REL-002 | main 的合并受 required checks 和 review/branch policy 保护；本地脚本不是唯一质量门。 |
| AC-REL-003 | Linux/Windows 安装树在干净环境运行 help/version/basic smoke，并验证 DLL/SO 依赖闭包。 |
| AC-REL-004 | release manifest 列出 source commit、工具链、dependency lock、Provider bundle、schema/type registry 和 artifact digests。 |
| AC-REL-005 | SBOM、license、hash、签名 policy 和安全扫描有可复现生成与审核记录。 |
| AC-REL-006 | long soak、fault injection、benchmark、memory diagnostics 和 static analysis 在声明范围内通过。 |
| AC-REL-007 | 文档链接、CLI reference、configuration reference、known limitations 和 mode claim 与实际功能一致。 |
| AC-REL-008 | 版本、upgrade/rollback、config migration 和 data retention 有明确 pre-release policy；不得暗中保留兼容层。 |
| AC-REL-009 | release candidate 对已知风险、未启用 feature、硬件 envelope 和非临床声明有明确清单。 |
| AC-REL-010 | 只有全部适用 AC 通过才能在相应 mode 下标记 qualified。 |

---

## 8. 实施纪律

### 8.1 每个工作项的最小交付

每一个工作项必须产生以下可审计结果：

1. 一个清晰的需求和范围边界；
2. 受影响的源文件、schema、Provider/CLI/API surface；
3. 正向测试、负向测试和至少一个不变量；
4. 必要的文档和 fixture；
5. 实际执行的验证命令和结果；
6. 本文件进度表与变更日志的更新。

如果无法在一个可审查变更中完成，拆分任务，但新任务必须有独立的输入、输出、验收和依赖；不要以“后续补测试”作为默认计划。

### 8.2 代码和文档变更规则

- 改 schema 时，必须同步检查 schema reader、canonical JSON、model、compiler/verifier、fixtures、文档和生成物。
- 改 TypeRef 时，必须同步更新 registry、generated headers、Provider contracts、runtime matcher、tests 和 type registry check。
- 改 Provider 时，必须同步更新 contract、catalog、CMake、bundle install list、SDK conformance、provider test 和 README。
- 改 runtime terminal/resource 语义时，必须同步更新 plan verifier、RunRecord、metrics、fault tests 和文档。
- 改 CLI 时，必须同步更新 CLI11 declaration、help、JSON envelope、exit code、app test 和 docs。
- 任何 Gateway Profile、Connector、session、transport、数据保留或采集控制变更都必须服从 P5-008 架构和对应 P5 工作项。不得绕过 P5-009 的公开 profile 冻结，或把厂商协议/私有 wire/设备控制塞入 runtime、Provider 或 app。

### 8.3 任务完成时的提交边界

一个提交宜对应一个工作项或一个可独立验证的子工作项。提交中必须包含代码、测试和本文件状态更新；不把格式化全库、无关重命名、vendor payload 或用户已有改动混入。若任务需要多个提交，第一个提交不得把公开 API 留在不编译或不安全的半状态。

### 8.4 文档完整性规则

P0-004 通过前，任何文档链接不得假定存在。当前已观察到 docs/conventions/README.md 对 reconstruction_state.md、release.md、benchmark_reports 和 research_reports 的引用需要逐项核实。若这些文件不是当前产品所需，删除链接；若需要，则同一工作项中创建实际内容并加链接检查。

---

## 9. 阶段门禁和依赖顺序

| 阶段 | 目标 | 入口条件 | 退出条件 |
| --- | --- | --- | --- |
| P0 规范与可信边界 | 建立单一事实来源、环境证据、链接完整性、真实能力矩阵和冲突裁决。 | 本文件和 AGENTS 已存在。 | P0 全部 ACCEPTED；不再有未裁决的规范冲突或失效文档引用。 |
| P1 串行可信离线基线 | 把现有 HDF5 reference 路径变为可复现、可解释、有限范围内正确的开发闭环。 | P0-001、P0-002、P0-004 ACCEPTED。 | Cartesian/non-Cartesian 正常/异常/golden/RunRecord 通过；无范围夸大。 |
| P2 计划编译与独立验证 | 让语义 Pipeline、artifact、compiler、verifier 和 CLI 工具成为可证明一致的边界。 | P1 的 artifact/data 基线 ACCEPTED。 | valid/invalid/property corpus、plan identity、CLI JSON protocol 通过。 |
| P3 有界 CPU runtime | 把现有同步组件收口为由 plan 驱动、资源闭环、终止正确的 generic runtime。 | P2 compiler/verifier ACCEPTED。 | serial equivalence、resource high-water、terminal/fault/lease 证据通过。 |
| P4 Provider 产品化 | 形成可审计的 SDK、bundle、loader、conformance、run record 和隔离路线。 | P3 baseline ACCEPTED。 | trusted development Provider 流程完整；isolated-provider-runtime 的隔离前提可验证。 |
| P5 外部集成网关与可选嵌入 ingress | 将受控外部集成限定在一个公开 Gateway Profile，同时保留独立的 in-process ISMRMRD feed 路径。 | P5-008 架构设计只依赖 P0-005；任何外部实现还需 P0-006、P1/P2/P3 的对应强依赖和已冻结 profile。 | gateway mode 只有 P5-013 与适用 P7 gate ACCEPTED 后才可宣称；不实现厂商采集、私有协议或未批准的上游控制。 |
| P6 并行、NUMA、GPU 与性能 | 以 feature-gated 方式按能力增加并行性和硬件利用。 | P3 的 bounded correctness ACCEPTED；MachinePolicy 已填写。 | 每个启用 feature 均有正确性、资源、cancel 和性能证据。 |
| P7 Qualification 与发布 | 让质量门在远程 CI、可安装包、长期运行、供应链和文档中可重复。 | P0-P4 ACCEPTED，启用的 P5/P6 工作项 ACCEPTED。 | 适用 AC-REL 全通过，mode claim 与证据一致。 |
| P8 离线可视化与检查工具 | 交付用户授权的 Qt 本地桌面检查器，保持 `.mrd` / PipelineDefinition 的唯一语义边界。 | P0-002、P1-002、P2-002 ACCEPTED；Qt dependency、插件部署和数据边界先冻结。 | `AC-VWR-001` 至 `005` 通过；它是独立附加能力，不改变或阻塞 P0-P7/v1 release gate。 |

阶段不能通过“多数任务完成”进入下一阶段。一个 BLOCKED 的强依赖会阻止其所有后续强依赖；只有明确定义 activation predicate 的 NOT_APPLICABLE 才能解除该依赖。

---

## 10. 可执行工作目录

本节定义任务内容；**唯一可变状态在第 12 节执行台账**。每个任务均允许更新本文件的自身台账、直接相关测试、必要 CMake 注册和直接相关文档。超出列出的主路径时，先在台账 Notes 写明原因和新增影响面。

### P0：规范、基线和工程治理

| ID | 目标和输出 | 主路径 | 依赖 | 验收和验证 |
| --- | --- | --- | --- | --- |
| P0-001 | 建立每项现有能力的事实矩阵：implemented、tested、system-tested、performance-tested、qualified 分离。输出为本文件第 12 节的 baseline evidence 行和能力状态。 | 根 README、AGENTS、docs/architecture、apps、libs/recon、providers、tests、CMakePresets。 | 无。 | AC-BLD-001、AC-ART-004；执行关键 configure/build/CTest，不能运行的环境逐项 BLOCKED。 |
| P0-002 | 验证 Linux/Windows toolchain、Conan、Git LFS、preset、install 和 check script 的实际可用性；形成机器可读/可复制命令记录。 | tools/devenv、tools/checks、CMakePresets、conan profiles、third_party/intel。 | P0-001。 | AC-BLD-001 至 003；Linux 必跑 ci_unit，Windows 至少 bootstrap/configure/install smoke 或准确 BLOCKED。 |
| P0-003 | 固定本文件为唯一状态账本，建立状态转换检查和恢复流程；禁止重新引入无同步机制的第二 TODO/工作项系统。 | AGENTS、docs/architecture、可选 tools/checks。 | 无。 | 第 0、12、13 节完整；AC-BLD-004；对任一任务能从状态、依赖和下一步恢复。 |
| P0-004 | 修复或删除所有断链、缺失目录、过时 app 角色描述和无效构建命令；增加轻量 link/path check。 | docs/README、docs/conventions、docs/architecture、README、tools/checks。 | P0-001。 | AC-BLD-004；链接检查必须能在无网络情况下发现相对路径错误。 |
| P0-005 | 解决规范冲突，至少包括 plain-text core diagnostics 对 structured log 主张、采集/transport/gateway scope claim、profile 名称和 artifact authority。每项冲突只留一个规范。 | AGENTS、docs/architecture、apps README、schemas README、tests。 | P0-001、P0-004。 | AC-ART-004、AC-OBS-007、AC-REL-007；冲突扫描无双重强制语义。 |
| P0-006 | 建立 MachinePolicy、ISMRMRD reconstruction-case 及 Gateway integration policy 参数登记：数据形状、算法配置、CPU/NUMA/GPU、SLO、隐私、Provider trust、输出、公开 profile、TLS/身份、Connector、网络区和保留 policy；不得登记框架 channel 上限或厂商采集链路参数。 | schemas、docs/architecture、本文件第 6.3 节、部署 config。 | P0-001。 | 参数均有 source/owner/review date；缺失参数明确 BLOCKED，不猜测默认值。 |
| P0-007 | 建立远程 CI 和分支保护计划；若用户授权，实际创建 CI workflow/现有 runner pipeline 和 required checks。 | .github 或现有 CI 目录、tools/checks、GitHub settings。 | P0-002。 | AC-REL-001、002；无远程写入授权时只产出设计和 BLOCKED 证据。 |
| P0-008 | 将本文件升级为可快速查看的 Master Plan：从唯一台账派生阶段完成度、当前项、READY/阻塞项和最近证据，并以离线检查器阻止其与台账漂移。 | 本文件、README、docs/README、tools/checks。 | P0-001。 | 总览只读派生自第 12 节，不复制单项状态；检查器验证第 10/12 节 ID 集合、状态、唯一 READY/活动项、READY 依赖、入口链接和总览一致性。 |
| P0-009 | 固化双仓库数据边界：KSpaceJet 不保存 MRI 原始重建数据；开发工作区必须与同级 `KSpaceJet-ismrmrd-data` 仓库配套，后者是唯一 raw ISMRMRD dataset 归属。删除旧 project-internal research raw-data 目录及专用 downloader/test，不迁入数据仓库。 | AGENTS、README、tools/devenv、tools/checks、research/benchmarks、canonical plan。 | P0-003。 | 离线检查必须拒绝 KSpaceJet 已跟踪或物理存在的 `.mrd`、`.h5`、`.hdf5`、`.ismrmrd` payload，并验证同级数据仓库、origin 与 manifest 结构；Linux/Windows pre-commit 都必须执行该检查，所有旧 local-data 引用必须清除，文档给出可复制布局。 |
| P0-010 | 将宿主机 `just` 与根 `justfile` 定义为 Linux/Windows 共享的 prepare、incremental build、install、format 和 check 入口；Linux bootstrap 直接用 apt 确保安装，Windows bootstrap 在缺少时用 winget 安装。 | `justfile`、`tools/devenv`、`.vscode/tasks.json`、`.githooks`、README、docs/conventions、tools/checks。 | P0-003。 | 不得下载项目私有 `just`、校验、缓存或版本锁定 `just`；Linux bootstrap 必须直接使用 `sudo apt-get update` 和 `sudo apt-get install --yes --no-install-recommends just`，由 apt 的幂等性处理已安装 package；Windows bootstrap 必须只在缺少时以 `winget install --id Casey.Just --exact` 安装；VS Code 与 Git hook 均须选择同一 `justfile` recipe；Linux 必须以宿主机 `just` 实际 bootstrap、解析/格式化 justfile 并运行代表性 recipes。Windows 接线须静态检查，真实 Windows 运行证据纳入 P0-002。 |

### P1：可信离线 reference 基线

| ID | 目标和输出 | 主路径 | 依赖 | 验收和验证 |
| --- | --- | --- | --- | --- |
| P1-001 | 冻结公开/合规 HDF5 fixture manifest，包含数据来源、SHA-256、encoding、预期 terminal、golden tolerance 和可再生成方式。 | tests fixtures、tools、docs、ISMRMRD reader。 | P0-002、P0-006。 | AC-DAT-001 至 004、AC-REF-001/008；生成和验证必须不依赖私有数据。 |
| P1-002 | 定义并实现唯一的 runtime-owned 标准 ISMRMRD HDF5 I/O 边界：`IsmrmrdHdf5ReplaySource` 将输入重放到 runtime，`IsmrmrdImageArtifactSink` 固定 dataset/image series、ImageHeader 映射、float magnitude image data 和 MetaAttributes provenance 后终结输出；CLI 只绑定路径，Provider 不得直接读写文件。直接替换并删除 native-endian row-major `.f32` 与 JSON sidecar。 | recon-runtime I/O、apps/kspacejet-recon、tests、docs。 | P0-005。激活前提：ADR-006 已冻结格式与边界；本项只用 test-time generated synthetic ISMRMRD HDF5 image 验证，不依赖 P1-001 的公开 raw-fixture manifest。 | AC-DAT-001、AC-ART-007/008、AC-REF-002/010/011；官方 ISMRMRD binding roundtrip/interoperability、source/sink focused tests、三条 route 和 CLI negative test 通过。完整 input/pipeline/config/result/timing identity 与 RunRecord 关联仍唯一归属 P1-006，不能以本项的 image-bound provenance 取代。 |
| P1-003 | 审计并验收 2-D fully sampled Cartesian RSS reference；把支持矩阵与 IFFT/RSS/orientation/scale golden 固定下来，并确保 reference route 不含 channel-count cap。 | cartesian_rss_hdf5、cartesian Provider、coil combine、tests。 | P1-001、P1-002、P1-007。 | AC-REF-001 至 007；执行 Cartesian focused CTest、normal/optional branch/negative cases。 |
| P1-004 | 审计并验收 2-D non-Cartesian direct-adjoint RSS reference；明确无 DCF、trajectory correction、SENSE 的界限。 | noncartesian_rss_hdf5、noncartesian Provider、coil combine、tests。 | P1-001、P1-002。 | AC-REF-008 至 013；执行 non-Cartesian focused CTest 与 golden compare。 |
| P1-005 | 实现或收口 scan/frame completion、classification、key separation、incomplete/duplicate/missing acquisition terminal semantics。 | acquisition_classification、host_frame_assembler、cartesian_frame_slot、scan_lifecycle、tests。 | P1-001、P0-006。 | AC-DAT-005、006、008、AC-RT-011/012；resource 和 terminal 负向测试通过。 |
| P1-006 | 将每次离线 run 的 input、plan/verifier、Provider/config、terminal、output hash 写入最小 RunRecord，并提供验证读取；持久化 image 不得借此重新引入 JSON sidecar，必要的 image-bound provenance 必须使用 ISMRMRD MetaAttributes。 | recon-model run_record、recon-runtime、apps、schemas、tests。 | P1-002、P1-003、P1-004、P2-001。 | AC-ART-003、006 至 008、AC-OBS-001 至 004。 |
| P1-007 | 删除 reference CLI 的 1 至 64 通道上限，并保证 ISMRMRD schema、generic reader、CLI、ScanDescriptor、planner 和 runtime 对任意正整数 channel count 走同一 generic path。算法特有限制只可由 Provider contract 声明。 | ScanDescriptor、ExecutionPlan、FrameSlot、CLI、fixtures、benchmarks。 | P0-006、P1-001、P1-005。 | AC-DAT-004/007、AC-PERF-001/004；资源不足以 bytes/work 明确失败，绝不因 channel count 失败。 |
| P1-008 | 建立统一 ISMRMRD semantic-frame ingress，使 HDF5 reference route 从同一 header/layout/flag/classification/frame-key/materialization 核心进入有界组帧；这是完成 sibling data repo 所有真实重建 Provider 的共同基础，而不是兼容层。 | kspacejet-ismrmrd、recon-runtime、cartesian/noncartesian HDF5 routes、synthetic tests。 | P0-002、P0-005、P0-009。 | AC-DAT-001、003、005、006、008 至 010；必须让当前 Cartesian route 首先切换到统一入口，不能把真实数据逻辑继续塞入 CLI 或单一路线。 |
| P1-009 | 完成 development-only 的 2-D radial gridding 与显式 analytic DCF Provider。保留现有 direct-NUDFT/direct-adjoint 作为独立 oracle；新增 `radial-rss` 显式路线，不把它伪装成旧 `noncartesian-rss` 的兼容或别名行为。 | kspacejet-nufft、kspacejet-noncartesian-recon Provider、recon-runtime radial route、ksj-recon、synthetic tests。 | P1-008。 | AC-REF-008 至 011、013；2-D radial 与 `radial_analytic_ramp` DCF 必须在 contract/config 中显式；gridding/FFT workspace 必须 caller-owned 且有上界；同一 DCF 的 direct NUDFT oracle、determinism、shape/nonfinite/mismatch/workspace negative、Provider contract/catalog/CMake/identity 与新 route 选择均须通过。明确排除 trajectory/phase correction、SENSE、coil compression、3-D/cine/EPI/partial-Fourier/GRAPPA、性能和临床 claim。 |

### P2：图、artifact、compiler、verifier 和 CLI 计划工具

| ID | 目标和输出 | 主路径 | 依赖 | 验收和验证 |
| --- | --- | --- | --- | --- |
| P2-001 | 审计 artifact 权威归属，移除重复/自引用 digest、重复 profile owner 或混合 semantic/physical 字段；所有新字段归属唯一。明确 Pipeline 作者化静态算法配置与 runtime/compiler 从 ISMRMRD 派生的 scan facts/effective binding 的边界。 | recon-model、recon-graph、schemas、pipeline docs、fixtures。 | P0-005、P1-002。 | AC-ART-001 至 005、AC-PLN-001 至 004。 |
| P2-002 | 完成 PipelineDefinition 静态验证、作者可编辑参数与正式 ISMRMRD input profile、Resolver 和 ResolvedPipeline determinism，并让受控 Provider/contract/type/config 诊断可解释。input profile 必须以 auto-or-explicit 的标准 raw-container selection 替代固定 `dataset` group：auto 仅在恰有一个可读 raw container 时成功，多个候选必须确定性失败并要求 pipeline 指定路径。 | pipeline_definition、artifact_json、operator_contract_json、provider binding/resolver、CLI、tests。 | P2-001。 | AC-PLN-001 至 007，以及 AC-CLI-009 的 validate/schema/semantic/resolver 报告层；valid/invalid fixture、参数/profile/解析负例和 JSON output tests。完整 explain/render/dry-run、compiler/verifier/admission 报告仍由 P2-005 接受。 |
| P2-003 | 完成 ExecutionPlan compiler 的资源、terminal、placement、Provider firing、PartitionCapability 与 WorkKey 输出；只能按已声明的并行语义构造实际 ISMRMRD group。 | execution_plan_compiler、synchronous graph compiler、planning inputs、resource vector、Provider contracts、schemas、tests。 | P2-002、P0-006。 | AC-SCH-001/002/006/009/010、AC-PLN-008 至 012、AC-RT-004。 |
| P2-004 | 强化 independent verifier，与 compiler 私有实现隔离；增加 mutated plan、small graph exhaustive/property、非法 partition/resource/terminal 和 differential corpus。 | synchronous graph verifier、plan storage、tests/fuzz tooling。 | P2-003。 | AC-SCH-002 至 010、AC-PLN-013 至 016；compiler/verifier verdict matrix 通过。 |
| P2-005 | 扩展 ksj 的 pipeline validate、explain、render、dry-run；每个命令具有 CLI11 help、text/json、exit code 和 app protocol test。 | apps/kspacejet-cli、recon graph/model、tests/apps、docs。 | P2-002、P2-004。 | AC-CLI-001 至 003、007 至 010。 |
| P2-006 | 建立 schema structural 与 semantic validation 的双层测试规则；schema 合法但 resolver/compiler/verifier 拒绝的 corpus 必须持续存在。 | schemas、fixtures、tests/unit/libs/recon、tools/checks。 | P2-002、P2-004。 | AC-ART-004、AC-PLN-013 至 015；单独 schema pass 不可让测试 green。 |
| P2-007 | 用 PipelineDefinition 驱动 `ksj-recon` 根命令：每次必须接收 `--input <scan.mrd>`、`--pipeline <pipeline.json>` 和 `--output <image.mrd>`；runtime 从 profile 的 auto-or-explicit **standard raw container** selection 选择 ISMRMRD frame adapter、解析 Provider、编译/验证并执行图，再以统一 Sink 写出结果。删除专用 route 命令、caller-supplied DLL/contract flag、`--dataset` 与 C++ 中临时拼接的 PipelineDefinition，不保留兼容路径或额外 `reconstruct` 子命令。 | apps/kspacejet-recon、recon-runtime、recon-graph、provider loader/binding、reference pipeline fixtures、tests、docs。 | P1-002、P1-006、P2-002、P2-004。 | AC-ART-003/006 至 008、AC-CLI-006/009/010、AC-PLN-001 至 016；三条 synthetic reference route 以用户 pipeline 文件端到端通过，single raw container 自动选择、multiple raw containers 的明确歧义失败以及 pipeline 显式 path 选择都须覆盖。 |

### P3：有界 generic CPU runtime

| ID | 目标和输出 | 主路径 | 依赖 | 验收和验证 |
| --- | --- | --- | --- | --- |
| P3-001 | 审计并收口 buffer pool、fixed edge、resource ledger 和 resource-vector ledger 的预留/提交/回滚/释放不变量；ReadyQueue 前必须完成原子 reservation。 | buffer_pool、fixed_buffer_edge、resource ledger、tests。 | P2-003。 | AC-SCH-007/008、AC-RT-001 至 006；成功/失败/cancel/overflow/double-release stress tests。 |
| P3-002 | 固化 current synchronous executor 支持矩阵：仅执行已验证的 ready WorkKey、exact identity cohort、无未声明 fan-out/join/retain/async/device；禁止 runtime 临时扩大 partition。 | synchronous_graph_executor、FiringLease、docs、tests。 | P2-004、P3-001。 | AC-SCH-005/009/010、AC-RT-007 至 010；未知 motif 必须 compile/verifier/runtime fail closed。 |
| P3-003 | 建立 KeyShard activation、calibration gate、ordering、normal flush 和 terminal state machine 的可执行不变量；分离 stateful writer 与 immutable ComputeTask。 | key_shard、calibration_gate/store、scan_lifecycle、executor、tests。 | P1-005、P3-001。 | AC-SCH-003 至 005、AC-RT-003、011 至 015；虚拟时间和 fault corpus。 |
| P3-004 | 收口 host-enforced FiringLease 和 ProviderNodeInstance：输入借用、输出 grant、identity、cancel、terminal 和 background work 全部可强制。 | synchronous_firing_lease、provider_node_instance、provider SDK、test Provider。 | P3-001、P3-002。 | AC-RT-007 至 010、AC-PRV-001/002。 |
| P3-005 | Generic graph 与 serial Cartesian/non-Cartesian oracle 做端到端差分；修复任何 WorkKey、语义、数值或资源不等价。 | serial_cartesian_pipeline、recon runtime、reference providers、tests。 | P1-003、P1-004、P3-002 至 004。 | AC-SCH-009、AC-RT-010、AC-REF-002/010；normal 和 fault path 对比。 |
| P3-006 | 实现 runtime resource/terminal/Provider trace 和 fatal diagnostic；实际 high-water 与 plan certificate 可比较。 | recon-runtime、performance、logging、run record、tests。 | P3-001、P3-003。 | AC-PLN-016、AC-OBS-005/006/010/011。 |

### P4：Provider 产品化、隔离路线与开发者体验

| ID | 目标和输出 | 主路径 | 依赖 | 验收和验证 |
| --- | --- | --- | --- | --- |
| P4-001 | 收口 Provider bundle manifest、catalog、contract、binary、TypeRegistry、dependency、SBOM/hash/signature 的 identity policy。 | provider SDK/loader、providers catalog/contracts、schemas、tests。 | P2-001、P3-004。 | AC-PRV-002、003、AC-REL-004/005。 |
| P4-002 | 强化 loader：ABI/export/architecture/dependency/manifest mismatch 的 deterministic failure 和跨平台 test matrix。 | provider-loader、platform dynamic library、tests/unit/recon。 | P4-001、P0-002。 | AC-PRV-001、002；Linux SO 与 Windows DLL evidence。 |
| P4-003 | 完成 ksj provider init、inspect、doctor、test、package 的单一 SDK 开发闭环；不得复制 runtime。 | apps/kspacejet-cli、sdk/templates/provider、tools、tests/apps。 | P4-001、P4-002、P2-005。 | AC-PRV-004、005、AC-CLI-004 至 006、015。 |
| P4-004 | 将每个 reference Provider 按 functional source layout、contract、catalog、CMake install 和 conformance 测试完整化；contract 必须声明 PartitionCapability，planned interface 不得混入 executable map。 | providers、sdk、tests/unit/providers、docs。 | P4-002、P3-005。 | AC-SCH-001、AC-PRV-003 至 006、AC-REF-013。 |
| P4-005 | 设计并实现 isolated worker/supervisor 路线、trust tier、quota、watchdog、crash reconciliation 和 rolling upgrade。 | recon runtime、process-runtime、platform、schemas、tests/fault. | P4-001、P3-003、P0-006。 | AC-RT-009/014、AC-PRV-006；不能只 kill host 来模拟 GPU safety。 |
| P4-006 | 把 audit、RunRecord、crash breadcrumb、config resolve/explain 和 Provider identity 形成可重放事故证据链。 | run_record、config、crash、logging、CLI、schemas、tests。 | P1-006、P4-001、P3-006。 | AC-OBS-001 至 004、007、010。 |

### P5：外部集成网关与可选嵌入 ISMRMRD ingress

| ID | 目标和输出 | 主路径 | 依赖 | 验收和验证 |
| --- | --- | --- | --- | --- |
| P5-001 | 冻结仅限同进程的 caller-to-framework ISMRMRD feed contract：ownership、callback lifetime、input identity、completion 与 terminal mapping。 | docs/architecture、schemas、io、fixtures。 | P0-005、P0-006、P3-003。 | AC-FED-001 至 004；不得添加 socket、session、transport、gateway 或 source-control 语义。 |
| P5-002 | 实现 feed materialization 和 HDF5/feed equivalence harness；所有异步路径仅持有 host-owned buffer。 | io/recon runtime、schemas、tests/fuzz。 | P5-001、P3-003。 | AC-DAT-001/002、AC-FED-001 至 004；无 borrowed view 跨异步边界。 |
| P5-003 | 实现本地/embedded run lifecycle、admission、run list/show/cancel、RunRecord 和受保护的 control API；它不得承载 raw-data transport。 | apps/kspacejet-recon、recon-runtime、config、tests/apps。 | P5-002、P4-006。 | AC-FED-005 至 008、AC-CLI-014/015。 |
| P5-004 | 历史 scope-closure：将 ksj-gateway、Connector、MRD session、relay、网络 auth 与采集/传输控制从 KSpaceJet 路线图和默认产品 claim 中移除；已由 P5-008 取代。 | AGENTS、docs/architecture、apps/kspacejet-gateway、CMake/docs。 | P0-005。 | 此任务的历史验收为 AC-FED-003、AC-REL-007；不得以它作为当前 gateway 实现依据。 |
| P5-005 | 使本地 admission、internal queue、Provider saturation 与结果 artifact writer 具有有界资源、确定 reject/terminal 和高水位观测。 | bounded edge/ledger、runtime、tests/fault。 | P5-003、P3-001。 | AC-FED-008 至 010、AC-RT-016 至 019。 |
| P5-006 | 建立 HDF5 replay 与 in-process feed 的端到端 equivalence、cancel、Provider crash、restart 和 result idempotence suite。 | replay tooling、runtime、fixtures、tests/system。 | P5-002、P5-003、P5-005、P4-005。 | AC-DAT-002、AC-FED-001/004/007/010、AC-OBS-010。 |
| P5-007 | 扩展 ksj replay、dataset validate、local run 与 run status 命令，只通过共享 runtime/embedded API，不创建第二数据面或 gateway 命令。 | apps/kspacejet-cli、io、tests/apps、docs。 | P5-003、P5-006。 | AC-CLI-011、014、AC-FED-005 至 010。 |
| P5-008 | 重置外部集成边界并冻结 `ksj-gateway` 的候选稳定架构：公开 profile 选择门、Connector 信任边界、连接/scan/运行时生命周期、资源、安全、输出和实施分层。输出 `KSpaceJet_gateway_architecture.md`；不写 listener 或私有协议实现。 | AGENTS、README、docs/architecture、apps README、canonical plan。 | P0-005。 | AC-GWY-001 至 004（架构/边界部分）、AC-REL-007；必须明确当前 scaffold 仍不是服务，并把未决产品输入列为阻塞条件。 |
| P5-009 | 冻结一个可互操作的公开 Gateway Profile：精确标准/版本、TLS transport binding、身份/授权、header-first admission、错误/terminal 映射、input/output schema 与 byte-level conformance corpus。 | gateway contracts/schema、fixtures、docs、tests。 | P5-008、P0-006。 | AC-GWY-001 至 004、009；不允许临时私有 framing、版本协商或 vendor 原始协议。 |
| P5-010 | 实现安全 listener、public-profile decoder/encoder、连接与单 scan session 状态机、认证/授权和有界网络资源；以 fake peer 做协议与负向测试。 | gateway transport/session/orchestrator、platform、config、tests。 | P5-009、P3-001。 | AC-GWY-002、003、005、007、009；现有 blocking IPv4 socket primitives 不可直接充当生产栈。 |
| P5-011 | 将已验证的外部规范化输入接入共享 runtime：host-owned materialization、双账本 admission、ScanLifecycle/RunRecord 映射、cancel/disconnect 与 HDF5 equivalence。 | gateway host、recon-model/graph/runtime、io、tests。 | P5-010、P1-005、P2-004、P3-003。 | AC-DAT-001 至 004、AC-GWY-004 至 006；gateway 不复制 planner/runtime 或把 socket buffer 交给 Provider。 |
| P5-012 | 完成有界 egress、结果交付/失败语义、Connector conformance harness 与部署形态；站点 Connector 仍是独立制品和信任边界。 | gateway egress/contracts、apps、test peer、docs。 | P5-011、P1-002、P4-006。 | AC-GWY-006 至 009、AC-OBS-001 至 011；不承诺 durable recovery、exactly-once、PACS/DICOM 或厂商 SDK。 |
| P5-013 | 对已选 Gateway Profile 做跨平台安装、端到端 fault/slow-peer/fuzz/security/数据治理 qualification，并记录真实互操作证据。 | install、system tests、security/operations docs、qualification evidence。 | P5-012、P7-002、P7-003、P7-005、P7-006。 | AC-GWY-001 至 009、AC-REL-003 至 010；无真实批准的对端、部署和 policy 时保持 BLOCKED。 |

### P6：并行、NUMA、GPU、容量与性能

| ID | 目标和输出 | 主路径 | 依赖 | 验收和验证 |
| --- | --- | --- | --- | --- |
| P6-001 | 按 Provider capability 开放 KeyShard 并行、continuation、bounded batching 与安全 fusion；每个 motif 独立 feature bit，并保持 grouped/ordered WorkKey 语义。 | executor、KeyShard、ExecutionPlan/verifier、Provider contracts、tests。 | P3-002 至 P3-005、P0-006。 | AC-SCH-001/004/005/011、AC-PERF-001 至 004、AC-RT-003/004；未声明 capability 必须被拒绝。 |
| P6-002 | 实现多 scan quota、admission、DRR 或已声明公平策略、cancel storm isolation 和 resource accounting；调度器只能重排合法 ready task。 | scheduler、ledger、runtime、metrics、tests/fault. | P3-001、P3-003、P6-001。 | AC-SCH-007/010/011、AC-RT-016 至 019、AC-PERF-002/004。 |
| P6-003 | 实现 NUMA discovery、memory placement、thread/backend budget、local queue 和 fallback；无 NUMA host 也有 deterministic behavior。 | core memory/threading/performance、recon runtime、MachinePolicy、tests/benchmarks. | P6-001、P0-006。 | AC-PERF-001、002、007、009；目标机与单 NUMA regression 比较。 |
| P6-004 | 设计并实现 GPU DevicePlan、host/device buffer domain、transfer ledger、stream/event ownership 和 CPU fallback；GPU task 只能来自 verified WorkKey，fence/token 进入 ResourceLedger。 | recon model/runtime、Provider SDK/contracts、GPU backend、tests。 | P3-001、P6-001、P0-006。 | AC-SCH-008/011、AC-PERF-005、009；无 GPU 环境不假装通过，保持 BLOCKED。 |
| P6-005 | 实现 GPU async Provider、fence、cancel quarantine、device loss 和 worker crash 回收规则。 | GPU runtime、Provider worker/supervisor、fault tests。 | P4-005、P6-004。 | AC-PERF-006、AC-RT-014；确认 completion 前绝不复用 device memory。 |
| P6-006 | 建立可复现 benchmark harness：machine descriptor、case manifest、warmup、repeat、collector、raw samples、correctness gate 和报告。 | ksj-research、tools、tests/benchmarks、docs/conventions/benchmark。 | P1-003、P1-004、P3-006、P0-006。 | AC-PERF-007/008、AC-OBS-006/008；single smoke 不是性能结论。 |
| P6-007 | 对每个实际 ISMRMRD reconstruction case 做 capacity/quality/performance evidence；256 及更大 channel count 只是普通 case，不能形成框架上限或 gate。 | MachinePolicy、ExecutionPlan、fixtures、benchmarks、docs。 | P1-007、P6-003、P6-006。 | AC-DAT-007、AC-PERF-001/007/008；仅以实际 bytes/work 的本地资源不足拒绝。 |

### P7：Qualification、CI、安装、供应链和发布

| ID | 目标和输出 | 主路径 | 依赖 | 验收和验证 |
| --- | --- | --- | --- | --- |
| P7-001 | 将本地 format/type/unit/app/benchmark gates 映射到远程 CI；启用 PR status、artifact retention 和可信 baseline。 | .github 或 CI 配置、tools/checks、CMakePresets。 | P0-007、P2-006、P3-006。 | AC-REL-001/002；远程 run URL 或 artifact 为证据。 |
| P7-002 | 建立 Linux 和 Windows clean-install、help/version、dynamic dependency closure、Provider load 和 basic reconstruction smoke。 | CMake install、apps、Provider packaging、CI scripts。 | P4-002、P4-003、P7-001。 | AC-BLD-002、AC-REL-003。 |
| P7-003 | 建立 static analysis、memory diagnostics、TSAN 或等效并发检查、fuzz/property corpus 与已知 sanitizer suppression policy。 | CMakePresets、tools/static analysis、tests/fuzz、CI。 | P3-001 至 P3-004、P6-001。 | AC-REL-006；所有 suppression 必须有 issue/expiry/owner。 |
| P7-004 | 完成 SBOM、license、hash/signature、Provider trust policy、LFS payload verification 和 secret/credential policy。 | conan, third_party, provider packaging, docs, CI. | P4-001、P7-001。 | AC-BLD-003、AC-REL-004/005。 |
| P7-005 | 完成长稳、fault injection、input-submission pressure、result-artifact failure、Provider worker failure 和 resource leak report。 | ksj-research、system tests、runtime、CI artifacts。 | P3-006、P4-005、P6-002 至 P6-005。 | AC-REL-006、AC-RT-006；启用 embedded P5 时附加 AC-FED-009/010，启用 Gateway mode 时附加 AC-GWY-005 至 007。 |
| P7-006 | 审核 release docs、CLI reference、mode claim、known limitation、target envelope、non-clinical statement 和 rollback policy。 | README、docs、apps help/tests、release manifest。 | P0-004、P0-005、P7-002、P7-004。 | AC-REL-007 至 009；文档与 executable behavior 一致。 |
| P7-007 | 执行最终 qualification review：列出适用/不适用 AC、风险、残余 BLOCKED 项和 mode。只在证据充分时提升 release profile。 | 本文件、CI reports、release artifacts。 | P0-P4、启用的 P5/P6 和 P7-001 至 P7-006 ACCEPTED。 | AC-REL-010；生成 signed-off qualification report，不得用 README 状态替代。 |

### P8：离线可视化与检查工具（用户授权的附加范围）

| ID | 目标和输出 | 主路径 | 依赖 | 验收和验证 |
| --- | --- | --- | --- | --- |
| P8-001 | 冻结 Qt 6 Widgets desktop viewer foundation：新增 `qt/6.8.3`、`ksj-viewer` 安装应用、CLI11 help/version、实际 QApplication UI smoke 与 Windows Qt platform-plugin deployment；写明唯一 `.mrd`/PipelineDefinition 只读边界。 | conanfile.py、CMake、apps/kspacejet-viewer、tests/apps、install checks、docs。 | P0-002、P1-002、P2-002。 | AC-VWR-001 至 003；Windows Release configure/build/install 和 build/install UI smoke。 |
| P8-002 | 在 `KSpaceJet::ismrmrd` 提供标准 HDF5 inspection reader：Header、streaming bounded Acquisition 与 Image/Header/MetaAttributes read model；不复制 HDF5 实现，不保留 raw payload。 | libs/io/kspacejet-ismrmrd、synthetic tests、docs。 | P8-001、P1-002。 | AC-VWR-004；normal/malformed/oversize/axis/meta fixtures 与 focused tests。 |
| P8-003 | 实现 Qt metadata、k-space、image、pipeline 与 export 视图；共享 P8-002 reader 和 P2 parser，明确采样可视化与重建图像的区别。 | apps/kspacejet-viewer、tests/apps、docs。 | P8-002、P2-002。 | AC-VWR-005；UI smoke、view-model/renderer tests、export provenance/negative tests。 |
| P8-004 | 按用户反馈将 `ksj-viewer` 重构为 HDFView-inspired、MRD-aware Qt desktop workbench：File/Window/Tools/Help、tool/file bar、standard MRD semantic tree、对象 inspector、typed `Inspect`/`Open As…` data views、页化 acquisition/coil/trajectory、`image_x` cine/window-level/zoom/probe/histogram、信息状态区和空状态；打开 MRD 的默认状态仅为语义树加 General/Object Attribute inspector，不显示固定 `Dataset overview` 或 `Image series` dashboard，Header/XML 只能经显式 inspect 打开 XML view，raw source 的零 image series 视为正常；以 HDF Group HDFView 的 UI 与操作逻辑为参考，但独立以 C++/Qt 实现。 | apps/kspacejet-viewer、libs/io/kspacejet-ismrmrd、tests/unit/apps、tests/unit/libs/io/kspacejet-ismrmrd、docs。 | P8-003。 | AC-VWR-006、AC-VWR-007、AC-VWR-008；recursive container/header 与 widget structure/state tests、Windows Release build/UI smoke 和用户视觉复核。 |

### 10.1 明确不在 v1 主线内的项目

除非用户明确新增范围，下列事项不得阻塞 P0 至 P7：

- GUI、移动端、Web dashboard；用户已于 2026-08-23 明确授权的 P8 Qt 离线 `ksj-viewer` 是唯一例外，但它仍不阻塞 P0 至 P7 或 v1；
- DICOM/PACS、诊断工作站、临床流程、医疗器械监管或诊断宣称；
- 云端多节点调度、跨院数据同步；
- 厂商私有 scanner protocol、旧 DPC/BRF/ComQ 兼容；
- 扫描仪/采集卡、FPGA、DMA、PCIe/QDMA、内核驱动、设备 ring、厂商私有 transport、设备 MRD session、Connector vendor SDK 或采集端流控；P5-008 至 P5-013 的公开 Gateway Profile 路线是明确例外；
- 私有或专有重建算法；
- 未被 Provider contract、TargetEnvelope 和 benchmark 证据支持的 GRAPPA、SENSE、partial Fourier、adaptive coil combine、trajectory correction 等算法。

如果未来启动这些能力，必须先在本文件新增一组 feature ID、边界、数据/合规决策、独立 acceptance 和 release profile，不能把它们悄悄塞进 reference Provider。

---

## 11. 任务选择与变更控制清单

每次开始实现前，逐项确认：

- [ ] 当前任务在第 12 节是 READY，且其依赖均为 ACCEPTED 或有已记录的 NOT_APPLICABLE predicate。
- [ ] 本次工作只对应一个任务 ID，已写明 base commit、目标和下一行动。
- [ ] 已读该任务指定的现有代码、tests、schemas 和架构规范。
- [ ] 已检查公开 API、ABI、schema、TypeRef、CLI JSON、artifact digest、Provider/catalog、CMake install 的影响。
- [ ] 已列出最小验证和完成所需完整验证；没有以 schema pass 替代 semantic/runtime test。
- [ ] 没有混入用户已有改动、vendor payload、大范围格式化或后续 feature。
- [ ] 任务如涉及外部 transport、采集 hardware、真实数据、session/gateway/Connector 或协议，已检查是否超出本项目边界并触发第 0.3 节暂停条件。

每次完成/阻塞时，逐项确认：

- [ ] 已运行并记录实际命令、平台、结果和失败输出摘要。
- [ ] 已更新 tests、fixtures、docs、registration 和 code；没有“以后补”的隐含欠债。
- [ ] 已执行 git diff --check，且格式检查范围与改动相符。
- [ ] 已检查所有生成文件；TypeRegistry 变更已运行 generator check。
- [ ] 第 12 节的 Status、Evidence、Known limitations、Next action 和更新时间已同步。
- [ ] 只在第 12 节改变工作项状态；随后运行 `tools/checks/check_execution_plan.py --write` 和 `--check` 同步第 0.4 节只读投影。总览不得反向改变状态。
- [ ] 若 ACCEPTED，所有任务级 acceptance 都有证据；若 BLOCKED，阻塞信息足以使下一位执行者立即复现。

---

## 12. 唯一执行台账

**更新时间**：2026-08-24，`P0-002` 已以用户指定的 Windows Release 路径完成 bootstrap、Conan/CMake configure、四个 application build/install、installed-help/version 和 DLL closure；`P0-007` 因用户要求暂缓 GitHub CI 而 BLOCKED，`P0-010` 因用户暂停 Linux 验证而 BLOCKED，二者均不在当前范围。`P1-002` 的 runtime-owned 标准 ISMRMRD source/sink 边界已通过 focused Windows Release 验证并 ACCEPTED；`P1-008` 的统一 ISMRMRD semantic-frame ingress 与 `P1-009` 的 development-only 2-D radial gridding/analytic DCF Provider 也均已 ACCEPTED。用户已冻结 `ksj-recon --input <scan.mrd> --pipeline <pipeline.json> --output <image.mrd>` 为唯一未来重建入口，并进一步冻结标准-first MRD container 原则：不假设 `/dataset`，任何 private `ksj_*` group 均不得成为查看或重建的前提。`P2-002` 的固定 `dataset` profile 因此 REOPENED，待 P8 的 shared discovery evidence 后改为 auto-or-explicit container selection。用户已明确授权 Qt 6 desktop `ksj-viewer` 作为不阻塞 v1 的附加范围；P8-004 已按 `E:\hdfview` / HDFView 的 UI 与操作原则完成 native C++/Qt shell 重构，并依用户视觉反馈将对象检查器收口为 HDFView 式紧凑双页。Windows unit-test target-only build 现在会部署其运行时 DLL 闭包和 Qt platform plugin，直接启动与裸 CTest 已验证；P8-004 仍待用户视觉复核。
**当前执行任务**：`P8-004`（IN_PROGRESS）。基线 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`；P8-003 保持 ACCEPTED，其数据读取、parse-only pipeline、display-derivative export 和依赖边界不可回退。当前工作树已提供有界 recursive standard container discovery 与 HDFView-inspired File/Window/Tools/Help、toolbar/file bar、semantic tree、`Object Attribute Info`/`General Object Info` inspector、typed Inspect/Open As 和 Info/status shell；focused Windows tests、Release build、install 和 UI/export smoke 均已有 evidence。下一精确行动是使用真实 sibling-data `cart_t1.mrd` 完成用户视觉复核，然后再决定是否转 VERIFYING。
**状态权威**：本节是本文件中唯一允许修改任务状态的位置。任务目录第 10 节不维护重复状态。

### 12.1 P0 台账

| ID | Status | 依赖 | 基线观察/证据 | 下一精确行动 | 已知限制 |
| --- | --- | --- | --- | --- | --- |
| P0-001 | ACCEPTED | 无 | 基线为 `8c31b30419ed330688b3f1b90f14a4498503317d` / tree `4455dc2fb45214952b710fa5519d8d084e9efad5`；同一未提交 code/test/fixture diff 在独立本地 clone 的新 `.venv`/unit build tree 上完成 bootstrap、321-step build、35/35 CTest。主工作树复验也通过；完整证据见第 13.1 节。 | P0-002：验证 Linux/Windows toolchain、Conan、LFS、preset、install 与 check scripts。 | 仅 Linux component evidence；Windows、install、app/system、性能和 qualification 尚未由本项验证。 |
| P0-002 | ACCEPTED | P0-001 | 2026-08-22：真实 Windows x64 已完成 Release-only acceptance。VS Build Tools `17.14.39`/MSVC `14.44.35207`、Windows SDK `10.0.26100.0`、Git LFS、host `just`、Conan/CMake 与 Intel payload 均经实际 bootstrap/verify；`just prepare-release`、`just build-release-applications`、`just install-release-applications` 和 `just check` 均成功。修正 Intel OpenMP library/runtime 目录、MSVC `/Zc:__cplusplus`/`/bigobj`，以及 Windows 安装 DLL 的冲突处理；四个已安装应用 `--help` 与 `ksj --version` 均 exit 0。`dumpbin /DEPENDENTS` 扫描 install tree 的 270 个 EXE/DLL，未发现非安装树或非 Windows 系统 DLL 的缺失依赖。完整证据见第 13.2 节。 | `P0-007`：检查本地 CI baseline，制定最小 CI/branch-protection 设计；外部写入须用户授权。 | 此项只验证 Windows Release developer install smoke，不构成 Debug、性能/容量、Provider isolation、临床或 release qualification 证据。 |
| P0-003 | ACCEPTED | 无 | `e1150f4b24627f5f5b847f57ee4d633a8f8b33c1` / tree `01c12fcd1b52fe45a86a3a26691bcfbf264e6589` 已将 `AGENTS.md`、唯一 execution ledger、检查器和交付文件提交到当前实际仓库；完整证据见第 13.10 节。 | 无 READY 项；维持唯一台账并在后续状态变更后运行 `check_execution_plan.py --write --check`。 | 当前只有本地 commit，未推送/创建远程 branch；但 P0-003 的本地仓库交付/commit 验收已满足。 |
| P0-004 | ACCEPTED | P0-001 | 基线为 `8c31b30419ed330688b3f1b90f14a4498503317d` / tree `4455dc2fb45214952b710fa5519d8d084e9efad5`，并包含未提交的 P0-001/P0-002 evidence diff。已修复五个真实本地 Markdown 断链、过时路径/命令和应用角色说明；新增离线 link/path gate，最终扫描为 75 个 Markdown 文件、151 个本地 link，零错误；完整证据见第 13.6 节。 | P0-005：列出冲突原文，并同一变更更新全部主规范、help 和测试。 | 外部 URL 不联网验证；当前 Linux 主机不能执行 Windows PowerShell hook。remote merge enforcement 仍属于 P0-007。 |
| P0-005 | ACCEPTED | P0-001, P0-004 | 基线为 `8c31b30419ed330688b3f1b90f14a4498503317d` / tree `4455dc2fb45214952b710fa5519d8d084e9efad5`，并包含未提交的 P0-001/P0-004 diffs。五个 canonical profile、schema/fixture/test、active help、artifact authority、plain-text diagnostics 和历史文档边界已原子收口；完整证据见第 13.7 节。 | P0-006：只收集真实 TargetEnvelope/MachinePolicy 参数或精确记录缺失输入。 | P4/P5/P7 mode 仍未接受；P0-002 的 Windows Release evidence 不扩大任何能力宣称。 |
| P0-006 | BLOCKED | P0-001 | 基线为 `8c31b30419ed330688b3f1b90f14a4498503317d` / tree `4455dc2fb45214952b710fa5519d8d084e9efad5`，并包含 P0-001/P0-004/P0-005 的未提交改动。第 6.3.1/13.8 节证明没有 committed、deployment-owned TargetEnvelope 或 MachinePolicy；fixture、reference defaults、research case 和本机硬件均已明确降级为非产品证据。2026-08-23 用户新增真实 Gateway 方向后，第 6.3.1 已追加公开 profile、PKI、Connector、网络区、egress 和保留 policy 的缺失输入。 | 收集第 6.3.1 所列 case、deployment、performance、data-governance、output、security/release、architecture 及 GWY-DEC-001 至 007 owner/source/scope/review inputs；收到后重新打开 P0-006 并按每项复核。 | 不得猜测 channel 上限、SLO、GPU 配置、Gateway Profile、TLS/PKI、Connector 或数据保留；不把 test fixture、reference-route value 或当前 Linux host 自动提升为产品 envelope。 |
| P0-007 | BLOCKED | P0-002 | 2026-08-22：已在隔离 branch `codex/p0-007-release-ci` 创建并推送 Release-only workflow，PR `#1` 曾产生实际 `linux-release-quality` 与 `windows-release-build-install` contexts；历史实现/失败记录保留在第 13.13 节。2026-08-23 用户明确要求“现在先跳过所有 GitHub CI 相关的内容”，因此停止把任何 run、PR、workflow 或 protection 状态当作当前工作。 | 等待用户明确恢复 GitHub CI 工作；恢复后先只读复核 remote branch、PR、workflow run 与 `main` protection 的实际状态，再决定是否继续 P0-007。 | 暂不监控、取消、触发或修改远端 workflow/run/PR/branch protection，也不推送任何与 GitHub CI 有关的本地改动；既有远端状态不是 ACCEPTED 证据。 |
| P0-008 | ACCEPTED | P0-001 | 用户要求的 Master Plan 视图已在第 0.4 节作为受控派生总览落地：显示阶段覆盖度、当前/READY/阻塞项、最近验收证据和阻塞恢复动作；完整证据见第 13.9 节。第 12 节仍是唯一状态权威。 | 后续状态变更只改第 12 节，然后运行 `python3 tools/checks/check_execution_plan.py --write` 和 `--check`；无 READY 时保持无下一项。 | 总览不能直接改变状态；P0-002 已有 Windows Release evidence，但 P0-006 的参数 authority 仍 BLOCKED，且当前 Linux 主机无法执行 Windows hook。 |
| P0-009 | ACCEPTED | P0-003 | 已确立并验证双仓库 raw-data contract：KSpaceJet 无原始 MRI payload，唯一外部数据归属为同级 `KSpaceJet-ismrmrd-data`。旧 `research/benchmarks/datasets/`、专用 downloader/test 已按用户授权删除（先移入 Trash）；完整证据见第 13.11 节。 | 无 READY 项；后续开发保持 sibling workspace，预提交自动检查 `check_workspace_layout.py`，数据仓库完整性在 sibling 中运行 `tools/verify-data.sh`。 | Windows PowerShell hook 未在本 Linux host 执行；其静态接线已与 Linux 同步，实际 Windows toolchain/host 证据仍只属于 P0-002。 |
| P0-010 | BLOCKED | P0-003 | 基线 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`，开始时有 26 个用户/先前工作树条目，均须保留。2026-08-23 已确认 Docker `29.7.2` 的 Linux/amd64 backend 可用，并已保留命名、非自动删除的 `kspacejet-linux-test`；它以 `/source:ro` 挂载仓库，在容器自己的工作副本中运行。缺失 `just` 的首次 apt bootstrap、第二次 apt 幂等 bootstrap 与 direct host `just --unstable --fmt --check` 已实际通过；后续 Linux Intel Git-LFS 传输在用户指示“不用处理 Linux”后停止，容器保留且未删除。 | 等待用户明确恢复 Linux 验证；恢复后仅在 `kspacejet-linux-test` 中继续，先确认 LFS payload，再执行 `just prepare-release` 与 `just check`，不得写入当前 Windows worktree、raw-data sibling 或任何 GitHub CI 资源。 | 用户当前明确暂缓 Linux；因此所需的 Linux `prepare-release`/`check` 全链路未完成，P0-010 不得 ACCEPTED。 |

### 12.1.1 P0-001 基线能力矩阵（2026-08-20，ACCEPTED）

| 能力范围 | implemented | tested | system-tested | performance-tested | qualified | 当前结论 |
| --- | --- | --- | --- | --- | --- | --- |
| Linux 构建、TypeRegistry 与已覆盖 recon 组件 | 是 | 是：独立本地 clone 的 Linux unit 配置、321-step build、35/35 CTest、TypeRegistry check。 | 否 | 否 | 否 | L2，仅 Linux unit/component 范围；clone 使用已存在的本机 LFS/Conan 缓存，不是远端传输或 cold-cache 证据。 |
| Pipeline schema、resolver 与 compiler 语义边界 | 是 | 是：两个 schema-valid fixture 分别由 resolver 与 compiler 拒绝；compiler focused GTest 通过。 | 否 | 否 | 否 | L2 组件；schema pass 不是 executable acceptance。 |
| bounded runtime、HDF5 replay、loader 与仓内 Provider | 是 | 是：全量 unit suite 和 `recon` label group 覆盖其组件边界。 | 否 | 否 | 否 | L2 组件；loader 仍为 in-process，不能推导 fault isolation。 |
| offline CLI/reference route | 是 | 仅相关 runtime/component test surface；未在本项运行正向 application protocol、golden artifact 或完整 CLI route。 | 否 | 否 | 否 | L1，不宣称离线产品闭环。 |
| gateway、research 与 online/transport | scaffold 或不在产品范围 | 否 | 否 | 否 | 否 | 不构成 gateway、online service、relay 或 transport 能力。 |
| 性能、容量与发布资格 | benchmark/基础代码可观察 | 无 reconstruction workload、target machine、长期运行或 release evidence。 | 否 | 否 | 否 | 不作吞吐、延迟、256-channel、production-ready 或 clinical claim。 |

2026-08-23 范围修订：上表关于 gateway “不在产品范围”的历史判断由 P5-008 取代；其
scaffold/无测试/无系统证据的观察仍然有效，不能被新架构解释为已实现能力。

当前 Cartesian 与 non-Cartesian reference route 的 `kMaximumChannels = 64` 是 P1-007 必须移除的临时实现限制，不是框架容量声明；本矩阵不把它解释为 64-channel 支持或 256-channel 反证。

### 12.2 P1 台账

| ID | Status | 依赖 | 基线观察/证据 | 下一精确行动 | 已知限制 |
| --- | --- | --- | --- | --- | --- |
| P1-001 | PLANNED | P0-002, P0-006 | HDF5 reader 与测试存在；fixture manifest 的合规/identity 状态未审计。 | 清点 tests 中 HDF5 fixture，建立 manifest draft。 | 不得上传/使用真实患者数据。 |
| P1-002 | ACCEPTED | P0-005 | 基线 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`；ADR-006 已按用户指令冻结唯一 ISMRMRD I/O 格式与 runtime source/sink 责任。P1-001 仍等待 P0-006 的公开 raw-fixture/data-governance authority，但本项只以 test-time generated synthetic ISMRMRD HDF5 image 做互操作验证，因而不绕过该依赖。Source/Sink、三条 route、CLI 负例、官方 binding 读回和既有 destination 原子替换均已通过；完整接受证据见第 13.17 节。 | `P2-001`：冻结 authored Pipeline、scan-derived facts、effective binding 与 digest 的唯一归属，为无专用子命令的通用重建入口建立前置模型。 | 仅开发期 offline image profile；不接受 public external consumer、partial/callback、retention/access、deployment durability、performance、release 或 clinical claim。ADR-007 已冻结未来产品入口必须以 PipelineDefinition 驱动；当前专用 route façade 仅为待 P2-007 替换的开发期实现，不能被当作长期 `ksj-recon` 契约。 |
| P1-003 | PLANNED | P1-001, P1-002 | Cartesian IFFT2 plus RSS 和 optional conditioning 有实现/测试。 | 读取 route README/contract/fixtures，制作 support matrix 与 golden test plan。 | 当前 route 仅是 development reference。 |
| P1-004 | PLANNED | P1-001, P1-002 | non-Cartesian adjoint plus RSS 有实现/测试。 | 制作明确的 non-DCF/non-SENSE support matrix 与 golden test plan。 | 不得宣称完整 non-Cartesian clinical reconstruction。 |
| P1-005 | PLANNED | P1-001, P0-006 | FrameSlot、classifier、assembler、scan lifecycle 已有源码/测试。 | 以 missing/duplicate/incomplete corpus 审计 completion/resource 分离。 | 现有 coverage 需实际运行验证。 |
| P1-006 | PLANNED | P1-002, P1-003, P1-004, P2-001 | RunRecord model/schema 及测试存在。 | 在 P2-001 已冻结 input/Pipeline/effective binding identity 后，核对离线 applications 是否每 run 写出完整 record。 | source 有实现不代表 CLI 已交付 artifact。 |
| P1-007 | PLANNED | P0-006, P1-001, P1-005 | 当前 Cartesian CLI 对 physical channel 采用 1 至 64 的临时 reference 限制；这与框架无通道上限的产品边界冲突。 | 删除该限制，建立任意正整数 channel generator/property corpus，并验证资源错误以 bytes/work 报告。 | reference Provider 可能仍有其自身算法 layout 限制，必须声明在 Provider contract。 |
| P1-008 | ACCEPTED | P0-002, P0-005, P0-009 | 基线 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`。共享 ingress 已集中 header/layout/finite/flag/classifier/frame-key 语义，generic replay、Cartesian RSS、non-Cartesian RSS 均已迁移；full FrameKey 包括 encoding/slice/contrast/repetition/set/phase/average/segment，binding resolver 不能覆盖它。Windows Release runtime target 已涵盖 exact required-index completion、EndOfInput incomplete terminal、duplicate/missing、bad trajectory/layout 与新 ingress 负向例；完整证据见第 13.15 节。 | 当前无 READY 项；等待 `P0-006` 收齐 TargetEnvelope/MachinePolicy authority 后按依赖重新选择。 | borrowed HDF5 view 只在 callback 内；路由在该边界同步复制到各自有界 host-owned destination，未引入无用途的第二通用 staging buffer。`P1-009` 已在此共同入口上另行验证一个 sibling radial HDF5 的 development-only 运行；当前 non-Cartesian direct-adjoint 仍是无 DCF reference，P0-006 继续阻塞产品/性能/临床 claim。 |
| P1-009 | ACCEPTED | P1-008 | 基线 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`，开始时已有 P1-008 及用户/先前工作树改动，均已保留。新增独立 `radial_gridding_reconstruct`（`radial_analytic_ramp`，内部仅 `radians_per_pixel`）与 `radial-rss` route；旧 direct-adjoint 保持独立 reference，带相同 DCF 的 direct NUDFT 仅作为 gridding oracle。Windows Release synthetic/Provider/app/install 验证和 sibling `zen-2d-radial-2025` 的显式 `encoded-matrix-index` 当前二进制运行均通过；完整证据见第 13.16 节。 | 当前无 READY 项；等待 `P0-006` 收齐 TargetEnvelope/MachinePolicy authority 后按依赖重新选择。 | 仅 development-only 2-D radial；旧 `noncartesian-rss` 语义未改，不扩展为 spiral、trajectory/phase correction、SENSE、coil compression、3-D/cine/EPI/PF/GRAPPA。sibling raw payload 仅经显式路径只读用于本项 development-only execution evidence；不得复制、symlink、vendor、Git-LFS 跟踪或纳入本仓库。P0-006 仍禁止产品 case、正式 artifact、性能和临床/发布 claim。 |

### 12.3 P2 台账

| ID | Status | 依赖 | 基线观察/证据 | 下一精确行动 | 已知限制 |
| --- | --- | --- | --- | --- | --- |
| P2-001 | ACCEPTED | P0-005, P1-002 | 基线 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`；以 runtime-owned `ScanFacts`、`EffectivePipelineBinding` 和 in-memory `PlanBuildRequest` 直接替换 caller-supplied digest tuple/portable PlanBuildRequest artifact。Pipeline 的静态算法配置与 runtime-derived scan facts/effective config 分离；ExecutionPlan 固定引用 resolved pipeline、scan facts、effective binding、TargetEnvelope 和 MachinePolicy 的唯一 identity。Windows Release recon CTest 6/6、`ksj_recon` 链接、type registry、JSON syntax、workspace/link/plan/diff 与 focused format 均通过；完整证据见 13.18。 | `P2-002` 已解锁：将作者可编辑参数、正式 `ismrmrd` input profile 与受控 Provider/contract resolver 从当前保留字段提升为可验证语义。 | 不引入 compatibility field、用户手填扫描尺寸、Provider module/contract 路径、输出路径或物理运行时策略；P2-007 前仍保留临时专用 CLI façade。 |
| P2-002 | REOPENED | P2-001 | 原 2026-08-23 Windows Release parameter/profile/resolver evidence 保留，但用户随后冻结“任意标准 MRD container、非固定 `/dataset`”原则，现有 `inputProfile.dataset_group == "dataset"` 结构/schema/parser 已不再符合 ADR-011。P8-004 先交付 shared bounded discovery/classification evidence；随后本项以 direct replacement 将 profile 改为 auto-or-explicit standard raw-container selection，并补 schema/semantic/CLI negative corpus。 | P8-004 接受后，替换 `dataset_group` profile 语义、schema、canonical JSON、fixtures、parser/resolver 与 docs；`auto` 仅允许唯一 raw candidate，multiple raw candidates 必须要求 pipeline 的 explicit absolute container path。 | 原有 parameter/binding/resolver evidence 不可被用于证明新的 container selector。不得保留 `dataset_group`/固定 `dataset` 兼容字段、不得把 group path 移到 CLI、不得以私有 `ksj_*` group 作为输入。 |
| P2-003 | PLANNED | P2-002, P0-006 | synchronous graph compiler/ExecutionPlan/resource vector 已存在；尚未证明 PartitionCapability/WorkKey 是权威输入。 | 先形成 Provider contract → ScanDescriptor → WorkKey → reservation → ExecutionPlan 字段矩阵与 valid/invalid fixture。 | 不得假定 GPU/parallel feature 已可用，也不得从 header 自动推断独立性。 |
| P2-004 | PLANNED | P2-003 | independent verifier 和 extensive tests 已存在。 | 检查 verifier 是否不依赖 compiler 私有状态；添加 undeclared partition、fake Cartesian WorkKey、missing join/reservation 的 mutated-plan matrix。 | 未跑 tests。 |
| P2-005 | PLANNED | P2-002, P2-004 | ksj 目前只含 pipeline validate/provider init。 | 按 CLI11/API boundary 分别实现 planned tool，不复制 runtime。 | 新 command 不可显示为已可用直到 app tests 通过。 |
| P2-006 | PLANNED | P2-002, P2-004 | schemas README 已声明结构和语义验证分层。 | 增加 schema-valid/semantic-invalid corpus inventory 和 CI gate。 | 当前 corpus completeness 未知。 |
| P2-007 | PLANNED | P1-002, P1-006, P2-002, P2-004 | 当前 `ksj-recon` 的 `cartesian-rss`、`noncartesian-rss`、`radial-rss` 命令从 caller-supplied module/contract path 和 route flags 在 C++ 中临时拼接 PipelineDefinition；用户不能编辑它，且现有 route config 混入 scan-derived facts。 | 以必选 `--input`、`--pipeline`、`--output` 的 `ksj-recon` 根命令替换三条专用命令；建立三条 user-editable reference pipeline fixture、runtime input profile adapter、受控 Provider binding、source/sink binding 与端到端/负例测试。 | 不得保留旧 CLI aliases、额外 `reconstruct` 子命令或允许 `.mrd` 单独重建；P2-001/002/004/P1-006 未 ACCEPTED 前不得开始实现。 |

### 12.4 P3 台账

| ID | Status | 依赖 | 基线观察/证据 | 下一精确行动 | 已知限制 |
| --- | --- | --- | --- | --- | --- |
| P3-001 | PLANNED | P2-003 | buffer pool、fixed edge、resource ledger 和相应 tests 已存在。 | 先执行 focused runtime tests，按每一个 WorkKey/task 的 reserve/charge/release/cancel path 建资源守恒表。 | synchronous implementation 不等于多 scan safe。 |
| P3-002 | PLANNED | P2-004, P3-001 | synchronous executor/FiringLease 已存在，当前明确是受限 motif。 | 固化“只执行 verified Ready task”的矩阵：dynamic edge/join/fan-out/async、runtime 不得临时扩展 partition。 | 不得扩大语义仅因 code 接受输入。 |
| P3-003 | PLANNED | P1-005, P3-001 | KeyShard、calibration gate/store、scan lifecycle 已有源码。 | 执行/扩充 FrameSlot seal、calibration epoch、ordered/window predecessor、single-writer KeyShard、cancel/normal flush corpus。 | 需要实体 tests 后才可自称 terminal-closed。 |
| P3-004 | PLANNED | P3-001, P3-002 | host-enforced FiringLease、ProviderNodeInstance 与 test Provider 存在。 | 审计 retain/double commit/out-of-bound/illegal terminal tests。 | in-process crash 仍与 host 同故障域。 |
| P3-005 | PLANNED | P1-003, P1-004, P3-002, P3-003, P3-004 | serial Cartesian path 和 generic graph executor 均存在。 | 建立同 fixture 的 WorkKey/parallel-plan versus serial differential harness。 | non-Cartesian serial/generic equivalence 要先明确。 |
| P3-006 | PLANNED | P3-001, P3-003 | performance/logging/run record 基础库存在。 | 设计 plan versus actual high-water trace field map。 | 当前日志/metric conflict 由 P0-005 先消除。 |

### 12.5 P4 台账

| ID | Status | 依赖 | 基线观察/证据 | 下一精确行动 | 已知限制 |
| --- | --- | --- | --- | --- | --- |
| P4-001 | PLANNED | P2-001, P3-004 | Provider catalog/contracts/SDK/loader identity tests 已存在。 | 列出 bundle identity 成分和 missing SBOM/signature policy。 | identity 不等于 trust/isolation。 |
| P4-002 | PLANNED | P4-001, P0-002 | loader 显式为 in-process dynamic loader，Linux/Windows boundary 需实测。 | 运行 loader focused tests 和 ABI negative matrix。 | 不提供 process isolation。 |
| P4-003 | PLANNED | P4-001, P4-002, P2-005 | provider init 已实现；其它 Provider developer commands 计划中。 | 先制定 command-by-command CLI/output/exit app test。 | 不创建第二套 runtime。 |
| P4-004 | PLANNED | P4-002, P3-005 | 多个 in-tree reference Provider 和 planned interfaces 存在。 | 对 catalog 的 implemented/planned operator 做一致性审计，并为可执行 reference Provider 建立 PartitionCapability matrix。 | planned interfaces 不可加载。 |
| P4-005 | PLANNED | P4-001, P3-003, P0-006 | 当前没有 worker/supervisor/fault boundary 证据。 | 先写 threat/fault model 和 process policy，未冻结时不写隔离代码。 | 需要平台和 security 决策。 |
| P4-006 | PLANNED | P1-006, P4-001, P3-006 | run record/crash/config/logging 基础存在。 | 用一个 reference run 定义完整 evidence chain。 | 需要 P1 reference artifacts。 |

### 12.6 P5 台账

| ID | Status | 依赖 | 基线观察/证据 | 下一精确行动 | 已知限制 |
| --- | --- | --- | --- | --- | --- |
| P5-001 | PLANNED | P0-005, P0-006, P3-003 | 尚未定义 caller-to-framework in-process ISMRMRD feed contract。 | 仅在存在 embedding 需求时定义 ownership/lifetime/terminal contract 与 fixtures。 | 不得引入 public binding、session、socket 或 transport。 |
| P5-002 | PLANNED | P5-001, P3-003 | 未观察到通用 in-process feed materialization/equivalence harness。 | 建立 HDF5 与 caller-submitted ISMRMRD feed semantic equivalence corpus。 | 仅保留 host-owned input；不能测试采集设备。 |
| P5-003 | PLANNED | P5-002, P4-006 | ksj-recon 当前为 offline CLI，尚无 embedded local run control API。 | 先定义 local run lifecycle/status/cancel API，禁止 raw-data transport scope。 | 隔离 Provider 不是该项强前置。 |
| P5-004 | SUPERSEDED | P0-005 | 2026-08-23：该项原为 scope-closure；用户随后明确要求 `ksj-gateway` 必须成为真实外部集成网关。保留历史任务和事实，但不得继续按删除/收口方向改动。 | 无；由 P5-008 接管。 | P5-004 从未证明 gateway 能力；现有 executable 仍只是 scaffold，直到后继工作项完成验收。 |
| P5-005 | PLANNED | P5-003, P3-001 | resource/edge 基础存在；本地 host API 未见完整 admission evidence。 | 定义 accepted-input 后的 bounded internal queue、Provider saturation 和 artifact writer contract。 | 不定义 ACK/read-gating/slow-source 语义。 |
| P5-006 | PLANNED | P5-002, P5-003, P5-005, P4-005 | 尚无 HDF5 与 in-process feed 的端到端 equivalence suite。 | 建立 equivalence、cancel、Provider crash、restart、result idempotence tests。 | P5 是可选能力包，不阻塞 v1。 |
| P5-007 | PLANNED | P5-003, P5-006 | ksj 尚无 local run status command。 | 仅增加 replay/dataset/local-run 命令与 app tests。 | 禁止 capture、gateway、session 或第二数据面。 |
| P5-008 | ACCEPTED | P0-005 | 2026-08-23：用户明确授权将 `ksj-gateway` 重置为真实外部集成网关。开始基线为 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`；现有程序仅有 CLI scaffold，平台 socket 仅是 blocking IPv4 基元，尚无 TLS、认证、framing、异步 I/O 或公开 profile。 | 已接受候选稳定架构及其所有规范入口；只有 P0-006 的 GWY-DEC-001 至 007 齐全后，P5-009 才可变为 READY。 | 此项只接受候选稳定设计，不实现网络 listener；任何外部服务、吞吐、可靠性或临床 claim 均仍禁止。 |
| P5-009 | PLANNED | P5-008, P0-006 | 未选择可公开互操作的 transport/profile，且 P0-006 缺少 deployment/security/output/data authority。 | 收到所有 owner/source/review inputs 后，先冻结一个 profile contract 和 conformance vectors。 | 不得以 MRD/ISMRMRD 名称假定存在已选网络 binding。 |
| P5-010 | PLANNED | P5-009, P3-001 | 当前只有 blocking IPv4 socket 基元；无 TLS、auth、framing、IPv6/DNS 或 session runtime。 | 选择受维护网络/TLS 依赖并实现 public-profile fake-peer testbed。 | 不能把现有 socket wrapper 或无界 MessageLoop/BlockingQueue 直接暴露给外部 peer。 |
| P5-011 | PLANNED | P5-010, P1-005, P2-004, P3-003 | HDF5 reader、HostFrameAssembler、ScanLifecycle、ResourceVectorLedger 存在，但没有 network-owned input materializer 或 verified gateway bridge。 | 建立 header-first admission、owned event bridge 与 runtime equivalence/fault corpus。 | runtime/Provider 保持内部共享库；不得发明 gateway-to-recon 私有数据面。 |
| P5-012 | PLANNED | P5-011, P1-002, P4-006 | 正式 image artifact/RunRecord evidence 和 Connector conformance 仍未接受。 | 冻结 egress/terminal semantics 与外部 Connector conformance harness。 | 不创建 vendor SDK、PACS/DICOM 路由、持久 raw spool 或 exactly-once 语义。 |
| P5-013 | PLANNED | P5-012, P7-002, P7-003, P7-005, P7-006 | 无 clean install、security/fuzz、slow-peer、approved peer 或 deployment qualification evidence。 | 在批准的 synthetic/fake-peer 和真实互操作环境中执行完整 qualification。 | 用户当前暂停 Linux 和 GitHub CI；它们不能被本项绕过。 |

### 12.7 P6 台账

| ID | Status | 依赖 | 基线观察/证据 | 下一精确行动 | 已知限制 |
| --- | --- | --- | --- | --- | --- |
| P6-001 | PLANNED | P3-002, P3-003, P3-004, P3-005, P0-006 | 当前 executor 是受限 synchronous implementation。 | 先冻结 Provider PartitionCapability/feature-flag grammar，并为每个 motif 建 serial-equivalence/KeyShard gate。 | 不能声称 generic async/keyed join。 |
| P6-002 | PLANNED | P3-001, P3-003, P6-001 | 无 multi-scan scheduler/fairness evidence。 | 从 quota/metrics/model 开始，只允许在合法 Ready task 集合内验证 DRR/priority。 | 不依赖网络或采集服务。 |
| P6-003 | PLANNED | P6-001, P0-006 | core memory/threading/performance 和 Linux NUMA dependency 存在。 | 采集真实 topology 并制定 placement oracle。 | 目标硬件未记录。 |
| P6-004 | PLANNED | P3-001, P6-001, P0-006 | 未观察到 GPU DevicePlan/runtime implementation。 | 先确定 CUDA/provider ABI/device policy，以及 device bytes/stream/fence/async-token 进入同一 ledger 的字段。 | GPU model、driver、backend 未冻结。 |
| P6-005 | PLANNED | P4-005, P6-004 | 未观察到 GPU async cancel/quarantine evidence。 | 先设计 fence/ownership/fault matrix。 | 无 GPU test environment。 |
| P6-006 | PLANNED | P1-003, P1-004, P3-006, P0-006 | benchmarks/research runner 已存在，但 end-to-end benchmark protocol 尚未接受。 | 审计 existing benchmark docs 和构建可复现 harness。 | 不可从 microbenchmark 推断 service SLO。 |
| P6-007 | PLANNED | P1-007, P6-003, P6-006 | 任何 channel count 都不是框架 gate；256 只是未来可测 case。 | 基于实际 ISMRMRD case、算法和目标 machine 运行 capacity/quality model。 | 需要 workload、host memory 与 scan data；不得创建 channel cap。 |

### 12.8 P7 台账

| ID | Status | 依赖 | 基线观察/证据 | 下一精确行动 | 已知限制 |
| --- | --- | --- | --- | --- | --- |
| P7-001 | PLANNED | P0-007, P2-006, P3-006 | local checks 存在，远程 workflow/required check 未见。 | 选择 CI carrier 并实现 test/artifact matrix。 | 需要外部 repo settings 授权。 |
| P7-002 | PLANNED | P4-002, P4-003, P7-001 | install preset 存在，clean install evidence 未观察到。 | 用 disposable Linux/Windows environment 验证 install tree。 | Windows runner 未知。 |
| P7-003 | PLANNED | P3-001, P3-002, P3-003, P3-004, P6-001 | static analysis/memory diagnostics preset 存在。 | 审计 sanitizers/fuzz/path coverage 与 CI feasibility。 | tools/hardware availability 未知。 |
| P7-004 | PLANNED | P4-001, P7-001 | local dependency payload/recipes 存在，release SBOM/signature evidence 未观察到。 | 选择 deterministic SBOM/attestation format。 | trust/signing policy 待决定。 |
| P7-005 | PLANNED | P3-006, P4-005, P6-002, P6-003, P6-004, P6-005 | 暂无 runtime/fault/soak evidence。 | 先建立 HDF5/input-submission pressure/artifact-writer/Provider failure testbed。 | P5 启用时增加 feed 测试；GPU 依赖仍独立。 |
| P7-006 | PLANNED | P0-004, P0-005, P7-002, P7-004 | README/docs 分散且部分链接可能失效。 | 自动核对 command/help/mode claim 对齐。 | release target未定义。 |
| P7-007 | PLANNED | P0-P4, enabled P5/P6, P7-001 至 P7-006 | 无 qualification report。 | 建立 checklist，等所有强依赖 ACCEPTED 后执行。 | 不得提前做 isolated-provider/deadline claim。 |

### 12.9 P8 台账

| ID | Status | 依赖 | 基线观察/证据 | 下一精确行动 | 已知限制 |
| --- | --- | --- | --- | --- | --- |
| P8-001 | ACCEPTED | P0-002, P1-002, P2-002 | 已新增 `qt/6.8.3`、第五个安装应用 `ksj-viewer`、CLI11 help/version、真实 QApplication 主窗口和 Qt 官方 `windeployqt` deployment helper；build/install tree 的 `platforms/qwindows.dll`、直接 UI smoke 与 install JSON protocol 均已实测通过。viewer 仅链接 `KSpaceJet::ismrmrd`、`KSpaceJet::recon_graph`、Qt Core/Gui/Widgets；其 executable/platform plugin import closure 不含 Qt Network/OpenGL/Quick/QML/WebEngine。完整证据见 13.20。 | `P8-002` 已启动：定义 inspection reader read model、API ownership、synthetic fixture 和 focused tests。 | `ksj-viewer` 仍只是 deployable UI shell，不读取 `.mrd` 或 pipeline、没有 image/k-space/metadata 真实视图或 export；Windows global install surface 可能包含其他应用的未使用 Qt package DLL，但 viewer import closure 已独立核验。 |
| P8-002 | ACCEPTED | P8-001, P1-002 | `InspectionReader` 已作为 `KSpaceJet::ismrmrd` 的标准 HDF5、只读、有界 inspection public read model 交付：XML/header/acquisition/image/MetaAttributes、callback-scoped payload、axis/type contract、per-reader limits、named-field HDF5 header mapping、deterministic malformed diagnostics 和 normal/malformed/oversize/axis/meta synthetic corpus 均有 focused Windows Release evidence；public header 已安装。完整证据见 13.21。 | 已完成；由 `P8-003` 消费该 reader 完成 Qt presentation/export。 | 不复制 HDF5 或在 KSpaceJet 保存 raw data；无界 full-file load 不可接受；本项不接入 Qt UI、Pipeline parser 或 export。 |
| P8-003 | ACCEPTED | P8-002, P2-002 | `ksj-viewer` 已交付共享 `InspectionReader` 与唯一 `PipelineDefinition::parse_json()` 的 app-local Qt presentation/export 层：metadata/k-space/image/pipeline 视图、PNG/SVG/CSV/JSON labelled display-derivative export、synthetic focused corpus、Windows build/install UI/export smoke 和 import-closure audit 均已通过。完整证据见 13.22。 | 无 READY 项；保持 P8 结果，等待用户解除现有 P0 policy / GitHub CI / Linux 阻塞或授权新的独立工作项。 | k-space 只为 acquisition magnitude projection，绝非 reconstructed image；export 不是第二种 MRI image artifact；仅有 Windows Release developer evidence，不构成 Linux、临床、service、性能或 release qualification claim。 |
| P8-004 | IN_PROGRESS | P8-003 | 用户指定本地源码 `E:\hdfview`、安装目录 `D:\HDFView` 与 `HDFGroup/hdfview` 为 UI/操作参考；`InspectionReader` 递归、有界发现 standard raw/image/waveform container，`ViewerWindow` 提供浅色紧凑的 HDFView-inspired File/Window/Tools/Help shell、semantic tree、`Object Attribute Info`/`General Object Info` inspector 和 Info/status。打开 standard MRD 默认只显示 tree 与 inspector，typed-data 区域隐藏；不再显示固定 `Dataset overview` 或 `Image series` dashboard。选择 Header/XML 后必须显式 `Inspect`/`Open As…` 才打开 XML typed view；`Images` 是独立语义对象，raw acquisition source 的零 image series 是正常状态。2026-08-24 已重跑 focused reader/viewer CTest 2/2、Release `ksj_viewer` build、build-tree UI smoke、install 与 installed UI/export smoke，均通过；完整进行中证据见 13.23。基线仍为 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`。 | 先修复 Windows unit-test executable target-only build 后未部署 Conan/Qt runtime DLL 的问题：将 runtime staging 绑定到 test target post-build，并验证不调用 `conanrun.bat` 的直接 executable/CTest 启动；随后以真实 `E:\KSpaceJet-ismrmrd-data\datasets\zen-2d-cartesian-2025\cart_t1.mrd` 在 build/install Viewer 完成用户视觉复核。 | 不复制/链接 HDFView Java/SWT code；不新增 QML/Quick/WebEngine、Python、Matplotlib、外部图标包、reconstruction、Provider/gateway 或新的 MRI artifact；不变成 generic HDF4/HDF5/NetCDF/FITS browser/editor，不支持 generic object/attribute edit/save、URL loading 或无界 hyperslab；正式 reconstructed image 只使用标准 ISMRMRD `image_x`，不创建 `/ksj_recon`、`/ksj_debug` 或 `/ksj_meta`；不将 `ksj_*` group 设为 input prerequisite；真实 raw data 只读且不进入 KSpaceJet。 |

---

## 13. 证据、变更和决策日志

此日志按时间追加，不删除旧条目。更正错误时增加新条目并链接旧条目。

| 日期 | 事件 | 关联工作项 | 证据/事实 | 结果与下一步 |
| --- | --- | --- | --- | --- |
| 2026-08-19 | 建立初始执行总规范和 AGENTS 自主流程。 | P0-003 | 静态审查 main 8c31b304；读取 README、AGENTS、CMake、apps、recon、Provider、schema 和测试目录。 | P0-003 等待文档结构校验；P0-001 是下一 READY 项。 |
| 2026-08-19 | 验证并尝试将执行规范写入隔离 GitHub branch。 | P0-003 | 两个 Markdown 文件通过 whitespace/diff 结构检查；创建 agent/kspacejet-execution-plan 分支返回 GitHub API 403 Resource not accessible by integration。 | 未改变远程仓库；P0-003 置 BLOCKED，待将交付文件复制/提交到仓库。 |
| 2026-08-19 | 基线成熟度审查。 | P0-001 | 观察到 offline HDF5 Cartesian/non-Cartesian reference、generic synchronous in-process graph、Provider SDK/loader/contract、bounded runtime primitives 和大量测试；未实际 build/test。 | 不能标任何已有 feature ACCEPTED；先执行可重复基线。 |
| 2026-08-19 | 范围更正：KSpaceJet 只接收 ISMRMRD 数据。 | P0-005, P5-004 | 用户确认本项目不是采集/transport/gateway；P5 改为可选 in-process feed，外部 session/relay 计划被移除。 | 不得在 README/CLI/计划中恢复 scanner、DMA、FPGA、PCIe、session 或 gateway 产品 claim。 |
| 2026-08-19 | 通道数更正。 | P0-006, P1-007, P6-007 | 用户确认框架不得对 channel count 设限；当前 Cartesian 1–64 是临时 reference 实现问题。 | P1-007 必须删除该限制；256 及更大仅为测试 case，资源不足按 bytes/work 报告。 |
| 2026-08-19 | 冻结并行、调度与资源架构。 | P2-003/004, P3-001 至 006, P4-004, P6-001 至 004 | 用户确认该架构必须进入可执行文档；第 18 节定义 Provider PartitionCapability、WorkKey、compiler/verifier、KeyShard、ResourceLedger、CPU/GPU scheduler 与无通道上限规则。 | 先按 P2/P3 的 schema、plan、ledger、serial-oracle 工作项实施；P6 仍保持 PLANNED，不能由文档直接视为已实现。 |
| 2026-08-19 | 文档冲突和断链审查。 | P0-004, P0-005 | 发现旧规划提出但未落地的 work-item YAML/schema；发现 document link 风险；plain-text core log 与 structured log 主张冲突。 | ADR-001/002 已记录；P0-004/005 必须原子收口。 |
| 2026-08-20 | 完成可重复 Linux 基线与 schema/semantic negative evidence。 | P0-001 | 独立本地 clone、全新 `.venv`/unit build tree、321-step build、35/35 CTest；`recon` label 4/4；两个 schema-valid fixture 与 compiler focused test 均通过。 | P0-001 ACCEPTED；P0-002 成为唯一 READY 项。 |
| 2026-08-20 | 完成 Linux toolchain、LFS、application install 与 check-script 验证。 | P0-002 | Linux/Windows Intel manifest 全量 hash、Git LFS pointer/object fsck、Linux bootstrap、35/35 unit、application build/install、clean-prefix four-app help、format/configure check 全部通过；Windows host probe 失败。 | P0-002 BLOCKED，等待真实 Windows MSVC 2022 runner；P0-004 READY。 |
| 2026-08-20 | 将唯一执行台账升级为可校验的 Master Plan 视图。 | P0-008 | 第 0.4 节从第 12 节生成阶段覆盖度、当前/READY/阻塞项、最近验收证据和恢复动作；离线 checker 与本地 document gates 已接入。 | P0-008 ACCEPTED；当前无 READY/IN_PROGRESS 项，P0-002、P0-003、P0-006 保持独立 BLOCKED。 |
| 2026-08-20 | 将 canonical governance/contract 交付提交到当前实际仓库。 | P0-003 | commit `e1150f4b24627f5f5b847f57ee4d633a8f8b33c1` / tree `01c12fcd1b52fe45a86a3a26691bcfbf264e6589`，pre-commit gate 通过。 | P0-003 ACCEPTED；P0-002 与 P0-006 保持 BLOCKED，当前无 READY 项。 |
| 2026-08-20 | 固化双仓库 raw-data 边界并删除旧 KSpaceJet local-data 工作流。 | P0-009 | 用户明确要求所有原始数据只留在同级 `KSpaceJet-ismrmrd-data`；旧 local raw data、metadata、downloader 与 test 已移除，离线 workspace checker、两端 pre-commit 接线和数据仓库 verifier 均已验证。 | P0-009 ACCEPTED；当前无 READY 项，P0-002/P0-006 的既有外部 BLOCKED 条件不变。 |
| 2026-08-20 | 建立 checksum-pinned project-local `just` 和跨平台统一开发入口。 | P0-010 | Linux/Windows bootstrap、runner、根 `justfile`、VS Code 和 hook 收敛到同名 recipe；Linux 实际 bootstrap、format、build/install、unit 和 check 通过，Windows 接线/命令长度经静态审查。 | P0-010 ACCEPTED；当前没有 READY 项，P0-002 的真实 Windows host 需求与 P0-006 的参数 authority 仍独立 BLOCKED。 |
| 2026-08-23 | 用户恢复外部集成网关产品方向。 | P5-004, P5-008 至 P5-013, P0-006 | 用户明确要求 ksj-gateway 必须成为真正的外部集成网关。旧 P5-004 的删除/收口方向与该明确要求冲突；当前源码审计确认它仅是 scaffold，socket 基元也不具备生产网络能力。 | P5-004 置 SUPERSEDED，由 P5-008 接管候选稳定架构；P5-009 至 P5-013 拆分 public profile、listener、runtime bridge、Connector/egress 和 qualification。未实现任何服务；P0-006 增加 Gateway policy 输入。 |
| 2026-08-23 | 用户将主目标从真实数据兼容性审计更正为完成 `KSpaceJet-ismrmrd-data` 所需的 Provider 与重建核心框架。 | P1-008, P1/P2/P3/P4 | 现有 Cartesian/non-Cartesian RSS CLI 仅是窄的单帧开发 reference；真实数据集还要求统一 semantic-frame ingress、正确的 Cartesian crop/组帧、NUFFT/DCF、partial-Fourier、SENSE/GRAPPA、dynamic/multi-echo、EPI 与 3-D 路线。 | 新建并启动 P1-008 作为共同 core implementation 起点；后续 Provider 依赖其 normalized acquisition/classification/frame-key/completion 语义。不得把 compatibility/reject 报告误当成重建完成。 |
| 2026-08-23 | 统一 ISMRMRD semantic-frame ingress 验证完成，开始真正的 radial reconstruction Provider。 | P1-008, P1-009 | shared ingress 已迁移 generic replay、Cartesian RSS、non-Cartesian RSS；Windows Release runtime CTest、application build、TypeRegistry、format、plan/link/diff checks 均通过。HDF5 borrowed view 在 callback 中被同步复制到有界 host destination，FrameSlot 以 required coverage 和 EndOfInput 而非 count/control flag 完成。 | P1-008 ACCEPTED；P1-009 成为唯一 IN_PROGRESS，先交付 bounded 2-D linear-gridding 与 analytic DCF 数值核心，再经独立 route 调用新 Provider Operator。 |

### 13.1 P0-001 ACCEPTED 证据

- Work item: `P0-001`；requirements/acceptance: `FUN-001` 的 Linux baseline 部分、`FUN-002`、`AC-BLD-001`、`AC-ART-004`。
- Commit/tree: `8c31b30419ed330688b3f1b90f14a4498503317d` / `4455dc2fb45214952b710fa5519d8d084e9efad5`。本项未提交；开始时已存在的 `AGENTS.md` 和本台账改动未被覆盖或回退。
- Changed files/public surface: `cmake/KSpaceJetBootstrap.cmake`、`schemas/README.md`、两项 graph tests、两个 semantic-invalid fixture 与本台账。无 public API/ABI/schema/CLI 改动；修复多值 CTest label 的注册，使 `ctest -L recon` 实际选择 4 个既有 recon tests。
- Clean-clone validation: 在临时目录 `out/ksj-p0-001-local-clone-fkOT8T/source` 执行本地 `git clone --no-local`，checkout 相同 baseline 并应用同一未提交 code/test/fixture diff；该 clone 使用新的 Git metadata、`.venv` 和 build tree。执行 `git lfs checkout`（3459/3459、3.0 GB）、`bash tools/devenv/linux/bootstrap.sh --prepare linux-release-unit-tests`、`bash tools/checks/linux/ci_unit.sh`；结果为 bootstrap/recipe export/configure 成功、321-step build 成功、CTest 35/35 通过（2.59 s）。clone 使用已存在的本机 LFS/Conan cache；它不是远端 clone、远端 LFS transfer 或 cold-cache evidence。
- Focused validation: `tools/devenv/linux/run.sh jsonschema --instance tests/unit/libs/recon/fixtures/invalid/pipeline-semantic-provider-mismatch.json schemas/pipeline.schema.json`；同命令针对 `pipeline-semantic-unbound-contract-port.json`；两者 schema pass。`tools/devenv/linux/run.sh out/build/linux-release-unit-tests/bin/ksj_recon_graph_tests --gtest_filter='SynchronousGraphPlan.RejectsSchemaValidPipelineWithUnboundContractPortFixture'` 通过，证明该 fixture 经 parser/resolver 后被 compiler 拒绝。`pipeline-semantic-provider-mismatch.json` 的 resolver negative test 随 graph suite 通过。
- Complete validation: 主工作树执行 `bash tools/checks/linux/ci_unit.sh`（35/35，2.61 s）、`tools/devenv/linux/run.sh python tools/type_registry/generate.py --project-root . --check`、`tools/devenv/linux/run.sh ctest --preset linux-release-unit-tests -L recon --output-on-failure`（4/4）、两个上述 `jsonschema` 命令、上述 focused GTest、`tools/devenv/linux/run.sh clang-format --dry-run --Werror`（两个改动测试）、`tools/devenv/linux/run.sh cmake-format --check cmake/KSpaceJetBootstrap.cmake`、`bash tools/checks/linux/format_check.sh --changed HEAD^` 与 `git diff --check`；全部退出 0。`jsonschema` 仅报告其 CLI deprecation warning；`cmake-format` 仅报告既有 install-form warning，均未失败。
- Platform/toolchain: Debian GNU/Linux 13 (trixie), Linux 6.12.101 x86_64, GCC/G++ 14.2.0, CMake 3.31.6, Git LFS 3.6.1, Python 3.13.5, Conan 2.31.2, 28 logical CPUs / 62 GiB RAM.
- Produced artifacts/digests: TypeRegistry generator check 未产生待提交差异；semantic fixtures 为 `tests/unit/libs/recon/fixtures/invalid/pipeline-semantic-provider-mismatch.json` 和 `tests/unit/libs/recon/fixtures/invalid/pipeline-semantic-unbound-contract-port.json`。
- Known limitations: 未验证 Windows、install、真实远端 LFS transfer、application end-to-end/golden artifact、system/fault/soak、性能/容量或 release qualification；Provider loader 仍为 in-process；不得作 online/gateway/GPU/256-channel/clinical 或 production-ready 宣称。
- Next READY item at the time of acceptance: `P0-002`。

### 13.2 P0-002 ACCEPTED 证据

- Work item: `P0-002`; requirements/acceptance: `FUN-001`, `AC-BLD-001` 至 `AC-BLD-003`。
- Base commit/tree: `8c31b30419ed330688b3f1b90f14a4498503317d` / `4455dc2fb45214952b710fa5519d8d084e9efad5`；工作树还包含已接受但未提交的 P0-001 code/test/fixture/docs diff。本项没有源码、测试或公开 contract 改动。
- Exact Linux LFS commands/results: `tools/devenv/linux/run.sh python tools/devenv/verify_intel_payload.py --platform linux-x86_64 --full` exit 0（`2981 files`）；同命令的 `--platform windows-x86_64 --full` exit 0（`611 files`）；`git lfs fsck --pointers` 与 `git lfs fsck --objects` 均 exit 0（`Git LFS fsck OK`）。Windows payload hash 可在 Linux 上验证，但不构成 Windows build/install evidence。
- Exact Linux bootstrap/build/install commands/results: `bash tools/devenv/linux/bootstrap.sh --no-hooks --prepare linux-release` exit 0（6.895 s）；`tools/devenv/linux/run.sh cmake --build --preset linux-release --target ksj_cli ksj_gateway ksj_recon ksj_research` exit 0（3.245 s），产出 `ksj`、`ksj-gateway`、`ksj-recon`、`ksj-research`；`tools/devenv/linux/run.sh cmake --build --preset linux-release-install` exit 0（4.342 s）；`tools/devenv/linux/run.sh cmake --install out/build/linux-release --prefix out/ksj-p0-002-install.koog7k` exit 0（1.385 s）。该临时 prefix 随后按精确路径安全删除。
- Install/runtime smoke: fresh prefix 含四个 executable、六个 Provider `.so` 和 15 个 Provider contracts；对每个 executable 运行 `env -i PATH=/usr/bin:/bin LANG=C <prefix>/bin/<ksj|ksj-recon|ksj-gateway|ksj-research> --help` 均 exit 0。`readelf -d <prefix>/bin/ksj` 显示 `RPATH [$ORIGIN/../lib]`；主 install tree 中四个 executable 的 `ldd` 均无 `not found`。gateway/research help 明确为 scaffold；gateway 仍有 external-session/connector/MRD forwarding 文案，转交 P0-005，不能作为能力证据。
- Check scripts/results: `bash tools/checks/linux/ci_unit.sh` exit 0，CTest 35/35（2.59 s）；`bash tools/checks/linux/ci_check.sh` exit 0（changed format + `linux-release` configure）。
- Incremental-build investigation: 初始既有 `out/build/linux-release` 的 Ninja metadata 报 `premature end of file; recovering`；CMake cache 指向 `.venv/bin/ninja` 1.13.0，而裸主机 `/usr/bin/ninja` 为 1.12.1 且报告 `build log version is too new`。在确认 `.ninja_log` / `.ninja_deps` 均为 `/out/` 下未跟踪、可再生文件后，只处理这两个精确 target：清理时 `.ninja_log` 已不存在，删除 `.ninja_deps`；以项目 Ninja 重建后，`tools/devenv/linux/run.sh cmake --build --preset linux-release --target ksj_cli ksj_gateway ksj_recon ksj_research` exit 0 且仅执行 glob/type-registry recheck，无 warning。随后 install preset 再次 exit 0，四个已安装 app 的空环境 `--help` 再次 exit 0。此为 ignored build artifact 修复，未改源码；后续必须经 platform runner 调用工具，不能以裸 host Ninja 混用该 build tree。
- Exact Windows investigation/output: `uname -srm` -> `Linux 6.12.101+deb13-amd64 x86_64` (exit 0)；`command -v powershell`、`command -v pwsh`、`command -v cl`、`command -v vswhere` 均 exit 1；`test -d /mnt/c` exit 1；`cmake --list-presets` 只显示可用 Linux presets。`wine` 存在但不是 Windows kernel、PowerShell 或 MSVC build host。
- Missing fact/authority/device: 可用的 Windows x64 host/CI runner，以及 Visual Studio 2022 v143 C++ Build Tools、Windows SDK、Git/Git LFS 和 PowerShell。无需也未请求远端/凭据授权。
- Why no safe local alternative exists: Wine 不能替代 Windows kernel、MSVC ABI/DLL loader 或 VS CMake generator；把 Linux result 伪装为 Windows result 会违反 `AC-BLD-002`。
- Impacted successor items: `P0-007`、`P1-001`、`P4-002`、`P7-002` 及其强依赖链均不能因本项进入 ACCEPTED。
- Unblock condition: 在真实 Windows host/runner 上，对 debug 和 release 分别运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 -Prepare windows-vs2022-debug` / `windows-vs2022-release`，随后以 `tools\devenv\windows\run.ps1 cmake --build --preset windows-vs2022-<config>` 和 `windows-vs2022-<config>-install` 完成 build/install，并从干净 install tree 运行 `ksj.exe --help` 等 basic smoke，记录实际输出、DLL dependency closure 和失败（如有）。
- Proposed next owner/action: 具备上述 Windows host/runner 的维护者或 CI runner；完成前保持 `P0-002` 为 `BLOCKED`，不作跨平台或 release-qualified 宣称。下一可执行项为 `P0-004`。
- 2026-08-21 Windows resumption evidence: 当前工作区在 Windows `10.0.26220.0` 上运行，PowerShell `5.1.26100.9202`、Git 和 `git-lfs/3.7.1` 可用。初次执行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 -Prepare windows-vs2022-debug` 在 PowerShell 解析阶段失败：`bootstrap.ps1:99` 与 `:334` 的 `$LASTEXITCODE:` 被判定为无效变量引用。将这两处改为 `${LASTEXITCODE}:` 后，`[System.Management.Automation.Language.Parser]::ParseFile(...)` 无错误；重跑同一 bootstrap 的实际输出为 `[kspacejet-devenv] Visual Studio 2022 v143 C++ Build Tools are missing; install the C++ workload and a Windows SDK`。因此没有执行 CMake configure/build/install，也没有生成任何 Windows build/install 或 DLL closure 证据。
- Latest missing prerequisite and unblock: 在本 Windows 主机或可用 Windows x64 runner 安装 Visual Studio 2022 v143 的 C++ Build Tools workload 与 Windows SDK，使 bootstrap 检出后，先重跑 debug bootstrap，再依照第 12 节完成 debug/release 的 build、install、已安装程序 help smoke 和 DLL dependency closure。不能以当前 CMake `4.2.0`、Git/Git LFS 或 Linux 历史结果代替 MSVC/SDK 证据。
- 2026-08-21 host-just policy update: 用户撤回 project-local `just` 版本锁定后，Windows bootstrap 将 `just` 作为 host prerequisite。`where just` exit 1（无匹配）；`powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 -Verify` 的实际输出为 `[kspacejet-devenv] host prerequisite is missing: just`。因此 P0-002 的 Windows unblock 现在同时要求 `just`、VS 2022 v143 C++ Build Tools 和 Windows SDK；该变更不以任何系统 `just` 版本作为供应链或 release 证据。
- 2026-08-21 automatic Windows just installation: 用户进一步明确 bootstrap 应安装非锁定 `just`。`winget search --id Casey.Just --exact --accept-source-agreements` 返回 `Just Casey.Just 1.58.0`；bootstrap 使用 `winget install --id Casey.Just --exact --accept-package-agreements --accept-source-agreements` 安装成功。该 package 未创建 WinGet Links entry，故 bootstrap 改为定位 `%LOCALAPPDATA%\Microsoft\WinGet\Packages\Casey.Just_*\just.exe`、将其目录加入当前进程和 user PATH；直接运行该 executable 输出 `just 1.58.0`。之后 `bootstrap.ps1 -Verify` 已越过 just 前提并正确报告 VS 2022 v143 C++ Build Tools 缺失。观察到的 winget package version 不是仓库锁定、checksum 或 release evidence；P0-002 现在仅等待 VS/SDK prerequisite。

- 2026-08-22 Windows resumption/update: 用户授权后安装 `Microsoft.VisualStudio.2022.BuildTools` 的 VCTools workload；`vswhere -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json` 返回 BuildTools `17.14.39`，`isComplete=true`、`isLaunchable=true`、`isRebootRequired=false`，MSVC 目录为 `14.44.35207`。实际 `bootstrap.ps1 -Prepare windows-vs2022-debug` 暴露 PowerShell regex `^14\\.4` 不能匹配该目录名；修正为 `^14\.4` 后，PowerShell parser 无错误且 bootstrap 越过 MSVC 检查。Conan 发现此前 Boost source folder 损坏并重取 `boost_1_91_0.tar.bz2`（204.6 MB），但 `archives.boost.io` 三次分别在约 17.6 MB、97.8 MB、95.0 MB 后报 `Read timed out`/`IncompleteRead`；最终输出为 `ERROR: boost/1.91.0: Error in source()`、`ConanConnectionError: Download failed`，以及 bootstrap 的 `project tool failed with exit code 1: conan install . --output-folder=out/build/windows-vs2022-debug --profile:host=conan/profiles/windows-msvc2022-debug --build=missing`。临时后台日志位于 `%TEMP%\KSpaceJet-bootstrap\windows-debug-bootstrap-fc7e2756780e4ba59bc44161ecf9efda.{stdout,stderr}.log`。P0-002 保持 BLOCKED，直到网络或受信任预热 cache 提供完整 Boost source；未执行 CMake configure/build/install。

- 2026-08-22 Windows Release acceptance: 用户明确指定以 Release 推进本项，故不把未完成的 Debug download 当作验收要求。`powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 -Verify` exit 0，报告 `just 1.58.0`、Python `3.13.5`、Conan `2.31.2`、CMake `3.31.6`、Ninja `1.13.0`、clang-format `19.1.7` 与 cmake-format `0.6.13`；`git lfs fsck` exit 0（`Git LFS fsck OK`）。`py -3 tools/devenv/verify_intel_payload.py --platform windows-x86_64` 和 `py -3 -m py_compile third_party/intel/conanfile.py` 均 exit 0。
- Windows Release implementation/contract impact: `CMakeLists.txt` 对 MSVC C++ 增加 `/Zc:__cplusplus` 和 `/bigobj`；`third_party/intel/conanfile.py` 与其 CMake build module 将 OpenMP/runtime 目录修正为 payload 实际 `oneapi/*/lib`。`cmake/KSpaceJetBuildSupport.cmake` 不再让 `install(RUNTIME_DEPENDENCY_SET)` 对 build staging DLL 与 Conan source DLL 做歧义解析；Windows install 明确复制已发现 Conan `bin/*.dll` 与 Intel `lib/*.dll`，对大小写不敏感 basename 的多来源计算 SHA-256，并在内容不同情况下 `FATAL_ERROR`。这改变了 Windows install 的第三方 DLL surface；无 public API、ABI、schema 或 CLI contract 改动。
- Windows Release commands/results: `just prepare-release` exit 0；`just build-release-applications` exit 0，产出 `ksj.exe`、`ksj-gateway.exe`、`ksj-recon.exe` 与 `ksj-research.exe`；`just install-release-applications` exit 0；`just check` exit 0（658 个 C/C++ clang-format、107 个 CMake cmake-format、73 Markdown/162 local links、execution-plan check 与 `cmake --preset windows-vs2022-release` 均通过）。cmake-format 对两个既有 indirect install-form 只报告 warning，未失败。
- Installed Release smoke/closure: `out/install/windows-vs2022-release/bin` 中四个应用的 `--help` 均 exit 0，`ksj.exe --version` exit 0；gateway/research help 仍明确为 unimplemented scaffold。使用 VS 2022 `dumpbin /DEPENDENTS` 扫描 install tree 的 270 个 EXE/DLL（包含 Provider DLL），按 install tree 文件、`%WINDIR%\\System32` 与 API-set 名称解析后，`closure_missing=0`。关键第三方 DLL `fmt.dll`、ITK、MATIO、OpenCV、IPP、MKL 与 `libiomp5md.dll` 都在 install `bin`。当前 242 个 Conan/Intel DLL 的 basename 在本配置下唯一，安装目录中均有匹配 SHA-256；此闭包与实际启动共同构成 Windows Release developer-install evidence。
- Platform/toolchain: Windows `10.0.26220.0`、PowerShell `5.1.26100.9202`、VS 2022 Build Tools `17.14.39`、MSVC `19.44.35228.0`、Windows SDK target `10.0.26100.0`、Git LFS `3.7.1`、host just `1.58.0`。
- Known limitations/next READY item: 该项只接受用户指定的 Release developer install smoke；不证明 Debug、clean-machine redistribution, minimal DLL bundle、performance/capacity、Provider load、fault/soak、clinical 或 release qualification。当前 bundle 保守包含 242 个 Conan/Intel DLL（约 826.8 MiB），最小化分发属于后续 qualification/package work。下一 READY 项为 `P0-007`；不进行任何远程 CI、secret 或仓库设置写入，除非用户明确授权。

### 13.3 决策记录规则

对于任何影响公共能力、数据语义、API/ABI/schema、Provider trust、在线协议、性能 SLO 或发布 mode 的决定，新增一行：

| ADR ID | 日期 | 决定 | 候选方案与取舍 | 证据 | 影响任务 | 复核触发条件 |
| --- | --- | --- | --- | --- | --- | --- |
| ADR-xxx | yyyy-mm-dd | 一句话结论 | 为什么没有选择其他方案 | 测试、测量、规范或用户指令 | Px-xxx | 何时必须重新评估 |

没有 ADR 的重大变更不得被隐藏在普通代码改动中。

### 13.4 BLOCKED 记录模板

每个 BLOCKED 行至少包含：

    Work item:
    Base commit/tree:
    Exact command or investigation:
    Actual output:
    Missing fact/authority/device/data:
    Why no safe local alternative exists:
    Impacted successor items:
    Unblock condition:
    Proposed next owner/action:

### 13.5 ACCEPTED 记录模板

每个 ACCEPTED 行至少包含：

    Work item:
    Commit/tree:
    Requirement and acceptance IDs:
    Changed files/public surface:
    Focused validation:
    Complete validation:
    Platform/toolchain:
    Produced artifacts/digests:
    Known limitations:
    Next READY item:

### 13.6 P0-004 ACCEPTED 证据

- Work item: `P0-004`; requirements/acceptance: `FUN-001` 的文档完整性部分与 `AC-BLD-004`。
- Base commit/tree: `8c31b30419ed330688b3f1b90f14a4498503317d` / `4455dc2fb45214952b710fa5519d8d084e9efad5`；工作树也包含 P0-001/P0-002 的未提交证据改动及用户已有的 `AGENTS.md` 改动，均未覆盖或回退。
- Changed surface: 新增 `tools/checks/check_markdown_links.py`（仅标准库、无网络），并接入 Linux `ci_check`/`pre_commit`、Windows `pre_commit`、hook 提示和 checks 文档；创建 `docs/benchmark_reports/` 与 `docs/research_reports/` 的实际说明/模板；修复 conventions、开发环境、static-analysis、numerics、benchmark、历史规划和 apps 前门文档中的失效路径、无效 app-test build 命令及过时角色描述。无 public API/ABI/schema/CLI 行为变更；文档明确 gateway/research 当前是 scaffold，`ksj-recon` 是离线 HDF5 reference。
- Path audit: 原有 `docs/conventions/README.md` 的 `reconstruction_state.md`、`release.md` 链接已删除并指向现有权威文档；benchmark/research reports 的三条链接已有实际目标。`tools/batch_recon_studio`、`tools/ksj_static_analysis`、不存在的 array benchmark singleton、已移除的 MRI math 路径、未实现 work-item schema 和错误的 app-test build 形式均已修复、删除或显式标为历史/未实现设计；重新扫描没有仍被当作现存目标的上述路径。
- Focused validation: `tools/devenv/linux/run.sh python -m py_compile tools/checks/check_markdown_links.py` exit 0；`tools/devenv/linux/run.sh python tools/checks/check_markdown_links.py --self-test` exit 0（4/4，包括临时目录中缺失相对路径使普通 CLI 返回非零的断言，以及缺失锚点、代码/LaTex 排除和 local-environment/vendor 排除）；`tools/devenv/linux/run.sh python tools/checks/check_markdown_links.py --project-root .` exit 0（`75 file(s), 151 local link(s)`）。
- Complete validation: `bash tools/checks/linux/ci_check.sh` exit 0（离线 link check、changed format 与 `linux-release` configure）；`bash -n tools/checks/linux/ci_check.sh tools/checks/linux/pre_commit.sh tools/checks/linux/install_hooks.sh` exit 0；`git diff --check` exit 0；`tools/devenv/linux/run.sh cmake --build --preset linux-release-app-tests --target help` exit 0；`tools/devenv/linux/run.sh python tests/apps/application_json_protocol_tests.py out/build/linux-release/bin/ksj out/build/linux-release/bin/ksj-gateway out/build/linux-release/bin/ksj-recon out/build/linux-release/bin/ksj-research` exit 0。
- Platform/toolchain: Debian GNU/Linux 13 (trixie), Linux 6.12.101 x86_64, GCC/G++ 14.2.0, CMake 3.31.6, Python 3.13.5；checks 使用 repository-local `.venv` 的 managed tools。
- Known limitations: 检查器刻意只验证仓内 inline Markdown 文件/路径/heading anchor，不请求外部 URL，也不替代真实远程 CI/branch protection；后者仍在 `P0-007`。当前 Linux 主机无 PowerShell/Windows host，因此 Windows hook 未执行；该事实不替代 P0-002 的 Windows BLOCKED record。历史 architecture/paper 的规范冲突不在本项范围，交由 P0-005 原子处理。
- Next READY item: `P0-005`。

### 13.7 P0-005 ACCEPTED 证据

- Work item: `P0-005`; requirements/acceptance: `AC-ART-004` 的持续 semantic-negative 证据、`AC-OBS-007`、`AC-REL-007`。
- Base commit/tree: `8c31b30419ed330688b3f1b90f14a4498503317d` / `4455dc2fb45214952b710fa5519d8d084e9efad5`；工作树包含 P0-001/P0-004 已接受但未提交的改动及用户原有的 `AGENTS.md` 编辑，均未覆盖或回退。
- Changed public/contract surface: 以不保留 alias 的 pre-release 直接替换，将 `ExecutionProfile`、六份 artifact schema、fixtures、parser、默认值和 Cartesian/non-Cartesian reference artifact 统一为 `offline-reference`、`bounded-reconstruction-graph`、`provider-development`、`embedded-incremental`、`isolated-provider-runtime`；旧 serialized 值被拒绝。`PublicMrdMessageKind`/`session_candidate` 分别直接替换为 `IsmrmrdMessageKind`/`input_candidate`，并澄清 TargetEnvelope/ingress 是调用方提交后的本地语义，不是 scanner/session/relay/网络 transport contract。TypeRegistry 的可读 ingress/egress prose 同步更新并重新生成 C++/C headers，结构 identity digest 未改变。gateway/research help JSON 现在明确输出 `status=scaffold`、`availability=reserved`、`operations=unimplemented`；请求保留操作仍以 JSON `unimplemented` 和 exit 5 失败。Core logger 的 `logging.output_format=text` 唯一支持语义新增回归测试；CLI JSON stdout、metrics、trace、RunRecord/audit 与 plain-text diagnostics 的边界已在 AGENTS、README 和组件说明中一致说明。
- Authority/scope closure: 本文件第 1.1 节将历史 architecture/paper 明确降为非规范性背景；`schemas/README.md` 将唯一 artifact chain 固定为 `PipelineDefinition -> ResolvedPipeline -> PlanBuildRequest -> ExecutionPlan -> VerificationRecord -> AdmissionRecord -> RunRecord`。七份历史 architecture/paper 记录和 `docs/README.md` 都有显著 historical/non-normative 标记，明确撤回外部 MRD session、gateway/Connector/scanner、network relay/transport 与 structured core logging 的旧提案。
- Focused validation: `tools/devenv/linux/run.sh out/build/linux-release-unit-tests/bin/ksj_logging_tests --gtest_filter='KSpaceJetLogging.RejectsStructuredDiagnosticOutput'`、`.../ksj_recon_model_tests --gtest_filter='KSpaceJetReconModelExecutionProfile.AcceptsCanonicalNamesAndRejectsLegacyNames'`、`.../ksj_recon_graph_tests --gtest_filter='SynchronousGraphPlan.RejectsSchemaValidPipelineWithUnboundContractPortFixture'` 均 exit 0（各 1/1）。六份 schema 的 `jq` enum 审计均精确等于上述五个值；scope scan 确认七份历史文档均含 non-normative marker、活跃 runtime 不再含 `PublicMrdMessageKind`/`session_candidate`，旧 profile 只出现在历史记录和 explicit rejection test。
- Complete validation: `tools/devenv/linux/run.sh python tools/type_registry/generate.py --project-root . --check` exit 0；`bash tools/checks/linux/ci_unit.sh` exit 0（35/35）；`tools/devenv/linux/run.sh cmake --build --preset linux-release --target ksj_cli ksj_gateway ksj_recon ksj_research` 与相同 app-test targets 均 exit 0；`tools/devenv/linux/run.sh ctest --test-dir out/build/linux-release-app-tests --output-on-failure -R '^apps[.]json_cli_protocol$'` exit 0（1/1）；`tools/devenv/linux/run.sh python tests/apps/application_json_protocol_tests.py out/build/linux-release/bin/ksj out/build/linux-release/bin/ksj-gateway out/build/linux-release/bin/ksj-recon out/build/linux-release/bin/ksj-research` exit 0；`bash tools/checks/linux/ci_check.sh` exit 0（offline link check、172 C/C++ clang-format、32 CMake file check、linux-release configure）；`tools/devenv/linux/run.sh python tools/checks/check_markdown_links.py --project-root .` exit 0（75 files、160 local links）；`git diff --check` exit 0。
- Platform/toolchain: Debian GNU/Linux 13 (trixie), Linux 6.12.101 x86_64, GCC/G++ 14.2.0, CMake 3.31.6, Python 3.13.5, Conan 2.31.2.
- Known limitations: 本项不使 `provider-development`、`embedded-incremental` 或 `isolated-provider-runtime` 成为已接受能力；当前 in-process runtime 只支持前两个 profile，P4/P5/P7 的独立 acceptance 仍未完成。P0-002 的真实 Windows MSVC host/install evidence 仍 BLOCKED；不作任何 gateway、online/session/transport、isolation、GPU、channel-capacity、clinical 或性能宣称。
- Next READY item on acceptance: `P0-006`，先收集真实 TargetEnvelope/MachinePolicy 参数或记录精确 BLOCKED 输入。

### 13.8 P0-006 BLOCKED 证据

- Work item: `P0-006`; requirements/acceptance: 第 6.3 节全部参数的 source/owner/review-date 要求，以及本项“缺失参数明确 BLOCKED、不猜测默认值”的验收条件。
- Base commit/tree: `8c31b30419ed330688b3f1b90f14a4498503317d` / `4455dc2fb45214952b710fa5519d8d084e9efad5`；工作树包含 P0-001/P0-004/P0-005 已接受但未提交的改动，未覆盖或回退它们。本项只更新 canonical ledger；没有写入产品参数、deployment config、schema、runtime 或 public contract。
- Audit evidence: `rg --files | rg '(^|/)(target-envelope|machine-policy)[^/]*[.]json$'` 的全部命中仅为两份 schema 和四份 unit fixtures；没有非测试 JSON policy。`rg -n 'kMaximumChannels|kMachineBudgetHeadroomBytes|TargetEnvelope::create|MachinePolicy::create' ...cartesian_rss_hdf5.cpp ...noncartesian_rss_hdf5.cpp` 显示两个 reference route 各自有 `kMaximumChannels = 64U`、16 MiB headroom，且只在每次 HDF5 preflight 后派生 `TargetEnvelope`/`MachinePolicy`。这不是 deployment-owned 参数，也不得成为 framework channel cap。
- Host observation (not policy): `uname -srm` 为 `Linux 6.12.101+deb13-amd64 x86_64`；`nproc` 为 `28`；`lscpu` 显示 i7-14700K、1 socket、1 NUMA node；`free -b` 总内存为 `67077595136` B；`nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader` 为 `NVIDIA GeForce RTX 4060 Ti, 550.163.01, 16380 MiB`。这些是本次 Linux build host 的可重复观察，不是经批准的 target topology；P6-004 仍明确没有 GPU DevicePlan/runtime acceptance。
- Classification: `org.example` envelope/policy fixtures、reference-route per-input calculations、Gadgetron research manifest 和 native-endian f32 + JSON sidecar 输出均已在第 6.3.1 节归类为 test/reference/research/developer evidence。research case 虽有 pinned source/hash 和 access date，但 license 为 `not stated by source`、redistribution 为 `unclear`，不能提升为产品 case。loader 的 path/root/digest controls 仍只是 trusted in-process ABI boundary，不能替代 release/security policy。
- Blocking inputs: 必须由具名 case、deployment、performance、data-governance、output、security/release 和 architecture owner 分别提供第 6.3.1 节所列 immutable source、适用范围和 review date。最低需要 approved case manifest（实际 shape/channel/algorithm/concurrency）、target MachinePolicy/topology、SLO measurement protocol/raw artifacts、privacy/retention/access、output/partial/ordering/durability 规则、Provider signing/SBOM/trust tier，以及 JSON/C++ parameter authority 的收口决定。
- Impact: P1-001、P1-005、P1-007、P2-003、P4-005、P5-001、P6-003/004/006/007 等依赖 P0-006 的工作项不能启动。P0-002 的 Windows 阻塞仍独立存在。
- Unblock and next action: 收到所有必填参数的 source/owner/scope/review inputs 后，将 P0-006 改为 `READY`；重新选中时先改为 `IN_PROGRESS`，再复核其不可变来源及是否误把 channel cap、采集链路或本机观察写入产品 policy。未齐全时保持 `BLOCKED`。
- Validation: `git diff --check` exit 0；`tools/devenv/linux/run.sh python tools/checks/check_markdown_links.py --project-root .` exit 0（`75 file(s), 160 local link(s)`）；`rg -n '[[:blank:]]+$' docs/architecture/KSpaceJet_project_plan_and_acceptance.md` exit 1 且无输出（无尾部空白）。这些命令只验证文档完整性，不会把该阻塞转为 acceptance。

### 13.9 P0-008 ACCEPTED 证据

- Work item: `P0-008`; requirements/acceptance: 用户要求的可追踪 Master Plan 视图，以及本项第 10 节的唯一状态来源、一致性检查和可发现入口要求。
- Base commit/tree: `8c31b30419ed330688b3f1b90f14a4498503317d` / `4455dc2fb45214952b710fa5519d8d084e9efad5`；开始时工作树已含 P0-001、P0-004、P0-005 的已接受未提交改动、P0-002/P0-003/P0-006 的真实阻塞记录及用户已有的 `AGENTS.md` 编辑，均未覆盖或回退。
- Changed surface: 本文件改为 Codex 主实施计划并新增第 0.4 节 marker-controlled dashboard；其内容从第 12 节生成阶段覆盖度、唯一活动/READY 项、BLOCKED 项、最近三条 ACCEPTED 证据和阻塞恢复动作。新增 `tools/checks/check_execution_plan.py`（标准库、无网络），检查第 10 节任务目录与第 12 节 ID 集合完全一致、合法状态、唯一 READY/活动项、READY/活动项的显式依赖、canonical-plan 三个入口和 dashboard 漂移。它只写入 markers 之间的投影。Linux `ci_check`/`pre_commit` 与 Windows `pre_commit` 已接入；root README、docs README 和 checks README 均说明 canonical route 与更新流程。无 public API/ABI/schema/CLI 或产品能力变更。
- Focused validation: `tools/devenv/linux/run.sh python -m py_compile tools/checks/check_execution_plan.py` exit 0；`tools/devenv/linux/run.sh python tools/checks/check_execution_plan.py --self-test` exit 0（13/13，覆盖 stale-dashboard、ID drift、入口丢失、unsupported status、duplicate ID、multiple active/READY、unsatisfied READY dependency、evidence/blocker projection、write/check 和百分比）；`tools/devenv/linux/run.sh python tools/checks/check_execution_plan.py --project-root . --write` 与同命令 `--check` 均 exit 0（54 work items）。
- Complete validation: `tools/devenv/linux/run.sh python tools/checks/check_markdown_links.py --project-root .` exit 0（`75 file(s), 165 local link(s)`）；`bash -n tools/checks/linux/ci_check.sh tools/checks/linux/pre_commit.sh` exit 0；`bash tools/checks/linux/ci_check.sh` exit 0（local Markdown links、execution-plan dashboard、172 C/C++ clang-format、32 CMake files 和 `linux-release` configure）；`git diff --check` exit 0；`! rg -n '( {3,}|[[:blank:]]+\\t)$' docs/architecture/KSpaceJet_project_plan_and_acceptance.md` exit 0（允许 Markdown 的两个空格 hard break，未发现意外尾部空白）。
- Platform/toolchain: Debian GNU/Linux 13 (trixie), Linux 6.12.101 x86_64, GCC/G++ 14.2.0, CMake 3.31.6, Python 3.13.5；checker 只使用 Python 标准库，CI configure 使用已准备的 repository-local developer environment。
- Known limitations: dashboard 是同一 Markdown 文档中的离线投影，不是远程 issue tracker、CI service 或第二份计划；它不证明任何产品功能。当前 Linux 主机无 PowerShell/Windows host，未执行 Windows pre-commit；这不替代 P0-002 的 Windows acceptance。P0-002、P0-006 的既有 BLOCKED 条件保持不变。
- Next READY item: 无。下一位 Codex 必须先读取第 0.4/12 节；只有 P0-002 的 Windows 主机或 P0-006 的必填 owner/source/review inputs 实际到位后，才能按记录的 predicate 恢复相应项。

### 13.10 P0-003 ACCEPTED 证据

- Work item: `P0-003`; requirements/acceptance: 固定本文件为唯一状态账本、建立恢复流程、禁止无同步机制的第二 TODO/工作项系统；`AC-BLD-004`。
- Delivery commit/tree: `e1150f4b24627f5f5b847f57ee4d633a8f8b33c1` / `01c12fcd1b52fe45a86a3a26691bcfbf264e6589`，message 为 `feat: establish canonical execution governance and contracts`。该 commit 已将 `AGENTS.md`、本文件、相关 documentation、schema/fixture/test、offline Markdown/execution-plan checkers 和 hook wiring 写入当前实际仓库。
- Acceptance evidence: commit hook 运行并通过 staged C/C++ clang-format、CMake format、local Markdown link check（75 files、165 local links）、execution-plan dashboard check、CMake preset listing 和 staged CMake configure；commit message gate 也通过。首次 hook 仅报告 `scan_lifecycle.cpp` 格式偏差，已使用 repository-local `clang-format` 格式化该精确文件、重新暂存并复跑通过；未回退任何用户改动。
- State/recovery evidence: `AGENTS.md` 明确本文件是 canonical execution ledger；第 0/12/13 节完整，`tools/checks/check_execution_plan.py --check` 已验证 dashboard 与 54 项台账一致。第 10/12 节 ID 集合、状态、依赖、唯一 READY/活动项及入口链接均受离线检查器保护。
- Known limitations: 未创建或推送远程 branch；此前 GitHub integration 403 仍是历史远程操作失败证据，但 P0-003 的明确本地 repository delivery/commit 条件已经满足。远程 CI、branch protection 和 release 仍属于 P0-007/P7。
- Next READY item: 无。P0-002 需要 Windows host，P0-006 需要产品参数 authority；两者保持 BLOCKED。

### 13.11 P0-009 ACCEPTED 证据

- Work item: `P0-009`; requirements/acceptance: 用户明确的双仓库工作区约束、`BND-009`，以及本项第 10 节的 raw-payload、sibling identity、hook、旧 local-data cleanup 和文档要求。
- Base commit/tree: `fc40fc278825de68323563adea09196e81d44295` / `b5209f56d60afe14768a7e43202bf76bc35ec01b`。本项尚未创建新 commit；仅改动本项的边界、检查、文档、hook 与删除路径。
- Changed surface: 新增标准库 `tools/checks/check_workspace_layout.py`。它要求真实（非 symlink）的同级 `../KSpaceJet-ismrmrd-data` Git worktree、canonical GitHub origin、`catalog.yaml`、`datasets/` 与 `tools/verify-data.sh`；它拒绝 KSpaceJet index 或物理工作树中的 `.mrd`、`.h5`、`.hdf5`、`.ismrmrd` payload（排除 `.git`、`out`、`build`、`.venv`、`.kspacejet`）。Linux/Windows pre-commit 均接入该 check；AGENTS、root README、developer/check 文档和 BND-009 明确同级布局。删除历史 `research/benchmarks/datasets/` 的 manifest/license evidence 和三个 local Gadgetron raw payload，以及仅服务该目录的 `research/benchmarks/tools/fetch_dataset.py` 与 `research/benchmarks/tests/test_fetch_dataset.py`；未修改 data repo。
- User-authorized removal: 用户在本任务中明确回复“都删除”。先精确确认旧目录仅含三个 raw 文件与三个 tracked metadata 文件；tracked metadata/downloader/test 通过本变更的 Git deletion 删除，三个未跟踪 raw payload 所在目录以 `gio trash -f /home/qiwen/Workspace/KSpaceJet/research/benchmarks/datasets` 移入本机 Trash，并将仅含该 downloader bytecode 的 `research/benchmarks` 残余目录同样移入 Trash。结果：`test ! -e research/benchmarks/datasets`、`test ! -e research/benchmarks/tools`、`test ! -e research/benchmarks/tests` 均成立；未使用不可恢复的 recursive delete。旧 payload 如有需要可通过桌面环境 Trash 恢复，tracked 文件则可在该变更提交前通过 Git 恢复，但二者均不得重新放入 KSpaceJet。
- Focused validation: `tools/devenv/linux/run.sh python -B -m py_compile tools/checks/check_workspace_layout.py` exit 0；`tools/devenv/linux/run.sh python tools/checks/check_workspace_layout.py --self-test` exit 0（6/6，覆盖 valid sibling、missing contract path、wrong origin、symlinked sibling、tracked/untracked raw payload）；同 checker `--project-root .` exit 0。`bash tools/verify-data.sh` 在 `../KSpaceJet-ismrmrd-data` exit 0（`Verified 6 dataset directories.`）；该 repo 仍为 clean，HEAD `8a5e1dec165e55e17e5afeed0f85af7f831c4668`，origin 为 `git@github.com:isqiwen/KSpaceJet-ismrmrd-data.git`。
- Complete validation: `tools/devenv/linux/run.sh python tools/checks/check_markdown_links.py --project-root .` exit 0（73 Markdown files、162 local links）；`tools/devenv/linux/run.sh python tools/checks/check_execution_plan.py --self-test` exit 0（13/13）；同 checker `--write` / `--check` exit 0（55 work items）；`bash -n tools/checks/linux/pre_commit.sh tools/checks/linux/ci_check.sh`、`git diff --check`、tracked raw suffix scan、live legacy-reference scan 和 deleted-path assertions 均 exit 0；`bash tools/checks/linux/ci_check.sh` exit 0（link/dashboard/format scope 与 `linux-release` configure）。
- Platform/toolchain: Debian GNU/Linux 13 (trixie), Linux 6.12.101 x86_64, Python 3.13.5；checker 只用 Python standard library、Git 与本地 filesystem，不访问网络、不下载/复制 raw data。
- Known limitations: 历史 Linux host 无 `powershell` / `pwsh`，当时 Windows pre-commit 仅静态审查；2026-08-22 在真实 Windows linked worktree 的 commit hook 中发现 Git hook 注入 `GIT_DIR`/`GIT_WORK_TREE` 会让 checker 对 sibling data repository 调用错误的 Git worktree。按状态机 P0-009 已 REOPENED 并修复：`run_git` 现清除本地 worktree Git environment variables，新增回归 self-test；Windows `--self-test`（7 tests，symlink case 因 host privilege 跳过）、显式模拟 hook environment、`just workspace-check` 以及完整 pre-commit 均通过，故重新 ACCEPTED。该修复不改变 P0-002 的 Windows Release-only developer-install scope。checker 按当前项目的 raw ISMRMRD suffix contract 识别 payload，不替代 data repo 的完整 manifest/checksum verifier。
- Next READY item: 无。只有 P0-002 收到真实 Windows x64 + VS 2022 + SDK runner，或 P0-006 收到完整产品参数 authority 后，才能按第 12 节重新选择工作项。

### 13.12 P0-010 ACCEPTED 证据

- Work item: `P0-010`; requirements/acceptance: 本项第 10 节的 project-local `just`、Linux/Windows 同名 recipe、bootstrap/runner/VS Code/hook 收敛和 Linux 实测要求。
- Base commit/tree: `eb323fb0c20abbfdff4310f8bb62ac4075088803` / `f419d09a66d72e7d92377ae5cdee83df05f37fcf`。本项尚未创建 commit；未覆盖 P0-009 已提交内容或任何 ignored build/cache artifact。
- Changed surface: `tools/devenv/tool-versions.env` 固定 `just 1.58.0` 的 Linux musl 与 Windows MSVC archive/binary SHA-256。两端 bootstrap 下载、版本检查和二次 binary digest 检查 project-local `.kspacejet/bootstrap/just/<version>/<platform>/just[.exe]`；runner 在 `.venv` 和系统 PATH 前选择它，并在缺失时拒绝执行 `just`。根 `justfile` 使用 `minimum-version`、Linux/Windows 条件 recipe 和相同的 prepare、incremental build、install、format、check、hook、link/plan/workspace/type 命令；VS Code 的首次 bootstrap 保留直接平台调用，所有其后 prepare/build/install 与 Git hooks 均转到同一 recipe。格式门禁跳过 checksum-owned `third_party/intel/payload/`，将两端 formatter 调用限制为最多 24,000 path characters 的批次（当前 Windows 全仓 clang-format 为两批），并以项目 formatter 修复 6 个既有 C++ 和 8 个 CMake 格式偏差。无 public API/ABI/schema/CLI 产品行为变更。
- Focused Linux validation: `bash tools/devenv/linux/bootstrap.sh --no-hooks` 首次下载并验证 `just 1.58.0`；随后 `bash tools/devenv/linux/bootstrap.sh --verify`、`tools/devenv/linux/run.sh bash -c 'command -v just; just --version; sha256sum "$(command -v just)"'` 均 exit 0，实际 binary 为 `.kspacejet/bootstrap/just/1.58.0/linux-x86_64/just`，digest `3ad66571feae522db2cad31b9613bf21035b8a06d2738d6fc8fe6089856f2042`。`tools/devenv/linux/run.sh just --fmt --check`、`just --list`、`just format-all`、`just format-changed`、`just type-check`、`just link-check`、`just plan-check`、`just workspace-check` 和 `just pre-commit` 全部 exit 0；`bash tools/devenv/linux/bootstrap.sh --no-hooks --smoke` 亦经 `just pre-commit` / `just format-changed` 通过。
- Complete Linux validation: `tools/devenv/linux/run.sh just prepare-release`、`just build-release-applications`、`just install-release-applications`、`just check` 和 `just unit` 均 exit 0；unit CTest 35/35 通过。`.githooks/pre-commit` 经 runner 调用 `just pre-commit` exit 0。一次既有 Ninja metadata `premature end of file; recovering` warning 被追溯到忽略且可再生的 `out/build/linux-release/.ninja_log` / `.ninja_deps`；确认二者均为 `/out/` 下 regular file 后，以精确 `find out/build/linux-release -maxdepth 1 -type f \( -name .ninja_log -o -name .ninja_deps \) -delete` 删除并立即重新运行 build/install（160-step app build、54-step install build，均无该 warning）。未删除 tracked source 或数据。`bash -n .githooks/pre-commit .githooks/pre-push tools/devenv/linux/bootstrap.sh tools/devenv/linux/run.sh tools/checks/linux/format_check.sh tools/checks/linux/install_hooks.sh`、`tools/devenv/linux/run.sh python -m json.tool .vscode/tasks.json` 与 `git diff --check` 全部 exit 0。`cmake-format` 的两条既有 `${_KSJ_INSTALL_*}` install-form warning 未导致失败。
- Windows static wiring: 检查 `windows/bootstrap.ps1` 的 `Install-ProjectJust`/checksum、`windows/run.ps1` 的 project-local PATH 前置、`.githooks/pre-commit` 和所有 Windows VS Code post-bootstrap task 的 `run.ps1 just <recipe>` 接线，以及根 justfile 的 `[windows]` recipes。当前 658 个 C/C++ tracked paths 按相同 24,000-character 算法拆为 2 批（340 / 318 paths，最长 23,988 characters，小于 CreateProcess 32,767），107 个 CMake paths 为 1 批（4,956）；因此 Windows `format-all` / `check` 不再依赖超长单一 argv。
- Platform/toolchain: Debian GNU/Linux 13 (trixie), Linux 6.12.101 x86_64, GCC/G++ 14.2.0, repository-local `just 1.58.0`, Python 3.13.5, Conan 2.31.2, CMake 3.31.6, Ninja 1.13.0, clang-format 19.1.7, cmake-format 0.6.13.
- Known limitations: 当前 host 无 `powershell` / `pwsh`、Windows kernel、MSVC 或 Windows SDK，因此未执行 Windows binary download/bootstrap/PowerShell recipe；静态审查不能替代该运行证据。P0-002 仍是此真实 Windows validation 的唯一 BLOCKED owner，且本项不宣称 Windows build/install/release qualification。开发者不得使用系统 `just`；首次 bootstrap 仍是有意保留的引导例外。
- Next READY item: 无。只有 P0-002 的 Windows host 或 P0-006 的完整参数 authority 到位后，才能依第 12 节状态机重新激活任务。
- 2026-08-21 reopened/regression resolution: 在真实 Windows 10.0.26220、PowerShell 5.1.26100.9202 中，P0-002 的首次 bootstrap 尝试暴露 `bootstrap.ps1:99`、`:334` 将 `$LASTEXITCODE:` 置于双引号字符串内，导致 `InvalidVariableReferenceWithDrive` 解析失败。该发现使原 Windows static-wiring evidence 失效，P0-010 已先置为 `REOPENED`。两处均改为 `${LASTEXITCODE}:`；随后以 `[System.Management.Automation.Language.Parser]::ParseFile` 验证 `tools/devenv/windows` 与 `tools/checks/windows` 下全部 PowerShell 脚本均无解析错误，并静态断言 legacy `$LASTEXITCODE:` 为零、正确的 `${LASTEXITCODE}:` 为两处。`py -3 -m json.tool .vscode/tasks.json`、`py -3 tools/checks/check_execution_plan.py --project-root . --write`、同命令的 `--check`、`py -3 tools/checks/check_markdown_links.py --project-root .`（73 files、162 local links）和 `git diff --check` 均 exit 0。实际 bootstrap 随后正确到达 prerequisite 检查并报告缺少 VS 2022 v143 C++ Build Tools 和 Windows SDK；该外部前提仍只阻塞 P0-002，不阻塞本项静态入口修复的重新验收。
- 2026-08-21 host-just migration: 用户明确要求不锁定 `just` 版本，故 P0-010 重新打开并替换旧 contract。删除 `tools/devenv/tool-versions.env` 的全部 `KSJ_JUST_*` 数据、Linux/Windows bootstrap 的下载/checksum/cache 安装路径、runner 的 project-local PATH prepend/版本检查以及 `justfile` 的 minimum-version；两端 bootstrap 现在只检查 host `just` 存在，所有标准文档示例直接执行 `just <recipe>`，runner 仅保留给 locked Python tooling 的聚焦诊断和 hook/recipe 内部调用。`D:\Git\bin\bash.exe -n tools/devenv/linux/bootstrap.sh tools/devenv/linux/run.sh tools/checks/linux/install_hooks.sh`、Linux bootstrap `--help`、全部 Windows devenv/check PowerShell parser check、Windows bootstrap `-Help`、obsolete just-lock reference scan、VS Code task JSON、Markdown link check（73 files、162 links）和 `git diff --check` 均通过。当前 `where just` 无结果，Windows bootstrap `-Verify` 正确报 `host prerequisite is missing: just`，且当前没有可用 Linux host 运行新的直接 `just` acceptance；P0-010 保持 BLOCKED，直到取得该实际证据。
- 2026-08-21 automatic-Windows-install revision: Windows bootstrap 不再把 `just` 作为先验前提；它在缺少命令时使用 winget 安装，并在 package 没有创建 WinGet Links 时解析 `Casey.Just_*\just.exe`、更新当前和 user PATH。PowerShell parser check 通过；实际 `bootstrap.ps1 -Verify` 已在同一进程解析该 executable，并仅因 VS v143/SDK 前提失败。README、AGENTS、开发/检查/commit 文档和 P0-010 contract 已说明 Linux 仍要求 host just、Windows 使用 winget 自动安装，且均不锁定仓库版本。P0-010 仍 BLOCKED：改动后的 Linux bootstrap 和 direct `just` recipes 尚未在真实 Linux GCC 14 host 上执行，Git Bash 静态语法检查不能代替该证据。
- 2026-08-22 apt revision: 用户选择 apt 作为 Linux 缺少 `just` 时的安装策略。`tools/devenv/linux/bootstrap.sh` 现在先以 `command -v just` 复用现有安装；只有缺失时才调用 `sudo apt-get update` 和 `sudo apt-get install --yes --no-install-recommends just`，`--verify` 与 `--offline` 均不会触发安装。Linux runner 在 `just` 缺失时指向 bootstrap。`D:\Git\bin\bash.exe -n tools/devenv/linux/bootstrap.sh tools/devenv/linux/run.sh tools/checks/linux/install_hooks.sh` 与 `D:\Git\bin\bash.exe tools/devenv/linux/bootstrap.sh --help` 通过；`tools/devenv/windows` 和 `tools/checks/windows` 的全部 PowerShell 脚本经 `Parser::ParseFile` 无错误；Linux apt guard 静态断言、`py -3 tools/checks/check_execution_plan.py --project-root . --write`/`--check`、`py -3 tools/checks/check_markdown_links.py --project-root .`（73 files、162 local links）、`.vscode/tasks.json` JSON parse、obsolete just-lock scan 和 `git diff --check` 均通过。当前仅有 Windows host，无法实际执行 apt 安装、复用分支或 Linux recipes，故 P0-010 BLOCKED，直到取得真实 Linux x86_64 + apt + GCC/G++ 14 运行证据。
- 2026-08-22 apt idempotence revision: 用户要求不再对 Linux `just` 作显式存在判断，因为 apt 本身不会重复安装已安装 package。正常 bootstrap 现在无条件执行 `sudo apt-get update` 与 `sudo apt-get install --yes --no-install-recommends just`；只有 `--verify` 或 `--offline` 保持只读检查并要求现有 `just`。`D:\Git\bin\bash.exe -n tools/devenv/linux/bootstrap.sh tools/devenv/linux/run.sh tools/checks/linux/install_hooks.sh`、bootstrap `--help` 以及静态断言（`ensure_host_just` 无 `command -v just` 且包含两个 apt 命令）通过；PowerShell parser、计划 write/check、Markdown link check（73 files、162 local links）、VS Code JSON、obsolete just-lock scan 和 `git diff --check` 也通过。仍缺真实 Linux x86_64 + apt + GCC/G++ 14 的运行证据，P0-010 保持 BLOCKED。
- 2026-08-22 Windows stale-PATH runner revision: 用户从 VS Code task 报告 `tools/devenv/windows/run.ps1 just prepare-release` 在新终端未继承 Winget user PATH 时错误报 missing `just`。runner 现仅为当前进程扫描 `%LOCALAPPDATA%\Microsoft\WinGet\Packages\Casey.Just_*\just.exe` 并前置该目录，不写 user PATH、不重装 package。全部 `tools/devenv/windows`/`tools/checks/windows` PowerShell parser checks 通过；移除该目录后的模拟环境中，`powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 just --version` 输出 `just 1.58.0`，同样的 `just --list` 正常列出 recipes。`check_execution_plan.py --write`/`--check`、Markdown link check（73 files、162 local links）、obsolete just-lock scan 和 `git diff --check` 均通过。该 Windows regression 已修复；P0-010 仍仅因真实 Linux apt/GCC 14 运行证据缺失而 BLOCKED。

- 2026-08-22 VS Code direct-just revision: 用户指出 post-bootstrap VS Code tasks 不应再通过 `run.ps1 just` 或 `run.sh just` 间接启动。`.vscode/tasks.json` 的 Linux/Windows prepare、build、install 共 12 项现全部为 `"command": "just"` 加同名单 recipe argument；首次 bootstrap 任务仍直接调用各平台 bootstrap 脚本。`py -3 -m json.tool .vscode/tasks.json` 与 PowerShell 映射断言通过，验证所有 12 项的 command、单一 recipe argument 与 bootstrap 保留行为；以 User/Machine PATH 重建新进程语义后直接运行 `just --list`，正常列出 18 个 recipes。`tools/devenv/README.md` 现在明确要求 Winget 首次安装后重启已开启的 VS Code/终端，再直接调用 `just`；runner 的 PATH fallback 只用于聚焦诊断。`py -3 tools/checks/check_execution_plan.py --project-root . --write`/`--check`、`py -3 tools/checks/check_markdown_links.py --project-root .`（73 files、162 local links）、obsolete just-lock scan 和 `git diff --check` 均通过。P0-010 仍 BLOCKED，直到取得真实 Linux x86_64 + apt + GCC/G++ 14 的 bootstrap、direct-just 和完整检查运行证据。
- 2026-08-22 VS Code process-resolution regression: 用户实际运行 direct task 得到 `Path to shell executable "E:\\KSpaceJet\\just" does not exist`，证明 Windows VS Code `type: "process"` 未以 shell/PATH 查找 `just`，使前一条 direct-task 接线证据失效。12 个 post-bootstrap prepare/build/install task 均改为 `"type": "shell"`，保留 `"command": "just"` 与单一 recipe argument；bootstrap 仍是 platform-script `process` task。`py -3 -m json.tool .vscode/tasks.json` 和映射断言验证 12 项均为 shell/direct-just、recipe 对应且 bootstrap 未改变；以 User/Machine PATH 重建的 PowerShell 子进程执行 `& just --list` 成功列出 18 个 recipes。计划 write/check、Markdown link check（73 files、162 local links）、obsolete just-lock scan 与 `git diff --check` 均通过。首次 Winget 安装后，已开启的 VS Code 仍必须重启以获得 user PATH；P0-010 仅因真实 Linux x86_64 + apt + GCC/G++ 14 验收证据仍缺失而 BLOCKED。

- 2026-08-22 Windows environment-notification revision: 用户在完全重新打开 VS Code 后仍得到 `just : 无法将“just”项识别为 cmdlet`。检查实际 user PATH 已包含 `C:\Users\wangqiwen\AppData\Local\Microsoft\WinGet\Packages\Casey.Just_Microsoft.Winget.Source_8wekyb3d8bbwe`，且其中存在唯一 `just.exe`，但当前进程不能解析，证明旧环境未收到 PATH 更新。`Add-WingetJustToPath` 现无论 PATH 条目是否早已存在，都会发送 `WM_SETTINGCHANGE` / `Environment` 广播。变更后 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\bootstrap.ps1 -Verify` exit 0，并输出 `just 1.58.0`、`developer environment is ready`。全部 8 个 Windows PowerShell 脚本 parser check、12 个 direct shell-`just` VS Code task mapping、tasks JSON、plan write/check、Markdown link check（73 files、162 local links）和 `git diff --check` 均通过。必须完全退出所有 VS Code 窗口后重开并复验任务；真实 Linux x86_64 + apt + GCC/G++ 14 acceptance 仍缺失，故 P0-010 保持 BLOCKED。

- 2026-08-22 runner simplification: 用户确认新开终端即可直接调用 `just`，故删除 `tools/devenv/windows/run.ps1` 的 `Add-WingetJustToCurrentPath` 和 `just` 专用分支。bootstrap 是唯一负责 Winget package 发现、user PATH 更新和环境通知的入口；runner 只前置 `.venv\\Scripts` 并执行聚焦的受管工具。`bootstrap.ps1 -Verify` exit 0 并显示 `just 1.58.0`；`run.ps1 python --version` 输出 `Python 3.13.5`；以 User/Machine PATH 重建环境后 `just --list` 列出 18 个 recipes。全部 8 个 Windows PowerShell parser、12 个 shell/direct-`just` VS Code task 映射、计划 write/check、Markdown link check（73 files、162 local links）和 `git diff --check` 均通过。P0-010 仍仅因真实 Linux x86_64 + apt + GCC/G++ 14 验收证据缺失而 BLOCKED。

- 2026-08-23 resumed: 原 BLOCKED 条件已有可复现的本地解锁路径：Docker `29.7.2` 报告 Linux/amd64 server，而 `wsl.exe -l -v` 没有可用于验证的常规发行版。`P0-010` 已按状态机由该可用环境恢复为 READY 并立即选为 `IN_PROGRESS`；以 `/source` 只读 bind mount 和容器内临时工作副本执行，不接触当前 Windows worktree、同级 raw-data 仓库或任何 GitHub CI 资源。下一步是执行缺失 `just` 的首次 apt bootstrap、第二次 apt 幂等 bootstrap、direct host `just` format/prepare/check 全链路，并只按实际输出决定 ACCEPTED 或 BLOCKED。

- 2026-08-23 persistent-container directive: 用户明确要求保留当前 Linux Docker 环境，并让以后所有 Linux 测试都经由同一容器执行。当前 `ksj-p0-010-linux-validation` 在此指令之前以 `AutoRemove=true` 启动，因而不能直接变更为永久容器；必须在其退出前从其 filesystem 建立 checkpoint，再启动命名、非自动删除的 `kspacejet-linux-test`。该持久容器只能挂载当前仓库为 `/source:ro`，并在容器自身的独立工作副本中运行 bootstrap/build/test；这保留 Linux tool/cache，同时不写入 Windows 工作树或 raw-data sibling。

- 2026-08-23 host-just formatter compatibility: Debian 13 apt 的 `just 1.40.0` 在真实容器中拒绝裸 `just --fmt --check`，并明确要求启用 unstable feature；该命令 exit 1 后停止了首次全链路。`just --unstable --fmt --check` 已在该 Debian host 与当前 Windows host 的 `just` 上成功，仍是直接使用宿主 `just`，不下载、不缓存也不锁定仓库版本。后续 Linux validation 改用显式 `--unstable` formatter mode；其余 recipe 均保持裸 `just <recipe>`。

### 13.13 P0-007 BLOCKED 证据（含历史记录）

- Work item: `P0-007`; requirements/acceptance: `FUN-035`, `AC-REL-001`、`AC-REL-002`。用户已明确本轮 Windows 只使用 Release 路径；本设计不把 Debug 作为 P0-007 的 gate。
- Base commit/tree: `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`。开始时已有 22 个未提交条目，均未回退、覆盖或混入本项。
- Exact local/remote read-only investigation: `git remote -v` 显示 `origin git@github.com:isqiwen/KSpaceJet.git`，`git ls-remote --heads origin main` 返回同一 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4`。`rg --files`、`git ls-files .github .gitlab-ci.yml azure-pipelines.yml Jenkinsfile .circleci` 和工作树目录检查均未发现 CI workflow；仓库也没有 `CODEOWNERS`、`SECURITY.md` 或 `CONTRIBUTING.md`。对 `https://api.github.com/repos/isqiwen/KSpaceJet/branches/main/protection` 的只读 `curl.exe --include` 查询返回 `HTTP/1.1 401 Unauthorized` / `{"message":"Requires authentication"}`，不能以无凭据观察替代 protection evidence。GitHub Actions 页面仅显示首次启用引导，而非既有 run。
- Existing local gates: `tools/checks/linux/ci_check.sh` 已覆盖 Markdown link、execution-plan、format 和 Release configure；`tools/checks/linux/ci_unit.sh` 覆盖 Release unit configure/build/CTest；`just prepare-app-tests` + `just app-tests` 覆盖 Linux Release app tests；`just type-check` 覆盖 TypeRegistry。`just check` 本身不执行 type registry、unit 或 app tests。Windows 当前只存在 Release/Debug product presets 且 `BUILD_TESTING=OFF`，没有 Windows CTest/app-test preset；现有 Windows Release build/install/help/DLL closure 是 P0-002 developer evidence，不能冒充 P7 clean-install qualification。
- Proposed GitHub Actions carrier and trigger: 创建单一 `KSpaceJet CI` workflow，触发 `pull_request`/`push` 的 `main` 和 `workflow_dispatch`；`permissions: contents: read`；以 PR number 或 ref 为并发组并取消同组旧 run。`actions/checkout` 使用完整 history，以保证 `ci_check.sh` 可解析 PR base；Intel LFS payload 不作为 artifact，bootstrap 按现有 verifier/pull 逻辑取得所需内容。
- Proposed Release-only required checks: (1) `linux-release-quality` 在 x86_64 Linux、默认 GCC/G++ 14、Git/Git LFS、apt/sudo、curl/wget、tar 和 hash 工具齐备后，先正常 bootstrap，再执行 `just prepare-release`、`KSJ_FORMAT_SCOPE=all just check`、`just type-check`、Release unit prepare/build/CTest 和 `just prepare-app-tests`/`just app-tests`；(2) `windows-release-build-install` 在 `windows-2022`、VS 2022 v143/SDK host 上于同一 PowerShell session 初始 bootstrap 后执行 `just prepare-release`、`just check`、`just type-check`、`just build-release-applications`、`just install-release-applications`、四个 installed `--help`、`ksj --version` 与既有 app JSON protocol script。`linux-release-unit-tests` 有 bootstrap/CMake preset 但没有 `just prepare-unit-tests` recipe；在 workflow 落地前必须将该映射补入 root justfile 或以获批准的等价单一入口处理，不能把 preset mapping 随意复制进 YAML。
- Artifact/retention design: 初始 CI 不缓存或上传 `out/`、install tree、Conan cache、`.venv`、Intel LFS payload 或任何数据；仅在失败时上传 `out/build/**/CMakeFiles/CMakeConfigureLog.yaml` 和 `Testing/Temporary/**`，`retention-days: 14`、无文件时忽略。无 `conan.lock`，因此 dependency-resolution/cache policy 留给 P7-004；benchmark、static-analysis、clean-install/closure、Provider load 和长稳 artifacts 留给 P7，不加入本 P0 required checks。
- Proposed main protection/ruleset: 两个实际 workflow context 均须成功且分支为最新；PR 才能合并；至少一名 approval、推送新 commit 后撤销旧 approval、全部 review conversation 已解决；禁止 force push 与删除。管理员 bypass、允许推送者和单维护者 review policy 是 owner 必须决定的产品治理输入；不假定或创建 `CODEOWNERS`。
- Historical unblock condition: 在此历史记录时，用户尚未明确授权创建/推送 workflow、配置 remote protection/ruleset 或使用 GitHub 管理权限；owner 也未决定 review/bypass/maintainer policy。当前用户已授权 Release-only workflow 与 `main` protection 的远端写入；按上述设计落地、运行至少一个 PR/push 的两个成功 check，记录实际 context 名称、run URL、失败日志 artifact 和 protection/ruleset evidence，才可接受本项。
- Impact/next action: `P7-001` 仍受本项阻塞。当前无 READY 项；不得以 Release 本机构建或本地 hook 替代远程 CI/branch protection。

- 2026-08-22 resumption: 用户已明确授权创建并推送 Release-only GitHub Actions workflow、配置 `main` protection。`gh` command 在当前 Windows host 不存在；本项已由 BLOCKED 转为 IN_PROGRESS，先验证可用的 GitHub API 或 SSH 写入权限，再实施本节已定义的 workflow 与 protection 设计。该授权不扩大到 Debug、benchmark、clean-install qualification、Provider load、性能或任何产品发布宣称。
- 2026-08-22 authenticated remote preflight: 通过现有 Git Credential Manager HTTPS credential 调用 GitHub REST API（未输出 credential）确认登录账户 `isqiwen` 对 `isqiwen/KSpaceJet` 具有 `admin=true`、`maintain=true`、`push=true`、`pull=true`；`GET /branches/main/protection` 为 `404`，`GET /rulesets` 为空，`GET /actions/workflows` 为零。SSH `git push --dry-run origin HEAD:refs/heads/codex/p0-007-release-ci` 成功，因此可在隔离 branch 上创建 workflow、PR 和真实 checks，随后按实际 context 配置 classic `main` protection。
- 2026-08-22 first remote run: PR `#1` 的 workflow run `32578740197` 已产生实际 contexts `linux-release-quality` 与 `windows-release-build-install`，但两者均失败，故尚未配置 required checks。Windows `windows-2022` runner 缺少 `winget`，而 Linux `ubuntu-24.04` apt 提供的未锁定 `just 1.21.0` 不支持非必要的 `set default-list`。修复为移除该 setting，并在 CI 同一 Windows PowerShell step 中仅当 `just` 和 `winget` 均不存在时以 runner 自带 Chocolatey 提供 host `just`；随后重推 PR 取得第二次真实结果。

- 2026-08-22 second remote run: PR `#1` 的 workflow run `32578991507` 再次产生上述两个 contexts，但仍均失败，故 protection 继续保持未配置。Linux `just 1.21.0` 不接受 conditional top-level `set shell :=`；Windows fallback 已通过 Chocolatey 提供 `just 1.58.0` 且首次 bootstrap 完成，但 recipe 内再启动的 `powershell.exe` 不提供 `Get-FileHash`，使第二次 bootstrap 的受管 UV 校验失败。修复为删除这两个非必要 conditional setting，Windows recipes 逐条显式以 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File` 执行，并将 bootstrap 的两个 SHA-256 计算替换为 .NET `SHA256` 流式实现。修复后本机 `just --list`、`just --fmt --check`、`just check`、PowerShell parser、`bootstrap.ps1 -Verify` 和 workflow YAML 解析均成功；下一步为提交、推送并等待第三次真实 CI 结果。

- 2026-08-22 third remote run: PR `#1` 的 workflow run `32579993693` 中两个实际 contexts 均已执行 normal bootstrap，但都在 `conan install` 失败：干净 runner 没有 `~/.conan2/profiles/default` / `C:\Users\runneradmin\.conan2\profiles\default`，而原命令只显式传入 `--profile:host`，Conan 因而为 build context 隐式寻找默认 profile。修复为 Linux/Windows bootstrap 均把已选择的项目 profile 同时传为 `--profile:host` 和 `--profile:build`；当前支持的路径都是 native build，故不存在跨编译 build-profile 推断。修复后本机 Windows `just prepare-release` 与 `just check` 成功；下一步为提交、推送并等待第四次真实 CI 结果。保护仍未配置。

- 2026-08-22 user-directed Conan HTTP timeout: 用户要求避免网络慢速下载造成 Conan failure。Linux/Windows bootstrap 的 `conan install` arguments 均新增 `-cc core.net.http:timeout=300`；它是单次命令 core conf，不写入用户 `global.conf`，不改变 Conan/just/dependency version，也不影响 Git LFS transport。受管 Conan `install --help` 确认 `-cc, --core-conf` 支持，且本机 Windows `just prepare-release` 在该参数下完整成功。该修复提交为 `5b6123f1dec392618a1a201510e9bb31ccfa73ba`；workflow concurrency 已将不含该参数的 run `32580409882` 正常取消，并启动 `32581901234` 验证最新 commit。保护继续保持未配置，直至最新两个真实 checks 均成功。

- 2026-08-23 user-directed deferral: 用户明确要求“现在先跳过所有 GitHub CI 相关的内容”。`P0-007` 因而从 `IN_PROGRESS` 变为 `BLOCKED`：不再查询、监控、取消或触发任何 GitHub Actions run，不再修改/push workflow、PR 或 `main` protection，亦不将已有远端 run 解释为 acceptance。该决定不撤销本地 Windows Release、Conan profile 或 HTTP-timeout 的既有事实，但它们不足以满足 `AC-REL-001/002`。解阻条件是用户明确指示恢复 GitHub CI 工作；届时必须先重新读取实际 remote 状态，不能依赖此处的历史 run 记录。

### 13.14 P5-008 ACCEPTED 证据

- Work item: `P5-008`; dependency `P0-005` 已 ACCEPTED。contract impact 仅为产品边界、架构、ADR、mode、requirements、acceptance 和 front-door 文档；未新增 public ABI、schema、CLI、CMake install surface 或 network listener。
- 基线：`978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`。开始时已存在的 dirty worktree 未被回退、覆盖或纳入本项范围。
- 已交付：`docs/architecture/KSpaceJet_gateway_architecture.md` 冻结一个唯一外部边界、public-profile gate、独立 Connector 信任边界、connection/scan/runtime 生命周期、双账本准入、TLS/mTLS 基线、egress/terminal 语义和 P5-009 至 P5-013 路线；AGENTS、README、docs 与 application front door、ADR-004、FUN-036 至 FUN-038、AC-GWY-001 至 AC-GWY-009 和 P0-006 决策门已原子对齐。
- Exact local validation (Windows, 2026-08-23): `py -3 tools/checks/check_execution_plan.py --self-test` — 13 tests passed; `just plan-check` — dashboard current (62 work items); `just link-check` — 74 Markdown files / 169 local links passed; `git diff --check` — passed (only Git's CRLF normalization warning).
- Limitations/next action: `ksj-gateway` 仍是 scaffold，未监听端口、未处理 peer、未建立 TLS、未实现 Connector 或 runtime bridge。`P5-009` 仍为 PLANNED，且受 `P0-006` BLOCKED；必须先由 architecture/security/deployment/integration/output/data-governance owner 提供 GWY-DEC-001 至 007 的来源、范围、取值和 review date。

### 13.15 P1-008 ACCEPTED 证据

- Work item: `P1-008`; requirements/acceptance: `FUN-004`、`FUN-005`、`AC-DAT-001`、`AC-DAT-003`、`AC-DAT-005`、`AC-DAT-006`、`AC-DAT-008`、`AC-DAT-009`、`AC-DAT-010`。
- State history and baseline: `IN_PROGRESS → VERIFYING → ACCEPTED` on 2026-08-23. Base commit/tree: `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`; existing user and prior-worktree changes were preserved.
- Changed surface: added `ismrmrd_semantic_ingress` to recon-runtime and its focused synthetic tests; migrated generic `ismrmrd_hdf5_replay`, Cartesian RSS and non-Cartesian RSS to it; changed the generic replay resolver to bind only order/placement; extended `FrameSemanticKey`, hashing, serial order and Provider key serialization with `segment`. The public pre-release replay binding API was directly replaced, with no compatibility alias.
- Semantic/materialization evidence: one normalizer performs HDF5 header/layout/finite validation, decodes control facts separately from semantic lanes, invokes the common `AcquisitionClassifier`, rejects unknown/unsupported acquisition semantics fail-closed, and projects the complete frame key. HDF5 sample/trajectory spans remain borrowed only in the reader callback; each route copies them synchronously into its own bounded host-owned FrameSlot, HostFrameAssembler or executor ingress storage. This deliberately avoids a second unbounded/common staging copy while preventing a borrowed view from crossing an asynchronous boundary.
- Completion/terminal evidence: the shared generic replay continues through `SerialCartesianPipeline`; `KSpaceJetCartesianFrameSlot.AppliesAllEndOfInputMissingPoliciesWithoutCountGuessing` and `KSpaceJetSerialCartesianPipeline.ResolvesPartialFailAndCertifiedSkipAtEndOfInput` cover exact required-index readiness and EndOfInput incomplete outcomes. The migrated HDF5 tests cover duplicate/missing Cartesian coverage, mixed FrameKey rejection, control flags that do not imply completion, non-imaging lanes, malformed sample/trajectory layout, nonfinite values, reserved/unsupported flags, reverse readout rejection, and resolver-visible normalized facts.
- Exact Windows Release validation (2026-08-23): `conan install . --output-folder=out/build/windows-vs2022-release-unit-tests --profile:host=conan/profiles/windows-msvc2022-release --profile:build=conan/profiles/windows-msvc2022-release -c tools.build:skip_test=False -cc core.net.http:timeout=300 --build=missing`; separate unit-tree CMake configure; `cmake --build out/build/windows-vs2022-release-unit-tests --config Release --target ksj_recon_runtime_tests --parallel 4` exit 0; generated `conanrun.bat` plus the build-bin and Intel runtime `PATH` then `ctest --test-dir E:\KSpaceJet\out\build\windows-vs2022-release-unit-tests -C Release -R "^recon\.kspacejet-recon-runtime\.ksj_recon_runtime_tests$" --output-on-failure` exit 0 (1/1). Bare CTest was not used as acceptance evidence because its DLL search path lacks the generated Conan/Intel runtime environment.
- Additional validation: `just build-release-applications` exit 0 (all four applications); `py -3 tools/type_registry/generate.py --project-root . --check` exit 0; Windows managed `clang-format --dry-run --Werror` on all changed C++/headers/tests exit 0; `py -3 tools/checks/check_execution_plan.py --self-test` 13/13; `just plan-check`, `just link-check` (74 Markdown files / 169 local links) and `git diff --check` all exit 0.
- Limitations and next item: no sibling raw payload was copied, symlinked, vendored, or tracked in this repository; `P1-009` subsequently used an explicit read-only sibling path only for development execution evidence. This does not accept a standardized output artifact, RunRecord, arbitrary-channel capacity, product envelope, performance, image-quality, or clinical behavior. There is currently no READY item.

### 13.16 P1-009 ACCEPTED 证据

- Work item: `P1-009`; requirements/acceptance: `FUN-023`, `AC-REF-008` through `AC-REF-011`, and `AC-REF-013`. State history: `IN_PROGRESS` → `VERIFYING` → `ACCEPTED` on 2026-08-23. Base commit/tree: `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`; existing user and prior-worktree changes were preserved.
- Changed pre-release surface: added the public numerical `radial_gridding` workspace API; added installed Provider contract/catalog/CMake identity for `radial_gridding_reconstruct`; and added the declarative CLI11 `ksj-recon radial-rss` command plus `kspacejet.radial-rss-*` JSON results. The command requires an explicit raw coordinate convention: `cycles-per-fov`, `radians-per-pixel`, or `encoded-matrix-index`; there is no alias or compatibility mode under `noncartesian-rss`.
- Reconstruction core evidence: `radial_gridding_reconstruct` accepts only `radial_analytic_ramp` and canonical `radians_per_pixel`. It uses 2-D periodic linear Cartesian gridding followed by a caller-owned inverse transform. Its image grid, FFT intermediate, source/destination line buffers, and density vector are all accounted by the Provider scratch formula `(2*rows*cols + 2*max(rows, cols))*sizeof(complex<float>) + samples*sizeof(float)` (maximum 4,464,640 bytes under the contract). The actual Provider limits each image axis to a power of two in `[2, 512]`; the numeric core uses caller-buffer-only radix-2 FFT on that path, with no Eigen FFT plan/cache/heap allocation. The retained direct-adjoint Operator remains unweighted and separate; a same-DCF direct NUDFT is used only as the numerical oracle in focused tests.
- Runtime semantic evidence: raw ISMRMRD trajectory is read as `[kx, ky]`. At the HDF5 boundary, `kx` is normalized by encoded width and `ky` by encoded height, then materialized once as canonical Provider `[row=ky, column=kx]` in `radians_per_pixel`. Synthetic 2×4 rectangular tests cover this asymmetric-axis conversion, equivalent cycles/radians/encoded-index inputs, required radial XML, missing/invalid units, out-of-range/nonfinite values, malformed shapes, contract mismatch, and non-power-of-two route rejection before output.
- Exact Windows Release focused validation (2026-08-23): repository-managed CMake built `ksj_nufft_tests`, `ksj_noncartesian_recon_provider_tests`, and `ksj_recon_runtime_tests`; generated `conanrun.bat`, build-bin, and Intel runtime `PATH` then ran `ctest --test-dir E:\KSpaceJet\out\build\windows-vs2022-release-unit-tests -C Release -R "^(numerics\.kspacejet-nufft\.ksj_nufft_tests|providers\.kspacejet-noncartesian-recon\.ksj_noncartesian_recon_provider_tests|recon\.kspacejet-recon-runtime\.ksj_recon_runtime_tests)$" --output-on-failure` exit 0 (3/3). These tests cover same-DCF direct-NUDFT comparison, deterministic output, caller-workspace/alias/shape/nonfinite negatives, Provider lifecycle/configuration/resource limits, independent Operator identity, and radial HDF5 route/CLI semantics.
- Application/install/contract validation (Windows Release, 2026-08-23): `just build-release-applications` exit 0; `tests/apps/application_json_protocol_tests.py` with all four built apps exit 0; `just install-release-applications` and `just smoke-release-install` exit 0, including installed `radial-rss --help`; `py -3 tools/type_registry/generate.py --project-root . --check` exit 0; provider catalog validation reported 16 implemented contracts / 17 planned interfaces and bundle identity validation reported 6 Providers / 16 descriptor-order Operators. Managed `clang-format --dry-run --Werror` and `cmake-format --check` on P1-009 paths, `just workspace-check`, `just link-check` (74 Markdown files / 169 local links), and `git diff --check` all passed.
- Sibling execution evidence (Windows Release, current binary, 2026-08-23): with the generated Conan runtime environment, ran `ksj-recon radial-rss --input E:\KSpaceJet-ismrmrd-data\datasets\zen-2d-radial-2025\radial2D_24spokes_golden_angle_with_traj.h5 --output E:\KSpaceJet\out\p1-009-zen-2d-radial-gridding-final-current.f32 --metadata E:\KSpaceJet\out\p1-009-zen-2d-radial-gridding-final-current.json --radial-provider E:\KSpaceJet\out\build\windows-vs2022-release\bin\ksj-noncartesian-recon.dll --radial-contract E:\KSpaceJet\providers\kspacejet-noncartesian-recon\contracts\radial_gridding_reconstruct.json --coil-combine-provider E:\KSpaceJet\out\build\windows-vs2022-release\bin\ksj-coil-combine.dll --coil-combine-contract E:\KSpaceJet\providers\kspacejet-coil-combine\contracts\coil_combine_rss.json --trajectory-units encoded-matrix-index --format json` exit 0. It read 24 acquisitions × 256 samples × 16 channels and wrote a 256×256 float32 image (262,144 bytes): all 65,536 values finite and nonzero, range `[0.0005110063, 0.009498747]`; SHA-256 image `2038548bd9d4c61c37c2c7cb2c4358077e58e21d256b93f5e7aef2fc1cb4c294`, metadata `f1da297ec7dc8963a30da032d1a5227aa32ff98270981c8ec2d8f1164b1001b3`. `out/` is ignored and no raw payload was copied to KSpaceJet.
- Limitations: this is development-only execution/finiteness evidence, not an image-quality/golden, standardized artifact, RunRecord, arbitrary-channel-capacity, product-envelope, performance, release, or clinical claim. It intentionally excludes trajectory/phase correction, SENSE, coil compression, spiral, 3-D, cine, EPI, partial Fourier, and GRAPPA. Per user scope, no Linux or GitHub CI work was run. No READY work item remains; `P0-006` authority is the next unblock condition.

- Historical I/O clarification (2026-08-23): the preceding sibling execution command, which wrote `.f32` plus `--metadata`, predates ADR-006/P1-002's replacement of that artifact interface. It remains only as historical development execution/finiteness evidence for P1-009; current `ksj-recon` no longer accepts `--metadata` or emits a raw `.f32` image artifact, and this record must not be reused as current-interface evidence.

### 13.17 P1-002 ACCEPTED 证据

- Work item: `P1-002`; acceptance ownership: `AC-DAT-001`, `AC-ART-007/008`, and the standard-image portions of `AC-REF-002/010/011`. `AC-ART-006` and `AC-REF-006` remain wholly owned by `P1-006`, because a complete input/Pipeline/config/result/timing identity chain requires the later RunRecord rather than an image-file sidecar. State history: `IN_PROGRESS` → `VERIFYING` → `ACCEPTED` on 2026-08-23. Base commit/tree: `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`; existing user and prior-worktree changes were preserved.
- Changed pre-release surface: `IsmrmrdHdf5ReplaySource` and its move-only one-pass `IsmrmrdHdf5ReplaySession` are the runtime-owned standard ISMRMRD HDF5 input boundary. `IsmrmrdImageArtifactSink` is the sole terminal writer for `ksj.image-frame`: it writes one `.mrd` file with `dataset` / `image_0`, standard float magnitude `ImageHeader`/image data, `DataRole=Image`, `ImageNumber=1`, and image-bound `KSpaceJet.*` provenance. The Cartesian, non-Cartesian and radial development routes use this Source/Sink; Providers do not own an HDF5 path or handle.
- Publication and ownership evidence: the Sink rejects a non-`.mrd` target, invalid geometry/provenance, a non-image-frame payload, non-finite/negative magnitude values, and a second publish. It writes a unique sibling temporary HDF5 file, reads it back through the official ISMRMRD C++ binding to verify XML, header mapping, metadata and pixels, then atomically replaces the target and only afterwards acknowledges the graph egress. Focused tests prove replacement of an existing target and prove a final-publication failure preserves the existing destination, removes the temporary sibling, and leaves egress unacknowledged. This is not a power-loss durability, retry, or exactly-once claim.
- Input evidence: Source metadata is copied into host-owned storage; an `AcquisitionView` is exposed only for one callback and a session rejects a second traversal. The serial adapter and all three routes use the shared normalized ingress/materialization boundary rather than retaining HDF5 reader views.
- Exact Windows Release validation (2026-08-23): `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 cmake --build out\build\windows-vs2022-release-unit-tests --config Release --target ksj_recon_runtime_ismrmrd_io_tests ksj_recon_runtime_ismrmrd_route_tests --parallel 4` exit 0. With `out\build\windows-vs2022-release-unit-tests\conanrun.bat`, its `bin` directory, and the generated Intel IPP/MKL/compiler runtime directories on `PATH`, `ctest --test-dir E:\KSpaceJet\out\build\windows-vs2022-release-unit-tests -C Release -R "ksj_recon_runtime_ismrmrd_(io|route)_tests" --output-on-failure` exit 0 (2/2). The I/O target covers source one-pass/borrow rules, Sink normal/negative/publication-failure paths and existing-destination replacement; the route target executes one standard `.mrd` artifact path each for Cartesian RSS, non-Cartesian direct-adjoint RSS, and radial analytic-ramp gridding.
- CLI/protocol validation (Windows Release, generated Conan and Intel runtime environment): all three pre-replacement development route `--format json --help` invocations exit 0, advertise `<image.mrd>`, and omit `--metadata`; `ksj-recon cartesian-rss --format json --metadata legacy.json` exits 2 with exactly one stdout `kspacejet.cartesian-rss-result` JSON error (`invalid_argument`) and only diagnostics on stderr. `E:\KSpaceJet\.venv\Scripts\python.exe tests\apps\application_json_protocol_tests.py` against all four current Release applications exits 0.
- Additional checks: `py -3 tools/type_registry/generate.py --project-root . --check`, `py -3 tools/checks/check_execution_plan.py --project-root . --check`, `py -3 tools/checks/check_markdown_links.py --project-root .` (74 files / 169 local links), focused managed `clang-format --dry-run --Werror`, and `git diff --check` all exit 0. No raw MRI payload was copied, symlinked, vendored or tracked in this repository.
- Limitations and successor: the current public route façades still construct fixed C++ graphs and remain only until the user-required PipelineDefinition migration deletes them. The future public root command is exactly `ksj-recon --input <scan.mrd> --pipeline <pipeline.json> --output <image.mrd>`—not a `reconstruct` subcommand. P2-001 now owns the authored Pipeline / scan-facts / effective-binding boundary; P1-006 later supplies full RunRecord identity and timing evidence. Linux and GitHub CI remain outside the current user scope.

### 13.18 P2-001 ACCEPTED 证据

- Work item: `P2-001`; acceptance ownership: `AC-ART-001` 至 `AC-ART-005` 与 `AC-PLN-001` 至 `AC-PLN-004` 的 artifact ownership / authored-versus-runtime input boundary。基线 commit/tree 为 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`；开始时已有用户与此前工作树改动，均未覆盖或回退。
- Changed pre-release surface: 删除 portable `plan-build-request.schema.json` 和 caller-supplied `PlanArtifactDigests`。`PlanBuildRequest` 仅是由 runtime 组装的强类型 in-memory compiler input；它直接持有 `ResolvedPipeline`、`ScanFacts`、`EffectivePipelineBinding`、TargetEnvelope 和 MachinePolicy。ExecutionPlan 的输入 identity 改为 `resolved_pipeline`、`scan_facts`、`effective_pipeline_binding`、`target_envelope`、`machine_policy`，不再伪造 ScanDescriptor/机器策略的 caller digest。
- Ownership/identity evidence: `ScanFacts` 从实际 raw ISMRMRD XML 派生唯一 domain-separated XML identity，并重解析 XML 验证传入 ScanDescriptor；其严格 parser 拒绝重复键、未知字段、非 canonical payload、替换的 XML/descriptor/detached digest，且 `$schema` decoration 不进入 identity。`EffectivePipelineBinding` 仅可在已有 ResolvedPipeline + ScanFacts 后由 host 派生；它必须包含全部 node、保留每个 authored static config、拒绝静态算法覆盖、路径/loader/contract 和 physical resource 字段，并将最终 canonical config 传给 Provider startup。Pipeline parser/schema 同步拒绝 authored scan shape、外部输入/库路径、loader material 和 physical schedule 字段。
- Fixtures and semantic coverage: 新增 `scan-facts.schema.json`、`effective-pipeline-binding.schema.json` 及其 valid/invalid fixtures；focused tests 实际读取这些 fixture，并验证 static-config preservation、parent digest substitution、loader-field、raw XML、trajectory-range、canonicalization 和 source/descriptor mismatch。Draft schema 文件与相关 fixture 共 13 个由 PowerShell JSON parser syntax-check；其结构/语义双层持续 corpus gate 仍由后续 `P2-006` 扩展，不以 schema syntax 代替 semantic tests。
- Exact Windows Release validation (2026-08-23): repository-managed CMake 分两次构建 `ksj_recon_model_tests ksj_recon_graph_tests ksj_recon_runtime_tests ksj_recon_runtime_ismrmrd_route_tests` 及 `ksj_recon_runtime_ismrmrd_io_tests ksj_provider_loader_tests`，均 exit 0。使用 generated `conanrun.bat`、build `bin` 与 Intel runtime `PATH` 执行 `ctest --test-dir E:\KSpaceJet\out\build\windows-vs2022-release-unit-tests -C Release -L recon --output-on-failure` exit 0（6/6：model、graph、runtime、ISMRMRD I/O、route、provider-loader）。`cmake --build --preset windows-vs2022-release --target ksj_recon --parallel 4` exit 0。
- Additional checks: managed `clang-format --dry-run --Werror` 覆盖本项 C++/test paths；`python tools/type_registry/generate.py --project-root . --check`、`just workspace-check`、`just link-check`（74 files / 169 local links）、`just plan-check` 与 `git diff --check` 均 exit 0。未运行 Linux 或 GitHub CI。
- CLI boundary observed on the current Release binary: `ksj-recon --help` 仅显示当前临时 `cartesian-rss`、`noncartesian-rss`、`radial-rss` façade，未注册 `reconstruct` 子命令。它们仍由 P2-007 原子替换；最终入口固定为 `ksj-recon --input <scan.mrd> --pipeline <pipeline.json> --output <image.mrd>`。
- Limitations and successor: 本项没有实现 P2-002 的 parameter/selector grammar、正式 ISMRMRD input profile 或受控 installed Provider resolver，也没有实现 P2-007 的根 CLI，因此没有产品在线、隔离、性能、临床或 release claim。`P2-002` 成为下一 READY 项；Linux/GitHub CI 仍按用户范围跳过。

### 13.19 P2-002 ACCEPTED 证据

- Work item: `P2-002`; acceptance ownership: `AC-PLN-001` 至 `AC-PLN-007`，以及 `AC-CLI-009` 的 pipeline validate/schema/semantic/resolver 报告层。基线 commit/tree 为 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`；开始时已有用户与此前工作树改动，均未覆盖或回退。
- Changed contract surface: `PipelineDefinition` 现在要求唯一 `ismrmrd-hdf5` input profile，并以闭合 typed parameter declaration/default/`$param` substitution 表达作者化配置；runtime-derived scan facts 只能通过已声明 selector materialize。`ControlledPipelineResolver` 仅接受 host-owned Provider/contract/type snapshot，不接受 caller DLL/SO、contract 路径或 catalog-directory discovery。`ResolvedPipeline` 冻结展开后的 config 和 OperatorContract identity，`ksj pipeline validate --format json` 报告 profile、parameter 和 graph 信息。
- Negative/semantic evidence: focused model/graph tests覆盖 parameter declaration、unknown/missing/wrong-type binding、profile/path/module/scan-fact 字段拒绝、controlled provider/contract/type mismatch、contract digest 与 declared scan-fact materialization；schema-valid 不等于 resolver semantic pass。该项没有让 Pipeline 持有输入/输出路径、scan-derived geometry 或 physical scheduler 字段。
- Exact Windows Release validation (2026-08-23): `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 cmake --build out\build\windows-vs2022-release-unit-tests --config Release --target ksj_recon_model_tests ksj_recon_graph_tests ksj_recon_runtime_ismrmrd_route_tests --parallel 4` exit 0；在 generated `conanrun.bat`、Intel runtime directories 的 `PATH` 下，`ctest --test-dir E:\KSpaceJet\out\build\windows-vs2022-release-unit-tests -C Release -R "ksj_recon_(model|graph|runtime_ismrmrd_route)_tests" --output-on-failure` exit 0（3/3）。`cmake --build out\build\windows-vs2022-release --config Release --target ksj_cli ksj_recon --parallel 4` exit 0；`tests\apps\application_json_protocol_tests.py` 对当前四个 Release applications exit 0。
- Additional checks: repository-managed Python type-registry `--check`、execution-plan `--check`、workspace layout check 与 `git diff --check` 均 exit 0；此前完成的 focused `clang-format --dry-run --Werror`、JSON fixture syntax、local link check 均通过。按用户指令未运行 Linux 或 GitHub CI。
- Acceptance partition: 原台账把 `AC-CLI-009` 整体同时列入 P2-002 和 P2-005，导致 P2-002 被尚未解锁的 P2-003/P2-004/P2-005 阻塞。本次仅将完整 criterion 的 ownership 明确回 P2-005；P2-002 的 validate/schema/semantic/resolver 子层已经验证，项目仍必须在 P2-005 达到 explain/render/dry-run、compiler/verifier/admission 分层报告，未降低任何最终验收要求。
- Limitations and successor: `ksj-recon` 仍保留三条开发专用 façade，P2-007 才能原子替换为 `ksj-recon --input <scan.mrd> --pipeline <pipeline.json> --output <image.mrd>`；P2-003 仍受 P0-006 阻塞。用户新授权的 P8-001 与此相互独立，只实现本地只读 Qt viewer foundation。

---

### 13.20 P8-001 ACCEPTED 证据

- Work item and baseline: `P8-001`，覆盖 `AC-VWR-001` 至 `AC-VWR-003`；基线 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`。
- Delivered surface: `qt/6.8.3` 已作为 shared Conan dependency；`apps/kspacejet-viewer` 是第五个安装 application，使用 `ksj_add_application`、CLI11 help/version、Qt Core/Gui/Widgets、真实 `QApplication → ViewerWindow → event loop` 与 `--ui-smoke --format json`。它不打开 `.mrd`、不解析 Pipeline、不重建、不加载 Provider、也不连接 gateway；四个 inspector tab 明确标为后续 P8-002/P8-003 功能。
- Windows deployment: `ksj_deploy_qt_widgets_runtime()` 复用受控 third-party runtime staging，并用 Qt 官方 `windeployqt` 把按 executable closure 所需的 platform plugin 部署到 `platforms/qwindows.dll`。wrapper 仅为 `windeployqt.exe` 激活 Conan run environment；build-tree 和 installed `ksj-viewer.exe` 的实际启动均不依赖 Conan PATH。
- Exact Windows Release validation (2026-08-23): `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\\tools\\devenv\\windows\\run.ps1 cmake --preset windows-vs2022-release`、`just build-release-applications`、`just install-release-applications` 与 `just smoke-release-install` 均 exit 0。smoke 同时检查 build/install tree 的 `platforms/qwindows.dll`、直接执行两处 `ksj-viewer.exe --ui-smoke --format json`、五个 installed application 的 help、`ksj`/viewer version，以及五应用 JSON protocol。另以 build tree 运行 `tests\\apps\\application_json_protocol_tests.py` exit 0；该 generic protocol 只验证 viewer help，真实 GUI smoke 保持 Windows-only，未形成 Linux GUI 支持声称。
- Link/deployment boundary audit: `dumpbin /DEPENDENTS` 对 build/install `ksj-viewer.exe` 仅列 `Qt6Widgets.dll`、`Qt6Core.dll`，对两处 `platforms/qwindows.dll` 仅列 `Qt6Gui.dll`、`Qt6Core.dll`；检查明确拒绝 `Qt6Network.dll`、`Qt6OpenGL.dll`、`Qt6OpenGLWidgets.dll`、`Qt6Quick.dll`、`Qt6Qml.dll` 与 `Qt6WebEngineCore.dll`。
- Additional checks: viewer C++ 的 `clang-format --dry-run --Werror`、viewer CMake 的 `cmake-format --check`、Qt helper 的 in-memory `cmake-format` comparison、`python -m py_compile tests/apps/application_json_protocol_tests.py`、type-registry check、workspace-layout check、75 Markdown files/169 links 的 link check 与 `git diff --check` 均 exit 0。完整 `KSpaceJetBuildSupport.cmake` 的 `cmake-format --check` 仍因该文件既有的 `install()` parser warning 失败，未将它伪记为全文件格式通过。
- Limitations and successor: 当前 Windows 全局 install staging 可能附带其他应用未使用的 Qt package DLL；本工作项以 viewer 自身与 plugin 的 import closure 作为部署边界证据。viewer 仍只是可部署 UI shell；P8-002 现为活动项，负责 public bounded inspection read model 与 corpus，P8-003 才接入真实 metadata/k-space/image/pipeline/export UI。

### 13.21 P8-002 ACCEPTED 证据

- Work item and baseline: `P8-002`，覆盖 `AC-VWR-004`；基线 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`。既有用户和先前 worktree 改动保持原样，未写入任何 raw MRI payload。
- Delivered public surface: 新增 move-only `KSpaceJet::ismrmrd::InspectionReader`、`InspectionReadLimits`、metadata/series/acquisition/image/header/MetaAttributes read model，以及仅 callback 有效的 acquisition/image payload views。reader 直接使用标准 ISMRMRD HDF5 数据集和官方 binding；私有 HDF5 preflight 仅验证标准 compound/VLEN layout、rank、type、算术与限额，按 named native fields 映射 header，不以文件的 field order、padding 或 byte order 作为 ABI。读者在 callback 内 move/assignment 后仍保持本次 operation 的 source lifetime，嵌套读则确定性拒绝。
- Corpus and negative coverage: Windows focused test 创建临时 synthetic `.mrd`，验证 XML、acquisition、image header、MetaAttributes、轴映射、八种标准 image data type 的非零像素、callback payload、reader limits、错误 logical type、rank-2/fixed XML、错误 VLEN element type、不完整 compound header、reordered/padded image/acquisition header，以及 callback 内移动 reader。测试数据只在 `%TEMP%`，不保留在 repository。
- Build/install contract: `KSpaceJet::ismrmrd` 私有链接 Conan 的 `hdf5::hdf5` target；本地 ISMRMRD Conan recipe 将 HDF5 header/library 传播给 CMake consumer；`inspection_reader.hpp` 作为 public include 被安装。Windows Release install tree 已实际发现 `include/kspacejet/ismrmrd/inspection_reader.hpp`（7151 bytes）和既有 `dataset_reader.hpp`。
- Exact Windows Release validation (2026-08-23): `just prepare-release`、`just build-release-applications`、`just install-release-applications` 均 exit 0。repository-managed tool environment 下 `cmake --build out/build/windows-vs2022-release-unit-tests --config Release --target ksj_ismrmrd_tests --parallel 4` exit 0；以 generated `conanrun.bat` 执行 `ctest --test-dir E:\\KSpaceJet\\out\\build\\windows-vs2022-release-unit-tests -C Release -R ksj_ismrmrd_tests --output-on-failure` exit 0（1/1，0.20 s）。`clang-format --dry-run --Werror`（public header/source/test）和 `cmake-format --check`（两个相关 CMakeLists）均 exit 0；`just type-check`、`just workspace-check`、`just link-check`（76 Markdown files / 172 local links）均 exit 0。
- Limitations and successor: 本项提供的是 native C++ inspection boundary，不是 Qt view、Pipeline presentation、reconstruction 或 export。P8-003 已在同一 Windows-only 用户范围内启动；它必须在 app-local 层消费 reader，且所有 PNG/SVG/CSV/JSON 均明确是显示派生产物而非 MRI artifact。

### 13.22 P8-003 ACCEPTED 证据

- Work item and scope: `P8-003`，覆盖 `FUN-040` / `AC-VWR-005`；基线为 `978215ef9915550bfc3897bb5fe7d4b7ab403ec4` / tree `dce7cc56c199c4c8fa33b3aa7bcee11f589197d0`。既有用户和先前 worktree 改动均保持原样；测试只在 `%TEMP%` 创建 synthetic `.mrd`，没有向 KSpaceJet 写入 raw MRI payload。
- Delivered Qt surface: `ksj-viewer` 现在以 app-local `InspectionSession` 共享 P8-002 的只读 `InspectionReader`，在四个 Qt Widgets tab 中显示 bounded XML/series metadata、按需 acquisition magnitude projection、单个 `[z, channel]` image plane 和 `PipelineDefinition`。k-space UI、summary 和 export detail 均明确标为非 reconstructed image；image/k-space 的 `QImage`、CSV rows、XML preview 与 UI pixmap 都有显式上界，callback-scoped payload 不会被 UI 保存。
- Pipeline and artifact boundary: pipeline 只以 public parser limit 读取文件并调用 `PipelineDefinition::parse_json()`；未复制 parser，也不 resolve、compile、load 或 execute Provider。PNG、SVG、CSV、JSON 均使用 `visualization-derivative` provenance，JSON detail 与各格式标识一致；`.mrd`、`.h5`、`.hdf5`、`.ismrmrd` 和格式不匹配的 destination 被拒绝，故 export 不能成为第二种 MRI artifact。`InspectionSession` 的 open 是事务性的，失败输入不会损坏既有 source provenance。
- Focused evidence (Windows Release, 2026-08-23): repository-managed `cmake --build out/build/windows-vs2022-release-unit-tests --config Release --target ksj_viewer_presentation_tests --parallel 4` exit 0；以 generated `conanrun.bat` 运行 `ctest --test-dir E:\\KSpaceJet\\out\\build\\windows-vs2022-release-unit-tests -C Release -R ksj_viewer_presentation_tests --output-on-failure` exit 0（1/1，0.10 s）。测试创建临时标准 ISMRMRD MRD，覆盖 metadata/k-space/non-reconstruction wording、受限 image derivative、invalid open 保持 session、有效/超限 parse-only pipeline、PNG readback/provenance、SVG/CSV/JSON provenance，以及 MRI extension、错误 extension、null image export 拒绝。
- Windows deployment evidence: `just build-release-applications`、`just install-release-applications` 和 `just smoke-release-install` 均 exit 0。最后一项实际检查 build/install `platforms/qwindows.dll`，执行两处 `--ui-smoke --format json` 和 `--export-smoke --format json`，并运行安装树五应用的 help/version 与 JSON protocol；export smoke 原子写入并读回 temporary PNG provenance。另以 build tree 执行 `tests/apps/application_json_protocol_tests.py` 和 `python -m py_compile tests/apps/application_json_protocol_tests.py` 均 exit 0。
- Dependency and quality evidence: direct CMake link closure 仅为 `KSpaceJet::ismrmrd`、`KSpaceJet::recon_graph`、Qt Core/Gui/Widgets；build/install `ksj-viewer.exe` 的 `dumpbin /DEPENDENTS` 为预期的 `ksj_ismrmrd`、`ksj_recon_graph`、传递的 recon-model/core、Qt6Widgets/Gui/Core 与系统 runtime，两个 `qwindows.dll` 仅依赖 Qt6Gui/Core 与系统 DLL。显式检查未发现 recon runtime、Provider loader、gateway、research、mri_debug、Qt Network/OpenGL/Quick/QML/WebEngine import。viewer/test C++ 的 `clang-format --dry-run --Werror`、相关 CMake 的 `cmake-format --check`、`just type-check`、`just workspace-check`、`just link-check`（76 Markdown files / 172 local links）、`git diff --check` 和 plan checker 均 exit 0。
- Limitations and successor: 此验收仅覆盖用户授权的 Windows Release local developer UI，未运行 Linux 或 GitHub CI；它不实现 reconstruction、Provider、gateway、online/service、clinical/diagnostic、performance 或 release qualification。第 12 节当前没有 READY 项，P0 policy、GitHub CI 与 Linux 仍按其 BLOCKED 记录等待用户输入或恢复授权。

### 13.23 P8-004 IN_PROGRESS 证据

- Work item and reference boundary: `P8-004` 以用户指定的本地 `E:\hdfview` 源码、`D:\HDFView` 安装程序和 HDF Group HDFView 的 File → tree → inspector → typed-view → info/status 模型为**功能与交互参考**。实现独立为 C++/Qt Widgets；未复制、链接、打包或执行 HDFView Java/SWT 代码，也未扩大为通用 HDF browser/editor。
- Delivered shell and interaction: `ViewerWindow` 现在提供 File/Window/Tools/Help 菜单、Open/Close/Inspect/Open As/Help toolbar、只接受本地路径的当前文件栏、语义对象图标树、`Object Attribute Info`/`General Object Info` tabbed inspector、四个 typed data tabs 及跨宽度 Info/status panel。依据用户对 HDFView 截图的视觉反馈，先前 card/dashboard 风格和 General/Header/Attributes 三页已直接替换为 HDFView 式平面 split-pane：General 默认显示紧凑 Name/Path/Type/Access form、standard dataset semantics 和 standard ISMRMRD member table；Object Attribute Info 只显示显式 inspected image 的 standard MetaAttributes，XML header preview 位于 Metadata typed view。树 selection 只更新 inspector；只有 Inspect、Open As、double click 或 context menu 才激活 container 并读取 bounded typed data。file bar Enter、Ctrl+O/Ctrl+W/Ctrl+Q、image cine/window-level/zoom/pixel probe/histogram 和 header-only acquisition table 均保持在只读、bounded display 语义内。
- Dashboard refinement validation (2026-08-24): 用户以真实 raw `cart_t1.mrd` 视觉复核时指出 file-level `Dataset overview` 与空 `Image series` 区域重复且占据主要空间。当前工作树已将其移除：打开 MRD 默认只保留 semantic tree 与 HDFView-style inspector，typed-data 区域保持隐藏；只有对 Header/XML 执行显式 `Inspect`/`Open As…` 才打开 XML typed view。`Images` 是独立语义对象，raw acquisition container 的零 image series 是标准且正常的状态，不再以空表作为 dashboard 内容。`ksj_viewer_presentation_tests` 现在以 nested `dataset_1`/`dataset_2` synthetic MRD 验证默认隐藏、选择 Header/XML 不展开 typed view、显式 Inspect 才显示 XML；选择另一个 semantic object 不会擅自切换已打开的 typed view。
- Focused Windows Release evidence (2026-08-24): `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 cmake --build out\build\windows-vs2022-release-unit-tests --config Release --target ksj_viewer_presentation_tests --parallel 4` exit 0；在 generated `conanrun.bat` 环境中串行运行 `ctest --test-dir E:\KSpaceJet\out\build\windows-vs2022-release-unit-tests -C Release -R ksj_viewer_presentation_tests --output-on-failure` 与 `... -R ksj_ismrmrd_tests --output-on-failure` 均 exit 0（各 1/1）。widget corpus 断言 1280×800 HDFView-style shell 的双 tab 名称/默认 General 页、compact form、semantics/member/MetaAttributes tables，并以 nested `dataset_1`/`dataset_2` temporary MRD 证明选择 `/dataset_2` Images 不切换 data view，显式 Inspect 才读取 image、切换 typed Image view 和只显示该 image 的 MetaAttributes。
- Windows deployment evidence (2026-08-24): `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 cmake --build out\build\windows-vs2022-release --config Release --target ksj_viewer --parallel 4` 与 build-tree `ksj-viewer.exe --ui-smoke --format json` exit 0；`just install-release-applications` 和 `just smoke-release-install` exit 0。install smoke 实际运行 build/install Viewer 的 UI/export smoke，并验证 installed Qt launch path。`windeployqt` 仍报告可选 translations catalog 不存在、runtime staging 报告系统组件候选 unresolved，但两处真实 Qt smoke 均成功；这些 warning 不被记作 failure 或 release qualification。
- Dashboard refinement validation commands (2026-08-24): `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 cmake --build out\build\windows-vs2022-release-unit-tests --config Release --target ksj_viewer_presentation_tests --parallel 4` exit 0；在 generated `conanrun.bat` 环境中 `ctest --test-dir E:\KSpaceJet\out\build\windows-vs2022-release-unit-tests -C Release -R "ksj_(ismrmrd|viewer_presentation)_tests" --output-on-failure` exit 0（2/2）；`cmake --build --preset windows-vs2022-release --target ksj_viewer --parallel 4`、build-tree `ksj-viewer.exe --ui-smoke --format json`、`just install-release-applications` 与 `just smoke-release-install` 均 exit 0。后者在 install tree 实际通过 UI/export smoke；`windeployqt` translations catalog 与系统 runtime candidate warnings 未影响启动验证。
- Windows unit-test runtime deployment remediation (2026-08-24): 根因是 `ksj_add_gtest()` 在 Windows 只注册 test executable，未调用既有 runtime-DLL scanner；直接 `ksj_viewer_presentation_tests.exe` 因缺少 Conan/Qt DLL 返回 `0xc0000135`，而手工 `call conanrun.bat` 只是临时补充 `PATH`。现在每个 Windows gtest target 都在 target post-build 与常规 ALL refresh 中复用 `ksj_stage_thirdparty_runtime_dlls()`，因此 target-only build 会把其 transitive Conan/Intel DLL 闭包部署到 build `bin`；Viewer focused test 同时用官方 `windeployqt` 部署 `platforms/qwindows.dll`。`powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\devenv\windows\run.ps1 -Command @('cmake','-S','.','-B','out\\build\\windows-vs2022-release-unit-tests')`、分别构建 `ksj_viewer_presentation_tests` 与 `ksj_ismrmrd_tests` 均 exit 0（分别报告 staged 92 / 69 DLL）；未调用 `conanrun.bat` 的两个直接 executable 分别通过 7/7 和 21/21，裸 `ctest --test-dir out\\build\\windows-vs2022-release-unit-tests -C Release -R "ksj_(ismrmrd|viewer_presentation)_tests" --output-on-failure` exit 0（2/2）。共享 Qt deployment branch 亦以 `ksj_viewer` Release target build 与直接 `ksj-viewer.exe --ui-smoke` exit 0 复核。runtime scanner 仍会报告 Windows system component candidate unresolved、`windeployqt` 仍会报告缺少可选 translations catalog；实际直接启动成功，二者均非 failure。
- Out-of-scope full-tree observation: 随后 Windows Release unit-tree default build 在未涉及本项的 `ksj_logging_tests` 编译阶段失败：`logging_tests.cpp:112` 将 `std::filesystem::path::value_type *` 传给只接受 `const char *` 的 `ksj::logging::Configure`。这不是 runtime loader failure，未在 P8-004 中修改 logging API/test；本项的两个 direct-launch 和裸 CTest evidence 保持有效。
- Remaining acceptance evidence: 仅剩 Windows-only 用户视觉复核，使用真实 sibling-data `E:\KSpaceJet-ismrmrd-data\datasets\zen-2d-cartesian-2025\cart_t1.mrd` 检查默认 inspector、container tree 和显式 typed views。P8-004 因此保持 IN_PROGRESS；未运行 Linux 或 GitHub CI，且不对 service、clinical、performance 或 release qualification 作任何声明。

## 14. 需求追踪矩阵

| 功能范围 | 功能 ID | 主工作项 | 关键验收 |
| --- | --- | --- | --- |
| 工具链、CI、文件完整性 | FUN-001, FUN-002 | P0-001 至 P0-010, P7-001 至 P7-004 | AC-BLD, AC-TYP, AC-REL-001 至 005 |
| 数据、FrameSlot、artifact | FUN-003 至 FUN-006 | P1-001 至 P1-008, P2-001 | AC-ART, AC-DAT |
| Pipeline/resolve/plan/verify | FUN-010 至 FUN-013 | P2-001 至 P2-006 | AC-SCH, AC-PLN |
| bounded execution/lifecycle | FUN-014 至 FUN-017 | P3-001 至 P3-006, P6-001/002 | AC-SCH, AC-RT, AC-PERF-001 至 004 |
| Provider SDK 和 reference algorithms | FUN-020 至 FUN-023 | P1-003/004/009, P4-001 至 P4-005 | AC-PRV, AC-REF |
| CLI、run artifact、developer experience | FUN-021, FUN-024, FUN-025 | P1-006, P2-005, P4-003/006, optional P5-007 | AC-CLI, AC-OBS-001 至 004 |
| 可选进程内 ISMRMRD feed/宿主 API | FUN-030 至 FUN-032 | optional P5-001 至 P5-007 | AC-FED |
| 外部集成网关与独立 Connector | FUN-036 至 FUN-038 | P5-008 至 P5-013 | AC-GWY |
| 用户授权的 Qt 离线检查器 | FUN-040 | P8-001 至 P8-003 | AC-VWR |
| observability, hardware, qualification | FUN-033 至 FUN-035 | P3-006, P6-003 至 P6-007, P7-001 至 P7-007 | AC-OBS, AC-PERF, AC-REL |

每个 pull request、commit 或 autonomous handoff 应至少能指出一条本表中的功能/工作项/验收链。找不到链条的改动应被视为无范围变更，需要先澄清或拆分。

---

## 15. 当前推荐执行路径

在没有新的用户优先级或阻塞前，Codex 按以下顺序推进：

1. P0-001：实际执行可重复基线，而不是重写已存在 runtime。
2. P0-002：Linux evidence 已完成；仅在真实 Windows MSVC host/runner 上恢复 Windows toolchain 与 test/install evidence。
3. P0-004：修复断链和过时 claim，并建立文档完整性检查。
4. P0-005：收口规范冲突，特别是日志、采集/transport/gateway scope 与 profile 宣称。
5. P0-006：填充或显式阻塞 TargetEnvelope/MachinePolicy 参数。
6. P0-008：已将第 12 节投影为可校验的 Master Plan 总览；此后每次状态变更均先改第 12 节、再执行 `--write` 和 `--check`。
7. P0-009：已固定 KSpaceJet 与 `KSpaceJet-ismrmrd-data` 的同级数据边界；每次开发/提交均维持该 workspace check，不把 raw payload 放回本仓库。
8. P1-008 与 P1-009 已接受为用户授权的真实数据重建核心入口及首个 development-only 2-D radial gridding/analytic DCF Provider 路线；后续 Cartesian、accelerated、dynamic、EPI 与 3-D Provider 仍须在各自依赖变为 READY 后推进。P1-001 至 P1-007 的产品 fixture、artifact、golden、RunRecord 与 policy gate 仍不得绕过。
9. P2、P3：把已有 graph/runtime 通过独立 verifier、resource/terminal/serial-oracle 证据提升为可信开发基线。
10. P4：Provider 开发体验、identity 和隔离路线。
11. P5：P5-008 先冻结唯一外部集成网关的候选稳定架构；P5-009 以后必须在 profile、安全、部署和数据治理输入到位后才实现。in-process feed 仍是独立路线，不能成为 gateway 的私有旁路。
12. P6/P7：性能、硬件、qualification 作为测量和证据项目，不作为预设承诺。
13. P8：用户已选择 Qt；先完成 Qt Core/Gui/Widgets 的可部署 desktop foundation，再以标准 inspection reader 驱动 metadata、k-space、image、pipeline 与派生 export。它不改变 P0-P7 的优先级或 v1 gate。

### 15.1 何时宣布项目完成

只有第 1.4 节的 v1 完成定义和 P7-007 同时满足，才可将本文件状态改为 COMPLETE。任何尚未 ACCEPTED 的强依赖、隐含 compatibility layer、未解释性能结论或未完成平台证据，都意味着项目仍处于进行中。未启用的可选 P5 不阻塞 v1。

---

## 16. 给下一位 Codex 的第一条指令

先阅读第 0.4 节，然后以第 12 节逐项状态为准；本节不是第二份状态来源。`P8-001` 至 `P8-003` 已交付 Qt 6 Widgets `ksj-viewer`、真实 QApplication/platform-plugin deployment、标准 header/acquisition/image 的有界 public inspection reader，以及共享 reader/parser 的 metadata、k-space、image、pipeline 与显示派生 export；它不执行 reconstruction、不加载 Provider、不连接 gateway，也不创建第二种 MRI artifact。`P8-004` 正在 IN_PROGRESS：用户要求以 `ismrmrdviewer` 的 group-first inspection workflow 作为功能参考。必须由原生 `InspectionReader` 有界发现和验证可用的标准 group，并在 Qt workbench 中提供 group → metadata/acquisitions(k-space)/images navigation、header-only acquisition index、按需 coil/trajectory inspection 和 image cine/window-level；不能使用 Python、PySide2、h5py、Matplotlib 或任意 HDF5 浏览，不能把 raw payload 无界载入 UI。`P2-002` 已因固定 `dataset` profile 与标准-first container 原则冲突而 REOPENED；P8-004 接受后，P2-002 必须改为 auto-or-explicit raw-container selection。`P2-007` 仍将最终重建入口固定为 `ksj-recon --input <scan.mrd> --pipeline <pipeline.json> --output <image.mrd>`，没有 `reconstruct` 子命令或旧命令兼容层。`P0-007` 因用户明确要求暂缓所有 GitHub CI 内容而保持 BLOCKED，禁止查询、触发或修改远端资源；`P0-010` 因用户暂停 Linux 验证而保持 BLOCKED，不删除 `kspacejet-linux-test`；`P0-006` 仍阻塞产品 case、正式 artifact、性能、Gateway policy 和资格 claim。

1. 每次有合法状态变更时，只修改第 12 节，然后执行 `python3 tools/checks/check_execution_plan.py --write` 和 `--check`；若检查失败，先修复第 10/12 节 ID、依赖或入口漂移，不能手改总览。
2. 第 13.2 节保留 P0-002 的历史 BLOCKED 记录及其后的 Windows Release ACCEPTED evidence；该证据只覆盖 developer install smoke，不能推导 Debug、clean-machine redistribution、性能、容量或 release qualification。
3. P0-003 已由本地 commit `e1150f4b24627f5f5b847f57ee4d633a8f8b33c1` 接受；P0-006 仍只能在收齐第 6.3.1/13.8 节的 owner/source/review inputs 后改为 READY。
4. P0-009 已 ACCEPTED：KSpaceJet 与 `KSpaceJet-ismrmrd-data` 必须是同级真实 Git worktree；不得将 raw `.mrd`/`.h5`/`.hdf5`/`.ismrmrd` payload、旧 downloader 或 project-internal data directory 放回本仓库。先运行 workspace checker，再在 sibling data repo 运行其 own verifier。
5. 以第 1.3、6.1/6.2、17.1 节与 ADR-002/004 为唯一 scope/mode/diagnostics 权威；P0-005 已 ACCEPTED，P5-008 是用户授权的范围替换，必须保留 plain-text diagnostics、ISMRMRD 语义、无私有 wire 和厂商采集隔离，且不得以 dashboard 扩张已实现 capability。

---

## 17. 十年架构宪章与项目组合

本节给出未来十年的方向、冻结节奏和激活门槛；它**不是第二份 TODO 或状态台账**。第 12 节仍是唯一可变进度账本，Codex 只能执行其中状态为 READY 的原子工作项。十年规划只解释哪些能力值得投资、何时可以拆成工作项、何时必须停止。

### 17.1 永久产品边界

KSpaceJet 是 ISMRMRD 重建框架，包含一个受严格限制的外部集成网关；它不是扫描仪或厂商采集系统。

- 输入语义仅是标准 ISMRMRD：当前为 HDF5；reader/recon 必须发现并选择语义有效的标准 container，不能把 `/dataset` 或任何 `ksj_*` group 当作必需路径。未来外部增量输入只能经已选公开 Gateway Profile 规范化为相同语义，或由同进程宿主提交。
- 它不拥有扫描仪、采集卡、FPGA、DMA、PCIe/QDMA、内核驱动、设备 ring、厂商网络协议、设备 MRD session、Connector vendor SDK、PACS/DICOM 路由或采集端流控。ksj-gateway 只处理 P5-009 冻结的公开 profile。
- 它不设框架级通道数上限。`channel_count` 是 ISMRMRD 数据形状，不是准入阈值；256、512 或更大的 case 都必须走同一 generic path。真实字节、设备内存或计算预算不足时，只能返回可审计的本地资源失败。
- Provider 负责算法，framework 负责 ISMRMRD 校验、数据所有权、frame/completion、plan、资源账本、执行、结果 artifact 与可追溯性。Provider 的算法限制必须写入自己的 contract，不能变成 framework 输入限制。
- 输出可以是标准 image artifact、本地 writer、同进程 callback，或已选 Gateway Profile 的受限外部交付；正式磁盘结果只使用标准 ISMRMRD `image_x` series，不定义私有 reconstruction/debug/meta group。作者化 PipelineDefinition 保持为独立 JSON；未来若确需文件内 pipeline material，只能另行定义可选 `/ksj_pipeline`，且不能破坏/替换标准 image artifact；不负责外部影像系统或站点工作流。

### 17.2 十年内稳定的语义与可替换的实现

| 层 | 需要长期稳定的语义 | 必须保持可替换的实现 |
| --- | --- | --- |
| ISMRMRD 边界 | ISMRMRD 字段解释、结构/语义校验、ownership、completion、terminal mapping | HDF5 reader、未来 in-process feed/Gateway Profile normalizer、缓存布局 |
| Gateway 外部边界 | 单一公开 profile、身份/route、连接/scan/run 分层、header-first admission、资源与终态映射 | TLS/network stack、profile codec、Connector 实现、部署拓扑、egress adapter |
| MRI semantic core | ScanDescriptor、classification、key、calibration、duplicate/missing/incomplete 和 image order 规则 | FrameSlot 内部字段、bitmap、index 和缓存算法 |
| Pipeline 与类型 | PipelineDefinition 的逻辑语义、TypeRef identity、Provider contract、terminal/partial 明示规则 | compiler pass、fusion、batch、queue、scheduler 与 C++ 类布局 |
| Runtime 安全 | host resource ledger、FiringLease、OutputGrant、取消、终态、原子提交 | allocator、thread pool、NUMA 策略、GPU stream/event 实现 |
| 证据链 | canonical identity、RunRecord、input/plan/Provider/config provenance、reference oracle | artifact store、日志/trace exporter、报告与可视化工具 |
| 加速 | CPU oracle 与结果一致性要求、DevicePlan 的资源声明原则 | CUDA/HIP/SYCL、FFT/BLAS backend、GPU 厂商和 AI runtime |

`ExecutionPlan`、`DevicePlan`、allocator、scheduler、GPU backend、内部 C++ API 和 CMake target 都是随机器与实现演进的可重建物，绝不是十年稳定 ABI。历史 RunRecord 应保存其 identity 与环境；新 runtime 应从长期语义 artifact 重新编译 plan，而不是永久执行旧物理计划。

### 17.3 证据驱动的十年路线

| Horizon | 时间 | 目标与产物 | 进入/退出门禁 | 明确不承诺 |
| --- | --- | --- | --- | --- |
| H0 Foundation | 0–18 个月 | P0–P4：可复现 HDF5 reference、generic bounded CPU runtime、artifact/verifier、Provider SDK、质量与供应链基线。 | Linux/Windows 可构建；Cartesian/non-Cartesian reference 的 golden、异常、取消、RunRecord 和资源证据；独立 Provider conformance。 | 采集集成、网络服务、固定通道上限、GPU 默认路径、临床宣称。 |
| H1 Gateway and embedded runtime | 18–36 个月 | P5：一个公开 Gateway Profile 的安全有界集成，以及可选的同进程 ISMRMRD feed/local run/HDF5 equivalence。 | H0 离线语义已 ACCEPTED；Gateway Profile、security/deployment policy、ownership/terminal/resource contract 和 conformance 已冻结。 | 厂商采集控制、私有协议、未批准的 upstream pause/credit/retry、临床宣称。 |
| H2 Platform candidate | 3–5 年 | 经过外部使用验证的 Provider SDK、worker isolation、DevicePlan/GPU experiment、任意 channel-count case 的质量与性能证据。 | 两个独立 Provider 或嵌入宿主；clean-machine 支持矩阵；CPU oracle、fault/soak、SBOM 与 ABI/conformance gate。 | 把任意测试 case 宣称为采集能力或医疗产品。 |
| H3 LTS and ecosystem | 5–7 年 | LTS 分支、Provider capability/conformance registry、签名与供应链、后端可移植性、可复现 benchmark。 | 多个独立使用者与维护者；安全响应、回归、升级与外部复现实验持续通过。 | 插件市场、云控制面、分布式采集系统。 |
| H4 Sustainable infrastructure | 7–10 年 | 可持续开源核心、长期 evidence archive、硬件换代路径、独立治理与受监管产品的明确分界。 | 非单一维护者可发布、验证、响应漏洞和维护 LTS。 | 框架自动成为扫描仪、采集系统或受监管诊断产品。 |

阶段只由证据推进，不因日历到达自动推进。H1–H4 不得直接变成 READY；每个半年审查后，只有满足 activation gate 的能力才拆为新的原子工作项。

### 17.4 能力包模型与半年审查

未来能力使用 `CapabilityWorkPackage` 描述，但不创建第二状态文件。每个能力包在本文件第 13 节追加 ADR/审查记录；只有拆出的 WorkItem 才进入第 12 节。

| 类型 | 可含内容 | Codex 是否可直接执行 |
| --- | --- | --- |
| FOUNDATION | ISMRMRD semantics、fixture、reference path、artifact identity | 仅其中已拆分并 READY 的 WorkItem |
| RUNTIME | bounded execution、completion、cancel、resource evidence | 同上 |
| EXPERIMENT | GPU、worker isolation、AI backend | 仅 time-boxed ADR/实验工作项；成功不等于产品功能 |
| PROVIDER | SDK、conformance、bundle、生态 | 同上 |
| QUALIFICATION | golden、benchmark、soak、supply-chain | 同上 |
| LTS/ECOSYSTEM | 兼容、维护、安全、贡献治理 | 同上 |

每半年必须产生一条审查记录，至少回答：

1. 该能力的用户价值、证据和剩余风险是什么？
2. 哪些语义将变为 `candidate-stable`，哪些仍是 internal？
3. 若证据不足，是 `defer`、`reject` 还是拆分更小实验？
4. 是否有新的外部数据、宿主、Provider 或硬件授权改变范围？

### 17.5 ContractClass 与 Codex 长期工作规则

每个非平凡工作项在开始时必须标注一个 `ContractClass`：

| Class | 含义 | 改动要求 |
| --- | --- | --- |
| `internal` | 仅内部实现，可以直接替换 | 正确性、资源和回归测试。 |
| `candidate-stable` | 预期将成为长期语义边界 | ADR、正/负 contract tests、Provider/schema/RunRecord 影响清单。 |
| `stable-vN` | 已对外冻结的 ABI/schema/API | 兼容性策略、迁移或明确的 major-version 决策、跨平台 evidence。 |

在 v1 冻结前默认采用 pre-release direct replacement：不加入 alias、双 parser、双 schema、版本协商或兼容 shim。只有两个独立外部使用上下文验证了 surface，且有维护预算、conformance、发布与漏洞响应机制，才可将其从 `candidate-stable` 提升为 `stable-vN`。

### 17.6 无通道上限的技术验收规则

这条规则覆盖所有未来 horizon：

1. ISMRMRD schema、reader、CLI、ScanDescriptor、planner、runtime 和 generic buffer accounting 不得有 `max_channels`、`1..64` 或等价硬编码。
2. 通道数必须使用不截断的通用维度/计数表示；所有乘法、stride 和 allocation 先做 overflow-safe byte/work 计算。
3. fixture/generator 必须允许任意正整数 channel count，并至少持续覆盖 1、64、256 及大于 256 的非特殊 case；测试不得把 256 当作最大值。
4. Provider 的算法或布局不能支持某一 case 时，resolver/contract 必须给出该 Provider 的可解释诊断；framework 仍接受该 ISMRMRD 数据并允许选择其他适用 Provider。
5. 当本地内存、device、线程或 deadline policy 无法满足时，错误必须指向具体 resource/work 预算，不能伪装为“通道数超限”。
6. 关于某个 channel-count case 的质量、内存、吞吐或 latency 结论必须同时附上数据 identity、算法配置、MachinePolicy、high-water、CPU oracle/quality准则和原始 benchmark 证据；它绝不代表采集端能力。

### 17.7 十年风险与止损

| 风险 | 早期信号 | 强制止损 |
| --- | --- | --- |
| 范围膨胀为采集/不可审计服务 | gateway 之外出现厂商协议、device driver、私有 ACK/credit、未选 profile、raw spool 或 scanner task | 停止对应工作项；保留 P5 网关边界，要求新的 owner/policy/acceptance 后再继续。 |
| 偷偷加入通道上限 | parser/CLI/plan 中出现 count threshold | 阻止合并；恢复 generic dimension 与 bytes/work resource accounting。 |
| 过早冻结 ABI | 只有仓内 Provider 就要求长期兼容 | 保持 pre-release direct replacement，先完成独立使用 conformance。 |
| GPU/AI 绑死核心 | vendor 类型进入公共语义或无 CPU oracle | 降级为 experiment；不通过证据不进入 default path。 |
| 物理计划变成公共合同 | 用户开始依赖 queue/thread/stream 字段 | 只发布语义 artifact；ExecutionPlan 继续可重编译。 |
| 证据腐烂 | 缺数据 hash、转换脚本、机器信息或 raw samples | 拒绝性能/质量 claim，补齐 manifest 与 RunRecord。 |

本节的唯一目标是让 KSpaceJet 在十年内持续替换硬件、调度器、Provider 和算法，同时不牺牲 ISMRMRD 语义、无通道上限、资源正确性、可复现性与产品边界。

---

## 18. 权威的并行、任务调度与资源管理架构

本节是 KSpaceJet 对并行、任务调度和资源管理的**唯一实现权威**。凡是修改 `recon-model`、`recon-graph`、`recon-runtime`、Provider contract、CPU/GPU executor、FrameSlot、KeyShard、buffer pool 或 resource ledger 的工作，必须先阅读本节，并在第 12 节对应工作项中引用它。若旧文档、临时代码或 benchmark 与本节冲突，以本节为准。

### 18.1 决策权边界

| 组件 | 唯一负责的决策 | 明确不得决定 |
| --- | --- | --- |
| Provider contract | 算法语义：哪些维度 independent/grouped/ordered/window/collective，哪些 calibration/state 依赖存在，是否可 CPU/GPU、是否可 partial。 | 线程数、队列实现、allocator、GPU stream 数、全局公平策略、未计费后台任务。 |
| `ExecutionPlanCompiler` | 将实际 ScanDescriptor、ResolvedPipeline、Provider capability 和 MachinePolicy 编译为 WorkKey、依赖、placement、资源 reservation 与 output order。 | 推测未声明的 MRI 独立性，或修改算法的 grouped/ordered 语义。 |
| Independent verifier | 拒绝不合法 WorkKey、非法笛卡尔积、未闭合 join/terminal、资源超限、ownership 或 capability 不匹配的 plan。 | 选择更快但未经声明的调度策略。 |
| Runtime scheduler | 仅在**已验证且 ready**的 task 中选择当前运行次序，落实 queue、CPU/GPU executor、取消和资源释放。 | 扩大 partition、打破 KeyShard、绕过 reservation、改变 output order 或让 Provider 自治调度。 |
| FrameSlot / KeyShard | FrameSlot 决定某个逻辑单元是否 complete/sealed；KeyShard 串行化同一可变状态的 writer。 | 运行重计算、猜测缺失 input 或用资源上限宣布 frame 完成。 |
| ResourceLedger | 原子预留、提交、实际计费、high-water、释放和 leak 检测。 | 通过 channel count、采集速率或外部硬件协议拒绝输入。 |
| MachinePolicy | 指定本机资源预算和偏好：CPU、RAM、NUMA、GPU、VRAM、并发 scan、性能 profile。 | 改变 Provider 的 MRI 依赖语义。 |

因此固定执行链为：

```text
Provider PartitionCapability + ScanDescriptor + MachinePolicy
    -> ExecutionPlanCompiler
    -> verified ExecutionPlan
    -> FrameSlot/KeyShard readiness
    -> ResourceLedger reservation
    -> Runtime scheduler / CPU-GPU executor
    -> ordered result + RunRecord
```

### 18.2 WorkKey、FrameSlot 和不可变数据边界

每一个可执行重建单元必须有一个可序列化、可比较且可写入 RunRecord 的 `WorkKey`：

```text
WorkKey = {
  scan_id,
  encoding_id,
  spatial_group_id,      // slice、volume 或 sms-group，三者只能择一
  contrast_group_id,     // 单 contrast 或联合 contrast group
  temporal_group_id,     // frame、ordered frame 或 temporal window
  calibration_epoch,
  pipeline_digest,
  provider_identity
}
```

- `WorkKey` 不是 ISMRMRD header 字段的机械拼接。它由实际存在的数据组和 Provider contract 导出，未出现的轴使用明确的 singleton/group identity。
- `channel_count` 不属于 WorkKey 的分区轴，也不应成为 framework 任务数或准入条件。channel/sample/tile block 只是 Provider 在一个合法 WorkKey 内的计算分块。
- FrameSlot 只在语义 completion 达成后从 `Collecting` 变为 `Sealed`；默认不允许未封口数据进入完整重建。支持 progressive/partial 的 Provider 必须明确声明 checkpoint、增量 identity、最终 seal 和取消规则。
- 所有异步 task 只接收 host-owned immutable BufferHandle；借用的 ISMRMRD view 绝不能跨 callback 或异步边界。

### 18.3 Provider 必须声明的 PartitionCapability

每个可执行 Provider Operator contract 必须拥有一个 `partition_capability`，其语义属于 Provider contract/schema，而不是 C++ 注释或 CLI 参数。最小形状如下：

```yaml
partition_capability:
  encoding: independent | grouped | ordered | collective
  spatial: slice_independent | volume_collective | sms_grouped
  contrast: independent | grouped | collective
  temporal: independent | causal_ordered | windowed | collective
  calibration: none | shared_readonly | ordered_update
  channel: reducible | collective
  partial_execution: unsupported | checkpointed | append_only
  output_order: work_key | provider_declared_stable
  backends: [cpu]                 # gpu 仅在 capability/DevicePlan 已启用时出现
  allowed_partitions: []          # 仅列出实际允许的组合；空即 serial/grouped default
```

规则：

1. 未声明某轴时，该轴默认 `collective`，不得并行拆分。
2. `slice_independent` 只适用于独立 2-D slice；3-D volume、through-plane regularization 必须是 `volume_collective`；SMS 必须是 `sms_grouped`。
3. 多回波、多 TI、多 flip、参数 mapping 或联合正则化的 contrast 必须 `grouped/collective`，不能因为 ISMRMRD 存在 `contrast` 索引就拆开。
4. temporal regularization、motion estimation、Kalman/causal state 分别使用 `windowed`、`collective`、`causal_ordered`；只有无时间依赖的 frame 才能 `independent`。
5. channel `reducible` 允许在一个 WorkKey 内按真实 bytes/work 分块并固定归约；它不允许 framework 因通道数拒绝该 ISMRMRD 数据。
6. Provider 需要 shared calibration 时，只能读取一个 immutable `calibration_epoch`；任何更新必须经过对应 KeyShard。

### 18.4 ExecutionPlanCompiler 的确定性算法

Compiler 必须按以下顺序工作；不得先建线程或队列再反推语义：

1. 从 ISMRMRD header/metadata 形成 `ScanDescriptor`，列出实际 encoding、spatial、contrast、temporal、calibration group 及 completion 规则。
2. Resolver 固定 Provider identity、Operator contract、config 和 TypeRef，产生不可变 ResolvedPipeline。
3. 读取每个 Operator 的 `PartitionCapability`；若 capability 缺失、相互冲突或不支持该 ScanDescriptor，拒绝 resolve/compile，不产生半有效 plan。
4. 从实际存在的 logical group 构造候选 WorkKey，绝不将所有维度做笛卡尔积。
5. 对 `grouped`、`windowed`、`ordered`、`collective` 轴合并 WorkKey，生成显式 dependency、join 和 terminal node；对 `independent` 轴生成彼此独立但有确定 output order 的 task。
6. 以实际元素数、bytes/work、Provider cost model 与 MachinePolicy 选择 channel/sample/spatial tile 大小。禁止任何 `max_channels`、`1..64`、`256` 等固定框架阈值。
7. 为每个 task 生成 placement（CPU/NUMA/GPU）、FiringLease、输入/输出 ownership、资源 reservation、queue 上限、join 和 cancel/failure cleanup。
8. 对相同输入必须生成相同 ExecutionPlan digest；若策略允许非确定性选择，选择依据、seed 和结果必须进入 plan/RunRecord。
9. 将 plan 交给 independent verifier；验证通过之前不允许 admission 或 task creation。

### 18.5 Runtime 状态机与调度规则

每个 WorkKey 只能经过下列状态；禁止跳过 reservation 或绕过 terminal：

```text
Collecting
  -> Sealed
  -> WaitingDependencies
  -> Admitted
  -> Ready
  -> Running
  -> Joining
  -> Completed | Cancelled | Failed | Rejected
```

- `Collecting -> Sealed`：仅由 FrameSlot 的真实 completion 触发。
- `Sealed -> WaitingDependencies`：等待 calibration epoch、ordered predecessor、window 邻居或 collective group。
- `WaitingDependencies -> Admitted`：ResourceLedger 为 plan 中全部必须资源原子预留成功。
- `Admitted -> Ready`：输入 BufferHandle/OutputGrant/FiringLease 均已准备；ReadyQueue 中不能出现“等待资源”的 task。
- `Ready -> Running`：scheduler 从 verified plan 的合法 ready task 中选择；P3 使用确定性的 WorkKey 顺序，P6 才可在不改变语义的前提下启用 DRR/priority/work-stealing。
- `Running -> Joining`：只接收对应 lease/identity 的完成事件；异步 GPU/worker 必须先完成 fence/token 归属确认。
- `Joining -> terminal`：显式检查所有 required input、join、flush、output order 和 cleanup；取消/失败不得产生伪 Completed。

调度分工固定为：

| 调度对象 | 正确策略 |
| --- | --- |
| 同一 KeyShard 的状态更新 | 单 writer；queued + running 不超过一。 |
| 无状态 CPU ComputeTask | 固定 worker queue 或 work-stealing；不得共享可变 MRI 状态。 |
| GPU ComputeTask | 经 DevicePlan 分配 stream/event；CPU worker 不阻塞等待 GPU。 |
| 多 scan | P6 前只允许确定性单 scan/有限场景；P6 后可用 quota/DRR，但只能重排已 ready 的合法 task。 |
| 输出 | 可以乱序计算，必须按 `WorkKey` 或 Provider 声明的 stable order 交付。 |

### 18.6 ResourceLedger：资源域、预留与释放

ResourceLedger 至少具备以下 resource domain；所有额度同时具备 plan-time reservation、runtime actual charge 和 high-water：

| 域 | 计费对象 |
| --- | --- |
| HostPageableBytes | materialized input、FrameSlot、CPU scratch、image/result buffer |
| HostPinnedBytes | GPU transfer staging；未启用 GPU 时为零 |
| DeviceBytes(device_id) | GPU input、scratch、output、quarantine buffer |
| QueueItems / QueueBytes | 每条 edge、ReadyQueue、reorder/join buffer |
| CpuConcurrency | host executor 的并发 token，不等同于 OS thread 的永久占用 |
| DeviceStreams(device_id) | 已分配的 GPU stream / in-flight work token |
| ArtifactStagingBytes | 尚未原子提交的本地输出 artifact |
| AsyncTokens | Provider callback、fence、worker request 等未完成异步所有权 |

资源生命周期固定为：

```text
estimate -> reserve atomically -> materialize/charge -> execute
         -> seal/commit output -> release exactly once -> recycle
```

- 任一 reservation 失败时，不可留下部分 task、partial output 或不可回收 BufferHandle。
- Provider 不得私建未计费 pool、线程池、GPU stream、device buffer 或后台 future。
- ResourceLedger 只保护 KSpaceJet 已接受后的内部资源；当资源不足时只能返回 local reject/terminal，不定义上游暂停、DMA credit、网络 ACK 或采集流控。
- 无通道上限不等于无限内存：channel 维度参与 overflow-safe byte/work 计算，但永不参与固定最大值比较。
- GPU cancel 后的 device buffer 必须进入 quarantine，直到 event/fence 或 device recovery 明确证明安全，才能释放 reservation。

### 18.7 四类 MRI 并行轴的选择规则

| 轴 | 何时可并行 | 何时必须合并/有序 | 推荐 WorkKey 形状 |
| --- | --- | --- | --- |
| encoding | 不同 encoding 无联合 reconstruction、phase/velocity 依赖或 shared mutable state。 | multi-encoding joint reconstruction、phase/velocity combination。 | `encoding_id` 或 `encoding_group_id` |
| spatial | 常规独立 2-D slice。 | 3-D volume、SMS/multiband、cross-slice regularization。 | `slice_id`、`volume_id` 或 `sms_group_id` |
| contrast | 每个 contrast 独立 IFFT/RSS/Provider path。 | multi-echo fitting、T1/T2 mapping、水脂分离、joint prior。 | `contrast_id` 或 `contrast_group_id` |
| temporal/frame | 各 frame 无状态、无 temporal prior。 | causal filter、temporal window、motion、dynamic CS。 | `frame_id`、`ordered_frame_id` 或 `temporal_window_id` |

如果一个 unit 太小，Compiler 可以在**已经声明独立的轴**上合并多个 WorkKey 形成 batch；不得为了吞吐把 grouped/ordered 数据拆开。相反，3-D、SMS 或 multi-contrast collective task 内部可按空间 tile、sample block、channel block 并行，但其最终 join 仍属于原 WorkKey。

### 18.8 AI 实现顺序与不可违反检查

AI 必须按以下顺序实现，不得先写 P6 的线程/GPU 优化：

1. **P2-003**：先扩展 Provider contract/model/schema，加入 PartitionCapability、WorkKey、plan fields 和 valid/invalid fixtures。
2. **P2-004**：让 independent verifier 拒绝未声明并行、虚假笛卡尔积、违反 grouped/ordered、无 join、无 reservation 的 plan。
3. **P3-001**：实现 ledger 的 reservation/charge/release 原子性与 fault tests。
4. **P3-003**：实现 FrameSlot seal、KeyShard、calibration epoch、ordered/window dependency 和 terminal state machine。
5. **P3-002/P3-004**：只执行 verified ready task，FiringLease/OutputGrant 由 host 强制；先保持 deterministic CPU executor。
6. **P3-005/P3-006**：以 serial oracle、RunRecord、actual high-water、cancel/failure corpus 验收。
7. **P4-004**：让每个 reference Provider 声明自己的 PartitionCapability，禁止框架硬编码算法并行性。
8. **P6**：仅在上述证据 ACCEPTED 后，分别启用并行、multi-scan fairness、NUMA、GPU；每个 feature flag 独立验收并可关闭。

实现前的最小问题清单：

- 这个 Operator 的各轴依赖是否已写进 contract？未写则默认 serial/grouped。
- 当前 WorkKey 是实际存在的数据组吗？是否错误生成了不存在的组合？
- FrameSlot 是否真的 sealed？calibration epoch / ordered predecessor 是否 ready？
- 所有 BufferHandle、lease、output、fence 是否已经计费并有唯一 owner？
- 是否通过 WorkKey/plan 而非硬编码 channel count 决定分块？
- 取消、Provider failure、device failure 后，ledger 与 terminal 是否可证明闭合？

### 18.9 必须长期保留的测试 corpus

至少维护以下 fixture/property/system tests；新增 scheduler 优化不得删弱任何一项：

| 用例 | 必须证明 |
| --- | --- |
| 独立 2-D multi-slice / multi-frame | 实际 slice/frame WorkKey 可并行，输出按确定 order，结果等于 serial oracle。 |
| 3-D volume | compiler 不得按 slice 拆分；必须在 volume work 内正确 join。 |
| SMS/multiband | compiler 必须用 sms group，不得在 unalias 前把 slice 当独立 task。 |
| multi-contrast joint mapping | contrast group 完整后才运行；不得提前生成单 contrast result。 |
| causal/windowed temporal reconstruction | predecessor/window 未 ready 时 task 不进入 ReadyQueue。 |
| shared calibration update | 同一 calibration KeyShard 无并发 writer；新的 epoch 不污染活动 task。 |
| arbitrary channel count | 至少 1、64、256、>256，走同一 generic path；无 channel-cap error。 |
| reservation failure | 无 task/输出/ledger 泄漏，返回明确 local reject。 |
| cancel/failure/device fence | 不产生 Completed，所有 host/device/async resource 正确释放或 quarantine。 |
| parallel versus serial | 每个启用并行模式与 serial oracle 在声明误差规则内等价。 |
