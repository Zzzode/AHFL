# AHFL Native Consumer Matrix V0.10

本文冻结 AHFL V0.10 中 scheduler-facing consumer 的当前矩阵，重点覆盖 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`SchedulerDecisionSummary`、`AuditReport` 与 `DryRunTrace` 各自稳定依赖什么，以及 future scheduler prototype / review tooling / checkpoint-friendly explorer 应落在哪一层。

关联文档：

- [native-consumer-matrix-v0.9.zh.md](./native-consumer-matrix-v0.9.zh.md)
- [runtime-session-compatibility-v0.9.zh.md](./runtime-session-compatibility-v0.9.zh.md)
- [execution-journal-compatibility-v0.9.zh.md](./execution-journal-compatibility-v0.9.zh.md)
- [replay-view-compatibility-v0.9.zh.md](./replay-view-compatibility-v0.9.zh.md)
- [audit-report-compatibility-v0.9.zh.md](./audit-report-compatibility-v0.9.zh.md)
- [failure-path-compatibility-v0.9.zh.md](./failure-path-compatibility-v0.9.zh.md)
- [scheduler-prototype-compatibility-v0.10.zh.md](./scheduler-prototype-compatibility-v0.10.zh.md)
- [native-scheduler-prototype-bootstrap-v0.10.zh.md](../design/native-scheduler-prototype-bootstrap-v0.10.zh.md)

## 当前矩阵

| Consumer | 主要用途 | 当前稳定输入 | 当前可稳定依赖的 scheduler-facing 边界 | 当前明确不承诺 |
|----------|----------|--------------|----------------------------------------|----------------|
| Execution Plan Consumer | planner / runner 的正式 planning artifact 输入 | `emit-execution-plan`、`handoff::ExecutionPlan`、`validate_execution_plan(...)` | planning DAG、dependency、binding、return contract | mock 输入、真实 runtime host、checkpoint 语义 |
| Runtime Session Consumer | workflow / node 当前状态快照输入 | `emit-runtime-session`、`runtime_session::RuntimeSession`、`validate_runtime_session(...)` | `workflow_status = Completed / Failed / Partial`、node 当前状态、workflow failure summary | ready-set / blocked frontier、checkpoint ABI |
| Execution Journal Consumer | event ordering / failure progression 输入 | `emit-execution-journal`、`execution_journal::ExecutionJournal`、`validate_execution_journal(...)` | deterministic event sequence、failed-event family、prefix ordering | ready-set projection、resume token |
| Replay View Consumer | consistency / progression 投影视角输入 | `emit-replay-view`、`replay_view::ReplayView`、`validate_replay_view(...)` | replay consistency、node progression summary、workflow failure projection | scheduler state 第一事实来源、durable checkpoint ABI |
| Scheduler Snapshot Consumer | scheduler-facing local state 的第一正式输入 | `emit-scheduler-snapshot`、`scheduler_snapshot::SchedulerSnapshot`、`validate_scheduler_snapshot(...)` | ready set、blocked frontier、executed prefix、next candidate、checkpoint-friendly local state | durable checkpoint id、resume token、host telemetry |
| Scheduler Review Consumer | reviewer-facing scheduler summary 输入 | `emit-scheduler-review`、`scheduler_snapshot::SchedulerDecisionSummary`、`validate_scheduler_decision_summary(...)` | terminal reason、next action、next-step recommendation、review-ready ready/blocked/prefix summary | 独立状态机、host-side推理链、persistence mutation plan |
| Audit Report Consumer | review / CI / archive 的 aggregate 归档输入 | `emit-audit-report`、`audit_report::AuditReport`、`validate_audit_report(...)` | runtime / partial / failure aggregate conclusion、journal failed-event 计数、consistency summary | scheduler ready-set 第一事实来源、checkpoint ABI |
| Dry-Run Trace Consumer | contributor-facing readable trace / local debug | `emit-dry-run-trace`、`dry_run::DryRunTrace` | deterministic mock invocation / local pending 视图 | 正式 scheduler failure ABI、长期 replay ABI |

## 当前共享入口

V0.10 当前 scheduler-facing consumer 共享入口是：

1. `handoff::build_execution_plan(...)`
2. `runtime_session::build_runtime_session(...)`
3. `runtime_session::validate_runtime_session(...)`
4. `execution_journal::build_execution_journal(...)`
5. `execution_journal::validate_execution_journal(...)`
6. `replay_view::build_replay_view(...)`
7. `replay_view::validate_replay_view(...)`
8. `scheduler_snapshot::build_scheduler_snapshot(...)`
9. `scheduler_snapshot::validate_scheduler_snapshot(...)`
10. `scheduler_snapshot::build_scheduler_decision_summary(...)`
11. `scheduler_snapshot::validate_scheduler_decision_summary(...)`
12. `audit_report::build_audit_report(...)`
13. `ahflc emit-execution-plan`
14. `ahflc emit-runtime-session`
15. `ahflc emit-execution-journal`
16. `ahflc emit-replay-view`
17. `ahflc emit-scheduler-snapshot`
18. `ahflc emit-scheduler-review`
19. `ahflc emit-audit-report`

这表示：

1. future scheduler prototype / checkpoint-friendly explorer 不应自己重建 session / journal / replay / snapshot 语义
2. reviewer tooling 不应在 CLI / golden / 外部脚本中自行拼接 next-step 结论
3. scheduler-facing 扩展应沿着 `plan -> session -> journal -> replay -> snapshot -> review` 层次前进，而不是跳层

## 当前扩展模板

当前建议复用下面十二层模板：

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
9. reviewer-facing scheduler summary 层
   - `scheduler_snapshot::SchedulerDecisionSummary`
10. aggregate audit 层
   - `audit_report::AuditReport`
11. direct helper / validator 层
   - `build_scheduler_snapshot(...)`
   - `validate_scheduler_snapshot(...)`
   - `build_scheduler_decision_summary(...)`
   - `validate_scheduler_decision_summary(...)`
12. backend / CLI 输出层
   - `emit-scheduler-snapshot`
   - `emit-scheduler-review`

新增 scheduler-facing consumer prototype 时，不应跳过第 2-11 层直接在 CLI、JSON parser 或外部脚本内实现私有语义。

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
6. 若要做 reviewer-facing next-step / terminal reason / readable summary
   - 最后消费 `SchedulerDecisionSummary`

换句话说：

- `SchedulerSnapshot` 适合回答“当前 scheduler 可见的 ready / blocked frontier 是什么”；
- `SchedulerDecisionSummary` 适合回答“reviewer 现在应该如何理解下一步调度动作”；
- `AuditReport` 仍适合回答“这次运行应如何被 review / CI / archive 归档”；
- `DryRunTrace` 仍适合“看懂本地 mock dry-run 做了什么”，但不是正式 scheduler ABI。

## Future Scheduler Prototype / Persistence Explorer 应落在哪里

当前 checkpoint-friendly / future persistence explorer 的推荐依赖顺序是：

1. 先消费 `ExecutionPlan`
   - 获取 DAG、dependency、binding 与稳定 execution order
2. 再消费 `RuntimeSession`
   - 获取 workflow / node 当前状态
3. 再消费 `ExecutionJournal`
   - 获取 deterministic event ordering 与 failure progression
4. 若需要 consistency 视角
   - 再消费 `ReplayView`
5. 若需要 scheduler-facing local state
   - 再消费 `SchedulerSnapshot`
6. 若需要 reviewer-facing readable summary
   - 最后消费 `SchedulerDecisionSummary`

这意味着：

1. future persistence explorer 不应直接从 `ReplayView` / `AuditReport` 倒推 checkpoint state
2. future persistence explorer 不应直接从 `DryRunTrace` 推导 runtime terminal failure
3. `SchedulerDecisionSummary` 是 projection，不是 checkpoint ABI
4. 若以后需要 durable checkpoint id / resume token，应先扩 `SchedulerSnapshot` compatibility contract，而不是把字段塞进 review 输出

## 当前反模式

当前明确不建议：

1. 把 `DryRunTrace` 当 future scheduler / persistence explorer 的唯一正式输入
2. 跳过 `RuntimeSession` / `ExecutionJournal`，直接从 trace、AST 或 source 文本重建 ready-set / blocked-reason
3. 跳过 `SchedulerSnapshot`，在 review / CLI / 外部脚本里私造 next-step state machine
4. 在 `SchedulerDecisionSummary` 中塞入 durable checkpoint id、resume token、host telemetry、provider payload
5. 在未更新 compatibility / matrix / contributor docs / golden / labels 的情况下静默扩张 scheduler-facing 稳定边界

## 当前状态

截至当前实现：

1. 当前推荐消费顺序已正式冻结为 `plan -> session -> journal -> replay -> snapshot -> review`
2. `SchedulerSnapshot` 已成为 ready set / blocked frontier / executed prefix 的第一正式事实来源
3. `SchedulerDecisionSummary` 已成为 reviewer-facing next-step / terminal reason 的正式 projection
4. `v0.10-scheduler-compatibility`、`v0.10-scheduler-golden` 与 `v0.10-scheduler-regression` 已作为 V0.10 consumer matrix 的最小回归锚点
