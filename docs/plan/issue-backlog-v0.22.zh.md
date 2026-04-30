# AHFL V0.22 Issue Backlog

本文给出与 [roadmap-v0.22.zh.md](./roadmap-v0.22.zh.md) 对齐的 issue-ready backlog。目标是把 V0.21 provider-neutral write attempt 边界推进到 provider driver binding contract，而不是直接实现某个 provider SDK、生产数据库或对象存储写入器。

## [x] Issue 01

标题：
冻结 V0.22 provider driver extension boundary 与 non-goals

验收标准：

1. 文档明确 V0.22 做 driver binding contract，不做 SDK 集成
2. non-goals 覆盖 credential、secret manager、endpoint URI、object path、database table、SDK payload
3. 后续 issue 不把 provider-specific secret 或 payload 混入 stable artifact

## [x] Issue 02

标题：
冻结 provider-neutral shim、provider-specific driver 与 production SDK writer 的职责分层

验收标准：

1. layering 文档覆盖 bound、source-not-planned、profile-mismatch、capability-gap
2. breaking-change 条件清晰
3. consumer matrix 可以据此扩展 V0.22 consumer 行

## [x] Issue 03

标题：
冻结 V0.21 provider write attempt 与 future provider driver 的输入边界

验收标准：

1. allowed / forbidden inputs 明确
2. provider driver binding 不读取 recovery handoff reviewer text
3. V0.21 compatibility contract 保持不变

## [x] Issue 04

标题：
定义 secret-free provider driver profile model

验收标准：

1. 有 direct model regression
2. validation 拒绝 credential、endpoint URI、endpoint secret、object path、database table、SDK payload schema
3. profile 可以与 `ProviderWriteAttemptPreview` 关联，但不修改 V0.21 稳定字段

## [x] Issue 05

标题：
定义 driver capability gating 与 unsupported capability diagnostics

验收标准：

1. 有正负例 direct regression
2. unsupported driver capability 不会被错误执行为 SDK call
3. diagnostics 区分 profile error 与 driver capability gap

## [x] Issue 06

标题：
定义 driver binding operation descriptor namespace

验收标准：

1. bound path 有 deterministic operation descriptor regression
2. not-bound path 不生成 operation descriptor
3. descriptor namespace 不依赖 wall clock、process id、host path 或 credential

## [x] Issue 07

标题：
定义 provider driver binding plan

验收标准：

1. plan 只消费 provider write attempt 与 driver profile
2. planned source path 形成 binding plan
3. non-planned source path 保持 non-bound plan

## [x] Issue 08

标题：
定义 profile mismatch / capability gap / source-not-planned failure attribution

验收标准：

1. 有 profile mismatch、capability gap、rejected source direct regression
2. failure attribution 不包含 provider credential 或 secret material
3. diagnostics 不混淆 source planning failure 与 driver binding failure

## [x] Issue 09

标题：
定义 provider driver readiness review 与真实 SDK ABI 的边界

验收标准：

1. review 只消费 provider driver binding plan
2. summary 与 machine artifact 一致
3. 不倒灌 provider payload、operator payload 或 telemetry

## [x] Issue 10

标题：
冻结 V0.22 compatibility contract

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
建立 V0.22 regression、CI 与 migration docs 闭环

验收标准：

1. `ctest --preset test-dev --output-on-failure -L ahfl-v0.22` 可用
2. `ctest --preset test-asan --output-on-failure -L ahfl-v0.22` 可用
3. `MIGRATION.md` 说明 V0.21 到 V0.22 的边界变化
