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
| KR2.5 | 真实 NuSMV / nuXmv 在 CI 留证 | 🚫 | 本机仅有 z3,NuSMV/nuXmv 缺,需环境提供 |
| KR2.6 | counterexample 更深映射 + AHFL property semantics 深化(§3.5) | ⬜ | — |
| KR2.7 | §3.6 Pass / target backend 产品化;§3.7 门禁趋势化(compile-time / memory-proxy / SMV-size budget → 趋势报告 + release-blocking 阈值) | ⬜ | — |

---

## Objective 3 — 已落库但未产品化的 backlog 主线收口

> 把"有源码 / handler"的能力推到"有用户入口 / 端到端路径 / CI 门禁 / 产品体验"。
> 依据 `issue-backlog-global-gaps.zh.md` §3.1–§3.4。**本 Objective 是本季主要开放面。**

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR3.1 | §3.2 死代码诊断清理:`diagnostics.hpp` 无"定义但零发射"的 ErrorCode | ✅ | 审计 23 个:3 wired(`UNEXPECTED_TOKEN`/`MATCH_ARM_TYPE_MISMATCH`/`EFFECT_INCOMPATIBLE`)+ 补 golden,20 删除;计数 134 → 114。commit 779765a6 / d59510e1 |
| KR3.2 | §3.2 `ConstSema` 收尾:剥离 `TypeCheckPass` 的 source/diagnostic context 状态依赖 | 🔵 | 部分已拆(`const_sema.*`/`expression_sema.*`);剩余状态依赖待剥离 |
| KR3.3 | §3.2 诊断迁移到稳定 code/template/related-notes 的一致性收口 + 语义矩阵测试升级为完成标准 | 🔵 | 大部分已覆盖,一致性收口 + 矩阵完成度待补 |
| KR3.4 | §3.1 Runtime/LLM 生产化:hour-scale soak、RSS/allocator 趋势可本地 / 常态观测 | ⬜ | 目前只在 `Production Confidence` GitHub Actions 跑,本地不闭环 |
| KR3.5 | §3.3 LSP 从"handler 可用"到"IDE 可用":hover/completion/signatureHelp 用 Typed HIR + condition facts,source-graph 级增量失效,真实编辑序列回归 | ⬜ | 后两项纠缠,需新 Typed HIR 字段 |
| KR3.6 | §3.4 工具链入口补齐(P1/P2) | 🔵 | 多数入口已补(fmt/repl/dap/incremental/telemetry),尾巴待收 |

---

## Objective 4 — 结构化 workflow 执行 UX(RFC 0012)收口

> RFC 0012 核心早在 breaking commit `7c3ae2d2` 落地(事件模型 + 投影 + 报告 + renderer +
> `[run]` 工程启动 + 旧 printer 删除),但 frontmatter 曾停在 `accepted` / `implementation_prs: []`。
> 本季做了状态核对与真实缺口补齐,目标是把 RFC 推进到 `implemented` / `stabilized`。

| KR | 目标(可验收) | 状态 | 证据 / 待做 |
|----|----------------|------|-------------|
| KR4.1 | frontmatter 核对为 `implementing`,8 个实现切片逐项审计入 Decision History | ✅ | commit 4a2cfc28;scope-freeze gate 同步放宽为 `accepted\|implementing` |
| KR4.2 | slice 7:TTY/`NO_COLOR` 检测 + 状态词 ANSI(剥离 ANSI 后与纯文本逐字节相同) | ✅ | commit 373e65b5 + renderer 单测 |
| KR4.3 | slice 2:可注入 monotonic clock + fake-clock 确定性单测 | ✅ | commit e980eba1 |
| KR4.4 | RFC 0012 → `implemented`:逐项 beta-gate 证据核对(`check-beta-gate.py` BETA-01..10) | 🔵 | 把"离 implemented 还差什么"从判断变成机器可验证事实(纯脚本,下一步抓手) |
| KR4.5 | RFC 0012 → `stabilized`:同步 release migration note + 真实 LLM run 证据 | 🚫 | 当前 run 证据用 local deterministic stub;真实 LLM 需外部 API,环境阻塞 |

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

## 进入 Q4 的基线(已完成,支撑 Objective 1)

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
- **Objective 4 的下一步抓手是 KR4.4**(beta-gate 逐项核对):纯脚本、可验证,能把 RFC 0012
  的剩余差距从判断变成事实,是推进到 `implemented` 的前提。
- **环境阻塞 KR(🚫)不占主线余量**:KR2.5(NuSMV/nuXmv)、KR4.5(真实 LLM)需外部条件,
  应在环境具备时单独闭环,不阻塞其余 Objective。
- **风险点**:KR3.2/KR3.5 涉及 `TypeCheckPass` 状态剥离与 Typed HIR 新字段,是语义敏感区,
  需充分的负例 / 语义矩阵测试;不要为赶 KR 引入临时规则(违反核心设计原则)。
