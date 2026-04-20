# AHFL V0.15 Roadmap

本文给出 AHFL 在 V0.14 store import prototype 闭环完成之后的下一阶段实施路线。V0.15 的重点不是直接交付真实 durable store import executor、object uploader、database writer、resume token、crash recovery 或 production durable store adapter，而是把已经稳定的 `StoreImportDescriptor` 推进为“可表达 durable-store-import machine contract、可冻结 adapter blocker、可被 future real durable store adapter 稳定消费”的下一层边界。

基线输入：

- [native-durable-store-import-prototype-bootstrap-v0.15.zh.md](../design/native-durable-store-import-prototype-bootstrap-v0.15.zh.md)
- [durable-store-import-prototype-compatibility-v0.15.zh.md](../reference/durable-store-import-prototype-compatibility-v0.15.zh.md)
- [roadmap-v0.14.zh.md](./roadmap-v0.14.zh.md)（已完成基线）
- [issue-backlog-v0.14.zh.md](./issue-backlog-v0.14.zh.md)（已完成基线）
- [native-store-import-prototype-bootstrap-v0.14.zh.md](../design/native-store-import-prototype-bootstrap-v0.14.zh.md)
- [store-import-prototype-compatibility-v0.14.zh.md](../reference/store-import-prototype-compatibility-v0.14.zh.md)
- [native-consumer-matrix-v0.14.zh.md](../reference/native-consumer-matrix-v0.14.zh.md)
- [contributor-guide-v0.14.zh.md](../reference/contributor-guide-v0.14.zh.md)

当前实现状态：

1. V0.14 已完成 store import descriptor / review 的模型、validation、bootstrap、CLI/backend 输出、golden regression、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能稳定跑通 `package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint record -> persistence descriptor -> export manifest -> store import descriptor -> store import review` 的本地 deterministic 路径。
3. 当前仓库已提供 `DurableStoreImportRequest` direct model、validation 与 deterministic bootstrap；尚未提供 `DurableStoreImportReviewSummary` reviewer-facing artifact、CLI/backend 输出或 golden regression。
4. 当前阶段仍明确停留在 local/offline durable store import request handoff，不进入真实 object storage、database write、distributed recovery、host telemetry、provider connector、retention/GC 或 crash recovery runtime 领域。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.15 的交付目标是：

1. 冻结 future real durable store adapter 的最小 durable-store-import 输入边界，使其能稳定消费 V0.14 `StoreImportDescriptor`，而不必在 CLI、脚本或外部工具里私造 adapter request contract。
2. 引入最小 `DurableStoreImportRequest` 语义，稳定表达 durable store import request identity、requested artifact set、adapter readiness 与 adapter blocker。
3. 让 single-file、project、workspace 与 `--package` 路径都能生成 deterministic durable-store-import-facing prototype 输出。
4. 冻结 durable store import request / review 的 compatibility、consumer matrix 与 contributor guidance，明确 store import descriptor、durable store import request、review surface 与 future real durable store adapter 的职责边界。
5. 保持 V0.15 仍然是 local/offline durable store import prototype，而不是 production durable store / recovery 平台。

## 非目标

V0.15 仍然不直接承诺：

1. 真实 durable checkpoint store、数据库 schema、object storage layout、replication、queue durability、retention 或 garbage collection
2. 真实 durable store import executor、object uploader、database writer、transaction commit protocol、repair job 或 operator console
3. 真实 import receipt、resume token、crash recovery、distributed recovery daemon、worker restart、failover orchestration 或 launcher ABI
4. 真实 capability connector、provider SDK、secret、endpoint、tenant、region、deployment metadata 或 storage credential
5. host telemetry、wall clock metrics、machine id、provider payload、event ingestion、SIEM integration 或 operator console

## 里程碑

### M0 V0.15 Scope、Durable Store Import Contract 与 Adapter Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.15 durable store import prototype scope 与 non-goals
- [x] Issue 02 冻结 durable store import request / requested artifact set 的最小模型与 adapter 边界
- [x] Issue 03 冻结 store import descriptor、durable store import request 与 future real durable store adapter 的分层关系

目标：

1. 明确 V0.15 做的是 local durable-store-import request prototype，而不是真实 durable store executor 或 production recovery。
2. 明确 durable store import request 必须建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor` 之上，而不是直接读取 review、trace、AST 或 host log。
3. 明确 store import descriptor、durable store import request、durable store import review 与 future real durable store adapter / recovery ABI 的职责拆分。

### M1 Durable Store Import Request Model 与 Validation

状态：

- [x] Issue 04 引入 durable store import request 数据模型
- [x] Issue 05 增加 durable store import request validation 与 direct regression
- [x] Issue 06 增加 deterministic durable store import request bootstrap

目标：

1. 让仓库可以稳定表达 request identity、requested artifact set、adapter-ready 状态与 adapter blocker。
2. 让 durable-store-import-facing failure 可以区分输入 artifact 错误、request model 错误与 prototype bootstrap 错误。
3. 让 durable store import request bootstrap 只依赖 V0.14 已冻结 artifact，而不是 CLI 文本或私有脚本约定。

### M2 Durable Store Import Output 与 Review Surface

状态：

- [x] Issue 07 增加 durable store import request 输出路径
- [x] Issue 08 增加 durable store import review / request preview surface
- [x] Issue 09 增加 single-file / project / workspace durable store import golden 回归

目标：

1. 让 contributor / future real durable store adapter consumer 可直接查看 durable-store-import-facing 状态，而不必拼接多份 artifact。
2. 让 durable store import review 输出覆盖 request identity、requested artifact set、adapter blocker、adapter boundary 与 next-step recommendation。
3. 让成功路径、partial 路径与 failure-path 都能稳定生成 durable store import prototype 输出。

### M3 Compatibility、Consumer Matrix 与 Adapter Guidance

状态：

- [x] Issue 10 冻结 durable store import request / review compatibility 契约
- [x] Issue 11 更新 native consumer matrix、contributor guide 与 future real durable store adapter boundary guidance

目标：

1. 让 durable-store-import-facing artifact 的格式演进不依赖隐含约定。
2. 让 future real durable store adapter / recovery explorer 清楚哪些字段可稳定依赖。
3. 把 durable-store-import-facing artifact 纳入统一扩展模板，避免在 CLI 或外部脚本中重建私有 request 状态机。

### M4 工程化收口

状态：

- [x] Issue 12 建立 V0.15 durable store import prototype regression、CI 与 reference 文档闭环

目标：

1. 让 durable store import request、compatibility、consumer matrix 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> durable store import request -> durable store import review”的 V0.15 路径。

## 关键依赖顺序

1. 先完成 M0，再引入 durable-store-import-facing 模型
2. 先稳定 store import descriptor，再扩 durable store import request
3. 先冻结 compatibility / consumer matrix，再承诺 durable store import prototype 的长期消费
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 durable store import request 未冻结前直接实现私有 real durable store / recovery 协议
2. 不要把真实 host log、provider payload、deployment telemetry、store URI、object path、database key 或 credential 塞进 durable store import request / review 输出
3. 不要让 future adapter 回退读取 store import review、export review、persistence review、checkpoint review、scheduler review、trace、AST、raw source 或 host log 猜运行语义
4. 不要把 V0.15 的实现目标偷换成 production durable store / recovery 平台开发

## 当前状态

V0.14 已完成并成为 V0.15 的正式上游基线；当前 `Issue 01-12` 已完成。仓库现已新增 V0.15 scope 文档、compatibility contract、consumer matrix、contributor guide、`DurableStoreImportRequest` direct model / validation / bootstrap、`DurableStoreImportReviewSummary` reviewer surface、`emit-durable-store-import-request` / `emit-durable-store-import-review` CLI/backend 输出，以及 single-file / project / workspace golden regression，并已在 CI 中显式执行 `ahfl-v0.15` 切片。V0.15 当前建议目标链路为 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceExportManifest -> StoreImportDescriptor -> DurableStoreImportRequest -> DurableStoreImportReviewSummary`。下一步建议进入 V0.16，讨论真实 durable store adapter 前还需要冻结哪些 machine contract，而不是继续扩张 V0.15 prototype 的非目标。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.15.zh.md](./issue-backlog-v0.15.zh.md)
