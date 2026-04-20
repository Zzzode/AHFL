# AHFL V0.18 Roadmap

本文给出 AHFL 在 V0.17 durable adapter receipt / review 闭环完成之后的下一阶段实施路线。V0.18 的重点不是直接交付真实 receipt persistence writer、object uploader、database writer、resume token 或 production recovery，而是把已经稳定的 `DurableStoreImportDecisionReceipt` 推进为“可表达 future adapter receipt persistence contract、可冻结 persistence blocker / capability gap、可被 future real durable store adapter 稳定消费”的下一层边界。

基线输入：

- [native-durable-store-adapter-receipt-persistence-prototype-bootstrap-v0.18.zh.md](../design/native-durable-store-adapter-receipt-persistence-prototype-bootstrap-v0.18.zh.md)
- [durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md](../reference/durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md)
- [native-consumer-matrix-v0.18.zh.md](../reference/native-consumer-matrix-v0.18.zh.md)
- [contributor-guide-v0.18.zh.md](../reference/contributor-guide-v0.18.zh.md)
- [roadmap-v0.17.zh.md](./roadmap-v0.17.zh.md)（已完成基线）
- [issue-backlog-v0.17.zh.md](./issue-backlog-v0.17.zh.md)（已完成基线）
- [native-durable-store-adapter-receipt-prototype-bootstrap-v0.17.zh.md](../design/native-durable-store-adapter-receipt-prototype-bootstrap-v0.17.zh.md)
- [durable-store-adapter-receipt-prototype-compatibility-v0.17.zh.md](../reference/durable-store-adapter-receipt-prototype-compatibility-v0.17.zh.md)
- [native-consumer-matrix-v0.17.zh.md](../reference/native-consumer-matrix-v0.17.zh.md)
- [contributor-guide-v0.17.zh.md](../reference/contributor-guide-v0.17.zh.md)

当前实现状态：

1. V0.17 已完成 durable adapter receipt / review 的模型、validation、bootstrap、CLI/backend 输出、golden regression、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能稳定跑通 `package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint record -> persistence descriptor -> export manifest -> store import descriptor -> durable store import request -> durable store import decision -> durable store import decision receipt -> durable store import decision receipt review` 的本地 deterministic 路径。
3. 当前仓库已提供 `DurableStoreImportDecisionReceiptPersistenceRequest` 与 `DurableStoreImportDecisionReceiptPersistenceReviewSummary` 的 direct model、validation、bootstrap、CLI/backend 输出与 single-file / project / workspace golden regression。
4. 当前阶段仍明确停留在 local/offline durable adapter receipt persistence contract handoff，不进入真实 object storage、database write、receipt persistence id、retry token、host telemetry、provider connector、retention/GC 或 crash recovery runtime 领域。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.18 的交付目标是：

1. 冻结 future real durable store adapter 对 V0.17 `DurableStoreImportDecisionReceipt` 的最小 receipt persistence 边界，使其能稳定表达“可持久化 / 阻塞 / 延后 / 拒绝”这一层 machine contract。
2. 引入最小 `DurableStoreImportDecisionReceiptPersistenceRequest` 语义，稳定表达 persistence request identity、persistence outcome、capability gap 与 persistence blocker。
3. 让 single-file、project、workspace 与 `--package` 路径都能生成 deterministic durable-adapter-receipt-persistence-facing prototype 输出。
4. 冻结 durable receipt persistence request / review 的 compatibility、consumer matrix 与 contributor guidance，明确 durable receipt、durable receipt persistence request、persistence review 与 future real adapter / receipt persistence 的职责边界。
5. 保持 V0.18 仍然是 local/offline durable adapter receipt persistence prototype，而不是 production durable store adapter / recovery 平台。

## 非目标

V0.18 仍然不直接承诺：

1. 真实 receipt persistence writer、数据库 schema、object storage layout、replication、queue durability、retention 或 garbage collection
2. 真实 object uploader、database writer、transaction commit protocol、repair job 或 operator console
3. 真实 import receipt persistence id、resume token、retry token、crash recovery、distributed recovery daemon、worker restart、failover orchestration 或 launcher ABI
4. 真实 capability connector、provider SDK、secret、endpoint、tenant、region、deployment metadata 或 storage credential
5. host telemetry、wall clock metrics、machine id、provider payload、event ingestion、SIEM integration 或 operator console

## 里程碑

### M0 V0.18 Scope、Adapter Receipt Persistence Contract 与 Layering 冻结

状态：

- [x] Issue 01 冻结 V0.18 durable adapter receipt persistence prototype scope 与 non-goals
- [x] Issue 02 冻结 durable adapter receipt persistence / capability gap 的最小模型与 adapter 边界
- [x] Issue 03 冻结 durable receipt、durable receipt persistence request 与 future real durable store adapter 的分层关系

目标：

1. 明确 V0.18 做的是 local durable adapter receipt persistence prototype，而不是真实 receipt persistence writer 或 production recovery。
2. 明确 durable adapter receipt persistence 必须建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision -> durable receipt` 之上，而不是直接读取 review、trace、AST 或 host log。
3. 明确 durable receipt、durable receipt persistence request、durable receipt persistence review 与 future real durable store adapter / receipt persistence / recovery ABI 的职责拆分。

### M1 Durable Adapter Receipt Persistence Model 与 Validation

状态：

- [x] Issue 04 引入 durable adapter receipt persistence 数据模型
- [x] Issue 05 增加 durable adapter receipt persistence validation 与 direct regression
- [x] Issue 06 增加 deterministic durable adapter receipt persistence bootstrap

目标：

1. 让仓库可以稳定表达 persistence request identity、persistence outcome、capability gap、persistence-ready 状态与 persistence blocker。
2. 让 adapter-receipt-persistence-facing failure 可以区分输入 artifact 错误、persistence model 错误与 prototype bootstrap 错误。
3. 让 durable adapter receipt persistence bootstrap 只依赖 V0.17 已冻结 artifact，而不是 CLI 文本或私有脚本约定。

### M2 Durable Adapter Receipt Persistence Output 与 Review Surface

状态：

- [x] Issue 07 增加 durable adapter receipt persistence 输出路径
- [x] Issue 08 增加 durable adapter receipt persistence review / persistence preview surface
- [x] Issue 09 增加 single-file / project / workspace durable adapter receipt persistence golden 回归

目标：

1. 让 contributor / future real durable store adapter consumer 可直接查看 durable-adapter-receipt-persistence-facing 状态，而不必拼接多份 artifact。
2. 让 durable adapter receipt persistence review 输出覆盖 persistence request identity、persistence outcome、capability gap、persistence boundary 与 next-step recommendation。
3. 让当前正式 fixture 至少稳定覆盖 persistence-ready、persistence-rejected 与 persistence-blocked 路径的 durable adapter receipt persistence prototype 输出，并通过 direct model regression 固定 deferred 语义。

### M3 Compatibility、Consumer Matrix 与 Adapter Guidance

状态：

- [x] Issue 10 冻结 durable adapter receipt persistence / review compatibility 契约
- [x] Issue 11 更新 native consumer matrix、contributor guide 与 future real durable store adapter boundary guidance

目标：

1. 让 adapter-receipt-persistence-facing artifact 的格式演进不依赖隐含约定。
2. 让 future real durable store adapter / receipt persistence / recovery explorer 清楚哪些字段可稳定依赖。
3. 把 adapter-receipt-persistence-facing artifact 纳入统一扩展模板，避免在 CLI 或外部脚本中重建私有 persistence 状态机。

### M4 工程化收口

状态：

- [x] Issue 12 建立 V0.18 durable adapter receipt persistence prototype regression、CI 与 reference 文档闭环

目标：

1. 让 durable adapter receipt persistence、compatibility、consumer matrix 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable request -> durable decision -> durable receipt -> durable receipt persistence request -> durable receipt persistence review”的 V0.18 路径。

## 关键依赖顺序

1. 先完成 M0，再引入 adapter-receipt-persistence-facing 模型
2. 先稳定 durable adapter receipt，再扩 durable adapter receipt persistence
3. 先冻结 compatibility / consumer matrix，再承诺 durable adapter receipt persistence prototype 的长期消费
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 durable adapter receipt persistence 未冻结前直接实现私有 real durable store adapter / receipt persistence / recovery 协议
2. 不要把真实 host log、provider payload、deployment telemetry、store URI、object path、database key、receipt persistence id 或 credential 塞进 durable receipt persistence / review 输出
3. 不要让 future adapter 回退读取 durable receipt review、durable decision review、durable request review、store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、raw source 或 host log 猜运行语义
4. 不要把 V0.18 的实现目标偷换成 production durable store adapter / recovery 平台开发

## 当前状态

V0.17 已完成并成为 V0.18 的正式上游基线；当前 `Issue 01-12` 已全部完成。仓库现已完成 V0.18 scope、compatibility、consumer matrix、contributor guide、direct model、validation、bootstrap、CLI/backend 输出、single-file/project/workspace golden、`ahfl-v0.18` 标签回归与 CI 切片收口。V0.18 当前建议目标链路为 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceExportManifest -> StoreImportDescriptor -> DurableStoreImportRequest -> DurableStoreImportDecision -> DurableStoreImportDecisionReceipt -> DurableStoreImportDecisionReceiptPersistenceRequest -> DurableStoreImportDecisionReceiptPersistenceReviewSummary`。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.18.zh.md](./issue-backlog-v0.18.zh.md)
