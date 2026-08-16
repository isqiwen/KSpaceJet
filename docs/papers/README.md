# KSpaceJet papers

本目录存放由 KSpaceJet 架构、实现和公开实验共同支撑的论文材料。论文文档不是产品规范的替代品：稳定接口和实现约束仍以 `docs/architecture/`、ADR、schema 和测试为准；论文负责提出可证伪的研究问题，并把实现证据组织成可复现的论证。

## 架构与理论基线

- [PipelineDefinition 与重建流水线设计](../architecture/pipeline_definition.md)：定义声明图、OperatorContract、ISMRMRD scan 编译、CalibrationReady、join/reorder、EndOfInput 与终态语义。
- [MRI 流水线、并行模型与可证明执行理论](../architecture/streaming_pipeline_parallelism_theory.md)：定义 scan-specific `ExecutionPlan`、OperatorInstance 内部 `KeyShard`、校准进展条件、资源/并行模型、条件性定理、机器可验证证书和实现轨迹精化边界。

## 当前稿件

- [KSpaceJet 资源合约流式重建论文初稿](kspacejet_resource_contract_streaming_paper_draft.md)：面向 Magnetic Resonance in Medicine 风格 full paper 的预结果初稿。
- [KSpaceJet 多基线公平对照与复现实验协议](kspacejet_gadgetron_comparison_protocol.md)：覆盖 Gadgetron 主基线、BART Streams 次级矩阵和 MRIReco.jl claim gate 的数据冻结、匹配算法、压力实验、消融、统计与公开制品协议。

## 对照证据层级

论文框架固定为“Gadgetron 完整主对照 + BART Streams 针对性次级实测 + MRIReco.jl 相关工作定位”。层级在实验前冻结，不能根据结果好坏互换：

| 系统 | 论文角色 | 实测要求 |
| --- | --- | --- |
| Gadgetron | 同类型在线重建框架，用于验证 KSpaceJet runtime 主张 | 必须完成完整 correctness、matched runtime、product pipeline、资源、过载和统计对照 |
| BART Streams | 与实时模块化 streaming、网络传输和端到端 latency 直接重叠的工作 | 强烈建议完成 passthrough、一个公开 radial workload、slow sink/burst 三类次级实测 |
| MRIReco.jl | 高性能、易扩展的离线算法开发框架 | 默认只作相关工作；仅由算法速度、工具箱覆盖或开发便利性主张触发独立实验 |

BART Streams 复现实验以 code tag `v0.1` 和数据 DOI [`10.5281/zenodo.17671124`](https://doi.org/10.5281/zenodo.17671124) 为冻结入口；该数据记录当前声明 CC BY 4.0，但 license 证据、派生物/再分发和人体数据隐私仍作为独立冻结门禁。公开输入只执行一次确定性转换，并冻结源 hash、转换工具/命令、输出 hash 和 metadata 映射。BS-00、BS-01、BS-02 默认均为 `comparison_class=product-level`、`evidence_role=secondary-contextual`；只有 BS-00 逐项通过完整 logical-event、wire/path、serialization、adapter-copy、kernel/backend/thread、output 和 timed-boundary 门禁后才可把 class 升为 `framework-isolation`，其 evidence role 不变；BS-01/BS-02 固定不升级。KSpaceJet 的生产在线 wire contract 始终是公开 MRD/ISMRMRD session，内部 resource ledger 不编码为私有 flow-control extension。MRIReco.jl 不进入默认在线排名，也不与 Gadgetron/BART Streams 组成四框架性能竞赛。

## 产品部署与论文计时边界

KSpaceJet 的产品部署分为 `ksj-gateway`（站点/外部系统集成）和
`ksj-recon`（重建 runtime）。独立部署的站点 Connector 先将专有系统适配为
公开 session；Connector、gateway 和 reconstruction service 之间只使用冻结的公开
MRD/ISMRMRD streaming session。Gadgetron 的 framework-isolation 与 matched
reconstruction 主比较默认由合规 client 直连 `ksj-recon`，避免把 Connector、
站点路由或额外 relay copy 误归因于 runtime。真实 scanner 的
`scanner → Connector → ksj-gateway → ksj-recon → ksj-gateway → destination` 验证仍是
重要的 product-integration 证据，但必须单列 Connector/gateway hop、staging、
serialization 和 copy。

论文 runner `ksj-research` 与其余三个应用一样默认构建并安装；它只承担外层实验编排，
绝不构成任何被测 runtime/data-plane 路径、Provider ABI 或私有 wire shortcut。VS Code 的
`KSJ: build Linux Debug applications`、`KSJ: build Linux Release applications`、
`KSJ: build Windows Debug applications` 和 `KSJ: build Windows Release applications`
均构建四个可执行程序。它与 `KSJ_BUILD_RESEARCH` 的测试/实验开关独立。

## 稿件状态约定

- `已实现`：代码和自动化验证已经存在，并可指向具体 commit/test/artifact。
- `已设计`：架构契约已经落盘，但实现或验证尚未完成。
- `待实现`：论文方法需要但仓库尚不具备的能力。
- `待实验`：必须由正式实验填充的数据、图、表或统计结论。
- `待外部验证`：需要真实扫描仪、独立机器、外部 provider 作者或第三方复现。

任何 `[待实验]` 占位都不得在没有原始机器可读结果的情况下改写成性能结论。论文不得把开发目标、单次运行、最佳值或设计推断写成已观察结果。

## 绘图约定

Markdown 中的架构、流程、时序、状态和依赖关系图统一使用 Mermaid，并使用稳定的 camelCase node id 和简短标签。算法伪代码、JSON/schema、目录结构、命令以及字段或指标清单仍使用与内容匹配的代码块或列表；不要为了形式统一把非图内容改成 Mermaid。

## 完成定义

稿件进入投稿候选状态前必须同时满足：

1. 所有核心方法都有实现、测试、canonical 配置和公开 commit。
2. Gadgetron 对照遵守冻结的公平比较协议，且没有仅对 KSpaceJet 有利的后端、线程或输入差异。
3. BART Streams 次级矩阵已完成，或以可审计的正式 waiver 关闭；完成时每个 case 都有独立 `comparison_class` 与 `evidence_role`，waiver 时包含阻断证据、已完成步骤和删除/收缩的 claim。
4. MRIReco.jl claim gate 已执行：没有对应证据的算法速度、工具箱完整度或开发便利性主张已删除。
5. 正确性先于性能通过；所有性能结论包含独立进程重复和置信区间。
6. 资源上界只覆盖明确记账或强制隔离的资源；限制条件在摘要、方法和讨论中一致。
7. 原始 JSON/CSV、数据 hash、pipeline、环境、Conan lock、脚本和生成图表的代码可复现。
8. 至少完成公开数据 replay；面向 MRM 投稿时，优先补充真实扫描仪在线验证。
9. 作者、机构、伦理、数据许可、软件许可和利益冲突信息完成核对。
