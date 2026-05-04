# AHFL V0.35 Issue Backlog

本文与 [roadmap-v0.35.zh.md](./roadmap-v0.35.zh.md) 对齐，目标是冻结 provider write 的 idempotency / retry contract。

## [x] Issue 01

标题：冻结 idempotency namespace

验收标准：

1. namespace 固定为 `ahfl.durable-store-import.provider-write.v1`
2. 每次 decision 输出稳定 idempotency key
3. retry token placeholder 带格式版本

## [x] Issue 02

标题：定义 retry eligibility

验收标准：

1. timeout 与 throttled 映射为 retryable
2. provider failure 与 schema mismatch 映射为 non-retryable
3. conflict 映射为 duplicate review required

## [x] Issue 03

标题：定义 duplicate detection summary

验收标准：

1. conflict path 标记 duplicate write possible
2. duplicate path 需要人工 review
3. accepted path 不允许无意义 retry

## [x] Issue 04

标题：接入 mock adapter matrix

验收标准：

1. mock adapter accepted / timeout / throttled / conflict / provider failure / schema mismatch 全覆盖
2. direct matrix regression 固化 taxonomy
3. CLI golden 覆盖三种输入模式

## [x] Issue 05

标题：更新 compatibility 与 CI

验收标准：

1. compatibility doc 说明 retry 边界
2. contributor guide 列出新命令
3. `ahfl-v0.35` 标签可用
