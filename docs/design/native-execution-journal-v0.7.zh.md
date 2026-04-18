# AHFL Native Execution Journal V0.7

本文冻结 AHFL V0.7 中 execution journal 的最小边界。它承接 runtime session bootstrap，但不定义生产 runtime log、host metrics 或真实 connector payload。

关联文档：

- [native-runtime-session-bootstrap-v0.7.zh.md](./native-runtime-session-bootstrap-v0.7.zh.md)
- [native-dry-run-bootstrap-v0.6.zh.md](./native-dry-run-bootstrap-v0.6.zh.md)
- [roadmap-v0.7.zh.md](../plan/roadmap-v0.7.zh.md)

## 目标

本文主要回答四个问题：

1. execution journal 要记录哪些最小事件。
2. journal 与 V0.6 dry-run trace 的关系是什么。
3. 哪些事件字段是当前稳定边界。
4. future replay / audit consumer 应如何依赖这层。

## 非目标

本文不定义：

1. 真实 host log 格式
2. wall clock duration、CPU、memory 指标
3. provider request / response payload
4. tenant、region、deployment、secret、endpoint

## 最小事件集合

当前建议最小 `ExecutionJournal` 至少包含：

1. `session_started`
2. `node_became_ready`
3. `node_started`
4. `node_completed`
5. `workflow_completed`

如需失败路径，后续可扩：

1. `node_failed`
2. `workflow_failed`
3. `mock_missing`
4. `journal_consistency_error`

## 事件字段的最小边界

当前建议每条 journal event 至少包含：

1. 顶层 `kind`
2. `workflow_canonical_name`
3. `node_name`（若适用）
4. `execution_index`（若适用）
5. `satisfied_dependencies`
6. `used_mock_selector` 或等价 mock usage 摘要

当前不承诺稳定的字段包括：

1. host pid / tid
2. monotonic timestamp
3. provider payload
4. runtime machine id

## 与 Dry-Run Trace 的关系

当前建议：

1. `DryRunTrace`
   - 面向 contributor 的稳定 review summary
2. `ExecutionJournal`
   - 面向 replay / audit / future scheduler prototype 的稳定 event 序列

换句话说：

- trace 适合审阅
- journal 适合重放和验证状态推进

V0.7 不建议直接用 trace 替代 journal，也不建议让 journal 直接承担全部 contributor-facing 排版职责。

## Consumer 建议

当前 future consumer 应按下面方式依赖：

1. replay / scheduler prototype
   - 优先消费 `ExecutionJournal`
2. review / human audit
   - 优先消费 `DryRunTrace`
3. 若需要 planning 语义补充
   - 再回看 `ExecutionPlan`

## 当前状态

截至当前设计：

1. V0.6 已有 dry-run trace 作为 contributor-facing summary
2. V0.7 下一步适合新增 journal，表达更细的 runtime event sequence
3. future audit / replay work 不应直接从 CLI trace 文本倒推内部 runtime state
