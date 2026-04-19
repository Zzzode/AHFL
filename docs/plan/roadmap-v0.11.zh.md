# AHFL V0.11 Roadmap

本文给出 AHFL 在 V0.10 scheduler prototype / checkpoint-friendly snapshot 闭环完成之后的下一阶段实施路线。V0.11 的重点不是直接交付 durable checkpoint store、crash recovery 或 production resume runtime，而是把已经稳定的 `ExecutionPlan` / `RuntimeSession` / `ExecutionJournal` / `ReplayView` / `SchedulerSnapshot` 继续推进为“可表达 persistable checkpoint record、可形成稳定 resume basis、可被 future persistence prototype 稳定消费”的下一层边界。

基线输入：

- [roadmap-v0.10.zh.md](./roadmap-v0.10.zh.md)（已完成基线）
- [issue-backlog-v0.10.zh.md](./issue-backlog-v0.10.zh.md)（已完成基线）
- [native-checkpoint-prototype-bootstrap-v0.11.zh.md](../design/native-checkpoint-prototype-bootstrap-v0.11.zh.md)
- [checkpoint-prototype-compatibility-v0.11.zh.md](../reference/checkpoint-prototype-compatibility-v0.11.zh.md)
- [native-consumer-matrix-v0.11.zh.md](../reference/native-consumer-matrix-v0.11.zh.md)
- [contributor-guide-v0.11.zh.md](../reference/contributor-guide-v0.11.zh.md)
- [native-scheduler-prototype-bootstrap-v0.10.zh.md](../design/native-scheduler-prototype-bootstrap-v0.10.zh.md)
- [scheduler-prototype-compatibility-v0.10.zh.md](../reference/scheduler-prototype-compatibility-v0.10.zh.md)
- [native-consumer-matrix-v0.10.zh.md](../reference/native-consumer-matrix-v0.10.zh.md)
- [contributor-guide-v0.10.zh.md](../reference/contributor-guide-v0.10.zh.md)
- [cli-commands-v0.10.zh.md](../reference/cli-commands-v0.10.zh.md)

当前实现状态：

1. V0.10 已完成 scheduler snapshot / review 的模型、validation、bootstrap、CLI/backend 输出、golden regression、compatibility、consumer matrix、contributor guide 与 CI 闭环。
2. 当前仓库已经能跑通 `package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> scheduler review` 的本地 deterministic 路径。
3. 当前仓库也已经能稳定跑通 `package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint record -> checkpoint review` 的 V0.11 本地 deterministic 路径。
4. 当前 checkpoint-facing 语义已冻结为 `CheckpointRecord` / `CheckpointReviewSummary` 两层 artifact，并具备 direct validation、bootstrap、CLI/backend 输出、compatibility、consumer matrix、contributor guide 与 golden regression 入口。
5. 当前阶段仍明确停留在 local/offline checkpoint ABI prototype，不进入 durable checkpoint store、resume protocol、crash recovery、distributed scheduler、真实 connector、host telemetry 或 recovery daemon 领域。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

## 目标

V0.11 的交付目标是：

1. 冻结 future persistence prototype 的最小输入 / 输出边界，使其能稳定消费 V0.10 artifact，而不必回退依赖 AST、trace、scheduler review 或私有脚本。
2. 引入最小 `CheckpointRecord` 语义，稳定表达 persistable prefix、checkpoint identity、resume basis、resume blockers 与 local/durable boundary。
3. 让 single-file、project、workspace 与 `--package` 路径都能生成 deterministic checkpoint prototype 输出。
4. 冻结 checkpoint-facing compatibility、consumer matrix 与 contributor guidance，明确 checkpoint prototype、scheduler review 与 future recovery protocol 的职责边界。
5. 保持 V0.11 仍然是 local/offline checkpoint ABI prototype，而不是 production persistence / recovery 平台。

## 非目标

V0.11 仍然不直接承诺：

1. durable checkpoint store、数据库 schema、object storage layout、replication 或 queue durability
2. 真实 resume protocol、crash recovery、replay recovery daemon、distributed worker restart 或 failover orchestration
3. 真实 capability connector、provider SDK、secret、endpoint、tenant、region 或 deployment metadata
4. host telemetry、wall clock metrics、machine id、provider payload、event ingestion 或 SIEM integration
5. 真正的 runtime resume executor、statement-level interpreter、retry/backoff、timeout、cancellation propagation 或 preemption

## 里程碑

### M0 V0.11 Scope、Checkpoint Record 与 Resume Boundary 冻结

状态：

- [x] Issue 01 冻结 V0.11 checkpoint prototype scope 与 non-goals
- [x] Issue 02 冻结 checkpoint record / resume basis 的最小模型与 durable-adjacent 边界
- [x] Issue 03 冻结 checkpoint prototype、scheduler artifact 与 future recovery protocol 的分层关系

目标：

1. 明确 V0.11 做的是 local checkpoint prototype，而不是 durable store 或 production recovery。
2. 明确 checkpoint 语义必须建立在 `plan -> session -> journal -> replay -> snapshot` 之上，而不是直接读取 review、trace、AST 或 host log。
3. 明确 checkpoint record、checkpoint review summary 与 future recovery ABI 的职责拆分。

### M1 Checkpoint Record Model 与 Validation

状态：

- [x] Issue 04 引入 checkpoint record 数据模型
- [x] Issue 05 增加 checkpoint record validation 与 direct regression
- [x] Issue 06 增加 deterministic checkpoint record bootstrap

目标：

1. 让仓库可以稳定表达 persistable prefix、checkpoint identity、resume-ready 状态与 resume blocker。
2. 让 checkpoint-facing failure 可以区分输入 artifact 错误、record model 错误与 prototype bootstrap 错误。
3. 让 checkpoint bootstrap 只依赖 V0.10 已冻结 artifact，而不是 CLI 文本或私有脚本约定。

### M2 Checkpoint Prototype Output 与 Review Surface

状态：

- [x] Issue 07 增加 checkpoint record 输出路径
- [x] Issue 08 增加 checkpoint review / resume preview surface
- [x] Issue 09 增加 single-file / project / workspace checkpoint golden 回归

目标：

1. 让 contributor / persistence prototype consumer 可直接查看 checkpoint-facing 状态，而不必拼接多份 artifact。
2. 让 checkpoint-facing review 输出覆盖 persistable prefix、resume basis、resume blocker 与 local/durable boundary summary。
3. 让成功路径、partial 路径与 failure-path 都能稳定生成 checkpoint prototype 输出。

### M3 Compatibility、Consumer Matrix 与 Recovery Boundary Guidance

状态：

- [x] Issue 10 冻结 checkpoint record / review compatibility 契约
- [x] Issue 11 更新 native consumer matrix、contributor guide 与 future recovery boundary guidance

目标：

1. 让 checkpoint-facing artifact 的格式演进不依赖隐含约定。
2. 让 future persistence prototype / recovery explorer 清楚哪些字段可稳定依赖。
3. 把 checkpoint-facing artifact 纳入统一扩展模板，避免在 CLI 或外部脚本中重建私有 checkpoint 状态机。

### M4 工程化收口

状态：

- [x] Issue 12 建立 V0.11 checkpoint prototype regression、CI 与 reference 文档闭环

目标：

1. 让 checkpoint record、compatibility、consumer matrix 与 contributor guidance 进入持续回归。
2. 让新贡献者能按文档跑通“package -> execution plan -> runtime session -> execution journal -> replay -> scheduler snapshot -> checkpoint prototype”的 V0.11 路径。

## 关键依赖顺序

1. 先完成 M0，再引入 checkpoint-facing 模型
2. 先稳定 checkpoint record，再扩 reviewer-facing checkpoint summary
3. 先冻结 compatibility / consumer matrix，再承诺 checkpoint prototype 的长期消费
4. M4 只做收口，不替代前面阶段的边界设计

## 不建议跳步

1. 不要在 checkpoint-facing artifact 未冻结前直接实现私有 durable store / recovery 协议
2. 不要把真实 host log、provider payload、deployment telemetry 塞进 checkpoint record / review 输出
3. 不要让 checkpoint prototype 回退读取 scheduler review、trace、AST、raw source 或 host log 猜运行语义
4. 不要把 V0.11 的实现目标偷换成 production persistence / recovery 平台开发

## 当前状态

V0.10 已完成并成为 V0.11 的正式上游基线；当前 `Issue 01-12` 已全部完成：`docs/design/native-checkpoint-prototype-bootstrap-v0.11.zh.md` 已冻结 V0.11 checkpoint prototype 的 scope、non-goals、上游输入链与 checkpoint / review / recovery layering，`docs/reference/checkpoint-prototype-compatibility-v0.11.zh.md` 已进一步收口为逐 artifact compatibility contract，正式冻结 `CheckpointRecord` 与 `CheckpointReviewSummary` 的版本入口、source artifact 校验顺序、兼容扩展 / breaking change 规则，以及 docs / code / golden / tests 的同步流程，而 `docs/reference/native-consumer-matrix-v0.11.zh.md` 与 `docs/reference/contributor-guide-v0.11.zh.md` 也已把 checkpoint-facing consumer matrix、推荐消费顺序、最小验证清单与 future recovery boundary guidance 统一收口到 `plan -> session -> journal -> replay -> snapshot -> checkpoint -> checkpoint-review`。本次又把 `.github/workflows/ci.yml` 显式补上 `ahfl-v0.11` 标签切片，使 checkpoint prototype 在常规 CI 与 ASan CI 中都先执行 V0.11 定向回归，再继续全量测试；`README.md`、roadmap 与 backlog 也已同步到最终闭环状态。V0.11 现已完成工程化收口，后续若继续推进，建议直接开启下一版 roadmap，而不是继续在未立项前静默扩张 checkpoint ABI。

## 对应 backlog

详细 issue-ready 任务清单见：

- [issue-backlog-v0.11.zh.md](./issue-backlog-v0.11.zh.md)
