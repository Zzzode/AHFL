# AHFL V0.37 Issue Backlog

本文与 [roadmap-v0.37.zh.md](./roadmap-v0.37.zh.md) 对齐，目标是冻结 provider write 的 recovery / resume artifact 边界。

## [x] Issue 01

标题：定义 recovery checkpoint

验收标准：

1. checkpoint 消费 V0.36 commit receipt
2. checkpoint 记录 resume token placeholder 与 idempotency key
3. checkpoint 覆盖 committed、duplicate、partial、failed、blocked

## [x] Issue 02

标题：定义 partial write recovery plan

验收标准：

1. partial commit 映射为 `resume_with_idempotency_key`
2. duplicate commit 映射为 provider commit lookup
3. failed / blocked commit 保持人工 review

## [x] Issue 03

标题：定义 recovery review

验收标准：

1. review 输出 next action
2. committed path 可进入 audit event
3. recoverable path 保留 failure attribution

## [x] Issue 04

标题：覆盖 crash / unknown commit regression

验收标准：

1. direct matrix 覆盖 committed、duplicate、partial、failed、blocked
2. golden 覆盖 single-file、project manifest、workspace
3. `ahfl-v0.37` 标签可用
