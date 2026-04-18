# AHFL V0.7 Roadmap

本文给出 AHFL 在 V0.6 execution plan / local dry-run bootstrap 完成之后的下一阶段实施路线。V0.7 的重点不是直接交付生产 launcher，而是把已经稳定的 planning / dry-run 输入进一步推进为“可表达 runtime session state、可记录 execution journal、可供 future scheduler / replay / audit consumer 复用”的 local runtime bootstrap 边界。

基线输入：

- [roadmap-v0.6.zh.md](./roadmap-v0.6.zh.md)（已完成基线）
- [issue-backlog-v0.6.zh.md](./issue-backlog-v0.6.zh.md)（已完成基线）
- [native-runtime-session-bootstrap-v0.7.zh.md](../design/native-runtime-session-bootstrap-v0.7.zh.md)
- [native-execution-journal-v0.7.zh.md](../design/native-execution-journal-v0.7.zh.md)
- [native-execution-plan-architecture-v0.6.zh.md](../design/native-execution-plan-architecture-v0.6.zh.md)
- [native-dry-run-bootstrap-v0.6.zh.md](../design/native-dry-run-bootstrap-v0.6.zh.md)
- [execution-plan-compatibility-v0.6.zh.md](../reference/execution-plan-compatibility-v0.6.zh.md)
- [dry-run-trace-compatibility-v0.6.zh.md](../reference/dry-run-trace-compatibility-v0.6.zh.md)
- [native-consumer-matrix-v0.6.zh.md](../reference/native-consumer-matrix-v0.6.zh.md)
- [contributor-guide-v0.6.zh.md](../reference/contributor-guide-v0.6.zh.md)

当前实现状态：

1. V0.6 已完成 execution plan、mock capability、local dry-run trace、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能跑通 `package -> execution plan -> capability mock -> dry-run trace` 的本地 deterministic 路径。
3. 当前 dry-run 仍然是 review-oriented summary：它可表达顺序、依赖与 mock 使用，但还没有正式 runtime session state surface。
4. 当前仓库仍没有 scheduler-ready session snapshot，也没有 replay / audit 可复用的 execution journal。
5. 当前阶段仍未进入生产 runtime launcher、真实 capability connector、deployment、tenant、secret、region、parallel scheduling 或 persistence 实现。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.7 的交付目标是：

1. 冻结最小 Runtime Session artifact，使 execution plan 可投影为 scheduler-ready state surface。
2. 新增 step-wise execution journal，使本地运行可输出稳定 event sequence。
3. 保持 V0.7 仍然是 local runtime bootstrap，而不是生产 launcher / distributed scheduler。
4. 继续保持 handoff / plan / dry-run trace / runtime session / journal 的职责拆分。

## 非目标

V0.7 仍然不直接承诺：

1. 生产 runtime launcher、host daemon 或 distributed worker
2. 真实 capability connector、provider SDK、secret、endpoint、region 或 tenant 配置
3. deployment、publish、registry 或 package distribution
4. parallel scheduling、retry、timeout、persistence、checkpoint / resume
5. 完整 statement interpreter 或真实 agent state machine execution

## 里程碑

### M0 V0.7 Scope 与 Runtime Session 边界冻结

状态：

- [x] Issue 01 冻结 V0.7 runtime session / execution journal scope 与 runtime 非目标
- [x] Issue 02 冻结 Runtime Session artifact 的最小模型与版本边界
- [x] Issue 03 冻结 Execution Journal 的最小事件集合与 replay 边界

目标：

1. 明确 V0.7 做的是“runtime session + execution journal bootstrap”，不是“生产 runtime implementation”。
2. 明确 session / journal 与 execution plan、dry-run trace、future scheduler / replay consumer 的职责拆分。
3. 明确 journal 记录哪些事件，哪些仍属于 future launcher / host integration。

### M1 Runtime Session Model 与 Validation

状态：

- [x] Issue 04 引入 Runtime Session 数据模型
- [x] Issue 05 增加 Runtime Session validation 与 direct regression
- [x] Issue 06 增加 session snapshot 输出路径

目标：

1. 把 execution plan 的 node order / dependency / binding surface 提升为 session state。
2. 让 session failure 可以区分 plan 错误、session construction 错误与 mock 输入错误。
3. 让 single-file、project、workspace 与 `--package` 路径都能输出一致 session snapshot。

### M2 Execution Journal Bootstrap

状态：

- [x] Issue 07 引入 execution journal 数据模型
- [x] Issue 08 增加 step-wise scheduler bootstrap 与 journal event 生成
- [x] Issue 09 增加 journal 输出与 golden 回归

目标：

1. 让仓库内可以基于 session 输出稳定 journal。
2. 让 journal 表达 node ready / start / complete 的 deterministic event sequence。
3. 让 replay / audit consumer 不再只能依赖 contributor-facing dry-run trace。

### M3 Compatibility、Consumer Matrix 与扩展路径

状态：

- [x] Issue 10 冻结 Runtime Session / Execution Journal compatibility 契约
- [x] Issue 11 更新 runtime-adjacent consumer matrix 与 contributor 扩展模板

目标：

1. 让 session 与 journal 的格式演进不依赖隐含约定。
2. 让 future scheduler / replay / audit consumer 清楚哪些字段可稳定依赖。
3. 把 session / journal 纳入 backend / consumer 扩展规则。

### M4 工程化收口

状态：

- [x] Issue 12 建立 V0.7 session / journal regression、CI 与 reference 文档闭环

目标：

1. 让 session、journal、compatibility 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> session -> journal -> trace/review”的 V0.7 路径。

## 关键依赖顺序

1. 先完成 M0，再实现 session 模型
2. 先稳定 session，再增加 journal
3. 先冻结 journal 事件边界，再承诺 replay consumer
4. 先冻结 compatibility / consumer matrix，再对后续 runtime work 承诺长期消费
5. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 Runtime Session artifact 未冻结前直接实现 scheduler 私有结构
2. 不要把真实 connector / secret / endpoint 配置塞进 session / journal
3. 不要让 session / journal 回退读取 AST、raw source 或 project descriptor 猜执行语义
4. 不要把 V0.7 的实现目标偷换成生产 runtime / scheduler / launcher 开发

## 当前状态

V0.7 已完成 M0-M4：当前仓库已补齐 runtime session / execution journal 的模型、validation、bootstrap、CLI 输出、compatibility 契约、consumer matrix、contributor guide 与 CI 闭环。下一步若继续推进，适合规划 V0.8 的 replay / audit consumer 或更真实的 scheduler prototype 边界。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.7.zh.md](./issue-backlog-v0.7.zh.md)
