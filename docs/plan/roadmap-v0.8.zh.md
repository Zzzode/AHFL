# AHFL V0.8 Roadmap

本文给出 AHFL 在 V0.7 runtime session / execution journal bootstrap 完成之后的下一阶段实施路线。V0.8 的重点不是直接交付生产 scheduler，而是把已经稳定的 runtime artifact 继续推进为“可 replay 验证、可 audit 审查、可承接 failure-path 扩展”的 consumer-facing 边界。

基线输入：

- [roadmap-v0.7.zh.md](./roadmap-v0.7.zh.md)（已完成基线）
- [issue-backlog-v0.7.zh.md](./issue-backlog-v0.7.zh.md)（已完成基线）
- [native-runtime-session-bootstrap-v0.7.zh.md](../design/native-runtime-session-bootstrap-v0.7.zh.md)
- [native-execution-journal-v0.7.zh.md](../design/native-execution-journal-v0.7.zh.md)
- [native-replay-audit-bootstrap-v0.8.zh.md](../design/native-replay-audit-bootstrap-v0.8.zh.md)
- [runtime-session-compatibility-v0.7.zh.md](../reference/runtime-session-compatibility-v0.7.zh.md)
- [execution-journal-compatibility-v0.7.zh.md](../reference/execution-journal-compatibility-v0.7.zh.md)
- [replay-view-compatibility-v0.8.zh.md](../reference/replay-view-compatibility-v0.8.zh.md)
- [audit-report-compatibility-v0.8.zh.md](../reference/audit-report-compatibility-v0.8.zh.md)
- [native-consumer-matrix-v0.8.zh.md](../reference/native-consumer-matrix-v0.8.zh.md)
- [contributor-guide-v0.8.zh.md](../reference/contributor-guide-v0.8.zh.md)

当前实现状态：

1. V0.7 已完成 runtime session、execution journal、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能跑通 `package -> execution plan -> runtime session -> execution journal -> dry-run trace` 的本地 deterministic 路径。
3. 当前 replay / audit consumer 仍未形成正式 artifact，review 侧仍主要依赖 trace 与 golden。
4. 当前 journal 仅覆盖 success-path event sequence，尚未冻结 failure-path 事件。
5. 当前阶段仍未进入生产 runtime launcher、distributed scheduler、persistence 或真实 connector 实现。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.8 的交付目标是：

1. 冻结最小 replay consumer 边界，使 journal 能稳定投影为 replay-ready state progression。
2. 新增 audit consumer 视图，使 plan / session / journal / trace 的审查语义可归档、可 CI 回归。
3. 为 failure-path journal 扩展建立兼容演进规则。
4. 保持 V0.8 仍然是 local/offline consumer bootstrap，而不是生产 scheduler / observability 平台。

## 非目标

V0.8 仍然不直接承诺：

1. 生产 runtime launcher、distributed scheduler、host daemon 或 worker orchestration
2. 真实 capability connector、provider SDK、secret、endpoint、region 或 tenant 配置
3. persistence、checkpoint / resume、crash recovery 或 deployment pipeline
4. wall clock metrics、telemetry、host log ingestion 或 provider payload capture
5. 真正的并发调度、retry、timeout、backoff 或 statement-level interpreter

## 里程碑

### M0 V0.8 Scope 与 Replay / Audit 边界冻结

状态：

- [x] Issue 01 冻结 V0.8 replay / audit scope 与 scheduler 非目标
- [x] Issue 02 冻结 Replay View 的最小模型与一致性边界
- [x] Issue 03 冻结 Audit Report 的最小字段集合与 failure-path 扩展原则

目标：

1. 明确 V0.8 做的是 replay / audit consumer bootstrap，而不是生产 scheduler。
2. 明确 replay / audit 与 session / journal / trace 的职责拆分。
3. 明确 failure-path 扩展先落在 journal compatibility，而不是 host log integration。

### M1 Replay Consumer Model 与 Validation

状态：

- [x] Issue 04 引入 Replay View 数据模型
- [x] Issue 05 增加 replay consistency validation 与 direct regression
- [x] Issue 06 增加 replay 输出路径

目标：

1. 让仓库可基于 journal + session 生成 deterministic replay 视图。
2. 让 replay failure 可以区分输入 artifact 错误与 consumer consistency 错误。
3. 让 single-file、project、workspace 与 `--package` 路径都能输出一致 replay 结果。

### M2 Audit Consumer Bootstrap

状态：

- [x] Issue 07 引入 Audit Report 数据模型
- [x] Issue 08 增加 plan / session / journal / trace 到 audit report 的 bootstrap
- [x] Issue 09 增加 audit 输出与 golden 回归

目标：

1. 让 contributor / CI 可消费稳定的 audit report，而不必拼接多份 artifact。
2. 让 audit report 覆盖规划、状态、事件与 reviewer-facing 摘要。
3. 让成功路径与 failure 分类都能被稳定审阅。

### M3 Failure-Path 与 Consumer 契约收敛

状态：

- [x] Issue 10 冻结 replay / audit compatibility 契约与 failure-path 演进规则
- [x] Issue 11 更新 native consumer matrix 与 contributor guide

目标：

1. 让 replay / audit 的格式演进不依赖隐含约定。
2. 让 future scheduler / audit tooling 清楚哪些字段可稳定依赖。
3. 为 V0.9 partial session / richer scheduler prototype 预留兼容扩展口。

### M4 工程化收口

状态：

- [x] Issue 12 建立 V0.8 replay / audit regression、CI 与 reference 文档闭环

目标：

1. 让 replay、audit、compatibility 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> session -> journal -> replay/audit”的 V0.8 路径。

## 关键依赖顺序

1. 先完成 M0，再实现 replay model
2. 先稳定 replay，再引入 audit report
3. 先冻结 compatibility / consumer matrix，再承诺 failure-path 长期消费
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 replay / audit artifact 未冻结前直接实现私有 scheduler 结构
2. 不要把真实 connector、deployment、host log 或 telemetry 塞进 replay / audit
3. 不要让 replay / audit 回退读取 AST、raw source 或 project descriptor 猜运行语义
4. 不要把 V0.8 的实现目标偷换成生产 observability / runtime 平台开发

## 当前状态

V0.8 已完成 M0-M4：当前仓库已补齐 `ReplayView` / `AuditReport` 的模型、validation、bootstrap、CLI/backend 输出、golden regression、CI 标签切片、compatibility 契约、consumer matrix、contributor guide 与 reference 文档闭环。下一步若继续推进，适合规划 V0.9 failure-path journal / partial session / richer scheduler prototype 边界。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.8.zh.md](./issue-backlog-v0.8.zh.md)
