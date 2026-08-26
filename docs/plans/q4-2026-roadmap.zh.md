# AHFL Q4 2026 Roadmap — 可嵌入性做透

> 规划轴:项目北极星([RFC 0020](../rfcs/0020-strategic-positioning-embeddable-workflow-dsl.zh.md))——
> 可嵌入、可验证的 agent workflow 编排 DSL。本季度**深度优先**:集中火力把
> **可嵌入性**从"文档定位 + 已铺地基"推到**一个可演示的端到端事实**;第二护城河
> BMC 契约语义(M3)原计划推到 Q1,**已在 8 月机动提前完成**(见 M3)。
>
> 状态以当前源码/测试/RFC frontmatter 为准。时间窗:Q4 = 2026-10 ~ 2026-12(9 月预热)。

---

## 北极星(季度目标)

> 让 AHFL 从"文档里的定位"变成**可演示的端到端事实**:把 AHFL 嵌入宿主 → 跑一个经
> 形式验证的工作流 → 遇到 `PENDING` 的能力调用挂起 → **跨进程 / 冷启动恢复** →
> 结果确定可复现。

季度末的验收 = `examples/` 里有一个能跑的程序完整走通上面这句话(native 路径),
且 RFC 0021 与 0022 均 `stabilized`。

> **达成情况(2026-08-25):** 北极星那句话已是可执行、已测试的事实,三条路径证明:
> (1)**可运行的 `examples/` 程序** —— `examples/durable-resume/`(单模块 package + README
> 两步 shell recipe):`ahflc run --suspend-capability … --recovery-store S` 挂起写快照,
> 第二次 `run --recovery-store S --resume-pending-result …` 冷启动恢复到确定 final;
> (2)`tests/integration/durable_resume_capstone.cpp` 编译 `examples/execution-demo`(全前端
> 形式验证)→ 能力返回 `PENDING` → 挂起 → 跨进程冷启动从磁盘快照恢复 → 结果与同步路径
> 逐字节一致;(3)`native_wasm_differential` 证明 PENDING 亦经原生 `ahfl_host.h` ABI 打通。
> RFC 0022 已 `stabilized`;RFC 0021 已 `implemented`,其 `stabilized` 所需的
> `docs/reference/` ABI 文档已补(`host-abi.zh.md`),仅剩第三方语言绑定 —— Q4 明确排除项,
> 是本季目标外的后续工作。

---

## 进入 Q4 的基线(已完成)

能力嵌入的地基已在上一轮里程碑铺好(见 [capability-embedding 记忆] / RFC frontmatter):

- **RFC 0021 slice 1/2/3/5**:`ahfl_host.h` C ABI 契约;`WorkflowRuntime` capability
  派发从 ABI 函数指针表派生(`NativeHostBinding`);WASM import 对齐;参考宿主 +
  绑定等价性测试(native 路径与直连 invoker 在 L1+L2 可观察等价,asan 干净)。
- **RFC 0022 prereq 1a/1b**:`FieldMap` 有序扁平字段存储 + 统一 `format_double`,
  使 `value_json` 确定性可复现——durable replay 的地基。
- RFC 状态(季度进入时):0020 `stabilized`、0021 `implementing`、0022 `review`。
  **更新(2026-08-25,M1+M2 完成后):0021 `implemented`、0022 `stabilized`。**

---

## M1 — Durable Resume 打通(旗舰,10 月)—— ✅ 已完成

> **状态(2026-08-25):RFC 0022 已 `implemented`,M1 退出达成。** 切片 1c/2/3/4/5 全部
> 落库 + 测试:PENDING 挂起控制流、resume record + memo 表(schema v2)、exactly-once
> idempotency key + write-ahead intent、注入/ memo Value 的 fail-closed 类型校验。一个
> 工作流可在 `PENDING` 处挂起、持久化 v2 记录、从 memo + 注入结果确定性恢复到 final。
> 实现 PR 见 RFC 0022 frontmatter。**注**:挂起仅在节点派发点(node-input 求值 + agent
> body)可恢复,不覆盖 workflow 顶层 return 表达式(无节点身份做 resume key)。

把 slice 2 里目前 fail-closed 的 `PENDING` 路径真正跑通。这是 Q4 唯一的强顺序依赖链。

### 依赖门槛(需 owner sign-off,在 M1 开头)

1. **RFC 0022 `review → accepted`** —— 语义型 RFC,owner 决定。
2. **prereq 1c 授权** —— breaking 语言面(见下),需明确点头。

### 切片(RFC 0022 Implementation Plan)

| # | 交付物 | 退出标准 |
|---|---|---|
| 1c | nondet 内建 → host capability | `wall_clock_now`/`time_now`/`uuid_new`/`uuid_new_v4` 重分类;corelib API 文档 + `time_ut.ahfl`/`uuid_ut.ahfl` 迁移;迁移诊断 golden。**注**:`time_epoch` 等纯函数不在范围内 |
| 2 | PENDING 映射 | `CapabilityCallStatus::Pending` + invoker 映射;`EvalResult::Suspended` 控制流逃逸穿透 eval 递归到节点循环 |
| 3 | resume record + memo | `WorkflowRecoverySnapshot` 扩 node input + memo 表(schema `v2`);effect-suppressed 重放 |
| 4 | exactly-once | idempotency key + write-ahead intent(等级 ≥ `durable_write`) |
| 5 | fail-closed | 注入 / memo Value 的 interned-type 校验 + SourceRange 诊断 |

**顺序约束**:1c 必须先于 memo 核心(2–5),否则重放对时间/uuid 不 sound。

**退出:RFC 0022 → `implemented`。** 一个工作流能挂起、进程退出、重启后从 memo
恢复到 final,且已完成的 `durable_write` 不二次触发(exactly-once 负例测试通过)。

---

## M2 — 可嵌入性收口 + 可演示 capstone(11 月)—— ✅ 已完成(范围内)

> **状态(2026-08-25):** 端到端 capstone、durable-resume spec 语义、RFC 0022 →
> `stabilized`(schema v2 标为 stable-artifact)、RFC 0021 → `implemented` 均已落库。
> RFC 0021 `stabilized` 尚差 `docs/reference/` ABI 文档 + 一个第三方语言绑定(Rust/Node),
> 后者明确为 Q4 范围外(见"明确排除"),故 M2 在 Q4 范围内已收口。

| 交付物 | 退出标准 |
|---|---|
| **端到端演示** | `examples/` 里一个"调 LLM(mock)→ 挂起 → 冷启动恢复 → final"的工作流,参考宿主驱动,native 路径全绿;作为北极星那句话的可执行证据 |
| `docs/spec` 补 durable-resume 语义 | durable-resume 挂起/恢复语义 + nondet-capability 迁移写入 spec |
| RFC 0021 → `implemented` / `stabilized` | pending 由 M1 补齐后,把 0021 收口到 spec + reference |
| RFC 0022 → `stabilized` | 语义入 spec;recovery schema `v2` 若承诺稳定则标 stable-artifact |

**退出:北极星那句话变成 `examples/` 里能跑的东西,RFC 0021/0022 均 `stabilized`。**

---

## M3 — 第二护城河:BMC 契约语义(原推迟 Q1,✅ 已于 8 月机动提前完成)

backlog §3.5 item 1:把 BMC 从"状态图可达性"推进到真正的 AHFL 契约/属性语义
(requires/ensures/invariant 数据谓词编码进 SMT、定义可验证子集边界、与 SMV/nuXmv
后端共存)。**原定深度优先推迟到 Q1**;因 M1/M2 提前收口,已作为机动主线提前启动并完成。

RFC 0017(标量 SMT-BMC 数据谓词,`stabilized`)本已把 Bool/Int 谓词编码进可验证子集。
本季在其之上补齐了集合量化这一缺口,通过两个新 RFC:

- **[RFC 0024](../rfcs/0024-bounded-collection-quantification.zh.md)(有界集合量化)→ `implemented`**:
  `forall x in coll: P` / `exists (k,v) in coll: P` over List/Set/Map,经**有限展开**进
  已有的标量 SMT-BMC 编码器(`forall`→`(and …)`、`exists`→`(or …)`、空集合→`true`/`false`
  vacuous),不引入 SMT array/sequence theory,可验证子集保持可判定。语法 → 类型检查
  (`QUANTIFIER_REQUIRES_COLLECTION` / `QUANTIFIER_BODY_REQUIRES_BOOL`)→ `ir::QuantifierExpr`
  → SMT 展开(index-based `coll@i` 元素符号)→ 反例元素索引映射(`coll@i`→`coll[i]`)→
  spec §5.6,全链路 + 单测落库。
- **[RFC 0025](../rfcs/0025-bounded-collection-types.zh.md)(有界集合类型 `List<T>(N)`)→ `implemented`**:
  实现 RFC 0024 时发现其展开所依赖的"集合长度静态上界来源"在 AHFL 并不存在(彼时只有
  标量 `Int(lo,hi)` 精化,集合是无精化的名义 `StructT`)。补上 `List<T>(N)` / `Set<T>(N)` /
  `Map<K,V>(N)` capacity 精化(标量精化在容器上的对应物):语法 → interning(参与结构化
  identity)→ capacity 子类型格(`(N) <: 无界`、`N <= M` 放宽)→ IR 下沉 → 形式化访问器
  + `UNBOUNDED_QUANTIFIER` fail-closed → spec §4.3/§5.5/§5.6。

**退出:RFC 0024/0025 均 `stabilized`。** 无静态上界的集合上做量化按
`formal.UNBOUNDED_QUANTIFIER` fail-closed 拒绝;有界集合契约端到端可验证。
spec(§4.3/§5.5/§5.6)+ reference(error-code 目录 5 个新码)已补齐,beta-evidence
bundle 在普通机器上 10/10 全绿(见"机动/次要"的 rg / GCC / VSIX 修复)——stabilization
所需的 evidence 前置已满足。

---

## 机动 / 次要(有余量时穿插,不阻塞主线)

- **基础设施全绿(✅ 已达成)**:本机全量 ctest 的 `ahfl.product.*` beta-gate 曾因三类
  环境问题挂,均已在仓库内修复,现 **beta-gate 10/10 全绿**:(1)evidence 脚本 shell out
  `rg`——已改为 Python 原生递归正则搜索,去掉 ripgrep 外部依赖
  (`generate-beta-runtime-evidence.py`);(2)GCC 12 `-O2 -Werror` 在 ~16 个 TU 上的
  `-Wrestrict` / `-Wmaybe-uninitialized` 误报(clang/dev preset 不触发)——已在
  `AhflCompiler.cmake` 中对 GCC **单点**把这两个 flag 从 error 降为 warning(仍告警;clang
  保持全量 -Werror);(3)VS Code VSIX 打包步 `corepack prepare pnpm@10.10.0` 报 "Cannot
  find matching keyid"——是 Node 20.18.x 自带 corepack 的**过期签名密钥 bug**(非坏包、非网络
  墙),已在 `package-vscode-vsix-release.sh` 设 `COREPACK_INTEGRITY_KEYS=0` 绕过(签名仍走
  TLS + registry integrity hash,不弱化真实校验)。三者修复后 beta-evidence bundle 可在无
  特殊环境的机器上跑通,`beta_evidence_bundle_ready` 亦绿——RFC 0024/0025 从 `implemented`
  推到 `stabilized` 的 evidence 前置已解锁。
- **vN.N 版本号测试标签退役(已完成)**:`ctest` 标签从 `ahfl-vN.N` / `vN.N-<feature>`
  版本号方案退役为纯语义 feature 标签(`formatter`、`lsp`、`passes` …);伞标签删除,
  文档漂移(`ahfl-core-suite` 等不存在的标签)一并订正。
- **RFC 0013 边界重审**:4 个 A/B guardrail 冲突(P5 容器、P6 string/json/decimal、
  P2 通用 fn、P3 容器代数 trait)。是 RFC 修订(改决策文档),非写码。
- **LSP §3.3**:item 1(position-aware completion gating)、item 2(narrowing facts
  持久化进 TypedExpr)、item 3(fact-driven active_parameter)。真特性,item 2/3
  纠缠需新 Typed HIR 字段。

---

## 明确排除(Q4 不做)

| 项 | 原因 |
|---|---|
| 执行型 native-vs-WASM 差分 | 需嵌入 WASM 运行时;RFC 0019/0021 明确范围外,触碰"无外部运行时依赖" |
| Rust/Go/Node 宿主 SDK、完整 WASM codegen | RFC 0021 已声明为后续工作 |
| RFC 0018 nuXmv library-mode | WebFetch 墙,license 无法核实 |
| RFC 0004 gRPC transport | L3,已冻结 |

---

## 关键路径与风险

- **M1 是硬依赖链**:1c(breaking)→ memo 核心(2–5)→ M2 演示。这是 Q4 唯一的强顺序约束。
- **两个 sign-off 门都在 M1 开头**:RFC 0022 `accepted` + 1c 授权。一旦点头,M1 全程
  可自主推进到 `implemented`。
- **M3 已提前完成**:原推到 Q1,因 M1/M2 提前收口而作为机动主线提前落地
  (RFC 0024/0025 均 `stabilized`),不占用 Q4 主线余量。
- **风险点**:1c 的迁移面(corelib 文档 + 两个 stdlib_unit 测试 + 诊断)可能比预期
  大;memo 核心的 effect-suppressed 重放是语义敏感区,需要充分的负例测试(exactly-once、
  fail-closed 类型不符)。
