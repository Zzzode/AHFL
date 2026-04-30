# AHFL V0.22 Roadmap

本文给出 AHFL 在 V0.21 provider-neutral durable store adapter planning 完成后的下一阶段实施路线。V0.22 的重点是启动 provider-specific driver extension boundary：冻结 secret-free driver profile、driver capability gating、binding plan、readiness review 与真实 SDK 接入前的职责边界。V0.22 不直接接入云 SDK、credential、真实 object store、database writer 或 recovery daemon。

基线输入：

- [roadmap-v0.21.zh.md](./roadmap-v0.21.zh.md)（已完成基线）
- [issue-backlog-v0.21.zh.md](./issue-backlog-v0.21.zh.md)（已完成基线）
- [native-durable-store-provider-adapter-prototype-bootstrap-v0.21.zh.md](../design/native-durable-store-provider-adapter-prototype-bootstrap-v0.21.zh.md)
- [durable-store-provider-adapter-prototype-compatibility-v0.21.zh.md](../reference/durable-store-provider-adapter-prototype-compatibility-v0.21.zh.md)
- [native-consumer-matrix-v0.21.zh.md](../reference/native-consumer-matrix-v0.21.zh.md)
- [contributor-guide-v0.21.zh.md](../reference/contributor-guide-v0.21.zh.md)

当前实现状态：

1. V0.21 已完成 `AdapterExecutionReceipt -> ProviderWriteAttemptPreview -> ProviderRecoveryHandoffPreview`。
2. 当前仓库已新增 provider driver profile、provider driver binding plan、provider driver readiness review。
3. V0.22 继续复用 V0.21 `ProviderWriteAttemptPreview`，不得回退读取 recovery handoff text、CLI 文本、host log、provider payload 或私有脚本推导 driver binding state。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.22 的交付目标是：

1. 冻结 provider driver profile，明确 provider-neutral shim、provider-specific driver 与 future SDK writer 的职责拆分。
2. 定义 secret-free driver profile，不把 credential、endpoint URI、object path、database table 或 SDK payload schema 写入 stable artifact。
3. 定义 driver capability gating 与 binding plan，使 provider write intent 可以被 future driver 翻译，但当前不调用 SDK。
4. 定义 readiness review，让 reviewer 可以判断 future driver implementation 是否可以继续。
5. 建立 docs、model、validation、bootstrap、backend / CLI、golden、compatibility、CI 的闭环。

## 非目标

V0.22 仍不直接承诺：

1. 真实云 SDK、credential lifecycle、secret manager、KMS、IAM policy 或 tenant provisioning
2. 生产数据库 schema、object storage layout、replication、retention、GC、跨 region failover 或 consistency protocol
3. recovery daemon、真实 retry token、真实 resume token、operator console、SIEM/event ingestion 或 host telemetry ingestion
4. 向后不兼容地修改 V0.21 provider write attempt 稳定字段
5. 绕过 AHFL artifact chain 的 provider driver 私有协议

## 里程碑

### M0 Driver Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.22 provider driver extension boundary 与 non-goals
- [x] Issue 02 冻结 provider-neutral shim、provider-specific driver 与 production SDK writer 的职责分层
- [x] Issue 03 冻结 V0.21 provider write attempt 与 future driver 的输入边界

### M1 Secret-Free Driver Profile Contract

状态：

- [x] Issue 04 定义 secret-free provider driver profile model
- [x] Issue 05 定义 driver capability gating 与 unsupported capability diagnostics
- [x] Issue 06 定义 driver binding operation descriptor namespace

### M2 Driver Binding 与 Readiness Planning

状态：

- [x] Issue 07 定义 provider driver binding plan
- [x] Issue 08 定义 profile mismatch / capability gap / source-not-planned failure attribution
- [x] Issue 09 定义 provider driver readiness review 与真实 SDK ABI 的边界

### M3 Compatibility、Consumer Matrix 与 CI 收口

状态：

- [x] Issue 10 冻结 V0.22 compatibility contract
- [x] Issue 11 更新 native consumer matrix 与 contributor guide
- [x] Issue 12 建立 V0.22 regression、CI 与 migration docs 闭环

## 当前状态

V0.22 已完成 provider driver binding prototype：secret-free provider driver profile、driver capability gating、binding plan、readiness review、golden regression、CI 标签与迁移文档均已落地。下一步可以进入 V0.23，开始设计真实 runtime / provider SDK adapter 的配置加载与 secret manager 接入前置边界。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.22.zh.md](./issue-backlog-v0.22.zh.md)
