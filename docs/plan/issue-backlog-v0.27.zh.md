# AHFL V0.27 Issue Backlog

本文给出与 [roadmap-v0.27.zh.md](./roadmap-v0.27.zh.md) 对齐的 issue-ready backlog。目标是把 V0.26 provider local host execution receipt contract 推进到 provider SDK adapter request / response artifact boundary，而不是直接实现真实 SDK client、provider network call、secret manager、host process、filesystem writer、object store writer 或 database writer。

## [x] Issue 01

标题：
冻结 V0.27 SDK adapter artifact boundary 与 non-goals

验收标准：

1. 文档明确 V0.27 只生成 request plan、response placeholder 与 readiness review
2. non-goals 覆盖真实 SDK call、credential、secret、endpoint、object path、database table、SDK payload、provider response payload、network 与 filesystem side effect
3. 后续 issue 不把真实 SDK interface、config loader、secret resolver、provider payload 或 host effect 混入 stable artifact

## [x] Issue 02

标题：
冻结 V0.26 local host execution receipt 的输入边界

验收标准：

1. request plan 只读取 `ProviderLocalHostExecutionReceipt`
2. response placeholder 只读取 `ProviderSdkAdapterRequestPlan`
3. readiness review 只读取 `ProviderSdkAdapterResponsePlaceholder`

## [x] Issue 03

标题：
定义 SDK adapter request / response placeholder identity namespace

验收标准：

1. ready path 有 deterministic `provider-sdk-adapter-request::<provider-local-host-execution-receipt-id>` regression
2. ready path 有 deterministic `provider-sdk-adapter-response-placeholder::<provider-sdk-adapter-request-id>` regression
3. namespace 不依赖 wall clock、process id、host path、credential、secret、endpoint、SDK payload 或 provider response payload

## [x] Issue 04

标题：
定义 provider SDK adapter request plan model

验收标准：

1. 有 ready / blocked direct regression
2. validation 拒绝 SDK payload、SDK call、network、secret、host env 与 filesystem side effect
3. model 保留 source local host receipt identity，但不修改 V0.26 稳定字段

## [x] Issue 05

标题：
定义 provider SDK adapter response placeholder model

验收标准：

1. ready request plan 生成 response placeholder artifact
2. blocked request plan 生成 blocked noop placeholder
3. placeholder 不包含 provider endpoint、object path、database table、SDK request payload 或 SDK response payload

## [x] Issue 06

标题：
定义 provider SDK adapter readiness review

验收标准：

1. review 只消费 response placeholder
2. ready path 给出进入 future adapter interface implementation 的 next action
3. blocked path 保留 failure attribution，不读取 reviewer text、host log、secret、SDK payload 或 provider response

## [x] Issue 07

标题：
冻结 V0.27 compatibility contract

验收标准：

1. compatibility 文档列出 stable / forbidden fields
2. breaking-change 条件覆盖 format string、identity、side-effect boundary 与 source layering
3. contributor guide 可引用该 contract

## [x] Issue 08

标题：
更新 native consumer matrix 与 contributor guide

验收标准：

1. `docs/README.md` 索引更新
2. root `README.md` backend command list 更新
3. contributor guide 覆盖 docs / code / golden / tests 同步流程

## [x] Issue 09

标题：
建立 V0.27 regression、CI 与 migration docs 闭环

验收标准：

1. `ctest --preset test-dev --output-on-failure -L ahfl-v0.27` 可用
2. `ctest --preset test-asan --output-on-failure -L ahfl-v0.27` 纳入 CI
3. `MIGRATION.md` 说明 V0.26 到 V0.27 的边界变化
