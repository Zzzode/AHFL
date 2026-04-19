# AHFL V0.14 Roadmap

本文给出 AHFL 在 V0.13 export package prototype 闭环完成之后的下一阶段实施路线。V0.14 的重点不是直接交付真实 durable store import executor、object storage layout、resume token、crash recovery 或 production storage adapter，而是把已经稳定的 `PersistenceExportManifest` 推进为“可表达 store-import-adjacent machine contract、可冻结 store-import staging blocker、可被 future durable store adapter 稳定消费”的下一层边界。

基线输入：

- [roadmap-v0.13.zh.md](./roadmap-v0.13.zh.md)（已完成基线）
- [issue-backlog-v0.13.zh.md](./issue-backlog-v0.13.zh.md)（已完成基线）
- [native-export-package-prototype-bootstrap-v0.13.zh.md](../design/native-export-package-prototype-bootstrap-v0.13.zh.md)
- [export-package-prototype-compatibility-v0.13.zh.md](../reference/export-package-prototype-compatibility-v0.13.zh.md)
- [native-consumer-matrix-v0.13.zh.md](../reference/native-consumer-matrix-v0.13.zh.md)
- [contributor-guide-v0.13.zh.md](../reference/contributor-guide-v0.13.zh.md)
- [native-persistence-prototype-bootstrap-v0.12.zh.md](../design/native-persistence-prototype-bootstrap-v0.12.zh.md)
- [persistence-prototype-compatibility-v0.12.zh.md](../reference/persistence-prototype-compatibility-v0.12.zh.md)
- [checkpoint-prototype-compatibility-v0.11.zh.md](../reference/checkpoint-prototype-compatibility-v0.11.zh.md)
- [scheduler-prototype-compatibility-v0.10.zh.md](../reference/scheduler-prototype-compatibility-v0.10.zh.md)
- [cli-commands-v0.10.zh.md](../reference/cli-commands-v0.10.zh.md)

当前实现状态：

1. V0.13 已完成 export manifest / review 的模型、validation、bootstrap、CLI/backend 输出、golden regression、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能稳定跑通 `package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint record -> persistence descriptor -> export manifest -> export review` 的本地 deterministic 路径。
3. 当前仓库仍未提供正式的 store-import-adjacent machine contract、staging blocker taxonomy、store-import candidate bundle contract 或 future durable store adapter-facing artifact。
4. 当前 `PersistenceExportReviewSummary` 仍然只是 reviewer-facing projection，并不会承诺 store import executor ABI、store location、object path、import receipt、resume token 或 recovery command。
5. 当前阶段仍明确停留在 local/offline export package handoff，不进入真实 database / object storage、distributed recovery、host telemetry、provider connector、retention/GC 或 crash recovery runtime 领域。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.14 的交付目标是：

1. 冻结 future durable store adapter 的最小 store-import 输入边界，使其能稳定消费 V0.13 `PersistenceExportManifest`，而不必在 CLI、脚本或外部工具里私造 staging contract。
2. 引入最小 `StoreImportDescriptor` 语义，稳定表达 store-import candidate identity、staging artifact set、source export chain、import readiness 与 staging blocker。
3. 让 single-file、project、workspace 与 `--package` 路径都能生成 deterministic store-import-facing prototype 输出。
4. 冻结 store import descriptor / review 的 compatibility、consumer matrix 与 contributor guidance，明确 export manifest、store import descriptor、review surface 与 future durable store adapter 的职责边界。
5. 保持 V0.14 仍然是 local/offline store-import prototype，而不是 production durable store / recovery 平台。

## 非目标

V0.14 仍然不直接承诺：

1. 真实 durable checkpoint store、数据库 schema、object storage layout、replication、queue durability、retention 或 garbage collection
2. 真实 store import executor、transaction commit protocol、object uploader、database writer、repair job 或 operator console
3. 真实 resume token、crash recovery、distributed recovery daemon、worker restart、failover orchestration 或 launcher ABI
4. 真实 capability connector、provider SDK、secret、endpoint、tenant、region、deployment metadata 或 storage credential
5. host telemetry、wall clock metrics、machine id、provider payload、event ingestion、SIEM integration 或 operator console

## 里程碑

### M0 V0.14 Scope、Store Import Contract 与 Adapter Boundary 冻结

状态：

- [ ] Issue 01 冻结 V0.14 store import prototype scope 与 non-goals
- [ ] Issue 02 冻结 store import descriptor / staging artifact set 的最小模型与 adapter 边界
- [ ] Issue 03 冻结 export manifest、store import descriptor 与 future durable store adapter 的分层关系

目标：

1. 明确 V0.14 做的是 local store-import descriptor prototype，而不是 durable store executor 或 production recovery。
2. 明确 store import descriptor 必须建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor -> export manifest` 之上，而不是直接读取 review、trace、AST 或 host log。
3. 明确 export manifest、store import descriptor、store import review 与 future durable store adapter / recovery ABI 的职责拆分。

### M1 Store Import Descriptor Model 与 Validation

状态：

- [ ] Issue 04 引入 store import descriptor 数据模型
- [ ] Issue 05 增加 store import descriptor validation 与 direct regression
- [ ] Issue 06 增加 deterministic store import descriptor bootstrap

目标：

1. 让仓库可以稳定表达 store-import candidate identity、staging artifact set、import-ready 状态与 staging blocker。
2. 让 store-import-facing failure 可以区分输入 artifact 错误、descriptor model 错误与 prototype bootstrap 错误。
3. 让 store import descriptor bootstrap 只依赖 V0.13 已冻结 artifact，而不是 CLI 文本或私有脚本约定。

### M2 Store Import Output 与 Review Surface

状态：

- [ ] Issue 07 增加 store import descriptor 输出路径
- [ ] Issue 08 增加 store import review / staging preview surface
- [ ] Issue 09 增加 single-file / project / workspace store import golden 回归

目标：

1. 让 contributor / future durable store adapter consumer 可直接查看 store-import-facing 状态，而不必拼接多份 artifact。
2. 让 store import review 输出覆盖 candidate identity、staging artifact set、staging blocker、local/store boundary 与 next-step recommendation。
3. 让成功路径、partial 路径与 failure-path 都能稳定生成 store import prototype 输出。

### M3 Compatibility、Consumer Matrix 与 Adapter Guidance

状态：

- [ ] Issue 10 冻结 store import descriptor / review compatibility 契约
- [ ] Issue 11 更新 native consumer matrix、contributor guide 与 future durable store adapter boundary guidance

目标：

1. 让 store-import-facing artifact 的格式演进不依赖隐含约定。
2. 让 future durable store adapter / recovery explorer 清楚哪些字段可稳定依赖。
3. 把 store-import-facing artifact 纳入统一扩展模板，避免在 CLI 或外部脚本中重建私有 staging 状态机。

### M4 工程化收口

状态：

- [ ] Issue 12 建立 V0.14 store import prototype regression、CI 与 reference 文档闭环

目标：

1. 让 store import descriptor、compatibility、consumer matrix 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint -> persistence descriptor -> export manifest -> store import descriptor -> store import review”的 V0.14 路径。

## 关键依赖顺序

1. 先完成 M0，再引入 store-import-facing 模型
2. 先稳定 export manifest，再扩 store import descriptor
3. 先冻结 compatibility / consumer matrix，再承诺 store import prototype 的长期消费
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 store import descriptor 未冻结前直接实现私有 durable store / recovery 协议
2. 不要把真实 host log、provider payload、deployment telemetry、store URI、object path 或 credential 塞进 store import descriptor / review 输出
3. 不要让 future adapter 回退读取 export review、persistence review、checkpoint review、scheduler review、trace、AST、raw source 或 host log 猜运行语义
4. 不要把 V0.14 的实现目标偷换成 production persistence / recovery 平台开发

## 当前状态

V0.13 已完成并成为 V0.14 的正式上游基线；当前仓库已经把 export package prototype 收口为稳定闭环，但还没有 store-import-facing machine contract。建议下一步进入 `Issue 01-03`，先冻结 V0.14 scope、store import descriptor 最小模型与 layering，再进入模型实现。V0.14 当前建议目标链路为 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceExportManifest -> StoreImportDescriptor -> StoreImportReviewSummary`。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.14.zh.md](./issue-backlog-v0.14.zh.md)
