# AHFL V0.23 Roadmap

本文给出 AHFL 在 V0.22 provider driver binding prototype 完成后的下一阶段实施路线。V0.23 的重点是冻结 runtime / SDK adapter preflight boundary：在 `ProviderDriverBindingPlan` 之后生成 secret-free runtime profile、runtime preflight plan 与 runtime readiness review，为后续真实 SDK adapter、配置加载、secret manager 接入保留稳定入口，但当前仍不加载配置、不解析 secret、不调用 SDK。

基线输入：

- [roadmap-v0.22.zh.md](./roadmap-v0.22.zh.md)（已完成基线）
- [issue-backlog-v0.22.zh.md](./issue-backlog-v0.22.zh.md)（已完成基线）
- [native-durable-store-provider-driver-prototype-bootstrap-v0.22.zh.md](../design/native-durable-store-provider-driver-prototype-bootstrap-v0.22.zh.md)
- [durable-store-provider-driver-prototype-compatibility-v0.22.zh.md](../reference/durable-store-provider-driver-prototype-compatibility-v0.22.zh.md)
- [native-consumer-matrix-v0.22.zh.md](../reference/native-consumer-matrix-v0.22.zh.md)
- [contributor-guide-v0.22.zh.md](../reference/contributor-guide-v0.22.zh.md)

当前实现状态：

1. V0.22 已完成 `ProviderWriteAttemptPreview -> ProviderDriverBindingPlan -> ProviderDriverReadinessReview`。
2. 当前仓库已新增 provider runtime profile、provider runtime preflight plan、provider runtime readiness review。
3. V0.23 继续复用 V0.22 `ProviderDriverBindingPlan`，不得读取 readiness review 文本、host log、provider payload、secret manager 响应或私有脚本推导 runtime preflight state。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.23 的交付目标是：

1. 冻结 provider runtime profile，明确 runtime config loader、secret resolver placeholder、future SDK adapter 的职责边界。
2. 定义 secret-free runtime profile，不把 credential、secret value、endpoint URI、object path、database table、SDK payload schema 或 request payload 写入 stable artifact。
3. 定义 runtime capability gating 与 preflight plan，使 driver operation descriptor 可以被规划为 future SDK invocation envelope identity，但当前不执行任何 side effect。
4. 定义 readiness review，让 reviewer 可以判断 future SDK adapter implementation 是否可以继续。
5. 建立 docs、model、validation、bootstrap、backend / CLI、golden、compatibility、CI 的闭环。

## 非目标

V0.23 仍不直接承诺：

1. 真实 SDK client、credential lifecycle、secret manager、KMS、IAM policy、tenant provisioning 或 runtime config loader
2. 真实 endpoint、object path、database table、provider request / response payload、retry token、resume token 或 idempotency backend
3. recovery daemon、operator console、SIEM/event ingestion、host telemetry ingestion 或 production observability sink
4. 向后不兼容地修改 V0.22 provider driver binding 稳定字段
5. 绕过 AHFL artifact chain 的 SDK adapter 私有协议

## 里程碑

### M0 Runtime Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.23 runtime / SDK adapter preflight boundary 与 non-goals
- [x] Issue 02 冻结 config loader、secret resolver placeholder 与 future SDK adapter 的职责分层
- [x] Issue 03 冻结 V0.22 provider driver binding 与 runtime preflight 的输入边界

### M1 Secret-Free Runtime Profile Contract

状态：

- [x] Issue 04 定义 secret-free provider runtime profile model
- [x] Issue 05 定义 runtime capability gating 与 unsupported capability diagnostics
- [x] Issue 06 定义 SDK invocation envelope identity namespace

### M2 Runtime Preflight 与 Readiness Planning

状态：

- [x] Issue 07 定义 provider runtime preflight plan
- [x] Issue 08 定义 binding-not-ready / profile-mismatch / capability-gap failure attribution
- [x] Issue 09 定义 provider runtime readiness review 与真实 SDK adapter ABI 的边界

### M3 Compatibility、Consumer Matrix 与 CI 收口

状态：

- [x] Issue 10 冻结 V0.23 compatibility contract
- [x] Issue 11 更新 native consumer matrix 与 contributor guide
- [x] Issue 12 建立 V0.23 regression、CI 与 migration docs 闭环

## 当前状态

V0.23 已完成 provider runtime preflight prototype：secret-free provider runtime profile、runtime capability gating、preflight plan、readiness review、golden regression、CI 标签与迁移文档均已落地。下一步可以进入 V0.24，开始设计真实 SDK adapter request envelope 的 schema 与 host execution handoff，但仍应避免接入真实 secret 或 SDK side effect。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.23.zh.md](./issue-backlog-v0.23.zh.md)
