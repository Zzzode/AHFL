# AHFL V0.26 Roadmap

本文给出 AHFL 在 V0.25 provider host execution prototype 完成后的下一阶段实施路线。V0.26 的重点是冻结 provider local host execution receipt boundary：在 `ProviderHostExecutionPlan` 之后生成本地 host execution harness 的受限、可审计、side-effect-free receipt 与 review，为后续真实 provider SDK adapter prototype 保留稳定入口，但当前仍不启动 host process、不读取 host environment、不打开网络连接、不生成 SDK payload、不调用 SDK、不写 host filesystem、不接入 secret manager。

基线输入：

- [roadmap-v0.25.zh.md](./roadmap-v0.25.zh.md)（已完成基线）
- [issue-backlog-v0.25.zh.md](./issue-backlog-v0.25.zh.md)（已完成基线）
- [native-durable-store-provider-host-execution-prototype-bootstrap-v0.25.zh.md](../design/native-durable-store-provider-host-execution-prototype-bootstrap-v0.25.zh.md)
- [durable-store-provider-host-execution-prototype-compatibility-v0.25.zh.md](../reference/durable-store-provider-host-execution-prototype-compatibility-v0.25.zh.md)
- [native-consumer-matrix-v0.25.zh.md](../reference/native-consumer-matrix-v0.25.zh.md)
- [contributor-guide-v0.25.zh.md](../reference/contributor-guide-v0.25.zh.md)

## 目标

V0.26 的交付目标是：

1. 定义 `ProviderLocalHostExecutionReceipt`，只消费 V0.25 `ProviderHostExecutionPlan`。
2. 为 ready host execution plan 生成 deterministic local host execution receipt identity。
3. 为 blocked host execution plan 保留 failure attribution，并保持 receipt blocked。
4. 定义 `ProviderLocalHostExecutionReceiptReview`，让 reviewer 可以判断后续 provider SDK adapter prototype 是否可以继续。
5. 建立 docs、model、validation、backend / CLI、golden、compatibility、CI 的闭环。

## 非目标

V0.26 仍不直接承诺：

1. 真实 SDK client、credential lifecycle、secret manager、KMS、IAM policy、tenant provisioning、runtime config loader 或 host process manager
2. 真实 endpoint、object path、database table、provider request / response payload、retry token、resume token、host command、host environment 或 network endpoint
3. recovery daemon、operator console、SIEM/event ingestion、host telemetry ingestion 或 production observability sink
4. 向后不兼容地修改 V0.25 provider host execution plan 稳定字段
5. 绕过 AHFL artifact chain 的 SDK adapter 私有协议

## 里程碑

### M0 Local Host Receipt Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.26 local host execution receipt boundary 与 non-goals
- [x] Issue 02 冻结 V0.25 host execution plan 的输入边界
- [x] Issue 03 定义 local host receipt identity namespace

### M1 Receipt 与 Review Contract

状态：

- [x] Issue 04 定义 provider local host execution receipt model
- [x] Issue 05 定义 host-execution-not-ready failure attribution
- [x] Issue 06 定义 provider local host execution receipt review

### M2 Compatibility、Consumer Matrix 与 CI 收口

状态：

- [x] Issue 07 冻结 V0.26 compatibility contract
- [x] Issue 08 更新 native consumer matrix 与 contributor guide
- [x] Issue 09 建立 V0.26 regression、golden、CI 与 migration docs 闭环

## 当前状态

V0.26 已完成 provider local host execution receipt prototype：receipt / review model、side-effect validation、CLI backend、single-file / project / workspace golden、CI 标签与迁移文档均已落地。下一步可以进入 V0.27，开始设计 provider SDK adapter prototype 的 artifact-only request / response boundary，但仍应避免真实 secret、网络、SDK 调用和生产 provider side effect。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.26.zh.md](./issue-backlog-v0.26.zh.md)
