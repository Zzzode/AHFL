# AHFL Native Replay / Audit Bootstrap V0.8

本文冻结 AHFL V0.8 中 replay consumer 与 audit consumer 的最小边界。它承接 V0.7 的 `RuntimeSession` 与 `ExecutionJournal`，目标是让仓库内首次形成“可重放验证的状态投影”和“可供人工/CI 审查的稳定审计视图”，但仍不定义生产 runtime、真实 connector 或持久化恢复系统。

关联文档：

- [native-runtime-session-bootstrap-v0.7.zh.md](./native-runtime-session-bootstrap-v0.7.zh.md)
- [native-execution-journal-v0.7.zh.md](./native-execution-journal-v0.7.zh.md)
- [roadmap-v0.8.zh.md](../plan/roadmap-v0.8.zh.md)

## 目标

本文主要回答五个问题：

1. V0.8 replay consumer 当前允许消费哪些输入。
2. replay 结果应冻结哪些最小状态投影。
3. audit consumer 应暴露哪些稳定审查字段。
4. failure-path 扩展应如何与 V0.7 journal 兼容演进。
5. replay / audit 与 future scheduler / persistence / production runtime 的职责如何拆分。

## 非目标

本文不定义：

1. 真实生产 scheduler、worker host 或 distributed executor
2. wall clock timestamp、CPU、memory、network 等运行指标
3. persistence store、checkpoint / resume、crash recovery
4. tenant、region、secret、deployment、connector endpoint
5. 真正的 provider request / response payload 回放

## 输入边界

V0.8 replay / audit 当前建议只消费以下稳定输入：

```text
ExecutionPlan
  + RuntimeSession
  + ExecutionJournal
  + DryRunTrace
  -> Replay View
  -> Audit Report
```

其中：

1. `ExecutionPlan`
   - 提供 workflow DAG、binding、dependency 与 execution order 的规划语义
2. `RuntimeSession`
   - 提供 workflow / node 的最终状态快照与 mock usage 摘要
3. `ExecutionJournal`
   - 提供 step-wise runtime event sequence
4. `DryRunTrace`
   - 提供 contributor-facing review summary，供 audit 侧引用

replay / audit consumer 不应回退消费 AST、raw source、`ahfl.project.json`、`ahfl.workspace.json` 或真实 runtime host log。

## Replay View 的最小边界

当前建议 `ReplayView` 至少回答三个问题：

1. journal 中每个 event 是否能映射到稳定的 workflow / node state progression。
2. session 最终状态是否与 replay 出来的 node lifecycle 一致。
3. execution order、dependency satisfaction 与 mock usage 是否存在 consumer-facing 不一致。

因此最小 replay 结果建议至少包含：

1. 顶层 `format_version`
2. source artifact identity（plan / session / journal）
3. workflow canonical name
4. deterministic replay status
5. per-node lifecycle projection
6. consistency check summary
7. replay diagnostics（若失败）

当前不承诺稳定的 replay 字段包括：

1. host machine metadata
2. wall clock duration
3. 真正的 statement-level interpreter trace
4. provider payload diff

## Audit Report 的最小边界

当前建议 `AuditReport` 主要面向人工审阅与 CI 归档，它至少应覆盖：

1. workflow 输入身份与 source artifact identity
2. execution plan 的 node order / dependency 摘要
3. runtime session 的最终 workflow / node 状态摘要
4. execution journal 的关键事件统计与 completion 结果
5. dry-run trace 中已有的 reviewer-friendly 摘要
6. consistency / validation 结论与 failure 分类

换句话说：

- replay 更关注“能否按事件重建状态推进”
- audit 更关注“是否能稳定解释这次运行为何成立或失败”

## Failure-Path 扩展原则

V0.8 若扩展失败路径，当前建议优先补齐以下事件族：

1. `node_failed`
2. `workflow_failed`
3. `mock_missing`
4. `replay_consistency_error`

扩展原则：

1. failure-path 应优先作为 `ExecutionJournal` 的兼容增量扩展
2. replay / audit 应能在缺少 failure-path 时继续消费 V0.7 成功路径 journal
3. 不允许为了 failure-path 回退引入真实 host log 或 provider payload 依赖

## 与 Future Runtime 的边界

V0.8 replay / audit bootstrap 的主要用途是：

1. 冻结 session + journal 的可消费下游视图
2. 建立 deterministic consistency checking surface
3. 让 future scheduler prototype 与 audit tooling 不再直接依赖 golden 文本
4. 为后续失败路径、partial session 与 richer scheduler 原型提供输入边界

它不用于：

1. 证明真实运行环境可恢复
2. 证明并发、重试、超时与吞吐语义正确
3. 证明部署配置或 provider 集成正确
4. 取代生产 observability / telemetry / log pipeline

## 当前状态

截至当前设计：

1. V0.7 已冻结 `RuntimeSession` 与 `ExecutionJournal` 的最小 bootstrap 边界
2. V0.8 最自然的下一步是把这些 artifact 提升为 replay / audit consumer 的正式输入
3. future scheduler、audit CLI 与 CI 归档都应优先复用本文定义的消费边界，而不是自行重建运行语义
