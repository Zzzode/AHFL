---
rfc: "0001"
title: "Enum Variant Payload Forms"
status: "stabilized"
area: ["language", "compiler"]
stability: "stable-language"
created: "2026-06-28"
updated: "2026-07-06"
authors: ["LLM-orchestrated"]
shepherd: "AHFL language steering"
owners:
  language: "AHFL language owners"
  compiler: "AHFL compiler owners"
required_reviewers: ["language", "compiler"]
tracking_issue: "../plans/phaseb-gap-analysis.zh.md#3d-enum-variant-named-fields"
discussion: "0001-enum-variant-payload.zh.md"
implementation_prs:
  - "0d678ab7"
  - "b5baa1ac"
  - "ec18642a"
  - "c6c4a23c"
  - "6288ef29"
decision_due: "2026-07-12"
---

# RFC 0001: Enum Variant Payload Forms

## Summary

AHFL enum variants have three explicit payload shapes: unit, tuple, and struct. Parser, AST, resolver, type checker, Typed HIR, IR lowering, runtime, formatter, LSP, diagnostics, and reference docs must preserve that shape as a first-class semantic property.

## Motivation

Enum payload shape used to be inferred from ad hoc payload vectors. That made `Name`, `Name(T)`, and future named-field forms too easy to conflate across parsing, pattern matching, diagnostics, and lowering.

AHFL needs the same clear ADT model used by modern compiler implementations:

- unit variant: no payload
- tuple variant: positional payload, for example `Some(T)`
- struct variant: named payload, for example `Ok { value: T }`

The distinction is user-visible because patterns and constructors must use the declaration shape exactly.

## Goals

1. Make enum variant payload shape explicit in grammar and AST.
2. Support struct-form enum variants with named fields and optional field defaults.
3. Support struct-form variant patterns with shorthand fields, explicit `field: pattern` entries, and `..`.
4. Keep tuple variants, including single-field tuple variants, as normal tuple variants.
5. Diagnose tuple/struct/unit shape mismatches with source ranges and declaration related notes.
6. Preserve payload shape through Typed HIR serialization, IR lowering, runtime evaluation, formatter, and LSP analysis.

## Non-Goals

1. This RFC does not introduce anonymous struct literals.
2. This RFC does not change enum-level generics.
3. This RFC does not define match exhaustiveness policy; RFC 0003 owns that topic.
4. This RFC does not add compatibility flags, staged legacy modes, or automated compatibility fixers.
5. This RFC does not change enum memory layout or FFI representation.

## Design

### Grammar

The enum declaration grammar accepts one explicit shape per variant:

```antlr
enumDecl:
    'enum' identifier typeParams? '{' enumVariant (',' enumVariant)* ','? '}';

enumVariant
    : IDENT '{' variantFieldList '}' # structEnumVariant
    | IDENT '(' typeList ')'         # tupleEnumVariant
    | IDENT                          # unitEnumVariant
    ;

variantFieldDecl: IDENT ':' type_ ('=' constExpr)?;
```

Variant patterns mirror those shapes:

```antlr
variantPattern:
    qualifiedVariantName ('(' patternList ')' | '{' patternFieldList? '}')?
    | IDENT '(' patternList ')'
    | IDENT '{' patternFieldList? '}';

patternFieldList: patternField (',' patternField)* ','?;

patternField:
    IDENT ':' pattern
    | IDENT
    | '..';
```

Bare short unit variants continue to parse through binding-pattern syntax and are disambiguated semantically in enum scrutinee contexts.

### AST

`EnumVariantDeclSyntax` carries an `EnumVariantPayloadKind` discriminant:

- `Unit`: both payload vectors are empty.
- `Tuple`: positional payload vector is non-empty and named-field vector is empty.
- `Struct`: named-field vector is non-empty and positional payload vector is empty.

Struct variant fields carry:

- field name
- field type
- optional const default expression
- source range

The AST validator rejects impossible shape combinations before semantic passes consume the tree.

### Static Semantics

Variant shape is part of semantic identity:

- A tuple variant pattern may only destructure a tuple variant.
- A struct variant pattern may only destructure a struct variant.
- A unit variant pattern may only match a unit variant.
- Tuple and struct variants are not interchangeable even when their field types are isomorphic.

Struct variant pattern fields follow these rules:

- Every named pattern field must exist in the variant declaration.
- Duplicate pattern fields are rejected.
- If `..` is absent, every declared field must be covered.
- `field` is shorthand for `field: field`.
- `field: pattern` type-checks the nested pattern against the declared field type.
- Field defaults only apply during construction, not during destructuring.

Constructors mirror declaration shape:

- `Enum::A`
- `Enum::B(x, y)`
- `Enum::C { x: 1, y: 2 }`

For struct variant constructors, fields may appear in any order. A field may be omitted only when the declaration provides a default expression. Required missing fields produce `typecheck.MISSING_VARIANT_FIELD_IN_CONSTRUCTOR`.

The resolver rejects variant names that collide with same-module nominal type names by emitting `resolve.VARIANT_NAME_SHADOWS_TYPE`.

### Diagnostics

The implementation uses stable diagnostic codes for the user-visible failure modes:

- `typecheck.INVALID_ENUM_VARIANT_SHAPE`
- `typecheck.MISSING_VARIANT_FIELD`
- `typecheck.UNEXPECTED_VARIANT_FIELD`
- `typecheck.MISSING_VARIANT_FIELD_IN_CONSTRUCTOR`
- `typecheck.DUPLICATE_VARIANT_FIELD`
- `resolve.VARIANT_NAME_SHADOWS_TYPE`

Shape mismatch diagnostics attach a related note pointing at the variant declaration.

## User Impact

Users can model self-documenting ADTs directly:

```ahfl
enum HttpResponse {
    Success { status: Int, body: String },
    Redirect(Int, String),
    Failure { code: Int, detail: String },
}
```

Patterns become explicit and readable:

```ahfl
match response {
    Success { status, .. } => status,
    Redirect(code, _) => code,
    Failure { code, detail: _ } => code,
}
```

Existing tuple variants such as `Some(T)`, `Ok(T)`, and `Err(E)` remain valid tuple variants.

## Compatibility and Migration

AHFL is still immature, and the repository policy explicitly rejects legacy compatibility paths. This RFC is therefore implemented as the direct language contract:

- no feature flag
- no staged warning mode
- no `--compat` namespace mode
- no `ahflc fix` compatibility command

`Name(T)` remains legal and means a tuple variant of arity one. Users who want a named payload must write `Name { field: T }`. The compiler does not warn on single-field tuple variants because they are a normal part of the language.

The intentional breaking behavior is `resolve.VARIANT_NAME_SHADOWS_TYPE`: a variant name may not collide with a same-module nominal type name. The migration is to rename either the type or the variant so the module-level API surface remains unambiguous.

## Implementation Plan

The implementation is complete across the compiler pipeline:

1. Grammar and frontend parse unit, tuple, and struct enum variants.
2. AST nodes carry `EnumVariantPayloadKind` plus tuple or named-field payload storage.
3. Resolver rejects same-module type/variant name collisions.
4. Type checker enforces shape, field existence, field coverage, field type, duplicate field, and constructor missing-field rules.
5. Const-sema validates struct variant field defaults.
6. Typed HIR serialization records variant kind, payload fields, defaults, and declaration ranges.
7. IR lowering materializes defaulted struct-variant fields during construction.
8. Runtime evaluation preserves named payload values.
9. Formatter round-trips struct variants and struct variant patterns.
10. LSP semantic analysis visits variant fields and default expressions.
11. Spec and error-code reference docs describe the stable contract.

## Test Plan

The feature is covered by:

- syntax tests for unit, tuple, and struct variants
- semantic tests for shape mismatch, missing fields, unexpected fields, duplicate fields, defaults, constructor validation, and related declaration notes
- runtime tests for default materialization
- WorkflowRuntime E2E coverage for struct-form enum variant construction, default materialization, and struct-pattern destructuring
- Typed HIR serialization round-trip tests
- formatter round-trip coverage through the existing formatter suite
- full repository `ctest --preset test-dev --output-on-failure`

The current implementation intentionally does not include tests for legacy compatibility flags, staged deprecation warnings, or automated fixers because those features are outside the accepted AHFL policy.

## Rollout and Stabilization

The feature is stabilized without feature gating. Ongoing stabilization requires keeping the following evidence green:

1. `python3 scripts/check-rfc.py`
2. `cmake --build --preset build-dev`
3. `ctest --preset test-dev --output-on-failure`
4. RFC, spec, and reference docs remain synchronized when diagnostic wording or payload semantics change.

Stabilization evidence for the 2026-07-06 status transition:

- `python3 scripts/check-rfc.py`
- `cmake --build --preset build-dev`
- `ctest --preset test-dev --output-on-failure`
- `ctest --preset test-dev --output-on-failure -R '^ahfl\.runtime\.enum_variant_e2e$'`
- RFC 0001 diagnostic-code contract was reconciled so struct-variant declaration, pattern, and constructor duplicate fields all use `typecheck.DUPLICATE_VARIANT_FIELD`, while ordinary struct literals retain `typecheck.DUPLICATE_FIELD`.

## Alternatives

1. Keep tuple-only variants and require named structs outside enums. Rejected because it weakens ADT modeling and keeps positional payloads unreadable.
2. Treat struct variants as sugar over tuple variants. Rejected because it erases shape identity and produces weaker diagnostics.
3. Ban single-field tuple variants. Rejected because tuple arity one is semantically valid and useful for `Option` and `Result` style APIs.
4. Add staged compatibility flags and fixers. Rejected because AHFL is immature and repository policy requires direct best-practice cleanup instead of legacy paths.

## Open Questions

There are no open design questions for RFC 0001. Future changes to exhaustiveness, visibility, or enum-level generics must go through their own RFCs.

## Decision History

- 2026-06-28: Initial draft created for enum variant payload shape.
- 2026-07-02: RFC assigned canonical ID 0001.
- 2026-07-06: RFC updated to implemented status, aligned with the repository no-legacy policy, and synchronized with the landed compiler behavior.
- 2026-07-06: RFC stabilized after the duplicate-field diagnostic contract was made consistent across enum struct-variant declarations, patterns, and constructors.
- 2026-07-06: Added WorkflowRuntime E2E coverage for struct-form enum variant construction, default field materialization, and struct-pattern destructuring.
