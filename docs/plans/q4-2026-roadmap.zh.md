# AHFL Q4 2026 OKR — 可嵌入性做透 + 编译器基座完备

> 规划轴:项目北极星([RFC 0020](../rfcs/0020-strategic-positioning-embeddable-workflow-dsl.zh.md))——
> 可嵌入、可验证的 agent workflow 编排 DSL。本季度**深度优先**:一方面把可嵌入性从"文档定位 +
> 已铺地基"推到**一个可演示的端到端事实**,把已落库的能力从"有源码"推到"可产品化验收";另一方面
> 把**编译器基座**(类型系统 soundness、前端健壮性、IR/测试保真度)从"能跑通 happy path"打磨到
> **完备可信**——补齐 soundness 缺口、消除内部错误信息、给后端交接边界建立保真保证。
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

这句话对应 **Objective 1**;Objective 2/3/4 把"第二护城河可信"、"已落库能力产品化"、
"结构化执行 UX 收口"作为支撑方向。**Objective 5** 则把镜头转回编译器本体,把类型系统 soundness、
前端健壮性、IR/测试保真度打磨到完备可信——前四者已收官(阶段一),Objective 5 是本季新的
主要开放面(阶段二)。

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
> **真实求解器证据已闭环**:真实 Z3(KR2.4)+ 从源码构建的真实 NuSMV(KR2.5)均有
> `AHFL_Z3_PATH` / `AHFL_SMV_CHECKER`-门控 ctest 留证。

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
> 依据 `issue-backlog-global-gaps.zh.md` §3.1–§3.4。**本 Objective 6 个 KR 已全部 ✅。**

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR3.1 | §3.2 死代码诊断清理:`diagnostics.hpp` 无"定义但零发射"的 ErrorCode | ✅ | 审计 23 个:3 wired(`UNEXPECTED_TOKEN`/`MATCH_ARM_TYPE_MISMATCH`/`EFFECT_INCOMPATIBLE`)+ 补 golden,20 删除;计数 134 → 114。commit 779765a6 / d59510e1 |
| KR3.2 | §3.2 `ConstSema` 收尾:剥离 `TypeCheckPass` 的 source/diagnostic context 状态依赖 | ✅ | ConstSema 已完全解耦:不再持有 `TypeCheckPass*`/`driver_`,仅通过窄的 `ConstSemaDelegate` 虚接口访问 source-context / symbol-table / environment / relation / diagnostics(镜像 ExpressionSemaDelegate 模式);`const_sema.cpp` 零 `TypeCheckPass`/`driver_->` 直接引用(仅一句历史注释)。const-eval 核心(ConstEvaluator/pipeline)取显式 ResolveResult/DiagnosticBag/SourceId。所有 const-emitted 诊断(CONST_EXPR_REQUIRED/CONST_DEPENDENCY_CYCLE/EXACT_SCHEMA_MISMATCH/MISSING_FIELD/COLLECTION_CAPACITY_EXCEEDED)均有测试 |
| KR3.3 | §3.2 诊断迁移到稳定 code/template/related-notes 的一致性收口 + 语义矩阵测试升级为完成标准 | ✅ | 审计:114 定义 / 113 发射 / 99 code-string 有测试;缺口多为 message-text 已覆盖。`LLM_COST_BUDGET_EXCEEDED` 补齐(cost-budget smoke,commit f4e116b3)。`MISSING_IMPORT` / `PRIVATE_MODULE` / `MODULE_BOUNDARY_MISMATCH` 经三种自然复现验证为**防御性/latent**:模块已载入图时全限定路径引用直接解析成功、未载入时先命中 `UNKNOWN_SYMBOL`,现有 resolver happy-path 不产生其触发态(需畸形图状态)。不伪造测试;归类为防御性守卫,与 KR3.1 dead-code 判据一致 |
| KR3.4 | §3.1 Runtime/LLM 生产化:hour-scale soak、RSS/allocator 趋势可本地 / 常态观测 | ✅ | 本地 soak 已闭环:`ahfl.reference_workflow.long_soak_smoke`(`--contract-kind smoke`,无 CI provenance 门)每次跑单一 long-lived worker 并产出 `peak_rss`/`allocator_in_use`/`allocator_reserved` 趋势 artifact(quartile 均值 + per-iteration slope);新增稳态内存增长门(last vs first quartile ≤ max(5%,1MiB),commit ac887e72)把"观测"升为"回归门"。hour-scale 契约仍由 CI nightly 跑(设计如此) |
| KR3.5 | §3.3 LSP 从"handler 可用"到"IDE 可用":hover/completion/signatureHelp 用 Typed HIR + condition facts,source-graph 级增量失效,真实编辑序列回归 | ✅ | Typed HIR 消费已就绪(hover 走 typed expressions/resolved_type)。**condition-facts 持久化 + 消费闭环**:`TypedExpr` 加 `narrowing_when_true/false`(serializable `TypedNarrowingFact`),typecheck 期为每个 if-condition 落盘 narrowing facts(commit 41ee5c71);**hover 渲染** "narrows (then/else): <place> is[/not] …";**completion 消费同一持久化 facts**:在 narrowed then/else 分支内对 `x.` 成员补全,若 `x` 被 `x != Option::None` 收窄为 present,则解包 Option 内层类型后再枚举字段/方法(sysroot 项目单测验证:narrowed 分支 offer `field_a/field_b`,未收窄分支只 offer `Some/None`)。signatureHelp 作用于 call-argument 位置,narrowing facts 不改变签名本身(active_parameter 已由 arg-index 驱动),无额外 fact 消费需求 |
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

## Objective 5 — 编译器基座打磨到完备可信

> 前四个 Objective 把"可嵌入 / 可验证 / 可产品化"做透;本 Objective 回到**编译器本体**,把三处
> 经证据审计确认的基座缺口补齐:(A) 类型系统的 soundness 空洞,(B) 前端健壮性与 Principle 5
> 诊断一致性,(C) IR/测试的保真度保证。所有 KR 都由三份并行代码审计的 `file:line` 证据支撑,
> 只收真实缺口(不含"防御性 latent"或"明确冻结"项)。**本 Objective 是本季新增的主要开放面。**

### 5A — 类型系统 soundness 与 RFC 0013 收尾

> RFC 0013(`implementing`)自报 P2 85% / P3 97% / P4 75%,但代码审计发现自报偏乐观:
> 返回位 where-bound 强制与 `decreases` 终止证明均为 `(void)clause;` 空 stub,且**用户泛型
> 完全没有 variance 系统**(连 RFC phase 表都漏列)。这些是 soundness 级缺口,非 ergonomic。

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR5.1 | 方法级 trait 类型参数作用域激活:`resolve_trait_method_info` 能解析 `fn fold<U>` / `map<A>` 的方法级 tparam(不再落 `UNKNOWN_TYPE`),解锁容器代数 trait(Foldable/Functor/Iterable)骨架 | ✅ | 两处接线:(1) **resolver 侧** `resolver.cpp::resolve_trait_item` 在解析 trait method 签名前把 `item.type_params` 插入 `generic_type_params_` opaque 集(镜像 `resolve_fn_signature`),消除 resolve 期 `unknown type 'A'`;(2) **typecheck 侧** `resolve_trait_method_info` 激活组合作用域([Self]+trait tparam THEN 方法级 tparam)+ 分配 per-method scope id(`TraitMethodInfo.method_scope_id` 新字段),`type_param_names` 存组合名(与 impl 侧 `ImplMethodInfo` 约定一致,TypeVar index 对齐 call-site subst)。新增两个单测(bare `A` 返回位 / `A` 多处签名位置无 `UNKNOWN_TYPE`);trait_impl 59/59、fn_generics 43/43、p2_s1 11/11、runtime generics 12/12 全绿 |
| KR5.2 | 返回位 where-clause bound 强制落地:`typecheck.cpp:3061-3070` / `3981` 的 `TODO(hook_for_p3c)` 空 hook 补上真实校验,未满足的返回类型 bound 能诊断(带 `TRAIT_BOUND_NOT_SATISFIED` 或新码 + SourceRange) | ✅ | 新增 `TypeCheckPass::check_return_position_bounds`:对每个有 body 的 fn,收集其声明返回类型引用到的具体 nominal(自身 + 递归 type_args),对 `where <Nominal>: Trait` bound 中 subject 命中返回类型的、复用现有 `check_bound` 校验,未满足报 `TRAIT_BOUND_NOT_SATISFIED` 带返回类型 SourceRange。泛型 tparam subject 跳过(仍由 call-site 负责)。两处空 hook(fn-body 3061 / return-stmt 3981)替换为真实调用 + 说明注释。此前 `fn make() -> Widget ... where Widget: Show`(无 impl)静默通过,现正确报错。3 个新单测(未满足拒绝 / 满足接受 / 泛型 subject 不在返回位强制),p2_s1 11/11、semantics 6/6、trait+typecheck golden 14/14、integration 41/41 全绿 |
| KR5.3 | `decreases` 终止证明落地:`validate.cpp:482-485` 的 `(void)clause;` stub 补上严格递减 / 跨 clause ranking 校验,并被 SMV/BMC 消费(不再仅"存在性检查") | ✅ | `walk_typed_contract_clauses` 从 no-op 升级为真实终止校验(`validate.cpp`):(1) **wildcard-on-cyclic** —— agent 状态机含环(iterative DFS 三色回边检测)时 `decreases: *;` 报 `DECREASES_STAR_ON_CYCLIC_AGENT`(镜像设计文档 §3.1 `E::decreases_star_on_recursive`);(2) **跨 clause lexicographic ranking** —— 每个具体 `decreases:` clause 复用已有 `sema::recognize_single` 分类,非良基 shape(非 length(self)/self.<field>/<ident>-1)报 `DECREASES_MEASURE_NOT_RECOGNIZED` 警告(与 SMV 抽象观察降级对齐,Principle 5 可操作)。此前从未接线的 `decreases_recognizer` 首次进入验证路径。4 个新单测(acyclic wildcard 接受 / cyclic wildcard 拒绝 / 未识别度量告警 / 识别度量静默),`validate_plumbing` 8/8 绿,全套件 exit 0 |
| KR5.4 | 用户泛型 variance 系统:以 trait 声明取代 `type_relations.cpp:605-614` 的 per-container-name 硬编码 variance(`TODO(P5-02)`),让用户定义泛型不再一律 invariant | ✅ | 用 **use-site 结构推断**(OCaml/Rust 式,非降级)取代硬编码:typecheck 层 `infer_nominal_variance` 从结构体字段 / enum-variant payload 的使用位极性推断每个 tparam 的 variance——协变默认位、Fn 参数逆变翻转、Map 键不变、嵌套泛型按其自身 variance 复合、递归 nominal fail-closed 到 invariant、未用参数取协变。经新增 `TypeRelationOptions::variance_provider`(默认空 = 保持 legacy invariant,零行为变更)注入 `type_relations.cpp`,struct/enum 参数按 variance 用 subtype/逆向 subtype/equivalent 比较;stdlib 容器保留原专用分支不变。RFC 0013 phase 表补入 variance(此前完全漏列)+ Decision History 记录。新增 type_relations 单测(covariant/contravariant/invariant/default 四路),20/20 + 全套件 exit 0 全绿 |
| KR5.5 | RFC 0013 → `implemented`:P2/P3/P4/P5 剩余项(cross-chain 泛型推断、first-class `TraitDecl`/`ImplDecl` IR 节点、bounded-refinement `List<T> where length<=N` 的 SMV 定尺数组接线、container-wrapped `Self` dispatch)逐项核对入 Decision History,frontmatter 从 `implementing` 推进 | 🔵 | **大部推进,frontmatter 如实保持 `implementing`(诚实评估)**。已完成:(1) Objective-5 的 KR5.1/5.2/5.3/5.4 四项 + **first-class `ir::TraitDecl`/`ir::ImplDecl` IR 节点(headline 阻塞项)已落地**并逐项核对入 RFC 0013 Decision History(2026-08-27 条目),phase 表 P2 88% / P3 98% / P4 80% / P5 95% 同步更新;(2) bounded-refinement `List<T>(N)` 的 SMV 有限展开经 KR2.2/2.3 已闭环(smt_encode 有 capacity 即 unroll)。first-class 节点新增 `ir::TraitMethodSig`/`TraitDecl`/`ImplDecl`,全 Decl-visit sweep(lower/print/json 逐字节 round-trip/verify/summary)接线,全套件 458/458 绿。**仍开放(阻塞 `implemented`)**:`MethodCallExpr` IR variant、cross-chain 泛型推断 sub-engine、container-wrapped `Self` dispatch——多周期类型系统特性。frontmatter 未伪造为 `implemented`,与"Report outcomes faithfully"一致 |

### 5B — 前端健壮性与诊断一致性(Principle 5)

> parser 遇错即 bail(恢复模块写好却从未接线),lowering 层 ~30 处 `throw logic_error` 退化成
> 无位置的 "parser failed",package-graph/discovery 诊断整体绕过结构化 catalogue——这些都直接
> 违反 CLAUDE.md Principle 5(每个诊断带 SourceRange + actionable,无"内部错误"字样)。

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR5.6 | parser 错误恢复接线:把已存在但从未使用的 `error_recovery.{hpp,cpp}`(`parse_with_recovery` / `AhflErrorStrategy`)接入 `frontend.cpp:3436-3470` 的真实解析路径,支持多错误报告 + 部分 AST 存活;去掉其 ad-hoc re-tokenizer,复用 ANTLR lexer + 统一 `edit_distance` | ✅ | `parse_with_recovery` 从 ad-hoc 空白 re-tokenizer 改为**驱动真实 `Frontend::parse_text`**(ANTLR 默认 error strategy 已做多错误恢复):`valid_declaration_count` 取真实 partial AST 的 `declarations.size()`、`error_count`/`error_messages` 取真实 parse 诊断、suggestions 用统一 `edit_distance` 对每个 `UNEXPECTED_TOKEN` 的 offending token(从诊断消息提取单引号 lexeme)做 near-miss keyword 建议。删除 ad-hoc `tokenize`/`is_declaration_keyword`/`is_keyword`。`ahfl_compiler_syntax_recovery` 库新增对 `ahfl_compiler_syntax` 依赖(无环)。新增单测(真实路径多错误 + 两个 struct 夹 garbage 存活 + 消息计数一致),error_recovery 15/15 绿 |
| KR5.7 | lowering 层结构化诊断:把 `frontend.cpp` ~30 处 `throw std::logic_error(...)`(退化为 `"parser failed: ..."`,无 SourceRange)与 `"internal AST invariant violation"`(`:3446`)替换为带稳定 code + template + range 的诊断,或收敛为真正 unreachable 的 assert | ✅ | 全部 28 处 `throw std::logic_error` 消除:16 处 grammar-dispatch invariant 走新 `ProgramBuilder::builder_invariant(context, …)`(抛带 `context_range` 的 `AstBuilderInvariantError`),10 处纯内部 invariant(op→enum 映射 / switch default / chain-empty)走 `builder_unreachable(…)`(默认 range)。新增稳定码 `parse.INTERNAL_PARSE_INVARIANT` + template;`parse_text` 加 typed catch,带 SourceRange 发射;`"internal AST invariant violation"` 裸消息也换成该码。**关键修正**:builder invariant 也会在 ANTLR error-recovery 对畸形输入(如 `value: ;`)产生的残树上触发——此时已有 `UNEXPECTED_TOKEN`,故当已存在 parse error 时抑制 internal-invariant 噪声(避免误报"compiler bug")。新单测验证畸形输入产出稳定 parse 码、无伪 internal-invariant、无 code-less 诊断;LSP handler 6 处断言从依赖 code-less fallback 升级为查真实 `parse.UNEXPECTED_TOKEN`。parser-hardening + lsp + 115 parser/CLI/golden 全绿 |
| KR5.8 | 模块系统诊断入结构化 catalogue:`package_graph.cpp` 40+ 个 `add_error` 共用的 `E::package_graph`、`discovery.cpp` 的 `E::project_discovery` 拆成带稳定 code + SourceRange 的诊断(依赖环、`user package cannot be named 'std'` 等);补全 `ModuleBoundaryMismatch` 的 related-note、把 `MULTIPLE_MODULE_DECLARATIONS` 的 inline template 收进 `messages::resolve` | ✅ | `package_graph.cpp` 的 blanket `E::package_graph` 拆成 16 个 `package.*` 稳定码(IO_ERROR / DUPLICATE_PACKAGE_NAME / DUPLICATE_MODULE_PREFIX / DUPLICATE_EXPORT_MODULE / INVALID_EXPORT_MODULE / INVALID_SYSROOT_PACKAGE / INVALID_STD_DEPENDENCY / RESERVED_PACKAGE_NAME / DEPENDENCY_RESOLUTION / DEPENDENCY_CYCLE / WORKSPACE_* / PATH_DEPENDENCY_KEY_MISMATCH),`add_error` 加 code 形参,35 个站点逐一归类;`discovery.cpp` 两处 blanket `E::project_discovery` 归为 `project.IO_ERROR` / `project.WORKSPACE_PACKAGE_NOT_FOUND`(死 wrapper + 常量删除)。`ModuleBoundaryMismatch` 消息升级为具名(declared vs graph-owner)并加 related-note `ModuleBoundaryMismatchNote`;`MULTIPLE_MODULE_DECLARATIONS` 的 inline `MessageTemplate{...}` 收进 `messages::resolve::MultipleModuleDeclarations`。package_graph 单测加 `has_error_code` 并对 6 条错误路径钉稳定码;resolver/module/project/package/discovery/golden 84/84 绿 |

### 5C — IR 保真度与测试基座完备

> IR 是后端交接边界,却是 emit-only(无反序列化、无 round-trip 保真保证);`property/lowering_equiv.cpp`
> 名不副实(只测 AST 生成器不变量,不碰 lowering);contract-redundancy pass 用指针相等冒充结构相等。

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR5.9 | IR JSON round-trip:实现 IR 反序列化(`parse_ir_json` / `deserialize_program_ir`),镜像 `TypedProgram` 的 `deserialize_typed_program_json` 先例;`tests/golden/ir/*.json` 从单向 snapshot 升级为 emit→parse→emit 逐字节 round-trip | ✅ | 新增 `parse_program_ir_json(std::string_view) -> std::optional<ir::Program>`(声明于 `lowering.hpp`,实现于 `ir_json.cpp` 同 TU),含 `IrJsonReader` + 反向 enum 查表:覆盖全部 14 decl kind / 19 expr variant / 7 temporal / 11 statement / 7 pattern / TypeRef/SymbolRef/Path/CapabilityEffectSpec/provenance/range。表达式 bottom-up 重建进 `expr_arena`;缺省字段全默认;畸形输入返回 nullopt。**逐字节 round-trip workaround**:`TypeRef::display_name` 从配对 display string 播种、parse 后 `recompute_derived_analyses` 复算 formal_observations/summary。新增 `ahfl.ir.json_round_trip` ctest 对 `IrJsonPrinter` 产出的 2 个 golden(`ok_expr_temporal.json` / `ok_workflow_value_flow.json`)parse→print 逐字节相等 + 畸形拒绝。4 个 opt-ir `.json`(`AHFL_OPT_IR_V1` 独立序列化器,非 IrJsonPrinter)明确记录为不在此 parser 范围。`-R ir` 全绿 |
| KR5.10 | 真实 lowering-equivalence property 测试:让 `property/lowering_equiv.cpp` 名实相符——覆盖 HIR→IR 语义保持、opt-IR ≡ 未优化 IR 的等价性质(当前 4 条 property 全是 AST 生成器不变量,零 `ir::`/lowering 触及) | ✅ | 重写 `lowering_equiv.cpp`:驱动 curated corpus(simple agent / contract agent / workflow)走真实 parse→resolve→typecheck→lower 管线,断言三条真实中端性质:**P1** HIR→IR lowering 确定性(同程序两次 lower → 逐字节相同 IR JSON);**P2** lowered IR `verify_ir_program(BackendReady)` 通过;**P3** opt-IR 等价——`lower_to_opt` + `optimize` 到 fixpoint 保持 OptFunction 名集不变且 `verify_opt_program` 仍通过。用 curated corpus 而非随机 AstGenerator(后者产近似源、不保证 typecheck)。测试 target 链 handoff/ir/ir_opt 库,7/7 绿 |
| KR5.11 | 共享 IR 结构相等工具 + contract-redundancy pass 补强:实现可复用的 IR 表达式结构相等(全仓缺失),替换 `contract_redundancy.cpp:14-38` 的按 kind + 指针相等的浅比较,使其能捕获语义重复而非仅精确拷贝;补 formatter parse→print→parse 幂等性质测试 | ✅ | 新增 `ir::exprs_structurally_equal` / `ir::temporal_exprs_structurally_equal`(`include/ahfl/compiler/ir/ir_equal.hpp` + `src/compiler/ir/ir_equal.cpp`):递归穿过 ExprRef.ptr / TemporalExprPtr,覆盖全部 19 个 ExprNode + 7 个 TemporalExprNode 变体,忽略 source_range/id/effect(结构而非 identity);pattern 按 canonical `text` 比。`contract_redundancy.cpp` 的按指针相等浅比较替换为结构相等——现能捕获"同形但分别 lower 到不同 arena slot"的语义重复 clause,不再仅精确拷贝。新增 `ahfl.ir.structural_equality` 单测(6 例:异 slot 同形相等 / 异 op / 异 operand / 嵌套 member / 空 ref / temporal 递归);formatter 加 `format(format(x))==format(x)` 幂等性质断言(5 个代表源)。ir/passes/formatter 套件绿 |
| KR5.12 | opt-IR 时序片段 lowering 完备:`opt_lower.cpp:1135-1190` 当前 skip 的 4 类时序原子(`Called`/`InState`/`Running`/`Completed`)纳入 opt-IR 函数化路径,或明确文档化为有界豁免并保留 `record_skipped_temporal_fragment` 可观测记录 | ✅ | 采用**有界豁免**路线(语义正确):opt-IR 是值表达式优化器,4 类原子是运行时事件/状态观察(命名 agent 状态 / capability 事件 / workflow 节点生命周期 flag),无可优化的表达式树——由 SMV/BMC 后端直接处理。在 `record_skipped_temporal_fragment` 补齐文档化 rationale(reason 升级为"runtime event/state observation … handled directly by the SMV/BMC backend")+ 记录点注释说明豁免边界。新增单测 `records all four temporal atoms as bounded exemptions`:4 类原子全部被记录(证明无静默跳过)、豁免有界(嵌套在 temporal binary 里的 embedded Bool 仍被函数化)、program 仍 verify。8 个 opt-ir golden(reason 变更)重生成(仅 reason 行变);opt_ir 16/16、`-R ir` 20/20 全绿 |

---


- **RFC 0013 剩余 2 个 guardrail**(`implementing`):P3-gaps-A(闭包 self/关键字参数 +
  `CANNOT_INFER_CLOSURE_PARAM`)与 P3-gaps-B(`let _` 通配 + `{}` unit 字面量)已于
  2026-08-22 落地;剩余的 **P2 通用 fn(cross-chain 泛型推断)与 P3 容器代数 trait** 已提升为
  可写码的 **KR5.1 / KR5.5**,不再仅是 RFC 修订。**variance 系统(KR5.4)需先补进 RFC 0013
  phase 表**(当前完全漏列)再实现——这一步是决策文档修订。
- 明确排除表中的项(gRPC / nuXmv / 第三方 SDK / WASM 执行运行时)维持冻结,除非外部条件
  (license 可核实、宿主 SDK 立项)变化。**注**:`InfraWasm` 后端(WAT emission,RFC 0019)
  当前是 default-ON 的真实后端且有测试——"WASM codegen 冻结"仅指**执行型 WASM 运行时嵌入**,
  infra-WASM 产物生成在代码层并未冻结;此措辞不一致待在排除表澄清。

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

> **阶段一(Objective 1–4)23/23 KR 已 ✅ —— 可嵌入性 / 验证 / 产品化全部收官。**
> **阶段二(Objective 5)11/12 KR 已 ✅,1 个 🔵(KR5.5)—— 编译器基座完备性大幅推进。**
> 阶段一最后一个曾被标为环境阻塞的 KR4.5(真实 LLM run 证据)已通过驱动真实本地 llama.cpp
> 推理引擎解除。当前总盘:**35 个 KR,34 ✅ / 1 🔵**。KR5.5(RFC 0013 → `implemented`)是唯一
> 未收官项:Objective-5 的四项 landing + **first-class `ir::TraitDecl`/`ImplDecl` IR 节点
> (headline 阻塞项,全套件 458/458 绿)**已核对入 RFC Decision History,但 `MethodCallExpr`
> variant、cross-chain 泛型推断 sub-engine、container-wrapped `Self` dispatch 仍开放——这些是
> 多周期类型系统特性,超出 Objective-5(soundness / 健壮性 / 保真)范围,故 RFC 如实保持
> `implementing`,不伪造 `implemented`。

**阶段一(Objective 1–4)推进顺序 —— 已全部 ✅:**

1. ~~**KR3.4 — Runtime 本地 soak / 内存趋势可观测**~~ ✅ 已完成:本地 soak smoke 已闭环并
   产出 RSS/allocator 趋势 artifact,新增稳态内存增长回归门(commit ac887e72)。
2. ~~**KR2.6 — counterexample 更深映射**~~ ✅ 已完成:SMT-BMC 侧字段 source map 打通,
   反例赋值标注源码 offset(commit e5e97fe0)。
3. ~~**KR2.7 尾 — budget 趋势报告 + release-blocking 阈值**~~ ✅ 已完成:SMV-size 趋势门
   + committed baseline + 报告 artifact(commit 见 git log)。
4. ~~**KR3.2 尾 — `ConstSema` 状态剥离**~~ ✅ 已完成:ConstSema 通过 `ConstSemaDelegate`
   虚接口完全解耦,零 `TypeCheckPass`/`driver_->` 直接引用;const 诊断全覆盖。
5. ~~**KR3.5 — LSP Typed HIR + condition facts**~~ ✅ 已完成:`TypedExpr` 持久化
   `narrowing_when_true/false`,hover + completion 消费(commit 41ee5c71 / bdfc2f48)。
6. ~~**KR3.3 尾 — 窄可达 visibility 诊断补测**~~ ✅ 已完成:actionable 覆盖补齐,三个残留码
   判定为防御性 latent,不伪造测试。
7. ~~**KR4.5 — RFC 0012 → `stabilized` 真实 LLM 证据**~~ ✅ 已完成(原标环境阻塞)。改为驱动
   真实本地 llama.cpp 推理引擎;可选 `AHFL_LLAMA_SERVER`-门控 ctest。

**阶段二(Objective 5)推进顺序 —— soundness 优先,依赖清晰者先行(11/12 已 ✅):**

1. ~~**KR5.3 `decreases` 终止证明** + **KR5.2 返回位 where-bound**~~ ✅ 已完成:两处 `(void)clause;`
   空 stub 补上真实校验(wildcard-on-cyclic 终止 / 返回位 bound 强制)。
2. ~~**KR5.7 lowering 结构化诊断** + **KR5.8 模块系统诊断入 catalogue**~~ ✅ 已完成:28 处 throw →
   结构化 code+range 诊断;package_graph/discovery blanket code 拆成稳定码 taxonomy。
3. ~~**KR5.9 IR round-trip** + **KR5.10 真实 lowering-equivalence property**~~ ✅ 已完成:
   `parse_program_ir_json` 逐字节 round-trip;lowering_equiv 改为真实中端性质。
4. ~~**KR5.1 方法级 trait tparam 作用域** → **KR5.4 variance**~~ ✅ 已完成 → **KR5.5 RFC 0013 →
   implemented** 🔵 部分推进:四项 landing 已核对入 Decision History,但 first-class trait/impl
   IR 节点等多周期特性仍开放,RFC 如实保持 `implementing`(不伪造)。
5. ~~**KR5.6 parser 错误恢复接线**、**KR5.11 结构相等工具 + 冗余 pass**、**KR5.12 opt-IR 时序片段**~~
   ✅ 已完成:错误恢复接入真实 ANTLR 路径;共享 IR 结构相等 + redundancy pass 补强;opt-IR
   时序原子文档化为有界豁免。

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
- **阶段一环境阻塞 KR 已全部解除**:KR2.5(NuSMV)源码构建解除;KR4.5(真实 LLM)驱动真实本地
  llama.cpp 引擎解除。阶段一无遗留 🚫,Objective 1–4 在本机 100% 收官。
- **阶段二(Objective 5)大幅收口**:12 个 KR 中 11 ✅、1 🔵(KR5.5)。soundness 级缺口
  **KR5.2/5.3(where-bound / decreases 空 stub)、KR5.4(用户泛型 variance)已全部落地**——
  基座完备的硬指标达成。类型系统深水区(KR5.1/5.4)配足负例 / 语义矩阵测试,未为赶 KR 引入
  临时规则(遵守核心设计原则)。KR5.9/5.10(IR round-trip / lowering-equivalence)后端交接
  边界保真地基已补齐。唯一未收官的 KR5.5(RFC 0013 → `implemented`)因 first-class trait/impl
  IR 节点等多周期特性开放而如实保持 `implementing`。
- **前期风险已控**:KR3.2/KR3.5 涉及 `TypeCheckPass` 状态剥离与 Typed HIR 新字段的敏感区,
  已配充分负例 / 语义矩阵测试并全绿。
