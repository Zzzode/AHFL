---
rfc: "0011"
title: "Pattern Usefulness Matrix"
status: "draft"
area: ["language", "compiler", "tooling"]
stability: "experimental"
created: "2026-07-07"
updated: "2026-07-08"
authors: ["LLM-orchestrated"]
shepherd: "project lead"
owners:
  language: "language owner"
  compiler: "compiler owner"
  tooling: "tooling owner"
required_reviewers: ["language", "compiler", "tooling"]
tracking_issue: "https://github.com/Zzzode/AHFL/issues/14"
discussion: "https://github.com/Zzzode/AHFL/issues/14"
implementation_prs: []
decision_due: "2026-08-04"
---

# RFC 0011: Pattern Usefulness Matrix

## Summary

本 RFC 定义 AHFL 后续完整 pattern usefulness matrix：在 `match`、`if let` 和未来 pattern binding 中，用统一算法判断 exhaustiveness、unreachable arm、redundant subpattern 和 missing witness。它接续 [RFC 0003](./0003-match-exhaustiveness-diagnostics.zh.md) 的基础 exhaustiveness diagnostics，但不把 nested pattern、literal/range pattern 或 enum payload destructuring 的半成熟规则塞回 RFC 0003。

## Motivation

RFC 0003 已经让 match exhaustiveness 从缺失能力推进到可用诊断，但它不是完整 usefulness 系统。随着 enum variant payload、if-let、optional narrowing、nested pattern、literal/range pattern 和 payload destructuring 进入语言，简单按顶层 constructor 枚举缺失 case 会产生三个问题：

1. 不能准确识别嵌套结构里的 unreachable arm。
2. 不能给出最小 missing witness，例如 `Some(JBool(_))` 或 `Err(_)`。
3. 不能在 guards、bindings、wildcards 和 range/literal pattern 混合时保持诊断稳定。

成熟编译器通常把这个问题建模成 usefulness matrix：把已有 arms 当作 pattern matrix，把候选 pattern 或 wildcard 当作 vector，通过 constructor specialization、default matrix 和 witness construction 判定一个 pattern 是否贡献新覆盖。AHFL 应采用同一模型，同时保持内部 canonical identity 使用 typed constructors、`TypeId`、`SymbolId`、`VariantId` 和 flat stores，而不是 variant spelling strings。

## Goals

1. 定义 AHFL pattern domain：wildcard、binding、enum variant、tuple-like payload、struct-like payload、bool、unit、literal、range 和 or-pattern 的 usefulness 语义。
2. 定义 match exhaustiveness、unreachable arm、redundant subpattern 和 witness diagnostics 的统一算法。
3. 让 if-let、optional narrowing 和 Result/Option convenience pattern 使用同一 pattern compiler。
4. 明确 guard 对 usefulness 的保守处理规则。
5. 定义 diagnostics 的 source range、stable code、witness rendering 和 LSP 展示行为。
6. 给出实现顺序，避免在语法和 AST 还未稳定时冻结半成熟 pattern 域。

## Non-Goals

1. 不在本 RFC 中设计 pattern matching 的新 surface syntax；它消费已经被 parser/RFC 接受的 pattern forms。
2. 不设计 regex、collection spread、slice pattern 或 SMT-backed arbitrary predicate coverage。
3. 不让 guards 参与证明 exhaustiveness；guarded arm 对 exhaustiveness 必须保守处理。
4. 不改变 RFC 0001 enum variant payload 的 constructor/typecheck 规则。
5. 不改变 RFC 0002 optional narrowing 的已稳定语义；本 RFC 只把 narrowing 的 pattern evidence 接入统一算法。

## Design

### Architecture

```mermaid
flowchart TD
    Parser["Parser pattern syntax"] --> PatternHIR["Typed pattern HIR"]
    Typecheck["Type checker"] --> PatternHIR
    PatternHIR --> ConstructorSet["Constructor set builder"]
    ConstructorSet --> Matrix["Usefulness matrix"]
    Matrix --> Witness["Witness builder"]
    Matrix --> Diagnostics["Exhaustive / unreachable / redundant diagnostics"]
    PatternHIR --> FlowFacts["Flow facts and narrowing"]
    Diagnostics --> Lsp["LSP diagnostics and quick fixes"]
```

Pipeline:

1. Parser produces syntax-level pattern nodes with source ranges.
2. Type checker lowers syntax patterns into typed pattern HIR using type context and resolver symbols.
3. Constructor set builder maps each typed pattern column to finite or open constructor domains.
4. Usefulness matrix computes whether each arm adds coverage and whether wildcard over the scrutinee type is fully covered.
5. Witness builder renders missing cases using source-level names from resolver facts.
6. Flow facts consume successful pattern refinements for arm bodies and if-let bodies.

### Pattern HIR

Pattern HIR must be flat-store based:

```mermaid
classDiagram
    class PatternId
    class PatternNode
    class ConstructorId
    class VariantId
    class TypeId
    class PatternMatrix

    PatternNode --> PatternId : children
    PatternNode --> ConstructorId
    ConstructorId --> VariantId
    ConstructorId --> TypeId
    PatternMatrix --> PatternId
```

`PatternNode` variants:

1. `Wildcard`
2. `Binding`
3. `Unit`
4. `BoolLiteral`
5. `IntLiteral`
6. `StringLiteral`
7. `Range`
8. `EnumVariant`
9. `StructVariant`
10. `TuplePayload`
11. `StructPayload`
12. `Or`

Every node records `SourceRange`, scrutinee `TypeId`, optional binding target and child `PatternId` list.

### Constructor Domains

| Type domain | Constructors | Exhaustiveness policy |
| --- | --- | --- |
| `Unit` | `()` | finite |
| `Bool` | `true`, `false` | finite |
| Enum | declared variants | finite if all variants known |
| `Option<T>` / `Result<T, E>` | std enum variants through normal enum model | finite |
| Int / Float / String literals | observed singleton constructors plus default | open |
| Range-capable numeric types | normalized interval constructors plus default | open |
| Struct nominal | single struct constructor | finite if all fields pattern-matchable |
| Unknown/Error | unknown | suppress cascading usefulness errors |

Open domains can prove redundancy against prior literal/range coverage, but cannot prove full exhaustiveness unless the covered interval set is complete for the type domain. For unbounded `Int`, wildcard/default remains required.

### Usefulness Algorithm

```mermaid
flowchart TD
    Input["matrix P and vector q"] --> Empty["is q empty?"]
    Empty -->|yes| Result["useful iff P has no empty row"]
    Empty -->|no| Head["inspect first column"]
    Head --> Constructors["collect constructors"]
    Constructors --> Specialize["specialize P and q per constructor"]
    Specialize --> Recurse["recursive usefulness"]
    Constructors --> Default["default matrix for wildcard/binding/open domain"]
    Default --> Recurse
    Recurse --> Witness["construct witness when useful"]
```

Rules:

1. Wildcard and binding are equivalent for coverage; binding additionally emits flow facts.
2. Or-pattern is useful if any alternative is useful against the current matrix; alternatives after fully-covered alternatives are redundant subpatterns.
3. A guarded arm is useful for unreachable-arm diagnostics if its pattern is useful, but it does not contribute to exhaustiveness unless guard is statically known true.
4. Unknown/Error typed patterns do not emit exhaustiveness misses; they allow type diagnostics to remain primary.
5. Constructor specialization must use `VariantId` / `ConstructorId`, not variant spelling strings.

### Diagnostics

| Code | Meaning |
| --- | --- |
| `typecheck.MATCH_MISSING_PATTERNS` | match does not cover all constructors or open-domain default witnesses |
| `typecheck.MATCH_UNREACHABLE_ARM` | arm pattern is not useful after previous arms |
| `typecheck.MATCH_OVERLAP` | arm pattern structurally overlaps a previous arm while still possibly contributing coverage |
| `typecheck.MATCH_REDUNDANT_PATTERN` | an or-pattern alternative is shadowed |
| `typecheck.UNREACHABLE_IF_LET_ELSE` | if-let else branch is statically unreachable |

Range pattern syntax is not implemented yet, so this RFC does not reserve a stable `INVALID_RANGE_PATTERN` code. When range syntax is accepted, the range diagnostic must be added to `include/ahfl/base/support/diagnostics.hpp`, `docs/reference/error-codes.zh.md`, and this table in the same implementation slice.

Diagnostics must include primary range, missing witness, related information pointing to prior covering arm when relevant, and LSP quick fix only when inserting a wildcard arm is source-safe.

## User Impact

Users get more precise diagnostics:

1. Missing cases show concrete witnesses instead of broad enum names.
2. Redundant arms are reported at the arm that is actually shadowed.
3. if-let and match behave consistently for Option/Result-like patterns.
4. LSP can surface quick fixes for missing wildcard or missing enum variant arms.

## Compatibility and Migration

This RFC may add new diagnostics to programs that currently typecheck but contain unreachable or redundant match arms. That is a source compatibility break for diagnostics, not for runtime semantics.

Migration:

1. Remove unreachable arms.
2. Replace broad earlier patterns with narrower patterns when later arms are intended to run.
3. Add wildcard/default arms for open domains.
4. Prefer explicit enum variant arms for finite enum domains.

## Implementation Plan

1. Pattern HIR.
   - Add flat pattern store and typed pattern lowering.
   - Map enum variants and struct variant payloads through stable IDs.
2. Constructor set builder.
   - Implement finite enum/bool/unit domains first.
   - Add literal/range domains only after their language syntax stabilizes.
3. Matrix engine.
   - Implement specialization, default matrix and witness construction.
   - Keep guard handling conservative.
4. Diagnostics.
   - Replace RFC 0003 ad-hoc exhaustiveness checks with matrix-backed diagnostics.
   - Add related information and witness rendering.
5. Flow integration.
   - Route if-let and optional narrowing through typed pattern HIR.
   - Preserve RFC 0002 narrowing behavior.
6. Tooling.
   - Surface LSP diagnostics and quick fixes.
   - Add formatter coverage for new pattern forms when syntax exists.

## Current Implementation Status

截至 2026-07-08，本 RFC 仍保持 `draft`，但多条编译器基础设施切片已经落库：

1. `PatternUsefulnessContext` 提供 ID-based flat stores：`PatternDomainId`、`PatternConstructorId`、`PatternId` 作为 canonical identity；字符串仅用于 witness/debug rendering。
2. `analyze_pattern_usefulness()` 已支持 finite constructor domains、nested constructor payload、or-pattern branch redundancy、guarded row 不参与 exhaustiveness、wildcard unreachable row 和 missing witness construction。
3. `ahfl_semantics_pattern_usefulness_tests` 覆盖 wildcard、finite enum-like constructors、bool、or-pattern、nested constructor、guarded row 和 open domain fallback。
4. RFC 0003 的 `match_exhaustiveness` 已改为 matrix-backed analyzer：现有 `MATCH_MISSING_PATTERNS` / `MATCH_UNREACHABLE_ARM` / `MATCH_OVERLAP` 行为继续由 ADT match 回归测试覆盖。
5. `match_exhaustiveness` 已接入 typed enum-payload lowering：typechecker 会把 scrutinee type 和 enum resolver 传给 matrix analyzer，nested enum payload pattern 可产生 `Some(Off)` 这类 witness，并能诊断重复 nested payload arm。
6. Bool payload literal lowering 已落库：`Some(true)` / `Some(false)` 会进入有限 Bool constructor domain，missing witness 可精确到 `Some(false)`；`none` literal pattern 会覆盖 unit `None` variant；不兼容 literal pattern 现在由 typechecker 报 `TYPE_MISMATCH`，不再被静默当成 empty coverage。
7. Struct payload witness rendering 已落库：constructor flat store 记录 payload display kind 和字段名，missing witness 会渲染为 `Data { flag: false, other: false }`，而不是丢失字段语义的 tuple 形式。
8. Redundant or-pattern branch diagnostic 已落库：matrix core 的 redundant branch analysis 现在通过稳定 warning code `MATCH_REDUNDANT_PATTERN` 暴露到 typechecker，range 指向冗余分支本身。
9. TypedProgram 一等 pattern fact store 的首个切片已落库：typechecker 的 `match` pattern lowering 会把 literal、variant、wildcard、binding、tuple 和 or-pattern 记录到 `TypedProgram::patterns`，包含 `SourceRange`、`SourceId`、matched type、enum symbol、variant payload kind、bindings 和 child pattern index；JSON typed HIR serialization/deserialization 已覆盖该 flat store。
10. `match_exhaustiveness` matrix consumer 已迁移到 typed pattern root rows：typechecker 传递每个 match arm 的 `TypedProgram::patterns` root index、source range 和 guard exhaustiveness flag，matrix analyzer 从 typed pattern flat store lowering 到 constructor matrix，不再为常规 typed match 重新从 AST pattern lower 一套局部结构。
11. `if let` statement 的 typed pattern fact 已落库：typechecker 会把 `if let` 根 pattern 写入 `TypedProgram::patterns`，并在 `TypedStatement::pattern_index` 记录 root index；typed HIR JSON round-trip 和 monomorphization remap 已覆盖该 statement-local pattern reference。
12. `if let` 的第一条 usefulness consumer 已落库：typechecker 使用同一个 typed-row matrix analyzer 判断 `if let` pattern 是否覆盖 enum 全部 constructor，并在 `else` 分支不可达时发出 `typecheck.UNREACHABLE_IF_LET_ELSE` warning；单 constructor / 多 constructor enum 回归测试已覆盖。
13. LSP pattern quick fixes v1 已落库：`typecheck.MATCH_MISSING_PATTERNS` 可从结构化 `Diagnostic.data["missing_witnesses"]` 插入具体 missing arms（例如 `B => <TODO>,` 或 `Data { flag: false, other: false } => <TODO>,`），并在 witness 不可安全提取或旧诊断缺少结构化 payload 时保留 rendered-message 解析与 `_ => <TODO>,` wildcard fallback；`typecheck.MATCH_UNREACHABLE_ARM` 可在诊断 range 对应单行 match arm 时删除整条 unreachable arm。handler 单元测试覆盖编辑位置、struct witness 字段逗号解析、fallback、unreachable-arm deletion 和 quickfix metadata。
14. `if let` narrowing consumer 已迁移到 typed pattern fact store：typechecker 从 `TypedStatement::pattern_index` 指向的 `TypedProgram::patterns` root 派生 then/else `FlowFacts` 和 branch-local payload bindings，保留 RFC 0002 Option narrowing 行为，同时避免 flow narrowing 再从 AST pattern 重新推导一套并行语义。
15. `MATCH_MISSING_PATTERNS` structured witness diagnostic payload 已落库：base diagnostic JSON、LSP protocol diagnostic JSON 和 typecheck emission 都会保留 `missing_witnesses` 字段；LSP diagnostics 回归测试覆盖从真实 typechecker 诊断到 JSON-RPC 输出的结构化 witness 数据。
16. 非 Bool open literal usefulness 已落库：Int / Float / String 类开放 payload domain 会保留 `_` 默认 witness，并把已出现 literal 降为 singleton constructor；`Some(1), None` 不再错误地证明 `Option<Int>` exhaustiveness，`Some(_)` 才覆盖开放剩余值，重复 literal 会继续产生 unreachable / overlap warning。
17. match-arm narrowing consumer 已迁移到 typed pattern fact store：typechecker 先 lower match arm pattern root，再从 `TypedPatternKind::Variant` fact 派生 arm-local `FlowFacts`；普通 enum variant narrowing 和 std `Option::Some(_)` arm 内的 non-none narrowing 均不再从 AST spelling 单独推导。
18. 当前已实现 pattern usefulness diagnostic taxonomy 已与代码和 `docs/reference/error-codes.zh.md` 对齐：稳定用户码保持在 `typecheck.MATCH_MISSING_PATTERNS`、`typecheck.MATCH_UNREACHABLE_ARM`、`typecheck.MATCH_OVERLAP`、`typecheck.MATCH_REDUNDANT_PATTERN` 和 `typecheck.UNREACHABLE_IF_LET_ELSE`；未实现的 range pattern 不预留 placeholder code。
19. LSP pattern binding hover v1 已落库：hover index 会遍历 `TypedProgram::patterns` 中的 `TypedPatternBinding` fact，为 `match` 和 `if let` pattern binding 声明位点注册 `LocalBinding` hover target；hover payload 在没有 expression fact 的声明位点仍显示 binding 名称和 typed pattern 推导出的类型。
20. `if let` 源语法和 AST 已迁移到通用 `PatternSyntax`：grammar 直接消费 `pattern` rule，frontend、formatter、semantic tokens、IR lowering 和 typechecker 共用 match pattern surface；旧 `IfLetPatternSyntax` 和 statement-local typed-pattern 构造路径已删除。
21. LSP pattern enum variant completion v1 已落库：completion 在光标位于 `TypedProgram::patterns` 的 pattern range 内时，使用最小 containing typed pattern 的 `matched_type` 查询 `TypeEnvironment::get_enum`，只返回该 scrutinee enum 的 variant 候选；`match`、`if let` 和 nested enum payload pattern 均由 handler 回归测试覆盖。

尚未完成：

1. 未来 destructuring UX 仍需继续推进：struct payload field completion、payload pattern snippet/editing、signatureHelp 和更深 IDE 编辑序列还没有统一消费 typed pattern fact store。
2. range pattern、typed-pattern-driven LSP diagnostics 的最终稳定化仍未实现。

## Test Plan

1. Unit tests for matrix usefulness: wildcard, enum variants, bool, or-pattern, nested constructor, guarded arm and open domain default.
2. Typecheck diagnostics tests: non-exhaustive match, unreachable arm, redundant or-pattern and invalid range.
3. Witness golden tests: enum payload, nested enum, bool, Result/Option and open Int with default.
4. if-let tests: else reachable/unreachable and narrowing preservation.
5. LSP tests: diagnostics ranges, related information and quick fix availability.
6. Regression tests proving RFC 0001 enum payload and RFC 0002 optional narrowing behavior remain stable.

## Rollout and Stabilization

Draft exit criteria:

1. Pattern syntax roadmap is aligned with parser/frontend owners.
2. Typed pattern HIR shape is approved.
3. Diagnostic codes and witness syntax are approved.

Accepted exit criteria:

1. Literal/range/nested payload pattern surface is stable enough to implement.
2. Owners agree guarded arms do not prove exhaustiveness.
3. Test matrix covers finite and open domains.

Implemented exit criteria:

1. Matrix engine replaces RFC 0003 exhaustiveness checker.
2. Existing RFC 0001 / RFC 0002 / RFC 0003 tests still pass.
3. New diagnostics and LSP tests pass.

Stabilized exit criteria:

1. `docs/spec/core-language.zh.md` describes full pattern semantics.
2. `docs/reference/error-codes.zh.md` lists stable pattern diagnostics.
3. Release evidence includes representative finite/open/nested pattern cases.

## Alternatives

1. Keep RFC 0003's current top-level exhaustiveness diagnostics.
   - Rejected because it cannot scale to nested payloads, ranges or precise unreachable-arm diagnostics.
2. Lower match into chained if statements and rely on typecheck.
   - Rejected because it loses constructor coverage information and cannot produce missing witnesses.
3. Use SMT for all pattern coverage.
   - Rejected for v1 because finite constructor matrix handles AHFL's near-term pattern domain more simply and deterministically.
4. Treat guards as proving exhaustiveness.
   - Rejected because arbitrary guard predicates are not decidable by the pattern compiler and would create unsound coverage claims.

## Open Questions

1. Should AHFL introduce or-pattern syntax before or after range pattern syntax?
2. Should Int range exhaustiveness ever be complete for bounded integer types, or should all numeric domains remain open until bounded numeric types exist?
3. Should missing witness rendering prefer fully-qualified module paths or imported local aliases?

## Decision History

- 2026-07-07: Draft opened as the follow-up home for RFC 0003's full usefulness matrix work after enum payload, optional narrowing and if-let support landed.
- 2026-07-08: Landed the first compiler infrastructure slice: an ID-based flat pattern usefulness context plus finite constructor matrix tests for wildcard, bool/enum-like constructors, nested payload witnesses, guarded rows, or-pattern redundancy and open-domain fallback.
- 2026-07-08: Routed RFC 0003 `match_exhaustiveness` through the matrix core for top-level enum coverage while preserving existing diagnostics and ADT match regression behavior.
- 2026-07-08: Added `TypedProgram::patterns` as the first typed pattern HIR flat-store slice for `match` typechecking, including typed HIR JSON round-trip coverage.
- 2026-07-08: Migrated regular `match` exhaustiveness analysis to consume typed pattern root rows from `TypedProgram::patterns`, leaving the AST lowering path only as compatibility fallback for callers without typed rows.
- 2026-07-08: Added typed pattern HIR roots for `if let` statements and serialized `TypedStatement::pattern_index`, so statement-local patterns now share the same flat-store evidence model as `match` arms.
- 2026-07-08: Routed `if let` unreachable-else diagnostics through the typed pattern matrix consumer and documented `typecheck.UNREACHABLE_IF_LET_ELSE` as the first if-let usefulness diagnostic.
- 2026-07-08: Added the first LSP pattern quick fix: `MATCH_MISSING_PATTERNS` can insert a wildcard match arm when the affected match block is source-locatable.
- 2026-07-08: Migrated `if let` flow narrowing and payload binding introduction to consume `TypedProgram::patterns`, making the statement's narrowing behavior use the same typed pattern evidence as usefulness diagnostics.
- 2026-07-08: Upgraded the LSP `MATCH_MISSING_PATTERNS` quick fix to insert rendered missing witness arms when the diagnostic message exposes a source-safe witness list, while keeping wildcard fallback for unsafe or unstructured diagnostics.
- 2026-07-08: Promoted missing-pattern witnesses into structured diagnostic payloads (`Diagnostic.data["missing_witnesses"]`) and taught the LSP quick fix to prefer that stable data over user-facing message parsing.
- 2026-07-08: Added symbolic open-domain literal usefulness for Int / Float / String payloads, including default `_` witnesses, literal singleton constructors and `Option<Int>` typecheck regressions for incomplete literal-only matches.
- 2026-07-08: Migrated match-arm flow narrowing to consume the typed pattern root fact instead of re-deriving the selected variant from AST pattern syntax.
- 2026-07-08: Aligned the RFC0011 diagnostic taxonomy with the shipped `typecheck.MATCH_*` / `typecheck.UNREACHABLE_IF_LET_ELSE` codes and removed the unimplemented range-code placeholder.
- 2026-07-08: Added an LSP quick fix for `typecheck.MATCH_UNREACHABLE_ARM` that removes a source-safe single-line unreachable match arm using the typed-row diagnostic range.
- 2026-07-08: Added typed-pattern-driven LSP hover for pattern binding declaration sites, covering both `match` and `if let` bindings through `TypedProgram::patterns`.
- 2026-07-08: Generalized `if let` source syntax and AST to consume the same `PatternSyntax` as `match`, removing the dedicated `IfLetPatternSyntax` path.
