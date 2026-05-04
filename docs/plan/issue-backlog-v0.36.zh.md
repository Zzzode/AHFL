# AHFL V0.36 Issue Backlog

本文与 [roadmap-v0.36.zh.md](./roadmap-v0.36.zh.md) 对齐，目标是将 durable provider write 的 commit receipt 提升为稳定 artifact。

## [x] Issue 01

标题：冻结 commit receipt stable fields

验收标准：

1. receipt 包含 commit identity、provider commit reference 与 digest
2. receipt 保留 idempotency key
3. receipt 明确 secret-free 且不持久化 raw provider payload

## [x] Issue 02

标题：实现 commit receipt validation

验收标准：

1. committed、duplicate、partial、failed、blocked 均可验证
2. 非 committed 状态必须有 failure attribution
3. secret-bearing response 与 raw payload persistence 被拒绝

## [x] Issue 03

标题：接入 retry decision bootstrap

验收标准：

1. not-applicable 映射 committed
2. duplicate-review-required 映射 duplicate
3. retryable 映射 partial，non-retryable 映射 failed

## [x] Issue 04

标题：实现 commit review

验收标准：

1. review 输出 summary 与 next action
2. retry path 指向 idempotency contract
3. committed path 指向 recovery audit

## [x] Issue 05

标题：覆盖 golden、consumer matrix 与 CI

验收标准：

1. golden 覆盖 receipt / review 的三种输入模式
2. `ahfl-v0.36` 标签可用
3. README 与 MIGRATION 同步
