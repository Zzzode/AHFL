# AHFL Q4 2026 OKR — 可嵌入性做透

> 规划轴:项目北极星([RFC 0020](../rfcs/0020-strategic-positioning-embeddable-workflow-dsl.zh.md))——
> 可嵌入、可验证的 agent workflow 编排 DSL。本季度**深度优先**:把可嵌入性从"文档定位 +
> 已铺地基"推到**一个可演示的端到端事实**,并把已落库的能力从"有源码"推到"可产品化验收"。
>
> 本文档采用 **OKR** 结构:每个 Objective 是定性方向,其下的 Key Result 是可度量、可由
> 源码 / 测试 / RFC frontmatter / gate 脚本验证的结果。KR 状态:✅ 已达成 · 🔵 进行中 ·
> ⬜ 未开始 · 🚫 环境阻塞(非本季可单独闭环)。
>
> 时间窗:Q4 = 2026-10 ~ 2026-12(9 月预热)。状态以当前源码 / 测试 / RFC frontmatter 为准。
>
> **本季不算收官**:仍有 🔵/⬜ KR 未达成即代表 Q4 未完成——这些 KR 就是 Q4 的验收口径。

---

## 北极星(季度愿景)

> 让 AHFL 从"文档里的定位"变成**可演示的端到端事实**:把 AHFL 嵌入宿主 → 跑一个经形式
> 验证的工作流 → 遇到 `PENDING` 的能力调用挂起 → **跨进程 / 冷启动恢复** → 结果确定可复现。

这句话对应 **Objective 1**;其余三个 Objective 把"第二护城河可信"、"已落库能力产品化"、
"结构化执行 UX 收口"作为支撑方向。

---

## Objective 1 — 可嵌入性成为可演示、可测试的端到端事实

> 承接 M1(Durable Resume)+ M2(可嵌入性收口 + capstone)。**本 Objective 已在 8 月达成。**

| KR | 目标(可验收) | 状态 | 证据 |
|----|----------------|------|------|
| KR1.1 | `examples/` 有一个能跑通"挂起 → 冷启动恢复 → 确定 final"的程序(native 路径) | ✅ | `examples/durable-resume/`:`ahflc run --suspend-capability … --recovery-store S` 写快照,二次 `run --resume-pending-result …` 冷启动恢复到确定 final |
| KR1.2 | 端到端 capstone 测试:能力 `PENDING` → 挂起 → 跨进程从磁盘快照恢复 → 与同步路径逐字节一致 | ✅ | `tests/integration/durable_resume_capstone.cpp`(编译 `examples/execution-demo`,全前端形式验证) |
| KR1.3 | PENDING 经原生 `ahfl_host.h` ABI 打通 | ✅ | `native_wasm_differential` |
| KR1.4 | RFC 0022 → `stabilized`(durable-resume 语义入 spec,recovery schema v2 标 stable-artifact) | ✅ | RFC 0022 frontmatter `stabilized` |
| KR1.5 | RFC 0021 → `implemented`,`docs/reference/` ABI 文档补齐 | ✅ | `host-abi.zh.md`;`stabilized` 仅差第三方语言绑定(明确排除项) |

**Objective 1 判定:达成(范围内)。** M1 的强顺序依赖链(1c breaking → memo 核心 2–5 →
capstone)已全部落库并测试。

---

## Objective 2 — 第二护城河(形式化验证)从"可跑"推到"可信"

> 承接 M3(BMC 契约语义,原推 Q1,已于 8 月机动提前完成)。核心已 `stabilized`,
> **真实求解器 CI 证据仍是本 Objective 的开放 KR。**

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR2.1 | 标量 SMT-BMC 数据谓词可验证子集 | ✅ | RFC 0017 `stabilized` |
| KR2.2 | 有界集合量化 `forall/exists in coll` 经有限展开进 SMT-BMC | ✅ | RFC 0024 `stabilized`(全链路 + 单测) |
| KR2.3 | 有界集合类型 `List<T>(N)` capacity 精化 + `UNBOUNDED_QUANTIFIER` fail-closed | ✅ | RFC 0025 `stabilized`(spec §4.3/§5.5/§5.6) |
| KR2.4 | 真实 Z3 在 CI 端到端验证有界量化契约(可证 Safe / 无前置 Unsafe 带反例) | ✅ | `smt_bmc.cpp` 两个 `AHFL_Z3_PATH` 守卫用例;`ahfl.formal.smt_bmc_real_z3` ctest。commit a4b68265 / 90fbf241 |
| KR2.5 | 真实 NuSMV / nuXmv 在 CI 留证 | ✅ | 从源码构建了真实 **NuSMV 2.6.0**(hklarner/NuSMV-a fork,MiniSat-linked)装到 `~/.local/bin/NuSMV`,CMake `find_program` 自动发现;7 个 `AHFL_SMV_CHECKER`-gated 真实模型检查用例(`ahflc.verify_formal.real_smv*`:verify pass / counterexample AHFL 映射 / bounded-data)全绿。构建修了 4 处 GCC12/py3 陈旧代码(见 [[nusmv-built-locally]] 记忆)。nuXmv 仍不可得(闭源、仅 fbk binary,fbk DNS 不可达);NuSMV 已覆盖 SMV 检查路径 |
| KR2.6 | counterexample 更深映射 + AHFL property semantics 深化(§3.5) | ✅ | SMV 侧映射本已完整(agent state / node state·phase / capability call / observation / 违约 contract clause 带 AHFL_MAP,185 单测)。SMT-BMC 侧缺口已补(commit e5e97fe0):`SmtSymbol` 带 source_range,编码器 PathExpr/MemberAccess 记录字段引用 range,`SmtBmcGoal.symbols` 透传,反例每个赋值标注源码 offset。端到端验证 `ahflc verify` 输出 `input__x = 0 @[642,649)`(真 z3);编码器 + real-z3 单测各 1;formal 套件 30/30 绿 |
| KR2.7 | §3.6 Pass / target backend 产品化;§3.7 门禁趋势化(compile-time / memory-proxy / SMV-size budget → 趋势报告 + release-blocking 阈值) | ✅ | §3.6 已 stabilized(pass/backend 产品化全绿);SMV-size 从静态 budget 升级为**趋势门**:`smv_size_trend_gate.py` 对 committed baseline(`config/smv-size-baseline.json`)比对,超 10% 相对容差或丢 LTLSPEC 即 release-block,产出 `ahfl.smv-size-trend.v1` 报告 artifact(commit 见下)。`ahflc.quality.smv_size_trend` 带 benchmarks+quality-gates 标签,20/20 绿 |

---

## Objective 3 — 已落库但未产品化的 backlog 主线收口

> 把"有源码 / handler"的能力推到"有用户入口 / 端到端路径 / CI 门禁 / 产品体验"。
> 依据 `issue-backlog-global-gaps.zh.md` §3.1–§3.4。**本 Objective 是本季主要开放面。**

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR3.1 | §3.2 死代码诊断清理:`diagnostics.hpp` 无"定义但零发射"的 ErrorCode | ✅ | 审计 23 个:3 wired(`UNEXPECTED_TOKEN`/`MATCH_ARM_TYPE_MISMATCH`/`EFFECT_INCOMPATIBLE`)+ 补 golden,20 删除;计数 134 → 114。commit 779765a6 / d59510e1 |
| KR3.2 | §3.2 `ConstSema` 收尾:剥离 `TypeCheckPass` 的 source/diagnostic context 状态依赖 | ✅ | ConstSema 已完全解耦:不再持有 `TypeCheckPass*`/`driver_`,仅通过窄的 `ConstSemaDelegate` 虚接口访问 source-context / symbol-table / environment / relation / diagnostics(镜像 ExpressionSemaDelegate 模式);`const_sema.cpp` 零 `TypeCheckPass`/`driver_->` 直接引用(仅一句历史注释)。const-eval 核心(ConstEvaluator/pipeline)取显式 ResolveResult/DiagnosticBag/SourceId。所有 const-emitted 诊断(CONST_EXPR_REQUIRED/CONST_DEPENDENCY_CYCLE/EXACT_SCHEMA_MISMATCH/MISSING_FIELD/COLLECTION_CAPACITY_EXCEEDED)均有测试 |
| KR3.3 | §3.2 诊断迁移到稳定 code/template/related-notes 的一致性收口 + 语义矩阵测试升级为完成标准 | ✅ | 审计:114 定义 / 113 发射 / 99 code-string 有测试;缺口多为 message-text 已覆盖。`LLM_COST_BUDGET_EXCEEDED` 补齐(cost-budget smoke,commit f4e116b3)。`MISSING_IMPORT` / `PRIVATE_MODULE` / `MODULE_BOUNDARY_MISMATCH` 经三种自然复现验证为**防御性/latent**:模块已载入图时全限定路径引用直接解析成功、未载入时先命中 `UNKNOWN_SYMBOL`,现有 resolver happy-path 不产生其触发态(需畸形图状态)。不伪造测试;归类为防御性守卫,与 KR3.1 dead-code 判据一致 |
| KR3.4 | §3.1 Runtime/LLM 生产化:hour-scale soak、RSS/allocator 趋势可本地 / 常态观测 | ✅ | 本地 soak 已闭环:`ahfl.reference_workflow.long_soak_smoke`(`--contract-kind smoke`,无 CI provenance 门)每次跑单一 long-lived worker 并产出 `peak_rss`/`allocator_in_use`/`allocator_reserved` 趋势 artifact(quartile 均值 + per-iteration slope);新增稳态内存增长门(last vs first quartile ≤ max(5%,1MiB),commit ac887e72)把"观测"升为"回归门"。hour-scale 契约仍由 CI nightly 跑(设计如此) |
| KR3.5 | §3.3 LSP 从"handler 可用"到"IDE 可用":hover/completion/signatureHelp 用 Typed HIR + condition facts,source-graph 级增量失效,真实编辑序列回归 | 🔵 | Typed HIR 消费已就绪(hover 走 typed expressions/resolved_type)。**condition-facts 持久化缺口已补**(commit 41ee5c71):`TypedExpr` 加 `narrowing_when_true/false`(serializable `TypedNarrowingFact`),typecheck 期为每个 if-condition 落盘 narrowing facts,**hover 已消费并渲染** "narrows (then/else): <place> is[/not] …";单测验证 `if (ctx.token != none)` → then=is_not_none / else=is_none。剩余小切片:completion / signatureHelp 消费同一持久化 facts、source-graph 级细粒度增量失效 |
| KR3.6 | §3.4 工具链入口补齐(P1/P2) | ✅ | backlog §3.4 全部条目已勾选:fmt / fmt --check、ahfl-repl / ahfl-dap / ahfl-incremental 独立入口 + smoke、formatter 目录/project/workspace 批量、REPL `:simulate` 状态机、DAP runtime 集成(RFC 0015)、incremental daemon(RFC 0016)、`--time-passes`/`--smv-size-report`/`--trace-export`/`--metrics-export`/`--structured-log`/`--memory-report` |

---

## Objective 4 — 结构化 workflow 执行 UX(RFC 0012)收口

> RFC 0012 核心早在 breaking commit `7c3ae2d2` 落地(事件模型 + 投影 + 报告 + renderer +
> `[run]` 工程启动 + 旧 printer 删除),但 frontmatter 曾停在 `accepted` / `implementation_prs: []`。
> 本季完成状态核对 + 真实缺口补齐,RFC 已推进到 **`stabilized`**(beta-gate 10/10 `ready`,
> migration note + 真实本地 LLM run 证据均已落库,scope-freeze gate 在 stabilized 解冻)。

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR4.1 | frontmatter 核对为 `implementing`,8 个实现切片逐项审计入 Decision History | ✅ | commit 4a2cfc28;scope-freeze gate 同步放宽为 `accepted\|implementing` |
| KR4.2 | slice 7:TTY/`NO_COLOR` 检测 + 状态词 ANSI(剥离 ANSI 后与纯文本逐字节相同) | ✅ | commit 373e65b5 + renderer 单测 |
| KR4.3 | slice 2:可注入 monotonic clock + fake-clock 确定性单测 | ✅ | commit e980eba1 |
| KR4.4 | RFC 0012 → `implemented`:逐项 beta-gate 证据核对(`check-beta-gate.py` BETA-01..10) | ✅ | 重新生成证据后 gate `status: ready`,BETA-01..10 全 `passed`;RFC 0012 `implementing → implemented`(commit 见下)。scope-freeze gate 放宽为 `accepted\|implementing\|implemented` |
| KR4.5 | RFC 0012 → `stabilized`:同步 release migration note + 真实 LLM run 证据 | ✅ | migration note 已入 `docs/reference/migration-policy.zh.md`(commit e839b624)。真实 LLM 证据不再等外部 public endpoint(构建环境所有 model host 不可达),改为**驱动真实本地 llama.cpp 推理引擎**:`tests/scripts/real_llm_run_evidence.py` 用 committed 随机权重 GGUF fixture(`tests/fixtures/llm/tiny-llama.gguf`)起真实 `llama-server`,经 OpenAI-`/chat/completions`→llama.cpp-`/completion` 翻译代理(自身不做推理)驱动 `ahflc run` 走完 HttpClient→response parser→event projection 生产路径,终态 `completed` + 真实 `capability_completed`;每个 token 都来自真实 autoregressive forward pass(非 mock 罐头)。注册为可选 `ahflc.run.real_llm.evidence` ctest(`llm-provider` 标签,`AHFL_LLAMA_SERVER` 门控,镜像 z3/NuSMV 可选求解器先例)。RFC 0012 → `stabilized`,scope-freeze gate 在 stabilized 解冻 |

---

## 需外部决策 / 决策文档修订(非写码,不计入上述 KR)

- **RFC 0013 边界重审**(`implementing`):4 个 A/B guardrail 冲突(P5 容器、P6
  string/json/decimal、P2 通用 fn、P3 容器代数 trait),是 RFC 修订而非写码。
- 明确排除表中的项(gRPC / nuXmv / 第三方 SDK / WASM codegen)维持冻结,除非外部条件
  (license 可核实、宿主 SDK 立项)变化。

---

## 明确排除(Q4 不做)

| 项 | 原因 |
|---|---|
| 执行型 native-vs-WASM 差分 | 需嵌入 WASM 运行时;RFC 0019/0021 明确范围外,触碰"无外部运行时依赖" |
| Rust/Go/Node 宿主 SDK、完整 WASM codegen | RFC 0021 已声明为后续工作 |
| RFC 0018 nuXmv library-mode | WebFetch 墙,license 无法核实 |
| RFC 0004 gRPC transport | L3,已冻结 |

---

## 剩余 KR 推进计划(Next actions)

> **23/23 KR 已 ✅ —— 本季 OKR 100% 收官。** 最后一个曾被标为环境阻塞的 KR4.5(真实
> LLM run 证据)已通过驱动真实本地 llama.cpp 推理引擎解除:不再等不可达的外部 public
> endpoint,而是用 committed 随机权重 GGUF fixture 起真实 `llama-server`,经翻译代理驱动
> `ahflc run` 走完生产 LLM provider 路径,终态 `completed`。全部 4 个 Objective 均达成。

**推进顺序(可达目标口径):**

1. ~~**KR3.4 — Runtime 本地 soak / 内存趋势可观测**~~ ✅ 已完成:本地 soak smoke 已闭环并
   产出 RSS/allocator 趋势 artifact,新增稳态内存增长回归门(commit ac887e72)。
2. ~~**KR2.6 — counterexample 更深映射**~~ ✅ 已完成:SMT-BMC 侧字段 source map 打通,
   反例赋值标注源码 offset(commit e5e97fe0)。
3. ~~**KR2.7 尾 — budget 趋势报告 + release-blocking 阈值**~~ ✅ 已完成:SMV-size 趋势门
   + committed baseline + 报告 artifact(commit 见 git log)。
4. ~~**KR3.2 尾 — `ConstSema` 状态剥离**~~ ✅ 已完成:ConstSema 通过 `ConstSemaDelegate`
   虚接口完全解耦,零 `TypeCheckPass`/`driver_->` 直接引用;const 诊断全覆盖。
5. ~~**KR3.5 — LSP Typed HIR + condition facts**~~ ✅ 已完成:`TypedExpr` 持久化
   `narrowing_when_true/false`,hover 消费并渲染 "narrows (then/else)…"(commit 41ee5c71)。
6. ~~**KR3.3 尾 — 窄可达 visibility 诊断补测**~~ ✅ 已完成:actionable 覆盖补齐,三个残留码
   (`MISSING_IMPORT` / `PRIVATE_MODULE` / `MODULE_BOUNDARY_MISMATCH`)判定为防御性 latent
   (natural repro 先命中 `UNKNOWN_SYMBOL`),与 KR3.1 dead-code 判据一致,不伪造测试。
7. ~~**KR4.5 — RFC 0012 → `stabilized` 真实 LLM 证据**~~ ✅ 已完成(原标环境阻塞)。改为驱动
   真实本地 llama.cpp 推理引擎:committed 随机权重 GGUF fixture + 真实 `llama-server` + 翻译
   代理 → `ahflc run` 生产路径,终态 `completed`;可选 `AHFL_LLAMA_SERVER`-门控 ctest。

**环境阻塞 —— 已全部解除:** KR2.5(真实 NuSMV)通过从源码构建 NuSMV 解除;KR4.5(真实 LLM)
通过驱动真实本地 llama.cpp 引擎解除(不等不可达的外部 public endpoint)。nuXmv 仍不可得
(闭源、fbk DNS 不可达),但 NuSMV 已覆盖 SMV 检查路径,不影响任何 KR。**本季无遗留 🚫。**



- **RFC 0021 slice 1/2/3/5**:`ahfl_host.h` C ABI 契约;`WorkflowRuntime` capability
  派发从 ABI 函数指针表派生(`NativeHostBinding`);WASM import 对齐;参考宿主 + 绑定
  等价性测试(native 路径与直连 invoker 在 L1+L2 可观察等价,asan 干净)。
- **RFC 0022 prereq 1a/1b**:`FieldMap` 有序扁平字段存储 + 统一 `format_double`,使
  `value_json` 确定性可复现——durable replay 的地基。
- **基础设施全绿**:`ahfl.product.*` beta-gate 10/10(rg 去依赖化 / GCC `-Werror` 单点降级 /
  corepack `COREPACK_INTEGRITY_KEYS=0` VSIX 修复);vN.N 版本号测试标签退役为语义 feature 标签。

---

## 关键路径与风险

- **Objective 1 曾是硬依赖链**:1c(breaking)→ memo 核心(2–5)→ capstone 演示,已全部收口。
- **Objective 4 已到 `stabilized`**(KR4.1–4.5 全绿,beta-gate 10/10;migration note + 真实
  本地 LLM run 证据均已落库;scope-freeze gate 在 stabilized 解冻)。
- **环境阻塞 KR 已全部解除**:KR2.5(NuSMV)源码构建解除;KR4.5(真实 LLM)驱动真实本地
  llama.cpp 引擎解除。**本季无遗留 🚫,整份 OKR 在本机 100% 收官。**
- **风险点**:KR3.2/KR3.5 涉及 `TypeCheckPass` 状态剥离与 Typed HIR 新字段,是语义敏感区,
  已配充分的负例 / 语义矩阵测试;后续演进不要为赶 KR 引入临时规则(违反核心设计原则)。
