---
rfc: "0001"
title: "Enum Variant Payload Forms"
status: "draft"
area: ["language", "compiler"]
stability: "stable-language"
created: "2026-06-28"
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

# RFC 0001: Enum Variant Payload Forms

## Summary

Standardize enum variant payloads as explicit unit, tuple, and struct forms, with struct-variant field names available to pattern matching and diagnostics.

## Motivation

AHFL currently lacks a canonical enum payload model that distinguishes positional payloads from named fields. That ambiguity blocks reliable pattern matching, Typed HIR representation, diagnostics, and downstream language specification work.

## Goals

1. Define enum variant payload shape as a first-class semantic property.
2. Support named-field enum variants and by-name destructuring.
3. Preserve tuple-variant syntax for existing `Optional<T>` and `Result<T, E>` style code.
4. Make invalid tuple-versus-struct pattern usage diagnosable with source ranges.

## Non-Goals

1. Do not introduce anonymous struct literals.
2. Do not implement enum-level generics in this RFC.
3. Do not decide full match exhaustiveness diagnostics; that is covered by RFC 0003.

## Design

The language should model every enum variant as one of three payload shapes: unit, tuple, or struct. Parser, AST, resolver, type checker, formatter, LSP, and IR lowering must preserve that shape instead of recovering it from string names or ad hoc payload vectors.

## User Impact

Users can write self-documenting enum variants such as `Ok { value: T }` and destructure by field name. Existing tuple variants such as `Some(T)` remain valid.

## Compatibility and Migration

This is a source-language extension with compatibility risk around previously ambiguous single-field forms. Migration must keep tuple variants explicit and report shape mismatches with actionable diagnostics.

## Implementation Plan

Implement parser and AST support first, then resolver/type checker shape validation, then Typed HIR/IR serialization, formatter, LSP, and documentation updates.

## Test Plan

Add parser, formatter, resolver, type checker, diagnostic, Typed HIR serialization, and golden tests for unit, tuple, and struct variants, including invalid shape and duplicate-field cases.

## Rollout and Stabilization

Keep the RFC in draft until owner review resolves the interaction with RFC 0003. Stabilization requires spec updates, conformance tests, and no remaining `TBD` fields in this RFC metadata.

## Alternatives

1. Keep tuple-only variants and require named structs outside enums.
2. Treat struct variants as sugar over tuple variants.
3. Adopt a full algebraic data type model immediately, including enum-level generics and nested patterns.

## Open Questions

1. Whether default values on struct-variant fields belong in the first implementation slice.
2. Whether short unqualified struct-variant patterns should be accepted before module-resolution hardening is complete.

## Decision History

- 2026-06-28: Initial draft created during Wave 18 planning.
- 2026-07-02: Canonicalized as RFC 0001.

## Detailed Design Notes

> **English Abstract / 英文摘要**: This RFC standardizes AHFL enum variant payloads into two
> explicit forms — tuple-variant `Name(T1, T2)` and struct-variant `Name { f1: T1, f2: T2 }` —
> replacing the previous ambiguous shape. It enables by-name destructuring in pattern matching
> (`Ok { data, meta }`), removes the ambiguity between single-field tuple variant and previous
> parenthesized type, and reserves variant identifiers at the enum scope to prevent struct-name
> shadowing. Semantic shape becomes first-class (you cannot pass a tuple-variant value where a
> struct-variant pattern is expected even if field types are isomorphic). The design explicitly
> defers enum-level generics, anonymous struct literals, and exhaustive-match lint to separate
> RFCs (PB-01, RFC 0002). Implementation is blocked on this RFC merging (`docs/plans/phaseb-gap-analysis.zh.md:128`).
>
> **中文摘要**: 本 RFC 将 AHFL 枚举 variant 的 payload 统一为两种显式形式——tuple-variant
> `Name(T1, T2)` 与 struct-variant `Name { f1: T1, f2: T2 }`——取代遗留的含混写法。
> 它使 pattern matching 中支持按名解构（`Ok { data, meta }`），消除单字段 tuple variant
> 与遗留括号类型间的歧义，并在 enum 作用域内保留 variant 标识符以防止 struct 名 shadow。
> 语义形状成为一等公民（即使字段类型同构，也不能在期望 struct-variant 模式的地方传入
> tuple-variant 值）。设计明确将 enum 级泛型、匿名 struct literal、穷尽性检查 lint
> 推迟到各自的独立 RFC（PB-01、RFC 0002）。实现需等本 RFC 合并后方可启动（`docs/plans/phaseb-gap-analysis.zh.md:128`）。

---

## 1. Metadata / 元数据

| Field / 字段 | Value / 值 |
|---|---|
| **RFC-ID** | 0001 |
| **Title** | Enum Variant Named Fields (Struct + Tuple Variant) |
| **Status** | DRAFT |
| **Created** | 2026-06-28 |
| **Shepherd** | TBD |
| **Consult** | RFC 0002, RFC 0003 |
| **Decision Due** | 2026-07-12 |
| **Blocking Items** | 与 RFC 0003 的 arm 语法一致性 review |
| **Author** | LLM-orchestrated |
| **Shepherds** | TBD |
| **PR** | TBD |
| **Implementation Issue** | TBD (per `phaseb-gap-analysis.zh.md:128-129`) |
| **Depends On** | NONE (RFC 0002 is parallel, not blocking; PB-01 Generics is downstream) |

---

## 2. Motivation & Scope / 动机与适用范围

### 2.1 Background / 背景

当前 AHFL 枚举声明在 `grammar/AHFL.g4:82-88` 中仅支持两种含混形态：

```
enumVariant: IDENT ('(' typeList ')')?;
```

这产生三个实际问题：

1. **单字段歧义**：`Ok(String)` 在语义上是 "tuple of arity 1" 还是 "previous 括号包裹的一个类型"？用户无法区分，而 pattern match 中对 `Ok(x)` 的解构也无法体现字段名。
2. **无命名载荷**：用户无法在 variant 上表达 `HttpResponse { status: Int, body: Bytes, headers: Map }` 这种自文档化的字段集，只能退而写成元组并用注释标注序号。
3. **模式匹配约束**：当前 `variantPattern`（`AHFL.g4:494-496`）只支持位置解构 `Some(x)`，用户在多字段 variant 中必须记住字段顺序，无法按名绑定（Rust/Swift/TypeScript 均已支持）。

AHFL Core-3 的 PB-01 基线（`docs/plans/phaseb-gap-analysis.zh.md:29`）已将本项列为 **P1 / M7**，并在 wave-16 集成报告的 Blocked 清单中登记为 **"无 RFC；需 core steering 组签核 enum variant 语义 spec"**（`docs/plans/wave-16-integration-report.zh.md:164`）。本 RFC 的目的即填补该设计空白，解除对应 BLOCKED 状态。

### 2.2 Scope / 适用范围

本 RFC 规范 **EnumType** 的 variant payload 形态：

- 将此前的"隐式三选一"（`Name` / `Name(Type)` / `Name{...}` 混合）统一为两种 **显式** 形式：
  - **tuple-variant**：`Name(T1, T2, ...)` — 位置字段，与现法兼容（但语义规范化）。
  - **struct-variant**：`Name { f1: T1, f2: T2, ... }` — 命名字段，本次新增。
- 同一 enum 中 **允许** tuple-variant 与 struct-variant 共存（例如 `Result { Ok { value: T }, Err(String) }`），但 **禁止**此前的 `Name(Type)` 单字段隐式、以及在同一 variant 内混用位置与命名。
- 在 `match` arm 解构中，struct-variant 支持按名解构、部分绑定、`..` 剩余通配。

**Out of this RFC but immediately downstream**：variant 命名解构的实现细节（d-2）在实现层面依本 RFC 的语义约束展开，不单独出 RFC。

### 2.3 Typical Use Cases / 典型使用场景

**场景 1 — Result 类型，按名解构 error 元信息**

```ahfl
enum Result<T, E> {
    Ok { value: T },
    Err { code: Int, message: String, recoverable: Bool },
}

fn report(r: Result<Bytes, IoError>) effect Pure -> String {
    match r {
        Ok { value } => "read ${value.length()} bytes",
        Err { code, message } => "error ${code}: ${message}",   // partial bind
    }
}
<!-- See [RFC 0003](0003-match-exhaustiveness-diagnostics.zh.md) §3.2 for MATCH_MISSING_PATTERNS 码对 struct-variant arm 的交互 -->
```

**场景 2 — Http 响应，自文档化 struct-variant + 元信息 tuple-variant 混合**

```ahfl
enum HttpResponse {
    Success { status: Int, body: Bytes, headers: Map<String, String> },
    Redirect(Int, String),                // tuple-variant: (status, location)
    Failure { code: Int, detail: String },
}

fn is_permanent(r: HttpResponse) effect Pure -> Bool {
    match r {
        Redirect(code, _) => code >= 308,
        Failure { code, .. } => code >= 500,   // `..` 剩余字段忽略
        _ => false,
    }
}
```

**场景 3 — 可选值语义，与现有 Optional 布局兼容**

```ahfl
// stdlib Optional<T> 保持 tuple-variant，与现有代码兼容：
enum Optional<T> {
    None,
    Some(T),            // tuple-variant, arity-1: 显式合法（见 §6）
}

fn greet(o: Optional<String>) effect Pure -> String {
    match o {
        Some(name) => "Hello, ${name}",
        None => "Hello, stranger",
    }
}
```

---

## 3. Design / 核心设计

### 3.1 Grammar Changes / 文法变更

**Before**（`grammar/AHFL.g4:82-92`）：

```
enumDecl: 'enum' identifier typeParams? '{' enumVariant (',' enumVariant)* ','? '}';
enumVariant: IDENT ('(' typeList ')')?;
typeList: type_ (',' type_)* ','?;
```

**After**：

```
enumDecl: 'enum' identifier typeParams? '{' enumVariant (',' enumVariant)* ','? '}';

enumVariant:
      structFieldsVariant      // NEW: 命名结构载荷
    | tupleFieldsVariant       // EXISTING, 规范化：位置元组载荷
    | unitVariant              // EXISTING, 规范化：无载荷 unit
    ;

// Unit variant: bare identifier, payload is the empty product.
unitVariant: IDENT;

// Tuple variant: `Name(T1, T2, ...)` — explicit positional payload.
// The `typeList` rule is REUSED unchanged (AHFL.g4:92).
tupleFieldsVariant: IDENT '(' typeList ')';

// Struct variant: `Name { f1: T1, f2: T2, ... }` — NEW.
// Reuses `structFieldDecl` pattern (AHFL.g4:80) but with COMMA separator
// (because enum variant body is brace-comma, struct body is brace-semicolon).
structFieldsVariant: IDENT '{' structVariantField (',' structVariantField)* ','? '}';

structVariantField: IDENT ':' type_ ('=' constExpr)?;
```

**Pattern grammar**（`AHFL.g4:494-496`）同步扩展：

```
// Before:
variantPattern:
    IDENT '::' IDENT ('::' IDENT)* ('(' patternList ')')?
    | IDENT '(' patternList ')';

// After:
variantPattern:
    // fully-qualified + tuple pattern
      IDENT '::' IDENT ('::' IDENT)* '(' patternList ')'
    // fully-qualified + struct pattern   (NEW)
    | IDENT '::' IDENT ('::' IDENT)* '{' patternField (',' patternField)* ','? '}'
    // short tuple pattern
    | IDENT '(' patternList ')'
    // short struct pattern   (NEW)
    | IDENT '{' patternField (',' patternField)* ','? '}'
    // fully-qualified unit variant (no payload)
    | IDENT '::' IDENT ('::' IDENT)*
    ;

patternField:
      IDENT ':' pattern          // explicit rename: `name: p` binds p under f.name
    | IDENT                      // shorthand: `name` = `name: name`
    | '..'                       // rest-pattern: ignores unmapped fields
    ;
```

### 3.2 AST Changes / AST 变更

当前 `EnumVariantDeclSyntax` 仅持有 `variant_name: string` + `payload: vector<TypeSyntaxPtr>`（见 `src/compiler/syntax/frontend/ast.cpp:753-769` 的验证逻辑）。本 RFC 引入 **variant_kind discriminant + payload union**：

```cpp
// In syntax AST (conceptual; exact placement follows existing NodeKind scheme).
enum class EnumVariantKind {
    Unit,          // no payload
    Tuple,         // positional: vector<TypeSyntaxPtr>
    Struct,        // named:      vector<NamedFieldSyntax>
};

struct NamedFieldSyntax {
    std::string name;
    TypeSyntaxPtr type;
    std::optional<ConstExprSyntaxPtr> default_value;
    SourceRange range;
};

struct EnumVariantDeclSyntax : DeclSyntax {
    std::string variant_name;
    EnumVariantKind kind;
    // union-style payload, discriminated by `kind`:
    std::vector<TypeSyntaxPtr> tuple_fields;        // iff kind == Tuple
    std::vector<NamedFieldSyntax> struct_fields;    // iff kind == Struct
};
```

**AST 校验 (ast.cpp validate_node)**：

- `kind == Tuple` → `tuple_fields.size() >= 1`，`struct_fields.empty()`。
- `kind == Struct` → `struct_fields.size() >= 1`，`tuple_fields.empty()`。
- `kind == Unit` → 两个 vector 均为空。
- Struct variant 中字段名必须唯一（诊断码 `syntax.DUPLICATE_VARIANT_FIELD`）。

### 3.3 TypeChecker / 类型检查

在 `match` arm 的 `variantPattern` 解构处（当前入口为 `src/compiler/semantics/typecheck_expr.cpp:609` `apply_flow_narrowing` 周边的 pattern 类型检查路径）：

1. **Scrutinee 形状一致性**：pattern 的 variant kind（Tuple / Struct / Unit）必须与 enum 声明中该 variant 的 kind 一致。类型不匹配发射诊断码 `typecheck.INVALID_ENUM_VARIANT_SHAPE`，`related[]` 附带 "declared here" 源定位。

   ```ahfl
   // 反例：
   enum E { A { x: Int } }
   fn f(e: E) -> Int { match e { A(v) => v } }
   //                        ^^^^ typecheck.INVALID_ENUM_VARIANT_SHAPE:
   //                             variant A is struct-kind but pattern uses tuple syntax
   ```

2. **Struct-variant 字段匹配**：
   - pattern 中出现的字段名必须全部在 struct variant 声明中存在（缺失 → `typecheck.MISSING_VARIANT_FIELD`，多余 → `typecheck.UNEXPECTED_VARIANT_FIELD`）。
   - 未出现的字段必须被 `..` 覆盖，否则同样 `MISSING_VARIANT_FIELD`。
   - 每个命名绑定的类型 = 声明字段类型；`IDENT` 简写即 `IDENT: IDENT`，绑定名等于字段名。
   - 默认值（`= constExpr`）仅在 variant **构造** 时生效（`A { x=42 }` 可省略 `x`），解构端忽略 `=` 子句。

3. **Tuple-variant 保持现状**：pattern 数量与类型顺序同构，诊断沿用 `typecheck.WRONG_ARITY` / `typecheck.TYPE_MISMATCH`。

### 3.4 Semantic / 语义规则

**Scope conflict（变体名 shadow 防护）**：

在同一 enum 声明的作用域内：

- variant 标识符 **不得** 与同模块已声明的 struct / union / enum 名冲突（诊断 `resolver.VARIANT_NAME_SHADOWS_TYPE`）。
- 两个 variant 名 **不得** 相同（现有规则，保持）。
- 同一 enum 内，variant 字段名的作用域仅限该 variant 内部，不与其它 variant 的字段名冲突（`A { x: Int }` 与 `B { x: String }` 合法）。

**Shape as identity**：

variant 的 **kind（Unit / Tuple / Struct）** 参与类型身份。即使字段类型列表相同，tuple 与 struct variant 也不兼容：

```ahfl
enum E {
    A(Int, String),      // tuple kind
    B { x: Int, y: String },  // struct kind — NOT interchangeable with A
}
```

**Expression constructors**：

构造端语法镜像：
- tuple-variant：`A(1, "hi")`，与现状一致。
- struct-variant：`B { x: 1, y: "hi" }`，字段可省略（若有默认值），顺序任意。
- unit-variant：`X` 或 `E::X`。

---

## 4. Alternatives Considered / 替代方案对比

| # | 方案 / Alternative | Pros / 优点 | Cons / 缺点 |
|---|---|---|---|
| A | **单字段 `Name(T)` 继续合法，但语义规范化为 tuple-variant arity=1（推荐方案 = 本 RFC §3 + §6）** | (1) 与现有 stdlib `Optional::Some(T)`、`Result::Ok(T)/Err(E)` 100% 源码兼容；(2) 无 immediate 破坏性变更；(3) 允许 2-release deprecation 平滑演进。 | (1) 首次接触的用户仍需区分 `Name(T)` 与 `Name { value: T }`，需要文档澄清；(2) formatter 需保留用户写法（或按 opt-in canonicalize），增加 formatter 状态。 |
| B | **禁止单字段 `Name(T)`，强制用户在 `Name((T))`（tuple arity=1）与 `Name { value: T }` 中选择** | (1) 彻底消除语法歧义，variant 形态 1:1 映射到 kind；(2) formatter / hover 输出唯一。 | (1) 严重破坏性：stdlib `Some(T)`、用户代码所有单字段 variant 均需迁移；(2) `Some((x))` 双括号对可读性是回归；(3) 迁移成本高，可能需要 automated fixer 与 3+ release cycle。 |
| C | **引入 ADT product kind 元语法：`Name(payload)`，payload 本身是 `(T1,T2)` 或 `{f:T}`** | (1) 文法最统一，解析器只一条 enumVariant 主干；(2) 未来易扩展 `Name(boxed StructType)` 等复合形式。 | (1) 括号嵌套增加：`Ok({ value: T })` 需要外层括号 + 内层花括号；(2) 与 Rust/Swift 主流语言体验不一致，用户心智负担增加；(3) AST 增加中间 `PayloadSyntax` 节点，现有 desugar/formatter 改动面大。 |

**推荐 / Recommendation**：方案 A。它在破坏性最低的前提下实现了本 RFC 的全部目标；§6 给出的 deprecation 路径在用户不迁移的情况下也零 breakage。

---

## 5. Non-Goals / 明确不做的事项

1. **不引入匿名 struct literal**：`{ x: 1, y: 2 }` 不能作为独立表达式存在（必须绑定到命名的 struct-variant 构造器）。匿名 struct 类型属于 struct RFC 扩展，不在本 RFC 范围。
2. **不做 enum 级泛型细化**：`<T>` 形参的 enum 保持现状，不引入 GATs、where-clause、variant-level 泛型参数。完整泛型设计交由 **PB-01 Generics**（`docs/plans/phaseb-gap-analysis.zh.md:142` OUT-OF-SCOPE）。
3. **不做 exhaustive match lint / non-exhaustive attribute**：match 穷尽性检查、missing arm 提示、`#[non_exhaustive]` 标记由 **RFC 0003** 独立负责（`docs/plans/phaseb-gap-analysis.zh.md:137`）。本 RFC 仅保证 variant 解构的形状/字段类型检查。
4. **不做 variant-level methods / impl blocks on enum**：`impl Option<T> { fn unwrap() -> T { ... } }` 属于 Core-4 impl-block 设计，不在本 RFC 处理。
5. **不改变 enum 的内存布局/FFI ABI**：AHFL 当前 enum 的 tagged-union 表示保持不变；struct-variant 与 tuple-variant 的 payload 在布局上等价（字段顺序依声明序）。本 RFC 不引入 `#[repr(C)]` 或自定义 discriminant。
6. **不做 variant 引用重命名**：`use MyEnum::*` 或 `use MyEnum::Var as V` 的 import-alias 规则保持现状，本 RFC 不扩展。

---

## 6. Migration / Backward Compatibility / 迁移与向后兼容

### 6.1 Compatibility Matrix / 兼容性矩阵

| 现有写法 | 本 RFC 语义 | Breakage? |
|---|---|---|
| `Name` (unit) | `EnumVariantKind::Unit` | 无，不变 |
| `Name(T)` (单字段) | `EnumVariantKind::Tuple`, arity=1 | 无，规范化保留（见 §6.2） |
| `Name(T1, T2)` (多字段) | `EnumVariantKind::Tuple`, arity=N | 无，不变 |
| `Name{f: T}` (previously unsupported mix) | 此前 parse 失败；本 RFC 后作为 struct-variant parse 成功 | 非破坏性；是新增合法语法 |
| `match e { Name(x) => ... }` on tuple-1 | 位置解构，保持 | 无 |
| `match e { Name(x, y) => ... }` on tuple-N | 位置解构，保持 | 无 |
| `struct Name { f: T }` + 同作用域 `enum E { Name(T) }` | 此前已合法？**否** — 新报 `VARIANT_NAME_SHADOWS_TYPE` | 破坏性，见 §6.3 |

### 6.2 Deprecation Path / 废弃路径

`Name(T)`（单字段 tuple-variant）在 **Release N** 引入 `#[deprecated = "use either Name((T)) for tuple arity-1 or Name { field: T } for struct variant"]` 的 lint-level warning：

- **Release N（本 RFC 落地版本）**：增加 compiler option `--warn variant-single-field-tuple`，默认 **不开启**（零 breakage）。stdlib `Some(T)`/`Ok(T)`/`Err(E)` 加 `#[allow(variant-single-field-tuple)]`。
- **Release N+2**：`--warn variant-single-field-tuple` 变为 **默认开启**，在 `--strict` 下升级为 error。
- **Release N+4**：正式讨论是否移除。若社区反馈以 struct-variant 为主、tuple-1 使用极少，则移除；否则永久保留为合法语法但继续 emit warning。

### 6.3 Shadow 诊断的 soft-fail

`VARIANT_NAME_SHADOWS_TYPE` 在 Release N 内：

- 对 **新写** 的冲突（本 RFC 引入 struct-variant 后首次出现的冲突）为 error。
- 对 **存量** 已被解析为合法的组合（目前 parse 允许 variant 名 = 已有 struct 名，因为二者语义命名空间不同）由 resolver 探测为 **warning**，并提供 `--compat old-variant-namespace` 开关降级为 note。Release N+2 移除 compat 开关，统一为 error。

### 6.4 Automated Tooling / 自动化工具

仿照 Rust 的 `cargo fix`，在 AHFL tooling 中提供两个 subcommand：

1. **`ahflc fix --diagnostics VARIANT_NAME_SHADOWS_TYPE`**：自动把冲突的 variant 名重命名为 `<Name>V`（在冲突位置追加 `V` 后缀），并更新所有引用（`use`、`match` arm、构造调用）。基于 resolver 的全工程 SymbolId 做 rename。
2. **`ahflc fix --diagnostics variant-single-field-tuple --mode structify`**：将单字段 `Name(T)` 转换为 `Name { value: T }`，同步把所有 `Name(x)` 构造 / `Name(y)` pattern 重写为命名形式。`--mode tuple` 则转换为 `Name((T))` 双括号。默认不自动转换，用户需显式指定 `--mode`。

两个 fixer 以独立 PR 的形式随 Alpha 阶段交付（§7.1）。

---

## 7. Rollout Plan + Acceptance Criteria / 落地计划与验收标准

### 7.1 Three-Phase Rollout / 三阶段落地

| Phase / 阶段 | 触发条件 / Trigger | Feature Flag / 开关 | 持续时间 / Duration | 交付内容 / Deliverables |
|---|---|---|---|---|
| **Alpha** | RFC 合并（Status: ACCEPTED） | `--feature enum-variant-named-fields=alpha` (off by default) | 2 周 / ~1 wave | (1) grammar + AST 变更合入；(2) parse 单测门控；(3) typecheck 基础规则；(4) `ahflc fix` 两个 fixer prototype；(5) ctest 中新增测试仅在 flag 打开时运行。 |
| **Beta** | Alpha 阶段 ctest 100% green + mutation score ≥ 目标（见 §7.2） | `--feature enum-variant-named-fields=beta` (opt-in, printed in `ahflc --version --verbose`) | 4 周 / ~2 waves | (1) LSP hover: struct-variant 显示字段名 + 类型；(2) formatter: struct-variant 与 tuple-variant 的双向 roundtrip；(3) CLI golden 输出: 7 类反例诊断的 stderr 字节级稳定；(4) 用户灰度：邀请 ≥ 3 个内部仓库开启 beta 并收集反馈。 |
| **Stable** | Beta 反馈无 blocking issue + fuzzer 稳定 | **default on** (开关移除，或变成 `=stable` 而无 opt-out) | 永久 | (1) §6.2 的 deprecation lint 随 release N 正式发出；(2) d-2 (命名字段解构) 的实现不再 feature-gate；(3) 文档：AHFL spec 与 tutorial 同步更新。 |

### 7.2 Acceptance Criteria / 验收标准（可量化）

| # | 标准 / Criterion | 目标值 / Target | 度量方式 / Measurement |
|---|---|---|---|
| A1 | **ctest 全量通过** | Alpha 末 ≥ 现有基线 + 新增 40/40 green；Stable 末 ≥ 基线 + 新增 80/80 green 且总体 100% | `ctest -j --output-on-failure` 报告，与 `wave-17 979/979` 基线对比 (`docs/plans/wave-17-integration-final-report.md` 终态) |
| A2 | **Mutation testing score** | Alpha 末 ≥ 85%；Stable 末 ≥ 92% | `mutation.yaml` 针对 `syntax/*enum*`、`semantics/*variant*`、`typecheck_expr.cpp:pattern_variant` 路径的 mutation 跑批，覆盖率由现有工具链（misfortune）输出。 |
| A3 | **Fuzzer 稳定性** | 自 Alpha 合入起，语法 fuzzer (`docs/fuzz/` 目录) 连续 **14 天** 零 crash；每日种子数 ≥ 1e6 | CI nightly fuzz job（已存在）输出；任何 ASan/UBSan/assert failure 立即回滚到上一 green commit。 |
| A4 | **Diagnostic 稳定性** | 新增 12 个 golden 反例文件（见下表），每次 PR 字节级匹配；Stable 阶段诊断码语义不改变（breaking change to diagnostics requires new RFC bump） | `tests/golden/` 目录下 `DIFF.md5` 校验，与 formatter roundtrip 组合。 |
| A5 | **Fixer 有效性** | `ahflc fix --mode structify` 对 stdlib 与 wave-1 测试仓库能零冲突应用，且修复后工程 ctest 100% 通过 | CI pipeline: checkout → apply fix → ctest → diff stdlib 规范预期文件（`tests/fix-expected/`）。 |
| A6 | **性能回归** | 开启 `enum-variant-named-fields=stable` 后，全量 ctest 的总 CPU 时间相对基线不超过 **+3%**；内存峰值不超过 **+2%** | 性能基准仓库 `benches/compile_perf/` 的 CI 报告，三次运行取中位数。 |

**A4 要求的 12 个 golden 反例清单（每类诊断码 ≥ 1 个）**：

| # | 场景 | 诊断码 |
|---|---|---|
| 1 | unit variant 用于 tuple pattern | `typecheck.INVALID_ENUM_VARIANT_SHAPE` |
| 2 | struct variant 用于 tuple pattern | `typecheck.INVALID_ENUM_VARIANT_SHAPE` |
| 3 | tuple variant 用于 struct pattern | `typecheck.INVALID_ENUM_VARIANT_SHAPE` |
| 4 | struct pattern 中缺失字段，无 `..` | `typecheck.MISSING_VARIANT_FIELD` |
| 5 | struct pattern 中字段名不存在 | `typecheck.UNEXPECTED_VARIANT_FIELD` |
| 6 | struct variant 字段类型不匹配 | `typecheck.TYPE_MISMATCH` (origin=variant field) |
| 7 | tuple variant pattern WRONG_ARITY | `typecheck.WRONG_ARITY` (保持) |
| 8 | enum 内 variant 名重复 | `syntax.DUPLICATE_ENUM_VARIANT` (保持) |
| 9 | struct variant 内字段名重复 | `syntax.DUPLICATE_VARIANT_FIELD` |
| 10 | variant 名 shadow 已有 struct | `resolver.VARIANT_NAME_SHADOWS_TYPE` |
| 11 | variant 构造中未提供必填字段 (struct) | `typecheck.MISSING_VARIANT_FIELD_IN_CONSTRUCTOR` |
| 12 | variant 单字段 tuple deprecation lint | `lint.variant-single-field-tuple` |

---

## 8. Decision Block / 决策块

| 评审维度 | 选择 / 说明 |
|---|---|
| **当前状态** | DRAFT · Awaiting inputs |
| **go-criteria 满足度** | (1) Grammar + AST 变更在 Alpha 阶段 ctest 全绿（≥ 基线 + 40/40）；(2) 12 类 golden 反例诊断字节级稳定；(3) Mutation score Alpha 末 ≥ 85%、Stable 末 ≥ 92% |
| **no-go 否决条件** | 若 struct-variant 与 tuple-variant 在 parser 层产生 S/R 冲突无法通过 ANTLR 优先级解决，则放弃本 RFC；若 VARIANT_NAME_SHADOWS_TYPE 对存量代码 breakage > 5% 且无法通过 compat 开关吸收，则放弃本 RFC |
| **未解决争议** | (1) 单字段 `Name(T)` 长期保留 vs 强制在 N+4 移除；(2) `Name { f = default }` 构造端默认值是否需要；(3) formatter 是否默认 canonicalize tuple-1 为 struct 形式 |
| **依赖其他 RFC** | RFC 0002, RFC 0003 |
| **预计实现工作量** | M（1-4 周）— grammar/AST 1 周 + typecheck/variant shape 校验 1 周 + LSP/formatter 1 周 + fixer 1 周 |
| **破坏性变更风险** | MEDIUM — VARIANT_NAME_SHADOWS_TYPE 对存量允许的"同名 struct+variant"产生新报错；需 compat 开关消化 |
| **Wave 归属** | Wave-20（依赖 RFC 0003 arm 语义一致性 review；PB-01 Generics 作为下游） |

---

## 9. References / 参考文献

### 8.1 仓库内链接 / Internal References (file:line)

1. **现有 enum 文法定义** — `grammar/AHFL.g4:82-92`（enumDecl + enumVariant 当前规则，本 RFC 替换目标）。
2. **现有 variant pattern 文法** — `grammar/AHFL.g4:488-496`（variantPattern 与 patternList，本 RFC 扩展目标）。
3. **现有 AST enum variant 验证** — `src/compiler/syntax/frontend/ast.cpp:753-769`（validate_node 中 EnumVariantDeclSyntax payload 遍历，需增加 kind discriminant 校验）。
4. **现有 match/narrowing 入口** — `src/compiler/semantics/typecheck_expr.cpp:609-611`（`apply_flow_narrowing`，struct-variant pattern 类型检查将在此链路上增加 variant-kind 判定）。
5. **PB-01 Gap Analysis 条目** — `docs/plans/phaseb-gap-analysis.zh.md:124-129`（本 RFC 即 §3.d 的 BLOCKED 解除前置条件；验收标准来源）。
6. **Wave-16 Blocked 清单登记** — `docs/plans/wave-16-integration-report.zh.md:160-167`，具体行 **164**。
7. **ADT match 现有单测基线** — `tests/unit/compiler/semantics/adt_match.cpp:17-28`（P1b 顶层注释，本 RFC A1 新增的 40/80 断言将以本文件为入口扩充）。

### 8.2 外部参考 / External References

1. **Rust RFC 2593 — Enum variant fields (struct variants)** <br/>
   `https://rust-lang.github.io/rfcs/2593-enum-variant-fields.html` <br/>
   Rust 中 struct-variant 的语义规范与 match 命名字段解构、`..` rest 模式、`Variant { field: binding }` / `Variant { field }` 简写规则。AHFL 本次设计的字段匹配规则 90% 对齐本 RFC。

2. **The Swift Programming Language — Enumerations / Associated Values & Raw Values** <br/>
   `https://docs.swift.org/swift-book/documentation/the-swift-programming-language/enumerations/` <br/>
   Swift 同时支持 tuple-associated-values（`enum HttpResponse { case success(Int, Data); case failure(Error) }`）与 named-field 语法（Swift 5.9+ 引入的 struct-like payload）。其 "same-case payload type identity not dependent on field order" 原则被 AHFL 采纳为 §3.4 的 Shape-as-identity。

3. **TypeScript Handbook — Discriminated Unions** <br/>
   `https://www.typescriptlang.org/docs/handbook/typescript-in-5-minutes-func.html#discriminated-unions` <br/>
   虽然 TS 是 `{ kind: "A", x: T }` 的结构 discriminated-union，但 AHFL 的 struct-variant 在内存模型与模式匹配语义上等价于其缩小语法。本参考用于 justify `{ field: T }` 在现代语言设计中的普遍性。

4. **Rust Reference — Pattern matching: Struct patterns & Tuple struct patterns** <br/>
   `https://doc.rust-lang.org/reference/patterns.html#struct-patterns` & `https://doc.rust-lang.org/reference/patterns.html#tuple-struct-patterns` <br/>
   本 RFC §3.1 的 `patternField` 简写规则 (`IDENT` = `IDENT: IDENT`)、`..` rest 模式、以及 identifier 解析优先级直接参考本规范的最小可行子集。

---

### Change history / 变更记录

- 2026-06-28 (V1.1): Added Decision Block §8; Consult / Decision Due / Blocking Items; cross-RFC refs
- 2026-06-28 (V1.0): Initial DRAFT
