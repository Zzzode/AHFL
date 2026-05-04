# AHFL V0.32 Issue Backlog

本文与 [roadmap-v0.32.zh.md](./roadmap-v0.32.zh.md) 对齐，目标是冻结 fake provider SDK payload materialization boundary。

## [x] Issue 01

标题：冻结 payload materialization scope

验收标准：

1. payload plan 只消费 V0.31 harness review
2. 仅允许 fake provider schema
3. 不调用真实 provider SDK

## [x] Issue 02

标题：定义 payload plan / digest / redaction summary

验收标准：

1. plan 包含 schema ref 与 payload digest
2. redaction summary 明确 secret-free、credential-free、token-free
3. raw payload 不进入持久化 artifact

## [x] Issue 03

标题：接入 fake provider schema

验收标准：

1. schema ref 固定为 `ahfl.fake-provider-sdk-payload-schema.v1`
2. digest deterministic
3. artifact 明确 `fake_provider_only=true`

## [x] Issue 04

标题：覆盖 payload forbidden-field validation

验收标准：

1. validation 拒绝 raw payload
2. validation 拒绝 secret、credential、token value
3. validation 拒绝真实 network / SDK side effect

## [x] Issue 05

标题：建立 golden 与 compatibility docs

验收标准：

1. golden 覆盖 single-file、project manifest、workspace
2. `ahfl-v0.32` 标签可用
3. docs index、README、MIGRATION 同步
