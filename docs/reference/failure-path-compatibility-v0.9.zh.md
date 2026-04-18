# AHFL Failure-Path Compatibility And Layering V0.9

本文冻结 AHFL V0.9 在 partial session、failure-path journal、replay 与 audit 演进上的当前兼容与分层约束，作为 Issue 10 的 umbrella compatibility contract。逐 artifact 的正式版本入口分别由 V0.9 runtime session、execution journal、replay view 与 audit report compatibility 文档冻结。

关联文档：

- [runtime-session-compatibility-v0.9.zh.md](./runtime-session-compatibility-v0.9.zh.md)
- [execution-journal-compatibility-v0.9.zh.md](./execution-journal-compatibility-v0.9.zh.md)
- [replay-view-compatibility-v0.9.zh.md](./replay-view-compatibility-v0.9.zh.md)
- [audit-report-compatibility-v0.9.zh.md](./audit-report-compatibility-v0.9.zh.md)
- [native-partial-session-failure-bootstrap-v0.9.zh.md](../design/native-partial-session-failure-bootstrap-v0.9.zh.md)
- [issue-backlog-v0.9.zh.md](../plan/issue-backlog-v0.9.zh.md)

## 目标

本文主要回答五个问题：

1. V0.8 success-path artifact 与 V0.9 failure-path artifact 的兼容层次关系是什么。
2. partial session / failure-path journal 的第一稳定版本入口应该怎么理解。
3. 哪些变化是兼容扩展，哪些变化必须 bump version。
4. replay / audit 应如何消费 failure-path，而不篡改上游语义。
5. future consumer 的最小迁移规则是什么。

## 当前基线

V0.8 历史 success-path baseline 是：

1. `ahfl.runtime-session.v1`
2. `ahfl.execution-journal.v1`
3. `ahfl.replay-view.v1`
4. `ahfl.audit-report.v1`

这些版本只稳定承诺 success-path 语义，继续作为 historical baseline 存在。

V0.9 当前稳定 failure-path baseline 是：

1. `ahfl.runtime-session.v2`
2. `ahfl.execution-journal.v2`
3. `ahfl.replay-view.v2`
4. `ahfl.audit-report.v2`

因此 V0.9 的当前兼容结论是：

1. 旧版 `v1` artifact 仍表示 success-path baseline
2. 当前 `v2` artifact 正式承诺 partial / failed / failure-aware replay-audit 语义
3. 后续实现若只是新增可忽略辅助字段，可视情况保持当前 `v2` 版本
4. 后续实现若改变顶层状态集合、事件种类或 failure 判定语义，必须继续 bump 对应 artifact version

## 当前冻结的分层契约

当前 failure-path 分层契约是：

1. `RuntimeSession`
   - partial / failed state snapshot 的第一事实来源
2. `ExecutionJournal`
   - failure progression / terminal event 的第一事实来源
3. `ReplayView`
   - consistency projection，不是 failure 第一事实来源
4. `AuditReport`
   - aggregate review summary，不是 failure 第一事实来源

这意味着：

1. replay / audit 不得在没有 session / journal failure 语义的前提下自创私有 failure taxonomy
2. CLI / golden / CI 不得跳过 helper / validator 层自己补齐 failure 结论
3. future scheduler prototype 不得回退依赖 AST、trace、host log 或 provider payload

## 什么算兼容扩展

在不修改对应 `format_version` 的前提下，当前允许的兼容扩展包括：

1. 新增可选 reviewer-facing summary 字段，且旧字段含义不变
2. 在 object 中新增旧 consumer 可忽略的辅助 metadata
3. 为当前 success-path / failure-path 增补更严格文档说明，而不改变旧字段语义
4. 在不改变已有状态 / 事件语义的前提下，新增可选 consistency summary

这些兼容扩展必须同时满足：

1. 旧字段仍存在
2. 旧字段语义不变
3. 旧 consumer 可以忽略未知字段后继续安全工作
4. 文档、golden 与 regression 已同步说明新增字段可忽略

## 什么算 breaking change

出现以下任一情况时，必须 bump 对应 artifact 的 `format_version`：

1. 新增 workflow / node status，导致旧状态集合语义被扩张
2. 新增 execution journal event kind，导致旧 event family 不再完整
3. 改变 `workflow_status`、`final_status`、`replay_status`、audit 总结状态的含义
4. 改变 replay / audit 对 failure consistency 的判定语义
5. 让旧 consumer 无法安全忽略 failure-path 新内容

当前 V0.9 最可能触发 version bump 的变化包括：

1. `RuntimeSession` 从只支持 `Completed` workflow 扩展到 `Failed` / `Partial`
2. `RuntimeSessionNode` 从只支持 `Blocked / Ready / Completed` 扩展到 `Failed / Skipped`
3. `ExecutionJournalEventKind` 引入 `node_failed`、`workflow_failed`、`mock_missing` 或等价 failure 事件
4. `ReplayView` 的 `replay_status` 从 success-path-only 语义扩展到正式 failure-path 语义
5. `AuditReport` 的最终结论从 success aggregate 扩展到正式 failure aggregate

## 当前迁移原则

future consumer 目前应采用下列迁移原则：

1. 若 consumer 只支持 V0.8 success-path
   - 明确只接受 `v1` artifact
2. 若 consumer 需要 V0.9 failure-path
   - 显式检查对应 artifact 的 `v2` 版本入口
3. 不允许通过“看到未知 status / event string 也继续猜测语义”的方式伪兼容
4. 不允许通过 AST / trace / host log 回填新版本缺失的 failure 语义

换句话说：

1. success-path 兼容不等于 failure-path 自动兼容
2. 新增 failure string / event kind 通常应视为强语义扩展，而不是普通可忽略字段

## Session / Journal / Replay / Audit 的依赖方向

当前冻结的依赖方向是：

```text
ExecutionPlan
  -> RuntimeSession
  -> ExecutionJournal
  -> ReplayView
  -> AuditReport
```

当前明确禁止：

1. `ReplayView` 跳过 `ExecutionJournal` 直接从 plan / trace 重建状态
2. `AuditReport` 跳过 `ReplayView` / `ExecutionJournal` 自行实现 failure 状态机
3. scheduler prototype 从 package / source / trace 私自合成 partial session

## 当前稳定性承诺

Issue 04-09 开始实现后，至少要继续满足：

1. builder diagnostics 与 runtime result failure 分离
2. session / journal / replay / audit 的 failure 分层不反转
3. failure-path 不引入 host telemetry、provider payload、deployment metadata
4. 每个 artifact 只有一个 `format_version` 入口
5. 任何 breaking change 都必须同步更新 design/reference/golden/regression

## 当前状态

截至当前设计：

1. M0 已明确 partial session 与 failure-path 不是对 V0.8 success-path 的静默字段补丁
2. Issue 04-09 已把 failure-path 正式落入 session / journal / replay / audit 四类 artifact
3. Issue 10 已将四类 artifact 的当前稳定入口冻结为 `v2`
4. 后续若扩张 failure taxonomy、event family、replay status 或 audit conclusion，必须同步更新 code / docs / golden / tests
