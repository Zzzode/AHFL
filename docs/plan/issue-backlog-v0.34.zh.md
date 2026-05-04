# AHFL V0.34 Issue Backlog

本文与 [roadmap-v0.34.zh.md](./roadmap-v0.34.zh.md) 对齐，目标是落地第一个低风险真实 provider adapter alpha，同时保持默认路径仍为 fake / mock adapter。

## [x] Issue 01

标题：选择 local filesystem alpha provider

验收标准：

1. provider key 固定为 `local-filesystem-alpha`
2. 默认路径不访问网络、不读取 secret、不调用云 SDK
3. 真实写入必须显式 opt-in

## [x] Issue 02

标题：实现 local filesystem alpha plan / result / readiness

验收标准：

1. plan 记录 provider 边界、目标对象名与 payload digest
2. result 能表达 accepted、dry-run-only、write-failed、blocked
3. readiness 将 alpha result 转为下一阶段可消费的 review

## [x] Issue 03

标题：保持默认 CI dry-run

验收标准：

1. CLI 默认不写本地文件
2. fake adapter 默认测试路径保持不变
3. opt-in 写入只在直接集成测试覆盖

## [x] Issue 04

标题：覆盖 golden matrix

验收标准：

1. golden 覆盖 single-file、project manifest、workspace
2. direct regression 覆盖 dry-run result 与 opt-in write
3. `ahfl-v0.34` 标签可用

## [x] Issue 05

标题：更新文档与迁移说明

验收标准：

1. design、compatibility、consumer matrix、contributor guide 已补齐
2. README 与 MIGRATION 同步新增命令
3. CI 覆盖 `ahfl-v0.34`
