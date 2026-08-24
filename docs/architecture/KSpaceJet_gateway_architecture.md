# KSpaceJet 外部集成网关架构

> **状态：候选稳定设计；P5-008 ACCEPTED。** 本文定义目标架构和后续验收边界，
> 不是已实现或已资格化的外部服务说明。当前 ksj-gateway 仍是只提供
> help/version/scaffold JSON 的应用，不能监听连接、认证对端、处理网络数据或交付结果。
>
> **权威关系：** 本文受 [主实施计划](KSpaceJet_project_plan_and_acceptance.md) 的
> P5-008 状态约束；P5-009 至 P5-013 才是实现和资格化工作项。历史 streaming 文档
> 只作背景，不能作为本设计的实现依据。

## 1. 设计决策

用户已明确要求 ksj-gateway 成为真实的外部集成网关。因此 KSpaceJet 的产品边界变为：

- ksj-gateway 是唯一的 KSpaceJet 外部数据入口和结果出口；它是网络、身份、会话、路由、
  资源准入和外部交付的边界。
- KSpaceJet 内部唯一的原始 MRI 数据语义仍是 ISMRMRD。网关必须把已选公开 profile 的输入
  确定性地规范化为该语义后，才可交给 reconstruction runtime。
- 厂商扫描仪 SDK、设备驱动、DMA/FPGA、专有 acquisition 协议和站点凭据留在独立的
  Connector 制品中。Connector 可以是外部客户程序，但不进入 Provider、runtime 或本仓库
  的默认产品 surface。
- 不定义 KSpaceJet 私有 wire protocol。任何可部署 listener 必须使用 P5-009 冻结、有公开
  规范和互操作证据的 Gateway Profile；内部 C++ 调用不是 wire protocol。

这里的 ContractClass 是 **candidate-stable**：命名、边界、状态和不变量将有契约测试，但在
pre-release 阶段仍可由一个原子变更直接替换，不能并行维护多个 profile 或兼容层。

## 2. 目标与非目标

### 2.1 必须提供的外部集成能力

目标实现完成后，网关必须能在受控部署中完成以下闭环：

1. 监听一个配置确定的公开 Gateway Profile，完成 TLS、身份认证、授权和精确 profile 匹配；
2. 接收 header-first 的标准化 MRI 流，做协议级大小、顺序和完整性检查；
3. 将每个异步输入 materialize 为 host-owned ISMRMRD 对象，并在 runtime 验证、plan admission
   和资源预留成功后处理 acquisition；
4. 维持独立的连接、gateway scan 和 reconstruction run 生命周期，并把每个终态记录为可审计、
   可对外映射的结果；
5. 以同一公开 profile 交付有界的结果和终态，不因慢 peer、断线、Provider failure 或输出失败
   造成无界堆积或静默丢失。

### 2.2 明确非目标

- 不实现扫描仪、采集卡、FPGA、DMA、PCIe/QDMA、内核驱动或设备 ring；
- 不解析、存储或转发厂商私有 raw protocol；这属于独立 Connector；
- 不引入 private KSpaceJet framing、私有 ACK/credit、私有 retry/resume 或第二套
  gateway-to-recon 网络数据面；
- 不把 Connector 伪装为 Provider，也不把算法放入 gateway；
- 初始版本不承诺 HA、跨连接恢复、durable raw spool、exactly-once、PACS/DICOM、
  临床/诊断用途、固定吞吐/延迟或 GPU 调度；
- 不把原始 .mrd、.h5、.hdf5 或 .ismrmrd payload 写入本仓库。开发 fixture 只能是小型
  synthetic vector；真实数据继续只属于同级数据仓库。

## 3. 总体拓扑与信任边界

~~~
站点 Connector / 标准客户端                         KSpaceJet 受控部署
────────────────────────────                         ─────────────────
厂商 SDK、设备协议、站点凭据
             │
             │ 已选公开 Gateway Profile + TLS
             ▼
┌─────────────────────────────────────────────────────────────────────┐
│ ksj-gateway                                                         │
│                                                                     │
│ Transport Endpoint -> Profile Decoder -> Gateway Session            │
│ TLS / mTLS / auth       bounded decode      one scan actor          │
│        │                       │                   │                │
│        └──── connection ledger ┴──── scan ledger ──┤                │
│                                                     ▼                │
│                    GatewayRunHost (in-process library boundary)     │
└─────────────────────────────────────────────────────────────────────┘
                                                     │
                                                     ▼
 ScanDescriptor -> resolve/compile/verify -> runtime admission
      -> HostFrameAssembler -> bounded executor -> Provider
                                                     │
                                                     ▼
 Gateway Egress Adapter <- OutputGrant / RunRecord / result artifact
             │
             ▼
 已选公开 profile 的结果消费者
~~~

初始部署形态只允许 ksj-gateway 通过共享库调用 GatewayRunHost；它**不得**在内部再用 socket、
HTTP 或自定义消息连接 ksj-recon。ksj-recon 保持离线 reference CLI，两者复用同一
reconstruction library boundary，而不是维护两套 planner、runtime 或 Provider 生命周期。

| 边界 | 可信程度 | 责任 | 明确不负责 |
| --- | --- | --- | --- |
| 外部 Connector/客户端 | 不可信网络对端 | 选择公开 profile、提供对端身份、遵循 profile 的 admission/flow semantics | 访问 runtime、Provider 或内部资源账本 |
| Transport Endpoint | 边界进程组件 | TLS、完整读写、frame 上限、连接限额、认证前最小化资源 | MRI 语义、pipeline 或 Provider 调用 |
| Profile Decoder/Normalizer | 处理不可信输入 | 公开消息校验、顺序、header-first、owned materialization | 厂商协议、借用 socket buffer 的长期持有 |
| Gateway Session/Orchestrator | 受控 | route authorization、scan actor、双账本 admission、终态映射 | 算法、物理 runtime 调度决策 |
| GatewayRunHost/runtime | 受控内部边界 | ISMRMRD 语义、plan/verifier、FrameSlot、Provider、RunRecord | 网络身份、TLS、外部 framing |
| Result consumer | 独立信任域 | 接收已授权的输出和终态 | 改写 runtime 的资源保证或重放语义 |

## 4. 公开协议与数据语义

### 4.1 不把格式误当成 transport

MRD v2 的模型是 stream-oriented，包含 header 和 StreamItem 一类数据项；官方说明还给出
binary 与 NDJSON serialization。它可作为外部标准化流的候选输入，而不是现成的网络会话
binding。[MRD model](https://ismrmrd.github.io/mrd/reference/model.html) 和
[MRD streaming format](https://ismrmrd.github.io/mrd/reference/format.html) 都不能替代具体的
TLS transport、身份、错误关闭或 admission 语义设计。

因此，P5-009 必须为首个可部署接口冻结一个 **Gateway Profile**，且配置只能选择一个精确
profile ID，不能做运行时协商或降级。一个合格 Profile 至少要指定：

| 项目 | 必须冻结的内容 |
| --- | --- |
| 外部标准 | 规范名称、精确版本、许可证、互操作参考实现和允许的 serialization |
| transport binding | 明确的标准化 secure transport、TLS 行为、frame 边界、超时与关闭规则 |
| identity | mTLS/证书链、principal 提取、route authorization 和证书轮换责任 |
| 输入 | header 与 data 的合法顺序、每种标准消息到 ISMRMRD 的映射、最大长度与未知消息处理 |
| admission | header-first request/reply 或等价的标准机制；runtime admit 前不得发送 acquisition |
| 输出 | result metadata、image/result payload、partial/terminal 顺序和慢消费者语义 |
| errors | profile 所规定的错误/关闭映射及不可泄露内部细节的稳定分类 |
| test corpus | byte-level valid/invalid vectors、fragmentation、truncation、out-of-order 和 version mismatch |

在 P5-009 ACCEPTED 前，ksj-gateway 不得监听任何外部端口。不得因为某个库能发送 TCP
字节，或因为数据名含 MRD/ISMRMRD，就把它宣称为公开互操作协议。

### 4.2 标准化与所有权

GatewayProfileDecoder 的唯一产物是内部 OwnedIngressEvent。它包含已验证的 header/metadata
或 host-owned payload；任何 socket read buffer、parser scratch buffer 和临时 span 都必须在
越过该边界前释放或复制。

~~~
wire bytes -> bounded decoder -> protocol object -> semantic validation
           -> owned ISMRMRD event -> GatewayRunHost -> runtime
~~~

内部 type registry 仍是可执行 payload 类型的唯一来源。公开 profile 的字节布局不能复制或
依赖内部 opaque layout/digest；两者只能由受测 Normalizer 连接。

## 5. 组件和目标源码布局

以下是 P5-009 以后允许创建的目标布局，不代表目录或 CMake target 已存在：

| 目标模块 | 主要责任 | 依赖方向 | 禁止事项 |
| --- | --- | --- | --- |
| libs/gateway/kspacejet-gateway-contracts | 配置、route、身份、错误、状态、资源和 profile contract 的 value type/schema | 仅 core/model 基础类型 | socket、TLS 实现、Provider 算法 |
| libs/gateway/kspacejet-gateway-transport | listener、TLS、connection lifecycle、受限读写 | contracts、platform；选定的网络/TLS 依赖 | MRI 数据语义、runtime/Provider |
| libs/gateway/kspacejet-gateway-profile | 已选公开 profile 的 decode/encode、conformance vector | contracts、type-registry factory | 私有 framing、厂商协议 |
| libs/gateway/kspacejet-gateway-session | connection/scan actor、authorization、gateway ledger、terminal mapping | contracts、profile、host abstraction | unbounded queue、算法或 socket buffer retention |
| libs/gateway/kspacejet-gateway-host | GatewayRunHost 到 recon model/graph/runtime 的内部桥 | session contracts、recon libraries | 另建网络 RPC、复制 planner/runtime |
| libs/gateway/kspacejet-gateway-egress | output grant、外部结果编码和慢 peer policy | host result、profile、contracts | PACS/DICOM、持久 raw spool |
| apps/kspacejet-gateway | composition root、CLI11 config validation、process lifecycle | 所有 gateway library | 放入协议解析、算法或业务状态机 |

现有 libs/core/kspacejet-platform socket API 只有 blocking IPv4 原语。P5-010 必须选择一个
有维护、可测且支持 TLS、非阻塞/事件驱动 I/O、完整读写、IPv6/DNS 与超时的网络栈；不能以
现有 wrapper 为基础拼装生产 listener。现有无界 MessageLoop 或 BlockingQueue 同样不得用于
外部连接或 acquisition 流。

## 6. 三个独立生命周期

连接、gateway scan 和 reconstruction run 不得混为同一个状态机。gateway 只能报告自身状态；
ScanLifecycle/RunRecord 才是 reconstruction run 的权威。

### 6.1 连接状态

~~~
Accepted -> Securing -> Authenticated -> ProfileLocked -> Idle
                                                |
                                                v
                                             Serving -> Draining -> Closed
~~~

- Securing 只分配固定 handshake budget；TLS 或认证失败直接关闭，不能创建 scan。
- ProfileLocked 表示 endpoint 已匹配一个配置确定的 profile；未知或不匹配版本失败，没有 fallback。
- 初始 profile 每个 connection 最多承载一个 active scan；多 scan 需要 P6 fairness 和显式
  profile/准入证据，不能默认启用。

### 6.2 Gateway scan 状态

~~~
Created -> HeaderReceived -> Resolving -> AdmissionPending -> Admitted
                                      |                         |
                                      v                         v
                                  Rejected                 Receiving
                                                                  |
                                                      InputClosed -> Draining
                                                                  |
                                                  Completed / Cancelled / Failed
~~~

- HeaderReceived 后才允许选择经授权的 route、pipeline 和 output policy。
- AdmissionPending 必须先完成 profile、gateway 与 runtime 的所有可逆资源预留。
- Admitted 是 runtime 明确成功后的事实；gateway 不得自行伪造 admitted/completed。
- 未完成输入、非法顺序、断线和本地取消都必须转成明确 terminal reason，绝不靠填零或静默
  删除 frame 收尾。

### 6.3 关键事件映射

| 事件 | admission 前 | admission 后 | 对外行为 |
| --- | --- | --- | --- |
| 未认证/未授权 | 不创建 scan | 不适用 | profile 规定的拒绝/关闭；无内部细节 |
| 非法长度、编码或顺序 | reject | fail | profile error/close，审计分类为 protocol violation |
| runtime/gateway 资源不足 | reject | fail 或 cancel，取决于已接收的语义 | 明确 terminal；绝不静默丢弃 |
| 客户端主动取消 | reject/cancel | cancel | terminal 可观察且资源释放一次 |
| 对端断线 | reject 或 close | peer_disconnected terminal | 不承诺重连恢复或 exactly-once |
| Provider/runtime failure | reject 或 fail | fail | 不把内部 stack/Provider details 发送给对端 |
| 输出慢/断线 | 不适用 | delivery failure 或声明的 cancel | 不得无界暂存或暗中保留结果 |

## 7. Header-first admission 和资源闭环

外部输入的关键顺序是：

~~~
authenticate -> lock profile -> receive/validate header -> authorize route
-> compile + independently verify plan -> reserve gateway + runtime resources
-> publish admission -> receive owned acquisition events -> close/drain -> terminal
~~~

### 7.1 双账本原子准入

网关维护 GatewayResourceLedger，runtime 继续维护 ResourceVectorLedger。两者责任不同，但
admission 必须形成一个可回滚 transaction：

1. gateway 预留 connection/decoder/session/output 的 item、charged-byte、I/O slot 和
   write-in-flight budget；
2. GatewayRunHost 为已验证 ExecutionPlan 请求 runtime 的 host/device/queue/CPU/artifact 预算；
3. 两边都成功后才把 scan 显示为 Admitted；任一失败则按相反顺序释放所有预留；
4. 每个 message 先预留 wire/decode budget，再 materialize；提交后由 runtime 接管
   host-owned buffer 的计费，gateway 只在安全点释放自己的 lease；
5. normal、cancel、failure、disconnect 和 process shutdown 都必须走一次、且仅一次释放。

| 域 | 例子 |
| --- | --- |
| connection | handshake bytes、socket read/write in flight、认证尝试数、idle timer |
| decoder | wire frame、解压/解析 scratch、metadata UTF-8、已验证 message |
| scan | header、owned ingress event、FrameSlot handoff、scan actor、pending terminal |
| egress | OutputGrant、encoded result、write queue、delivery timeout |
| runtime | host/device bytes、queue item/bytes、CPU permit、Provider async token、artifact staging |

每个域必须同时有 item 和 charged-byte 上限。不得把未计费 vector、parser cache、spool、future、
promise 或 coroutine continuation 留在账本外。

### 7.2 背压不是采集保证

网关可以根据已选 public transport 的标准 flow-control 暂停读取，但这只限制 KSpaceJet
进程内增长，不保证上游设备不会丢数据。每个 Connector/Profile 必须声明它能否安全等待
header admission、如何处理 reject/terminal、是否允许取消；若不能满足，profile 不得作为
live ingress 进入 P5-010。

初始版本不允许 raw data disk spool。需要持久暂存时，必须在新的工作项中先冻结加密、容量、
保留、删除、审计、数据所有者和故障恢复 policy；它不能作为网络不确定性的隐式补丁。

## 8. 身份、安全和隐私

对非 loopback listener，初始安全基线是 TLS 加双向身份验证。P5-009/P0-006 必须冻结证书
发行者、信任锚、轮换、吊销、网络区、principal 到 route 的授权和审计保留责任。

| 控制点 | 规则 |
| --- | --- |
| 配置 | config 仅引用 secret/certificate 的外部 reference；私钥、token、PHI 和 raw payload 不得提交或写日志 |
| 认证 | 在 header 或任何 MRI payload 之前完成；失败使用固定预算和速率限制 |
| 授权 | principal、tenant/site、route、可用 pipeline、input/output scope 都必须显式匹配 |
| 输入防护 | frame/message/header/metadata/shape/整数乘法上限在 allocation 前验证 |
| TLS 失败 | 不能降级为 plaintext 或替代 profile；只记录脱敏审计分类和 correlation ID |
| 审计 | 记录身份的可审计引用、route、profile、资源结果、terminal 与 RunRecord link；不得记录 PHI 或 secret |
| 日志 | Core diagnostics 仍是 plain text stderr/file；机器可读 audit/metric/RunRecord 不得与日志混淆 |

P5-010 必须增加恶意长度、畸形编码、TLS/auth failure、证书无效、route 越权、slow peer、
连接 storm 和断线注入测试。安全评审失败不能由“只在内网使用”替代。

## 9. Connector 边界

Connector 是独立可部署的外部集成制品，不是 KSpaceJet Provider、动态插件或默认链接库。
它的职责是：

- 适配站点/厂商协议，持有站点专属 SDK 和凭据；
- 在自己的信任/进程边界完成厂商数据到已选公开 Gateway Profile 的转换；
- 按 profile 在 header admission 前等待，并正确处理 reject、cancel、close 和 terminal；
- 使用 mTLS 证明其身份，接受网关的 route authorization；
- 通过 P5-012 的 fake-peer/negative conformance harness；真实设备互操作只在 P5-013 得到
  明确授权和数据治理批准后进行。

网关不动态加载 Connector，不保存 Connector 凭据，不管理厂商 SDK ABI，也不把 Connector
注册/健康状态误当作 reconstruction run 状态。若未来需要 Connector catalog、签名、升级或站点
管理，那是一个单独的 control-plane 工作项和信任模型，不能隐含塞入数据面。

## 10. 输出与失败语义

网关的 egress 必须使用 OutputGrant，并让 result artifact/RunRecord 的所有权留在 shared runtime。
GatewayEgressAdapter 只做授权的公开编码和受限交付。

- 连接中断、进程故障或 delivery timeout 不承诺断点续传、跨连接去重或 exactly-once；
- 输入已关闭但对端在 output 前断开时，只有已接受的 durable artifact policy 才可允许
  reconstruction 继续；否则 terminal 是明确的 delivery failure/cancel；
- partial result、最终 result 和 terminal 的顺序由 P5-009 profile 冻结；
- 当前标准 image artifact 仍受 P1-002 约束，所以 P5-012 前不得声称正式外部 image delivery；
- PACS/DICOM、站点影像路由和长期存储均不在本设计范围。

## 11. 配置模型（设计，不是当前文件格式）

未来配置必须有 schema 和语义验证。下列结构只说明 owner，不是可以直接部署的 YAML：

~~~yaml
gateway:
  profile_id: selected-public-profile
  listener:
    bind: deployment-owned-address
    transport: profile-defined-secure-transport
  security:
    tls_certificate_ref: external-secret-reference
    client_trust_ref: external-trust-reference
    authorization_policy_ref: deployment-policy-reference
  routes:
    - route_id: approved-route
      pipeline_ref: immutable-pipeline-reference
      output_policy_ref: approved-output-policy
  limits:
    connection_bytes: policy-owned
    decode_bytes: policy-owned
    scan_bytes: policy-owned
    output_bytes: policy-owned
    connection_count: policy-owned
  audit:
    sink_ref: approved-audit-sink
~~~

所有具体值必须由 P0-006 的具名 owner、来源、适用范围和 review date 提供。开发者本机观察、
fixture 数字或现有 socket 缓冲区都不能成为 deployment policy。

## 12. 观测与运维

每个连接和 scan 生成不含 PHI 的 opaque correlation ID。最少需要以下关系：

~~~
ConnectionId -> GatewaySessionId -> GatewayScanId -> RunRecordId
                                  -> profile_id / route_id / principal reference
~~~

必须可观察的事件包括：listener start/stop、TLS/auth outcome、profile mismatch、header admission、
每个资源域 high-water、normal/cancel/fail/reject、output delivery、对端断线和一次性资源释放
结果。metrics/audit/trace 是机器可读证据；普通日志保持当前 plain-text 规则。不得将完整 header、
raw acquisition、token、证书私钥或患者标识写入任一观测面。

## 13. 验收矩阵

| 阶段 | 必须证明 | 最小证据 |
| --- | --- | --- |
| P5-008 | 只有一个外部边界、明确未实现状态、无私有 protocol/厂商 raw scope、所有决策 gate 可追踪 | 本文、AGENTS、计划、front-door docs 与 link/plan check |
| P5-009 | 精确标准 profile 与 byte-level contract 可互操作 | versioned public spec、conformance vectors、valid/invalid fixtures |
| P5-010 | listener 安全且网络状态有界 | fake peer、TLS/auth negative、fragmentation、length/timeout/slow-peer/resource tests |
| P5-011 | 外部流与 HDF5 在 runtime-frame 边界等价 | owned-buffer test、admission rollback、disconnect/cancel/provider failure corpus |
| P5-012 | 结果交付与 Connector 契约完整 | egress backpressure/terminal test、Connector conformance test、RunRecord/audit linkage |
| P5-013 | 真正部署可被资格化 | clean install、fuzz/security、fault/soak、approved peer、data governance、cross-platform evidence |

每项实现必须同时包含正向和负向 fixture。网络代码的 build 成功、一个手工 localhost 连接、
或一个已存在的 socket wrapper 都不能替代这些证据。

## 14. 当前阻塞决策

P5-008 不猜测下列产品输入；它们是 P5-009 以后实现的显式阻塞条件：

| ID | 所需决策 | 需要的 owner/input |
| --- | --- | --- |
| GWY-DEC-001 | 首个 Gateway Profile 的精确公开标准、版本、transport binding 与互操作参考 | architecture/protocol owner，附规范与许可证 |
| GWY-DEC-002 | deployment topology、listener 网络区、gateway/recon 进程关系、容量与 SLO | deployment/performance owner，纳入 P0-006 |
| GWY-DEC-003 | PKI、mTLS principal、route authorization、证书轮换/吊销与 secret owner | security owner，纳入 P0-006 |
| GWY-DEC-004 | 首个 Connector 的归属、发布、签名、漏洞响应和厂商数据处理授权 | integration owner / site owner |
| GWY-DEC-005 | upstream admission/flow 行为与不支持暂停时的明确失败策略 | Connector/profile owner |
| GWY-DEC-006 | 输出协议、partial/terminal 顺序、保留、断线后 delivery policy | output/data-governance owner，纳入 P0-006 |
| GWY-DEC-007 | 允许的数据类别、隐私、审计、spool 禁止或未来例外 | data-governance owner，纳入 P0-006 |

## 15. 实施顺序

1. P5-008 完成本文及所有规范入口的一致性；
2. P5-009 在 P0-006 输入到位后冻结一个公开 profile 和 conformance corpus；
3. P5-010 先完成安全、有界 fake-peer listener，而不是直接连接真实设备；
4. P5-011 才将 owned normalized event 接入 verified runtime；
5. P5-012 冻结 egress 与 Connector conformance；
6. P5-013 在获得真实对端、部署和数据治理授权后做系统资格化。

这条顺序刻意把协议与信任边界放在 socket 代码之前，把 runtime correctness 放在真实站点集成
之前。它使 ksj-gateway 成为真正可验收的外部集成网关，而不是一个名称相同的 scaffold 或未定义
relay。
