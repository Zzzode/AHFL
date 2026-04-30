# AHFL V0.25 Roadmap

本文给出 AHFL 在 V0.24 provider SDK request envelope prototype 完成后的下一阶段实施路线。V0.25 的重点是冻结 provider host execution planning boundary：在 `ProviderSdkRequestEnvelopePlan` 之后生成 secret-free、network-free host execution plan 与 host execution readiness review，为后续本地 host execution harness、真实 SDK adapter、配置加载、secret manager 接入保留稳定入口，但当前仍不加载配置、不解析 secret、不启动 host process、不读取 host environment、不打开网络连接、不生成真实 SDK payload、不调用 SDK、不写 host filesystem。

基线输入：

- [roadmap-v0.24.zh.md](./roadmap-v0.24.zh.md)（已完成基线）
- [issue-backlog-v0.24.zh.md](./issue-backlog-v0.24.zh.md)（已完成基线）
- [native-durable-store-provider-sdk-envelope-prototype-bootstrap-v0.24.zh.md](../design/native-durable-store-provider-sdk-envelope-prototype-bootstrap-v0.24.zh.md)
- [durable-store-provider-sdk-envelope-prototype-compatibility-v0.24.zh.md](../reference/durable-store-provider-sdk-envelope-prototype-compatibility-v0.24.zh.md)
- [native-consumer-matrix-v0.24.zh.md](../reference/native-consumer-matrix-v0.24.zh.md)
- [contributor-guide-v0.24.zh.md](../reference/contributor-guide-v0.24.zh.md)

当前实现状态：

1. V0.24 已完成 `ProviderRuntimePreflightPlan -> ProviderSdkRequestEnvelopePlan -> ProviderSdkHandoffReadinessReview`。
2. 当前仓库已新增 provider host execution policy、provider host execution plan、provider host execution readiness review。
3. V0.25 继续复用 V0.24 `ProviderSdkRequestEnvelopePlan`，不得读取 readiness review 文本、host log、provider payload、secret manager 响应、SDK payload、host command、host environment 或私有脚本推导 host execution state。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.25 的交付目标是：

1. 冻结 provider host execution policy，明确 execution profile ref、sandbox policy ref 与 timeout policy ref 的职责边界。
2. 定义 host execution plan，使 ready SDK request envelope 可以被规划成 deterministic host execution descriptor identity 与 dry-run host receipt placeholder identity。
3. 定义 host capability gating 与 failure attribution，区分 SDK handoff not ready、host policy mismatch、unsupported host capability。
4. 定义 host execution readiness review，让 reviewer 可以判断后续 local host execution harness 是否可以继续。
5. 建立 docs、model、validation、bootstrap、backend / CLI、golden、compatibility、CI 的闭环。

## 非目标

V0.25 仍不直接承诺：

1. 真实 SDK client、credential lifecycle、secret manager、KMS、IAM policy、tenant provisioning、runtime config loader 或 host process manager
2. 真实 endpoint、object path、database table、provider request / response payload、retry token、resume token、host command、host environment 或 network endpoint
3. recovery daemon、operator console、SIEM/event ingestion、host telemetry ingestion 或 production observability sink
4. 向后不兼容地修改 V0.24 provider SDK request envelope 稳定字段
5. 绕过 AHFL artifact chain 的 host executor 或 SDK adapter 私有协议

## 里程碑

### M0 Host Execution Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.25 host execution planning boundary 与 non-goals
- [x] Issue 02 冻结 V0.24 SDK envelope 与 host execution 的输入边界
- [x] Issue 03 冻结 execution profile、sandbox policy 与 timeout policy 的职责分层

### M1 Secret-Free Host Execution Policy Contract

状态：

- [x] Issue 04 定义 provider host execution policy model
- [x] Issue 05 定义 host capability gating 与 unsupported capability diagnostics
- [x] Issue 06 定义 host execution descriptor 与 host receipt placeholder identity namespace

### M2 Host Execution 与 Readiness Planning

状态：

- [x] Issue 07 定义 provider host execution plan
- [x] Issue 08 定义 sdk-handoff-not-ready / policy-mismatch / capability-gap failure attribution
- [x] Issue 09 定义 provider host execution readiness review 与真实 host execution harness ABI 的边界

### M3 Compatibility、Consumer Matrix 与 CI 收口

状态：

- [x] Issue 10 冻结 V0.25 compatibility contract
- [x] Issue 11 更新 native consumer matrix 与 contributor guide
- [x] Issue 12 建立 V0.25 regression、CI 与 migration docs 闭环

## 当前状态

V0.25 已完成 provider host execution prototype：secret-free、network-free host execution policy、host execution planning、host execution readiness review、golden regression、CI 标签与迁移文档均已落地。下一步可以进入 V0.26，开始设计本地 host execution harness 的受限执行记录，但仍应避免真实 secret、网络、SDK 调用和生产 provider side effect。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.25.zh.md](./issue-backlog-v0.25.zh.md)
