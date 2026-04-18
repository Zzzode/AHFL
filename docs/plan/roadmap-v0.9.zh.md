# AHFL V0.9 Roadmap

本文给出 AHFL 在 V0.8 replay / audit consumer bootstrap 完成之后的下一阶段实施路线。V0.9 的重点不是直接交付生产 scheduler，而是把当前仅覆盖 success-path 的 runtime-adjacent artifact 继续推进为“可表达 partial session、可承接 failure-path journal、可供 richer scheduler prototype / replay / audit 稳定消费”的下一层边界。

基线输入：

- [roadmap-v0.8.zh.md](./roadmap-v0.8.zh.md)（已完成基线）
- [issue-backlog-v0.8.zh.md](./issue-backlog-v0.8.zh.md)（已完成基线）
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

1. V0.8 已完成 replay view、audit report、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能跑通 `package -> execution plan -> runtime session -> execution journal -> replay / audit` 的本地 deterministic 路径。
3. 当前 `RuntimeSession` 仍只有 success-path 状态：workflow 只有 `Completed`，node 只有 `Blocked / Ready / Completed`。
4. 当前 `ExecutionJournal` 仍只有 success-path 事件：`session_started`、`node_became_ready`、`node_started`、`node_completed`、`workflow_completed`。
5. 当前 replay / audit 只能稳定消费 success-path 语义，failure-path 仍停留在 compatibility 预留口与 dry-run 层的局部诊断。
6. 当前阶段仍未进入生产 runtime launcher、distributed scheduler、persistence、checkpoint / resume、真实 connector 或 host log ingestion。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.9 的交付目标是：

1. 冻结 partial runtime session 的最小状态集合，使失败或未完成执行也能形成稳定 state snapshot。
2. 新增 failure-path execution journal 事件族，使 journal 能稳定表达 node / workflow failure progression。
3. 让 replay / audit 正式消费 failure-path，并能区分 input artifact error、runtime bootstrap failure 与 replay consistency failure。
4. 为 richer local scheduler prototype 提供更真实但仍 deterministic 的输入边界。
5. 保持 V0.9 仍然是 local/offline runtime-adjacent bootstrap，而不是生产 scheduler / observability / persistence 系统。

## 非目标

V0.9 仍然不直接承诺：

1. 生产 runtime launcher、host daemon、distributed worker orchestration 或 multi-tenant control plane
2. 真实 capability connector、provider SDK、secret、endpoint、region、deployment 或 tenant 配置
3. persistence store、checkpoint / resume、crash recovery、durable queue 或 recovery protocol
4. wall clock metrics、telemetry、host log payload、provider request/response capture 或 SIEM integration
5. 真正的并发调度、retry、timeout、backoff、cancellation propagation 或 statement-level interpreter

## 里程碑

### M0 V0.9 Scope、Failure Taxonomy 与 Partial Session 边界冻结

状态：

- [ ] Issue 01 冻结 V0.9 failure-path / partial session scope 与 scheduler prototype 非目标
- [ ] Issue 02 冻结 partial runtime session 的最小模型与 failure taxonomy
- [ ] Issue 03 冻结 failure-path journal / replay / audit 的分层与兼容边界

目标：

1. 明确 V0.9 做的是“partial session + failure-path journal + replay/audit 扩展”，不是“生产 scheduler implementation”。
2. 明确 failure-path 首先落在 session / journal，而不是直接把失败结论硬塞进 replay / audit。
3. 明确 richer scheduler prototype 仍只能消费 plan / session / journal / replay 这条链路，而不是直接读取 AST、trace 或 host log。

### M1 Partial Runtime Session Model 与 Bootstrap

状态：

- [ ] Issue 04 扩展 Runtime Session 数据模型与 validation 到 partial / failed 语义
- [ ] Issue 05 增加 deterministic partial session bootstrap 与 failure 分类
- [ ] Issue 06 增加 partial session 输出路径与 golden 回归

目标：

1. 让 session 可以稳定表达 workflow 未完成、node 失败、node 未启动或被阻断等状态。
2. 让 session failure 能区分 plan 错误、mock / request 错误、bootstrap 构造错误与运行结果失败。
3. 让 single-file、project、workspace 与 `--package` 路径都能输出一致 partial session 结果。

### M2 Failure-Path Journal / Replay / Audit Bootstrap

状态：

- [ ] Issue 07 扩展 execution journal 数据模型与 validation 到 failure-path
- [ ] Issue 08 增加 failure-path journal bootstrap 与 replay / audit 消费升级
- [ ] Issue 09 增加 failure-path replay / audit 输出与 golden 回归

目标：

1. 让 journal 可稳定表达 `node_failed`、`workflow_failed`、`mock_missing` 或等价最小失败事件。
2. 让 replay 可基于 partial session + failure-path journal 生成 deterministic progression 与 consistency 结论。
3. 让 audit report 可稳定归档成功路径与失败路径的统一审查摘要。

### M3 Compatibility、Consumer Matrix 与 Scheduler Prototype Guidance

状态：

- [ ] Issue 10 冻结 partial session / failure-path journal / replay / audit compatibility 契约
- [ ] Issue 11 更新 native consumer matrix、contributor guide 与 richer scheduler prototype guidance

目标：

1. 让 failure-path 的格式演进不依赖隐含约定。
2. 让 future scheduler prototype / replay verifier / audit tooling 清楚哪些字段可稳定依赖。
3. 把 partial session / failure-path journal 纳入统一扩展模板，避免在 CLI 或外部脚本中重建私有语义。

### M4 工程化收口

状态：

- [ ] Issue 12 建立 V0.9 partial session / failure-path regression、CI 与 reference 文档闭环

目标：

1. 让 partial session、failure-path journal、replay / audit compatibility 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> partial session -> failure-path journal -> replay / audit”的 V0.9 路径。

## 关键依赖顺序

1. 先完成 M0，再扩展 partial session 模型
2. 先稳定 partial session，再增加 failure-path journal
3. 先冻结 session / journal failure 语义，再扩 replay / audit 结论
4. 先冻结 compatibility / consumer matrix，再承诺 richer scheduler prototype 的长期消费
5. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 partial session artifact 未冻结前直接实现 scheduler 私有失败状态机
2. 不要把真实 host log、provider payload、deployment telemetry 塞进 session / journal / replay / audit
3. 不要让 failure-path consumer 回退读取 AST、raw source、project descriptor 或 trace 猜运行语义
4. 不要把 V0.9 的实现目标偷换成 production runtime / persistence / recovery 平台开发

## 当前状态

V0.9 尚未开始实现：当前仓库的 next step 已从“补 success-path replay / audit”转到“把 failure-path 与 partial session 做成正式 artifact”。建议优先顺序是：

1. 先冻结 partial session / failure taxonomy 文档边界
2. 再扩 Runtime Session / Execution Journal 模型与 direct regression
3. 再把 replay / audit 升级到 failure-path
4. 最后补 compatibility、matrix、guide、golden 与 CI 收口

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.9.zh.md](./issue-backlog-v0.9.zh.md)
