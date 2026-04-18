# AHFL Native Partial Session / Failure Bootstrap V0.9

本文冻结 AHFL V0.9 中 partial runtime session、failure-path execution journal，以及 replay / audit failure 消费的最小边界。它承接 V0.8 已完成的 success-path `RuntimeSession`、`ExecutionJournal`、`ReplayView` 与 `AuditReport`，目标是把仓库推进到“可稳定表达未完成/失败执行”的下一层 artifact 边界，但仍不进入生产 scheduler、persistence、checkpoint / resume 或 host telemetry 领域。

关联文档：

- [native-runtime-session-bootstrap-v0.7.zh.md](./native-runtime-session-bootstrap-v0.7.zh.md)
- [native-execution-journal-v0.7.zh.md](./native-execution-journal-v0.7.zh.md)
- [native-replay-audit-bootstrap-v0.8.zh.md](./native-replay-audit-bootstrap-v0.8.zh.md)
- [roadmap-v0.9.zh.md](../plan/roadmap-v0.9.zh.md)

## 目标

本文主要回答六个问题：

1. V0.9 当前到底要交付什么 failure-path / partial session 能力。
2. partial `RuntimeSession` 的最小状态集合是什么。
3. failure taxonomy 的第一落点在哪里。
4. `ExecutionJournal`、`ReplayView` 与 `AuditReport` 各自如何承接 failure-path。
5. richer scheduler prototype 可以稳定依赖哪条输入链路。
6. 哪些内容明确不属于当前阶段。

## 非目标

本文不定义：

1. 生产 runtime launcher、distributed worker orchestration 或 host daemon
2. persistence store、checkpoint / resume、crash recovery 或 durable queue
3. wall clock metrics、machine id、worker id、host log payload 或 provider request/response capture
4. tenant、region、secret、deployment、endpoint、connector SDK integration
5. 真正的并发调度、retry、timeout、backoff、cancellation propagation

## 当前稳定输入链

V0.9 当前建议只沿着下面这条链路扩展 failure-path：

```text
ExecutionPlan
  + DryRunRequest
  + CapabilityMockSet
  -> RuntimeSession (partial / failed)
  -> ExecutionJournal (failure-path events)
  -> ReplayView
  -> AuditReport
```

这表示：

1. failure-path 的第一落点是 `RuntimeSession`
2. step-wise failure progression 的第一落点是 `ExecutionJournal`
3. `ReplayView` 只负责把 session + journal 投影为 deterministic replay consistency 结果
4. `AuditReport` 只负责聚合 reviewer-facing / CI-facing summary
5. richer scheduler prototype 应沿着 `plan -> session -> journal -> replay` 前进，而不是回退扫描 AST、raw source、project descriptor、workspace descriptor 或 host log

## Partial Runtime Session 的最小模型

V0.9 当前冻结的 partial session 最小问题是：

1. workflow 是否完成、失败，还是停留在未完成的 partial 状态
2. 每个 node 是完成、失败、被阻断、已就绪但未执行，还是被跳过
3. failure / partial 结论来自输入错误、bootstrap 构造错误，还是 runtime result 本身

当前建议 `RuntimeSession` 至少向下面的状态集合演进：

### Workflow 顶层状态

1. `Completed`
   - 所有 node 都成功完成，沿用 V0.7 success-path 语义
2. `Failed`
   - 至少一个 node 已产生 terminal failure，workflow 不会再成功完成
3. `Partial`
   - workflow 没有完成，但当前 snapshot 仍保留未完成 / 未执行 / 被阻断 / 待解释状态

### Node 最小状态

1. `Blocked`
   - 当前仍不满足依赖或被上游状态阻断
2. `Ready`
   - 已满足依赖，但未进入执行
3. `Completed`
   - 成功完成
4. `Failed`
   - 节点已产生 terminal failure
5. `Skipped`
   - 节点因为 workflow terminal failure 或等价终止原因而明确不会执行

## Failure Summary 的最小边界

V0.9 当前建议给 workflow / node 引入“最小 failure summary”，但不引入真实 host payload。最小摘要应回答：

1. failure 属于哪一层
2. failure 属于哪一类
3. failure 归属哪个 node（若适用）
4. 当前 artifact 是否仍可稳定被下游消费

当前建议先冻结下面的 failure taxonomy：

### A. Input Artifact Error

表示输入本身不成立，因此不应产出 partial session / journal artifact：

1. execution plan 不合法
2. dry-run request 不合法
3. capability mock set 不合法
4. workflow 选择错误

这类错误应继续通过 builder diagnostics 返回，而不是伪造 failure-path artifact。

### B. Bootstrap Construction Error

表示仓库内部在把稳定输入投影为 session / journal / replay 时发现对象模型不成立，例如：

1. session validation 失败
2. journal event ordering 不成立
3. replay projection 前置条件不成立

这类错误也不应冒充 runtime result failure。它们属于 artifact construction / validation error。

### C. Runtime Result Failure

表示输入有效、artifact 可构建，但执行语义给出了失败或未完成结论。V0.9 当前最小集合建议包括：

1. `mock_missing`
   - node 所需 capability mock 无法解析到稳定结果
2. `node_failed`
   - node 在 deterministic runtime-adjacent bootstrap 中产生 terminal failure
3. `workflow_failed`
   - workflow 已形成稳定失败结论
4. `workflow_partial`
   - 当前 snapshot 不是成功完成态，但也不应被误标为构造错误

### D. Downstream Consumer Consistency Failure

表示上游 artifact 已经存在，但下游消费链路发现不一致：

1. `journal_consistency_error`
2. `replay_consistency_error`
3. `audit_consistency_error`

这些 failure 不属于 `RuntimeSession` 的 runtime result 本身，而属于 journal / replay / audit 的消费与校验层。

## Session / Journal / Replay / Audit 的职责拆分

V0.9 当前冻结的分层关系是：

1. `RuntimeSession`
   - final / partial state snapshot
   - 拥有 workflow / node 当前状态与最小 failure summary
2. `ExecutionJournal`
   - step-wise event sequence
   - 拥有 readiness / start / complete / fail progression
3. `ReplayView`
   - replay projection + consistency summary
   - 只解释 session / journal 是否能形成 deterministic progression
4. `AuditReport`
   - reviewer-facing aggregate summary
   - 只归档 plan / session / journal / replay 的结论，不重新创造私有 failure 状态机

因此：

1. 不允许先在 replay / audit 层捏造 failure taxonomy，再倒逼 session / journal 适配
2. 不允许 future scheduler prototype 直接从 trace / source / host log 倒推 failure 语义
3. 不允许 CLI 或外部脚本跳过 helper / validator 层重建私有 partial session 结论

## Failure-Path 对 Replay / Audit 的影响

V0.9 当前建议：

1. replay 必须区分“上游 runtime result failure”和“replay consistency failure”
2. audit 必须区分“输入 artifact 错误”“runtime result failure”“replay / journal consistency error”
3. replay / audit 可以新增 reviewer-facing failure summary，但不应成为 failure 的第一事实来源

换句话说：

1. `mock_missing` / `node_failed` / `workflow_failed` 先属于 session / journal
2. `journal_consistency_error` 先属于 journal validation / replay input validation
3. `replay_consistency_error` 先属于 replay validation

## Future Scheduler Prototype 当前应怎么消费

V0.9 richer scheduler prototype 当前只建议依赖：

1. `ExecutionPlan`
   - 提供 DAG / dependency / execution order
2. `RuntimeSession`
   - 提供 partial / failed / completed state snapshot
3. `ExecutionJournal`
   - 提供 deterministic event ordering
4. `ReplayView`
   - 提供 session / journal consistency projection

当前不建议 future scheduler prototype 直接依赖：

1. `DryRunTrace`
   - 它仍是 contributor-facing review artifact
2. AST / raw source / descriptor 文件
3. 真实 host log / provider payload

## 当前实现前的约束

Issue 04-09 开始编码前，后续实现必须遵守：

1. partial session / failure-path 的第一实现目标是稳定 artifact，不是生产 runtime 模拟器
2. builder diagnostics 与 runtime result failure 必须严格区分
3. session / journal / replay / audit 不得混入 host telemetry、deployment metadata 或 provider payload
4. 若 failure-path 需要改变现有 success-path status / event 语义，应通过 artifact versioning 明确迁移
5. CLI 输出路径只负责序列化稳定 artifact，不得在 backend JSON printer 中二次发明 failure 语义

## 当前状态

截至当前设计：

1. V0.8 已完成 success-path `plan -> session -> journal -> replay -> audit` 闭环
2. V0.9 M0 已冻结 failure-path / partial session 的 scope、taxonomy 与分层边界
3. 下一阶段实现应先进入 `RuntimeSession` partial model 与 bootstrap，而不是先做 replay / audit 表层输出
