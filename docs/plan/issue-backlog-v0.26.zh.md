# AHFL V0.26 Issue Backlog

本文给出与 [roadmap-v0.26.zh.md](./roadmap-v0.26.zh.md) 对齐的 issue-ready backlog。目标是把 V0.25 provider host execution planning contract 推进到 provider local host execution receipt contract，而不是直接实现 SDK client、配置加载器、secret manager、host process manager、网络执行器或生产 provider writer。

## [x] Issue 01

标题：
冻结 V0.26 local host execution receipt boundary 与 non-goals

验收标准：

1. 文档明确 V0.26 做 local host execution receipt simulation，不做真实 host execution
2. non-goals 覆盖 credential、secret value、provider endpoint、object path、database table、SDK payload、host command、host environment、network endpoint
3. 后续 issue 不把真实 config、secret、payload、host process、host env、network 或 filesystem side effect 混入 stable artifact

## [x] Issue 02

标题：
冻结 V0.25 host execution plan 的输入边界

验收标准：

1. receipt 只读取 `ProviderHostExecutionPlan`
2. receipt 不读取 host execution readiness reviewer text
3. V0.25 compatibility contract 保持不变

## [x] Issue 03

标题：
定义 local host receipt identity namespace

验收标准：

1. ready path 有 deterministic `provider-local-host-execution-receipt::<provider-host-execution-descriptor-id>` regression
2. namespace 不依赖 wall clock、process id、host path、credential、secret、host command 或 SDK payload
3. blocked path 不生成 local host receipt identity

## [x] Issue 04

标题：
定义 provider local host execution receipt model

验收标准：

1. 有 ready / blocked direct regression
2. validation 拒绝 host process、host env、network、SDK payload、SDK call 与 filesystem side effect
3. model 可以与 `ProviderHostExecutionPlan` 关联，但不修改 V0.25 稳定字段

## [x] Issue 05

标题：
定义 host-execution-not-ready failure attribution

验收标准：

1. blocked source 有 direct regression
2. failure attribution 不包含 provider credential、secret material、endpoint、host command、host env 或 SDK payload
3. diagnostics 不混淆 host execution planning failure 与 local host receipt failure

## [x] Issue 06

标题：
定义 provider local host execution receipt review

验收标准：

1. review 只消费 local host execution receipt
2. summary 与 machine artifact 一致
3. 不倒灌 config snapshot、secret resolver response、provider payload、operator payload、host telemetry、host command、host env、network endpoint 或 filesystem output

## [x] Issue 07

标题：
冻结 V0.26 compatibility contract

验收标准：

1. compatibility 文档列出 stable / unstable / forbidden fields
2. breaking-change 条件清晰
3. contributor guide 可引用该 contract

## [x] Issue 08

标题：
更新 native consumer matrix 与 contributor guide

验收标准：

1. `docs/README.md` 索引更新
2. README entry points 更新
3. contributor guide 覆盖 docs / code / golden / tests 同步流程

## [x] Issue 09

标题：
建立 V0.26 regression、CI 与 migration docs 闭环

验收标准：

1. `ctest --preset test-dev --output-on-failure -L ahfl-v0.26` 可用
2. `ctest --preset test-asan --output-on-failure -L ahfl-v0.26` 可用
3. `MIGRATION.md` 说明 V0.25 到 V0.26 的边界变化
