# AHFL V0.33 Issue Backlog

本文与 [roadmap-v0.33.zh.md](./roadmap-v0.33.zh.md) 对齐，目标是建立 provider SDK mock adapter，让真实 provider adapter 之前先有完整回归替身。

## [x] Issue 01

标题：冻结 mock adapter fixture schema

验收标准：

1. contract 只消费 V0.32 payload audit
2. scenario 覆盖 success、failure、timeout、throttle、conflict、schema mismatch
3. contract 明确 fake provider only

## [x] Issue 02

标题：实现 mock adapter execution

验收标准：

1. mock execution 不访问网络
2. mock execution 不读取 secret
3. mock execution 不调用真实 provider SDK

## [x] Issue 03

标题：定义 result normalization 与 failure attribution

验收标准：

1. 每个 scenario 都映射为 normalized result
2. 非 success scenario 保留 failure attribution
3. success path 输出 accepted result

## [x] Issue 04

标题：覆盖 golden matrix

验收标准：

1. direct regression 覆盖六类 scenario
2. golden 覆盖 single-file、project manifest、workspace
3. `ahfl-v0.33` 标签可用

## [x] Issue 05

标题：更新 contributor guide 与 CI 标签

验收标准：

1. contributor guide 列出 mock adapter 命令
2. CI 覆盖 `ahfl-v0.33`
3. README 与 MIGRATION 同步
