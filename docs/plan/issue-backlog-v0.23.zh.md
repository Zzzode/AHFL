# AHFL V0.23 Issue Backlog

本文给出与 [roadmap-v0.23.zh.md](./roadmap-v0.23.zh.md) 对齐的 issue-ready backlog。目标是把 V0.22 provider driver binding boundary 推进到 runtime / SDK adapter preflight contract，而不是直接实现 SDK client、配置加载器、secret manager 或生产 provider writer。

## [x] Issue 01

标题：
冻结 V0.23 runtime / SDK adapter preflight boundary 与 non-goals

验收标准：

1. 文档明确 V0.23 做 runtime preflight contract，不做 SDK 集成
2. non-goals 覆盖 credential、secret value、secret manager endpoint、provider endpoint、object path、database table、SDK payload
3. 后续 issue 不把真实 config、secret 或 payload 混入 stable artifact

## [x] Issue 02

标题：
冻结 config loader、secret resolver placeholder 与 future SDK adapter 的职责分层

验收标准：

1. layering 文档覆盖 ready、driver-binding-not-ready、profile-mismatch、capability-gap
2. breaking-change 条件清晰
3. consumer matrix 可以据此扩展 V0.23 consumer 行

## [x] Issue 03

标题：
冻结 V0.22 provider driver binding 与 runtime preflight 的输入边界

验收标准：

1. allowed / forbidden inputs 明确
2. runtime preflight 不读取 readiness reviewer text
3. V0.22 compatibility contract 保持不变

## [x] Issue 04

标题：
定义 secret-free provider runtime profile model

验收标准：

1. 有 direct model regression
2. validation 拒绝 credential、secret value、endpoint URI、object path、database table、SDK payload schema、SDK request payload
3. profile 可以与 `ProviderDriverBindingPlan` 关联，但不修改 V0.22 稳定字段

## [x] Issue 05

标题：
定义 runtime capability gating 与 unsupported capability diagnostics

验收标准：

1. 有正负例 direct regression
2. unsupported runtime capability 不会被错误执行为 config load、secret resolution 或 SDK call
3. diagnostics 区分 runtime profile error 与 runtime capability gap

## [x] Issue 06

标题：
定义 SDK invocation envelope identity namespace

验收标准：

1. ready path 有 deterministic envelope identity regression
2. blocked path 不生成 envelope identity
3. namespace 不依赖 wall clock、process id、host path、credential 或 secret

## [x] Issue 07

标题：
定义 provider runtime preflight plan

验收标准：

1. plan 只消费 provider driver binding plan 与 runtime profile
2. bound driver path 形成 ready preflight plan
3. non-bound driver path 保持 blocked preflight plan

## [x] Issue 08

标题：
定义 binding-not-ready / profile-mismatch / capability-gap failure attribution

验收标准：

1. 有 profile mismatch、capability gap、blocked source direct regression
2. failure attribution 不包含 provider credential、secret material、endpoint 或 SDK payload
3. diagnostics 不混淆 driver binding failure 与 runtime preflight failure

## [x] Issue 09

标题：
定义 provider runtime readiness review 与真实 SDK adapter ABI 的边界

验收标准：

1. review 只消费 provider runtime preflight plan
2. summary 与 machine artifact 一致
3. 不倒灌 config snapshot、secret resolver response、provider payload、operator payload 或 telemetry

## [x] Issue 10

标题：
冻结 V0.23 compatibility contract

验收标准：

1. compatibility 文档列出 stable / unstable / forbidden fields
2. breaking-change 条件清晰
3. contributor guide 可引用该 contract

## [x] Issue 11

标题：
更新 native consumer matrix 与 contributor guide

验收标准：

1. `docs/README.md` 索引更新
2. README entry points 更新
3. contributor guide 覆盖 docs / code / golden / tests 同步流程

## [x] Issue 12

标题：
建立 V0.23 regression、CI 与 migration docs 闭环

验收标准：

1. `ctest --preset test-dev --output-on-failure -L ahfl-v0.23` 可用
2. `ctest --preset test-asan --output-on-failure -L ahfl-v0.23` 可用
3. `MIGRATION.md` 说明 V0.22 到 V0.23 的边界变化
