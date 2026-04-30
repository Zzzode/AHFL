# AHFL V0.25 Issue Backlog

本文给出与 [roadmap-v0.25.zh.md](./roadmap-v0.25.zh.md) 对齐的 issue-ready backlog。目标是把 V0.24 provider SDK request envelope / host handoff boundary 推进到 provider host execution planning contract，而不是直接实现 SDK client、配置加载器、secret manager、host process manager、网络执行器或生产 provider writer。

## [x] Issue 01

标题：
冻结 V0.25 host execution planning boundary 与 non-goals

验收标准：

1. 文档明确 V0.25 做 host execution planning，不做真实 host execution
2. non-goals 覆盖 credential、secret value、provider endpoint、object path、database table、SDK payload、host command、host environment、network endpoint
3. 后续 issue 不把真实 config、secret、payload、host process、host env、network 或 filesystem side effect 混入 stable artifact

## [x] Issue 02

标题：
冻结 V0.24 SDK envelope 与 host execution 的输入边界

验收标准：

1. allowed / forbidden inputs 明确
2. host execution plan 不读取 SDK handoff readiness reviewer text
3. V0.24 compatibility contract 保持不变

## [x] Issue 03

标题：
冻结 execution profile、sandbox policy 与 timeout policy 的职责分层

验收标准：

1. layering 文档覆盖 ready、sdk-handoff-not-ready、policy-mismatch、capability-gap
2. breaking-change 条件清晰
3. consumer matrix 可以据此扩展 V0.25 consumer 行

## [x] Issue 04

标题：
定义 provider host execution policy model

验收标准：

1. 有 direct model regression
2. validation 拒绝 credential、secret value、host command、host environment secret、provider endpoint、network endpoint、object path、database table、SDK request / response payload
3. policy 可以与 `ProviderSdkRequestEnvelopePlan` 关联，但不修改 V0.24 稳定字段

## [x] Issue 05

标题：
定义 host capability gating 与 unsupported capability diagnostics

验收标准：

1. 有正负例 direct regression
2. unsupported host capability 不会被错误执行为 host process start、host env read、network open、SDK payload materialization、SDK call 或 filesystem write
3. diagnostics 区分 SDK handoff error、host policy mismatch 与 host capability gap

## [x] Issue 06

标题：
定义 host execution descriptor 与 host receipt placeholder identity namespace

验收标准：

1. ready path 有 deterministic host execution descriptor identity regression
2. ready path 有 deterministic dry-run host receipt placeholder identity regression
3. namespace 不依赖 wall clock、process id、host path、credential、secret、host command 或 SDK payload

## [x] Issue 07

标题：
定义 provider host execution plan

验收标准：

1. plan 只消费 provider SDK request envelope plan 与 host execution policy
2. ready SDK envelope 形成 ready host execution plan
3. blocked SDK envelope 保持 blocked host execution plan

## [x] Issue 08

标题：
定义 sdk-handoff-not-ready / policy-mismatch / capability-gap failure attribution

验收标准：

1. 有 policy mismatch、capability gap、blocked source direct regression
2. failure attribution 不包含 provider credential、secret material、endpoint、host command、host env 或 SDK payload
3. diagnostics 不混淆 SDK envelope failure 与 host execution failure

## [x] Issue 09

标题：
定义 provider host execution readiness review 与真实 host execution harness ABI 的边界

验收标准：

1. review 只消费 host execution plan
2. summary 与 machine artifact 一致
3. 不倒灌 config snapshot、secret resolver response、provider payload、operator payload、host telemetry、host command、host env、network endpoint 或 filesystem output

## [x] Issue 10

标题：
冻结 V0.25 compatibility contract

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
建立 V0.25 regression、CI 与 migration docs 闭环

验收标准：

1. `ctest --preset test-dev --output-on-failure -L ahfl-v0.25` 可用
2. `ctest --preset test-asan --output-on-failure -L ahfl-v0.25` 可用
3. `MIGRATION.md` 说明 V0.24 到 V0.25 的边界变化
