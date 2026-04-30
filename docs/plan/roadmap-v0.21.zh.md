# AHFL V0.21 Roadmap

本文给出 AHFL 在 V0.20 production-adjacent local fake durable store adapter execution 完成后的下一阶段实施路线。V0.21 的重点是启动 real provider adapter boundary planning：冻结 provider-neutral adapter contract、secret-free adapter config、persistence id namespace、retry / recovery handoff 与 provider extension point。V0.21 不直接接入某个云 SDK，也不交付生产数据库或对象存储写入器；它先把真实 provider adapter 将来如何替换 V0.20 fake store 的边界做成 issue-ready backlog。

基线输入：

- [roadmap-v0.20.zh.md](./roadmap-v0.20.zh.md)（已完成基线）
- [issue-backlog-v0.20.zh.md](./issue-backlog-v0.20.zh.md)（已完成基线）
- [native-durable-store-adapter-execution-prototype-bootstrap-v0.20.zh.md](../design/native-durable-store-adapter-execution-prototype-bootstrap-v0.20.zh.md)
- [durable-store-adapter-execution-prototype-compatibility-v0.20.zh.md](../reference/durable-store-adapter-execution-prototype-compatibility-v0.20.zh.md)
- [native-consumer-matrix-v0.20.zh.md](../reference/native-consumer-matrix-v0.20.zh.md)
- [contributor-guide-v0.20.zh.md](../reference/contributor-guide-v0.20.zh.md)

当前实现状态：

1. V0.20 已完成 `Response -> AdapterExecutionReceipt -> RecoveryCommandPreview` 的 local fake durable store contract。
2. 当前仓库有 deterministic fake persistence id、accepted / rejected / partial CLI golden，以及 accepted、blocked、deferred、rejected direct regression。
3. 当前仓库已新增 provider adapter config、provider capability matrix、provider write attempt preview、retry / resume placeholder 与 recovery handoff preview。
4. V0.21 继续复用 V0.20 `AdapterExecutionReceipt`，不得回退读取 reviewer preview、CLI 文本、host log、provider payload 或私有脚本推导 provider adapter state。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.21 的交付目标是：

1. 冻结 provider-neutral adapter boundary，明确 fake store、provider adapter shim、provider driver 与 future production writer 的职责拆分。
2. 定义 secret-free provider adapter config 与 provider capability matrix，不把 credential、endpoint secret 或 tenant secret 写入 machine artifact。
3. 定义 real persistence id namespace、provider write intent、retry / resume placeholder 与 recovery handoff 的最小分层。
4. 明确 future provider-specific adapter 只能消费 V0.20 `AdapterExecutionReceipt` 和上游 machine artifacts，不能从 `RecoveryCommandPreview` 或 CLI 文本反推状态。
5. 输出 issue-ready backlog，使后续可以按 boundary docs、model、validation、bootstrap、backend / CLI、golden、compatibility、CI 的顺序推进。

## 非目标

V0.21 初始阶段仍不直接承诺：

1. 真实云 SDK、credential lifecycle、secret manager、KMS、IAM policy 或 tenant provisioning
2. 生产数据库 schema、object storage layout、replication、retention、GC、跨 region failover 或 consistency protocol
3. 分布式 worker restart、crash recovery daemon、operator console、SIEM/event ingestion 或 host telemetry ingestion
4. 向后不兼容地修改 V0.20 `AdapterExecutionReceipt` 稳定字段
5. 绕过 AHFL artifact chain 的私有 provider adapter protocol

## 里程碑

### M0 Provider-Neutral Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.21 real provider adapter boundary 与 non-goals
- [x] Issue 02 冻结 fake store、provider adapter shim、provider driver 与 production writer 的职责分层
- [x] Issue 03 冻结 V0.20 adapter execution 与 future provider-specific adapter 的输入边界

目标：

1. 明确 V0.21 是 provider-neutral contract 阶段，不是 provider SDK 集成阶段。
2. 明确真实 provider adapter 只能从 V0.20 `AdapterExecutionReceipt` 和上游 machine artifact 获取输入。
3. 明确哪些字段属于 stable provider-neutral input，哪些字段只能留给 future provider extension。

### M1 Secret-Free Provider Adapter Contract

状态：

- [x] Issue 04 定义 secret-free provider adapter config model
- [x] Issue 05 定义 provider capability matrix 与 unsupported capability diagnostics
- [x] Issue 06 定义 provider-neutral persistence id namespace 与 write intent model

目标：

1. 把 provider adapter 的配置边界做成不含 secret 的 machine artifact。
2. 让 future provider adapter 可以声明 capability support / gap，而不是在执行时隐式失败。
3. 把 fake persistence id 与 future real persistence id 的 namespace 规则分开。

### M2 Retry / Recovery Handoff Planning

状态：

- [x] Issue 07 定义 provider write attempt preview / dry-run handoff
- [x] Issue 08 定义 retry / resume placeholder 与 failure attribution 分层
- [x] Issue 09 定义 recovery handoff preview 与真实 recovery daemon ABI 的边界

目标：

1. 在没有真实 provider writer 的前提下固定 retry / recovery 语义入口。
2. 区分 adapter contract error、provider capability error、provider write planning error 与 recovery planning error。
3. 避免把真实 retry token、resume token、operator payload 或 telemetry 混入 stable artifact。

### M3 Compatibility、Consumer Matrix 与 CI 收口

状态：

- [x] Issue 10 冻结 V0.21 compatibility contract
- [x] Issue 11 更新 native consumer matrix 与 contributor guide
- [x] Issue 12 建立 V0.21 regression、CI 与 migration docs 闭环

目标：

1. 把 provider-neutral durable adapter boundary 纳入持续回归。
2. 让 future provider-specific adapter work 有明确 docs / code / golden / tests 同步流程。
3. 保持 V0.20 adapter execution compatibility 不被 V0.21 静默破坏。

## 当前状态

V0.21 已完成 provider-neutral durable store adapter prototype：secret-free provider adapter config、provider capability matrix、provider-neutral write intent、retry / resume placeholder、provider write attempt JSON、provider recovery handoff preview、golden regression、CI 标签与迁移文档均已落地。下一步可以进入 V0.22，开始设计 provider-specific driver extension 和真实 SDK 接入前的配置加载边界。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.21.zh.md](./issue-backlog-v0.21.zh.md)
