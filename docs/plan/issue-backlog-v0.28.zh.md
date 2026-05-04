# AHFL V0.28 Issue Backlog

本文给出与 [roadmap-v0.28.zh.md](./roadmap-v0.28.zh.md) 对齐的 issue-ready backlog。目标是在 V0.27 SDK adapter request plan 后冻结 provider SDK adapter interface contract，而不是接入真实云 SDK、database client、secret manager、network endpoint、dynamic plugin loader 或 production registry discovery。

## [x] Issue 01

标题：
冻结 V0.28 adapter interface scope 与 non-goals

验收标准：

1. 文档明确 V0.28 定义 interface plan / review，不做真实 SDK 调用
2. non-goals 覆盖 credential、secret、endpoint、SDK payload、provider response、network、filesystem 与动态插件加载
3. 后续 issue 不把真实 adapter side effect 混入 stable artifact

## [x] Issue 02

标题：
定义 adapter descriptor 与 registry identity

验收标准：

1. ready path 生成 deterministic provider registry identity
2. ready path 生成 deterministic adapter descriptor identity
3. descriptor 包含 provider key、adapter name、adapter version 与 ABI version

## [x] Issue 03

标题：
定义 capability descriptor

验收标准：

1. descriptor 声明 `durable_store_import.write` capability
2. input contract 指向 V0.27 request plan format
3. output contract 指向 V0.27 response placeholder format

## [x] Issue 04

标题：
定义 provider error normalization skeleton

验收标准：

1. artifact 包含 error taxonomy version
2. ready path 使用 `none`
3. blocked request path 使用 `sdk_adapter_request_not_ready`

## [x] Issue 05

标题：
建立 model、CLI、golden、docs、CI 闭环

验收标准：

1. direct regression 覆盖 ready、blocked、forbidden material 与 review validation
2. golden 覆盖 single-file、project manifest、workspace
3. `ahfl-v0.28` 标签可用，CI、README、MIGRATION 与 docs index 同步
