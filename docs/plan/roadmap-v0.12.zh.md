# AHFL V0.12 Roadmap

本文给出 AHFL 在 V0.11 checkpoint prototype / checkpoint record 闭环完成之后的下一阶段实施路线。V0.12 的重点不是直接交付真实 durable checkpoint store、crash recovery 或 production resume runtime，而是把已经稳定的 `CheckpointRecord` 继续推进为“可表达 persistence-facing handoff、可形成 planned durable identity、可被 future persistence prototype 稳定消费”的下一层边界。

基线输入：

- [roadmap-v0.11.zh.md](./roadmap-v0.11.zh.md)（已完成基线）
- [issue-backlog-v0.11.zh.md](./issue-backlog-v0.11.zh.md)（已完成基线）
- [native-persistence-prototype-bootstrap-v0.12.zh.md](../design/native-persistence-prototype-bootstrap-v0.12.zh.md)
- [persistence-prototype-compatibility-v0.12.zh.md](../reference/persistence-prototype-compatibility-v0.12.zh.md)
- [native-checkpoint-prototype-bootstrap-v0.11.zh.md](../design/native-checkpoint-prototype-bootstrap-v0.11.zh.md)
- [checkpoint-prototype-compatibility-v0.11.zh.md](../reference/checkpoint-prototype-compatibility-v0.11.zh.md)
- [native-consumer-matrix-v0.11.zh.md](../reference/native-consumer-matrix-v0.11.zh.md)
- [contributor-guide-v0.11.zh.md](../reference/contributor-guide-v0.11.zh.md)
- [native-scheduler-prototype-bootstrap-v0.10.zh.md](../design/native-scheduler-prototype-bootstrap-v0.10.zh.md)
- [scheduler-prototype-compatibility-v0.10.zh.md](../reference/scheduler-prototype-compatibility-v0.10.zh.md)
- [cli-commands-v0.10.zh.md](../reference/cli-commands-v0.10.zh.md)

当前实现状态：

1. V0.11 已完成 checkpoint record / review 的模型、validation、bootstrap、CLI/backend 输出、golden regression、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能稳定跑通 `package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint record -> checkpoint review` 的本地 deterministic 路径。
3. 当前仓库仍未提供正式的 persistence-facing descriptor、planned durable checkpoint identity、export blocker 或 storage-facing machine artifact 入口。
4. 当前 `CheckpointReviewSummary` 仍然只是 contributor-facing projection，并不会承诺 durable store mutation plan、planned durable id、resume token 或 recovery launcher ABI。
5. 当前阶段仍明确停留在 local/offline checkpoint basis，不进入真实 durable checkpoint store、resume protocol、crash recovery、distributed scheduler、真实 connector、host telemetry 或 recovery daemon 领域。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.12 的交付目标是：

1. 冻结 future persistence prototype 的最小输入 / 输出边界，使其能稳定消费 `CheckpointRecord`，而不必在 CLI、脚本或外部工具里私造 durable export 语义。
2. 引入最小 `CheckpointPersistenceDescriptor` 语义，稳定表达 planned durable checkpoint identity、exportable prefix、persistence blocker 与 local/store boundary。
3. 让 single-file、project、workspace 与 `--package` 路径都能生成 deterministic persistence-facing prototype 输出。
4. 冻结 persistence-facing compatibility、consumer matrix 与 contributor guidance，明确 `CheckpointRecord`、persistence descriptor、review surface 与 future recovery protocol 的职责边界。
5. 保持 V0.12 仍然是 local/offline persistence handoff ABI prototype，而不是 production persistence / recovery 平台。

## 非目标

V0.12 仍然不直接承诺：

1. 真实 durable checkpoint store、数据库 schema、object storage layout、replication、queue durability 或 garbage collection
2. 真实 resume protocol、crash recovery、replay recovery daemon、distributed worker restart 或 failover orchestration
3. 真实 capability connector、provider SDK、secret、endpoint、tenant、region 或 deployment metadata
4. host telemetry、wall clock metrics、machine id、provider payload、event ingestion 或 SIEM integration
5. 真正的 runtime resume executor、statement-level interpreter、retry/backoff、timeout、cancellation propagation 或 preemption

## 里程碑

### M0 V0.12 Scope、Persistence Descriptor 与 Store Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.12 persistence handoff scope 与 non-goals
- [x] Issue 02 冻结 persistence descriptor / planned durable identity 的最小模型与 store-adjacent 边界
- [x] Issue 03 冻结 checkpoint artifact、persistence descriptor 与 future durable store / recovery protocol 的分层关系

目标：

1. 明确 V0.12 做的是 persistence-facing local prototype，而不是 durable store 或 production recovery。
2. 明确 persistence 语义必须建立在 `plan -> session -> journal -> replay -> snapshot -> checkpoint` 之上，而不是直接读取 review、trace、AST 或 host log。
3. 明确 persistence descriptor、persistence review summary 与 future durable store / recovery ABI 的职责拆分。

### M1 Persistence Descriptor Model 与 Validation

状态：

- [x] Issue 04 引入 checkpoint persistence descriptor 数据模型
- [x] Issue 05 增加 persistence descriptor validation 与 direct regression
- [x] Issue 06 增加 deterministic persistence descriptor bootstrap

目标：

1. 让仓库可以稳定表达 planned durable checkpoint identity、exportable prefix、descriptor-ready 状态与 persistence blocker。
2. 让 persistence-facing failure 可以区分输入 artifact 错误、descriptor model 错误与 prototype bootstrap 错误。
3. 让 persistence descriptor bootstrap 只依赖 V0.11 已冻结 artifact，而不是 CLI 文本或私有脚本约定。

### M2 Persistence Prototype Output 与 Review Surface

状态：

- [x] Issue 07 增加 persistence descriptor 输出路径
- [x] Issue 08 增加 persistence review / export preview surface
- [x] Issue 09 增加 single-file / project / workspace persistence golden 回归

目标：

1. 让 contributor / persistence prototype consumer 可直接查看 persistence-facing 状态，而不必拼接多份 artifact。
2. 让 persistence-facing review 输出覆盖 planned durable identity、exportable prefix、persistence blocker 与 local/store boundary summary。
3. 让成功路径、partial 路径与 failure-path 都能稳定生成 persistence prototype 输出。

### M3 Compatibility、Consumer Matrix 与 Store Boundary Guidance

状态：

- [x] Issue 10 冻结 persistence descriptor / review compatibility 契约
- [x] Issue 11 更新 native consumer matrix、contributor guide 与 future persistence boundary guidance

目标：

1. 让 persistence-facing artifact 的格式演进不依赖隐含约定。
2. 让 future persistence prototype / recovery explorer 清楚哪些字段可稳定依赖。
3. 把 persistence-facing artifact 纳入统一扩展模板，避免在 CLI 或外部脚本中重建私有 persistence 状态机。

### M4 工程化收口

状态：

- [x] Issue 12 建立 V0.12 persistence prototype regression、CI 与 reference 文档闭环

目标：

1. 让 persistence descriptor、compatibility、consumer matrix 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint -> persistence descriptor”的 V0.12 路径。

## 关键依赖顺序

1. 先完成 M0，再引入 persistence-facing 模型
2. 先稳定 checkpoint record，再扩 persistence descriptor
3. 先冻结 compatibility / consumer matrix，再承诺 persistence prototype 的长期消费
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 persistence-facing artifact 未冻结前直接实现私有 durable store / recovery 协议
2. 不要把真实 host log、provider payload、deployment telemetry 或 store metadata 塞进 persistence descriptor / review 输出
3. 不要让 persistence prototype 回退读取 checkpoint review、scheduler review、trace、AST、raw source 或 host log 猜运行语义
4. 不要把 V0.12 的实现目标偷换成 production persistence / recovery 平台开发

## 当前状态

V0.11 已完成并成为 V0.12 的正式上游基线；当前 `Issue 01-12` 已全部完成：仓库已经具备 `CheckpointPersistenceDescriptor` 与 `PersistenceReviewSummary` 的 model、validation、bootstrap、backend / CLI emission、single-file / project / workspace golden、compatibility contract、consumer matrix、contributor guide 与 CI 显式回归切片。V0.12 当前闭环路径已稳定为 `ExecutionPlan -> RuntimeSession -> ExecutionJournal -> ReplayView -> SchedulerSnapshot -> CheckpointRecord -> CheckpointPersistenceDescriptor -> PersistenceReviewSummary`，并继续明确不承诺真实 durable store、resume token、recovery daemon、host telemetry 或 provider payload。建议后续进入下一版本规划前，先 commit / push 当前 V0.12 闭环。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.12.zh.md](./issue-backlog-v0.12.zh.md)
