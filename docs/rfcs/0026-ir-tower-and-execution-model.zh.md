---
rfc: "0026"
title: "IR Tower and Execution Model"
status: "implementing"
area: ["compiler", "ir", "runtime"]
stability: "experimental"
created: "2026-08-28"
updated: "2026-08-31"
authors: ["zzzode"]
shepherd: "project lead"
owners:
  compiler: "compiler owner"
  ir: "compiler owner"
  runtime: "runtime owner"
required_reviewers: ["compiler", "runtime"]
tracking_issue: "none"
discussion: "none"
implementation_prs: ["0243ff10", "a1a38d1a", "31eded7f", "c48e2c18", "c3aa3ec2", "ddf7f391", "fc06245b", "1e676d3b", "814d3378", "98da0bd9", "86ea43f3", "c60b46fc", "3334fba0", "2df0357c", "a143cd7f", "6a2eb394", "eb26502f", "0109054046", "049465f2", "9abd0569"]
decision_due: "2026-10-15"
---

# RFC 0026: IR Tower and Execution Model

## Summary

本 RFC 把 [RFC 0020](0020-strategic-positioning-embeddable-workflow-dsl.zh.md)
「架构北极星」的前两条（IR 塔、WASM 唯一执行引擎）从定位判据展开为可实施的设计。它做两件事：

1. **用三层 IR 塔取代当前的单层 IR**：`Typed HIR`（诊断层，已有）→ `AHFL-IR`（验证 / 编排层）→
   `Core-IR`（执行层）。验证后端（SMV/SMT/BMC）消费 `AHFL-IR`，执行后端（WASM）消费 `Core-IR`，
   两条路径在塔上**分叉**（Dafny 式），互不污染。
2. **确立 WASM 为唯一执行引擎**：`Core-IR → WASM` codegen 消费 [RFC 0019](0019-wasm-runtime-model.zh.md)
   的 `ahfl_cap` capability import 契约；当前的 tree-walking evaluator
   （`src/runtime/evaluator/evaluator.cpp`）判定为**过渡形态**，在 WASM codegen 通过一致性
   （conformance）验收后**原子删除**。不自研虚拟机。

本 RFC 是**架构与分片计划**决策；具体的 WASM 字节序列 lowering 规则由本 RFC 的
Implementation Plan 分片承载并逐片落地，不在本文一次写死指令级细节。

## Motivation

当前 IR 是**单层**的：`ir::Program`（`include/ahfl/compiler/ir/program.hpp`）持有一个扁平
`ExprArena`，`ExprNode` 是一个 20-alternative 的 `std::variant`
（`include/ahfl/compiler/ir/expr.hpp:347`），`Decl` 覆盖 module/const/struct/enum/agent/flow/
workflow/contract/fn/trait/impl 等（`include/ahfl/compiler/ir/decl.hpp`）。**同一层 IR 同时**
被以下所有消费者共享：

- 形式化验证：`src/compiler/backends/smv/smv.cpp`、`src/verification/formal/`（SMT-BMC、
  k-induction、counterexample）。
- 执行：`src/runtime/evaluator/evaluator.cpp`（tree-walking，2000+ 行）。
- 五个 emit backend：WASM（`src/compiler/backends/infra/wasm_backend.cpp`，当前仅 WAT 骨架）、
  K8s CRD / Terraform / OpenAPI（`src/compiler/backends/infra/`）、IR-JSON
  （`src/compiler/ir/ir_json.cpp`）。

这带来三个不做决策就无法消除的结构性问题：

1. **Altitude 冲突（海拔混装）**：`TemporalExprNode`（`expr.hpp:421`，temporal 算子只有验证
   关心）与执行相关节点、`ContractDecl`（`decl.hpp:213`，验证语义）与 `FnDecl` 执行体挤在
   同一套 IR 里。每个执行后端都被迫"处理或显式拒绝"自己根本不消费的验证节点，反之亦然。
   单态化（执行必需）与 temporal/contract 保真（验证必需）是**互相冲突的降级方向**，压在
   一层 IR 上无法同时最优。

2. **`8-location sweep` 是设计债**：`expr.hpp:329-344` 有一份权威 checklist——每新增一个
   `ExprNode` alternative，必须手改 8 个文件（analysis / ir_print / verify / ir_json /
   opt_lower / visitor / typed_hir_lower / assurance），"漏一处即静默 data-loss bug"。这不是
   纪律问题，是**单层 IR 无分层职责 + 无单一真相源**的必然结果。（消灭 sweep 的机制由
   [RFC 0027](0027-query-frontend-and-ir-ssot.zh.md) 承载；本 RFC 负责把节点按层拆开，缩小每层
   的 variant 面。）

3. **执行路径不是顶尖形态**：唯一执行器是 tree-walking 解释器，无法产出可部署产物、无法上
   浏览器、无法达到编译执行的性能。[RFC 0019](0019-wasm-runtime-model.zh.md) 明确把完整 WASM
   codegen 列为 Non-Goal（只定了 ABI 契约与 WAT 骨架），留下"能 emit 一段跑不了真实 agent 的
   WAT"的缺口。

不做本决策，AHFL 的编译器基座停在"单层 IR + 解释执行",与 RFC 0020 声称的"可验证 + 可高效
嵌入多宿主"北极星有结构性差距。

## Goals

1. **确立三层 IR 塔的职责边界**：明确每层持有什么、擦除什么、被谁消费，使"某构造属于哪一层"
   与"某后端消费哪一层"成为架构约束而非惯例。
2. **确立验证路径与执行路径在塔上的分叉点**：验证后端消费 `AHFL-IR`；执行后端消费 `Core-IR`。
3. **定义 `AHFL-IR → Core-IR` 的降级语义**：单态化、effect 降为显式 capability-call、
   temporal/contract 擦除、控制流结构化（对齐 WASM region）、ADT/闭包显式内存表示。
4. **确立 WASM 为唯一执行引擎**，并定义 `Core-IR → WASM` codegen 的值表示 / 内存模型 /
   capability import 绑定 / 分片交付顺序。
5. **定义 tree-walking evaluator 的退役路径**：conformance 验收标准 + 原子删除条件，使"删除"
   成为可执行的验收门，而非口号。
6. **保持 `AGENTS.md` 非协商原则**：索引式身份、hash-consed 类型 + flat store、
   `std::variant` + visitor、诊断带 `SourceRange`；并保持 RFC 0020 定位不变（计算留宿主、
   capability 是唯一外部效应入口）。

## Non-Goals

1. **不在本 RFC 写死指令级 lowering 表**。`Core-IR → WASM` 的逐节点字节序列由 Implementation
   Plan 分片承载、随实现落地为文档；本 RFC 定层、定值表示、定分叉、定分片。
2. **不引入 MLIR / LLVM**。见 Alternatives：对 AHFL 体量属过度工程；塔用现有
   `std::variant` + flat arena + hash-consing 手写。
3. **不改变 RFC 0020 定位或 A/B 型表达力边界**。计算仍留宿主；本 RFC 只重构编译器内部结构。
4. **不改 capability 嵌入 ABI 的字节契约**。复用 [RFC 0019](0019-wasm-runtime-model.zh.md) /
   [RFC 0021](0021-capability-embedding-abi.zh.md) 的 `ahfl_cap` `(ptr,len)->ptr` 契约。
5. **不做 query 化前端与 IR 单一真相源派生**。那是 [RFC 0027](0027-query-frontend-and-ir-ssot.zh.md)。
   本 RFC 的分层为其铺路，但两者可独立评审。
6. **不承诺 GC**。Core-IR 的内存模型倾向 arena / 区域式管理（见 Open Questions），不引入
   通用垃圾回收。

## Design

### 塔的总览

```mermaid
flowchart TD
    Src["AHFL source"]
    Src --> THIR["Typed HIR (diagnostics layer): source-faithful, fully typed, pre-monomorphization"]
    THIR --> AIR["AHFL-IR (verification / orchestration layer): agent, flow, workflow, contract, effect, temporal, decreases are first-class"]
    AIR --> VER["Verification backends: SMV / SMT-BMC / k-induction"]
    AIR --> VIEW["View backends: K8s CRD / Terraform / OpenAPI"]
    AIR --> LOWER["lower: monomorphize, erase temporal/contract, effect to capability-call, structure control flow"]
    LOWER --> CORE["Core-IR (execution layer): monomorphized, explicit memory layout, WASM-aligned regions"]
    CORE --> WASM["Execution backend: WASM codegen (sole engine)"]
    CORE --> REFEVAL["Transitional: tree-walking evaluator (retired after WASM conformance)"]
```

三层各自的职责、身份、消费者：

| 层 | 持有 | 擦除 / 尚未做 | 主消费者 | 现状锚点 |
| --- | --- | --- | --- | --- |
| **Typed HIR** | 源码 range、完整类型、泛型参数、trait bound、诊断上下文 | 未 lower 到 IR | 诊断 / LSP / const eval | `include/ahfl/compiler/semantics/typed_hir.hpp`、`typed_hir_lower.cpp` |
| **AHFL-IR** | agent 状态机 / flow / workflow DAG / contract / effect 分级 / temporal 算子 / decreases 度量，全为一等公民；泛型**未**单态化 | 无内存布局、无指令级控制流 | 验证后端、视图后端 | 当前 `ir::Program` 的编排 + 验证子集 |
| **Core-IR** | 单态化后的函数体、显式 capability-call、结构化控制流（block/loop/if region）、ADT/闭包显式内存表示、值表示（见下） | temporal / contract / decreases **已擦除**（验证在上层完成）；无源码级泛型 | 执行后端（WASM）、过渡期 evaluator | 新增层 |

**关键不变式**：一个构造只在它该在的层出现。`temporal` / `contract` / `decreases` 在
`AHFL-IR` 是一等公民、在 `Core-IR` 不存在（已被验证消费并擦除）；单态化实例、内存布局在
`Core-IR` 存在、在 `AHFL-IR` 不存在。这消除 Motivation §1 的 altitude 冲突。

### 为什么是三层，而不是两层或四层

- **不能两层**（Typed HIR + 单一 IR，即现状）：验证要保 temporal/contract、执行要单态化擦除
  temporal/contract——同一层无法同时满足，就是今天的冲突。
- **不必四层**（如再插一个 STG/CPS 层）：AHFL 的执行语义是**结构化编排 + capability 调用**，
  不是通用高阶函数求值；闭包是受限的（RFC 0013 Non-Goal：无捕获列表首版），不需要 GHC 那样
  的 STG。Core-IR 直接对齐 WASM 结构化控制流即可。
- **三层的每层都有明确、互斥的消费者**：诊断 / 验证 / 执行。这是 Rust（HIR/MIR 面向不同
  消费者）与 Swift（SIL raw/canonical 分相）验证过的划分。

### AHFL-IR：验证 / 编排层

`AHFL-IR` 是 AHFL 的独特价值层，保留当前 `ir::Program` 里所有**编排与验证语义**：

- **一等公民**：`AgentDecl`（`decl.hpp:180`）状态机、`FlowDecl`（`decl.hpp:261`）、
  `WorkflowDecl`（`decl.hpp:293`）DAG、`ContractDecl`（`decl.hpp:213`）、`CapabilityDecl`
  的 effect 分级（`CapabilityEffectKind`）、`TemporalExprNode`（`expr.hpp:421`）、decreases
  度量（RFC 0013 KR5.3 已接入的 `decreases_recognizer`）。
- **泛型未单态化**：`FnDecl` / `TraitDecl` / `ImplDecl`（`decl.hpp:340/384/402`）保留类型参数与
  trait bound——验证在这一抽象层进行（SMV 建模状态机/temporal，SMT-BMC 建模 bounded 契约）。
- **消费者**：`src/compiler/backends/smv/smv.cpp`、`src/verification/formal/*`、视图后端
  （K8s/Terraform/OpenAPI 投影编排结构）。

`AHFL-IR` 相对今天 `ir::Program` 的**净化**：移出纯执行细节（如把容器字面量 lower 成
`CallExpr` 这类"为执行方便"的构造下沉到 Core-IR 的降级步骤里，`AHFL-IR` 保留更贴近源码语义
的形式），使验证后端看到的 IR 不含"为跑而做"的噪声。

### Core-IR：执行层

`Core-IR` 是 `AHFL-IR` 经 `lower` 得到的执行专用层。降级步骤（每步都是一个 pass，
Implementation Plan 分片）：

1. **单态化（monomorphization）**：泛型 `FnDecl`/`ImplDecl` 按具体类型实例化为单态副本
   （Rust MIR 式，索引式实例身份，复用现有 `include/ahfl/compiler/ir/mangling.hpp` 的
   name mangling 作为实例 key）。Core-IR 无类型参数。
2. **effect 降为显式 capability-call**：`AHFL-IR` 的 capability 调用（带 effect 分级）在
   Core-IR 降为一个显式的 `CapabilityCall{ symbol_id, args_ptr, args_len }` 节点，直接对应
   [RFC 0019](0019-wasm-runtime-model.zh.md) 的 `ahfl_cap` import 帧。这是"计算留宿主"在
   执行层的落点。
3. **temporal / contract / decreases 擦除**：验证已在 `AHFL-IR` 完成，Core-IR 不含这些节点。
   （契约的**运行时检查**若需要，降为普通的 `assert` + 分支，而非 temporal 节点。）
4. **控制流结构化**：`MatchExpr`（`expr.hpp:289`）等降为对齐 WASM `block`/`loop`/`if` 的
   结构化 region（WASM 无任意跳转，必须结构化）；`decreases`-verified 的递归/迭代降为
   WASM `loop`。
5. **ADT / 闭包显式内存表示**：enum payload（RFC 0001 的 variant 形式）、struct、容器
   （RFC 0025 bounded 容量）获得显式线性内存布局；闭包（RFC 0013 首版无捕获列表）降为
   WASM `table` 中的函数索引 + 环境指针。

**值表示（value representation）** —— Core-IR 的核心决策：

| AHFL 类型 | Core-IR / WASM 表示 |
| --- | --- |
| `Bool` / `Int`（有界 `BoundedIntT`） | `i32` / `i64`（按 refinement 宽度选择） |
| `Float` | `f64` |
| `Decimal` / `Duration` | 定点 / 整数纳秒（`i64`），语义见 spec |
| `String` | 线性内存 `(ptr, len)`，UTF-8 |
| enum（带 payload） | `(tag: i32, payload...)`，payload 按最大 variant 对齐 |
| struct | 字段按声明序的线性内存布局 |
| bounded 容器（RFC 0025） | 定长数组 `(ptr, len<=cap)`，cap 是编译期常量 |
| 闭包 | `(func_index: i32, env_ptr: i32)`，env 在线性内存 |
| capability 结果 | `(ptr, len)` 帧，宿主经 `ahfl_cap` 回填 |

内存管理倾向 **arena / bump 分配**（workflow 执行是有界、阶段化的，天然适合区域回收），
**不引入 GC**（见 Open Questions Q1）。

### 执行模型：WASM 唯一引擎 + evaluator 退役

```mermaid
flowchart LR
    CORE["Core-IR"] --> CG["WASM codegen"]
    CG --> MOD["WASM module: transition / step / run + ahfl_cap imports"]
    MOD --> WT["wasmtime host (native)"]
    MOD --> BR["browser host (playground)"]
    WT --> CAP["capabilities via ahfl_cap import (host, any language)"]
    BR --> CAP
```

- **唯一执行引擎**：`Core-IR → WASM`，capability 经 `ahfl_cap` import 回调宿主（复用 RFC 0019
  契约与 `emit_capability_imports`）。执行由成熟宿主（wasmtime / 浏览器）承担，**不自研 VM**。
- **evaluator 退役路径**（可执行的验收门，非口号）：
  1. WASM codegen 覆盖 Core-IR 全部节点；
  2. **conformance 套件**：现有 evaluator 的 e2e/golden 测试（`tests/` 下 evaluator 驱动的
     用例）迁移为"输入 `.ahfl` + 期望 output"的**引擎无关** conformance 用例；WASM 执行结果
     逐一匹配；
  3. capability 调用序列、状态迁移序列、workflow output 在 WASM 与旧 evaluator 上对齐（迁移
     期的差分测试，仅作过渡验收，**不长期保留第二引擎**）；
  4. 全绿后，**同一个 commit 内删除 `src/runtime/evaluator/`**（及其 evaluator-only 依赖），
     WASM 成为唯一执行路径。删除是 Implementation Plan 的显式最后一片。

  正确性此后由 conformance 套件 + AHFL 自身的语义保持验证（CompCert 式，长期可选，见 Open
  Questions Q3）保证，**而非**靠养一个慢解释器。

### backend / target 各消费哪一层（分类学落地）

| 类别 | 成员 | 消费层 |
| --- | --- | --- |
| 执行后端 | WASM codegen | Core-IR |
| 验证后端 | SMV / SMT-BMC / k-induction | AHFL-IR |
| 视图后端 | K8s CRD / Terraform / OpenAPI | AHFL-IR |
| 交换格式 | IR-JSON（每层可各有投影） | 对应层 |

IR-JSON（`src/compiler/ir/ir_json.cpp`，RFC 0013 KR5.9 已做到逐字节 round-trip）扩展为
**每层各有一个 JSON 投影**，作为层间边界的机器可读快照与差分测试锚点。

## User Impact

- **产物形态**：`ahflc emit wasm` 从"WAT 骨架"变为"可在 wasmtime / 浏览器真正执行的 WASM
  模块"。这解锁可部署产物与 Online Playground（后者仍是独立冻结项，本 RFC 只提供执行地基）。
- **执行语义唯一化**：开发内循环（REPL `:eval`、DAP、`ahflc run`）最终统一走 WASM 引擎；
  evaluator 删除后不再有"两套执行语义可能不一致"的风险。
- **诊断 / 验证不变**：Typed HIR 诊断与 SMV/SMT 验证的用户可观察行为不变（它们消费的层未改
  语义，只是从"单层"净化为"AHFL-IR"）。
- **IR-JSON 消费者**：下游工具会看到分层的 IR-JSON 投影；这是新增能力，旧的单层 IR-JSON 在
  过渡期保留直至消费者迁移。

## Compatibility and Migration

**这是一次大型内部重构，对语言语义非 breaking，对内部 IR 消费者 breaking。**

- **语言 / 源码层**：非 breaking。`.ahfl` 源码语义不变。
- **IR 内部结构**：breaking。`ir::Program` 单层结构被三层替代；所有直接依赖当前 `ExprNode` /
  `Decl` 布局的内部代码（analysis / verify / ir_json / opt / visitor / assurance / evaluator /
  backends）随分片迁移。这是内部 API，项目不承诺其前向兼容（见 `AGENTS.md`）。
- **执行行为**：evaluator 删除前，`ahflc run` 行为不变；删除后由 WASM 承担，行为须经
  conformance 套件证明等价。**迁移策略**：evaluator 与 WASM 引擎在过渡期并存并差分测试，
  仅当全套件绿才原子删除 evaluator——用户不会经历"能跑变不能跑"的中间态。
- **持久化 artifact**：IR-JSON 分层投影是新格式；旧单层投影在过渡期保留。artifact 仍
  secret-free、确定性（无 wall-clock/pid/host-path），遵守 artifact-chain 边界。

破坏范围与迁移步骤按 Implementation Plan 分片逐片记录；涉及内部 IR schema 的 commit 在 footer
标注 `BREAKING CHANGE:` 及影响子系统。

## Implementation Plan

按分片交付，每片一个可独立评审、可回归的 Conventional Commit。分片顺序尊重依赖：

1. **P1 塔骨架**：定义 `AHFL-IR` 与 `Core-IR` 的头（`include/ahfl/compiler/ir/` 下新增
   `ahfl_ir.hpp` / `core_ir.hpp` 或等价），先建**空壳类型 + 层边界**,不改行为。
2. **P2 AHFL-IR 净化**：把当前 `ir::Program` 的编排 + 验证子集迁移为 `AHFL-IR`；验证后端
   （smv/smt）改为消费 `AHFL-IR`；golden 无回归。
3. **P3 lower pass：AHFL-IR → Core-IR（骨架）**：单态化 + temporal/contract 擦除 + effect→
   capability-call，先覆盖 agent 状态机 + flow/workflow 编排（不含复杂表达式），产出 Core-IR。
4. **P4 Core-IR 值表示 + 内存布局**：实现 Design §值表示表；ADT/struct/bounded 容器/闭包的
   显式布局；单元 + golden。
5. **P5 WASM codegen（编排层）**：`Core-IR → WASM`，agent transition / flow / workflow +
   `ahfl_cap` import；产物在 wasmtime 真正跑起来；conformance 子集绿。
6. **P6 WASM codegen（计算层）**：表达式 / 算术 / 控制流 / match / 闭包全部 lower 到 WASM 指令。
7. **P7 conformance 套件**：把 evaluator e2e/golden 迁移为引擎无关 conformance 用例；WASM 全绿；
   WASM vs evaluator 差分测试通过。
8. **P8 evaluator 退役**：删除 `src/runtime/evaluator/`；`ahflc run` / REPL / DAP 切到 WASM 引擎；
   全套件绿。`BREAKING CHANGE:` 标注。
9. **P9 IR-JSON 分层投影**：每层 JSON 投影 + round-trip；旧单层投影标记弃用。

每片落地后追加 `implementation_prs` 与 Decision History 条目。

## Test Plan

- **单元**：每层 IR 的构造 / visitor / 布局计算；lower pass 的单态化正确性、temporal/contract
  擦除完备性、effect→capability-call 映射。
- **golden（正 + 负）**：`AHFL-IR` 与 `Core-IR` 的文本 dump golden；`Core-IR → WASM` 的 WAT/WASM
  golden；负例覆盖"验证节点误入 Core-IR""未单态化残留"等结构违规（由分层 verifier 捕获）。
- **conformance（引擎无关）**：`.ahfl` + 期望 output，同时喂 WASM 与（过渡期）evaluator，逐一
  匹配。这是 evaluator 退役的验收门。
- **一致性 / 差分**：WASM 执行的状态迁移序列 / capability 调用序列 / output vs evaluator（过渡期）。
- **round-trip**：分层 IR-JSON 逐字节 round-trip（沿用 RFC 0013 KR5.9 的方法）。
- **真实执行**：`ahflc emit wasm` 产物在 wasmtime 加载执行的集成测试（对标 RFC 0019 的真实
  执行边界）；浏览器 profile 的最小 smoke。
- **fuzz / property**：lower pass 的语义保持 property（对随机合法程序，Core-IR 执行 ≡ AHFL-IR
  参考语义，延续 RFC 0013 KR5.10 lowering-equivalence 的思路）。
- **release-evidence / budget**：WASM 产物 size budget、codegen compile-time budget（沿用现有
  `quality-gates`）。

## Rollout and Stabilization

1. `draft → review`：清零 Open Questions（内存管理、controlflow 结构化边界、语义保持验证
   深度、单态化爆炸预算），compiler + runtime owner sign-off。
2. `accepted`：塔与执行模型被采纳；开始 P1 分片。
3. `implementing`：填 `tracking_issue` / `discussion`，按分片落地，逐片记 Decision History。
4. `implemented`：P1–P9 全部落地，evaluator 已删除，WASM 为唯一引擎，conformance 全绿。
5. `stabilized`：三层塔在 `docs/design` 稳定表述，`docs/spec` 的执行语义引用 WASM 引擎为准，
   路线图以本架构为组织轴。

## Alternatives

1. **保持单层 IR，只加 pass**（现状延续）。**失败原因**：无法解决 altitude 冲突——验证要保
   temporal/contract、执行要单态化擦除,同层不可兼得；`8-location sweep` 债务随节点增长线性
   恶化。这是"一百个小 workaround"，违反 `AGENTS.md` Principle 1。
2. **采用 MLIR 作为 IR 基础设施**。MLIR 提供 dialect / region / 自动化 pass 基建,看似省事。
   **失败原因**:对 AHFL 体量是过度工程——引入巨大 C++ 依赖与构建复杂度、学习曲线陡、且其
   通用性换来的抽象与 AHFL 已验证的 `std::variant` + flat arena + hash-consing 风格
   （`AGENTS.md` Principle 2/3/4）冲突。Rust 手写 MIR 塔、GHC 手写 Core/STG 都没用 MLIR;
   AHFL 该学它们,不该把地基押在一个重依赖上。
3. **采用 LLVM 直接做 native codegen**（跳过 WASM）。**失败原因**:①与 RFC 0020"多宿主 + 可
   移植沙箱"定位冲突——native 产物不可移植、无沙箱;②LLVM 依赖极重;③放弃浏览器 Playground;
   ④WASM 已提供成熟引擎生态,`ahfl_cap` 契约已定,是更短且战略自洽的路径。
4. **自研字节码 VM**。**失败原因**:自定义字节码 + 解释器/JIT + GC + 跨平台 + 调试接入是
   数十人年工程,换来的东西 WASM 生态（wasmtime/浏览器/组件模型/调试）已免费提供且更成熟。
   今天的新语言（MoonBit/Grain/AssemblyScript）全选 WASM 而非自研 VM。
5. **保留 evaluator 作为长期"参考实现",与 WASM 双引擎共存**。**失败原因**:AHFL 只有一家
   实现,不需要"给第三方对齐用的基准";双引擎意味着两套执行语义要永久同步,是持续的
   一致性负担,且违反 Principle 1 的"禁止 just-in-case 死代码"。正确做法是 conformance 套件 +
   语义保持验证保正确性,evaluator 用完即删。

## Open Questions

进入 `review` 前,五个设计问题已给出**决策 + 理由**(共同哲学:执行层薄而确定、内存有界、
爆炸可控;验证深度分"必做工程级"与"长期可选证明级")。具体阈值与证明深度的落地细节由
Implementation Plan 对应分片承载。

1. **Core-IR 内存管理模型** → **决策:arena / region-based,不引入 GC。** workflow 执行是
   有界、阶段化的(agent 状态迁移 / flow 步 / workflow DAG 节点),天然适合按执行 region 分配
   与整体回收;bounded 容器(RFC 0025)容量编译期已知,布局定长。闭包(RFC 0013 首版无捕获
   列表)的环境是受限的,其跨阶段存活用**显式 region 提升**处理(把需跨阶段存活的环境分配到
   更外层 region),而非引入通用 GC。**理由**:GC 与 WASM 线性内存 + 形式化验证的确定性诉求
   冲突(GC 引入非确定回收时机);arena 的分配/回收确定、可建模、与 bounded 验证子集一致。
   若未来出现 region 无法覆盖的真实存活场景,再按数据升级到区域推断,不预先过度设计。
2. **控制流结构化边界** → **决策:全部降为 WASM 结构化 region,不需 relooper。** AHFL 无任意
   goto;`MatchExpr`(`expr.hpp:289`)降为嵌套 `block`/`br_table`,`?`(RFC 0014 try 算子)与
   early-return 降为 `block` + `br` 到块末(结构化 early-exit,WASM 原生支持)。AHFL 的控制流
   本就是结构化的,不会产生不可归约(irreducible)控制流图,故无需 relooper 级算法。**理由**:
   语言层已无 goto,结构化 lowering 是自然映射;这也与"decreases 可验证终止"一致(结构化
   循环才好证终止)。
3. **语义保持验证的深度** → **决策:conformance 差分测试为必做验收门(工程级);CompCert 式
   形式化 lower-证明为长期可选,本 RFC 只留接口不承诺。** evaluator 退役(KR6.8)严格门控在
   conformance 全绿(KR6.7),这是必做。是否把"lower pass 保持参考语义"上升为形式化证明,作为
   AHFL 验证护城河的延伸,列为 accepted 后可独立立项的增强,不阻塞本 RFC 落地。**理由**:
   工程级差分已足以安全退役 evaluator;形式化证明 ROI 高但工程量大,不该绑死在架构落地的
   关键路径上。
4. **单态化爆炸预算** → **决策:复用 RFC 0013 §Design 的"编译期预算 + 缓存"策略,超限
   fail-closed 报诊断。** 单态化实例按 mangled key(`include/ahfl/compiler/ir/mangling.hpp`)
   缓存去重;设一个编译期实例数预算(具体阈值由 KR6.4 分片定,可配置),超限时报一条带
   SourceRange 的可操作诊断(指出爆炸源的泛型调用链),而非静默生成或 OOM。**理由**:
   fail-closed + 可操作诊断符合 Principle 5;预算可配置留调优空间。
5. **过渡期双 IR-JSON 的弃用时点** → **决策:旧单层 IR-JSON 在 evaluator 退役(KR6.8)后、
   所有下游消费者迁移到分层投影(KR6.9)时删除,以 `BREAKING CHANGE:` 标注。** 过渡期两者
   并存,用 round-trip golden 双守护。**理由**:artifact 消费者迁移是渐进的,弃用时点绑定到
   一个明确的里程碑(KR6.9 完成)而非悬空。

## Decision History

- 2026-08-28: Draft opened。承接 [RFC 0020](0020-strategic-positioning-embeddable-workflow-dsl.zh.md)
  「架构北极星」的北极星一（IR 塔）与北极星二（WASM 唯一执行引擎),展开为三层塔设计 +
  `AHFL-IR → Core-IR` 降级语义 + WASM codegen 值表示/内存模型 + evaluator 退役路径 + 9 片
  Implementation Plan。query 化前端与 IR 单一真相源派生转交
  [RFC 0027](0027-query-frontend-and-ir-ssot.zh.md)。
- 2026-08-28: Status draft → review(KR6.1 部分)。五个 Open Questions 全部给出决策 + 理由:
  Core-IR 内存 = arena/region(无 GC);控制流全部降 WASM 结构化 region(无 relooper);
  语义保持 = conformance 差分必做、形式化证明长期可选;单态化 = 编译期预算 + 缓存 + 超限
  fail-closed 诊断;双 IR-JSON 弃用绑定 KR6.9 里程碑。填 shepherd/tracking/discussion。
  **`review → accepted` 待 compiler + runtime owner sign-off**——该步授权一场删除现役
  evaluator、重写 IR 分层的大型重构,须显式批准。
- 2026-08-28: Status review → accepted(KR6.1 达成)。sign-off 经项目 lead 于 Q4 路线图
  Objective 6 确认:路线图已把本 RFC 的完整落地(三层塔 + WASM 唯一引擎 + evaluator 退役)
  列为 Q4 验收项(KR6.1–6.9)并从"明确排除"表移除"完整 WASM codegen",即授权本 RFC 进入
  实现。实现按 §Implementation Plan 9 片推进,KR6.2 起为**零行为变更**的塔骨架;evaluator
  删除(P8/KR6.8)严格门控在 conformance 全绿(P7/KR6.7)之后,过渡期不破坏 `ahflc run`。
- 2026-08-28: Status accepted → implementing。**P1 塔骨架落地(KR6.2,commit `0243ff10`)**:
  新增 `include/ahfl/compiler/ir/tower.hpp`——`Layer` 枚举(TypedHir=0 → AhflIr=1 → CoreIr=2,
  索引式身份)、`Path` 枚举、`path_of()` 编码 Dafny 式分叉(验证消费 AhflIr、执行消费 CoreIr、
  诊断在分叉之上)、零成本 `LayerTag<L>`(后续分片可把"层身份"带进类型,使读错层成为编译错误)。
  `src/compiler/ir/tower.cpp` 用 `static_assert` 锁死层序与层→路径分叉;`tests/unit/compiler/ir/
  tower.cpp` 3 个 doctest 用例。**零行为变更**:无消费者,现有 backend 仍走 `ir::Program`。
  全套件 463/463 绿。
- 2026-08-28: **P2 AHFL-IR 层边界落地(KR6.3,commit `a1a38d1a`)**:alias-first 增量——
  `program.hpp` 新增 `using AhflIr = Program`(doc 注明这是 `tower::Layer::AhflIr` 验证/编排层,
  今日结构同 Program,节点集净化留后续片)。验证/视图后端入口签名 re-point 到 `const ir::AhflIr&`:
  `print_program_smv` / `SmvPrinter::print`(smv)、`lower_k8s_crd` / `lower_openapi` /
  `lower_terraform` / `lower_wasm` / `build_capability_effects`(infra)、`emit_program_smt`(smt)。
  内部 indexing/collection helper 保留 `ir::Program`(别名同型,只标注顶层边界)。**零行为变更**:
  8 文件 +58/-19,SMV/emit-ir/k8s/openapi/terraform golden 逐字节不变;全套件 exit 0。
- 2026-08-28: **P3 Core-IR 骨架 + lower 脚手架首增量(KR6.4 部分,commit `31eded7f`)**:新增
  `include/ahfl/compiler/ir/core_ir.hpp`(namespace `ahfl::ir::core`)——`CoreProgram`(format
  version `ahfl.core.v1`,flat store)、`CoreDecl = variant<CoreAgentDecl>`(最小起步)、
  `CoreAgentDecl`(状态机投影:`SymbolRef` + `mangle_instance` 实例 key + 索引式 `CoreStateId`
  states/initial/finals/`CoreTransition`)。header 注明 temporal/contract/decreases/quota 在此层
  **不可表示(已擦除)**。新增 `src/compiler/ir/core_lower.cpp` 的 `lower_ahfl_to_core(const
  AhflIr&) -> CoreProgram`:lower AgentDecl 状态机(状态 intern 成索引、确定性),其余 Decl kind
  catch-all 跳过(留后续子片)。3 doctest(states/initial/finals/transitions 保真 + contract 不
  泄漏 + 二次 lower 相等)。**additive、零行为变更**:无 backend/evaluator 消费 CoreProgram(WASM
  codegen 是 KR6.5);3 新文件 + 2 CMake 行,全套件 463/463 绿。**KR6.4 剩余子片**:完整单态化、
  effect→capability-call、flow/workflow lower、值表示/内存布局(P4)。
- 2026-08-28: **P3 effect→capability-call lower 子片(KR6.4,commit `c48e2c18`)**:`core_ir.hpp`
  的 `CoreDecl` variant 扩为 `variant<CoreAgentDecl, CoreCapabilityDecl>`。`CoreCapabilityDecl`
  持 `SymbolRef`(canonical 身份)+ `CapabilityEffectKind`(复用枚举)+ `param_types`(ahfl_cap
  marshalling 签名)+ `return_type_ref`——即 [RFC 0019](0019-wasm-runtime-model.zh.md)/[RFC 0020]
  (0020-strategic-positioning-embeddable-workflow-dsl.zh.md) 的 `ahfl_cap` import 显式边界
  (「计算留宿主」的执行层落点);effect kind 之外的 domain/idempotency/receipt/retry/timeout/
  compensation 全部擦除,param name 丢弃。`lower_ahfl_to_core` 加 `CapabilityDecl` visitor 臂
  (确定性、源序;含 per-TU `clone_type_ref` 深拷贝,TypeRef 是 move-only)。3 新 doctest。
  **additive、零行为变更**:仍无 backend 消费;3 文件,ir 测试 59/59、全套件 463/463 绿。
- 2026-08-31: **P4 coercion(annotated-let 类型调整)三段实现链闭合(KR6.4 子片)**。设计
  `docs/design/core-ir-p4-coercion.zh.md`(Codex-approved rev 4,commit `eff693ac`);实现者
  Codex、审查者 Claude 的分工评审循环。**F1(变体/泛型-decl 元数据 + inert typed
  adjustment-plan 数据模型 + IR-JSON 保真)**:`c3aa3ec2` 落地,`ddf7f391` forward-fix
  (显式 RegistrationOrigin、fail-closed wire enum、真实 variance gate、单 SSOT、mono 保真),
  `fc06245b` 二次 forward-fix(无条件 BackendReady variance 长度门 + typed-HIR 内层 kind 校验),
  roadmap 验收记于 `17fb4cc6`。**F2(witness-carrying memoized solver + let 产出)**:`1e676d3b`
  `feat(sema): persist coercion witnesses for annotated lets`——RelationDecision/witness arena、
  producer 接 annotated-let、BackendReady adjustment verifier。评审发现并修复 1 个 P0:top/bottom
  边界(`let x: Any = y`)产空-ops witness,IR verifier 必拒——修法是新增 `ToAny`/`FromNever`
  双 leaf op 贯通 solver→wire→bridge→verifier,`has_top_bottom` 纳入节点末尾短路。**F3(Core
  plan/expr lowering + verifier + probes)**:`814d3378`
  `feat(core-ir): lower and verify coercion plans`——CoreCoercionPlanId/Op/PlanNode + CoreCoerceExpr
  进 CoreExprNode,flow/workflow 各自 normalized proof arena;Core op **不含** variance/ToAny/
  FromNever/VariantToEnum(方向从 `CoreTypeDecl.variances` SSOT 读)。lower_let_boundary 非
  identity 一律 bind_pure(CoreCoerceExpr) 产 fresh SSA(**绝不 re-label**,P0-4);ToAny/FromNever
  在任何 Core type intern 前早拒(唯一 `core.INVALID_COERCION`,不泄漏 Any/Never materialize
  错误);VariantToEnum 仅 Core source==result 擦除;缺 plan 且 Core 类型不同 -> `core.MISSING_ADJUSTMENT`。
  verifier:从 CoreCoerceExpr root 可达性(no-orphan)、全局 3-color 无环、per-op canonical order +
  唯一性 + bounds + TypeArg cov/contra 方向(读 `CoreTypeDecl.variances`,invariant/非法拒)+
  Fn param-contra/return-cov + unnamed 维度覆盖。新增码 `core.MISSING_ADJUSTMENT`/
  `core.INVALID_COERCION` + `core.verify.{COERCION_INVALID,COERCION_KIND_MISMATCH,
  COERCION_VARIANCE_INVALID,COERCION_IDENTITY}`。**additive、零行为变更**(无 backend 消费
  CoreCoerceExpr;物理实现留待 P4-D):dev -Werror 全量 build clean,ir 262/262
  (1743 assertions)、ir_equal 9、ir_json 10、ir_opt 16 绿。KR6.4 剩余:完整单态化、值表示/
  内存布局(P4 主体)、WASM codegen(KR6.5)。
- 2026-08-31: **P4-C nominal member templates 两段实现链闭合(KR6.4 子片,P4-D layout 硬前置)**。
  目标:消除 struct field / enum payload 的 `kInvalid` 类型洞,让后端能跨成员算 layout。旧
  二元 `CoreTypeTemplateRef{Concrete|Param}` 无法表示 `Option<T>` / `Map<String,T>` /
  `Fn(T)->List<T>(4)`,改为**声明拥有的 flat compositional arena**(kind Concrete|Param|Nominal|Fn,
  postorder child<parent,全节点 root-reachable,无 orphan)。**sourcing pin(P0)**:模板 arena 的
  `Param{index}` 只在 `typed_hir_lower.cpp` 从 semantics 层 `StructTypeInfo.fields[].type` /
  `EnumVariantInfo.payload` / `EnumVariantFieldInfo.type`(保留 `TypeVarT::index` 的 TypePtr)构建,
  绝不从已 Any-erased 的 `ir::FieldDecl.type_ref` 反推(与 F1 variance 同一 bridge SSOT)。
  **C1(inert AHFL bridge,commit `98da0bd9`)**:`feat(ir): persist nominal member type templates`
  ——`ir::MemberTypeTemplateNode` arena 挂 `StructDecl`(`member_type_templates` +
  `field_type_template_roots`)/ `EnumDecl`(共享 arena + 每 variant `payload_type_template_roots`);
  ir_json fail-closed wire;`verify.cpp` BackendReady 锁(roots↔slot 数量、kind field mask、
  param bounds、postorder、nominal arity 含"std 未 inline→交 Core SSOT"fallback、Fn return、
  orphan reachable;enum 聚合全 variant roots 后一次性校验共享 arena)。无 Core consumer。
  **C2(Core consumer,commit `86ea43f3`)**:`feat(core): materialize nominal member type templates`
  ——`CoreMemberTypeTemplateNode` arena 挂 `CoreTypeDecl`;`VariantPayload.slot_types` 替换为
  `slot_type_template_roots`,`field_types` 更名 `field_nominal_types`(注释锁 navigation-only、
  禁 layout/codegen 当逻辑类型)。公共 `instantiate_member_template(program,owner,root,owner_args)`:
  memoized DAG + cycle guard、per-kind field mask、Param 从 owner_args 代入、Nominal/Fn 走同一
  shared `ValueTypeArena` hash-cons(重复实例化得同一 CoreValueTypeId、arena 不增长)、**不展开字段**
  (递归 layout 留 P4-D)。`finalize_member_templates` 时序:全 nominal 注册 + `fixup_field_nominal_types`
  后建 shared arena、按声明序转换、原子 publish(完整成功才 move 到 target),失败
  `core.INVALID_MEMBER_TEMPLATE` 带 range。builtin SSOT:`BuiltinEnumDescriptor.Variant` 从
  `payload_arity` 升为 `payload_type_params`(slot→param position:Option::Some=P0/Result::Ok=P0/
  Err=P1),real builtin cross-check 每 slot root 必是 Param 且 param_index 对上 descriptor,否则
  `core.BUILTIN_METADATA_DRIFT`。variance 仍唯一 SSOT 在 `CoreTypeDecl.variances`,模板不复制。
  **additive、零行为变更**(仍无 backend 消费模板;物理 layout 留 P4-D):dev -Werror 全量 build clean,
  ir 269/269(1902 assertions)、ir_equal 9、ir_json 12、ir_opt 16 绿。KR6.4 剩余:值表示/内存
  布局(P4-D 递归 layout)、WASM codegen(KR6.5)。
- 2026-08-31: **P4-D 值表示/内存布局设计硬化(D0,commit `89109d1e`,docs only)**。硬化
  `docs/design/core-ir-p4-value-representation.zh.md` 并同步 coercion 文档的 layout-equality 措辞。
  裁决(reviewer-ruled):**递归 = 方案 A**——Inline 边(struct field / enum payload aggregate)
  vs Indirect 边(collection element/key/value / 未来 closure env);首访 reserve stable placeholder;
  Inline 重入 => `core.layout.INFINITE_RECURSION`;Indirect 回 placeholder;final verifier 只沿
  Inline 三色判环 + 拒 dangling placeholder。**判等 = `value_layouts_equivalent`**:同
  `CoreValueTypeId` 快路径,否则 pair-memo cycle-safe bisimulation;原始 `CoreLayoutId` 相等
  ≠ 物理等价;`value_layouts` 不 hash-cons、每 CoreValueType 唯一。**P0**:`instantiate_member_template`
  当前 mutate program arena,D1 须把 template evaluator 抽成 supplied `ValueTypeArena` 的同一实现,
  layout 用以 `program.value_types` 副本 seed 的 layout-private arena、绝不回写 `CoreProgram`,
  `value_layouts` domain 严格等于原 program arena。**Decimal = i64 unscaled mantissa**(scale 是
  type metadata;literal/const 越界 fail-closed;dynamic overflow runtime trap;无 silent truncation;
  非 bignum handle)——依据 std/decimal + const_sema + runtime `decimal_raw_*` 现有语义。UUID
  inline16/align1;Fn i32 table index;Closure 任意可达路径显式 `core.layout.UNSUPPORTED`(env 布局
  留 D2);Unit size0/align1;Never Uninhabited(非 loadable ZST);unbounded collection
  `core.layout.UNBOUNDED`;align-up/`stride*capacity` checked → `core.layout.OVERFLOW`。
  `compute_core_layouts` 纯函数/确定性(源序 placeholder、确定性 append、原子 publish、二次 compute
  结构相等)。side artifact `CoreLayoutTable` 绝不进 `CoreProgram`。**D1(实现:supplied-arena
  evaluator 重构 + core_layout model/builder/equivalence/verifier + focused matrix)未开始**;D1
  LGTM 后 P4-D 闭合、解锁 WASM codegen(KR6.5)。
- 2026-08-31: **P4-D layout 实现落地(D1,commit `c60b46fc`)**,`feat(core-ir): compute deterministic
  wasm32 layouts`。新增独立 side artifact `include/ahfl/compiler/ir/core_layout.hpp` +
  `src/compiler/ir/core_layout.cpp`(`compute_core_layouts(const CoreProgram&, TargetDataLayout)` +
  `verify_core_layout_table` + `layouts_equivalent`/`value_layouts_equivalent`),`CoreLayoutTable`
  绝不进/改 `CoreProgram`(builder 持 `const CoreProgram&`,编译期强制只读;scratch 用
  `program.value_types` 副本)。P4-C `instantiate_member_template` 抽成
  `instantiate_member_template_into(value_types,types,...)` 共享 evaluator,原 `CoreProgram&` API 转
  forwarding wrapper(行为不变),layout 以私有副本复用同一 evaluator。**递归 = 方案 A 两阶段**:states
  三色 DFS,Inline 重入 => `core.layout.INFINITE_RECURSION`,Indirect 重入回 stable placeholder;
  container header 先 finalize、`stride`/`value_offset`/`backing_size` 在 inline roots finalize 后
  统一 fixup(checked overflow),故 `Node{children:List<Node>(4)}` 终止;final 拒任何 Pending。
  **verifier 双层**:独立 local(target/root 唯一/shape 算术/bounds/no Pending/inline-acyclic/
  reachable-no-orphan)+ **reprojection**(同一 deterministic builder 重算整表、要求
  `*expected == table`,堵"自洽但 Bool root 被篡成 i64"的 mapping 漏洞)。`CoreTypeDecl` 加
  `source_range`,递归/overflow 诊断带 decl range。representation:i32/i64/f64、PtrLen 8/4、UUID
  Bytes16/align1、FnRef i32、Struct/Tuple decl-order、Enum tag+per-variant aggregate、Container
  element/value/capacity/value_offset/stride/backing;Closure/unbounded/overflow/bad-target 全
  fail-closed(`core.layout.{UNSUPPORTED,UNBOUNDED,OVERFLOW,INFINITE_RECURSION,INVALID}`)。
  **additive、零行为变更**(无 backend 消费 layout;`CoreProgram` 不变——probe 实测
  `program.value_types` 布局后相等 + 二次 compute 结构相等):dev -Werror 全量 build clean,
  ir 277/277(1996 assertions)、P4-D focused 8/8(94)、ir_equal 9、ir_json 12、ir_opt 16 绿。
  **P4-D 闭合,KR6.5 WASM codegen 解锁**;KR6.4 值表示/内存布局收尾。
- 2026-08-31: **KR6.5(P5)WASM 编排 codegen 首片 E1 设计(commit `5c8613f5`,docs only)**。
  新增 `docs/design/core-ir-kr6-5-wasm-codegen.zh.md`(rev2,commit `43918af8`;rev1 `5c8613f5` 的
  "empty final handler" 前提被真实 frontend 否定——grammar 要求 `return expr`、validation 要求
  final handler 必须 return-on-all-paths,故空 final 不可构造)。E1 是**最小可执行 vertical
  slice**:编排结构真跑、表达式计算不 lower(严守 P5/P6 层界——RFC:280-282,P5=编排层不含复杂
  表达式,P6=表达式/算术/控制流/match/闭包)。E1 只收单 agent + 单 flow、非 final handler 恰一个
  `CoreGotoStmt`、final handler = **canonical opaque identity return**(精确 ANF
  `CoreLetStmt(CorePathExpr{root=Input, projection=[]})` + `CoreReturnStmt` 同一 SSA、
  input_type==output_type、逐位 CoreValueTypeId 相等)、goto 图全覆盖且必达 final;
  expr/非-identity let/非-canonical return/if/match/cap/workflow + member-projection/literal/
  construct/coerce/output-type-mismatch 全部 `wasm.UNSUPPORTED_ORCHESTRATION`(identity 是结构识别
  + opaque 指针透传,非 Core 值求值,不偷做 P6)。
  纯函数 `emit_core_wasm(const CoreProgram&, const CoreLayoutTable&, target)`;Core + layout 双
  verifier 前置;**P4-D 是唯一 layout 权威**(codegen 零自算 size/offset/align);in-tree canonical
  wasm binary encoder(不经 WAT/wat2wasm,fixed index 表 + canonical LEB128 + 无 names section →
  确定性逐字节相等);initial/finals 读 Core SSOT(不猜 state0/不从出边推 final)。CLI `emit wasm`
  E1 单 agent 切真 binary、多 agent/workflow fail-closed(不拼接/不选 first)。**差分锚点**:repo
  无 Core evaluator(新增会违背 RFC 0026 删引擎目标),故同一 checked frontend 分叉——旧
  AgentRuntime/evaluator vs AHFL→Core→layout→wasm,`wasmtime --invoke step` 比 final CoreStateId;
  transition_count exact-increment 由 always-on binary/structural probe 锁,跨 instance 完整 trace
  留后续 embedded-host harness(不虚报 CLI 可观测)。**ABI 不偷改**:E1 `run` 返 input ptr 不变(纯
  透传,无 load/store/parse/copy/alloc),output aliases input、len==in_len、host 拥有并只 dealloc 一次;
  该 identity alias 是 E1 唯一窄例外、非通用先例,RFC0019(ptr)vs RFC0021(ptr+len)张力显式记录、
  值返回的 `run2` 类方案留独立评审、不 pre-approve。wasmtime
  optional-gated(P-6A provenance:无工具 exit 77 visible SKIP、显式失败 hard FAIL);无 non-skipped
  evidence 不宣称 KR6.5 execution-proven。诊断码 `wasm.{INVALID_CORE,INVALID_LAYOUT,
  UNSUPPORTED_TARGET,ENTRY_AMBIGUOUS,UNSUPPORTED_ORCHESTRATION,NONTERMINATING_E1_RUN,
  BINARY_OVERFLOW,INTERNAL_INVALID}`。后续片:E2 capability boundary、E3 workflow DAG、E4 P5
  conformance,之后才 KR6.6/P6 表达式/算术/match/闭包。**E1 codegen 实现未开始**(design-only)。
- 2026-08-31: **KR6.5 E1 codegen 落地(commit `3334fba0`)**,`feat(wasm): add Core-IR E1
  orchestration codegen`。新增 `src/compiler/backends/infra/core_wasm_codegen.{hpp,cpp}`——纯函数
  `emit_core_wasm(const CoreProgram&, const CoreLayoutTable&, target)`:Core + layout 双 verifier
  前置、single-agent/flow、canonical identity 7 项校验(两句 ANF、Input root/root_type、
  members+projection 空、resolved、result nominal base==input、input==output type、SSA/expr
  CoreValueTypeId 逐位相等、ret==let SSA、no-orphan)、goto total + 三色终止、临时 bytes 完成才
  publish。in-tree 确定性 wasm32 encoder(固定 sections/indices、canonical LEB128、无 import/name/
  custom;`run` 结尾仅 `local.get 0`、frame 零 load/store)。CLI `ahflc emit wasm` 改走
  AHFL→Core→P4-D→binary(不再 lower_wasm/WasmAgentConfig/拼 WAT);多 agent/workflow fail-closed。
  reference-runtime 窄修:`AgentRuntime` 额外 bind reserved local `input`(aggregate),使真实 frontend
  `return input;` 可解析,原 input.field 路径不变。**identity output = E1 唯一窄 alias 例外**
  (run 返 input ptr、len==in_len、host 拥有并只 dealloc 一次、module 不读写 frame),不 pre-approve
  run2。诊断码 `wasm.*` fail-closed 矩阵齐(member-projection/literal/construct/coerce/output-mismatch/
  noncanonical-return -> UNSUPPORTED_ORCHESTRATION 无 artifact;cycle -> NONTERMINATING;multi-agent ->
  ENTRY_AMBIGUOUS;tampered layout -> INVALID_LAYOUT)。**证据分类严格**:always-on binary/structural
  gate(binary_gate/same_frontend_probe/profile/preflight)全绿;real-wasmtime execution differential
  optional-gated,dev 机无 wasmtime 如实 SKIP(exit 77)。**KR6.5 尚未 execution-proven**——需 CI/
  release 有 non-skipped wasmtime pass 才可宣称;本片仅闭合 E1 orchestration spine。验证:full build
  -Werror clean、wasm 33/33、AgentRuntime 43/43、IR 277/1996、ir_equal 9、ir_json 12;reviewer
  独立复核 emit 真 wasm(WebAssembly.validate=true、node 执行 initial_state=1→final=0、
  transition_count=1、run identity alias 运行时确认、双 emit 逐字节相等)。后续:E2 capability
  boundary、E3 workflow DAG、E4 P5 conformance。
- 2026-08-31: **KR6.5 E2 capability boundary 设计(`9ce5f6ef`)+ C1 Core foundation 落地
  (commit `2df0357c`)**。设计 `docs/design/core-ir-kr6-5-e2-capability.zh.md`。审计发现两个 Core-side
  P0 先收成 index-based SSOT(C1,`feat(core): materialize capability signatures and authorization`):
  (1) `CoreCapabilityDecl` 原持 `ir::TypeRef` param/return(双 SSOT、verifier 只能验 arity)→ 收为
  program-global `CoreValueTypeId`(经 body SSA 同一 shared ValueTypeArena 物化,ir::TypeRef 删除)
  + source_range;(2) `CoreAgentDecl` 无 capability whitelist(frontend typecheck_expr.cpp:5006 已验
  agent_info->capability_symbols,但 Core 无法自证)→ 加 declaration-order `vector<CoreCapabilityId>`
  + source_range;两者补 default equality。`CapabilityIndex` 改 strict id-first(present-but-miss
  SymbolId 返 nullopt,不降级 display name)。lower fail-closed:unresolved signature →
  `core.UNRESOLVED_CAPABILITY_SIGNATURE`、unresolved/duplicate agent cap →
  `core.UNRESOLVED_AGENT_CAPABILITY`/`core.DUPLICATE_AGENT_CAPABILITY`,绝不 synth id0。verifier 锁
  capability kind==Capability + 唯一 SymbolId、param/return materialized 非 Never、agent whitelist
  bounds+unique、flow call 逐 arg/result exact CoreValueTypeId + target-agent 授权;**workflow region
  先做同一 signature 检查,再以独立 `CAPABILITY_OUTSIDE_FLOW` 拒(两条 code/两条 path、signature-before-
  reject、不猜 workflow whitelist)**——对齐 frontend「cap 仅 Flow 可调用」语义(typecheck_expr.cpp:4996)。
  新增 verify 码 kCapability{SymbolInvalid,SignatureInvalid,WhitelistInvalid,ArgumentTypeMismatch,
  ResultTypeMismatch,Unauthorized,OutsideFlow}。**additive、零行为变更**(C1 纯 Core foundation、无
  wasm consumer,E1 codegen 不变):dev -Werror clean、compiler_ir 285/285(2077 assertions)、ir_equal 9、
  ir_json 12、wasm 33/33、AgentRuntime 43/43、native_host 17/17、native_wasm_diff 17/17 绿;reviewer
  独立复跑确认。**C2 consumer(wasm import + append-only `run2(in_ptr,in_len)->(status,out_ptr,out_len)`
  + OK/ERROR/PENDING ownership + PENDING latch trap;ahfl_cap 字节契约零改、wire=value_json 非 layout)
  未开始**。KR6.5 仍非 execution-proven(须 CI/release non-skipped conforming-OK wasmtime host)。
- 2026-08-31: **KR6.5 E2-C2 capability consumer 落地(commit `a143cd7f`)**,`feat(wasm): add
  capability run2 boundary`。wasm import section 最小权限(只 import 从 initial 可达的 terminal
  capability,unreachable final cap 不获授权)+ 精确 `ahfl_cap.cap_<SymbolId>` `(i32,i32)->(i32,i32,i32)`
  multi-value ABI、imports 按 CoreCapabilityId 升序、无 source name 泄漏。append-only
  `run2(in_ptr,in_len)->(status,out_ptr,out_len)`、import-aware function indices、ABI ver 仍 1;含 cap
  的 artifact 中 v1 `run` 在任何 effect 前 `unreachable`(不丢 status/len)。三态归一化:OK(nonzero
  ptr+len 透传所有权)/OK-empty→ERROR/ERROR(0,0)/PENDING-null→latch+(PENDING,0,0)/PENDING-nonnull+
  unknown→ERROR。`pending_latched` global 作 run2 **首指令**门禁——suspended instance 再 run2 在读/
  转移新 input 前 `unreachable`(无清 latch 操作,resume 留 E4/RFC0022)。`region_contains_capability`
  递归 CoreIf/CoreMatch,nested/non-final cap → `wasm.UNSUPPORTED_CAPABILITY_FRAME`(不误降 generic
  orchestration)。**零 layout 算术**(P4-D 只读)、wire=value_json opaque forwarding、不做 serializer。
  新增码 `wasm.INVALID_CAPABILITY_ABI`/`wasm.UNSUPPORTED_CAPABILITY_FRAME`。**证据分层诚实**:always-on
  binary/structural 覆盖完整 OK/ownership 路径;Node embedded-host 真执行 run2 OK/result bytes +
  ERROR + PENDING + latch(reviewer 亲跑 PASS);wasmtime CLI `--preload` 仅证 import linkage +
  ERROR/PENDING(无 result frame——preloaded module 无法 alloc target 私有 memory,OK frame 不经此宣称)。
  验证:dev -Werror clean、wasm 50/50、11 wasm ctest(Node + same-frontend + binary gate Passed、
  E1+E2 wasmtime 本机无工具如实 Skipped)、compiler_ir/AgentRuntime/native-host 全绿(reviewer 独立复跑)。
  **KR6.5 仍非 execution-proven**——OK 路径本机 Node 证、wasmtime CLI SKIP,须 CI/release non-skipped
  conforming-OK wasmtime host 才坐实。**E2 完成(C1 `2df0357c` + C2 `a143cd7f`)**;后续 E3 workflow
  DAG scheduler + multi-agent packaging、E4 P5 conformance,之后 KR6.6/P6 表达式/算术/match/闭包。
- 2026-08-31: **KR6.5 E3 workflow DAG + multi-agent packaging 设计(commit `c80a06ac`,docs only)**。
  `docs/design/core-ir-kr6-5-e3-workflow.zh.md`。拆 C1(typed entry + 严格 CLI entry resolution +
  selected-agent plan refactor + deterministic workflow plan/validator,不启 workflow bytes)/ C2
  (workflow codegen)。裁决:**entry identity 解 ENTRY_AMBIGUOUS**——typed
  `CoreWasmEntry = variant<CoreAgentId, CoreWorkflowId>` 为唯一 entry,explicit 缺失无 SymbolId/name
  fallback,metadata-free 仅保 E1 单-agent 兼容、**绝不选 first workflow**,选中 workflow 只 package
  reachable CoreInstanceId(排序去重)、一 module 一 entry。**确定性 topo**:declaration-order FIFO
  Kahn、zero-indegree + successor 升序 CoreWorkflowNodeId、tie 用 node id 破、每 node 恰一次、无
  liveness pruning、与 native WorkflowRuntime 同序。**(A) identity-only borrowed routing**:frame 是
  borrowed opaque wire alias(host 单一所有权、identity runner 不读写不 free、所有 node pair 可 alias),
  首个 reachable capability action/非 identity producer → `wasm.UNSUPPORTED_WORKFLOW_FRAME`;E2
  capability composition(跨 node result ownership/pending/resume)留 E4。**(B) workflow-entry ABI**:
  run2 跑完整 schedule + reset/inc workflow_completed_count;legacy run 安全(本片无 ERROR/PENDING/
  new-length);transition_count 聚合 goto;**step/current_state 在 effect 前 trap**(DAG 无单一 agent
  state);append-only workflow_node_count + workflow_completed_count i32 global;pending_latched 缺席
  (无 cap);E1/E2 agent-entry ABI/bytes 锁不变。workflow-cap 仍 Core-outside-Flow(C1/E2 规则保留);
  P4-D 只读(零 layout 算术);wire=value_json。新增码 `wasm.ENTRY_NOT_FOUND`/
  `wasm.UNSUPPORTED_WORKFLOW_FRAME`(ENTRY_AMBIGUOUS 保留给 metadata-free multi-agent)。证据:
  same-frontend 对 native WorkflowRuntime 差分;Node persistent-instance host 证 counters/schedule/
  OK identity/trap;wasmtime CLI 因分离 instance 不能观测 persistent counter,只证 run passthrough、
  本机无工具 SKIP 77。**KR6.5 仍非 execution-proven**。**E3 codegen(C1/C2)未开始**(design-only)。
- 2026-08-31: **KR6.5 E3-C1 workflow planning foundation 落地(commit `6a2eb394`)**,`feat(wasm):
  add E3 workflow planning foundation`。typed `CoreWasmEntry=variant<CoreAgentId,CoreWorkflowId>`
  成为 target/artifact 唯一 entry(artifact 加 sorted reachable instance metadata);
  `resolve_core_wasm_entry` 是 CLI/package 解析 SSOT:explicit exact-canonical + kind、miss/dup →
  `ENTRY_NOT_FOUND`、unknown ExecutableKind → `ENTRY_NOT_FOUND`(fail-closed,不再 `if Agent else
  Workflow` 误判)、present metadata 无 entry → `ENTRY_AMBIGUOUS`、metadata-free 仅 single-agent/
  no-workflow 兼容、**绝不选 first**;driver 去掉硬编码 agent0。`build_agent_plan` policy 化(去
  whole-program size/workflow gate,explicit agent 在多声明 program 中只取 unique target flow);
  **E1/E2 agent-entry emit 字节逐字节不变**(probe 钉死,reviewer 亲手 emit E1 复核 final_state/
  transition_count/identity 与 node 执行一致),E2 encoder 未改。E3 WorkflowPlan foundation:FIFO Kahn
  (initial/successor 均 id 升序、tie 用 node id)、schedule completeness、reachable CoreInstanceId
  sort+dedup、每 instance 复用同一 agent rule engine(policy 禁 cap)、canonical Path+Yield、exact
  dispatch CVT/node-output/ancestor + schedule/type/finalized-layout,**无二次 subtyping/layout
  engine**;valid plan 返 C1 sentinel `UNSUPPORTED_ORCHESTRATION` 无 bytes(workflow codegen 留 C2)。
  新增码 `wasm.ENTRY_NOT_FOUND`/`wasm.UNSUPPORTED_WORKFLOW_FRAME`。**additive、agent-entry 零回归**:
  dev -Werror clean、wasm 67/67、compiler_ir 285/285、AgentRuntime 43/43、native_host 17/17、
  native_wasm_diff 17/17、11 wasm ctest(Node/gate Passed、3 wasmtime SKIP);reviewer 独立复跑。
  **C2 workflow codegen(internal runner 表 + Kahn dispatch + borrowed alias + run2 schedule +
  counters + step/current_state trap)未开始**;KR6.5 仍非 execution-proven。
- 2026-08-31: **KR6.5 E3-C2 workflow codegen 落地(commit `eb26502f`)**,`feat(wasm): emit
  deterministic E3 workflow modules`。**E3 完成(设计 `c80a06ac` + C1 `6a2eb394` + C2 `eb26502f`)——
  P5 编排 codegen 塔完整:E1 agent + E2 capability + E3 workflow。** 独立 `encode_workflow_module`
  (agent `encode_module` 路径/字节零改——reviewer 亲测 E1 emit md5 C1==C2 相同,证明隔离);private
  runner 按 packaged_instances 升序、逐条执行 C1 已证 goto 链、聚合 transition count、返回
  `(OK,in_ptr,in_len)` **无 frame memory op**。workflow run2 按 C1 Kahn schedule unrolled call、每
  runner OK gate 后 completed++、入口 reset transition/completed、return source 只选 input/completed
  node;legacy run 调 run2 返 ptr;`step`/`current_state` body 首指令 `unreachable` trap(DAG 无单一
  agent state)。globals immutable workflow_node_count/ABI + mutable transition/completed/heap,无
  imports/pending latch。P4-D 仅 C1 finalized-root gate、C2 零 layout 算术;wire=value_json opaque。
  CLI 闭环:EmitWasm 进 package-supported SSOT,`--manifest --target workflow` 真加载 metadata +
  strict typed entry;package CLI vs direct Core target byte-identical(实测 470 bytes)。**additive、
  agent-entry 零回归**:dev -Werror no-work、wasm 69/69、compiler_ir 285/285、AgentRuntime 43/43、
  WorkflowRuntime 194/194、native-wasm 17/17、CLI routing 91/91、15 wasm ctest(Node persistent
  workflow Passed、E3 wasmtime 等 4 项诚实 SKIP);reviewer 独立复跑 + 亲手 emit/Node 执行 workflow
  (schedule=first,second、completed=2、transition=2、identity)。**KR6.5 仍非 execution-proven**——
  workflow 亦 Node 证、wasmtime SKIP。后续 E4 P5 conformance(把 E1-E3 的 wasmtime SKIP 收成真跑
  evidence、坐实 execution-proven),之后 KR6.6/P6 表达式/算术/match/闭包;evaluator 退役(KR6.8)
  严格门控在 conformance 全绿之后。
- 2026-08-31: **KR6.5 E4-A real-Wasmtime conformance closure 设计(commit `3c326f85`,docs only)**。
  `docs/design/core-ir-kr6-5-e4-conformance.zh.md`。**诚实拆 E4-A/E4-B**:E4-A 只把已实现 E1-E3 从
  optional/SKIP 升成 **required real-Wasmtime evidence**,**零 Core/codegen/ABI/字节改动**(reviewer
  后续用 byte-identical gate 证);A 后仅可称"implemented E1-E3 subset execution-evidenced",
  **KR6.5 仍 false**。E4-B(later)才做 capability chain、workflow capability ownership、durable
  resume(RFC0022 P0:runtime type check 不能用 host type-id/layout-id/byte-hash 冒充类型证明,须独立
  design 解)、exact runtime node order。§2.4 honest-status 表 + manifest gate 保 composite-not-exact
  (workflow order 仍 native+structural+executed counters、**非 event log**;`runtime_node_order_observed`
  /`durable_resume_observed` 强制 false)。claim gate fail-closed:拒 any `<skipped>`、错 provenance、
  `execution_proven:true` while resume/order false、unknown manifest field;manifest secret-free +
  deterministic(无 wall-clock/pid/path/allocator/hostname/map-order)。E2 OK 所有权守住(result 从
  callee/target allocator、preallocation 非 shortcut)。**环境 decision gate 显式留 owner**:是否在 CI
  加 Ubuntu required job + wasmtime CLI 28.0.0 + python binding 28.0.0 + importlib_resources 7.1.0
  (全 SHA-256 pin、官方 Bytecode Alliance 源);设计头标 pending,**owner 拍板前不落任何 CI/依赖实现**。
  feasibility audit(文档明确非 evidence):pin 的 toolchain 亲跑 4 harness 全 non-skipped PASS。
  **E4-A 实现(harness required 化 + manifest + checker + CI job)blocked on owner decision**;KR6.5
  仍非 execution-proven。
- 2026-08-31: **KR6.5 E4-B0 wire-type-check / durable-resume seam 设计(commit `7c432b74`,docs only)**。
  `docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md`。E4-B 前置调研,解最硬的 wire runtime type-check
  与 resume record seam。结论:(1) **唯一有资格 decode/验型的是 trusted host adapter**,wasm 保持
  opaque `(ptr,len)`、P4-D 不入 wire,live-args/OK/memo/pending 四路同一规则引擎。(2) **现有两套 check
  不够**(reviewer 独立在代码证实):`value_matches_return_type`(workflow_runtime.cpp:286) 只验外层
  variant(Struct/Enum 不深验、Never→true、bounds 忽略);schema-free `value_from_json`(value_json.cpp:336)
  丢类型(String→String、Array→List、Null→None),故必须 **schema-guided decode**、不能 decode-then-validate。
  (3) SSOT = 从 verified CoreValueType + P4-C member template 投影的 flat cyclic `WireSchemaTable`(index
  identity、非 subtype/layout engine),经确定性 `ahfl.wire-schema.v1` custom section 传输(strict verify +
  import cross-check);module/schema SHA 只 bind artifact、不充当 type proof。(4) **安全**:"literal
  secret-free resume state 不可能"(node input/cap result 是应用数据、可能含 secret)——control record 只
  含 `PayloadSlotId`(无 raw value_json)、payload 走 host-owned confidential+integrity store、现有 plaintext
  `ahfl.workflow-recovery.v2` 不 relabel 成 production-safe;RFC0022 `arg_hash` 只 replay/idempotency、
  命中仍深验;RFC0022 "interned TypeContext handle pointer-equality" 在 fresh process 是 stale 句、expected
  root 从 verified schema table 派生。分片门控:B0-C1 schema model/projector/verifier(无 wasm byte)→
  B0-C2 shared codec + 删 shallow checker(无 resume)→ B1 custom-section transport(首个 capability wasm
  byte change,E1/E3 identity 不变)→ **B2 resume ABI/atomicity/ownership 另审**。**E4-B 实现未开始**
  (design-only);优先级:owner 拍 E4-A 环境 gate 后 E4-A 优先。
- 2026-08-31: **KR6.5 E4-B0-C1 wire-schema projector/verifier/encoder 落地**,链
  `0109054046 -> 049465f2 -> 9abd0569`(均可达、无 amend)。Claude 实现 / Codex 独立复审(FINAL LGTM)。
  - `0109054046` `feat(ir): add Core wire-schema projector/verifier/encoder`:新 `core_wire_schema.{hpp,cpp}`。
    `project_core_wire_schema` 从 verified `CoreProgram` + 严格递增去重的 capability selection 投影 flat
    `CoreWireSchemaTable`(15-shape variant arena、node-id 索引、per-cap params+result);`const CoreProgram&`
    + scratch value-type arena(P4-C `instantiate_member_template_into` 零改 program);reserve-before-descend
    可环;fail-closed:Never/Fn/Closure→`kUnsupported`、非-String Map key→`kUnsupportedMapKey`、
    非 canonical/OOR selection→`kInvalidSelection`、unverified Core→`kInvalidCore`。`verify_core_wire_schema_table`
    = local 图不变量 + 确定性 Core reprojection 等值;`encode_core_wire_schema_table` = magic `AHFLWS` +
    canonical LEB128(**不追加 wasm custom section,transport 属 B1**)。
  - `049465f2` `fix(ir): forward-fix E4-B0-C1 review`:Codex ASan 实锤 2 个同根 P0 + 2 P1。**P0(wire)+P0
    sibling(layout `core_layout.cpp`)**:visitor 前持 scratch arena 的 `const` 引用,descend 中
    `instantiate_member_template_into` hash-cons append 触发 realloc → 第二个 generic field/slot 读悬垂
    nominal = heap-use-after-free;两处均改 **按值 snapshot node 再 visit**。**layout 那处是已落 D1 的
    latent supplied-arena UAF,被本次缺失的 multi-field-generic probe 一并暴露并修复**。P1-1:orphan gate 只在
    首次 `mark()` 才 size `reachable_`,empty-caps 漏检 → `run()` 无条件 `reachable_.assign(nodes.size())`。
    P1-2:node 序改按设计 §2.2 program-global `CoreValueTypeId` first-discovered(原为 cap params/result DFS),
    `canonicalize()` 显式 old→new map 一次 remap 全部 node edge + cap root 后 publish。
  - `9abd0569` `test(ir): close E4-B0-C1 wire-schema coverage per design 7.1`(test-only):补直达
    scalar/refinement(bounds/scale verbatim)、enum Struct payload、shared 两字段 generic UAF regression
    (wire + P4-D layout 双跑 ASan clean)、recursive nominal、Result、以及 dup cap(local gate)/wrong
    source_symbol(reprojection gate)/wrong arity(`kInvalidCore`)/unknown Sequence|Payload kind(local shape
    gate + encoder refuse) negatives(distinctive-message 锁实际 gate)。`compiler_ir 313/313`(dev + 独立
    ASan,detect_leaks=1);**E1 emit md5 `5afff711e859e6db179a1bddd1487333` 不变**;wasm 69/69 + 17 ctest
    (4 real-wasmtime honest SKIP)、agent_runtime 43/43 保持。**下一片:B0-C2 shared schema-guided codec +
    删 shallow `value_matches_return_type`(无 resume)。**
