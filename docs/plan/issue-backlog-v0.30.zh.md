# AHFL V0.30 Issue Backlog

本文与 [roadmap-v0.30.zh.md](./roadmap-v0.30.zh.md) 对齐，目标是冻结 secret handle resolver boundary，而不解析、读取或持久化任何 secret value、credential material、token 或 secret manager response。

## [x] Issue 01

标题：冻结 secret handle 与 resolver non-goals

验收标准：

1. 定义 `ProviderSecretHandleReference`
2. handle 只包含 stable identity、provider/profile/purpose
3. validation 拒绝 handle 内出现 secret、credential 或 token value

## [x] Issue 02

标题：定义 resolver request / response placeholder

验收标准：

1. request 只消费 `ProviderConfigSnapshotPlaceholder`
2. response placeholder 只消费 resolver request
3. ready / blocked 两条路径都有 deterministic identity 与 failure attribution

## [x] Issue 03

标题：定义 credential lifecycle placeholder

验收标准：

1. response placeholder 暴露 `ahfl.provider-credential-lifecycle.v1`
2. ready path 标记 `placeholder_pending_resolution`
3. blocked path 标记 `blocked`

## [x] Issue 04

标题：建立 secret-free policy review

验收标准：

1. review 只消费 resolver response placeholder
2. next action 指向 V0.31 local host harness
3. 所有 artifact 拒绝 secret value、credential material、token 与 secret manager response

## [x] Issue 05

标题：建立 CLI、golden、docs、CI 标签

验收标准：

1. direct regression 覆盖 ready、blocked、forbidden material、side effect 与 invalid placeholder
2. golden 覆盖 single-file、project manifest、workspace
3. `ahfl-v0.30` 标签可用
