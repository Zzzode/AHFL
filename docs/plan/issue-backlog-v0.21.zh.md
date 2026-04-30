# AHFL V0.21 Issue Backlog

本文给出与 [roadmap-v0.21.zh.md](./roadmap-v0.21.zh.md) 对齐的 issue-ready backlog。目标是把 V0.20 已冻结的 local fake durable store adapter execution 边界推进到 provider-neutral real adapter contract，而不是直接实现某个 provider SDK、生产数据库或对象存储写入器。

## [ ] Issue 01

标题：
冻结 V0.21 real provider adapter boundary 与 non-goals

目标：

1. 明确 V0.21 做 provider-neutral adapter contract，不做 provider SDK 集成
2. 明确 fake store、provider adapter shim、provider driver、production writer 与 recovery preview 的职责边界
3. 明确不得绕过 V0.20 `AdapterExecutionReceipt` artifact

验收标准：

1. roadmap / backlog / 后续 design docs 使用同一组 provider adapter、provider capability、write intent、retry placeholder、recovery handoff 术语
2. 文档明确 non-goals：credential、secret manager、真实 object path、database key、replication、retention、GC、operator console
3. 后续 issue 不把 provider-specific secret 或 payload 混入 stable machine artifact

主要涉及文件：

- `docs/plan/`
- `docs/design/`

## [ ] Issue 02

标题：
冻结 fake store、provider adapter shim、provider driver 与 production writer 的职责分层

目标：

1. 明确 V0.20 fake store 是 deterministic regression substitute
2. 明确 provider adapter shim 是 provider-neutral handoff，不直接持有 credential
3. 明确 provider driver / production writer 是 future extension，不属于 V0.21 初始实现

验收标准：

1. layering 文档覆盖 accepted、blocked、deferred、rejected 四类 adapter execution 输入
2. breaking-change 条件清晰
3. consumer matrix 可以据此扩展 V0.21 consumer 行

主要涉及文件：

- `docs/design/`
- `docs/reference/`

## [ ] Issue 03

标题：
冻结 V0.20 adapter execution 与 future provider-specific adapter 的输入边界

目标：

1. 明确 provider-specific adapter 只能消费 `AdapterExecutionReceipt` 和上游 machine artifacts
2. 明确 `RecoveryCommandPreview` 不是 provider adapter ABI
3. 明确 CLI text、host log、trace 与 reviewer text 不得成为 provider adapter state source

验收标准：

1. design / compatibility 文档明确 allowed / forbidden inputs
2. provider extension point 不读取 recovery preview
3. V0.20 compatibility contract 保持不变

主要涉及文件：

- `docs/design/`
- `docs/reference/`

## [ ] Issue 04

标题：
定义 secret-free provider adapter config model

目标：

1. 引入 provider adapter config 的最小 model
2. config 只包含 provider-neutral reference，不包含 credential / secret
3. 为 future provider driver 留出 extension slot

验收标准：

1. 有 direct model regression
2. validation 拒绝 credential、secret、真实 endpoint token、object path、database key
3. config 可以与 `AdapterExecutionReceipt` 关联，但不修改 V0.20 稳定字段

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

## [ ] Issue 05

标题：
定义 provider capability matrix 与 unsupported capability diagnostics

目标：

1. 定义 provider capability matrix artifact
2. 表达 provider 支持 / 不支持的 write、retry、resume、recovery planning capability
3. 固定 unsupported capability diagnostics

验收标准：

1. 有正负例 direct regression
2. unsupported capability 不会被错误执行为 provider write
3. diagnostics 区分 adapter config error 与 provider capability gap

主要涉及文件：

- `src/`
- `tests/`

## [ ] Issue 06

标题：
定义 provider-neutral persistence id namespace 与 write intent model

目标：

1. 定义 real persistence id namespace 的 provider-neutral 规则
2. 定义 provider write intent，不直接写真实 object path / database key
3. 明确 fake persistence id 与 future real persistence id 的兼容关系

验收标准：

1. accepted path 有 deterministic provider-neutral write intent regression
2. blocked / deferred / rejected path 不生成 provider write intent
3. persistence id namespace 不依赖 wall clock、process id、host path 或 credential

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

## [ ] Issue 07

标题：
定义 provider write attempt preview / dry-run handoff

目标：

1. 增加 provider write attempt preview artifact
2. 让 contributor 能查看 provider-neutral write planning 是否成立
3. 明确 preview 不等于真实 provider write result

验收标准：

1. preview 只消费 provider-neutral config、capability matrix 与 `AdapterExecutionReceipt`
2. accepted path 形成 write attempt preview
3. non-accepted path 保持 non-mutating preview

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

## [ ] Issue 08

标题：
定义 retry / resume placeholder 与 failure attribution 分层

目标：

1. 定义 retry / resume placeholder，不暴露真实 retry token 或 resume token
2. 区分 provider capability error、write planning error 与 recovery planning error
3. 固定 failure attribution diagnostics

验收标准：

1. 有 accepted / rejected / capability-gap direct regression
2. placeholder 不包含 provider credential 或 secret material
3. diagnostics 不混淆 source response failure 与 provider planning failure

主要涉及文件：

- `src/`
- `tests/`

## [ ] Issue 09

标题：
定义 recovery handoff preview 与真实 recovery daemon ABI 的边界

目标：

1. 提供 provider-neutral recovery handoff preview
2. 明确 preview 不等于真实 recovery daemon ABI
3. 覆盖 retry unavailable、resume unavailable、manual review required 等 next-step recommendation

验收标准：

1. review / preview 只消费 provider-neutral machine artifacts
2. summary 与 machine artifact 一致
3. 不倒灌 provider payload、operator payload 或 telemetry

主要涉及文件：

- `include/ahfl/`
- `src/`
- `tests/`

## [ ] Issue 10

标题：
冻结 V0.21 compatibility contract

目标：

1. 定义 provider adapter config、capability matrix、write attempt preview 的 stable fields
2. 定义 provider-specific extension 的兼容规则
3. 明确 version bump 条件

验收标准：

1. compatibility 文档列出 stable / unstable / forbidden fields
2. breaking-change 条件清晰
3. contributor guide 可引用该 contract

主要涉及文件：

- `docs/reference/`

## [ ] Issue 11

标题：
更新 native consumer matrix 与 contributor guide

目标：

1. 增加 V0.21 provider-neutral adapter consumer 行
2. 更新 contributor-facing 命令与回归标签
3. 明确 fake store、provider-neutral shim 与 future provider-specific adapter 的边界

验收标准：

1. `docs/README.md` 索引更新
2. README entry points 更新
3. contributor guide 覆盖 docs / code / golden / tests 同步流程

主要涉及文件：

- `README.md`
- `docs/README.md`
- `docs/reference/`

## [ ] Issue 12

标题：
建立 V0.21 regression、CI 与 migration docs 闭环

目标：

1. 新增 `ahfl-v0.21` 与专用标签
2. CI 显式执行 V0.21 dev / ASan 切片
3. 更新 migration notes

验收标准：

1. `ctest --preset test-dev --output-on-failure -L ahfl-v0.21` 可用
2. `ctest --preset test-asan --output-on-failure -L ahfl-v0.21` 可用
3. `MIGRATION.md` 说明 V0.20 到 V0.21 的边界变化

主要涉及文件：

- `.github/workflows/ci.yml`
- `tests/cmake/`
- `MIGRATION.md`
