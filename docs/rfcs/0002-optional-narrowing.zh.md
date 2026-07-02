---
rfc: "0002"
title: "Optional Narrowing in Pattern Matching"
status: "draft"
area: ["language", "compiler"]
stability: "stable-language"
created: "2026-06-25"
updated: "2026-07-02"
authors: ["LLM-orchestrated"]
shepherd: "TBD"
owners:
  language: "TBD"
  compiler: "TBD"
required_reviewers: ["language", "compiler"]
tracking_issue: "TBD"
discussion: "TBD"
implementation_prs: []
decision_due: "2026-07-12"
---

# RFC 0002: Optional Narrowing in Pattern Matching

## Summary

Introduce `if let` and a limited enum-variant narrowing model for `match`, `if let`, and recognized `is_<variant>()` predicates.

## Motivation

AHFL users need a static way to destructure `Optional<T>`, `Result<T, E>`, and user enum variants without relying on panic-prone `unwrap()` calls or full control-flow type analysis.

## Goals

1. Add `if let <pattern> = <expr>` syntax.
2. Reuse the pattern subsystem across `match` and `if let`.
3. Attach variant witnesses to narrowable local paths inside matching branches.
4. Keep narrowing local, conservative, and compatible with the existing bidirectional type checker.

## Non-Goals

1. Do not implement full TypeScript-style CFA or union-type narrowing.
2. Do not make variant witnesses user-visible types.
3. Do not decide match exhaustiveness diagnostics; that is covered by RFC 0003.

## Design

The compiler should derive a temporary variant witness when a pattern match succeeds. The witness narrows the matched path inside the branch and is invalidated by writes or control-flow joins that cannot prove the same witness on every path.

## User Impact

Users can write `if let Some(value) = opt { ... }` and use `value` directly. Existing `unwrap()` code remains legal in draft semantics, but diagnostics can later guide users toward safer bindings.

## Compatibility and Migration

This is primarily additive. Migration risk comes from future lints around redundant or provably unsafe unwraps; those must be staged through warnings before any hard error.

## Implementation Plan

Add parser and AST support for `if let`, implement a narrowing environment in semantic analysis, annotate Typed HIR where needed, then update formatter, diagnostics, LSP hover, and docs.

## Test Plan

Add parser, formatter, semantic narrowing, Typed HIR, LSP hover, and negative tests for `match`, `if let`, `is_some`, `is_none`, and assignment invalidation.

## Rollout and Stabilization

Keep in draft until owner review confirms the `TernaryNarrowingEnv` boundary. Stabilization requires spec updates, negative diagnostics, and conformance tests for all supported narrowing roots.

## Alternatives

1. Only support `match` and reject `if let`.
2. Only support method-based narrowing such as `is_some()`.
3. Implement full path-sensitive CFA with union types.

## Open Questions

1. Whether custom `is_<variant>()` methods require explicit attributes.
2. Whether else-branch complementary narrowing should ship in the first implementation.

## Decision History

- 2026-06-25: Initial draft created during Wave 18 planning.
- 2026-07-02: Canonicalized as RFC 0002.

## Detailed Design Notes

---

## 1 Metadata / 元数据

| Field                    | Value                                          |
| ------------------------ | ---------------------------------------------- |
| RFC-ID                   | 0002                                           |
| Title                    | Optional narrowing in pattern matching (`match` / `if-let`) |
| Status                   | DRAFT                                          |
| Created                  | 2026-06-25                                     |
| Shepherd                 | TBD                                            |
| Consult                  | RFC 0001, RFC 0003                            |
| Decision Due             | 2026-07-12                                     |
| Blocking Items           | TypedHIR TernaryNarrowingEnv 设计 sketch 通过  |
| Author                   | LLM-orchestrated                               |
| Shepherds                | TBD                                            |
| PR                       | TBD                                            |
| Implementation Issue     | TBD                                            |
| Depends On               | NONE                                           |

> 中文对照：
> 本 RFC 讨论在现有 `match e { ... }` 表达式基础上，新增 `if let Some(x) = expr { block } [ else { block } ]` 语法，并对 `match` arm 与 `if-let` 中的 `Option` / `Result` / 自定义 enum variant 提供**类型窄化（narrowing）**——即在匹配成功的分支内，被匹配表达式自动被视为具体 variant，无需显式 `unwrap`。

---

## 2 Motivation & Scope / 动机与范围

### 2.1 Background / 背景

AHFL 当前的双向类型检查器**显式不做**完整的控制流敏感类型窄化（full CFA-based narrowing），因为这会引入路径敏感、别名分析、终止性等额外复杂度，破坏 typecheck 单遍 + 局部双向的架构边界（参见 [`optional-narrowing-rfc.zh.md` §1](../design/optional-narrowing-rfc.zh.md)）。

但在实际 DSL 使用中，`Optional<T>`（以及 `Result<T,E>`、自定义 enum）的解构是**最高频模式之一**。缺少一种"语言原生"的、既能在语法上友好、又能在类型上被证明安全的解构方式，会迫使用户写出大量冗余的 `is_some()` 判断 + 显式 `unwrap()`，既不美观也有运行时 panic 风险。

当前代码示例（现状）：

```ahfl
flow Greeting {
    state Idle {
        let x: Optional<Reply> = ctx.reply;
        if (x.is_some()) {
            // ❌ 仍然需要 unwrap，存在 panic 风险
            let payload = x.unwrap();
            do_something(payload);
        }
    }
}
```

### 2.2 Scope / 适用范围

本 RFC 覆盖：

1. **新增语法** `if let <pat> = <expr> { <block> } [ else { <block> } ]`，与 `match` 表达式共享 pattern 子系统。
2. **类型窄化**：在匹配成功的分支内部，被匹配的 scrutinee（或其绑定路径）的类型被编译器"窄化"为具体 variant，字段可直接访问，**不再需要 `unwrap`**。
3. **一致性原则**：对 `if (x.is_some()) { ... }` 这样的现有调用，也触发等价的窄化——与 Rust 的 pattern-matching-with-method-sugar 一致。
4. `match` arm 中 `Option::Some(t)` / `Option::None`、`Result::Ok(v)` / `Result::Err(e)`、以及**用户自定义 enum variant** 全部受同一窄化机制约束。

> 不覆盖的内容见 §5 Non-Goals。

### 2.3 Typical Usage / 典型使用场景

**场景 1：if-let 解构 Option，避免 unwrap**

```ahfl
flow ShoppingCart {
    state Checkout {
        let coupon: Optional<String> = input.coupon_code;
        if let Some(code) = coupon {
            <!-- See [RFC 0001](0001-enum-variant-payload.zh.md) §3 for struct-variant pattern grammar -->
            // ✅ code 在本分支内类型为 String，直接使用
            let discount = lookup_discount(code);
            apply(ctx.total, discount);
        } else {
            // ✅ coupon 仍为 Optional<String>，不发生窄化
            log("no coupon applied");
        }
    }
}
```

**场景 2：match + Result，双 arm 各自窄化**

```ahfl
fn parse_config(src: Bytes) -> Result<Config, ParseError> {
    let parsed = parser::run(src);
    match parsed {
        Ok(cfg) => {      // cfg: Config
            if cfg.version > 2 { return Ok(cfg); }
            return Err(ParseError::UnsupportedVersion);
        }
        Err(e) => {       // e: ParseError
            log("parse failed: {e}");
            return Err(e);
        }
    }
}
```

**场景 3：自定义 enum + is_some 方法糖的一致性窄化**

```ahfl
enum Status { Active(User), Idle, Blocked(Reason) }

fn greet(s: Status) -> String {
    // 直接 pattern：窄化 s 为 Active(User)，u 绑定为 User
    if let Active(u) = s { return "Hi, " + u.name; }

    // 方法调用：等价窄化（与 Rust `if let` 语义对齐）
    if s.is_active() {
        // s 的类型已窄化为 Active(User)，可直接 .0 访问 payload
        return "Welcome back";
    }

    return "Hello stranger";
}
```

---

## 3 Design / 核心设计

### 3.1 Grammar / 文法

在现有 [`AHFL.g4` L363 `statement` 规则](../../grammar/AHFL.g4) 中新增一个备选 `ifLetStmt`，并添加文法：

```ebnf
// 插入到 statement 备选列表，位于 ifStmt 之后（保持 lexer 关键字顺序稳定）
statement:
    ...
    | ifStmt
    | ifLetStmt        // ← 新增
    ...

ifLetStmt: 'if' 'let' pattern '=' expr block ('else' block)?;
```

关键字 `if` + `let` 的顺序遵循 Rust / Swift 等主流语言习惯，保持用户熟悉度。`pattern` 复用已有的 [`pattern` / `orPattern` / `concatPattern` 规则](../../grammar/AHFL.g4#L468-L505)，不新增 pattern 形态。

在 AST 层（[`ast.cpp` L290 `MatchExpr` 校验](../../src/compiler/syntax/frontend/ast.cpp) 附近）新增 `IfLetStmtSyntax` 节点，字段为 `pattern: PatternSyntax`、`scrutinee: ExprSyntax`、`then_body: BlockSyntax`、`else_body: BlockSyntax?`。

### 3.2 Semantics: `TernaryNarrowingEnv` / 语义：三元窄化环境

引入一个与 `TypeEnvironment` **并行**的新数据结构 `TernaryNarrowingEnv`：

```cpp
// 概念结构（不写死字段，供评审讨论）
class TernaryNarrowingEnv {
  // path → variant-witness 映射
  // path 形如 "identifier" | "ctx.field" | "input.field"，限制为单层路径
  std::unordered_map<NarrowingPath, VariantConstnessWitness> pos_;
  std::unordered_map<NarrowingPath, VariantConstnessWitness> neg_;  // 排除集合
};
```

与 `TypeEnvironment` 的关系：
- `TypeEnvironment` 负责**符号→类型**绑定，保持现有单遍、局部双向架构不变。
- `TernaryNarrowingEnv` 负责**路径→variant 证明**的**控制流敏感**绑定，生命周期仅限于一个 block 内部的顺序执行链。
- 合并规则（ternary 三值）：
  - `then` 分支 = 父 env + `pos_` 插入 pattern 绑定的 witness。
  - `else` 分支 = 父 env + `neg_` 插入 pattern 排除的 witness；若 pos 只覆盖单一 variant，则 else 分支实际上也能得到**互补窄化**（例如 `if let Some(_) = x` 的 else 中 `x` 被证为 `None`）。
  - 汇合点（if-let 后或 match 后）= 各分支 env 的**并集交集**；若某 path 在所有分支的 pos 一致，保留窄化，否则回落为父 env 的原始类型。

### 3.3 Type System: Enum Variant Constness Witness / 类型系统：枚举变体常性见证

定义编译器**内部**的证明概念 `VariantConstnessWitness`：

```cpp
// 概念定义
struct VariantConstnessWitness {
    SymbolId   enum_symbol;   // e.g. Option<T>
    VariantIdx variant_idx;   // e.g. 0 = Some, 1 = None
    std::vector<Type> inner;  // 每个 payload 字段的具体类型
    SourceRange origin;       // 诊断溯源
};
```

该 witness 满足两条性质：
1. **不可伪造**：只能由以下 3 条"信任根"产生：
   - `match` arm 成功匹配具体 variant；
   - `if let <variant-pat> = e` 条件成立；
   - 编译器内置识别 `e.is_some() == true` / `e.is_ok() == true` / 自定义 enum 的 `is_<variant>()` 方法。
2. **不可持久**：仅在当前作用域（及其嵌套作用域）内有效；一旦 path 被赋值（`=`）写入，对应的所有 witness 失效（保守 invalidation，避免别名分析）。

### 3.4 Interaction with TypedHIR / 与 TypedHIR 的交互

遵循"不产新节点，只附加 typing context annotation"原则：

- `IfLetStmtSyntax` 在 resolver 阶段（[`resolver.cpp` L1447 `MatchExpr` 分支](../../src/compiler/semantics/resolver.cpp)）被 lowered 为一条"语义等价的 `match`"，但 AST 上仍保留原 `IfLetStmt`，以便诊断还原。
- TypedExpr / TypedStatement 不新增 variant；通过 [`TypedExpr`](../../include/ahfl/compiler/semantics/typed_hir.hpp#L318) 的 `typing_context_annotations_` 可选字段（本 RFC 新增），在被窄化的表达式节点上附加 `VariantConstnessWitness` 列表。消费者（lower / IR gen / LSP hover）读取 annotation 即可获知当前 path 的窄化状态。
- 这保持了 [`typed_hir_lower.cpp`](../../src/compiler/ir/typed_hir_lower.cpp) 现有 IR 节点种类不变，仅 lowering 时在窄化处省略 `unwrap` 调用。

### 3.5 Deduplication with `is_some()` / `is_none()` / 与方法糖的去重

为实现与 Rust 的"pattern + method sugar 一致性原则"，在 typechecker 的 [`typecheck_expr.cpp` L131 `StdContainerKind::Option` 分支](../../src/compiler/semantics/typecheck_expr.cpp) 处理 `MethodCall` 时，若同时满足：
1. receiver 类型为 `Option<T>` 或 `Result<T,E>` 或"标注 `#[narrowing_method(is_X = VariantName)]` 的自定义 enum"；
2. 方法名属于内置白名单（`is_some` / `is_none` / `is_ok` / `is_err` / 用户声明的 `is_<variant>`）；

则将该 `MethodCall` 视为一条 `NarrowingAssertion` 指令，写入 `TernaryNarrowingEnv`，效果与对应的 `if let Some(_) = x` 完全相同。由此两种写法等价：

```ahfl
// 写法 A：语法糖
if let Some(x) = opt { use(x); }
// 写法 B：方法调用 → 等价窄化
if (opt.is_some()) { use(opt.unwrap()); } // ← unwrap 仍允许，但编译器可将其优化为零开销
```

---

## 4 Alternatives Considered / 被考虑过的替代方案

| # | 方案 / Alternative | Pros / 优点 | Cons / 缺点 |
|---|-------------------|-------------|-------------|
| A | **仅做 `is_some()` 级别的方法窄化**，不新增 `if-let` 语法 | 1) 零文法改动，无 parser/ANTLR 风险；<br>2) 实现最小，仅 1 个 typecheck 扩展点 | 1) 用户仍需手写 `unwrap()`，运行时安全靠的是"优化掉调用"而非"静态保证"；<br>2) 无法优雅地绑定 payload 名称（必须 `x.unwrap().field`）；<br>3) 与 Rust/Swift 主流语法不一致，学习曲线反而高 |
| B | **引入 full CFA + union type**，让所有控制流分支都能窄化（类似 TypeScript） | 1) 表达能力最强，`if (a && b)`、嵌套、返回后的 unreachable 都能窄化；<br>2) 长期看是 typechecker 现代化方向 | 1) 违背 `TypeEnvironment` 单遍设计，需要重写 bidirectional 推断核心；<br>2) 与 TypedHIR 现有节点结构深度耦合，迁移成本 ≥ 本 RFC 的 5x；<br>3) alias / termination 工程复杂度高，无法在本里程碑交付 |
| C | **沿用 `match` 表达式**，把 `if-let` 视为纯粹 desugar（`if let P = E { B } else { C }` → `match E { P => B, _ => C }`），不引入独立窄化 env | 1) 架构极简，resolver 内一个 desugar pass 即可；<br>2) 零新增类型系统概念 | 1) desugar 丢失原始 `if-let` 结构，诊断只能指向 match；<br>2) 无法独立支持 §3.5 的 `is_some()` 方法窄化（因为 match arm 与方法调用是两条路径）；<br>3) 与 RFC 0003 的 exhaustive check 集成时需要反向"desugar source map"增加复杂度 |

**推荐采用方案**：本 RFC §3 的主方案，在 A/B/C 之间取平衡——语法上新增 `if-let`（对齐 Rust）、类型上新增独立 `TernaryNarrowingEnv`（避免侵入 TypeEnvironment）、并通过 desugar + annotation 的方式兼容 match arm（为 RFC 0003 预留接口）。

---

## 5 Non-Goals / 非目标

本 RFC **明确不做**以下事项（留待后续 RFC 或明确拒绝）：

1. **不做全依赖类型（full dependent types）**。`VariantConstnessWitness` 仅在编译器内部以"编译时常性证明"形式存在，不作为一等类型暴露给用户类型签名，例如不允许 `fn f(x: Option<T> where x is Some) -> T` 这种谓词语法。
2. **不做 `match` arm 的穷尽性检查（exhaustiveness checking）**。该功能独立为 **RFC 0003**，本 RFC 仅保证单个 arm 内部的窄化正确；不完整 match 在当前版本仍会回退为"匹配失败时产生 panic / 诊断"，具体策略由 RFC 0003 决定。
3. **不做 pattern guards**（`match arm` 上的 `if <cond>` 进一步参与窄化）。现有语法中 `matchArm: pattern ('if' expr)? '=>' expr`（[`AHFL.g4` L458](../../grammar/AHFL.g4)）的 `if expr` 仅作为**运行时守卫**使用，不写入 `TernaryNarrowingEnv`；将其扩展为编译时窄化条件留给 **e-3**。
4. **不做别名分析**。narrowing 仅作用于 **simple Identifier** 与 **单层成员访问**（`ctx.field` / `input.field` / `obj.field`），嵌套路径 `a.b.c`、通过引用/指针间接访问、`let y = x` 后的 alias，**均不触发反向窄化**——即 `if y is Some { ... }` 仅窄化 `y`，不窄化 `x`。
5. **不做跨循环 / 跨 goto 的窄化持久化**。`TernaryNarrowingEnv` 是**前向单遍**的，在循环头 / `goto` 目标点一律重置为块入口的初始 env（保守且可判定）。
6. **不做 union type 窄化**。AHFL 目前没有 union type，narrowing 仅针对 enum variant。

---

## 6 Migration / Backward Compatibility / 迁移与向后兼容

### 6.1 现有代码兼容策略

- **新增语法**：`if let` 是新关键字序列 `if` + `let`，不与任何现有合法程序冲突（因为现有语法中 `if` 只能接 `(` 或 expr，`let` 不能作为 expr 开头）。
- **新增类型推断行为**：对于原本合法、在 then-block 中使用 `x.unwrap()` 的代码，窄化后 `unwrap()` 仍然合法（witness 存在时编译器可证明它是无 panic 路径），**不产生破坏性变更**。
- **现有 `match` 表达式**：已有 match arm 的 body 在未启用本功能时类型推断结果**完全一致**（`TernaryNarrowingEnv` 为空集时退化为旧行为）。

### 6.2 破坏性变更与 deprecation 路径

本 RFC 不引入破坏性变更。有一处"可选 lint"级别的行为变更：

| 阶段 | Release | 行为 |
|------|---------|------|
| 引入 | vN（本 RFC 稳定版） | 新增 `clippy-style` lint：**`redundant_unwrap_in_narrowed_scope`**。当在已被 narrow 的作用域内调用 `x.unwrap()` 时给出 warn，建议直接使用绑定变量。默认 warn，不视为错误。 |
| 宽限期 1 | vN+1 | lint 默认升级为 deny，但提供 `#[allow(redundant_unwrap_in_narrowed_scope)]` 覆盖；同时 CLI 提供自动化修复（见 §6.3）。 |
| 宽限期 2 | vN+2 | 对于明显"可静态证明永远 panic"的分支（例如 `if let None = x { x.unwrap(); }`），lint 升级为 **hard error**，与 `-Werror` 对齐。其他场景保持 deny+allow。 |

> 即至少 **2 个 release cycle** 的 deprecation 路径。

### 6.3 自动化工具（rustfix 风格）

在 `ahfl fix`（或现有 `ahfl fmt` 扩展模式）中提供子命令：

```
ahfl fix --narrowing-migration <files...>
```

迁移器做两类 rewrite：
1. **`is_some()` → `if let`**：若检测到 `if (x.is_some()) { let y = x.unwrap(); ... }` 模式，整体改写为 `if let Some(y) = x { ... }`（保留局部语义等价性，不变更未被捕获的副作用）。
2. **narrowed scope 中删除冗余 `unwrap()`**：当 witness 存在且 `unwrap()` 被调用在被窄化的变量上时，直接删除 `.unwrap()` 调用（或将表达式替换为 payload 绑定）。

所有迁移脚本以 `--dry-run` 为默认，生成 patch 供用户 review 后再应用。

---

## 7 Rollout Plan + Acceptance Criteria / 上线计划与验收标准

### 7.1 三阶段 Rollout

| 阶段 | 名称 | Feature Flag | 内容 |
|------|------|--------------|------|
| **Phase 1** | Alpha | `#[cfg(feature = "experimental-e1")]`，默认关 | 仅在 compiler-sema 单测目录下编译启用。parser 门控：不启用时 `if let` 与旧行为一致（报错"unstable feature"）。只测 unit level 的 witness 生成 / TernaryNarrowingEnv 合并规则。 |
| **Phase 2** | Beta | `#[cfg(feature = "e1-preview")]`，仍默认关 | CLI golden 输出门控：`tests/golden/` 下新增 ~30 个 if-let + match narrowing golden 文件，覆盖 Option、Result、自定义 enum 与 `is_some()` 方法糖四种入口。 |
| **Phase 3** | Stable | 默认启用，可通过 `--no-e1` 回退 | 正式进入默认 feature flag。此时若回退 `--no-e1`，原本依赖 narrowing 通过的代码会得到"类型错误：x 未被 narrow，尝试 unwrap"的诊断。 |

### 7.2 Acceptance Criteria / 验收标准（可量化）

验收标准全部为**可度量**指标，任一阶段进入下一个阶段前必须全部满足：

1. **单元测试覆盖率**：Alpha 阶段结束时，`tests/compiler/sema/narrowing/` 目录下 **≥ 120/120** 条 assertion 通过（含正例 + 负例 + 跨作用域失效 + 赋值 invalidation 四类），**`ctest -R narrowing` 通过率 100%**。
2. **Golden CLI 回归**：Beta 阶段结束时，`tests/golden/e1/` 下 **≥ 35/35** 个 golden case 通过；同时整个仓库的端到端回归 **ctest 总通过率 ≥ 980/980**（与 wave-17 基线对齐），**零新增失败**。
3. **Mutation 测试分数**：针对 `TernaryNarrowingEnv` + `VariantConstnessWitness` 两处核心模块，`mutationpp` / `mull` 风格 mutation score **≥ 92%**（kill ratio 92/100 mutants killed）。
4. **Fuzzer 稳定性**：`docs/fuzz/` 目录下 grammar-aware fuzzer 针对 if-let + match 组合输入**连续运行 7 天零 crash**、零 sanitizer 报告（ASAN + UBSAN）；单轮 fuzzing corpus 最小 **50k** 样本。
5. **性能回归**：在不启用 narrowing feature 的编译路径上，`tests/benchmarks/typecheck-throughput.ahfl`（~10k LOC）编译耗时**相对 baseline 回归 ≤ 2%**（CI 上 20 次采样均值比较，p < 0.05）。

---

## 8. Decision Block / 决策块

| 评审维度 | 选择 / 说明 |
|---|---|
| **当前状态** | DRAFT · Awaiting inputs |
| **go-criteria 满足度** | (1) Alpha 阶段 narrowing 目录 ≥ 120/120 assertion 通过；(2) Beta 阶段 RFC 0002 golden ≥ 35/35 通过且 ctest 总基线零新增失败；(3) TernaryNarrowingEnv + Witness 模块 mutation score ≥ 92% |
| **no-go 否决条件** | 若 TernaryNarrowingEnv 引入使 typechecker 总体吞吐回归 > 2% 无法回退，则放弃该设计；若 `is_some()` 方法糖窄化与 witness失效机制在 alias 场景下 20% 以上误报，则放弃本 RFC |
| **未解决争议** | (1) 赋值 invalidation 是否支持单层成员访问；(2) else 分支互补窄化（Some→else=None）是否默认启用；(3) `VariantConstnessWitness` 是否暴露给用户类型签名 |
| **依赖其他 RFC** | RFC 0001, RFC 0003 |
| **预计实现工作量** | M（1-4 周）— TernaryNarrowingEnv 结构 1 周 + if-let parser/resolver 1 周 + 方法糖 + TypedHIR annotation 1 周 + golden/fix 1 周 |
| **破坏性变更风险** | LOW — 新增 lint 级 warn（redundant_unwrap），不改变合法程序类型推断；默认 feature off 下零副作用 |
| **Wave 归属** | Wave-20（与 RFC 0001 并列，TypedHIR TernaryNarrowingEnv 需先完成 sketch review） |

---

## 9. References / 参考资料

### 8.1 仓库内文件（可点击链接）

1. [grammar/AHFL.g4:363-374](../../grammar/AHFL.g4#L363-L374) — `statement` / `ifStmt` 文法规则，本 RFC `ifLetStmt` 插入位置参考
2. [grammar/AHFL.g4:454-505](../../grammar/AHFL.g4#L454-L505) — 现有 `matchExpr` / `matchArm` / `pattern` 系列规则，if-let pattern 直接复用
3. [src/compiler/syntax/frontend/ast.cpp:290-370](../../src/compiler/syntax/frontend/ast.cpp#L290-L370) — `MatchExpr` / `MatchArmSyntax` / `PatternSyntax` 的 validator 实现，`IfLetStmtSyntax` validator 镜像位置
4. [src/compiler/semantics/resolver.cpp:1447-1460](../../src/compiler/semantics/resolver.cpp#L1447-L1460) — resolver 中 `ast::MatchExpr` 分支，`if let` lowering 镜像位置
5. [include/ahfl/compiler/semantics/typecheck.hpp:54](../../include/ahfl/compiler/semantics/typecheck.hpp#L54) — `class TypeEnvironment` 声明，`TernaryNarrowingEnv` 设计需与现有接口并行
6. [include/ahfl/compiler/semantics/typed_hir.hpp:318-430](../../include/ahfl/compiler/semantics/typed_hir.hpp#L318-L430) — `TypedExpr` / `TypedStatement` 结构体，narrowing annotation 挂接位置
7. [src/compiler/semantics/typecheck_expr.cpp:106-135](../../src/compiler/semantics/typecheck_expr.cpp#L106-L135) — `StdContainerKind::Option` 分派处，`is_some()`/`is_none()` 方法糖识别点
8. [docs/design/optional-narrowing-rfc.zh.md:1-189](../design/optional-narrowing-rfc.zh.md) — 本仓库已有 narrowing 设计草案，本 RFC 是其正式结构化版本，范围从"仅 if + is 判断"扩展到"match / if-let / 方法糖统一架构"

### 8.2 外部参考

1. **Rust RFC 2909 "match ergonomics" 及相关 `if let` RFC**：RFC 160（原始 `if let` 设计）与 RFC 2005（match ergonomics，binding mode）。
   <https://github.com/rust-lang/rfcs/blob/master/text/0160-if-let.md>
   <https://github.com/rust-lang/rfcs/blob/master/text/2005-match-ergonomics.md>
2. **Swift Language Guide — Pattern Matching 与 Optional Binding (`if let`)**：Swift 的 optional binding 设计是 `if let` 作为语言关键字序列的起源，其 `guard let` / 可选链与本 RFC 的"方法糖一致性原则"可以对照。
   <https://docs.swift.org/swift-book/documentation/the-swift-programming-language/thebasics/#Optional-Binding>
3. **TypeScript Handbook — Narrowing 章节**：涵盖 `typeof` / truthiness / `instanceof` / `in` / user-defined type guards 等多种窄化入口。虽然 AHFL 不做 full CFA，但"三值窄化环境 + 显式信任根 witness"的设计受 TypeScript `control flow analysis` 架构启发。
   <https://www.typescriptlang.org/docs/handbook/2/narrowing.html>
4. **Kotlin Specification — Smart Casts**：Kotlin 的 smart cast（智能强转）是"通过 `if (x is Type)` 自动窄化 x 的类型"的典型实现，其"assignment invalidation"规则与本 RFC §5.4 非目标中的"不做别名分析"策略直接对应。
   <https://kotlinlang.org/spec/type-inference.html#smart-casts>

---

*Document status: DRAFT. Comments welcome on the implementation issue (tracker: TBD).*
*文档状态：草稿。欢迎在实现跟踪 Issue（TBD）中提出评审意见。*

---

### Change history / 变更记录

- 2026-06-28 (V1.1): Added Decision Block §8; Consult / Decision Due / Blocking Items; cross-RFC refs
- 2026-06-28 (V1.0): Initial DRAFT
