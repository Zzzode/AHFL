# AHFL V0.10 Roadmap

本文给出 AHFL 在 V0.9 partial session / failure-path artifact 闭环完成之后的下一阶段实施路线。V0.10 的重点不是直接交付生产 scheduler 或 durable persistence，而是把已经稳定的 `ExecutionPlan` / `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `AuditReport` 继续推进为“可表达 richer scheduler prototype state、可形成 checkpoint-friendly 本地快照、可被 future prototype 稳定消费”的下一层边界。

基线输入：

- [roadmap-v0.9.zh.md](./roadmap-v0.9.zh.md)（已完成基线）
- [issue-backlog-v0.9.zh.md](./issue-backlog-v0.9.zh.md)（已完成基线）
- [native-partial-session-failure-bootstrap-v0.9.zh.md](../design/native-partial-session-failure-bootstrap-v0.9.zh.md)
- [runtime-session-compatibility-v0.9.zh.md](../reference/runtime-session-compatibility-v0.9.zh.md)
- [execution-journal-compatibility-v0.9.zh.md](../reference/execution-journal-compatibility-v0.9.zh.md)
- [replay-view-compatibility-v0.9.zh.md](../reference/replay-view-compatibility-v0.9.zh.md)
- [audit-report-compatibility-v0.9.zh.md](../reference/audit-report-compatibility-v0.9.zh.md)
- [failure-path-compatibility-v0.9.zh.md](../reference/failure-path-compatibility-v0.9.zh.md)
- [scheduler-prototype-compatibility-v0.10.zh.md](../reference/scheduler-prototype-compatibility-v0.10.zh.md)
- [native-consumer-matrix-v0.9.zh.md](../reference/native-consumer-matrix-v0.9.zh.md)
- [contributor-guide-v0.9.zh.md](../reference/contributor-guide-v0.9.zh.md)

当前实现状态：

1. V0.9 已完成 partial runtime session、failure-path execution journal、failure-aware replay / audit 的模型、validation、bootstrap、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能跑通 `package -> execution plan -> runtime session -> execution journal -> replay / audit` 的本地 deterministic 路径。
3. 当前 richer scheduler prototype 仍主要停留在 guidance 层，还没有单独冻结的 scheduler-facing artifact、state machine 或 checkpoint-friendly 快照模型。
4. 当前仓库尚未提供正式的 ready-set / blocked-reason / next-step decision / local resume token 等 prototype 语义入口。
5. 当前阶段仍未进入生产 runtime launcher、distributed scheduler、durable persistence、checkpoint storage、crash recovery、真实 connector 或 host log ingestion。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.10 的交付目标是：

1. 冻结 richer scheduler prototype 的最小输入 / 输出边界，使其能稳定消费 V0.9 artifact，而不必回退依赖 AST、trace 或私有脚本。
2. 引入最小 scheduler snapshot / cursor 语义，稳定表达 ready set、blocked reason、已执行前缀与 checkpoint-friendly 本地状态。
3. 让 single-file、project、workspace 与 `--package` 路径都能生成 deterministic scheduler prototype 输出。
4. 冻结 scheduler-facing compatibility、consumer matrix 与 contributor guidance，明确 prototype 与 future persistence 的职责边界。
5. 保持 V0.10 仍然是 local/offline scheduler prototype，而不是生产 runtime / persistence 平台。

## 非目标

V0.10 仍然不直接承诺：

1. 生产 runtime launcher、host daemon、distributed worker orchestration 或 multi-tenant control plane
2. durable persistence store、真实 checkpoint 文件格式、resume protocol、crash recovery 或 queue replication
3. 真实 capability connector、provider SDK、secret、endpoint、region、deployment 或 tenant 配置
4. wall clock metrics、telemetry、host log payload、provider request/response capture 或 SIEM integration
5. 真正的并发调度、retry、timeout、backoff、cancellation propagation、preemption 或 statement-level interpreter

## 里程碑

### M0 V0.10 Scope、Scheduler Prototype 与 Checkpoint-Friendly Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.10 richer scheduler prototype scope 与 non-goals
- [x] Issue 02 冻结 scheduler snapshot / cursor 的最小模型与 checkpoint-friendly 边界
- [x] Issue 03 冻结 scheduler prototype、runtime artifact 与 future persistence 的分层关系

目标：

1. 明确 V0.10 做的是 local richer scheduler prototype，而不是 durable persistence 或 production scheduler。
2. 明确 scheduler-facing 语义必须建立在 `plan -> session -> journal -> replay` 之上，而不是直接读取 trace、AST 或 host log。
3. 明确 checkpoint-friendly local snapshot 与真实 persistence / recovery protocol 的职责拆分。

### M1 Scheduler Snapshot Model 与 Validation

状态：

- [x] Issue 04 引入 scheduler snapshot / cursor 数据模型
- [x] Issue 05 增加 scheduler snapshot validation 与 direct regression
- [x] Issue 06 增加 deterministic scheduler snapshot bootstrap

目标：

1. 让仓库可以稳定表达 ready node、blocked reason、已完成前缀与 terminal workflow 决策。
2. 让 scheduler-facing failure 可以区分输入 artifact 错误、snapshot model 错误与 prototype bootstrap 错误。
3. 让 snapshot bootstrap 只依赖 V0.9 已冻结 artifact，而不是私有脚本约定。

### M2 Scheduler Prototype Output 与 Review Surface

状态：

- [x] Issue 07 增加 scheduler snapshot 输出路径
- [x] Issue 08 增加 scheduler decision summary / review surface
- [x] Issue 09 增加 single-file / project / workspace golden 回归

目标：

1. 让 contributor / prototype consumer 可直接查看 scheduler-facing 状态，而不必拼接多份 artifact。
2. 让 scheduler-facing review 输出覆盖 ready set、blocked reason、executed prefix 与 next-step summary。
3. 让成功路径、partial 路径与 failure-path 都能稳定生成 prototype 输出。

### M3 Compatibility、Consumer Matrix 与 Persistence Boundary Guidance

状态：

- [x] Issue 10 冻结 scheduler snapshot / decision summary compatibility 契约
- [x] Issue 11 更新 native consumer matrix、contributor guide 与 future persistence boundary guidance

目标：

1. 让 scheduler-facing artifact 的格式演进不依赖隐含约定。
2. 让 future scheduler prototype / persistence explorer 清楚哪些字段可稳定依赖。
3. 把 scheduler-facing artifact 纳入统一扩展模板，避免在 CLI 或外部脚本中重建私有状态机。

### M4 工程化收口

状态：

- [x] Issue 12 建立 V0.10 scheduler prototype regression、CI 与 reference 文档闭环

目标：

1. 让 scheduler snapshot、compatibility、consumer matrix 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> runtime session -> execution journal -> replay -> scheduler prototype”的 V0.10 路径。

## 关键依赖顺序

1. 先完成 M0，再引入 scheduler-facing 模型
2. 先稳定 scheduler snapshot，再扩 reviewer-facing summary
3. 先冻结 compatibility / consumer matrix，再承诺 checkpoint-friendly prototype 的长期消费
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 scheduler-facing artifact 未冻结前直接实现私有 resume / persistence 结构
2. 不要把真实 host log、provider payload、deployment telemetry 塞进 scheduler snapshot / review 输出
3. 不要让 scheduler prototype 回退读取 AST、raw source、project descriptor 或 trace 猜运行语义
4. 不要把 V0.10 的实现目标偷换成 production runtime / persistence / recovery 平台开发

## 当前状态

V0.10 已完成 M0-M4：当前仓库已补齐 scheduler snapshot / review 的模型、validation、bootstrap、CLI/backend 输出、golden regression、compatibility 契约、consumer matrix、contributor guide、README / docs index、CLI command reference 与 CI 标签切片闭环。当前推荐消费顺序已稳定冻结为 `plan -> session -> journal -> replay -> snapshot -> review`；下一步若继续推进，更适合规划 V0.11 或 future persistence / checkpoint ABI 的下一阶段边界，而不是继续在 V0.10 内扩张私有 scheduler 语义。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.10.zh.md](./issue-backlog-v0.10.zh.md)
