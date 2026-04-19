# AHFL Native Consumer Matrix V0.12

本文冻结 AHFL V0.12 中 persistence-facing consumer 的当前矩阵，重点覆盖 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`CheckpointRecord`、`CheckpointPersistenceDescriptor`、`PersistenceReviewSummary`、`AuditReport` 与 `DryRunTrace` 各自稳定依赖什么，以及 future durable store / recovery explorer 当前应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.11.zh.md](./native-consumer-matrix-v0.11.zh.md)
- [persistence-prototype-compatibility-v0.12.zh.md](./persistence-prototype-compatibility-v0.12.zh.md)
- [native-persistence-prototype-bootstrap-v0.12.zh.md](../design/native-persistence-prototype-bootstrap-v0.12.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前可稳定依赖的 persistence-facing 边界 | 当前明确不承诺 |
|----------|----------|--------------|------------------------------------------|----------------|
| Execution Plan Consumer | planner / runner 的正式 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | planning DAG、dependency、binding、return contract | persistence blocker、durable store ABI |
| Runtime Session Consumer | workflow / node 当前状态快照输入 | `emit-runtime-session`、`runtime_session::RuntimeSession`、`validate_runtime_session(...)` | `workflow_status = Completed / Failed / Partial`、node 当前状态、workflow failure summary | exportable prefix、planned durable identity |
| Execution Journal Consumer | event ordering / failure progression 输入 | `emit-execution-journal`、`execution_journal::ExecutionJournal`、`validate_execution_journal(...)` | deterministic event sequence、failed-event family、prefix ordering | reviewer-facing export preview、resume token |
| Replay View Consumer | consistency / progression 投影视角输入 | `emit-replay-view`、`replay_view::ReplayView`、`validate_replay_view(...)` | replay consistency、node progression summary、workflow failure projection | persistence state 第一事实来源、durable recovery ABI |
| Scheduler Snapshot Consumer | scheduler-facing local state 的第一正式输入 | `emit-scheduler-snapshot`、`scheduler_snapshot::SchedulerSnapshot`、`validate_scheduler_snapshot(...)` | ready set、blocked frontier、executed prefix、checkpoint-friendly local state | planned durable identity、store mutation plan |
| Checkpoint Record Consumer | checkpoint-facing machine artifact 输入 | `emit-checkpoint-record`、`checkpoint_record::CheckpointRecord`、`validate_checkpoint_record(...)` | persistable prefix、checkpoint identity、resume basis / blocker、local-only boundary | durable checkpoint id、resume token、store metadata |
| Persistence Descriptor Consumer | persistence-facing machine artifact 输入 | `emit-persistence-descriptor`、`persistence_descriptor::CheckpointPersistenceDescriptor`、`validate_persistence_descriptor(...)` | planned durable identity、exportable prefix、persistence basis / blocker、source version chain | durable store mutation ABI、store URI、operator payload |
| Persistence Review Consumer | reviewer-facing persistence summary 输入 | `emit-persistence-review`、`persistence_descriptor::PersistenceReviewSummary`、`validate_persistence_review_summary(...)` | store boundary、export preview、planned durable identity / blocker / next-step summary | 独立 durable store / recovery 状态机、host-side payload |
| Audit Report Consumer | review / CI / archive 的 aggregate 归档输入 | `emit-audit-report`、`audit_report::AuditReport`、`validate_audit_report(...)` | runtime / partial / failure aggregate conclusion、journal failed-event 计数、consistency summary | persistence state 第一事实来源、store mutation ABI |
| Dry-Run Trace Consumer | contributor-facing readable trace / local debug | `emit-dry-run-trace`、`dry_run::DryRunTrace` | deterministic mock invocation / local pending 视图 | 正式 persistence / recovery ABI |

## 当前共享入口

V0.12 当前 persistence-facing consumer 共享入口是：

1. `handoff::build_execution_plan(...)`
2. `runtime_session::build_runtime_session(...)`
3. `runtime_session::validate_runtime_session(...)`
4. `execution_journal::build_execution_journal(...)`
5. `execution_journal::validate_execution_journal(...)`
6. `replay_view::build_replay_view(...)`
7. `replay_view::validate_replay_view(...)`
8. `scheduler_snapshot::build_scheduler_snapshot(...)`
9. `scheduler_snapshot::validate_scheduler_snapshot(...)`
10. `checkpoint_record::build_checkpoint_record(...)`
11. `checkpoint_record::validate_checkpoint_record(...)`
12. `persistence_descriptor::build_persistence_descriptor(...)`
13. `persistence_descriptor::validate_persistence_descriptor(...)`
14. `persistence_descriptor::build_persistence_review_summary(...)`
15. `persistence_descriptor::validate_persistence_review_summary(...)`
16. `ahflc emit-execution-plan`
17. `ahflc emit-runtime-session`
18. `ahflc emit-execution-journal`
19. `ahflc emit-replay-view`
20. `ahflc emit-scheduler-snapshot`
21. `ahflc emit-checkpoint-record`
22. `ahflc emit-persistence-descriptor`
23. `ahflc emit-persistence-review`

这表示：

1. future durable store / recovery explorer 不应自己重建 session / journal / replay / snapshot / checkpoint / persistence 语义
2. reviewer tooling 不应在 CLI / golden / 外部脚本中自行拼接 export preview 或 store boundary 结论
3. persistence-facing 扩展应沿着 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> persistence-review` 层次前进，而不是跳层

## 当前扩展模板

当前建议复用下面十五层模板：

1. authoring 输入层
   - `ahfl.package.json`
2. handoff 共享模型层
   - `handoff::Package`
3. planning artifact 层
   - `handoff::ExecutionPlan`
4. deterministic input 层
   - `CapabilityMockSet`
   - `DryRunRequest`
5. state snapshot 层
   - `runtime_session::RuntimeSession`
6. event sequence 层
   - `execution_journal::ExecutionJournal`
7. replay projection 层
   - `replay_view::ReplayView`
8. scheduler-facing local state 层
   - `scheduler_snapshot::SchedulerSnapshot`
9. checkpoint-facing machine state 层
   - `checkpoint_record::CheckpointRecord`
10. persistence-facing machine state 层
   - `persistence_descriptor::CheckpointPersistenceDescriptor`
11. reviewer-facing persistence summary 层
   - `persistence_descriptor::PersistenceReviewSummary`
12. aggregate audit 层
   - `audit_report::AuditReport`
13. direct helper / validator 层
   - `build_persistence_descriptor(...)`
   - `validate_persistence_descriptor(...)`
   - `build_persistence_review_summary(...)`
   - `validate_persistence_review_summary(...)`
14. golden / regression 层
   - `tests/persistence/`
   - `v0.12-persistence-golden`
15. backend / CLI 输出层
   - `emit-persistence-descriptor`
   - `emit-persistence-review`

新增 persistence-facing consumer prototype 时，不应跳过第 2-14 层直接在 CLI、JSON parser 或外部脚本内实现私有语义。

## 推荐消费顺序

当前推荐依赖顺序是：

1. 若要做 planning / dependency reasoning
   - 优先消费 `ExecutionPlan`
2. 若要做 final state / failure snapshot reasoning
   - 优先消费 `RuntimeSession`
3. 若要做 event ordering / failure progression / prefix execution reasoning
   - 优先消费 `ExecutionJournal`
4. 若要做 consistency / replay verification
   - 再消费 `ReplayView`
5. 若要做 ready-set / blocked-frontier / checkpoint-friendly local state
   - 再消费 `SchedulerSnapshot`
6. 若要做 machine-facing persistable basis / resume blocker
   - 再消费 `CheckpointRecord`
7. 若要做 machine-facing planned durable identity / export blocker
   - 再消费 `CheckpointPersistenceDescriptor`
8. 若要做 reviewer-facing export preview / store boundary / next-step summary
   - 最后消费 `PersistenceReviewSummary`

换句话说：

- `CheckpointRecord` 适合回答“当前 checkpoint 能否成为 future persistence 的基础”；
- `CheckpointPersistenceDescriptor` 适合回答“当前是否能形成 stable persistence handoff，以及 planned durable identity 是什么”；
- `PersistenceReviewSummary` 适合回答“reviewer 现在应该如何理解 export preview / store boundary / next step”；
- `AuditReport` 仍适合回答“这次运行应如何被 review / CI / archive 归档”；
- `DryRunTrace` 仍适合“看懂本地 mock dry-run 做了什么”，但不是正式 persistence ABI。

## Future Durable Store / Recovery Explorer 应落在哪里

当前 future durable store / recovery explorer 的推荐依赖顺序是：

1. 先消费 `ExecutionPlan`
2. 再消费 `RuntimeSession`
3. 再消费 `ExecutionJournal`
4. 若需要 consistency 视角
   - 再消费 `ReplayView`
5. 若需要 scheduler-facing local state
   - 再消费 `SchedulerSnapshot`
6. 若需要 machine-facing checkpoint basis
   - 再消费 `CheckpointRecord`
7. 若需要 machine-facing persistence handoff
   - 再消费 `CheckpointPersistenceDescriptor`
8. 若需要 reviewer-facing guidance
   - 最后消费 `PersistenceReviewSummary`

这意味着：

1. future durable store prototype 不应直接从 `ReplayView` / `AuditReport` 倒推 persistence state
2. future durable store prototype 不应直接从 `DryRunTrace` 推导 export blocker
3. `PersistenceReviewSummary` 是 projection，不是 durable store / recovery protocol
4. 若以后需要 durable checkpoint id / resume token / store mutation ABI，应先扩 persistence compatibility contract，而不是把字段塞进 review 输出

## 当前反模式

当前明确不建议：

1. 跳过 `CheckpointPersistenceDescriptor`，直接在 `emit-persistence-review` / 外部脚本里私造 export preview state machine
2. 把 `DryRunTrace` 当 persistence prototype / durable recovery explorer 的正式第一输入
3. 跳过 `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` / `CheckpointRecord`，直接从 AST、source、trace 重建 planned durable identity / exportable prefix / blocker
4. 在 persistence descriptor / review 中塞入 durable checkpoint id、resume token、store URI、host telemetry、provider payload
5. 在未更新 compatibility / matrix / contributor docs / golden / labels 的情况下静默扩张 persistence-facing 稳定边界

## 当前状态

截至当前实现：

1. 当前推荐消费顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> persistence -> persistence-review`
2. `CheckpointPersistenceDescriptor` 已成为 planned durable identity / export blocker 的第一正式事实来源
3. `PersistenceReviewSummary` 已成为 reviewer-facing store boundary / export preview 的正式 projection
4. `v0.12-persistence-golden` 已作为 V0.12 consumer matrix 的最小 golden 回归锚点
