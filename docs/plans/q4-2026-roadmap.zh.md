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
前端健壮性、IR/测试保真度打磨到完备可信——前四者已收官(阶段一),Objective 5(阶段二)
亦已 12/12 收官,RFC 0013 类型系统演进推进到 `implemented`。**阶段一 + 二共 35/35 KR 达成。**

**阶段三(2026-08-28 扩张):从"完备可信"到"业内顶尖"。** 基于架构重构想(RFC 0020 补
「架构北极星」,RFC 0026 IR 塔+执行模型、RFC 0027 query 前端+IR 单一真相源已 draft),新增
**Objective 6**(编译器架构升级:三层 IR 塔取代单层、WASM 唯一执行引擎、tree-walking
evaluator 退役、前端 query 化、消灭 8-location sweep)与 **Objective 7**(backlog §3 已落库
能力产品化到顶尖)。**Q4 验收口径随之扩张:总盘 54 KR,当前 39 ✅ / 1 🔵 / 14 ⬜。**

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
> 只收真实缺口(不含"防御性 latent"或"明确冻结"项)。**本 Objective 12/12 KR 已全部 ✅ 收官。**

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
| KR5.5 | RFC 0013 → `implemented`:P2/P3/P4/P5 剩余项(cross-chain 泛型推断、first-class `TraitDecl`/`ImplDecl` IR 节点、bounded-refinement `List<T> where length<=N` 的 SMV 定尺数组接线、container-wrapped `Self` dispatch)逐项核对入 Decision History,frontmatter 从 `implementing` 推进 | ✅ | **达成:RFC 0013 frontmatter `implementing → implemented`(commit `9ac22713`),以本文 §Implementation Plan 阶段验收表 P0–P7 逐条核对为准,全部达成并经 clean-rebuild 全套件 457/457 验证**。落地链(全部 commit 于 develop):first-class `ir::TraitDecl`/`ir::ImplDecl`(`3d227536`)+ `ir::MethodCallExpr`(`2242404b`)IR 节点;container-wrapped `Self` dispatch(`440d156b`,`has_body` 门控不误伤真实泛型 impl);P4 `List<T> where length<=N` refinement sugar(`f93f6458`,归一到 RFC 0025 `collection_capacity`,零后端改动);P6 `#![no_prelude]` inner attribute(`139bce8f`);**跨模块 trait dispatch 崩溃修复**(`c52f9730`,check_bound canonical 回退 + 失败发诊断而非静默 ErrorT 崩溃 lowering,Principle 5);P5 容器 trait impl 层 `Foldable/Iterable/Functor for List` + `Functor for Map`(`8c4ec5b2`);**impl-level where-clause 存储+强制**(`0a6d7898`,不可满足 impl 不再静默生效)。phase 表 P2–P6 → 100%。**明确非验收项(不阻塞 `implemented`,均安全失败无静默漏检)**:cross-chain 泛型推断(Non-Goal #3)、effect 多态(Non-Goal #2)、闭包捕获(Non-Goal #4)、const 泛型(Non-Goal #5);method-level where-clause 精化;Set/Map Foldable/Iterable(需 runtime 枚举 @builtin,属 P7 后续 runtime 能力扩展,非 P5 类型系统范围) |

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


- **RFC 0013 已 `implemented`**:P3-gaps-A(闭包 self/关键字参数 +
  `CANNOT_INFER_CLOSURE_PARAM`)与 P3-gaps-B(`let _` 通配 + `{}` unit 字面量)已于
  2026-08-22 落地;**P2 通用 fn / P3 容器代数 trait** 经 KR5.1/KR5.5 全部落地(容器 trait impl
  层 `Foldable/Iterable/Functor for List` + `Functor for Map` 已入 std,经 trait-dispatch 测试
  验证)。**variance 系统(KR5.4)已补进 RFC 0013 phase 表**并实现(use-site 结构推断)。
  cross-chain 泛型推断维持 **Non-Goal #3**(体验级,不阻塞验收)。
- 明确排除表中的项(gRPC / nuXmv / 第三方 SDK / WASM 执行运行时)维持冻结,除非外部条件
  (license 可核实、宿主 SDK 立项)变化。**注**:`InfraWasm` 后端(WAT emission,RFC 0019)
  当前是 default-ON 的真实后端且有测试——"WASM codegen 冻结"仅指**执行型 WASM 运行时嵌入**,
  infra-WASM 产物生成在代码层并未冻结;此措辞不一致待在排除表澄清。

---

## Objective 6 — 编译器架构升级到业内顶尖(RFC 0020/0026/0027)

> Objective 1–5 把 AHFL 做到**完备可信**;本 Objective 把它推向**业内顶尖**(对标
> Rust/Swift/GHC/Dafny)。根据 2026-08-28 的架构重构想:当前**单层 IR + tree-walking
> 解释执行**被判定为过渡形态。[RFC 0020](../rfcs/0020-strategic-positioning-embeddable-workflow-dsl.zh.md)
> 已补「架构北极星」,[RFC 0026](../rfcs/0026-ir-tower-and-execution-model.zh.md)(IR 塔 +
> 执行模型)与 [RFC 0027](../rfcs/0027-query-frontend-and-ir-ssot.zh.md)(query 前端 + IR
> 单一真相源)已 draft。本 Objective 把这三篇从设计推到**完整落地**:三层 IR 塔取代单层、
> WASM 成为唯一执行引擎、tree-walking evaluator 退役、前端 query 化、8-location sweep 消灭。
> **这会重新打开 Q4 验收口径**:「完整 WASM codegen」从下方「明确排除」表移出(见该表更新)。

### 6A — IR 塔与执行模型(RFC 0026,9 片)

> 三层塔:Typed HIR(诊断,已有)→ AHFL-IR(验证/编排)→ Core-IR(执行)。验证路径吃
> AHFL-IR、执行路径吃 Core-IR(Dafny 式分叉),根治当前 temporal/contract 与执行节点混装的
> altitude 冲突。WASM 唯一引擎,不自研 VM;evaluator conformance 验收后原子删除。
>
> **工具链前置(2026-08-29 加入,两 bot review 达成一致)**:本机 / CI 当前**无** `wasmtime` /
> `wasm-tools` / `wat2wasm`,而 KR6.5 要求 wasmtime 真实执行。为避免把版本 / WAT→WASM 转换 /
> runner 风险推迟到主干最敏感处,先落一个**独立小提交**做 wasmtime preflight(见下 Prerequisite P-6A,非计数)。
> 边界:wasmtime 作为**可选测试/宿主工具**,`find_program` 门控 + 确定 skip reason,**绝不链接进
> AHFL 核心构建**——复用 z3(`AHFL_Z3_PATH`)/ NuSMV(`AHFL_SMV_CHECKER`)/ llama.cpp
> (`AHFL_LLAMA_SERVER`)先例,不破坏 RFC 0019/0021「无强运行时依赖」定位。

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR6.1 | RFC 0026 → `accepted`:清 4 个 Open Question(Core-IR 内存模型 arena vs 所有权、控制流结构化边界、语义保持验证深度、单态化爆炸预算)+ compiler/runtime owner sign-off | ✅ | 5 个 Open Questions 全部给出决策+理由(arena/无 GC、WASM 结构化 region/无 relooper、conformance 差分必做+形式化证明长期可选、单态化预算 fail-closed、双 IR-JSON 弃用绑 KR6.9);draft→review→accepted(commit `728db370`),sign-off 经 Q4 路线图 Objective 6 确认 |
| KR6.2 | IR 塔骨架落地(RFC 0026 P1):`AHFL-IR` / `Core-IR` 层类型 + 层边界定义,空壳零行为变更,现有 backend 仍消费旧路径 | ✅ | `tower.hpp`:`Layer`(TypedHir=0→AhflIr=1→CoreIr=2 索引式身份)+ `Path` + `path_of()` 编码 Dafny 式分叉 + 零成本 `LayerTag<L>`;`tower.cpp` static_assert 锁层序/分叉;3 doctest 用例。零消费者、现有 backend 仍走 `ir::Program`;全套件 463/463 绿(commit `0243ff10`,RFC 0026 → `implementing`) |
| KR6.3 | AHFL-IR 净化(P2):验证/视图后端(smv/smt/k8s/terraform/openapi)改为消费 `AHFL-IR`,移出纯执行细节;golden 无回归 | ✅ | alias-first:`using AhflIr = Program`(验证/编排层名,`tower::Layer::AhflIr`);验证/视图后端入口签名 re-point 到 `const ir::AhflIr&`(print_program_smv / SmvPrinter::print / lower_k8s_crd / lower_openapi / lower_terraform / lower_wasm / emit_program_smt)。8 文件 +58/-19,零行为变更,SMV/emit-ir/k8s/openapi/terraform golden 逐字节不变,全套件 exit 0(commit `a1a38d1a`)。节点集实际净化留后续片 |
| KR6.4 | lower pass `AHFL-IR → Core-IR`(P3+P4):单态化 + effect→显式 capability-call + temporal/contract/decreases 擦除 + 控制流结构化 + 值表示/内存布局(Bool/Int/Float/Decimal/String/enum/struct/bounded 容器/闭包) | 🔵 | **已落地子片**:(1) P3 骨架(`31eded7f`):`CoreProgram`/`CoreAgentDecl`,索引式 `CoreStateId`,temporal/contract 擦除;(2) capability 声明/import 签名投影(`c48e2c18`):`CoreCapabilityDecl`;(3) **A-normal-form flow lowering(`eaba55f8` + executable/scope/target/typed-identity 收口 `3ec4fe3f`,取代经 Codex review REJECTED 的 call-site scaffold `a7935e01`)**:handler body A-normalize(Rust MIR/Swift SIL 式)—— 纯计算入 per-flow `CoreExpr` arena,**每个 capability 调用是有序 `CoreCapabilityCallStmt`**,使求值序/数据依赖/pending·suspend/恢复 checkpoint/不重放成为**结构事实**。修复 scaffold 两处 P0(嵌套 capability call 不再丢失、`if/else`→`CoreIfStmt` 分支互斥保留)。第二轮 review 再收口:**executable 完整性**(unsupported stmt/expr → Error → is_executable=false,`assert(false)` 不再静默变 no-op);**分支 block scope**(then/else snapshot/restore,branch-local `let` 不外泄);**target/state fail-closed**(`CoreAgentId` 身份索引;missing target / unknown handler state → 结构化 Error+range,绝不默认 state 0);**typed nominal 身份**(`CoreTypeId`+`CoreProgram::types`;construct 带所属 type id;变体构造按 `callee_ref` 符号身份解析,去掉 unqualified 索引/module-path 盲剥防跨模块劫持;struct-field member projection 尚未解析到 `CoreFieldId` → **fail-closed** `core.UNLOWERED_FIELD_PROJECTION`,不发不可执行的字符串成员);**builtin 表单一 SSOT**(`builtin_enum_table()` 公开,lowerer 与同步测试同源,测试比对生产表 vs sysroot 声明序)。第三轮 review 再收口(`adac86cc`):pure unsupported expr → Error(不再伪 executable);struct constructor 用 `{CoreFieldId,CoreValueId}` 按字段身份绑定(`Pair{b,a}` 不再位置错配)、unresolvable 字段 fail-closed;unit variant(`CoreQualifiedExpr`)带 `CoreTypeId+CoreVariantId` 全限定解析;`CoreFlowDecl` 持 typed `CoreAgentId target`;补 `CoreFieldId`/`CoreVariantId` typed wrapper。第四轮(最终验收,`1c677c1e`):新增**真实 sysroot 项目级 e2e 回归**(`parse_project`+repo std→`lower_program_ir`→`lower_ahfl_to_core`):std `Some(Charge(x))` 嵌套调用在 then-region、`Some`→std Option type_id+variant0 / `None`→同 type_id+variant1、用户自定义 `Option` 与 std Option **distinct CoreTypeId 不串表**、unit `Verdict::Approve` typed、仅 field-projection 诊断;`CoreFlowDecl::operator==` 补比 typed `target`;`core.*` 诊断码收进 `diag::` 常量 catalogue(lowerer+测试同源;**当前集中的是稳定 code,message 文本仍在 call site,待 Core verifier/CLI 对外消费前再收进统一 `MessageTemplate`**)。ir_tests 69 例/841 断言绿(含真实 sysroot e2e)。**剩余子片**:match/try/loop region、workflow lower、单态化、**struct-field member projection(读/写)当前 fail-closed 待 value-type 跟踪**、值表示/内存布局(P4)、Core-IR verifier、擦除负例 |
| KR6.5 | WASM codegen 编排层(P5):`Core-IR → WASM`,agent transition/flow/workflow + `ahfl_cap` import(复用 RFC 0019 契约);产物在 wasmtime 真正跑起来,conformance 子集绿 | ⬜ | wasm_backend 从 WAT 骨架升级为真 codegen;wasmtime 集成测试(前置见下 **P-6A 工具链 preflight**) |
| KR6.6 | WASM codegen 计算层(P6):表达式/算术/控制流/match/闭包全部 lower 到 WASM 指令,产物完全自包含 | ⬜ | 覆盖 Core-IR 全节点 |
| KR6.7 | conformance 套件(P7):evaluator e2e/golden 迁移为**引擎无关**用例(`.ahfl` + 期望 output),WASM 全绿 + WASM vs evaluator 差分(状态迁移/capability 序列/output)通过 | ⬜ | evaluator 退役的验收门 |
| KR6.8 | evaluator 退役(P8):conformance 全绿后**原子删除** `src/runtime/evaluator/`,`ahflc run`/REPL/DAP 切 WASM 引擎;`BREAKING CHANGE:` 标注,全套件绿 | ⬜ | 单一执行路径,无 legacy |
| KR6.9 | 分层 IR-JSON 投影(P9):每层 JSON 投影 + 逐字节 round-trip(沿用 KR5.9 方法),旧单层投影标记弃用 | ⬜ | ir_json.cpp 扩展 |

> **Prerequisite P-6A(非计数,不进 Q4 总盘)—— WASM 工具链 preflight。** 这是 KR6.5+ 的
> 前置证据项,不是一个可验收 KR(不占 54 计数)。**实现已落地**(commit `a625efc3` + P0/P1 修复):
> CMake 保留 discovery provenance —— `AHFL_WASMTIME`(用户显式:`-D`/env,不可用 → **fail**)与
> `AHFL_DETECTED_WASMTIME`(PATH 发现,不可用 → **skip**)分开,两路 provenance 经
> `--explicit-wasmtime`/`--detected-wasmtime` 传给 harness(harness 而非 CMake 拥有 skip/fail 决策);
> `tests/scripts/wasm_preflight.py` 决策逻辑(版本正则锚定 `wasmtime`/`wasmtime-cli` 行 / 最低
> ≥15.0.0 / `TimeoutExpired`+`OSError` 按 provenance 归一化 / smoke 真跑 fixture 断言 exit+stdout
> sentinel);手写 152B WASI fixture(`--check` byte-exact,无 wat2wasm 依赖);`wasm_preflight_test.py`
> 15 单测(含真实 CMake 接线分支:同一过低 wasmtime PATH 发现→skip、显式 `-D`→fail);execution
> test **始终注册**(missing→exit 77 可见 skip);`scripts/bootstrap-wasmtime.sh` 显式、默认零联网、
> 版本锁定。**状态:实现已落、真实 wasmtime execution 证据待补** —— 本机拿不到真实 wasmtime
> (无网/不可得),已在真 Node/V8 WASI 引擎验证 fixture 合法可执行;真实最低版本 wasmtime 的
> CLI/WASI 行为确认列为 **KR6.5 开工前 checklist 项**(未关闭证据项,不阻塞 preflight 代码)。

### 6B — query 前端与 IR 单一真相源(RFC 0027)

> 前端从 pass 流水线重构为 salsa 式 query 图,LSP 增量成为架构自然产物;IR 节点单一真相源
> 自动派生 visitor/printer/verifier/serializer,把 8-location sweep 从"漏一处=静默 bug"变成
> "漏一处=编译不过"。两条子线可与 6A 并行。

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR6.10 | RFC 0027 → `accepted`:清 Open Question(SSOT 选型 X-macro vs 代码生成、query 粒度、cycle 策略、RFC 0016 持久化衔接)+ compiler/tooling owner sign-off | ✅ | 5 个 Open Questions 全部给出决策+理由(SSOT 先 X-macro 后按需升级、query 分级粒度对齐 rust-analyzer、cycle 用 coinductive fixpoint 统一 RFC 0013、持久化承接 RFC 0016 cache contract、穷尽性用编译期 static_assert);draft→review→accepted(commit `728db370`) |
| KR6.11 | 自研 QueryEngine 内核(P1)+ 前端 query 化(P2/P3):input storage + memo + revision 失效 + cycle 策略;parse/hir/resolve/type_of 包成 query,driver 经 QueryEngine 求值,全量结果与旧流水线逐位等价 | ⬜ | 纯库 + 等价回归守护 |
| KR6.12 | LSP 切 query + 删手工 incremental(P4/P5):hover/completion/signatureHelp 改读 `type_of`/`hir` query(取代文本启发,关闭 backlog §3.3 缺口);`src/tooling/incremental/` 退役,能力并入 QueryEngine,RFC 0016 cache contract 重锚为持久化层 | ⬜ | 真实编辑序列回归;`BREAKING CHANGE:` |
| KR6.13 | IR 单一真相源(P6/P7/P8):选定机制后,三层 IR 全部节点迁到单一定义,自动派生 visitor/print/verify/json(与手写产物逐字节等价),删除 `expr.hpp:329-344` 的 8-location sweep,加编译期穷尽性门禁(漏节点即编译失败) | ⬜ | 负例:故意漏节点断言编译失败 |

## Objective 7 — 已落库能力产品化到顶尖(backlog §3)

> 依据 `issue-backlog-global-gaps.zh.md` §3.1–§3.7:一批能力**代码已在、但未做到产品级**。
> 本 Objective 把它们从"handler 可用"推到"生产可信"。注:LSP IDE 化(§3.3)已被 Objective 6B
> 的 KR6.12 覆盖,不在此重复。

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR7.1 | Runtime/LLM 生产化(§3.1):hour-scale nightly soak + RSS/allocator 趋势常态观测 + 真实部署环境复跑(从 bounded CI 提升到小时级) | ⬜ | 现有 telemetry/profiling 基础扩展为趋势报告 |
| KR7.2 | Formal backend 真实性深化(§3.5):BMC/k-induction 从"reachability 原型"做成真 k-induction;counterexample 更深源码映射;真实模型检查器矩阵扩展 | ⬜ | **非从零**:已有 contract/property BMC + 反例 source/workflow/capability/contract 映射 + 工具能力矩阵。**剩余**:真 k-induction + 更深/更广生产级验证。`src/verification/formal/bmc.cpp` + counterexample |
| KR7.3 | 质量门禁趋势化(§3.7):compile-time / memory-proxy / SMV-size budget → 趋势报告 + release-blocking 阈值;真实 mutation score(非仅 config report) | ⬜ | **非从零**:真实 mutation runner/score 已完成(backlog 记录 9 个 mutant + 真实 score)。**剩余**:趋势报告、release-blocking 阈值、更多代表样本。现有 `quality-gates` 扩展 |
| KR7.4 | Pass / target backend 产品化(§3.6):扩大 `-O` 收益指标 + target 验收;K8s/Terraform/OpenAPI 视图后端从骨架到产品级语义 | ⬜ | **非从零**:已有 backend/schema baseline。**剩余**:产品级语义 + 验收。passes + infra 后端 |
| KR7.5 | VS Code 真实 Marketplace 发布闭环(§3.3/§四):真实发布演练 + workspace folder extension 序列 + 更深真实编辑序列回归 | ⬜ | **非从零**:extension 已可用。**剩余**:真实 Marketplace 发布 + workspace-folder extension 序列 + 更深真实编辑回归。tools/vscode + 发布脚本 |
| KR7.6 | Distributed scheduler 生产语义(§3.1/四):确定性 + restore 语义债务收口,向真实多 region control plane 推进(或明确记为 P3 冻结依赖) | ⬜ | **非从零**:已有 scheduler baseline。**剩余**:确定性/restore 语义债务 + 生产语义验收。`src/runtime/engine/distributed.cpp` |

---

## 明确排除(Q4 不做)

| 项 | 原因 |
|---|---|
| Rust/Go/Node 宿主 SDK | RFC 0021 已声明为后续工作(embedding ABI 已定,多语言 SDK 未立项) |
| RFC 0018 nuXmv library-mode | WebFetch 墙,license 无法核实 |
| RFC 0004 gRPC transport | L3,已冻结 |

> **2026-08-28 更新**:原「完整 WASM codegen」与「执行型 native-vs-WASM 差分」两项已从本表
> **移出**——它们现由 **Objective 6**(RFC 0026)正式纳入 Q4 范围:WASM 成为唯一执行引擎、
> Core-IR → WASM 完整 codegen、evaluator 经 conformance 差分验收后退役。这是 2026-08-28 架构
> 重构想(RFC 0020 架构北极星)带来的范围扩张,非"无外部运行时依赖"原则的破坏——WASM 由
> 成熟宿主(wasmtime/浏览器)执行,capability 仍经 `ahfl_cap` 边界回调宿主,战略定位不变。

---

## 剩余 KR 推进计划(Next actions)

> **阶段一(Objective 1–4)23/23 KR 已 ✅ —— 可嵌入性 / 验证 / 产品化全部收官。**
> **阶段二(Objective 5)12/12 KR 已 ✅ —— 编译器基座完备性收官。**
> **阶段三(Objective 6–7)4 ✅ + 1 🔵 / 19 KR —— 编译器架构升级到顶尖 + 已落库能力产品化,本季新增开放面。**
> 阶段一最后一个曾被标为环境阻塞的 KR4.5(真实 LLM run 证据)已通过驱动真实本地 llama.cpp
> 推理引擎解除。当前总盘:**54 个 KR,39 ✅ / 1 🔵 / 14 ⬜。** 阶段一+二(35 KR)已收官:KR5.5
> (RFC 0013 → `implemented`)达成,frontmatter `implementing → implemented`(commit
> `9ac22713`),以 §Implementation Plan 阶段验收表 P0–P7 逐条核对为准,全部达成并经
> clean-rebuild 全套件 457/457 验证。收官路上补齐的两处 soundness 洞——跨模块 trait dispatch
> 崩溃(`c52f9730`)与 impl-level where-clause 静默漏检(`0a6d7898`)——均为 Objective-5
> mandate 内的健壮性修复;P5 容器 trait impl 层(`8c4ec5b2`)、P4 `where length<=N` sugar
> (`f93f6458`)、P6 `#![no_prelude]`(`139bce8f`)、first-class `MethodCallExpr`/`TraitDecl`/
> `ImplDecl` IR 节点全部落地。**阶段三(Objective 6–7,19 KR:4 ✅ / 1 🔵 / 14 ⬜)是新的主要开放面:承接
> 2026-08-28 架构重构想,把 RFC 0020 架构北极星 + RFC 0026(`implementing`)/ 0027(`accepted`)推到完整落地——
> 三层 IR 塔、WASM 唯一执行引擎、evaluator 退役、query 前端、8-location sweep 消灭,以及
> backlog §3 的产品化缺口收口。**

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

**阶段二(Objective 5)推进顺序 —— soundness 优先,依赖清晰者先行(12/12 已 ✅):**

1. ~~**KR5.3 `decreases` 终止证明** + **KR5.2 返回位 where-bound**~~ ✅ 已完成:两处 `(void)clause;`
   空 stub 补上真实校验(wildcard-on-cyclic 终止 / 返回位 bound 强制)。
2. ~~**KR5.7 lowering 结构化诊断** + **KR5.8 模块系统诊断入 catalogue**~~ ✅ 已完成:28 处 throw →
   结构化 code+range 诊断;package_graph/discovery blanket code 拆成稳定码 taxonomy。
3. ~~**KR5.9 IR round-trip** + **KR5.10 真实 lowering-equivalence property**~~ ✅ 已完成:
   `parse_program_ir_json` 逐字节 round-trip;lowering_equiv 改为真实中端性质。
4. ~~**KR5.1 方法级 trait tparam 作用域** → **KR5.4 variance** → **KR5.5 RFC 0013 →
   implemented**~~ ✅ 已完成:四项 landing + first-class trait/impl/method-call IR 节点 +
   跨模块 dispatch 崩溃修复 + impl-level where-clause 强制 + P4 where-length sugar + P5 容器
   trait impl 层 + P6 `#![no_prelude]` 全部落地;RFC 0013 frontmatter `implementing →
   implemented`(P0–P7 验收表逐条核对,clean rebuild 457/457)。
5. ~~**KR5.6 parser 错误恢复接线**、**KR5.11 结构相等工具 + 冗余 pass**、**KR5.12 opt-IR 时序片段**~~
   ✅ 已完成:错误恢复接入真实 ANTLR 路径;共享 IR 结构相等 + redundancy pass 补强;opt-IR
   时序原子文档化为有界豁免。

**阶段三(Objective 6–7)推进顺序 —— 架构主干先行,产品化并行(4/19 ✅:设计门 + 塔骨架 + AHFL-IR 层边界):**

1. **KR6.1 / KR6.10 先行(设计门)**:RFC 0026 / 0027 从 draft → accepted——清各自 Open
   Questions + owner sign-off。这是所有实现 KR 的前置(未 accepted 不动代码)。
2. **6A 主干(依赖图,非全程串行 —— 2026-08-29 Codex review 修正)**:准确依赖为
   `KR6.4 → {KR6.5 编排 codegen ∥ KR6.6 计算 codegen} → KR6.7 → KR6.8`。其中
   **① 工具链前置 P-6A(独立小提交,非计数)先落**,再推 KR6.4;② KR6.5/6.6 在 Core-IR
   边界稳定后可按节点集**并行**,不必完全串行;③ **唯一硬门控是 KR6.7 conformance 全绿 →
   KR6.8 删 evaluator**;④ KR6.9 分层 IR-JSON 投影本身可**提前并行**,充当 6.4–6.6 的层边界
   golden/round-trip 锚点(删旧单层投影才依赖 KR6.8)。**6A 是架构关键主干,但不是每一步都不可并行。**
3. **6B 并行(KR6.11→6.13)**:QueryEngine 内核 + 前端 query 化 → LSP 切 query + 删手工
   incremental → IR 单一真相源派生。与 6A 软依赖,可并行推进。
4. **Objective 7 产品化(KR7.1→7.6)**:runtime soak / formal 真实性 / 质量门禁趋势化 /
   pass·target 产品化 / VS Code 发布 / distributed 语义。多数可与架构主干并行;LSP IDE 化已
   并入 KR6.12,不重复。**注:O7 多数 KR 非从零(已有 mutation runner/baseline/extension 等,
   见各行「非从零」标注)。** 6A(WASM 引擎)是架构主干——它同时解锁可部署产物与浏览器
   Playground;但工期未估,6B/O7 大项仍可能成为同等或更长路径,不宜简单称「唯一关键路径」。

**环境阻塞 —— 无已知外部不可解除阻塞(措辞 2026-08-29 收紧):** KR2.5(真实 NuSMV)通过从源码
构建 NuSMV 解除;KR4.5(真实 LLM)通过驱动真实本地 llama.cpp 引擎解除。nuXmv 仍不可得(闭源、
fbk DNS 不可达),但 NuSMV 已覆盖 SMV 检查路径,不影响任何 KR。**待补齐的工具链前置**:本机 / CI
当前无 `wasmtime` / `wasm-tools` / `wat2wasm`,KR6.5 需真实 wasmtime——不是硬阻塞(gated 可选 +
bootstrap),但须在 6A 主干开工前由 Prerequisite P-6A 补齐。**本季无遗留 🚫。**



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
- **阶段二(Objective 5)收官**:12 个 KR 全部 ✅。soundness 级缺口
  **KR5.2/5.3(where-bound / decreases 空 stub)、KR5.4(用户泛型 variance)已全部落地**——
  基座完备的硬指标达成。类型系统深水区(KR5.1/5.4)配足负例 / 语义矩阵测试,未为赶 KR 引入
  临时规则(遵守核心设计原则)。KR5.9/5.10(IR round-trip / lowering-equivalence)后端交接
  边界保真地基已补齐。KR5.5(RFC 0013 → `implemented`)收官:frontmatter 已推进到
  `implemented`,P0–P7 验收表逐条核对达成;过程中补齐跨模块 trait dispatch 崩溃与 impl-level
  where-clause 静默漏检两处 soundness 洞,落地 P4/P5/P6 剩余功能项,clean rebuild 457/457 绿。
- **前期风险已控**:KR3.2/KR3.5 涉及 `TypeCheckPass` 状态剥离与 Typed HIR 新字段的敏感区,
  已配充分负例 / 语义矩阵测试并全绿。
