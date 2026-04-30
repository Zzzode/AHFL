# AHFL V0.24 Roadmap

本文给出 AHFL 在 V0.23 provider runtime preflight prototype 完成后的下一阶段实施路线。V0.24 的重点是冻结 provider SDK request envelope / host handoff readiness boundary：在 `ProviderRuntimePreflightPlan` 之后生成 secret-free SDK request envelope plan 与 host handoff readiness review，为后续 host execution prototype、真实 SDK adapter、配置加载、secret manager 接入保留稳定入口，但当前仍不加载配置、不解析 secret、不生成真实 SDK payload、不启动 host process、不打开网络连接、不调用 SDK。

基线输入：

- [roadmap-v0.23.zh.md](./roadmap-v0.23.zh.md)（已完成基线）
- [issue-backlog-v0.23.zh.md](./issue-backlog-v0.23.zh.md)（已完成基线）
- [native-durable-store-provider-runtime-preflight-prototype-bootstrap-v0.23.zh.md](../design/native-durable-store-provider-runtime-preflight-prototype-bootstrap-v0.23.zh.md)
- [durable-store-provider-runtime-preflight-prototype-compatibility-v0.23.zh.md](../reference/durable-store-provider-runtime-preflight-prototype-compatibility-v0.23.zh.md)
- [native-consumer-matrix-v0.23.zh.md](../reference/native-consumer-matrix-v0.23.zh.md)
- [contributor-guide-v0.23.zh.md](../reference/contributor-guide-v0.23.zh.md)

当前实现状态：

1. V0.23 已完成 `ProviderDriverBindingPlan -> ProviderRuntimePreflightPlan -> ProviderRuntimeReadinessReview`。
2. 当前仓库已新增 provider SDK envelope policy、provider SDK request envelope plan、provider SDK handoff readiness review。
3. V0.24 继续复用 V0.23 `ProviderRuntimePreflightPlan`，不得读取 readiness review 文本、host log、provider payload、secret manager 响应、SDK payload、host command 或私有脚本推导 SDK envelope state。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.24 的交付目标是：

1. 冻结 provider SDK envelope policy，明确 secret-free request schema ref 与 host handoff policy ref 的职责边界。
2. 定义 SDK request envelope plan，使 ready runtime preflight 可以被规划成 deterministic request envelope identity 与 host handoff descriptor identity。
3. 定义 envelope capability gating 与 failure attribution，区分 runtime preflight not ready、policy mismatch、unsupported capability。
4. 定义 handoff readiness review，让 reviewer 可以判断后续 host execution prototype 是否可以继续。
5. 建立 docs、model、validation、bootstrap、backend / CLI、golden、compatibility、CI 的闭环。

## 非目标

V0.24 仍不直接承诺：

1. 真实 SDK client、credential lifecycle、secret manager、KMS、IAM policy、tenant provisioning、runtime config loader 或 host process manager
2. 真实 endpoint、object path、database table、provider request / response payload、retry token、resume token、host command 或 network endpoint
3. recovery daemon、operator console、SIEM/event ingestion、host telemetry ingestion 或 production observability sink
4. 向后不兼容地修改 V0.23 provider runtime preflight 稳定字段
5. 绕过 AHFL artifact chain 的 SDK adapter 私有协议

## 里程碑

### M0 SDK Envelope Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.24 SDK request envelope / host handoff boundary 与 non-goals
- [x] Issue 02 冻结 V0.23 runtime preflight 与 SDK envelope 的输入边界
- [x] Issue 03 冻结 secret-free request schema ref 与 host handoff policy ref 的职责分层

### M1 Secret-Free SDK Envelope Policy Contract

状态：

- [x] Issue 04 定义 provider SDK envelope policy model
- [x] Issue 05 定义 envelope capability gating 与 unsupported capability diagnostics
- [x] Issue 06 定义 request envelope identity 与 host handoff descriptor identity namespace

### M2 Request Envelope 与 Handoff Readiness Planning

状态：

- [x] Issue 07 定义 provider SDK request envelope plan
- [x] Issue 08 定义 runtime-preflight-not-ready / policy-mismatch / capability-gap failure attribution
- [x] Issue 09 定义 provider SDK handoff readiness review 与真实 host execution ABI 的边界

### M3 Compatibility、Consumer Matrix 与 CI 收口

状态：

- [x] Issue 10 冻结 V0.24 compatibility contract
- [x] Issue 11 更新 native consumer matrix 与 contributor guide
- [x] Issue 12 建立 V0.24 regression、CI 与 migration docs 闭环

## 当前状态

V0.24 已完成 provider SDK request envelope prototype：secret-free envelope policy、request envelope planning、host handoff readiness review、golden regression、CI 标签与迁移文档均已落地。下一步可以进入 V0.25，开始设计 host execution prototype 的本地、无网络、无 secret、无真实 SDK 调用骨架。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.24.zh.md](./issue-backlog-v0.24.zh.md)
