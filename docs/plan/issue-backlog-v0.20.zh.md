# AHFL V0.20 Issue Backlog

本文给出与 [roadmap-v0.20.zh.md](./roadmap-v0.20.zh.md) 对齐的 issue-ready backlog。目标是把 V0.19 已冻结的 durable adapter receipt persistence response 边界推进到 production-adjacent durable store adapter / persistence executor contract，而不是直接实现完整生产存储平台。

## [x] Issue 01

标题：
冻结 V0.20 durable store adapter / persistence executor scope 与 non-goals

目标：

1. 明确 V0.20 做 production-adjacent adapter contract，不做完整生产存储平台
2. 明确 fake durable store、real provider adapter、recovery command 与 reviewer summary 的职责边界
3. 明确不得绕过 V0.19 `Response` artifact

验收标准：

1. roadmap / backlog / 后续 design docs 使用同一组 adapter execution、persistence id、store mutation intent、recovery command 术语
2. 文档明确 non-goals：provider SDK、credential、multi-tenant production schema、replication、retention、GC、operator console
3. 后续 issue 不把 provider-specific payload 混入 stable machine artifact

主要涉及文件：

- `docs/plan/`
- `docs/design/`

## [x] Issue 02

标题：
冻结 persistence id、store mutation intent 与 adapter execution receipt 的最小模型

目标：

1. 定义 production-adjacent durable store adapter execution 的 machine-facing 对象
2. 定义 persistence id、mutation intent、execution receipt、failure attribution 的最小字段
3. 明确 format version 与 breaking-change 入口

验收标准：

1. 稳定字段可以从 V0.19 `Response` 及上游 machine artifact 派生
2. 模型不包含 provider credential、host telemetry、object path 或 database key
3. 有后续 direct validation 的明确入口

主要涉及文件：

- `docs/design/`
- `docs/reference/`

## [x] Issue 03

标题：
冻结 V0.19 response、future real adapter 与 recovery command 的分层关系

目标：

1. 明确 `Response -> AdapterExecutionReceipt -> RecoveryCommandPreview` 的职责拆分
2. 明确 recovery command preview 不能成为真实 recovery daemon ABI
3. 明确 future provider adapter 不得读取 review summary 或 CLI 文本作为第一事实来源

验收标准：

1. layering 文档覆盖 accepted、blocked、deferred、rejected 四类 response 输入
2. breaking-change 条件清晰
3. consumer matrix 可以据此扩展 V0.20 consumer 行

主要涉及文件：

- `docs/design/`
- `docs/reference/`

## [x] Issue 04

标题：
引入 local fake durable store contract 数据模型

目标：

1. 引入 fake durable store config、mutation intent 与 execution result model
2. 以 deterministic local store 作为真实 provider adapter 前的测试替身
3. 保持 fake store model 与 provider-specific fields 解耦

验收标准：

1. 有 direct model regression
2. fake store model 只消费 stable machine artifact
3. diagnostics 能区分 model error 与 source artifact error

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

## [x] Issue 05

标题：
增加 store mutation intent validation 与 direct regression

目标：

1. 校验 persistence id、mutation intent、source response、execution identity 的一致性
2. 覆盖 accepted、blocked、deferred、rejected 输入
3. 固定 failure attribution diagnostics

验收标准：

1. 有正负例 direct regression
2. blocked/deferred/rejected 不会被错误执行为 store mutation
3. accepted path 缺失 persistence id 或 mutation target 时会失败

主要涉及文件：

- `src/`
- `tests/`

## [x] Issue 06

标题：
增加 deterministic fake store bootstrap

目标：

1. 从 V0.19 `Response` 构建 fake store mutation intent
2. 为 accepted path 生成 deterministic fake persistence id
3. 为 blocked/deferred/rejected path 生成 non-mutating execution result

验收标准：

1. bootstrap 不依赖 CLI 文本或 review summary
2. accepted / blocked / deferred / rejected 四类输入都有 deterministic regression
3. failure attribution 不混淆 adapter contract error 与 store mutation error

主要涉及文件：

- `src/`
- `tests/`

## [x] Issue 07

标题：
增加 adapter execution receipt 输出路径

目标：

1. 增加 backend / CLI 输出 production-adjacent adapter execution receipt
2. 覆盖 single-file、project、workspace 与 `--package`
3. 固定 accepted path 的 fake store execution golden

验收标准：

1. CLI 输出稳定 adapter execution receipt
2. 输出包含 source response identity、persistence id、mutation status 与 failure attribution
3. 不输出 provider credential、host log、真实 object path 或 database key

主要涉及文件：

- `src/backends/`
- `src/cli/`
- `tests/`

## [x] Issue 08

标题：
增加 recovery command preview / reviewer summary

目标：

1. 提供 reviewer-facing recovery preview
2. 明确 preview 不等于真实 recovery daemon ABI
3. 覆盖 accepted / blocked / deferred / rejected 的 next-step recommendation

验收标准：

1. review summary 只消费 adapter execution receipt
2. summary 与 machine artifact 一致
3. 不倒灌 provider payload 或 telemetry

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

## [x] Issue 09

标题：
增加 accepted、blocked、deferred、rejected 至 adapter execution 的 golden / direct 回归

目标：

1. accepted path 使用 CLI golden 固定 fake store execution
2. blocked / deferred / rejected path 至少使用 direct regression 固定 non-mutating execution
3. 若出现自然 source fixture，再补 CLI golden

验收标准：

1. `ahfl-v0.20` 标签覆盖 V0.20 direct / emission / golden
2. non-mutating path 不会生成 persistence id
3. golden 与 compatibility 文档同步

主要涉及文件：

- `tests/cmake/`
- `tests/`

## [x] Issue 10

标题：
冻结 V0.20 compatibility contract

目标：

1. 定义 adapter execution receipt 的 stable fields
2. 定义 fake store / future real provider 的兼容扩展规则
3. 明确 version bump 条件

验收标准：

1. compatibility 文档列出 stable / unstable / forbidden fields
2. breaking-change 条件清晰
3. contributor guide 可引用该 contract

主要涉及文件：

- `docs/reference/`

## [x] Issue 11

标题：
更新 native consumer matrix 与 contributor guide

目标：

1. 增加 V0.20 adapter execution consumer 行
2. 更新 contributor-facing 命令与回归标签
3. 明确 fake store 与 future real provider adapter 的边界

验收标准：

1. `docs/README.md` 索引更新
2. README entry points 更新
3. contributor guide 覆盖 docs / code / golden / tests 同步流程

主要涉及文件：

- `README.md`
- `docs/README.md`
- `docs/reference/`

## [x] Issue 12

标题：
建立 V0.20 regression、CI 与 migration docs 闭环

目标：

1. 新增 `ahfl-v0.20` 与专用标签
2. CI 显式执行 V0.20 dev / ASan 切片
3. 更新 migration notes

验收标准：

1. `ctest --preset test-dev --output-on-failure -L ahfl-v0.20` 可用
2. `ctest --preset test-asan --output-on-failure -L ahfl-v0.20` 可用
3. `MIGRATION.md` 说明 V0.19 到 V0.20 的边界变化

主要涉及文件：

- `.github/workflows/ci.yml`
- `tests/cmake/`
- `MIGRATION.md`
