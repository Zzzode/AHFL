# AHFL V0.29 Issue Backlog

本文给出与 [roadmap-v0.29.zh.md](./roadmap-v0.29.zh.md) 对齐的 issue-ready backlog。目标是在 V0.28 provider SDK adapter interface 之后冻结 provider config loader boundary，而不是读取 secret value、credential material、remote config service、provider endpoint 或打开网络。

## [x] Issue 01

标题：
冻结 config loader scope、输入边界与 forbidden fields

验收标准：

1. config load plan 只消费 `ProviderSdkAdapterInterfacePlan`
2. snapshot placeholder 只消费 `ProviderConfigLoadPlan`
3. artifact 拒绝 secret value、credential material、endpoint URI、remote config URI、SDK payload、network、host env 与 filesystem side effect

## [x] Issue 02

标题：
定义 config profile descriptor

验收标准：

1. ready path 生成 deterministic config profile identity
2. profile descriptor 包含 provider key、profile key 与 schema version
3. profile descriptor 只能声明 future secret handle requirement，不能包含 secret value

## [x] Issue 03

标题：
定义 config snapshot placeholder

验收标准：

1. ready load plan 生成 deterministic snapshot placeholder identity
2. blocked load plan 生成 blocked noop snapshot
3. placeholder 不包含 provider endpoint、credential、secret 或 remote config response

## [x] Issue 04

标题：
定义 config readiness review

验收标准：

1. review 只消费 snapshot placeholder
2. ready path next action 指向 future secret handle resolver
3. blocked path 保留 failure attribution

## [x] Issue 05

标题：
建立 CLI、golden、docs、CI 闭环

验收标准：

1. direct regression 覆盖 ready、blocked、forbidden material 与 invalid snapshot
2. golden 覆盖 single-file、project manifest、workspace
3. `ahfl-v0.29` 标签可用，CI、README、MIGRATION 与 docs index 同步
