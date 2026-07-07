---
rfc: "0002"
title: "Optional Narrowing in Pattern Matching"
status: "stabilized"
area: ["language", "compiler", "tooling"]
stability: "stable-language"
created: "2026-06-25"
updated: "2026-07-06"
authors: ["LLM-orchestrated"]
shepherd: "AHFL language steering"
owners:
  language: "AHFL language owners"
  compiler: "AHFL compiler owners"
  tooling: "AHFL tooling owners"
required_reviewers: ["language", "compiler", "tooling"]
tracking_issue: "../plans/project-status.zh.md"
discussion: "0002-optional-narrowing.zh.md"
implementation_prs:
  - "local-rfc-0002-implementation"
decision_due: "2026-07-12"
---

# RFC 0002: Optional Narrowing in Pattern Matching

## Summary

AHFL supports local enum-variant narrowing for `match`, `if let`, and recognized `Option`/`Result` predicate methods. The feature is deliberately scoped: it uses branch-local flow facts, introduces payload bindings only inside the matched branch, and invalidates facts on writes to the narrowed path.

## Motivation

AHFL programs frequently destructure `Option<T>`, `Result<T, E>`, and user-defined enums. Requiring users to combine boolean checks with manual extraction makes code noisy and moves correctness into runtime convention.

The language needs the common Rust/Swift-style pattern workflow while preserving AHFL's bidirectional checker architecture. The chosen design does not introduce union types, global control-flow analysis, alias tracking, compatibility modes, or user-visible witness types.

## Goals

1. Parse and format `if let Variant(bindings...) = expr { ... } else { ... }`.
2. Type-check `if let` scrutinees as pure enum expressions.
3. Validate `if let` variant names, payload shape, and tuple payload arity with the same diagnostics used by `match`.
4. Introduce tuple payload bindings in the `then` block with substituted generic payload types.
5. Narrow matched paths inside `match` arms and `if let` branches.
6. Recognize `is_some`, `is_none`, `is_ok`, and `is_err` as built-in predicate narrowing roots.
7. Preserve fact invalidation on assignment to the narrowed path or its descendants.

## Non-Goals

1. No TypeScript-style full control-flow analysis.
2. No union-type narrowing.
3. No alias propagation, for example `let y = x; if y.is_some() { ... }` does not narrow `x`.
4. No pattern-guard narrowing for `match arm if guard`.
5. No custom `is_<variant>()` predicate recognition without a future metadata RFC.
6. No compatibility flag, staged syntax gate, or old semantic mode.
7. No user-visible `where x is Variant` type syntax.

## Design

### Narrowing Facts

The compiler represents narrowing as branch-local `FlowFacts` keyed by a source path:

- `IsNone`
- `IsNotNone`
- `IsVariant(enum, variant)`
- `IsNotVariant(enum, variant)`

The facts are not canonical type identity; they are local proof context. Canonical type identity remains in `TypeContext` and nominal type metadata. When expression resolution sees a path with facts, it computes the local view:

- `Option<T>` plus `IsNotNone` resolves as `T`.
- enum plus `IsVariant(E, V)` resolves as `EnumVariantT(E::V)` with the parent enum's type arguments preserved.
- enum plus enough `IsNotVariant` facts resolves to the only remaining variant when the enum metadata proves uniqueness.

Assignment to a path invalidates facts for that path and all descendant paths.

### `if let`

Implemented grammar:

```ebnf
IfLetStmt       ::= "if" "let" IfLetPattern "=" Expr Block [ "else" Block ] ;
IfLetPattern    ::= Ident [ "(" Ident { "," Ident } [ "," ] ")" ] ;
```

The semantic rules are:

1. The scrutinee must be pure.
2. The scrutinee type must be an enum.
3. The variant name must exist in the scrutinee enum.
4. Empty pattern payload matches only unit variants.
5. Parenthesized bindings match only tuple variants.
6. Binding count must equal tuple payload arity.
7. Duplicate bindings in the same pattern are rejected.
8. Tuple payload types are substituted through the scrutinee enum's generic arguments before entering the `then` block.
9. The `then` block receives the positive fact. The `else` block receives the complementary negative fact.

For `Option<T>`, `Some`/`None` use the dedicated `IsNotNone`/`IsNone` facts so AHFL's existing optional narrowing behavior remains consistent.

### `match`

For a top-level arm pattern that names a concrete variant, the arm body receives the matching positive fact for the scrutinee path. Wildcards, catch-all bindings, and or-patterns do not produce a path fact because they do not prove one unique variant for the entire arm.

Pattern payload bindings continue to be handled by the match pattern checker. RFC 0001 owns tuple and struct variant payload shape; RFC 0003 owns exhaustive diagnostics.

### Predicate Methods

The condition fact extractor recognizes these zero-argument method calls:

| Method | True branch | False branch |
| --- | --- | --- |
| `x.is_some()` | `x: IsNotNone` | `x: IsNone` |
| `x.is_none()` | `x: IsNone` | `x: IsNotNone` |
| `r.is_ok()` | `r: IsVariant(Result, Ok)` | `r: IsNotVariant(Result, Ok)` |
| `r.is_err()` | `r: IsVariant(Result, Err)` | `r: IsNotVariant(Result, Err)` |

The extractor is intentionally syntactic and conservative. If the receiver is not a narrowable path, no fact is produced. If the method name appears on an unrelated type, later expression resolution ignores the fact because it cannot match the receiver type.

## User Impact

Users can bind payloads directly:

```ahfl
if let Some(code) = input.coupon {
    return apply_coupon(code);
} else {
    return checkout_without_coupon();
}
```

Users can also rely on path narrowing for existing boolean-style code:

```ahfl
if order.id.is_some() {
    return submit(order.id);
}
```

For user-defined enums, `match` and `if let` provide the stable narrowing form:

```ahfl
enum Priority { Low, High }

fn keep(level: Priority) -> Priority {
    return match level {
        High => level,
        Low => Priority::Low,
    };
}
```

Inside the `High` arm, `level` has the local singleton type `Priority::High`.

## Compatibility and Migration

This RFC intentionally does not preserve a previous `if let` parse-only mode. The repository is not maintaining compatibility modes for immature language features.

The change is source-compatible for existing programs that do not use `if let`. Existing optional narrowing based on `x != Option::None` remains in place. Programs that rely on a matched scrutinee retaining its wider enum type inside a branch should bind a separate name before matching or return through an expected enum type where `EnumVariantT <: EnumT` applies.

## Implementation Plan

Implemented components:

1. Parser, AST, AST validation, formatter, AST printer, semantic tokens, and hover tree traversal already support `if let`.
2. `TypeCheckPass::check_statement` now validates `if let`, introduces payload bindings, and propagates then/else facts.
3. `condition_facts.cpp` extracts method predicate facts for `Option` and `Result`.
4. `ExpressionSema::apply_expression_flow_narrowing` preserves generic type arguments when producing `EnumVariantT`.
5. `ExpressionChecker::visit_match` adds arm-local scrutinee facts for concrete top-level variant arms.
6. Assignment invalidation continues to run through `FlowFacts::invalidate`.

No separate `TernaryNarrowingEnv` or Typed HIR annotation field was added. The existing `FlowFacts` structure is the correct AHFL implementation boundary because it already models path-local positive and negative facts, is scoped through `ValueContext`, and avoids adding dead metadata to Typed HIR.

## Test Plan

Regression coverage is in `tests/unit/compiler/semantics/flow_condition.cpp`:

1. Existing optional narrowing through `x != Option::None`.
2. Assignment invalidation.
3. `if let Some(value)` payload binding with substituted `String` type.
4. `if let Some(value)` narrowing of the original scrutinee path in the `then` branch.
5. `if let None` complementary else narrowing.
6. `Option::is_some()` and `Option::is_none()` predicate narrowing.
7. `Result::is_ok()` and `Result::is_err()` narrowing while preserving generic arguments.
8. `match` arm narrowing to a concrete enum variant.
9. Unknown `if let` variants.
10. Tuple payload arity mismatch.
11. Non-enum scrutinees, payload shape mismatches, duplicate bindings, and non-pure scrutinees.

Adjacent suites verify the shared infrastructure:

- `tests/unit/compiler/syntax/frontend/if_let_syntax.cpp`
- `tests/unit/compiler/semantics/adt_match.cpp`
- `tests/unit/compiler/semantics/typed_hir.cpp`
- `tests/unit/compiler/ir/opt_ir.cpp`
- `tests/unit/runtime/evaluator/executor.cpp`
- `tests/unit/runtime/engine/if_let_e2e.cpp`

## Rollout and Stabilization

The feature is enabled by default as part of the stable language surface. There is no feature flag and no parse-only compatibility path.

Stabilization requirements satisfied:

1. Language grammar and static semantics are documented in `docs/spec/core-language.zh.md`.
2. RFC registry status and index are updated.
3. Focused semantic regression tests pass.
4. Syntax, match, Typed HIR/IR, opt lowering, and runtime executor regression suites pass after the implementation change.
5. WorkflowRuntime E2E validates `if let Some(value)` then-branch payload binding and `None` else-branch execution from source fixture through IR lowering.

## Alternatives

1. Only support `match`.
   This rejects a common ergonomic pattern and forces verbose single-arm matches.

2. Only support predicate methods such as `is_some`.
   This cannot bind payloads and keeps users near manual extraction patterns.

3. Add full path-sensitive control-flow analysis.
   This is more powerful but conflicts with the current checker architecture and is unnecessary for the targeted enum-variant use cases.

4. Add a new Typed HIR witness annotation.
   This was rejected because no downstream pass needs a persistent annotation today. The local type view is computed at expression-check time from `FlowFacts`.

## Open Questions

No blocking open questions remain for RFC 0002.

Future RFC candidates:

1. Metadata for custom predicate methods such as `is_active`.
2. Pattern-guard narrowing in `match` arms.
3. A first-class `let else` statement if AHFL adopts early-exit ergonomics.

## Decision History

- 2026-06-25: Initial draft created during narrowing design exploration.
- 2026-07-02: Canonicalized as RFC 0002.
- 2026-07-06: Stabilized around `FlowFacts`, `if let`, match-arm facts, and built-in Option/Result predicate narrowing.
