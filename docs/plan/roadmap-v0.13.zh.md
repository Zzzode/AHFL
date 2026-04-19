# AHFL V0.13 Roadmap

本文给出 AHFL 在 V0.12 persistence descriptor / review 闭环完成之后的下一阶段实施路线。V0.13 的重点不是直接交付真实 durable checkpoint store、resume token、crash recovery 或 production storage adapter，而是把已经稳定的 `CheckpointPersistenceDescriptor` 推进为“可表达 local export package manifest、可固定 artifact bundle 边界、可被 future durable store prototype 稳定消费”的下一层边界。

基线输入：

- [roadmap-v0.12.zh.md](./roadmap-v0.12.zh.md)（已完成基线）
- [issue-backlog-v0.12.zh.md](./issue-backlog-v0.12.zh.md)（已完成基线）
- [native-persistence-prototype-bootstrap-v0.12.zh.md](../design/native-persistence-prototype-bootstrap-v0.12.zh.md)
- [persistence-prototype-compatibility-v0.12.zh.md](../reference/persistence-prototype-compatibility-v0.12.zh.md)
- [native-consumer-matrix-v0.12.zh.md](../reference/native-consumer-matrix-v0.12.zh.md)
- [contributor-guide-v0.12.zh.md](../reference/contributor-guide-v0.12.zh.md)
- [checkpoint-prototype-compatibility-v0.11.zh.md](../reference/checkpoint-prototype-compatibility-v0.11.zh.md)
- [scheduler-prototype-compatibility-v0.10.zh.md](../reference/scheduler-prototype-compatibility-v0.10.zh.md)
- [cli-commands-v0.10.zh.md](../reference/cli-commands-v0.10.zh.md)

当前实现状态：

1. V0.12 已完成 persistence descriptor / review 的模型、validation、bootstrap、CLI/backend 输出、golden regression、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能稳定跑通 `package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint record -> persistence descriptor -> persistence review` 的本地 deterministic 路径。
3. 当前仓库仍未提供正式的 export package manifest、artifact bundle layout、manifest-level blocker 或 store-import-facing machine artifact 入口。
4. 当前 `PersistenceReviewSummary` 仍然只是 reviewer-facing projection，并不会承诺 durable store mutation plan、object path、resume token、recovery command 或 storage adapter payload。
5. 当前阶段仍明确停留在 local/offline persistence handoff，不进入真实 database / object storage、distributed recovery、host telemetry、provider connector、retention/GC 或 crash recovery runtime 领域。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.13 的交付目标是：

1. 冻结 future durable store prototype 的最小 export package 输入边界，使其能稳定消费 V0.12 `CheckpointPersistenceDescriptor`，而不必在 CLI、脚本或外部工具里私造 bundle layout。
2. 引入最小 `PersistenceExportManifest` 语义，稳定表达 export package identity、artifact bundle、source version chain、manifest readiness 与 store-import blocker。
3. 让 single-file、project、workspace 与 `--package` 路径都能生成 deterministic export-package-facing prototype 输出。
4. 冻结 export manifest / review 的 compatibility、consumer matrix 与 contributor guidance，明确 descriptor、manifest、review surface 与 future durable store adapter 的职责边界。
5. 保持 V0.13 仍然是 local/offline export package manifest prototype，而不是 production durable store / recovery 平台。

## 非目标

V0.13 仍然不直接承诺：

1. 真实 durable checkpoint store、数据库 schema、object storage layout、replication、queue durability、retention 或 garbage collection
2. 真实 resume token、crash recovery、distributed recovery daemon、worker restart、failover orchestration 或 launcher ABI
3. 真实 capability connector、provider SDK、secret、endpoint、tenant、region、deployment metadata 或 storage credential
4. host telemetry、wall clock metrics、machine id、provider payload、event ingestion、SIEM integration 或 operator console
5. 真正的 runtime resume executor、statement-level interpreter、retry/backoff、timeout、cancellation propagation 或 preemption

## 里程碑

### M0 V0.13 Scope、Export Package 与 Store Import Boundary 冻结

状态：

- [ ] Issue 01 冻结 V0.13 export package manifest scope 与 non-goals
- [ ] Issue 02 冻结 export manifest / artifact bundle layout 的最小模型与 store-import 边界
- [ ] Issue 03 冻结 persistence descriptor、export manifest 与 future durable store adapter 的分层关系

目标：

1. 明确 V0.13 做的是 local export package manifest prototype，而不是 durable store 或 production recovery。
2. 明确 export package manifest 必须建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence descriptor` 之上，而不是直接读取 review、trace、AST 或 host log。
3. 明确 persistence descriptor、export manifest、export review summary 与 future durable store adapter / recovery ABI 的职责拆分。

### M1 Export Manifest Model 与 Validation

状态：

- [ ] Issue 04 引入 persistence export manifest 数据模型
- [ ] Issue 05 增加 export manifest validation 与 direct regression
- [ ] Issue 06 增加 deterministic export manifest bootstrap

目标：

1. 让仓库可以稳定表达 export package identity、artifact bundle、manifest-ready 状态与 store-import blocker。
2. 让 export-package-facing failure 可以区分输入 artifact 错误、manifest model 错误与 prototype bootstrap 错误。
3. 让 export manifest bootstrap 只依赖 V0.12 已冻结 artifact，而不是 CLI 文本或私有脚本约定。

### M2 Export Package Output 与 Review Surface

状态：

- [ ] Issue 07 增加 export manifest 输出路径
- [ ] Issue 08 增加 export package review / import preview surface
- [ ] Issue 09 增加 single-file / project / workspace export package golden 回归

目标：

1. 让 contributor / store prototype consumer 可直接查看 export-package-facing 状态，而不必拼接多份 artifact。
2. 让 export package review 输出覆盖 export package identity、artifact bundle、store-import blocker、local/store boundary 与 next-step recommendation。
3. 让成功路径、partial 路径与 failure-path 都能稳定生成 export package prototype 输出。

### M3 Compatibility、Consumer Matrix 与 Store Adapter Guidance

状态：

- [ ] Issue 10 冻结 export manifest / review compatibility 契约
- [ ] Issue 11 更新 native consumer matrix、contributor guide 与 future store adapter boundary guidance

目标：

1. 让 export-package-facing artifact 的格式演进不依赖隐含约定。
2. 让 future durable store prototype / recovery explorer 清楚哪些字段可稳定依赖。
3. 把 export-package-facing artifact 纳入统一扩展模板，避免在 CLI 或外部脚本中重建私有 store import 状态机。

### M4 工程化收口

状态：

- [ ] Issue 12 建立 V0.13 export package prototype regression、CI 与 reference 文档闭环

目标：

1. 让 export manifest、compatibility、consumer matrix 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint -> persistence descriptor -> export manifest”的 V0.13 路径。

## 关键依赖顺序

1. 先完成 M0，再引入 export-package-facing 模型
2. 先稳定 persistence descriptor，再扩 export manifest
3. 先冻结 compatibility / consumer matrix，再承诺 export package prototype 的长期消费
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 export manifest 未冻结前直接实现私有 durable store / recovery 协议
2. 不要把真实 host log、provider payload、deployment telemetry、store URI、object path 或 credential 塞进 export manifest / review 输出
3. 不要让 store prototype 回退读取 persistence review、checkpoint review、scheduler review、trace、AST、raw source 或 host log 猜运行语义
4. 不要把 V0.13 的实现目标偷换成 production persistence / recovery 平台开发

## 当前状态

V0.12 已完成并成为 V0.13 的正式上游基线；当前 `Issue 01-12` 均未开始。建议先执行 `Issue 01-03`，冻结 export package manifest 的 scope、artifact bundle layout 与 layering，再进入 model / validation / bootstrap 实现。V0.13 当前建议闭环路径为 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceExportManifest -> PersistenceExportReviewSummary`，并继续明确不承诺真实 durable store、resume token、recovery daemon、host telemetry、provider payload、store URI 或 object path。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.13.zh.md](./issue-backlog-v0.13.zh.md)
