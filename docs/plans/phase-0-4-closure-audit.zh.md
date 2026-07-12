# AHFL Phase 0–4 Prompt-to-Artifact Closure Audit

本文把 Phase 0–4 路线和 10 项 beta 条件映射到真实用户入口、实现路径、行为证据与剩余边界。源码存在、golden 更新、manifest 完整或单个 gate 绿色都不能单独证明完成。

## 状态口径

| 状态 | 含义 |
|------|------|
| 已闭环 | 用户入口可达，核心行为由真实执行验证，并有阻断式回归或 release evidence |
| Bounded 闭环 | CI 级受控试点已闭环，但长期或真实部署信心仍不足 |
| 未闭环 | 缺真实行为、持续证据、产品入口或明确验收 |
| 冻结 | 按产品合同暂不实现；scope gate 必须拒绝扩面 |

## Phase 0：产品合同与冻结

| 要求 | 用户入口 / Artifact | 实现与治理 | 行为证据 | 状态 |
|------|---------------------|------------|----------|------|
| RFC 0012 owner、shepherd、tracking issue | `docs/rfcs/0012-structured-workflow-execution-ux.zh.md` | frontmatter 固定 owner、shepherd、reviewer、issue #16 | `python3 scripts/check-rfc.py` | 已闭环 |
| 唯一 reference workflow | `examples/execution-demo` | RFC、beta contract、pilot contract 共享该路径 | `ahflc.run.profile_and_output_contract.smoke`、recovery、production matrix | 已闭环 |
| 冻结 CLI action / backend / artifact | `config/product-scope-freeze.json` | 删除允许，新增必须先接受 RFC | `ahfl.product.scope_freeze`、`scope_freeze_smoke` | 已闭环 |
| 10 项 beta gate | `config/beta-gate.json` | criterion-specific evidence；从空目录重建；同 revision；拒绝 stale evidence | `ahfl-beta-gate` label | 已闭环 |
| 示例 secret contract | `examples/execution-demo/llm_config.example.json` | `api_key_secret` + env handle；无 inline key | `ahfl.security.execution_demo_secret` | 已闭环 |

## Phase 1：Runtime Kernel

| 要求 | 实现路径 | 行为证据 | 状态 |
|------|----------|----------|------|
| Strong numeric identity | `include/ahfl/runtime/execution_event.hpp`、`ExecutionMetadataStore` | runtime identity evidence、architecture gate、runtime tests | 已闭环 |
| Flat canonical event store | `ExecutionEventSink -> ExecutionEventStore -> WorkflowResult.events` | event/report/projection/renderer tests | 已闭环 |
| `std::variant` event model | `ExecutionEventPayload` | compile-time exhaustive visitors + event taxonomy tests | 已闭环 |
| Human / JSON / JSONL / quiet | `execution_renderer.cpp` | renderer unit、run profile smoke | 已闭环 |
| Replay / audit / scheduler / checkpoint | `execution_projection.cpp` | projection tests；beta runtime evidence | 已闭环 |
| Provider usage / cache / fallback / policy | `CapabilityUsageRecorded`、`CapabilityCompleted.cache_hit`、`ProviderDegraded` | `ahflc.run.llm_provider_runtime.smoke` | 已闭环 |
| Actionable runtime diagnostics | failure event materializes `DiagnosticId` 对应 code/message/range/related notes | failure matrix + renderer test + call-site range test | 已闭环 |
| Lifecycle success/skip/retry/fallback/cancel/budget/timeout/interruption/resume | `WorkflowRuntime` | lifecycle evidence、workflow runtime tests、provider runtime smoke | 已闭环 |
| Started / terminal invariant | `validate_execution_events` | missing/duplicate/orphan terminal 与 usage ordering negative tests | 已闭环 |
| 删除第二事实源 | 旧 session/journal/replay/audit/scheduler/checkpoint/persistence/provider artifact 链物理删除 | architecture gate 扫描 source/CMake/tests/docs；约 9.3 万行旧 proxy 删除 | 已闭环 |

Provider 内部 cache、streaming 和 secret lifecycle vectors 只用于 provider/secret 单测。它们不构成公开 runtime artifact，也不能绕过 `ExecutionEventStore` 生成第二套 provider audit 文件。

## Phase 2：语言与开发体验

| 要求 | 实现路径 | 行为证据 | 状态 |
|------|----------|----------|------|
| 顶层 AST declaration 使用 variant/value semantics | `include/ahfl/compiler/frontend/ast.hpp` | architecture gate、syntax/sema/IR/LSP consumers | 已闭环 |
| 删除 AST data inheritance / dynamic_cast | frontend 与 formatter 已迁移 | architecture gate + build | 已闭环 |
| Sema owners | `DeclarationSema`、`ExpressionSema`、`FlowWorkflowSema`、`ConstSema` | semantics owner gate + 语义 suites | 已闭环；更深状态解耦仍属后续架构优化 |
| ExprEffect 正式输入 | Typed HIR -> `ahfl.ir.v2` expression / flow summary -> assurance / SMV | typed HIR、IR、assurance、formal tests | 已闭环 |
| Nominal stdlib containers | `std/option.ahfl`、`result.ahfl`、`collections.ahfl` | stdlib container evidence + schema negatives | 已闭环 |
| 核心 stdlib | Option/Result/List/Map/Set/String/JSON/Time/UUID modules | stdlib unit/bridge/evaluator tests | 已闭环 |
| Formatter lossless/idempotent | `formatter_api.cpp` + lossless formatter | blocking format CI、std/ + fixtures、formatter evidence | 已闭环 |
| LSP 深化边界 | TypedProgram/project-aware snapshot 已存在；更深 condition-fact completion/hover/signatureHelp 未纳入 beta | LSP handler/extension tests | 基线已实现，深化仍是 P1 |

## Phase 3：受控生产试点

| 要求 | 实现 / Evidence | 当前边界 | 状态 |
|------|-----------------|----------|------|
| 真实 provider / capability adapter | local OpenAI-compatible HTTP server + `LLMCapabilityProvider` | 无公网依赖；真实 HTTP、auth、streaming、usage 协议路径 | Bounded 闭环 |
| Durable store | `WorkflowRecoveryStore` atomic replace + persistent response cache | local filesystem durability，不代表云对象存储/数据库 | Bounded 闭环 |
| Crash/restart/replay | worker `SIGKILL`、checkpoint load、`NodeRestored` | 单机进程恢复 | Bounded 闭环 |
| Idempotency / side-effect dedupe | persistent capability receipt cache；resume 外部请求计数不增加 | reference workflow 一种副作用模型 | Bounded 闭环 |
| Human approval | `ahfl.operator-approval.v1`；无批准不调用 provider | file-backed approval fixture | Bounded 闭环 |
| Fault injection | disconnect、429、timeout、partial response、process crash | 本地 deterministic fault server | Bounded 闭环 |
| Token/cost/latency budget | canonical usage event；warn/fail；`BudgetRejected` terminal lifecycle | provider runtime smoke + controlled-pilot dependency | Bounded 闭环 |
| OTel adapter | canonical events -> OTLP-compatible JSON spans | 当前不包含 SDK/collector transport | Bounded 闭环 |
| CLI/sysroot/VSIX/version migration | clean-prefix install、platform VSIX isolated install、schema rejection | 未做公开 Marketplace 发布 | Beta 闭环 |
| Bounded soak | 30 秒下限、至少 12 次；event/request count稳定 | 非 hour-scale | Bounded 闭环 |
| Hour-scale soak / RSS / allocator trend | CI-only single-long-lived-worker harness、nightly workflow、GitHub Actions provenance 与 revision-bound evidence checker | 本地禁止运行 hour-scale；以 live `production-confidence` gate 判定，不在文档缓存 ready 状态 | Live gate |

正式合同：

- `config/controlled-pilot-gate.json`
- `build/release-evidence/pilot/reference-workflow-production-matrix.json`
- CTest label `ahfl-controlled-pilot`
- evidence schema `ahfl.controlled-pilot-evidence.v1`

gate 会验证 evidence schema、当前 source revision、soak threshold、四类网络故障、crash/recovery schema policy、OTel 和 provider budget tests。旧 evidence 或 mixed revision 不能通过。

小时级生产信心由独立命令判定：

```bash
python3 scripts/check-production-confidence-gate.py --require-ready
```

该命令只检查 evidence，不启动长任务。正式 hour-scale 任务只能通过 GitHub Actions
中的 `Production Confidence` workflow（nightly schedule 或 `workflow_dispatch`）
运行；本地调用 `--contract-kind hour-scale` 会在创建目录和启动 worker 前立即失败。
Gate 要求 evidence 带匹配 repository、workflow、event、job、commit SHA 和 run ID 的
GitHub Actions provenance；同一长生命周期 worker 至少运行 3600 秒与 100 次，
provider request/event count 稳定，RSS 与 allocator 指标有限且末四分位增长不超过
合同阈值。文档不静态声称 ready；源码变更会让旧 hour-scale evidence 立即 stale。

## Phase 4：生态扩展

以下工作继续冻结，不计为当前 beta 完成项：

1. MCP adapter。
2. LangGraph / CrewAI / AutoGen wrapper。
3. Marketplace 正式发布。
4. 官方 package registry service。
5. Counterexample IDE 深映射。
6. Web Playground。
7. Native gRPC / Protobuf。

Native gRPC 继续服从 RFC 0004 draft/No-Go gate。当前 `grpc_json_transcoding` 是 JSON over HTTP/2 seam，不是 native Protobuf transport。

## Beta 10 项逐条审计

| ID | 条件 | Canonical evidence | 覆盖行为 | 状态 |
|----|------|--------------------|----------|------|
| BETA-01 | manifest run profile | `run-profiles.json` | default/low-risk、input override、JSON/JSONL/quiet | 已闭环 |
| BETA-02 | strong IDs | `runtime-identity.json` | runtime association、budget identity、symbol identity | 已闭环 |
| BETA-03 | one event store | `event-projections.json` | renderer、replay/audit/scheduler/checkpoint/OTel/provider facts | 已闭环 |
| BETA-04 | lifecycle matrix | `lifecycle-matrix.json` | 11 类 lifecycle + terminal validator | 已闭环 |
| BETA-05 | formatter | `formatter-idempotence.json` | std/、fixtures、second pass zero changes、blocking CI | 已闭环 |
| BETA-06 | nominal containers | `stdlib-container-migration.json` | semantic/runtime fallback removal | 已闭环 |
| BETA-07 | reference recovery | `reference-workflow-recovery.json` | SIGKILL、approval、partial write、dedupe | 已闭环 |
| BETA-08 | install | `install-smoke.json` | CLI、LSP、sysroot、VSIX、schema migration | 已闭环 |
| BETA-09 | README claims | `readme-capabilities.json` | 双语 marker、claim contract、forbidden claims | 已闭环 |
| BETA-10 | scope freeze | `product-scope-freeze.json` | command/backend/artifact catalog no expansion | 已闭环 |

## 后续开发方向

### P0：Production Confidence

1. 建立至少一小时 nightly soak。
2. 记录 RSS、allocator、event throughput、provider latency 和 cache growth 趋势。
3. 在真实部署环境重复 reference workflow，而不只使用本地 fault server。
4. 将 nightly evidence 与 release revision、toolchain、host metadata绑定。

### P0：Sema 与 Diagnostics 收尾

1. 继续剥离 `TypeCheckPass` 的 source/diagnostic session状态。
2. 扩大语义 negative matrix，确保每个用户错误都有 stable code、`SourceRange` 与修复建议。
3. 保持 assurance/formal只消费 Typed HIR/IR facts。

### P1：IDE Productization

1. 用 Typed HIR condition facts深化 completion/hover/signatureHelp。
2. 增加 source-graph粒度增量失效和真实 workspace edit序列。
3. 在 production-confidence gate关闭后再做 Marketplace正式发布演练。

### 继续暂停

在 live production-confidence gate 未 ready 前，继续暂停新 emit artifact、infra backend
深化、多 region scheduler、NuSMV library mode、DAP深集成、独立 incremental daemon、
官方 registry service、Native gRPC 和 Web Playground。
