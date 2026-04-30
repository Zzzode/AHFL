# AHFL V0.24 Issue Backlog

本文给出与 [roadmap-v0.24.zh.md](./roadmap-v0.24.zh.md) 对齐的 issue-ready backlog。目标是把 V0.23 provider runtime preflight boundary 推进到 provider SDK request envelope / host handoff readiness contract，而不是直接实现 SDK client、配置加载器、secret manager、host process manager 或生产 provider writer。

## [x] Issue 01

标题：
冻结 V0.24 SDK request envelope / host handoff boundary 与 non-goals

验收标准：

1. 文档明确 V0.24 做 SDK request envelope planning，不做 SDK 集成
2. non-goals 覆盖 credential、secret value、provider endpoint、object path、database table、SDK payload、host command、network endpoint
3. 后续 issue 不把真实 config、secret、payload 或 host side effect 混入 stable artifact

## [x] Issue 02

标题：
冻结 V0.23 runtime preflight 与 SDK envelope 的输入边界

验收标准：

1. allowed / forbidden inputs 明确
2. SDK envelope plan 不读取 runtime readiness reviewer text
3. V0.23 compatibility contract 保持不变

## [x] Issue 03

标题：
冻结 secret-free request schema ref 与 host handoff policy ref 的职责分层

验收标准：

1. layering 文档覆盖 ready、runtime-preflight-not-ready、policy-mismatch、capability-gap
2. breaking-change 条件清晰
3. consumer matrix 可以据此扩展 V0.24 consumer 行

## [x] Issue 04

标题：
定义 provider SDK envelope policy model

验收标准：

1. 有 direct model regression
2. validation 拒绝 credential、secret value、provider endpoint、object path、database table、SDK request / response payload、host command、network endpoint
3. policy 可以与 `ProviderRuntimePreflightPlan` 关联，但不修改 V0.23 稳定字段

## [x] Issue 05

标题：
定义 envelope capability gating 与 unsupported capability diagnostics

验收标准：

1. 有正负例 direct regression
2. unsupported envelope capability 不会被错误执行为 SDK payload materialization、host process start、network open 或 SDK call
3. diagnostics 区分 runtime preflight error、policy mismatch 与 envelope capability gap

## [x] Issue 06

标题：
定义 request envelope identity 与 host handoff descriptor identity namespace

验收标准：

1. ready path 有 deterministic request envelope identity regression
2. ready path 有 deterministic host handoff descriptor identity regression
3. namespace 不依赖 wall clock、process id、host path、credential、secret 或 SDK payload

## [x] Issue 07

标题：
定义 provider SDK request envelope plan

验收标准：

1. plan 只消费 provider runtime preflight plan 与 envelope policy
2. ready runtime preflight 形成 ready request envelope plan
3. blocked runtime preflight 保持 blocked request envelope plan

## [x] Issue 08

标题：
定义 runtime-preflight-not-ready / policy-mismatch / capability-gap failure attribution

验收标准：

1. 有 policy mismatch、capability gap、blocked source direct regression
2. failure attribution 不包含 provider credential、secret material、endpoint、host command 或 SDK payload
3. diagnostics 不混淆 runtime preflight failure 与 SDK envelope failure

## [x] Issue 09

标题：
定义 provider SDK handoff readiness review 与真实 host execution ABI 的边界

验收标准：

1. review 只消费 SDK request envelope plan
2. summary 与 machine artifact 一致
3. 不倒灌 config snapshot、secret resolver response、provider payload、operator payload、host telemetry、host command 或 network endpoint

## [x] Issue 10

标题：
冻结 V0.24 compatibility contract

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
建立 V0.24 regression、CI 与 migration docs 闭环

验收标准：

1. `ctest --preset test-dev --output-on-failure -L ahfl-v0.24` 可用
2. `ctest --preset test-asan --output-on-failure -L ahfl-v0.24` 可用
3. `MIGRATION.md` 说明 V0.23 到 V0.24 的边界变化
