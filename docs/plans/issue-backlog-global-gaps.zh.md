# AHFL 全局后续工作清单

本文档按当前源码事实重整后续工作，不再沿用旧版“全部未实现”的 checkbox。每一项只进入三类之一：已实现、已落库但未产品化、真正未做或证据不足。

---

## 一、分类规则

| 分类 | 判定标准 | Backlog 处理 |
|------|----------|--------------|
| 已实现 | 源码、CLI 注册、单元测试、golden、CMake target 或文档证据能证明基础能力存在 | 不作为新功能待办；只保留维护、回归和文档索引 |
| 已落库但未产品化 | 模块或 handler 已存在，但缺用户入口、端到端路径、真实协议/失败矩阵、CI 门禁或产品体验 | 作为近期主线 backlog |
| 真正未做或证据不足 | 当前树没有可验证实现，或只有研究方向、占位意图、外部系统假设 | 作为新功能、探索或上游依赖任务 |

---

## 二、已实现：不应继续按“未做”重复立项

这些能力已在当前树中具备基础实现。后续任务应聚焦维护、验收或产品化，而不是重新实现同名模块。

| 领域 | 已有证据 | 说明 |
|------|----------|------|
| Pass Manager 与核心 pass | `src/compiler/passes/pass_manager.cpp`、dead state、workflow simplification、expression canonicalization、temporal simplification、capability reachability、contract redundancy | 基础 pass pipeline 已存在；`ahflc.passes.semantic_backend_effect` 已证明 `-O` 会改变普通 IR/SMV backend 输出；后续重点是扩大收益指标和 target 验收 |
| Opt IR | `src/compiler/ir/opt/` | 作为 compiler diagnostic artifact 存在；不应混入常规 LSP 主状态 |
| Counterexample 解析与 JSON | `src/verification/formal/counterexample.cpp`、`counterexample_json.cpp`、formal golden | 反例基础链路存在；后续是更强源码映射和真实模型检查器矩阵 |
| BMC / k-induction 基础 | `src/verification/formal/bmc.cpp`、`tests/unit/verification/formal/bmc.cpp` | 当前更接近状态图 reachability/k-induction 原型；不要再写成完全未实现 |
| nuXmv / SPIN / TLA+ backend 文件 | `src/verification/formal/nuxmv_backend.cpp`、`spin_backend.cpp`、`tlaplus_backend.cpp` | backend seam 与机器可读 capability/availability matrix 已存在；nuXmv/NuSMV 是当前 AHFL SMV 外部验证路径，SPIN/TLA+ 仍是 emit-only |
| 外部进程 launcher | `src/verification/formal/process_launcher.cpp`、`base/support/process.*` | `popen()` 替换方向已落库；后续补跨平台和超时/错误矩阵 |
| Runtime evaluator / executor / agent / workflow baseline | `src/runtime/evaluator/`、`src/runtime/engine/agent_runtime.cpp`、`workflow_runtime.cpp` | local deterministic runtime baseline 已存在 |
| HTTP / gRPC JSON transcoding / schema validator | `src/runtime/engine/http_transport.cpp`、`grpc_transport.cpp`、`response_schema_validator.cpp` | transport 基础存在；bearer/OAuth2/mTLS 认证解析、缺 secret manager/secret fail-closed、TLS client cert/key path 传递、gRPC JSON transcoding response metadata/trailer capture、metadata/trailer `grpc-status` fail-closed、timeout、retry、schema mismatch 与 malformed JSON 已有 capability 级回归；workflow 层 capability 失败传播已有回归；真实 binding 配置的 CLI 端到端协议矩阵已覆盖成功、auth negative、gRPC metadata/trailer 非 OK、timeout/retry、schema mismatch 与 malformed JSON 投影；native gRPC / Protobuf transport 仍未定案 |
| Parallel scheduler | `src/runtime/engine/parallel_scheduler.cpp`、`tests/unit/runtime/engine/parallel_scheduler.cpp` | 线程池、failure propagation、retry 基础存在 |
| Distributed scheduler baseline | `src/runtime/engine/distributed.cpp`、`tests/unit/runtime/engine/distributed.cpp` | checkpoint、remote endpoint、failover 基础存在；仍有确定性和 restore 语义债务 |
| LLM provider 扩展模块 | `src/runtime/providers/llm/streaming.cpp`、`tool_calling.cpp`、`provider_registry.cpp`、`token_budget.cpp`、`response_cache.cpp` | secret handle、HTTP fallback、streaming、tool catalog、persistent cache 与 token/cost budget 已进入真实 run path；公开机器证据统一为 `ahfl.run-event` JSONL，包含 invocation-scoped usage、cache outcome、provider degradation、policy notices 与 ranged diagnostics。provider/secret 内部 audit vectors 只用于单测，不再生成平行公开 artifact；后续重点是跨 provider audit 与长期运行证据 |
| Secret provider baseline | `src/runtime/providers/secret/` | env/vault/cloud/key rotation/auth provider 模块存在，并已接入 LLM run 与 HTTP/gRPC capability 基础路径；`SecretManager::refresh` lifecycle audit、`ahflc run` 的 refresh-before-use 投影、key rotation retry policy 和 secret-free rotation history 已落地，更完整跨 provider audit 仍待产品化 |
| LSP server 与核心 handler | `src/tooling/lsp/server.cpp`、`tests/unit/tooling/lsp/server_handlers.cpp` | JSON-RPC、diagnostics publish、`textDocument/diagnostic` / `workspace/diagnostic` pull report、completion、definition、hover、references、rename/prepareRename、documentSymbol、workspace/symbol、signatureHelp 已存在 |
| VS Code extension packaging baseline | `tools/vscode/package.json`、`tools/vscode/src/extension.ts`、`tools/vscode/syntaxes/ahfl.tmLanguage.json`、`tools/vscode/snippets/ahfl.json`、`tools/vscode/test/suite/index.js`、`tools/vscode/test/installSmoke.js`、`tools/vscode/test/problemsTranscript.js`、`tools/vscode/test/packageInventory.js`、`scripts/package-vscode-vsix-release.sh`、`.github/workflows/vscode-extension.yml` | VS Code client、语言声明、TextMate grammar、snippet、基础 CodeLens、platform VSIX 打包脚本、CI artifact workflow、Marketplace package inventory gate、platform VSIX install smoke、hover/completion/rename/watched-files extension test、Problems diagnostics transcript 和 diagnostics publish/recovery extension test 已存在；剩余是 workspace folder extension 序列、更深 IDE 体验验证与真实 Marketplace 发布演练 |
| Formatter library / CLI | `src/tooling/formatter/formatter.cpp`、`src/tooling/cli/cli_driver.cpp`、`tests/unit/tooling/formatter/formatter.cpp` | library、配置加载、`ahflc fmt` / `fmt --check`、文件/目录/project/workspace 批量模式与 partial failure 汇总已存在；剩余是 formatter 语法覆盖质量与完整 import graph 格式化策略 |
| REPL library / executable | `src/tooling/repl/repl.cpp`、`src/tooling/repl/main.cpp`、`tests/unit/tooling/repl/repl.cpp`、`tests/scripts/repl_smoke.py` | `:type`、`:verify`、`:simulate`、eval 与独立 `ahfl-repl` 入口存在；`:simulate` 已接入 IR AgentDecl 状态机 step simulation，后续可继续深化为 flow/runtime 值级模拟 |
| DAP library / executable | `src/tooling/dap/`、`src/tooling/dap/main.cpp`、`tests/unit/tooling/dap/dap_basic.cpp`、`tests/scripts/dap_smoke.py` | DAP handler、breakpoint、state inspector 与独立 `ahfl-dap` 入口存在；runtime 深集成仍待做 |
| Incremental compiler library / executable | `src/tooling/incremental/`、`src/tooling/incremental/main.cpp`、`tests/unit/tooling/incremental/incremental.cpp`、`tests/scripts/incremental_smoke.py` | dependency graph、IR cache、changed compile 与独立 `ahfl-incremental` 入口存在；project-aware invalidation 和持久 cache contract 仍待做 |
| Telemetry / profiling libraries / CLI timing | `src/tooling/telemetry/`、`src/tooling/profiling/`、`src/tooling/cli/cli_driver.cpp` | trace/metrics/logging/pass timing/hotspot/memory tracker 基础存在；`--time-passes`、`--smv-size-report`、`--trace-export`、`--metrics-export`、`--structured-log` 与 `--memory-report` 已接入 CLI |
| WASM / K8s / OpenAPI / Terraform target backend | `src/compiler/backends/infra/`、`CommandKind::EmitWasm`、`EmitK8sCrd`、`EmitOpenApi`、`EmitTerraform` | 基础 generation 和 CLI 注册存在；仍需产品级语义和验收 |
| Fuzz / bench / mutation target | `tests/fuzz/`、`tests/bench/`、`tests/mutation/` | 目录和 target 已存在；CI 显式运行 `quality-gates`，覆盖 fuzz/property、真实 compile-time budget、memory proxy budget、bench smoke、mutation config report 与真实 `emit smv` size/spec budget；趋势化与真实 mutation score 仍未闭合 |

---

## 三、已落库但未产品化：近期主 backlog

### 3.1 P0：Runtime / LLM Provider 生产化收口

目标：让 `ahflc run` 从 demo-grade direct config 变成可审计、可失败、可恢复的 runtime product path。

待办：

- [ ] 明确 native gRPC / Protobuf transport 是否进入近期目标；若进入，必须独立于 JSON transcoding seam。

已完成产品化（详见下方验收证据与 `ahflc.run.*` / `ahfl.secret.*` / `ahfl.runtime.*` smoke 矩阵）：secret handle 链（env/vault/cloud provider + OAuth2 token-secret + mTLS cert/key + `refresh_secrets_before_use` + secret-free key rotation lifecycle）、token/cost budget（全局 / capability / workflow / node 粒度 + fail/warn policy）、provider fallback、streaming、response cache 持久化 snapshot（跨进程命中）、runtime tool catalog、workflow input schema validation、HTTP/gRPC JSON transcoding认证与失败矩阵、`--capability-bindings` runtime binding，以及 canonical JSONL/OTel projection。

验收证据：

- `ahflc run` 使用 secret handle、预算和 fallback 配置成功运行，并覆盖 secret 缺失、空值、预算非法、fallback secret 缺失和 provider 失败。
- `ahflc.run.llm_provider_runtime.smoke` 使用本地 OpenAI-compatible stub 覆盖 fallback+stream、进程内/跨进程 cache、canonical usage event、warn policy、fail policy `BudgetRejected` terminal lifecycle、ranged diagnostics 和 secret-free JSONL。
- `ahflc.run.llm_failure_matrix.smoke` 使用本地 OpenAI-compatible stub 覆盖 HTTP 401 认证失败、fallback exhausted、`provider_degradation_summary` exhausted outcome、streaming interrupted、runtime tool catalog 成功路径、catalog schema negative、catalog-specific invalid args/unknown tool、tool failure、tool timeout、mock-backed tool call invalid args 与 unknown tool 的 CLI 失败诊断和 secret-free JSON artifact。
- `ahflc.run.llm_secret_manager.smoke` 使用本地 Vault / Secret Manager HTTP stub 覆盖 secret 解析成功、404、认证失败、超时、`refresh_secrets_before_use` secret refresh lifecycle events、OAuth2 token-secret bearer header、mTLS cert/key curl 配置、LLM 调用前 fail closed 和 secret-free JSON artifact。
- `ahfl.secret.vault_rotation_all` 覆盖 key rotation policy、失败时 fail closed、retry attempt 计数、callback firing，以及 rotation history 不保存旧/新 secret value。
- `ahfl.runtime.grpc_transport_all` 覆盖 gRPC JSON transcoding curl-backed default transport 的 response metadata/trailer capture；`ahfl.runtime.capability_bridge_all` 覆盖 metadata/trailer `grpc-status` 非 OK、timeout/deadline、retry exhaustion、schema mismatch 和 malformed JSON 时 capability fail closed；`ahfl.runtime.workflow_runtime_all` 覆盖 capability timeout / retry exhaustion 向 workflow 状态、node result、诊断和 invocation context 的传播；`ahflc.run.capability_bindings.smoke` 覆盖 `ahflc run --capability-bindings` 下 HTTP binding 与 gRPC JSON transcoding binding 的真实 curl-backed workflow 成功路径，以及 auth 缺 secret、HTTP retry exhaustion、HTTP malformed JSON、gRPC metadata/trailer 非 OK、gRPC deadline retry exhaustion 和 gRPC schema mismatch 的 CLI fail-closed 诊断路径。
- 缺 secret、认证失败、HTTP/gRPC capability auth 缺失配置、workflow input schema mismatch、stream 中断、tool call invalid args、unknown tool、provider fallback 全部有确定诊断。
- runtime/provider 单测、CLI 集成测试、golden 或 fixture 覆盖真实失败路径。

### 3.2 P0：TypeCheck / Sema 最终闭环

目标：把已经落库的 sema 模块收敛为规范闭包，而不是继续扩散临时规则。

待办：

- [x] 对齐容器 variance：当前代码、type relation 单测、源码级 Typed HIR 回归、CLI fail golden 与 `docs/spec/core-language.zh.md` 一致，`Optional/List/Set` 元素协变，`Map` key 不变、value 协变；CLI fail golden 已覆盖 `Optional/List/Set` 反向协变、`Map` key invariant 与 `Map` value 反向协变拒绝路径。
- [x] 默认关闭规范未列出的 numeric widening：`Int` 不是 `Float` 或 `Decimal(p)` 的子类型，`Decimal(p)` 不是 `Float` 的子类型，`Decimal(p1)` 不是 `Decimal(p2)` 的子类型，并以 CLI fail golden 固化全部 numeric subtyping 负例路径。
- [x] 将 `StructT` / `EnumT` / `EnumVariantT` 的 type relation 等价规则收口为 SymbolId-first，canonical name 仅作缺 symbol fallback。
- [x] 为 `TypeEnvironment::get_struct(Type)` / `get_enum(Type)` 补 SymbolId-first lookup 回归，证明 canonical name 变化时仍按 symbol 找回声明。
- [x] 完成 SymbolId-first nominal identity 的剩余审计：dump-types 按 `SymbolId` 从 `TypeEnvironment` 取声明；IR declaration/reference 通过 `SymbolRef.id` 保留数值 identity；`TypeRef` 保持 canonical type boundary，不把 display string 当内部身份。
- [x] 检查 `SchemaBoundaryKind` 是否覆盖 agent input/output/context default、workflow input/output/node input，并补缺失 negative golden；声明级 schema boundary 非 struct 诊断已使用稳定 `typecheck.INVALID_AGENT_TYPE` code 与 message template。
- [ ] 将 `ConstSema` 收尾：继续剥离 source/diagnostic context 注入等 `TypeCheckPass` 状态依赖，并补齐剩余语义测试矩阵。（已落地：完整 const evaluation boundary、Typed HIR 可序列化 const value tree、`AHFL_CONST_VALUE_ARTIFACT_V1` schema gate、直接 const dependency graph artifact、前序/forward/跨 source const value inline、bool/Int/Float/Decimal/String/Duration folding、member/index folding、Set/Map const value 规范化、稳定 const dependency cycle 诊断；handler 已拆入 `const_sema.*` / `expression_sema.*` / `typecheck_expr.cpp`，旧 `TypeCheckPass` 转发 wrapper 已清理——详见验收证据。2026-08-24：语义测试矩阵审计完成——107 个 emission site 中 52 covered / 10 partial / 45 not covered；20 个 reachable-but-untested 诊断已补齐 golden negative + 单测；23 个 dead code emission site 正在清理。）
- [x] 将 `ExprEffect` 贯穿 assurance/formal：Typed HIR effect 已进入 `ahfl.ir.v2` expression 与 flow `inferred_effect`；assurance/SMV 消费该 fact，并对 capability identity/effect 矛盾 fail closed。
- [ ] 继续把 typecheck / validate 诊断迁移到稳定 code、message template、related notes：当前 type mismatch、exact schema、const expr、non-pure expression、bool semantic boundary、callable arity、capability gating、predicate argument purity、member/field access、struct literal field/target、qualified enum value、schema declaration、declaration duplicate field/variant、agent context default、index access（list index 与非集合目标）、empty literal、`none` context、expression operation、flow assignment target、unknown path root、unknown type / qualified value / callable missing-reference fallback、invalid type reference fallback、invalid callable reference fallback、missing callable metadata fallback、missing const/type metadata fallback、let shadowing warning、validation agent/flow invalid-state、duplicate capability、workflow graph 和 temporal formula 已覆盖；type mismatch related notes 已覆盖 struct field、parameter、return、assignment、schema boundary、optional payload 和 list/set/map 推断元素/key/value，剩余重点是语义测试矩阵一致性。
- [x] 拆深 `TypeCheckPass`：`TypeResolver`、`ExpressionSema`、`DeclarationSema`、`FlowWorkflowSema`、`ConstSema` 已形成明确 owner；driver 仅保留共享 session/state 与 orchestration。
- [ ] 将语义矩阵测试与 golden 负例升级为完成标准。（2026-08-24：新增 17 个 golden negative .ahfl 文件覆盖 agent state-machine / flow handler / workflow graph / temporal formula 验证诊断，3 个单测覆盖 UNKNOWN_CALLABLE method-dispatch 与 UNKNOWN_ENUM_VARIANT struct/tuple variant 站点；ahflc.fail golden 套件 64/64 通过。）

验收证据：

- `tests/unit/compiler/semantics/type_relations.cpp` 与规范一致。
- `tests/unit/compiler/semantics/typed_hir.cpp` 覆盖 const value tree、`AHFL_CONST_VALUE_ARTIFACT_V1` schema gate / mismatch、直接 const dependency graph artifact、前序/forward/跨 source const inline、基础 folding、Float/Decimal/String/Duration folding、Duration const value artifact 规范化、Set/Map const value 规范化、源码级容器 variance、schema declaration diagnostic、const dependency cycle diagnostic 与 Typed HIR JSON 往返；`tests/unit/compiler/semantics/effects.cpp` 覆盖 const initializer declared-type related note 与 agent context default exact-schema related note。
- typecheck golden 覆盖 duplicate field/variant、context default、const expr、readonly input、exact schema、variance（含 `Optional/List/Set` 反向协变、`Map` key invariant、`Map` value 反向协变）、numeric widening。
- LSP diagnostic 能消费稳定 code 和 related information。

### 3.3 P1：LSP 从 handler 可用到 IDE 可用

目标：让 LSP 成为真实编辑体验，而不是只通过 handler 单测证明协议存在。

待办：

- [x] 将 `AnalysisService` 升级为 package-aware snapshot/cache：snapshot 记录 document revision、content hash、workspace revision、PackageGraph context 与 open buffer overlay。
- [x] 以 `TypedProgram` + Resolver symbol/reference data 作为常规 LSP 主状态；Semantic IR 按需 lower，Opt IR 不进入常规 LSP state。
- [x] open/change/close 后刷新全部已打开文档 diagnostics，覆盖跨文件依赖被未保存 overlay 改坏的 IDE 场景。
- [x] 实现 `textDocument/prepareRename`，并在 initialize capability 中声明 `prepareProvider`，覆盖声明、引用和不可重命名位置。
- [x] 为 `workspace/symbol` 增加同一 project snapshot 多打开文档下的符号去重回归。
- [x] 实现 `textDocument/diagnostic` full report 与 `diagnosticProvider` capability，覆盖 broken source pull diagnostics 和修复后空报告。
- [x] 实现 `workspace/diagnostic` full report 与 `workspaceDiagnostics` capability，覆盖未打开 project source 的 resolver diagnostic。
- [x] 支持 `workspace/didChangeWatchedFiles` 后失效 LSP analysis cache，覆盖未打开 imported source 修改后的重新分析。
- [x] 支持 `workspace/didChangeWorkspaceFolders` 后更新 workspace roots 并重新选择 PackageGraph context，覆盖跨 workspace folder manifest 切换。
- [x] 补 VS Code extension packaging baseline：client 激活、TextMate grammar、snippet、基础 CodeLens、内置 release `ahfl-lsp` platform VSIX 打包脚本、CI VSIX artifact workflow、Marketplace package inventory gate、platform VSIX install smoke、hover/completion/rename/watched-files extension test、Problems diagnostics transcript 与 diagnostics publish/recovery extension test。
- [ ] 为 hover/completion/signatureHelp 使用 Typed HIR 和 condition facts，而不是只依赖轻量文本/符号启发。
- [ ] 继续补齐 workspace diagnostics：source graph 级细粒度增量失效。
- [ ] 补 VS Code 真实 IDE 体验闭环：workspace folder extension 序列、更深真实编辑序列和真实 Marketplace 发布演练；当前本地 `vsce` 不提供 `publish --dry-run`，已用 package inventory gate 覆盖离线发布包清单预检。
- [ ] 增加真实编辑序列测试：open/change/save/close 组合回归、更多 manifest / PackageGraph 负例与复杂 broken source recovery。

验收证据：

- LSP handler tests 之外，已有 hover、completion、rename、watched-files、Marketplace package inventory gate、platform VSIX install smoke、Problems diagnostics transcript 与 diagnostics publish/recovery extension test；workspace folder 切换仍只有 handler/protocol 级证据。
- 大文件/多文件场景不会每次无差别全量重建。
- diagnostics、completion、hover 在破损输入下可降级而不是消失。
- 已打开文档的跨文件 diagnostics 在依赖源发生未保存修改时同步刷新。

### 3.4 P1/P2：工具链入口补齐

目标：把已有 tooling library 暴露为用户可执行能力。

待办：

- [x] 在 `CommandKind` 和 CLI registry 中新增 `fmt` / `fmt --check`，接入从输入文件目录向上查找的 `.ahfl-format`。
- [x] 为 REPL 增加独立可执行入口 `ahfl-repl` 与进程级 smoke test。
- [x] 为 DAP 增加独立可执行入口 `ahfl-dap` 与进程级 smoke test。
- [x] 将 formatter CLI 从单文件扩展到目录/project/workspace 批量模式，并定义 partial failure 输出格式。
- [x] 修复 REPL `:simulate` 当前复用 verify 的语义偏差，接入基于 IR AgentDecl 的结构化状态机 step simulation。
- [x] 将 DAP 接入 runtime state、capability breakpoint、step execution。（RFC 0015 全部 8 个 slice 已实现：DebugSession + DAP 事件 + state/capability/line breakpoint + stepping + stack trace/variables + evaluate + output events + VS Code debuggers contribution。）
- [x] 为 incremental compiler 增加独立 `ahfl-incremental` 入口和进程级 smoke test。
- [x] 为 incremental compiler 定义 project-aware import graph、持久 cache 和 daemon invalidation contract。（RFC 0016 全部 6 个 slice 已实现：CacheCore + import graph discovery + signature fingerprint + daemon mode + cache lifecycle + LSP persistent cache + IrCache unification。）
- [x] 为 profiling 增加 `--time-passes`，输出 `-O` Semantic IR pass pipeline 耗时。
- [x] 为 profiling 增加 `--smv-size-report`，输出 `emit smv` artifact 的 byte、line、LTLSPEC 数量。
- [x] 为 telemetry 增加 `--trace-export` 与 `--metrics-export`，输出 CLI command span、duration 和 exit_code 的 JSONL 文件，不污染 stdout artifact。
- [x] 为 telemetry 增加 `--structured-log`，输出 CLI command completion level、duration 和 exit_code 的 JSONL 文件，不污染 stdout artifact。
- [x] 为 profiling 增加 `--memory-report`，输出 source、TypedProgram、IR 规模与结构性 memory proxy 的 JSON report，不污染 stdout artifact。
- [x] 将 `--memory-report` 从结构性 proxy 扩展到可选平台 RSS / allocator 观测，并保留跨平台可比性边界。（`read_process_memory_stats()` 支持 Linux/macOS/Windows，不支持平台返回 nullopt；structural proxy 保持跨平台 baseline。）

验收证据：

- `ahflc --help` 或独立工具 `--help` 暴露新入口且 `docs/reference/cli-commands.zh.md` 同步更新。
- 每个入口至少有一个 CLI integration 或 CTest target。

### 3.5 P2：Formal backend 真实性增强

目标：把形式化验证从 backend seam 和有限状态图 proof 提升到 AHFL 语义级验证。

待办：

- [ ] 将 BMC/k-induction 从简单状态图 reachability 推进到 AHFL contract/property semantics。
- [x] 为 nuXmv、SPIN、TLA+ backend 建立机器可读工具能力矩阵和 skip reason，明确 nuXmv/NuSMV 是当前 AHFL SMV 验证路径，SPIN/TLA+ 仍为 emit-only。
- [x] 将工具能力矩阵接入 CLI/report，让 CI 能区分 `missing_binary`、`verification_unsupported` 和 `checker_error`。
- [x] 增强 counterexample 到 source range、workflow node、capability call、contract clause 的映射。（2026-08-24：capability call 与 contract clause 映射已落地——SMV backend 为 `agent__*__called__*` 与 contract/workflow LTLSPEC 发射带 source range 的 AHFL_MAP，counterexample 投影新增 `ProjectedCapabilityCall` 与 `ViolatedContractInfo` source range，未映射符号回退空 range 不伪造；185/185 counterexample_parse 单测。）
- [x] 将 state-space estimator 接入 `verify` report，输出 agent 数、估计状态空间、transition 数和 tractability warning。
- [x] 建立 NuSMV/nuXmv 输出 parser fixture matrix，覆盖 true/false/error/timeout，并把 timeout 归类为确定 checker failure。
- [x] 将外部 checker 进程 timeout 接入 `ahflc verify --checker-timeout-seconds`，并用 fake checker CTest 证明卡死进程会被杀掉且报告 `checker_timed_out: true`。

验收证据：

- `verify formal` 在 NuSMV/nuXmv 可用时有真实外部工具测试；不可用时有确定 skip 语义；外部 checker 卡死时有可配置 timeout 和确定 failure report。
- counterexample JSON 可被 LSP/IDE 或 CLI explain 稳定消费。

### 3.6 P2：Pass 与 target backend 产品化

目标：证明 pass 和 target backend 对用户有价值，而不只是能生成基础文本。

待办：

- [x] 为 pass pipeline 增加 backend-effect 回归，证明 `-O` 会让普通 `emit ir` / `emit smv` 输出发生可审查改变。
- [x] 将 `--optimize` 与普通 CLI/backend emission 明确绑定，普通 backend 仍消费 Semantic IR，但会先运行 Semantic IR pass pipeline。
- [x] 将 backend-effect 回归扩展到 dead state、workflow simplification、SMV size / IR size 指标和 runtime plan 输出。（2026-08-24：workflow simplification 与 SMV/IR size delta 已落地——`ok_workflow_simplification.ahfl` fixture + `RunWorkflowSimplificationTest.cmake` 断言 `-O` 真实收缩 transitive edge 与 LTLSPEC；dead-state 与 runtime-plan 已补——`RunDeadStateEliminationTest.cmake` 锁住 validator gate（unreachable state 通过 CLI 前即被 `validation.INVALID_STATE` 拒绝，`-O` 前后一致），`RunRuntimePlanEffectTest.cmake` 断言 execution plan 在 `-O` 下丢弃 transitive dependency edge 且 JSON 收缩；均接入 quality-gates label。）
- [x] 将当前 CLI 级 trace/metrics 扩展为 pass-level trace event schema、Opt IR function-level timing 与历史对比报告。（2026-08-24：三部分全部落地——`--pass-trace-export <path>` 输出 `ahfl.pass_trace.v2` JSON;per-pass 记录（name/duration_ms/modified/iteration）+ `semantic_ms`;Opt IR function-level timing（`functions` 数组,每个函数的 per-function optimize() 耗时 + modified,`function_ms` 汇总）;历史对比 `--pass-trace-baseline <path>` 输出 `baseline_delta`（baseline_total_ms + delta_ms,缺失/损坏 baseline 优雅置 null）。`pass_trace_gate.py` 驱动 `emit opt-ir -O` 校验 v2 schema 与 function 记录。）
- [ ] 为 WASM backend 定义 runtime model、WASI/capability mapping、browser-side execution boundary。
- [x] 为 K8s/OpenAPI/Terraform target 定义稳定 schema、validation 和 golden。（2026-08-24：`tests/scripts/infra_target_gate.py` + `InfraTargetTests.cmake` 对三个 emit target 提供 snapshot 比对、结构校验（openapi JSON openapi/info/paths；k8s-crd YAML apiVersion/kind/metadata/spec；terraform HCL resource blocks）与 determinism（两次 emit 字节一致）；顺带修复 `YamlWriter::begin_list_item` 的 dangling-dash YAML 非法输出 bug。）

验收证据：

- pass 对 SMV 状态空间、IR size 或 runtime plan 有可测收益。
- target backend 输出能被外部 validator 或 snapshot gate 验证。

### 3.7 P2/P3：质量工程门禁

目标：把已有 fuzz、bench、mutation 资产变成回归门禁。

待办：

- [x] 将 parser/typechecker/SMV fuzz target 纳入 CI：GitHub Actions 显式运行 `quality-gates`。
- [x] 为 compile time 建立初始真实前端管线 budget gate，覆盖 parse、resolve、typecheck、validate、IR lowering。
- [x] 为 memory usage 建立初始结构性 proxy budget gate，覆盖真实 TypedProgram 与 IR 产物规模。
- [x] 将 memory usage 从结构性 proxy 扩展到平台可比的 RSS/allocator 观测。（`ProcessMemoryStats` + `read_process_memory_stats()`，Linux/macOS/Windows 支持，其他平台 nullopt。）
- [x] 为真实 `emit smv` 输出建立初始 size/spec budget CTest gate，覆盖 formal workflow、pass-productization fixture 和 refund audit example。
- [ ] 将 compile time、memory proxy 与 SMV size budget 扩展为趋势报告、release-blocking 阈值和更多 state-space 代表样本。
- [x] 将 mutation config/report plumbing 纳入 CTest 与 `quality-gates`，输出机器可读 config report。
- [x] 将 mutation testing 从 config/report plumbing 升级为真实 runner job，输出 mutation score。（2026-08-24：`run_fallback_mutation.sh` 自包含 runner 对固定 mutant 集应用于 target 拷贝、重建窄测试并记录 killed/survived，输出真实 mutation score JSON；mutant 集已从 4 扩到 9（新增 boolean-connective、unary-negation、constant-replacement、comparison-boundary、off-by-one loop 五类,fixture 上 killed=8 survived=1 score=0.8889，`scaled_arith` 为刻意 expected survivor）；mull 不可用时 `run_mutation.sh` 输出 `status: tool_unavailable, score: null` 而非伪造分数；`ahfl.mutation.fallback_score` ctest 校验 report 结构与全部 mutant 已评估。）
- [x] 为 fuzz crash corpus 和 minimized repro 建立保存位置。（2026-08-24：`tests/fuzz/crashes/<target>/` 目录约定 + README + `_TEMPLATE.repro.md`，`crash_replay.sh` 把每个 crash 输入回放给对应 libFuzzer 二进制作为 single-run 回归门（空目录时 no-op 通过，提交 crash 文件即成为回归守卫）；此前挡住端到端验证的 fuzzing-configure bug（`LabelTests.cmake` 无条件给 non-fuzzing 分支专属的 `ahfl.fuzz.*_check` 打 label）已修复——`AHFL_ENABLE_FUZZING=ON` 现可 configure，crash_replay ctest 可枚举。）

验收证据：

- CI 已显式运行 `quality-gates`；nightly 趋势数据仍待补齐。
- `quality-gates` 标签可运行 fuzz/property/bench smoke 和 mutation config report；真实 compile time、memory proxy、`emit smv` 输出超出 budget 时会 fail。
- 性能/SMV size 超阈值会 fail 或至少生成 release-blocking report。

---

## 四、真正未做或证据不足

这些项目当前没有足够证据证明已经形成可用 product path。

| 优先级 | 工作项 | 当前判断 |
|--------|--------|----------|
| P1 | 官方 VS Code 扩展完整发布与 IDE 体验闭环 | extension scaffold、TextMate grammar、snippet、基础 CodeLens、platform VSIX 打包脚本、CI VSIX artifact workflow、Marketplace package inventory gate、platform VSIX install smoke、hover/completion/rename/watched-files extension test、Problems diagnostics transcript 与 diagnostics publish/recovery extension test 已有；真实 Marketplace 发布演练和 workspace folder extension 序列仍需补齐 |
| P2 | Native gRPC / Protobuf transport | JSON transcoding 已有，native Protobuf transport 未证明 |
| P2 | NuSMV/nuXmv library-mode embedding | 当前是外部进程 seam，library-mode 仍是研究项 |
| P2 | 浏览器端 Online Playground | WASM backend 有基础，playground product path 未证明 |
| P3 | 官方包仓库 | package resolver/registry 库存在，但 publish/install 和官方仓库未证明 |
| P3 | 多 region 生产调度 | distributed scheduler 有基础，真实多 region control plane 未证明 |

---

## 五、推荐执行顺序

1. P0：受控试点从 bounded CI 提升到小时级 nightly、RSS/allocator 趋势与真实部署环境复跑。
2. P0：TypeCheck / Sema 最终闭环。
3. P1：LSP IDE 产品化。
4. P1/P2：CLI/tooling 入口补齐。
5. P2：Formal backend 真实性增强。
6. P2：Pass 与 target backend 产品化。
7. P2/P3：质量工程门禁。

MCP/framework adapters、Marketplace 正式发布、官方 registry service、Web Playground、
counterexample IDE 深映射和 Native gRPC / Protobuf 在上述 P0 production-confidence gate
关闭前继续冻结。

每个主题应拆成独立 Conventional Commit；涉及 breaking schema、typing 或 runtime config 的提交必须在 footer 中写明 `BREAKING CHANGE:`、影响范围和迁移方式。
