# AHFL 项目状态与演进记录

本文档合并了 V0.1–V0.59 的全部 roadmap 与 issue backlog，作为项目实现状态的唯一入口。

---

## 一、项目概览

| 属性 | 值 |
|------|-----|
| 项目名称 | AHFL (Agent Handoff Flow Language) |
| 当前版本 | v0.59 |
| 语言标准 | C++23 |
| 构建系统 | CMake 3.22+ / Ninja |
| 解析器 | ANTLR4 (4.13.1, vendored) |
| 外部依赖 | ANTLR4 vendored；HTTP Provider 路径通过系统 `curl` 工具执行；无包管理器级第三方运行时依赖 |
| 测试框架 | 自研 golden-file + 直接 C++ 测试 |
| CI 平台 | GitHub Actions (ubuntu-24.04, macos-14, ASan) |
| 许可证 | Apache-2.0 |

**定位**：面向 AI Agent 工作流的强类型 DSL 编译器，支持状态机建模、行为契约、DAG 编排、形式化验证 (NuSMV) 与端到端执行 (LLM Provider)。

---

## 二、编译器架构

```
.ahfl Source
  → ANTLR4 Lexer/Parser     (src/compiler/syntax/parser/generated/)
  → AST Lowering             (src/compiler/syntax/frontend/)
  → Resolver                 (src/compiler/semantics/resolver.cpp)
  → TypeChecker              (src/compiler/semantics/typecheck.cpp)
  → Validator                (src/compiler/semantics/validate.cpp)
  → IR Lowering              (src/compiler/ir/ir_lower.cpp)
  → Backend Dispatch         (src/compiler/backends/driver.cpp)
      ├── IR Text / JSON     (src/compiler/ir/)
      ├── NativeJson         (src/compiler/backends/)
      ├── SMV                (src/compiler/backends/smv/smv.cpp)
      ├── ExecutionPlan      (src/compiler/backends/)
      ├── AssuranceJson      (src/compiler/backends/)
      ├── PackageReview      (src/compiler/backends/)
      └── Summary            (src/compiler/backends/)
  → [Optional] Runtime Execution
      ├── ExpressionEvaluator (src/runtime/evaluator/)
      ├── StatementExecutor   (src/runtime/evaluator/)
      ├── AgentRuntime        (src/runtime/)
      ├── WorkflowRuntime     (src/runtime/)
      ├── CapabilityBridge    (src/runtime/)
      └── LLMProvider         (src/runtime/providers/llm/)
```

### 各阶段职责

| 阶段 | 职责 | 关键文件 |
|------|------|----------|
| Parse | ANTLR4 parse tree → 手写 AST | `src/compiler/syntax/frontend/frontend.cpp` |
| Resolve | 多命名空间符号表，跨文件 import 解析，循环检测 | `src/compiler/semantics/resolver.cpp` |
| TypeCheck | 结构化类型推导，子类型关系，capability 调用上下文检查 | `src/compiler/semantics/typecheck.cpp` |
| Validate | Agent/Flow/Workflow 结构约束（不可达状态、DAG 合法性等） | `src/compiler/semantics/validate.cpp` |
| IR Lower | AST → variant-based IR，计算 StateHandler::Summary | `src/compiler/ir/ir_lower.cpp` |
| Backend | IR → 目标格式分发 | `src/compiler/backends/driver.cpp` |

---

## 三、已完成工作总览

### 3.1 核心编译器（V0.1–V0.2）✅

| 版本 | 聚焦 | 关键产物 |
|------|------|----------|
| V0.1 | 单文件编译器前端闭环 + restricted formal backend | 完整 AST、Resolver、TypeChecker、Validator、IR、`emit-smv` |
| V0.2 | Project-aware 多文件编译模型 | SourceGraph、跨文件 import/resolve、DeclarationProvenance、project-aware IR |

**设计文档**：`docs/design/core-scope-v0.1.en.md`、`docs/design/compiler-phase-boundaries-v0.2.zh.md`、`docs/design/formal-backend-v0.2.zh.md`、`docs/design/module-loading-v0.2.zh.md`

### 3.2 执行工件管线（V0.3–V0.10）✅

| 版本 | 引入的工件 | 说明 |
|------|-----------|------|
| V0.3 | `NativeJson` backend | 可被运行时消费的 JSON 格式 |
| V0.4 | `ExecutionPlan` | Agent/Workflow 的执行计划描述 |
| V0.5 | `DryRunTrace` | Mock capability 的模拟执行跟踪 |
| V0.6 | `RuntimeSession` | 执行会话元数据 |
| V0.7 | `ExecutionJournal` | 执行历史日志 |
| V0.8 | `ReplayView` | 执行回放可视化 |
| V0.9 | `AuditReport` | 审计报告 |
| V0.10 | `SchedulerSnapshot` | 调度器状态快照 |

每个工件都具备：model → validation → bootstrap → CLI/backend emission → golden regression → compatibility contract → CI 标签。

### 3.3 持久化与导出工件（V0.11–V0.13）✅

| 版本 | 引入的工件 | 说明 |
|------|-----------|------|
| V0.11 | `CheckpointRecord` | 可持久化的检查点记录（resume basis、resume blockers） |
| V0.12 | `CheckpointPersistenceDescriptor` | 持久化标识与导出边界 |
| V0.13 | `CheckpointPersistenceExportManifest` | 导出包清单 |

### 3.4 Durable Store Import 管线（V0.14–V0.19）✅

| 版本 | 引入的工件 | 说明 |
|------|-----------|------|
| V0.14 | `StoreImportDescriptor` | 外部 store 导入描述符 |
| V0.15 | `DurableStoreImportRequest` | durable store 导入请求 |
| V0.16 | `DurableAdapterDecision` | adapter 是否接受请求的决策 |
| V0.17 | `DurableAdapterReceipt` | adapter 执行回执 |
| V0.18 | `DurableAdapterReceiptPersistenceRequest` | 回执持久化请求 |
| V0.19 | `DurableAdapterReceiptPersistenceResponse` | 回执持久化响应 |

### 3.5 Provider Adapter 契约层（V0.20–V0.30）✅

| 版本 | 聚焦 | 关键产物 |
|------|------|----------|
| V0.20 | Production-adjacent fake adapter 执行 | `AdapterExecutionReceipt`、`RecoveryCommandPreview` |
| V0.21 | Provider-neutral adapter boundary | secret-free adapter config、capability matrix、write intent、retry/recovery placeholder |
| V0.22 | Provider driver binding | secret-free driver profile、driver capability gating、binding plan |
| V0.23 | Runtime / SDK adapter preflight | runtime profile、preflight plan、readiness review |
| V0.24 | SDK request envelope | envelope policy、request envelope plan、handoff readiness review |
| V0.25 | Host execution planning | execution policy、host execution plan、sandbox/timeout policy |
| V0.26 | Local host execution receipt | side-effect-free receipt simulation |
| V0.27 | SDK adapter request/response artifact | request plan、response placeholder、readiness review |
| V0.28 | SDK adapter interface contract | adapter descriptor、registry model、capability descriptor、error normalization |
| V0.29 | Config loader boundary | config profile、snapshot placeholder、schema compatibility |
| V0.30 | Secret handle resolver | secret handle reference、resolver request/response placeholder、credential lifecycle |

**核心设计原则**（贯穿 V0.20–V0.30）：
- 所有 artifact 必须 **secret-free**（不含 credential、token、endpoint secret）
- 每层只消费上一层的 machine artifact，**不可绕过 artifact chain**
- 每层有 **deterministic identity namespace**（不依赖 wall clock、process id、host path）
- 每层区分 **stable fields / unstable fields / forbidden fields**

### 3.6 Provider 执行基础设施（V0.31–V0.42）✅

| 版本 | 聚焦 | 关键产物 |
|------|------|----------|
| V0.31 | Local host harness | sandbox-first harness runner、execution request/record |
| V0.32 | SDK payload materialization | payload plan、digest、redaction/audit summary |
| V0.33 | Provider SDK mock adapter | mock contract、result fixture、response normalization |
| V0.34 | Real adapter alpha（local filesystem/sqlite） | alpha adapter、real result normalization |
| V0.35 | Idempotency / retry contract | idempotency key namespace、retry token、duplicate detection |
| V0.36 | Durable write commit receipt | commit receipt model、commit identity、provider reference |
| V0.37 | Recovery / resume contract | recovery checkpoint、resume token、partial write recovery plan |
| V0.38 | Provider failure taxonomy | failure kind/category/retryability、response normalization |
| V0.39 | Observability / audit event | execution audit event、telemetry summary、redaction policy |
| V0.40 | Provider compatibility suite | test manifest、fixture matrix、compatibility report |
| V0.41 | Multi-provider registry | provider selection plan、fallback/degradation policy |
| V0.42 | Production readiness review | readiness evidence model、release gate |

### 3.7 Production 准入链（V0.43–V0.50）✅

| 版本 | 聚焦 | 关键产物 |
|------|------|----------|
| V0.43 | Contract conformance runner | conformance input/output、cross-check report |
| V0.44 | Artifact schema/version compatibility | schema drift report、source chain validation |
| V0.45 | Production config bundle validation | config bundle validation、secret handle/endpoint binding 校验 |
| V0.46 | Release evidence archive | archive manifest、artifact digest、missing evidence summary |
| V0.47 | Operator approval workflow | approval request/decision/receipt、rejection reason |
| V0.48 | Real provider opt-in execution guard | OptInDecision、DenialReason、gate 校验 |
| V0.49 | Runtime policy enforcement | policy input/decision/violation、permit/deny/warning |
| V0.50 | Production integration dry run | end-to-end readiness report、evidence chain aggregation |

**注**：V0.45、V0.47、V0.48 的 M4（工程化收口）有少量未完成项，但核心模型与验证均已实现。

### 3.8 解释器与运行时（V0.51–V0.55）✅

| 版本 | 聚焦 | 关键产物 | 关键文件 |
|------|------|----------|----------|
| V0.51 | Expression Evaluator | `EvalContext`、`Value` variant、字面量/二元/一元/路径/结构体求值 | `src/runtime/evaluator/evaluator.cpp` |
| V0.52 | Statement Executor | `ExecContext`、Let/Assign/If/Goto/Return/Assert/ExprStmt | `src/runtime/evaluator/executor.cpp` |
| V0.53 | Agent State Machine Runtime | `AgentRuntime`、状态转换循环、quota enforcement | `src/runtime/engine/agent_runtime.cpp` |
| V0.54 | Workflow Integration | `WorkflowRuntime`、DAG 拓扑调度、跨节点依赖 | `src/runtime/engine/workflow_runtime.cpp` |
| V0.55 | Capability Bridge | `CapabilityRegistry`、`FunctionCapability`、HTTP/gRPC stub、retry 策略 | `src/runtime/engine/capability_bridge.cpp` |

**V0.55 Deferred 项**（仍未实现）：
- Secret 管理集成
- mTLS / OAuth2 认证
- Capability 调用缓存与幂等
- 分布式 capability routing
- 调用链追踪（OpenTelemetry）

---

## 四、当前实现重点（V0.56–V0.59）

### 目标

实现并收紧 LLM Provider 与 runtime 执行边界，将 AHFL Capability 与 OpenAI-compatible LLM API 打通，同时保持运行时输入解析 fail-closed。

### 架构

```
CapabilityRegistry
  └── LLMCapabilityProvider
        ├── LLMProviderConfig (endpoint, model, api_key, temperature)
        ├── PromptBuilder (IR types → system prompt + user prompt)
        ├── HttpClient (POST /v1/chat/completions)
        └── ResponseParser (JSON → Value)
```

### 当前状态

- LLM Provider 基础路径已实现：`LLMProviderConfig`、`HttpClient`、`PromptBuilder`、`ResponseParser`、`LLMCapabilityProvider`。
- 配置解析使用 repo 内 `base/json` DOM，不再维护独立字符串 JSON 解析器。
- `ResponseParser` 对非法 enum、缺失 struct 字段和类型解析失败返回 `nullopt`，避免把错误响应伪装成业务值。
- `ahfl-run` 在 endpoint、model、api_key 缺失时拒绝启动。

### 非目标 / 后续增强

- 多模型路由 / fallback（v0.57+）
- Streaming 输出
- Function Calling 协议
- Token 预算控制
- Prompt 缓存

---

## 五、关键设计边界

以下设计原则贯穿整个项目演进，是架构决策的核心约束：

### 5.1 Artifact Chain 不可绕过

每一层 artifact 只能消费上一层的 machine artifact 输出，不得：
- 回退读取 AST、raw source、parse tree
- 读取 CLI 文本输出、host log、reviewer text
- 使用私有脚本或外部工具推导状态

### 5.2 Secret-Free Artifact

所有持久化到仓库的 machine artifact 禁止包含：
- Credential、API key、token value
- Secret manager response
- 真实 endpoint URI、object path、database table
- Host environment variable value

Secret 引用仅通过 `SecretHandleReference` 间接表达。

### 5.3 Deterministic Identity Namespace

所有 artifact identity 必须：
- 仅由上游 artifact identity 推导
- 不依赖 wall clock、process id、host path、random seed
- 保证 regression 确定性

### 5.4 Formal Backend 边界

SMV 后端的 formal subset：
- Agent 状态变量 + ASSIGN 转换
- Workflow DAG → 多状态变量 + 生命周期阶段
- Contract clauses → LTLSPEC
- 非有界谓词用 observation 变量抽象
- 不 promise statement/data 完整执行语义

### 5.5 Runtime 不替代 Formal

运行时（V0.51–V0.55）提供 local deterministic 执行能力，但不替代 SMV 形式化验证。两者是互补关系：
- Formal：穷举验证安全/活性属性
- Runtime：单路径真实执行

### 5.6 版本演进约定

- 每版只推进一个 artifact/capability 边界
- 项目仍处于未成熟阶段，不承诺前向兼容
- 允许按照行业最佳实践 aggressive refactor，但必须同步更新实现、测试、golden 与文档
- Artifact schema/format 的 breaking change 需要显式记录影响面和验证证据

---

## 六、延期与未来工作

详见 [issue-backlog-global-gaps.zh.md](./issue-backlog-global-gaps.zh.md)，主要涵盖：

- **P0**：反例诊断、HTTP/gRPC Capability 真实实现
- **P1**：LSP、LLM Streaming/Function Calling、优化 Pass 层、Secret 管理
- **P2**：并行执行、格式化器、nuXmv/BMC、OpenTelemetry、Fuzzing、IDE 扩展
- **P3**：WASM 后端、增量编译、REPL、调试器、分布式执行、包管理

---

## 七、版本索引（快速查阅）

| 版本范围 | 系统 | 状态 |
|----------|------|------|
| V0.1–V0.2 | 核心编译器（Parse → IR → SMV） | ✅ 完成 |
| V0.3–V0.10 | 执行工件管线 | ✅ 完成 |
| V0.11–V0.13 | 持久化与导出工件 | ✅ 完成 |
| V0.14–V0.19 | Durable Store Import 管线 | ✅ 完成 |
| V0.20–V0.30 | Provider Adapter 契约层 | ✅ 完成 |
| V0.31–V0.42 | Provider 执行基础设施 | ✅ 完成 |
| V0.43–V0.50 | Production 准入链 | ✅ 完成（V0.45/47/48 M4 部分项除外） |
| V0.51–V0.55 | 解释器与运行时 | ✅ 完成 |
| V0.56 | LLM Provider 适配器 | 🔄 进行中 |
